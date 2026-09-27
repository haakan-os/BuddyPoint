#!/usr/bin/env python3
"""Rasterise the repository SVG into the reader's rotated, white=1 bitmap format.

Run with the notes companion's optional rendering packages installed.
"""
from io import BytesIO
from pathlib import Path
from PIL import Image
from resvg_py import svg_to_bytes

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / 'crosspoint-upstream/src/images'
SIZE = 120


def main():
    pixels = svg_to_bytes(svg_string=(ASSETS / 'BuddyPointLogo.svg').read_text(),
                          background='white', skip_system_fonts=True)
    picture = Image.open(BytesIO(pixels)).convert('L').point(lambda pixel: 255 if pixel >= 128 else 0).convert('1')
    assert picture.size == (SIZE, SIZE)
    picture.save(ASSETS / 'BuddyPointLogo120.png')
    # drawIcon maps (row, col) to (size-1-row, col), undoing this rotation.
    packed = picture.transpose(Image.Transpose.ROTATE_90).tobytes()
    assert len(packed) == SIZE * SIZE // 8
    rows = ['    ' + ', '.join(f'0x{byte:02x}' for byte in packed[i:i+16]) + ','
            for i in range(0, len(packed), 16)]
    (ASSETS / 'BuddyPointLogo120.h').write_text(
        '#pragma once\n#include <cstdint>\n\n'
        '// Source: BuddyPointLogo.svg; regenerate with scripts/generate_buddy_logo.py.\n'
        f'static constexpr int BUDDY_POINT_LOGO_SIZE = {SIZE};\n'
        'static constexpr uint8_t BuddyPointLogo120[] = {\n' + '\n'.join(rows) + '\n};\n')
    # Check the exact renderer mapping, rather than just the source SVG.
    decoded = Image.frombytes('1', (SIZE, SIZE), packed).transpose(Image.Transpose.ROTATE_270)
    assert decoded.tobytes() == picture.tobytes()
    print(f'Logo: {SIZE} × {SIZE}, {len(packed)} bytes, bitmap orientation verified.')


if __name__ == '__main__':
    main()
