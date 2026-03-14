#!/usr/bin/env bash
# package_linux.sh
# Usage: ./scripts/package_linux.sh [--build-type Release|Debug] [--qt-prefix /path/to/qt]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$REPO_ROOT/dist"
BUILD_DIR="$REPO_ROOT/build"
BUILD_TYPE="Release"
QT_PREFIX=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --qt-prefix)  QT_PREFIX="$2";  shift 2 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

if [[ -z "$QT_PREFIX" ]]; then
    if pkg-config --exists Qt6Widgets 2>/dev/null; then
        QT_PREFIX="$(pkg-config --variable=prefix Qt6Widgets)"
    elif [[ -d /usr/lib/qt6 ]]; then
        QT_PREFIX=/usr
    fi
fi

echo "=== Prober Linux Distribution Builder ==="
echo "Repo root : $REPO_ROOT"
echo "Dist dir  : $DIST_DIR"
echo "Build type: $BUILD_TYPE"
echo "Qt prefix : ${QT_PREFIX:-<system>}"
echo ""

echo "[1/7] Cleaning dist folder..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

echo "[2/7] Configuring CMake..."
rm -rf "$BUILD_DIR"
CMAKE_ARGS=(-B "$BUILD_DIR" -G Ninja "-DCMAKE_BUILD_TYPE=$BUILD_TYPE")
if [[ -n "$QT_PREFIX" ]]; then
    CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_PREFIX")
fi
cmake "${CMAKE_ARGS[@]}" "$REPO_ROOT"

echo "[3/7] Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "[4/7] Copying executables..."
CLI_BIN="$BUILD_DIR/prober"
GUI_IMPL_BIN="$BUILD_DIR/gui/prober_gui_impl"
LAUNCHER_BIN="$BUILD_DIR/gui_launcher/prober_gui"

[[ -f "$CLI_BIN"       ]] || { echo "ERROR: CLI not found: $CLI_BIN";             exit 1; }
[[ -f "$GUI_IMPL_BIN"  ]] || { echo "ERROR: GUI impl not found: $GUI_IMPL_BIN";   exit 1; }
[[ -f "$LAUNCHER_BIN"  ]] || { echo "ERROR: Launcher not found: $LAUNCHER_BIN";   exit 1; }

cp "$CLI_BIN"      "$DIST_DIR/prober"
cp "$LAUNCHER_BIN" "$DIST_DIR/prober_gui"

GUI_DIST_DIR="$DIST_DIR/tools/gui"
mkdir -p "$GUI_DIST_DIR"
cp "$GUI_IMPL_BIN" "$GUI_DIST_DIR/prober_gui_impl"

echo "[5/7] Copying tools..."

AVRDUDE_SRC="$REPO_ROOT/tools/avrdude"
AVRDUDE_DST="$DIST_DIR/tools/avrdude"
mkdir -p "$AVRDUDE_DST"
if [[ -f "$AVRDUDE_SRC/avrdude" ]]; then
    cp "$AVRDUDE_SRC/avrdude" "$AVRDUDE_DST/"
    chmod +x "$AVRDUDE_DST/avrdude"
    echo "  avrdude binary copied"
fi
if [[ -f "$AVRDUDE_SRC/avrdude_linux.conf" ]]; then
    cp "$AVRDUDE_SRC/avrdude_linux.conf" "$AVRDUDE_DST/"
    echo "  avrdude_linux.conf copied"
fi

