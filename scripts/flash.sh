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
echo "   Flashing BuddyPoint to Xteink X3      "
echo "=========================================="

cd "$FIRMWARE_DIR"

# Auto-detect USB serial port on macOS
PORT=$(ls /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wch* 2>/dev/null | head -n 1 || true)

if [ -z "$PORT" ]; then
    echo "[!] No USB serial port detected."
    echo ""
    echo "[*] Troubleshooting Steps:"
    echo "  1. Ensure you are using a USB-C DATA cable (some cables are charge-only)."
    echo "  2. Use your reader's recovery instructions to enter download mode."
    echo "  3. Check available serial devices by running: ls /dev/cu.*"
    echo ""
    exit 1
fi

echo "[*] Using serial port: $PORT"

# Use project venv pio if available
if [ -f "$PROJECT_ROOT/.venv/bin/pio" ]; then
    export PATH="$PROJECT_ROOT/.venv/bin:$PATH"
fi

if command -v pio &> /dev/null; then
    echo "[*] Flashing via PlatformIO (env: default)..."
    pio run -e default --target upload --upload-port "$PORT"
    echo ""
    echo "[✓] Flash complete! Connecting to serial monitor (115200 baud)..."
    echo "    (Press Ctrl+C to exit monitor)"
    echo "--------------------------------------------------"
    pio device monitor --port "$PORT" -b 115200
else
    echo "[*] Install PlatformIO, or run python3 scripts/serve_web_flasher.py from the project root."
    echo "[*] Open http://localhost:8000 in desktop Chrome/Edge."
    exit 1
fi
