param(
    [switch]$UseNuitka,
    [switch]$SkipToolDownloads
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

$userAgent = "VideoDownloaderPro/3.1 (+https://github.com/Jacksony100/Youtube-Downloader)"
$toolchainDir = "build_assets\toolchain"
$ffmpegExtractDir = "build_assets\ffmpeg_extract"
$ffmpegZip = "build_assets\ffmpeg-release-essentials.zip"

function Download-FileChecked {
    param(
        [string]$Url,
        [string]$OutFile
    )
    Write-Host "[INFO] Downloading $Url"
    Invoke-WebRequest -Uri $Url -OutFile $OutFile -Headers @{ "User-Agent" = $userAgent } -TimeoutSec 180
    if (-not (Test-Path $OutFile)) {
        throw "Download failed: $OutFile"
    }
    if ((Get-Item $OutFile).Length -le 0) {
        throw "Downloaded file is empty: $OutFile"
    }
}

function Verify-YtDlpChecksum {
    param(
        [string]$FilePath
    )
    $sumsPath = "build_assets\SHA2-256SUMS"
    Download-FileChecked "https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS" $sumsPath
    $expected = $null
    foreach ($line in Get-Content $sumsPath) {
        $parts = $line.Trim() -split "\s+"
        if ($parts.Count -ge 2 -and $parts[-1].Replace("*", "").EndsWith("yt-dlp.exe")) {
            $expected = $parts[0]
            break
        }
    }
    if (-not $expected) {
        throw "Unable to find yt-dlp.exe checksum in SHA2-256SUMS"
    }
    $actual = (Get-FileHash $FilePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected.ToLowerInvariant()) {
        throw "yt-dlp.exe checksum mismatch"
    }
    Write-Host "[OK] yt-dlp.exe checksum verified"
}

function Prepare-Toolchain {
    New-Item -ItemType Directory -Force $toolchainDir | Out-Null

    $ytDlpPath = Join-Path $toolchainDir "yt-dlp.exe"
    $ffmpegPath = Join-Path $toolchainDir "ffmpeg.exe"
    $ffprobePath = Join-Path $toolchainDir "ffprobe.exe"

    if (-not $SkipToolDownloads -or -not (Test-Path $ytDlpPath)) {
        Download-FileChecked "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp.exe" $ytDlpPath
        Verify-YtDlpChecksum $ytDlpPath
    }

    if (-not $SkipToolDownloads -or -not ((Test-Path $ffmpegPath) -and (Test-Path $ffprobePath))) {
        if (Test-Path $ffmpegExtractDir) {
            Remove-Item $ffmpegExtractDir -Recurse -Force
        }
        Download-FileChecked "https://www.gyan.dev/ffmpeg/builds/ffmpeg-release-essentials.zip" $ffmpegZip
        Expand-Archive -Path $ffmpegZip -DestinationPath $ffmpegExtractDir -Force

        $ffmpegSource = Get-ChildItem $ffmpegExtractDir -Recurse -Filter "ffmpeg.exe" |
            Where-Object { $_.FullName -match "\\bin\\ffmpeg\.exe$" } |
            Select-Object -First 1
        $ffprobeSource = Get-ChildItem $ffmpegExtractDir -Recurse -Filter "ffprobe.exe" |
            Where-Object { $_.FullName -match "\\bin\\ffprobe\.exe$" } |
            Select-Object -First 1

        if (-not $ffmpegSource -or -not $ffprobeSource) {
            throw "ffmpeg.exe or ffprobe.exe not found inside downloaded archive"
        }

        Copy-Item $ffmpegSource.FullName $ffmpegPath -Force
        Copy-Item $ffprobeSource.FullName $ffprobePath -Force
        Remove-Item $ffmpegExtractDir -Recurse -Force
        Write-Host "[OK] ffmpeg fallback prepared"
    }

    foreach ($path in @($ytDlpPath, $ffmpegPath, $ffprobePath)) {
        if (-not (Test-Path $path)) {
            throw "Fallback tool missing: $path"
        }
    }
}

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

$py = ".\.venv\Scripts\python.exe"
$pyinstaller = ".\.venv\Scripts\pyinstaller.exe"

& $py -m pip install --upgrade pip
& $py -m pip install -r requirements.txt pyinstaller

New-Item -ItemType Directory -Force "build_assets" | Out-Null
New-Item -ItemType Directory -Force "dist" | Out-Null
Prepare-Toolchain

if ($UseNuitka) {
    & $py -m pip install nuitka ordered-set zstandard

    & $py -m nuitka app/main.py `
        --standalone `
        --enable-plugin=pyside6 `
        --windows-console-mode=disable `
        --windows-icon-from-ico=icon.ico `
        --output-dir=dist_win `
        --output-filename=VideoDownloaderPro.exe `
        --include-data-files="icon.ico=icon.ico" `
        --include-data-files="ui/styles/dark.qss=ui/styles/dark.qss" `
        --include-data-files="build_assets/toolchain/yt-dlp.exe=toolchain/yt-dlp.exe" `
        --include-data-files="build_assets/toolchain/ffmpeg.exe=toolchain/ffmpeg.exe" `
        --include-data-files="build_assets/toolchain/ffprobe.exe=toolchain/ffprobe.exe" `
        --remove-output

    $distFolder = Get-ChildItem -Directory dist_win | Where-Object { $_.Name -like "*.dist" } | Select-Object -First 1
    if (-not $distFolder) {
        throw "Nuitka build folder (*.dist) not found in dist_win"
    }

    $zipPath = "dist\VideoDownloaderPro-win-x64-obf.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $distFolder.FullName "*"), "README.md", "CHANGELOG.md" -DestinationPath $zipPath -Force

    Write-Host "[OK] Obfuscated build ready: $zipPath"
    exit 0
}

& $pyinstaller `
    --noconfirm `
    --clean `
    --onefile `
    --windowed `
    --name VideoDownloaderPro `
    --icon icon.ico `
    --add-data "icon.ico;." `
    --add-data "ui\styles\dark.qss;ui\styles" `
    --add-binary "build_assets\toolchain\yt-dlp.exe;toolchain" `
    --add-binary "build_assets\toolchain\ffmpeg.exe;toolchain" `
    --add-binary "build_assets\toolchain\ffprobe.exe;toolchain" `
    app/main.py

$exePath = "dist\VideoDownloaderPro.exe"
if (-not (Test-Path $exePath)) {
    throw "Build finished but $exePath was not found"
}

$zipPath = "dist\VideoDownloaderPro-win-x64.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path $exePath, "README.md", "CHANGELOG.md" -DestinationPath $zipPath -Force

Write-Host "[OK] Windows onefile exe ready: $exePath"
Write-Host "[OK] Zip package ready: $zipPath"
