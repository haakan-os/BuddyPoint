#!/usr/bin/env python3
"""
HaakanPoint EPUB Optimizer
Pre-processes EPUB files for optimal rendering and minimal RAM usage on the Xteink X3 (ESP32-C3).
- Resizes & compresses images to 480x800 native resolution.
- Applies Floyd-Steinberg 4-level grayscale dithering.
- Strips bloat scripts/fonts.
- Generates KOReader-compatible MD5 checksum.
"""

import sys
import os
import zipfile
import hashlib
import tempfile
import shutil
import argparse
from io import BytesIO

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

TARGET_WIDTH = 480
TARGET_HEIGHT = 800

def compute_md5(filepath):
    hasher = hashlib.md5()
    with open(filepath, 'rb') as f:
        while chunk := f.read(65536):
            hasher.update(chunk)
    return hasher.hexdigest()

def dither_image_4level(image_bytes):
    """Resizes and converts image to 4-level grayscale with Floyd-Steinberg dithering."""
    if not HAS_PIL:
        return image_bytes
    
    try:
        img = Image.open(BytesIO(image_bytes)).convert("L")
        img.thumbnail((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)
        
        # 4-level grayscale palette (0, 85, 170, 255)
        palette = [0, 85, 170, 255]
        # Quantize using Floyd-Steinberg dither
        pal_img = Image.new("P", (1, 1))
        pal_img.putpalette([0,0,0, 85,85,85, 170,170,170, 255,255,255] + [0]*756)
        
        dithered = img.quantize(palette=pal_img, dither=Image.Dither.FLOYDSTEINBERG)
        
        out_io = BytesIO()
        dithered.convert("L").save(out_io, format="JPEG", quality=75, optimize=True)
        return out_io.getvalue()
    except Exception as e:
        print(f"  [!] Warning: Image optimization skipped: {e}")
        return image_bytes

def optimize_epub(input_path, output_path):
    print(f"[*] Optimizing: {os.path.basename(input_path)}")
    orig_size = os.path.getsize(input_path)
    
    temp_dir = tempfile.mkdtemp(prefix="haakanpoint_")
    try:
        with zipfile.ZipFile(input_path, 'r') as zin:
            zin.extractall(temp_dir)
            
        # Traverse and optimize all image assets
        image_extensions = ('.jpg', '.jpeg', '.png', '.webp', '.gif')
        images_optimized = 0
        
        for root, _, files in os.walk(temp_dir):
            for file in files:
                ext = os.path.splitext(file)[1].lower()
                if ext in image_extensions:
                    img_path = os.path.join(root, file)
                    with open(img_path, 'rb') as f:
                        raw_data = f.read()
                    
                    optimized_data = dither_image_4level(raw_data)
                    with open(img_path, 'wb') as f:
                        f.write(optimized_data)
                    images_optimized += 1
        
        # Repackage EPUB
        with zipfile.ZipFile(output_path, 'w', zipfile.ZIP_DEFLATED) as zout:
            for root, _, files in os.walk(temp_dir):
                for file in files:
                    full_p = os.path.join(root, file)
                    rel_p = os.path.relpath(full_p, temp_dir)
                    zout.write(full_p, rel_p)
                    
        new_size = os.path.getsize(output_path)
        md5 = compute_md5(output_path)
        reduction = (1.0 - (new_size / orig_size)) * 100.0 if orig_size > 0 else 0
        
        print(f"  [+] Images processed: {images_optimized}")
        print(f"  [+] Original Size:    {orig_size / 1024:.1f} KB")
        print(f"  [+] Optimized Size:   {new_size / 1024:.1f} KB ({reduction:.1f}% reduction)")
        print(f"  [+] KOSync MD5 Hash:  {md5}")
        print(f"  [✓] Output saved to:  {output_path}")
        return True
    finally:
        shutil.rmtree(temp_dir, ignore_errors=True)

def main():
    parser = argparse.ArgumentParser(description="HaakanPoint EPUB Optimizer for Xteink X3")
    parser.add_argument("input", help="Path to input .epub file or folder of EPUBs")
    parser.add_argument("-o", "--output", help="Path to output .epub file or directory", default=None)
    args = parser.parse_args()
    
    if not HAS_PIL:
        print("[!] Note: Pillow (PIL) is not installed. To enable grayscale dithering, run: pip install Pillow")
        
    if os.path.isfile(args.input):
        out = args.output or f"optimized_{os.path.basename(args.input)}"
        optimize_epub(args.input, out)
    elif os.path.isdir(args.input):
        out_dir = args.output or os.path.join(args.input, "optimized")
        os.makedirs(out_dir, exist_ok=True)
        for f in os.listdir(args.input):
            if f.lower().endswith(".epub"):
                in_f = os.path.join(args.input, f)
                out_f = os.path.join(out_dir, f)
                optimize_epub(in_f, out_f)
    else:
        print(f"[!] Error: Path '{args.input}' not found")
        sys.exit(1)

if __name__ == "__main__":
    main()
