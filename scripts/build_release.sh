#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS packaging must run on macOS'; exit 1; }
QT_ROOT="${QT_ROOT_DIR:-}"
if [[ -z "$QT_ROOT" && -n "${Qt6_DIR:-}" ]]; then QT_ROOT="$(cd "$Qt6_DIR/../../.." && pwd)"; fi
[[ -x "$QT_ROOT/bin/macdeployqt" ]] || { echo 'QT_ROOT_DIR or Qt6_DIR must identify a Qt installation'; exit 1; }
export PATH="$QT_ROOT/bin:$PATH"
VERSION="$(python3 scripts/release_tools.py version)"
case "$(uname -m)" in arm64) PLATFORM=macos-arm64 ;; x86_64) PLATFORM=macos-x64 ;; *) exit 1 ;; esac
BUILD="$ROOT_DIR/build-cpp/pro5-package-$PLATFORM"
TOOLCHAIN="$ROOT_DIR/build_assets/pro5-toolchain-$PLATFORM"
python3 -m unittest discover -s scripts -p test_release_tools.py -v
python3 scripts/release_tools.py validate-lock
python3 scripts/release_tools.py prepare-tools "$PLATFORM" "$TOOLCHAIN"
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT" -DBUILD_TESTING=ON
cmake --build "$BUILD"
ctest --test-dir "$BUILD" --output-on-failure
mkdir -p "$ROOT_DIR/dist"
STAGING="$(mktemp -d "$ROOT_DIR/dist/staging.XXXXXXXX")"
python3 scripts/release_tools.py engine-smoke "$TOOLCHAIN" "$BUILD/engine_smoke_arguments" "$BUILD/engine-$(basename "$STAGING")"
APP="$STAGING/VideoDownloaderPro.app"
cp -R "$BUILD/VideoDownloaderPro.app" "$APP"
"$QT_ROOT/bin/macdeployqt" "$APP"
mkdir -p "$APP/Contents/Resources/toolchain"
cp -R "$TOOLCHAIN/." "$APP/Contents/Resources/toolchain/"
cp README.md CHANGELOG.md THIRD_PARTY_NOTICES.md "$APP/Contents/Resources/"
cp runtime/toolchain-lock.json "$APP/Contents/Resources/toolchain/"
if [[ -d licenses ]]; then cp -R licenses "$APP/Contents/Resources/"; fi
if [[ -d "$QT_ROOT/sbom" ]]; then cp -R "$QT_ROOT/sbom" "$APP/Contents/Resources/licenses/qt-sbom"; fi
# Runtime executables live in Resources, outside codesign's conventional nested
# code directories. Sign each explicitly and retain the upstream JIT entitlements.
for TOOL in yt-dlp deno ffmpeg ffprobe; do
  if [[ -n "${VDP_MAC_SIGN_IDENTITY:-}" ]]; then
    codesign --force --options runtime --timestamp --preserve-metadata=entitlements --sign "$VDP_MAC_SIGN_IDENTITY" "$APP/Contents/Resources/toolchain/$TOOL"
  else
    codesign --force --preserve-metadata=entitlements --sign - "$APP/Contents/Resources/toolchain/$TOOL"
  fi
done
if [[ -n "${VDP_MAC_SIGN_IDENTITY:-}" ]]; then
  codesign --force --deep --options runtime --timestamp --sign "$VDP_MAC_SIGN_IDENTITY" "$APP"
else
  codesign --force --deep --sign - "$APP"
fi
python3 scripts/release_tools.py rehash-signed-tools "$APP/Contents/Resources/toolchain"
# Re-seal resources after updating signed binary hashes, preserving nested signatures.
if [[ -n "${VDP_MAC_SIGN_IDENTITY:-}" ]]; then
  codesign --force --options runtime --timestamp --sign "$VDP_MAC_SIGN_IDENTITY" "$APP"
else
  codesign --force --sign - "$APP"
fi
codesign --verify --deep --strict "$APP"
python3 scripts/release_tools.py smoke "$APP/Contents/MacOS/VideoDownloaderPro" "$BUILD/smoke-$(basename "$STAGING")"
ZIP="$ROOT_DIR/dist/VideoDownloaderPro-$VERSION-$PLATFORM.zip"
ditto -c -k --keepParent "$APP" "$ZIP"
if [[ -n "${VDP_MAC_NOTARY_PROFILE:-}" ]]; then
  [[ -n "${VDP_MAC_SIGN_IDENTITY:-}" ]] || { echo 'Notarization requires Developer ID signing'; exit 1; }
  xcrun notarytool submit "$ZIP" --keychain-profile "$VDP_MAC_NOTARY_PROFILE" --wait --timeout 15m
  xcrun stapler staple "$APP"
  xcrun stapler validate "$APP"
  ditto -c -k --keepParent "$APP" "$ZIP"
fi
python3 scripts/release_tools.py checksums "$ZIP" > "$ROOT_DIR/dist/SHA256SUMS-macos.txt"
echo "[OK] macOS package passed smoke: $ZIP"
