#!/usr/bin/env python3
"""Package a completed X3/X4 build for the static browser flasher."""
import argparse
import hashlib
import json
import struct
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / 'web-flasher/firmware'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--environment', choices=('default', 'gh_release'), default='default')
    args = parser.parse_args()
    build = ROOT / 'crosspoint-upstream/.pio/build' / args.environment
    app = (build / 'firmware.bin').read_bytes()
    factory = (build / 'firmware.factory.bin').read_bytes()
    bootloader = (build / 'bootloader.bin').read_bytes()
    partitions = (build / 'partitions.bin').read_bytes()
    if not (app[0] == 0xe9 and struct.unpack_from('<H', app, 12)[0] == 5
            and b'CROSSPOINT-BOARD-V1:x4;' in app and len(app) <= 0x640000):
        raise SystemExit('Expected an ESP32-C3 X3/X4 application build.')
    expected = [
        (1, 2, 0x9000, 0x5000), (1, 0, 0xe000, 0x2000),
        (0, 0x10, 0x10000, 0x640000), (0, 0x11, 0x650000, 0x640000),
    ]
    # Validate offsets from the actual partition binary, not its filename.
    entries = []
    for offset in range(0, len(partitions), 32):
        if partitions[offset:offset + 2] != b'\xaa\x50':
            break
        _, kind, subtype, address, size = struct.unpack_from('<HBBII', partitions, offset)
        entries.append((kind, subtype, address, size))
    if entries[:4] != expected:
        raise SystemExit('Partition table does not match the 16 MB X3/X4 layout.')
    if len(factory) < 0x10000 + len(app) or factory[0x10000:0x10000 + len(app)] != app:
        raise SystemExit('Factory image and application are from different builds.')
    if factory[:len(bootloader)] != bootloader or factory[0x8000:0x8000 + len(partitions)] != partitions:
        raise SystemExit('Factory bootloader/partitions do not match the build.')
    parts = [('bootloader.bin', 0, bootloader), ('partitions.bin', 0x8000, partitions),
             ('boot_app0.bin', 0xe000, factory[0xe000:0x10000]), ('firmware.bin', 0x10000, app)]
    DEST.mkdir(parents=True, exist_ok=True)
    manifest = {
        'name': 'BuddyPoint — notes sync, themes, Sudoku & Markdown checklists',
        'chip': 'ESP32-C3', 'flashSize': '16MB',
        'builtAt': datetime.fromtimestamp((build / 'firmware.bin').stat().st_mtime, timezone.utc).isoformat(),
        'parts': [],
    }
    for name, offset, data in parts:
        (DEST / name).write_bytes(data)
        manifest['parts'].append({'path': name, 'offset': offset, 'size': len(data),
                                  'sha256': hashlib.sha256(data).hexdigest()})
    (DEST / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Packaged {len(app):,} bytes of application firmware in {DEST}')


if __name__ == '__main__':
    main()
