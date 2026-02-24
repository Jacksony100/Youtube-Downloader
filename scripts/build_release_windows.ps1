param(
    [switch]$UseNuitka
)

$ErrorActionPreference = "Stop"
$root = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $root

if (-not (Test-Path ".venv")) {
    python -m venv .venv
}

$py = ".\\.venv\\Scripts\\python.exe"
$pyinstaller = ".\\.venv\\Scripts\\pyinstaller.exe"

& $py -m pip install --upgrade pip
& $py -m pip install -r requirements.txt pyinstaller imageio-ffmpeg

New-Item -ItemType Directory -Force "build_assets\\ffmpeg" | Out-Null
New-Item -ItemType Directory -Force "dist" | Out-Null

$ffmpegPath = & $py -c "import imageio_ffmpeg; print(imageio_ffmpeg.get_ffmpeg_exe())"
Copy-Item $ffmpegPath "build_assets\\ffmpeg\\ffmpeg.exe" -Force

if ($UseNuitka) {
    & $py -m pip install nuitka ordered-set zstandard

    & $py -m nuitka main.py `
        --standalone `
        --enable-plugin=pyside6 `
        --windows-console-mode=disable `
        --windows-icon-from-ico=icon.ico `
        --output-dir=dist_win `
        --output-filename=VideoDownloaderPro.exe `
        --include-data-files="icon.ico=icon.ico" `
        --include-data-files="build_assets/ffmpeg/ffmpeg.exe=ffmpeg.exe" `
        --remove-output

    $distFolder = Get-ChildItem -Directory dist_win | Where-Object { $_.Name -like "*.dist" } | Select-Object -First 1
    if (-not $distFolder) {
        throw "Nuitka build folder (*.dist) not found in dist_win"
    }

    $zipPath = "dist\\VideoDownloaderPro-win-x64-obf.zip"
    if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
    Compress-Archive -Path (Join-Path $distFolder.FullName "*") -DestinationPath $zipPath -Force

    Write-Host "[OK] Obfuscated build ready: $zipPath"
    exit 0
}

& $pyinstaller `
    --noconfirm `
    --clean `
    --onedir `
    --windowed `
    --name VideoDownloaderPro `
    --icon icon.ico `
    --add-data "icon.ico;." `
    --add-binary "build_assets\\ffmpeg\\ffmpeg.exe;." `
    main.py

if (-not (Test-Path "dist\\VideoDownloaderPro\\ffmpeg.exe")) {
    throw "ffmpeg.exe was not bundled into dist\\VideoDownloaderPro"
}

$zipPath = "dist\\VideoDownloaderPro-win-x64.zip"
if (Test-Path $zipPath) { Remove-Item $zipPath -Force }
Compress-Archive -Path "dist\\VideoDownloaderPro\\*" -DestinationPath $zipPath -Force

Write-Host "[OK] Windows x64 build ready: $zipPath"
