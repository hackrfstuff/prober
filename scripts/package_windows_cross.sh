#!/usr/bin/env bash
# package_windows_cross.sh
# Cross-compile Windows build from Linux using MinGW-w64.
# Usage: ./scripts/package_windows_cross.sh [--qt-prefix /usr/x86_64-w64-mingw32] [--build-type Release]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
DIST_DIR="$REPO_ROOT/dist"
BUILD_DIR="$REPO_ROOT/build-windows"
BUILD_TYPE="Release"
TOOLCHAIN="$REPO_ROOT/cmake/toolchain-mingw64.cmake"
MINGW_PREFIX="x86_64-w64-mingw32"
QT_PREFIX="/usr/${MINGW_PREFIX}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-type) BUILD_TYPE="$2"; shift 2 ;;
        --qt-prefix)  QT_PREFIX="$2";  shift 2 ;;
        *) echo "Unknown argument: $1"; exit 1 ;;
    esac
done

echo "=== Prober Windows Cross-Compile Builder ==="
echo "Repo root  : $REPO_ROOT"
echo "Dist dir   : $DIST_DIR"
echo "Build dir  : $BUILD_DIR"
echo "Build type : $BUILD_TYPE"
echo "Qt prefix  : $QT_PREFIX"
echo ""

for tool in "${MINGW_PREFIX}-gcc" "${MINGW_PREFIX}-g++" cmake ninja; do
    if ! command -v "$tool" &>/dev/null; then
        echo "ERROR: '$tool' not found. Install with:"
        echo "  sudo pacman -S --needed cmake ninja mingw-w64-gcc mingw-w64-binutils mingw-w64-headers mingw-w64-crt mingw-w64-winpthreads"
        exit 1
    fi
done

QT6_CMAKE="$QT_PREFIX/lib/cmake/Qt6/Qt6Config.cmake"
if [[ ! -f "$QT6_CMAKE" ]]; then
    echo "ERROR: Qt6 for Windows not found at $QT_PREFIX"
    echo ""
    echo "Install Qt6 MinGW cross-compile packages from AUR:"
    echo "  yay -S --needed mingw-w64-qt6-base"
    echo ""
    echo "Or set --qt-prefix to your Qt6 MinGW installation."
    exit 1
fi
echo "Qt6 found : $QT6_CMAKE"

echo ""
echo "[1/8] Cleaning dist folder..."
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

echo "[2/8] Configuring CMake (cross-compile to Windows)..."
rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -G Ninja \
    "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN" \
    "-DCMAKE_BUILD_TYPE=$BUILD_TYPE" \
    "-DCMAKE_PREFIX_PATH=$QT_PREFIX" \
    "$REPO_ROOT"

echo "[3/8] Building..."
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "[4/8] Copying executables..."
CLI_EXE="$BUILD_DIR/prober.exe"
GUI_IMPL_EXE="$BUILD_DIR/gui/prober_gui_impl.exe"
LAUNCHER_EXE="$BUILD_DIR/gui_launcher/prober_gui.exe"

[[ -f "$CLI_EXE"      ]] || { echo "ERROR: CLI not found: $CLI_EXE";           exit 1; }
[[ -f "$GUI_IMPL_EXE" ]] || { echo "ERROR: GUI impl not found: $GUI_IMPL_EXE"; exit 1; }
[[ -f "$LAUNCHER_EXE" ]] || { echo "ERROR: Launcher not found: $LAUNCHER_EXE"; exit 1; }

cp "$CLI_EXE"      "$DIST_DIR/prober.exe"
cp "$LAUNCHER_EXE" "$DIST_DIR/prober_gui.exe"

GUI_DIST_DIR="$DIST_DIR/tools/gui"
mkdir -p "$GUI_DIST_DIR"
cp "$GUI_IMPL_EXE" "$GUI_DIST_DIR/prober_gui_impl.exe"

echo "[5/8] Deploying Qt DLLs..."
QT_BIN="$QT_PREFIX/bin"
QT_PLUGINS="$QT_PREFIX/lib/qt6/plugins"

MINGW_BIN="/usr/${MINGW_PREFIX}/bin"
python3 - "$MINGW_BIN" "$GUI_DIST_DIR" <<'PYEOF'
import sys, os, subprocess, shutil

mingw_bin, dst_dir = sys.argv[1], sys.argv[2]

SYSTEM = {
    "kernel32.dll","user32.dll","gdi32.dll","ole32.dll","shell32.dll",
    "advapi32.dll","ntdll.dll","msvcrt.dll","ucrtbase.dll","d3d11.dll",
    "d3d12.dll","dwrite.dll","dxgi.dll","uxtheme.dll","winspool.drv",
    "comdlg32.dll","oleaut32.dll","imm32.dll","winmm.dll","ws2_32.dll",
    "iphlpapi.dll","setupapi.dll","cfgmgr32.dll","crypt32.dll","secur32.dll",
    "shlwapi.dll","version.dll","netapi32.dll","userenv.dll","opengl32.dll",
    "dwmapi.dll","wtsapi32.dll","mpr.dll","bcrypt.dll","ncrypt.dll",
}

