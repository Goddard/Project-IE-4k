#!/bin/bash
# Dependency Checker for PIE4K
# Helps identify missing DLLs for GemRB plugins

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
INSTALL_DIR="$ROOT_DIR/install"
BIN_DIR="$INSTALL_DIR/bin"
PLUGINS_DIR="$INSTALL_DIR/gemrb/plugins"

echo "=== PIE4K Dependency Checker ==="
echo "Checking dependencies for PIE4K and GemRB plugins..."
echo ""

# Function to check dependencies of a DLL
check_dll_deps() {
    local dll_path="$1"
    local dll_name=$(basename "$dll_path")
    
    echo "Checking $dll_name:"
    
    if command -v ldd >/dev/null 2>&1; then
        # Use ldd if available
        ldd "$dll_path" 2>/dev/null | grep -i "not found" && {
            echo "  ❌ Missing dependencies found!"
            return 1
        } || {
            echo "  ✅ All dependencies found"
            return 0
        }
    else
        echo "  ⚠️  ldd not available, cannot check dependencies"
        return 0
    fi
}

# Check main executable
if [[ -f "$BIN_DIR/pie4k.exe" ]]; then
    echo "=== Main Executable ==="
    check_dll_deps "$BIN_DIR/pie4k.exe"
    echo ""
else
    echo "❌ pie4k.exe not found in $BIN_DIR"
    exit 1
fi

# Check GemRB core library
if [[ -f "$BIN_DIR/libgemrb_core.dll" ]]; then
    echo "=== GemRB Core Library ==="
    check_dll_deps "$BIN_DIR/libgemrb_core.dll"
    echo ""
elif [[ -f "$INSTALL_DIR/gemrb/libgemrb_core.dll" ]]; then
    echo "=== GemRB Core Library ==="
    check_dll_deps "$INSTALL_DIR/gemrb/libgemrb_core.dll"
    echo ""
fi

# Check problematic GemRB plugins
echo "=== GemRB Plugins (Known Problematic) ==="
PROBLEMATIC_PLUGINS=(
    "GUIScript.dll"
    "OGGReader.dll" 
    "OpenALAudio.dll"
    "SDLAudio.dll"
    "TTFImporter.dll"
    "VLCPlayer.dll"
)

for plugin in "${PROBLEMATIC_PLUGINS[@]}"; do
    plugin_path="$PLUGINS_DIR/$plugin"
    if [[ -f "$plugin_path" ]]; then
        check_dll_deps "$plugin_path"
    else
        echo "$plugin: ❌ Plugin file not found"
    fi
done

echo ""
echo "=== Dependency Summary ==="
echo "If any dependencies are missing, run:"
echo "  ./scripts/deploy-windows.sh"
echo ""
echo "This will copy all required DLLs to the bin directory."

