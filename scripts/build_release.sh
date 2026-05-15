#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

export COPYFILE_DISABLE=1

PYTHON_BIN="${PYTHON_BIN:-python3}"
USER_AGENT="VideoDownloaderPro/3.1 (+https://github.com/Jacksony100/Youtube-Downloader)"
TOOLCHAIN_DIR="build_assets/toolchain"

download_file() {
  local url="$1"
  local out="$2"
  echo "[INFO] Downloading $url"
  curl -L --fail --retry 3 --connect-timeout 20 -A "$USER_AGENT" "$url" -o "$out"
  if [[ ! -s "$out" ]]; then
    echo "[ERR] Download failed or file is empty: $out"
    exit 1
  fi
}

verify_ytdlp_checksum() {
  local file_path="$1"
  local expected
  download_file "https://github.com/yt-dlp/yt-dlp/releases/latest/download/SHA2-256SUMS" "build_assets/SHA2-256SUMS"
  expected="$(awk '$NF ~ /yt-dlp_macos$/ { print $1; exit }' build_assets/SHA2-256SUMS)"
  if [[ -z "$expected" ]]; then
    echo "[ERR] Unable to find yt-dlp_macos checksum"
    exit 1
  fi
  local actual
  actual="$(shasum -a 256 "$file_path" | awk '{print $1}')"
  if [[ "$actual" != "$expected" ]]; then
    echo "[ERR] yt-dlp_macos checksum mismatch"
    exit 1
  fi
  echo "[OK] yt-dlp_macos checksum verified"
}

extract_single_binary() {
  local zip_path="$1"
  local binary_name="$2"
  local out_path="$3"
  "$PY" - "$zip_path" "$binary_name" "$out_path" <<'PY'
import os
import sys
import zipfile
from pathlib import Path

zip_path, binary_name, out_path = sys.argv[1:4]
out = Path(out_path)
with zipfile.ZipFile(zip_path) as zf:
    candidates = [
        name for name in zf.namelist()
        if Path(name).name == binary_name and not name.endswith("/")
    ]
    if not candidates:
        raise SystemExit(f"{binary_name} not found in {zip_path}")
    with zf.open(candidates[0]) as src:
        out.write_bytes(src.read())
out.chmod(0o755)
print(f"[OK] {binary_name} extracted: {out}")
PY
}

prepare_toolchain() {
  mkdir -p "$TOOLCHAIN_DIR" build_assets

  local ytdlp_path="$TOOLCHAIN_DIR/yt-dlp"
  download_file "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos" "$ytdlp_path"
  verify_ytdlp_checksum "$ytdlp_path"
  chmod 755 "$ytdlp_path"

  local ffmpeg_zip="build_assets/ffmpeg-macos.zip"
  local ffprobe_zip="build_assets/ffprobe-macos.zip"
  download_file "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip" "$ffmpeg_zip"
  download_file "https://evermeet.cx/ffmpeg/getrelease/ffprobe/zip" "$ffprobe_zip"
  extract_single_binary "$ffmpeg_zip" "ffmpeg" "$TOOLCHAIN_DIR/ffmpeg"
  extract_single_binary "$ffprobe_zip" "ffprobe" "$TOOLCHAIN_DIR/ffprobe"
}

if [[ ! -d ".venv" ]]; then
  "$PYTHON_BIN" -m venv .venv
fi

source .venv/bin/activate
PY="$ROOT_DIR/.venv/bin/python"

if ! command -v iconutil >/dev/null 2>&1; then
  echo "[ERR] iconutil not found (required on macOS)."
  exit 1
fi

"$PY" -m pip install --upgrade pip
"$PY" -m pip install -r requirements.txt pyinstaller pillow

mkdir -p build_assets/icon.iconset dist
prepare_toolchain

# Build macOS icon (.icns) from icon.ico.
"$PY" - <<'PY'
from pathlib import Path
from PIL import Image

src = Path("icon.ico")
out = Path("build_assets/icon.iconset")
out.mkdir(parents=True, exist_ok=True)
img = Image.open(src).convert("RGBA")

mapping = {
    "icon_16x16.png": 16,
    "icon_16x16@2x.png": 32,
    "icon_32x32.png": 32,
    "icon_32x32@2x.png": 64,
    "icon_128x128.png": 128,
    "icon_128x128@2x.png": 256,
    "icon_256x256.png": 256,
    "icon_256x256@2x.png": 512,
    "icon_512x512.png": 512,
    "icon_512x512@2x.png": 1024,
}
for name, size in mapping.items():
    img.resize((size, size), Image.Resampling.LANCZOS).save(out / name, format="PNG")
print("[OK] iconset generated")
PY

iconutil -c icns build_assets/icon.iconset -o build_assets/icon.icns

echo "[INFO] Building app bundle with PyInstaller..."
pyinstaller \
  --noconfirm \
  --clean \
  --onedir \
  --windowed \
  --name VideoDownloaderPro \
  --icon build_assets/icon.icns \
  --add-data "icon.ico:." \
  --add-data "ui/styles/dark.qss:ui/styles" \
  --add-binary "build_assets/toolchain/yt-dlp:toolchain" \
  --add-binary "build_assets/toolchain/ffmpeg:toolchain" \
  --add-binary "build_assets/toolchain/ffprobe:toolchain" \
  app/main.py

APP_PATH="dist/VideoDownloaderPro.app"
if [[ ! -d "$APP_PATH" ]]; then
  echo "[ERR] Build finished but $APP_PATH not found"
  exit 1
fi

STAGING_DIR="$(mktemp -d)"
cleanup() {
  rm -rf "$STAGING_DIR"
}
trap cleanup EXIT

STAGED_APP="$STAGING_DIR/VideoDownloaderPro.app"
ditto --norsrc "$APP_PATH" "$STAGED_APP"
xattr -cr "$STAGED_APP"

# Some cloud-synced folders reapply com.apple.FinderInfo to bundles under the
# project directory. Codesign strict verification rejects that attribute, so the
# release archive is created from a clean temporary copy outside the workspace.
find "$STAGED_APP" -xattrname com.apple.FinderInfo -print0 | while IFS= read -r -d '' path; do
  xattr -wx com.apple.FinderInfo 0000000000000000000000000000000000000000000000000000000000000000 "$path" 2>/dev/null || true
done

codesign --force --deep --sign - "$STAGED_APP"
codesign --verify --deep --strict --verbose=2 "$STAGED_APP" >/dev/null

OUT_ZIP="dist/VideoDownloaderPro-macOS.zip"
rm -f "$OUT_ZIP"
ditto -c -k --norsrc --keepParent "$STAGED_APP" "$OUT_ZIP"

echo "[OK] App bundle: $APP_PATH"
echo "[OK] Zip package: $OUT_ZIP"