C2_SRC="$REPO_ROOT/tools/c2_firmware"
C2_DST="$DIST_DIR/tools/c2_firmware"
if [[ -d "$C2_SRC" ]]; then
    mkdir -p "$C2_DST"
    cp "$C2_SRC"/*.hex "$C2_DST/" 2>/dev/null || true
    cp "$C2_SRC/README.txt" "$C2_DST/" 2>/dev/null || true
    HEX_COUNT=$(find "$C2_DST" -name "*.hex" | wc -l)
    echo "  c2_firmware: $HEX_COUNT hex file(s) copied"
else
    echo "  WARNING: c2_firmware source missing: $C2_SRC"
fi

BJ_ROOT="$REPO_ROOT/tools/bluejay_firmware"
for VER_DIR in "$BJ_ROOT"/v*/; do
    [[ -d "$VER_DIR" ]] || continue
    VER="$(basename "$VER_DIR")"
    BJ_DST="$DIST_DIR/tools/bluejay_firmware/$VER"
    mkdir -p "$BJ_DST"
    HEX_COUNT=$(find "$VER_DIR" -name "*.hex" | wc -l)
    if [[ $HEX_COUNT -eq 0 ]]; then
        echo "  WARNING: Bluejay $VER contains no .hex files"
        continue
    fi
    cp "$VER_DIR"*.hex "$BJ_DST/"
    echo "  Bluejay $VER: $HEX_COUNT hex files copied"
done
if [[ -f "$BJ_ROOT/README.txt" ]]; then
    mkdir -p "$DIST_DIR/tools/bluejay_firmware"
    cp "$BJ_ROOT/README.txt" "$DIST_DIR/tools/bluejay_firmware/"
fi

echo "[6/7] Bundling Qt runtime..."
LINUXDEPLOY=$(command -v linuxdeploy 2>/dev/null || command -v linuxdeploy-x86_64.AppImage 2>/dev/null || true)
LINUXDEPLOY_QT=$(command -v linuxdeploy-plugin-qt 2>/dev/null || true)

if [[ -n "$LINUXDEPLOY" && -n "$LINUXDEPLOY_QT" ]]; then
    echo "  Using linuxdeploy to bundle Qt..."
    APPDIR="$BUILD_DIR/AppDir"
    rm -rf "$APPDIR"
    mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/lib"
    cp "$GUI_DIST_DIR/prober_gui_impl" "$APPDIR/usr/bin/"
    export QMAKE="${QT_PREFIX}/bin/qmake6"
    [[ -x "$QMAKE" ]] || QMAKE=$(command -v qmake6 2>/dev/null || command -v qmake)
    "$LINUXDEPLOY" --appdir "$APPDIR" --plugin qt --output appimage \
        --executable "$APPDIR/usr/bin/prober_gui_impl" 2>&1 | tail -20 || true
    if [[ -d "$APPDIR/usr/lib" ]]; then
        cp -r "$APPDIR/usr/lib/"* "$GUI_DIST_DIR/" 2>/dev/null || true
        echo "  Qt libs bundled into tools/gui/"
    fi
else
    echo "  linuxdeploy / linuxdeploy-plugin-qt not found."
    echo "  Qt libraries will NOT be bundled — the system Qt will be used."
    echo "  To bundle Qt, install linuxdeploy and linuxdeploy-plugin-qt:"
    echo "    https://github.com/linuxdeploy/linuxdeploy/releases"
    echo "    https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases"
fi

echo "[7/7] Sanity checks..."

check_bin() {
    local label="$1" path="$2"
    if [[ -x "$path" ]]; then
        SIZE=$(du -sh "$path" | cut -f1)
        echo "  $label: OK ($SIZE)"
    else
        echo "  $label: MISSING or not executable"
    fi
}
check_bin "CLI"         "$DIST_DIR/prober"
check_bin "Launcher"    "$DIST_DIR/prober_gui"
check_bin "GUI impl"    "$DIST_DIR/tools/gui/prober_gui_impl"

if "$DIST_DIR/prober" --list-ports --json >/dev/null 2>&1; then
    echo "  CLI smoke test: OK"
else
    echo "  CLI smoke test: FAILED (exit $?)"
fi

echo ""
echo "=== DIST READY ==="
echo "Location: $DIST_DIR"
echo ""
echo "Contents:"
TOTAL=0
while IFS= read -r -d '' f; do
    REL="${f#$DIST_DIR/}"
    SZ=$(du -sh "$f" | cut -f1)
    echo "  $REL ($SZ)"
    BYTES=$(stat -c%s "$f")
    TOTAL=$((TOTAL + BYTES))
done < <(find "$DIST_DIR" -type f -print0 | sort -z)
echo ""
printf "Total size: %.1f MB\n" "$(awk "BEGIN{printf \"%.1f\", $TOTAL/1048576}")"

