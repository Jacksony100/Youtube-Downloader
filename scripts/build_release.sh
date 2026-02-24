#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

if [[ ! -d ".venv" ]]; then
  echo "[ERR] .venv not found in $ROOT_DIR"
  exit 1
fi

source .venv/bin/activate

if ! command -v iconutil >/dev/null 2>&1; then
  echo "[ERR] iconutil not found (required on macOS)."
  exit 1
fi

mkdir -p build_assets/ffmpeg build_assets/icon.iconset dist

# 1) Bundle native ffmpeg (arm64 on Apple Silicon)
python - <<'PY'
from pathlib import Path
import imageio_ffmpeg

src = Path(imageio_ffmpeg.get_ffmpeg_exe())
dst = Path('build_assets/ffmpeg/ffmpeg')
dst.write_bytes(src.read_bytes())
dst.chmod(0o755)
print(f"[OK] ffmpeg bundled: {dst}")
PY

# 2) Build macOS icon (.icns) from icon.ico
python - <<'PY'
from pathlib import Path
from PIL import Image

src = Path('icon.ico')
out = Path('build_assets/icon.iconset')
out.mkdir(parents=True, exist_ok=True)
img = Image.open(src).convert('RGBA')

mapping = {
    'icon_16x16.png': 16,
    'icon_16x16@2x.png': 32,
    'icon_32x32.png': 32,
    'icon_32x32@2x.png': 64,
    'icon_128x128.png': 128,
    'icon_128x128@2x.png': 256,
    'icon_256x256.png': 256,
    'icon_256x256@2x.png': 512,
    'icon_512x512.png': 512,
    'icon_512x512@2x.png': 1024,
}
for name, size in mapping.items():
    img.resize((size, size), Image.Resampling.LANCZOS).save(out / name, format='PNG')
print('[OK] iconset generated')
PY

iconutil -c icns build_assets/icon.iconset -o build_assets/icon.icns

# 3) Build app bundle (PyInstaller stores Python bytecode inside bootloader archive)
echo "[INFO] Building app bundle with PyInstaller..."
pyinstaller \
  --noconfirm \
  --clean \
  --onedir \
  --windowed \
  --name VideoDownloaderPro \
  --icon build_assets/icon.icns \
  --add-data "icon.ico:." \
  --add-binary "build_assets/ffmpeg/ffmpeg:." \
  main.py

APP_PATH="dist/VideoDownloaderPro.app"
if [[ ! -d "$APP_PATH" ]]; then
  echo "[ERR] Build finished but $APP_PATH not found"
  exit 1
fi

# 4) Normalize bundle metadata and re-sign to avoid "app is damaged" dialog
xattr -cr "$APP_PATH"
codesign --force --deep --sign - "$APP_PATH"
codesign --verify --deep --strict --verbose=2 "$APP_PATH" >/dev/null

# 5) Zip final .app bundle for transfer
OUT_ZIP="dist/VideoDownloaderPro-macOS.zip"
rm -f "$OUT_ZIP"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$OUT_ZIP"

echo "[OK] App bundle: $APP_PATH"
echo "[OK] Zip package: $OUT_ZIP"
