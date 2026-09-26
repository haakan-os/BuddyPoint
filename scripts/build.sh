#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
FIRMWARE_DIR="$PROJECT_ROOT/crosspoint-upstream"

if [ ! -d "$FIRMWARE_DIR" ]; then
    echo "[!] Missing crosspoint-upstream firmware source."
    exit 1
fi

echo "=========================================="
echo "   Building BuddyPoint for Xteink X3     "
echo "=========================================="

cd "$FIRMWARE_DIR"

# Use project venv pio if available
if [ -f "$PROJECT_ROOT/.venv/bin/pio" ]; then
    export PATH="$PROJECT_ROOT/.venv/bin:$PATH"
fi

if command -v pio &> /dev/null; then
    echo "[*] Compiling with PlatformIO (env: default for X3/X4)..."
    pio run -e default
    python3 "$SCRIPT_DIR/package_web_firmware.py"
    echo "[✓] Build complete! Firmware binary is at: $FIRMWARE_DIR/.pio/build/default/firmware.bin"
else
    echo "[!] PlatformIO CLI is not in your current PATH."
    echo "[*] Run in your terminal where pio is installed:"
    echo "    cd crosspoint-upstream && pio run -e default"
    exit 1
fi
