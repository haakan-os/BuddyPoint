#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

echo "=========================================="
echo "   Testing HaakanPoint Sync Workflows     "
echo "=========================================="

echo "[*] 1. Testing Python Optimizer..."
python3 "$PROJECT_ROOT/companion/optimizer.py" --help > /dev/null
echo "  [✓] Optimizer CLI functional"

echo "[*] 2. Checking Mock Server..."
python3 -c "import http.server, json, hashlib; print('  [✓] Standard libraries OK')"

echo "[*] 3. Testing KOReader optimization and transfer failures..."
if command -v luajit > /dev/null 2>&1; then
    luajit "$SCRIPT_DIR/test_plugin.lua"
    luajit "$SCRIPT_DIR/test_epub_xml.lua"
elif command -v lua > /dev/null 2>&1; then
    lua "$SCRIPT_DIR/test_plugin.lua"
    lua "$SCRIPT_DIR/test_epub_xml.lua"
else
    echo "[!] Install Lua or LuaJIT to run the plugin regression tests."
    exit 1
fi

if [ -n "${HAAKANPOINT_NATIVE_LUA:-}" ]; then
    NATIVE_ARGS=(--lua "$HAAKANPOINT_NATIVE_LUA")
    if [ -n "${HAAKANPOINT_NATIVE_BOOTSTRAP:-}" ]; then
        NATIVE_ARGS+=(--bootstrap "$HAAKANPOINT_NATIVE_BOOTSTRAP")
    fi
    python3 "$SCRIPT_DIR/test_epub_native.py" "${NATIVE_ARGS[@]}"
else
    echo "[*] Native EPUB integration requires a KOReader-compatible LuaJIT runtime; see docs/PLUGIN_DEVELOPMENT.md."
fi

echo "[✓] All tests passed!"
