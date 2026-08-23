#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

QT_DIR="${Qt6_DIR:-}"
QT_ROOT="${QT_ROOT_DIR:-}"
if [[ -z "$QT_ROOT" && -n "$QT_DIR" ]]; then QT_ROOT="$(cd "$QT_DIR/../../.." && pwd)"; fi
[[ -n "$QT_ROOT" ]] || { echo "[ERR] Qt6_DIR or QT_ROOT_DIR is required"; exit 1; }
TOOLCHAIN="$ROOT_DIR/build_assets/toolchain"
mkdir -p "$TOOLCHAIN" "$ROOT_DIR/build_assets" "$ROOT_DIR/dist"

download() { curl -L --fail --retry 3 -A "VideoDownloaderPro/4.0.2" "$1" -o "$2"; }

download "https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_macos" "$TOOLCHAIN/yt-dlp"
chmod 755 "$TOOLCHAIN/yt-dlp"

case "$(uname -m)" in arm64) ARCH=aarch64 ;; x86_64) ARCH=x86_64 ;; *) exit 1 ;; esac
DENO_NAME="deno-${ARCH}-apple-darwin.zip"
download "https://github.com/denoland/deno/releases/latest/download/$DENO_NAME" "build_assets/$DENO_NAME"
download "https://github.com/denoland/deno/releases/latest/download/$DENO_NAME.sha256sum" "build_assets/$DENO_NAME.sha256sum"
EXPECTED="$(awk 'NR==1 {print $1}' "build_assets/$DENO_NAME.sha256sum")"
ACTUAL="$(shasum -a 256 "build_assets/$DENO_NAME" | awk '{print $1}')"
[[ "$EXPECTED" == "$ACTUAL" ]] || { echo "Deno checksum mismatch"; exit 1; }
unzip -o "build_assets/$DENO_NAME" -d "$TOOLCHAIN"
chmod 755 "$TOOLCHAIN/deno"

download "https://evermeet.cx/ffmpeg/getrelease/ffmpeg/zip" build_assets/ffmpeg-macos.zip
download "https://evermeet.cx/ffmpeg/getrelease/ffprobe/zip" build_assets/ffprobe-macos.zip
unzip -oj build_assets/ffmpeg-macos.zip ffmpeg -d "$TOOLCHAIN"
unzip -oj build_assets/ffprobe-macos.zip ffprobe -d "$TOOLCHAIN"
chmod 755 "$TOOLCHAIN/ffmpeg" "$TOOLCHAIN/ffprobe"

cmake -S . -B build-cpp -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT" -DBUILD_TESTING=ON
cmake --build build-cpp
ctest --test-dir build-cpp --output-on-failure

APP="$ROOT_DIR/dist/VideoDownloaderPro.app"
rm -rf "$APP"
cp -R build-cpp/VideoDownloaderPro.app "$APP"
"$QT_ROOT/bin/macdeployqt" "$APP"
mkdir -p "$APP/Contents/Resources/toolchain"
cp "$TOOLCHAIN/yt-dlp" "$TOOLCHAIN/deno" "$TOOLCHAIN/ffmpeg" "$TOOLCHAIN/ffprobe" "$APP/Contents/Resources/toolchain/"
codesign --force --deep --sign - "$APP"
SMOKE_HOME="$ROOT_DIR/build-cpp/smoke-home"
mkdir -p "$SMOKE_HOME"
HOME="$SMOKE_HOME" VDP_SMOKE_TEST=1 "$APP/Contents/MacOS/VideoDownloaderPro"
[[ -x "$SMOKE_HOME/Library/Application Support/VideoDownloaderPro/runtime/deno/deno" ]] || {
  echo "[ERR] Packaged app smoke test did not provision Deno"
  exit 1
}
echo "[OK] Packaged executable smoke test passed"
rm -f dist/VideoDownloaderPro-macOS.zip
ditto -c -k --keepParent "$APP" dist/VideoDownloaderPro-macOS.zip
echo "[OK] Native C++ package: dist/VideoDownloaderPro-macOS.zip"
