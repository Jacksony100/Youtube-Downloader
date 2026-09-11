param(
    [string]$QtDir = $env:Qt6_DIR,
    [string]$InnoSetupCompiler = $env:ISCC_PATH,
    [string]$BuildDir = 'build-cpp/pro5-package',
    [string]$Python = 'python',
    [switch]$SkipToolDownloads
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location -LiteralPath $root
function Run-Checked([string]$Program, [string[]]$Arguments) {
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Program failed with exit code $LASTEXITCODE" }
}
function Workspace-Child([string]$Parent, [string]$Path) {
    $parentPath = [IO.Path]::GetFullPath($Parent).TrimEnd('\','/')
    $childPath = [IO.Path]::GetFullPath($Path)
    if (-not $childPath.StartsWith($parentPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) { throw "Path escapes workspace: $childPath" }
    return $childPath
}
foreach ($command in @('cmake','ninja','ctest',$Python)) {
    if (-not (Get-Command $command -ErrorAction SilentlyContinue)) { throw "Build dependency missing: $command" }
}
if (-not $QtDir) { throw 'Set Qt6_DIR or pass -QtDir (Qt installation root or lib/cmake/Qt6).' }
$qtRoot = (Resolve-Path -LiteralPath $QtDir).Path
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'bin/windeployqt.exe'))) { $qtRoot = [IO.Path]::GetFullPath((Join-Path $qtRoot '../../..')) }
$windeployqt = Join-Path $qtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $windeployqt)) { throw "windeployqt missing: $windeployqt" }
$env:PATH = (Join-Path $qtRoot 'bin') + ';' + $env:PATH
if (-not $InnoSetupCompiler) {
    $iscc = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($iscc) { $InnoSetupCompiler = $iscc.Source }
    elseif (Test-Path -LiteralPath "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe") { $InnoSetupCompiler = "${env:ProgramFiles(x86)}/Inno Setup 6/ISCC.exe" }
}
if (-not $InnoSetupCompiler -or -not (Test-Path -LiteralPath $InnoSetupCompiler)) { throw 'Inno Setup compiler missing. Pass -InnoSetupCompiler or set ISCC_PATH.' }
$version = (& $Python scripts/release_tools.py version).Trim()
if ($LASTEXITCODE -ne 0 -or $version -notmatch '^\d+\.\d+\.\d+$') { throw 'Cannot read canonical CMake version' }
$build = Workspace-Child (Join-Path $root 'build-cpp') ([IO.Path]::GetFullPath((Join-Path $root $BuildDir)))
$toolchain = Join-Path $root 'build_assets/pro5-toolchain-windows-x64'
Run-Checked $Python @('-m','unittest','discover','-s','scripts','-p','test_release_tools.py','-v')
Run-Checked $Python @('scripts/release_tools.py','validate-lock')
$toolArgs = @('scripts/release_tools.py','prepare-tools','windows-x64',$toolchain)
if ($SkipToolDownloads) { $toolArgs += '--reuse-verified' }
Run-Checked $Python $toolArgs
Run-Checked 'cmake' @('-S',$root,'-B',$build,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_PREFIX_PATH=$qtRoot",'-DBUILD_TESTING=ON')
Run-Checked 'cmake' @('--build',$build,'--config','Release','--clean-first')
Run-Checked 'ctest' @('--test-dir',$build,'--output-on-failure')
$runId = [Guid]::NewGuid().ToString('N')
Run-Checked $Python @('scripts/release_tools.py','engine-smoke',$toolchain,(Join-Path $build 'engine_smoke_arguments.exe'),(Join-Path $build "engine-smoke-$runId"))
$staging = Join-Path $root "dist/staging-$runId"
$package = Join-Path $staging "VideoDownloaderPro-$version-win-x64"
New-Item -ItemType Directory -Path (Join-Path $package 'toolchain') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $build 'VideoDownloaderPro.exe') -Destination $package
Copy-Item -Path (Join-Path $toolchain '*') -Destination (Join-Path $package 'toolchain') -Recurse
Copy-Item -LiteralPath 'README.md','CHANGELOG.md','THIRD_PARTY_NOTICES.md' -Destination $package
Copy-Item -LiteralPath 'runtime/toolchain-lock.json' -Destination (Join-Path $package 'toolchain')
if (Test-Path -LiteralPath 'licenses') { Copy-Item -LiteralPath 'licenses' -Destination $package -Recurse }
if (Test-Path -LiteralPath (Join-Path $qtRoot 'sbom')) { Copy-Item -LiteralPath (Join-Path $qtRoot 'sbom') -Destination (Join-Path $package 'licenses/qt-sbom') -Recurse }
Run-Checked $windeployqt @('--release','--no-translations','--compiler-runtime',(Join-Path $package 'VideoDownloaderPro.exe'))
# windeployqt can copy only the redistributable installer for MSVC. App-local CRT
# DLLs make the portable package and per-user installer work without elevation.
if (Test-Path -LiteralPath (Join-Path $package 'vc_redist.x64.exe')) {
    if (-not $env:VCToolsRedistDir) { throw 'MSVC packaging requires VCToolsRedistDir; run in an x64 Visual Studio developer environment.' }
    $crtDirectories = @(Get-ChildItem -LiteralPath (Join-Path $env:VCToolsRedistDir 'x64') -Directory -Filter 'Microsoft.VC*.CRT')
    if ($crtDirectories.Count -ne 1) { throw 'Cannot select the exact x64 MSVC CRT redistribution directory.' }
    Copy-Item -Path (Join-Path $crtDirectories[0].FullName '*.dll') -Destination $package
    foreach ($crtDll in @('msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $package $crtDll))) { throw "Missing app-local CRT: $crtDll" }
    }
}

# Signing material is supplied by the caller; passwords are never printed.
function Sign-Artifact([string]$File) {
    if (-not $env:VDP_SIGN_CERT_PATH) { return }
    if (-not $env:VDP_SIGN_CERT_PASSWORD) { throw 'Signing certificate password is missing' }
    $signtool = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if (-not $signtool) { throw 'signtool is required when signing is configured' }
    & $signtool.Source sign /fd SHA256 /tr https://timestamp.digicert.com /td SHA256 /f $env:VDP_SIGN_CERT_PATH /p $env:VDP_SIGN_CERT_PASSWORD $File
    if ($LASTEXITCODE -ne 0) { throw 'Authenticode signing failed' }
    if ((Get-AuthenticodeSignature -LiteralPath $File).Status -ne 'Valid') { throw 'Authenticode validation failed' }
}
Sign-Artifact (Join-Path $package 'VideoDownloaderPro.exe')
Run-Checked $Python @('scripts/release_tools.py','smoke',(Join-Path $package 'VideoDownloaderPro.exe'),(Join-Path $build "portable-smoke-$runId"))
$zip = Join-Path $root "dist/VideoDownloaderPro-$version-win-x64.zip"
Compress-Archive -Path (Join-Path $package '*') -DestinationPath (Join-Path $staging 'portable.zip') -CompressionLevel Optimal
Copy-Item -LiteralPath (Join-Path $staging 'portable.zip') -Destination $zip -Force
Run-Checked $InnoSetupCompiler @("/DAppVersion=$version","/DPackageDir=$package","/DOutputDir=$staging",(Join-Path $root 'installer/VideoDownloaderPro.iss'))
$installer = Join-Path $root "dist/VideoDownloaderPro-Setup-$version.exe"
Copy-Item -LiteralPath (Join-Path $staging "VideoDownloaderPro-Setup-$version.exe") -Destination $installer -Force
Sign-Artifact $installer

# Smoke mode avoids user registration and shortcuts and uses a fresh directory.
$installTarget = Join-Path $build "installer-smoke-$runId/app"
New-Item -ItemType Directory -Path $installTarget -Force | Out-Null
$setupProcess = Start-Process -FilePath $installer -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/SP-','/SMOKETEST=1',("/DIR=`"$installTarget`"") -WindowStyle Hidden -PassThru
if (-not $setupProcess.WaitForExit(120000)) { $setupProcess.Kill(); throw 'Installer smoke timed out after 120 seconds' }
$setupProcess.Refresh()
if ($setupProcess.ExitCode -ne 0) { throw "Installer failed: $($setupProcess.ExitCode)" }
Run-Checked $Python @('scripts/release_tools.py','smoke',(Join-Path $installTarget 'VideoDownloaderPro.exe'),(Join-Path $build "installed-smoke-$runId"))
$sums = & $Python scripts/release_tools.py checksums $zip $installer
if ($LASTEXITCODE -ne 0) { throw 'Distributable checksum generation failed' }
[IO.File]::WriteAllLines((Join-Path $root 'dist/SHA256SUMS-windows.txt'), [string[]]$sums, [Text.UTF8Encoding]::new($false))
Write-Host "[OK] Windows package and installer passed smoke: $version"
