param(
    [string]$QtDir = $env:Qt6_DIR,
    [switch]$SkipToolDownloads
)

$ErrorActionPreference = "Stop"
$PSNativeCommandUseErrorActionPreference = $true
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root
$userAgent = "VideoDownloaderPro/4.0 (+https://github.com/Jacksony100/Youtube-Downloader)"
$toolchain = Join-Path $root "build_assets\toolchain"

function Download-Checked([string]$Url, [string]$Output) {
    Write-Host "[INFO] Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $Output -Headers @{"User-Agent" = $userAgent} -TimeoutSec 240
    if (-not (Test-Path -LiteralPath $Output) -or (Get-Item -LiteralPath $Output).Length -eq 0) {
        throw "Downloaded file is missing or empty: $Output"
    }
}

function Get-PublishedHash([string]$ChecksumFile, [string]$FileName) {
    $text = Get-Content -LiteralPath $ChecksumFile -Raw
    $powerShellHash = [regex]::Match($text, '(?im)^Hash\s*:\s*([0-9a-f]{64})\s*$')
    if ($powerShellHash.Success) { return $powerShellHash.Groups[1].Value }
    foreach ($line in $text -split "`r?`n") {
        if ($line -match '^([0-9a-fA-F]{64})\s+\*?(.+)$' -and $Matches[2].EndsWith($FileName)) { return $Matches[1] }
    }
    throw "Checksum not found for $FileName"
}

function Assert-Checksum([string]$File, [string]$ChecksumUrl, [string]$FileName) {
    $checksumFile = "$File.sha256sum"
    Download-Checked $ChecksumUrl $checksumFile
    $expected = Get-PublishedHash $checksumFile $FileName
    $actual = (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash
    if ($actual -ne $expected) { throw "SHA256 mismatch for $FileName" }
    Write-Host "[OK] SHA256 verified: $FileName"
}

function Prepare-Toolchain {
    New-Item -ItemType Directory -Force $toolchain | Out-Null
    $ytdlp = Join-Path $toolchain "yt-dlp.exe"
    $deno = Join-Path $toolchain "deno.exe"
    $ffmpeg = Join-Path $toolchain "ffmpeg.exe"
    $ffprobe = Join-Path $toolchain "ffprobe.exe"

    if (-not $SkipToolDownloads -or -not (Test-Path -LiteralPath $ytdlp)) {
        $sums = Join-Path $root "build_assets\SHA2-256SUMS"
        Download-Checked "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" $ytdlp
        Download-Checked "https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS" $sums
        $expected = Get-PublishedHash $sums "yt-dlp.exe"
        if ((Get-FileHash -LiteralPath $ytdlp -Algorithm SHA256).Hash -ne $expected) { throw "yt-dlp checksum mismatch" }
    }

    if (-not $SkipToolDownloads -or -not (Test-Path -LiteralPath $deno)) {
        $denoName = "deno-x86_64-pc-windows-msvc.zip"
        $denoUrl = "https://github.com/denoland/deno/releases/latest/download/$denoName"
        $archive = Join-Path $root "build_assets\$denoName"
        $extract = Join-Path $root "build_assets\deno_extract"
        Download-Checked $denoUrl $archive
        Assert-Checksum $archive "$denoUrl.sha256sum" $denoName
        if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
        Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
        Copy-Item -LiteralPath (Join-Path $extract "deno.exe") -Destination $deno -Force
    }

    if (-not $SkipToolDownloads -or -not ((Test-Path -LiteralPath $ffmpeg) -and (Test-Path -LiteralPath $ffprobe))) {
        $archive = Join-Path $root "build_assets\ffmpeg-release-essentials.zip"
        $extract = Join-Path $root "build_assets\ffmpeg_extract"
        Download-Checked "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" $archive
        if (Test-Path -LiteralPath $extract) { Remove-Item -LiteralPath $extract -Recurse -Force }
        Expand-Archive -LiteralPath $archive -DestinationPath $extract -Force
        $ffmpegSource = Get-ChildItem -LiteralPath $extract -Recurse -Filter "ffmpeg.exe" | Select-Object -First 1
        $ffprobeSource = Get-ChildItem -LiteralPath $extract -Recurse -Filter "ffprobe.exe" | Select-Object -First 1
        if (-not $ffmpegSource -or -not $ffprobeSource) { throw "ffmpeg tools not found in archive" }
        Copy-Item -LiteralPath $ffmpegSource.FullName -Destination $ffmpeg -Force
        Copy-Item -LiteralPath $ffprobeSource.FullName -Destination $ffprobe -Force
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) { throw "CMake not found" }
if (-not (Get-Command ninja -ErrorAction SilentlyContinue)) { throw "Ninja not found" }
if (-not $QtDir) { throw "Qt6_DIR is not set" }
$qtBin = Resolve-Path (Join-Path $QtDir "..\..\..\bin")
$windeployqt = Join-Path $qtBin "windeployqt.exe"
if (-not (Test-Path -LiteralPath $windeployqt)) { throw "windeployqt not found: $windeployqt" }

Prepare-Toolchain
cmake -S . -B build-cpp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QtDir" -DBUILD_TESTING=ON
cmake --build build-cpp --config Release
ctest --test-dir build-cpp --output-on-failure

$package = Join-Path $root "dist\VideoDownloaderPro-win-x64"
if (Test-Path -LiteralPath $package) { Remove-Item -LiteralPath $package -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $package "toolchain") | Out-Null
Copy-Item -LiteralPath "build-cpp\VideoDownloaderPro.exe" -Destination $package
Copy-Item -Path "build_assets\toolchain\*" -Destination (Join-Path $package "toolchain") -Force
Copy-Item -LiteralPath "README.md", "CHANGELOG.md" -Destination $package
& $windeployqt --release --no-translations --compiler-runtime (Join-Path $package "VideoDownloaderPro.exe")

$smokeData = Join-Path $root "build-cpp\smoke-localappdata"
New-Item -ItemType Directory -Force $smokeData | Out-Null
$previousLocalAppData = $env:LOCALAPPDATA
$env:LOCALAPPDATA = $smokeData
$env:VDP_SMOKE_TEST = "1"
try {
    & (Join-Path $package "VideoDownloaderPro.exe")
} finally {
    $env:LOCALAPPDATA = $previousLocalAppData
    Remove-Item Env:\VDP_SMOKE_TEST -ErrorAction SilentlyContinue
}
$managedDeno = Join-Path $smokeData "VideoDownloaderPro\runtime\deno\deno.exe"
if (-not (Test-Path -LiteralPath $managedDeno)) { throw "Packaged app smoke test did not provision Deno" }
Write-Host "[OK] Packaged executable smoke test passed"

$zip = Join-Path $root "dist\VideoDownloaderPro-win-x64.zip"
if (Test-Path -LiteralPath $zip) { Remove-Item -LiteralPath $zip -Force }
Compress-Archive -Path (Join-Path $package "*") -DestinationPath $zip -Force
Write-Host "[OK] Native C++ package: $zip"