def get_deps(path):
    try:
        out = subprocess.check_output(
            ["x86_64-w64-mingw32-objdump", "-p", path],
            stderr=subprocess.DEVNULL).decode()
        return [l.split()[-1] for l in out.splitlines() if "DLL Name:" in l]
    except:
        return []

seeds = ["Qt6Core.dll","Qt6Gui.dll","Qt6Widgets.dll","Qt6Network.dll"]
seen = set()
queue = list(seeds)

while queue:
    dll = queue.pop(0)
    low = dll.lower()
    if low in seen: continue
    seen.add(low)
    if low in SYSTEM or low.startswith("api-ms"): continue
    src = os.path.join(mingw_bin, dll)
    if not os.path.exists(src): continue
    dst = os.path.join(dst_dir, dll)
    if not os.path.exists(dst):
        shutil.copy2(src, dst)
        print(f"  Copied: {dll}")
    for dep in get_deps(src):
        if dep.lower() not in seen:
            queue.append(dep)
PYEOF

PLATFORM_DST="$GUI_DIST_DIR/platforms"
mkdir -p "$PLATFORM_DST"
PLATFORM_SRC="$QT_PLUGINS/platforms/qwindows.dll"
if [[ -f "$PLATFORM_SRC" ]]; then
    cp "$PLATFORM_SRC" "$PLATFORM_DST/"
    echo "  Copied: platforms/qwindows.dll"
else
    echo "  WARNING: qwindows.dll not found: $PLATFORM_SRC"
fi

STYLES_DST="$GUI_DIST_DIR/styles"
mkdir -p "$STYLES_DST"
STYLES_SRC="$QT_PLUGINS/styles/qwindowsvistastyle.dll"
if [[ -f "$STYLES_SRC" ]]; then
    cp "$STYLES_SRC" "$STYLES_DST/"
    echo "  Copied: styles/qwindowsvistastyle.dll"
fi

echo "[6/8] Copying tools..."

AVRDUDE_SRC="$REPO_ROOT/tools/avrdude"
AVRDUDE_DST="$DIST_DIR/tools/avrdude"
mkdir -p "$AVRDUDE_DST"
cp "$AVRDUDE_SRC"/* "$AVRDUDE_DST/" 2>/dev/null || true
echo "  avrdude: copied"

C2_SRC="$REPO_ROOT/tools/c2_firmware"
C2_DST="$DIST_DIR/tools/c2_firmware"
if [[ -d "$C2_SRC" ]]; then
    mkdir -p "$C2_DST"
    cp "$C2_SRC"/*.hex "$C2_DST/" 2>/dev/null || true
    cp "$C2_SRC/README.txt" "$C2_DST/" 2>/dev/null || true
    HEX_COUNT=$(find "$C2_DST" -name "*.hex" | wc -l)
    echo "  c2_firmware: $HEX_COUNT hex file(s)"
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
    [[ $HEX_COUNT -eq 0 ]] && { echo "  WARNING: Bluejay $VER has no .hex files"; continue; }
    cp "$VER_DIR"*.hex "$BJ_DST/"
    echo "  Bluejay $VER: $HEX_COUNT hex files"
done
if [[ -f "$BJ_ROOT/README.txt" ]]; then
    mkdir -p "$DIST_DIR/tools/bluejay_firmware"
    cp "$BJ_ROOT/README.txt" "$DIST_DIR/tools/bluejay_firmware/"
fi

echo "[7/8] Enforcing clean dist root..."
ALLOWED=("prober.exe" "prober_gui.exe" "vc_redist.x64.exe" "tools")
for item in "$DIST_DIR"/*/; do
    name="$(basename "$item")"
    ok=0
    for a in "${ALLOWED[@]}"; do [[ "$name" == "$a" ]] && ok=1; done
    if [[ $ok -eq 0 ]]; then
        echo "  Removing unexpected: $name"
        rm -rf "$item"
    fi
done
for item in "$DIST_DIR"/*.exe "$DIST_DIR"/*.dll; do
    [[ -f "$item" ]] || continue
    name="$(basename "$item")"
    ok=0
    for a in "${ALLOWED[@]}"; do [[ "$name" == "$a" ]] && ok=1; done
    [[ $ok -eq 0 ]] && { echo "  Removing unexpected: $name"; rm -f "$item"; }
done
echo "  Dist root: OK"

echo "[8/8] Building NSIS installer..."
VERSION=$(grep -oP 'project\(esctool VERSION \K[0-9.]+' "$REPO_ROOT/CMakeLists.txt" || echo "1.0.0")
echo "  Version: $VERSION"

if command -v makensis &>/dev/null; then
    makensis -DVERSION="$VERSION" "$REPO_ROOT/scripts/installer.nsi"
    INSTALLER="$DIST_DIR/prober-${VERSION}-setup.exe"
    if [[ -f "$INSTALLER" ]]; then
        SZ=$(du -sh "$INSTALLER" | cut -f1)
        echo "  Installer: $INSTALLER ($SZ)"
    else
        echo "  WARNING: NSIS ran but installer not found at $INSTALLER"
    fi
else
    echo "  SKIPPED: makensis not found. Install with: yay -S nsis"
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

