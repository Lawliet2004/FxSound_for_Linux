#!/usr/bin/env bash
# Build an FxSound AppImage.
#
# Usage:
#   ./packaging/build-appimage.sh [--build-dir DIR] [--output DIR]
#
# Requirements (downloaded automatically to /tmp/appimage-tools if not on PATH):
#   linuxdeploy   https://github.com/linuxdeploy/linuxdeploy/releases/continuous
#   appimagetool  https://github.com/AppImage/AppImageKit/releases/continuous
#
# System requirements (not bundled — must be on the target):
#   pipewire, libasound (ALSA)
#
# Produces: FxSound-<version>-<arch>.AppImage in the output directory.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="$REPO_ROOT/build-appimage"
OUTPUT_DIR="$REPO_ROOT"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir) BUILD_DIR="$2"; shift 2 ;;
        --output)    OUTPUT_DIR="$2"; shift 2 ;;
        *) echo "Unknown option: $1" >&2; exit 1 ;;
    esac
done

APPDIR="$BUILD_DIR/AppDir"
VERSION="$(grep -m1 'project(FxSound VERSION' "$REPO_ROOT/CMakeLists.txt" \
           | grep -oP '\d+\.\d+\.\d+\.\d+')"
ARCH="$(uname -m)"
APPIMAGE_OUT="$OUTPUT_DIR/FxSound-$VERSION-$ARCH.AppImage"
TOOLS_DIR=/tmp/appimage-tools

# ── helpers ───────────────────────────────────────────────────────────────────

need_tool() {
    local name="$1" url="$2"
    if ! command -v "$name" &>/dev/null; then
        local dest="$TOOLS_DIR/$name"
        if [[ ! -x "$dest" ]]; then
            mkdir -p "$TOOLS_DIR"
            echo "==> Downloading $name …"
            curl -fsSL "$url" -o "$dest"
            chmod +x "$dest"
        fi
        export PATH="$TOOLS_DIR:$PATH"
    fi
    local expected actual tool_path
    tool_path="$(command -v "$name")"
    expected="$(awk -v tool="$name" '$2 == tool {print $1}' "$SCRIPT_DIR/appimage-tools.sha256")"
    actual="$(sha256sum "$tool_path" | cut -d' ' -f1)"
    if [[ -z "$expected" || "$actual" != "$expected" ]]; then
        echo "Checksum mismatch for $name ($tool_path); review the tool before updating packaging/appimage-tools.sha256" >&2
        exit 1
    fi
}

# ── download packaging tools if needed ────────────────────────────────────────

need_tool linuxdeploy \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"

need_tool appimagetool \
    "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-x86_64.AppImage"

# ── build ─────────────────────────────────────────────────────────────────────

echo "==> Building FxSound $VERSION (Release) …"
cmake -S "$REPO_ROOT" -B "$BUILD_DIR/cmake" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX=/usr \
    -DFXSOUND_STUB_DSP=OFF -DFXSOUND_STUB_AUDIO=OFF -DFXSOUND_SANITIZER=none
cmake --build "$BUILD_DIR/cmake" --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
ctest --test-dir "$BUILD_DIR/cmake" -L unit --output-on-failure

# ── populate AppDir ────────────────────────────────────────────────────────────

echo "==> Installing into AppDir …"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR/cmake"

# AppImage runtime expects the .desktop and main icon at AppDir root.
cp "$APPDIR/usr/share/applications/fxsound.desktop" "$APPDIR/"
cp "$APPDIR/usr/share/icons/hicolor/256x256/apps/fxsound.png"  "$APPDIR/"

# AppRun: sets up library and data search paths before launching the binary.
# Presets are found via the exe-relative path built into FxController, so
# no explicit preset env var is needed.
cat > "$APPDIR/AppRun" <<'APPRUN'
#!/bin/sh
HERE="$(dirname "$(readlink -f "$0")")"
export XDG_DATA_DIRS="$HERE/usr/share:${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
export LD_LIBRARY_PATH="$HERE/usr/lib:${LD_LIBRARY_PATH:-}"
exec "$HERE/usr/bin/fxsound" "$@"
APPRUN
chmod +x "$APPDIR/AppRun"

# ── bundle libraries ───────────────────────────────────────────────────────────

echo "==> Bundling libraries with linuxdeploy …"
# NO_STRIP=1: linuxdeploy's bundled strip does not understand the .relr.dyn
# compressed-relocations section produced by gcc 14+ / modern linkers (as used
# on CachyOS). Without NO_STRIP it exits 1 and aborts the build. Stripping is
# unnecessary anyway since we already build with -O2 in Release mode.
APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 linuxdeploy \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/fxsound"

# Keep notices for the third-party libraries copied into the AppImage. On
# Debian-family build hosts these notices are provided by the owning packages.
if command -v dpkg-query >/dev/null; then
    mkdir -p "$APPDIR/usr/share/doc/fxsound/bundled-libraries"
    while IFS= read -r lib; do
        package=$(dpkg-query -S "*/$(basename "$lib")" 2>/dev/null | head -1 | cut -d: -f1 || true)
        if [[ -n "$package" && -f "/usr/share/doc/$package/copyright" ]]; then
            cp "/usr/share/doc/$package/copyright" \
                "$APPDIR/usr/share/doc/fxsound/bundled-libraries/$package.copyright"
        fi
    done < <(find "$APPDIR/usr/lib" -type f -name '*.so*')
fi

# ── create AppImage ────────────────────────────────────────────────────────────

echo "==> Packing AppImage …"
APPIMAGE_EXTRACT_AND_RUN=1 appimagetool \
    --comp gzip \
    "$APPDIR" "$APPIMAGE_OUT"

echo ""
SIZE="$(ls -lh "$APPIMAGE_OUT" | awk '{print $5}')"
echo "Done: $APPIMAGE_OUT  ($SIZE)"
