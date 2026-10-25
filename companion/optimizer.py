#!/usr/bin/env python3
"""
BuddyPoint EPUB Optimizer
Bounds and validates EPUB archives, resizes PNG/JPEG images to 480x800, and
applies four-level grayscale dithering while retaining each image's format.
Chapter content, identifiers, metadata, navigation and author notes are preserved.
"""

import sys
import os
import zipfile
import hashlib
import tempfile
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

OPTIMIZER_VERSION = "x3-images-v1"
MAX_ARCHIVE = 64 * 1024 * 1024
MAX_EXPANDED = 128 * 1024 * 1024


def validate_epub(path):
    """Validate bounded ZIP/package structure without extracting untrusted paths."""
    import posixpath
    import xml.etree.ElementTree as ET
    if os.path.getsize(path) > MAX_ARCHIVE:
        raise ValueError("EPUB exceeds 64 MiB")
    with zipfile.ZipFile(path) as archive:
        entries = archive.infolist()
        names = [entry.filename for entry in entries]
        if len(entries) > 10000 or len(names) != len(set(names)):
            raise ValueError("Invalid EPUB entry count or duplicate paths")
        if sum(entry.file_size for entry in entries) > MAX_EXPANDED:
            raise ValueError("Expanded EPUB exceeds 128 MiB")
        for entry in entries:
            name = entry.filename
            if (name.startswith("/") or "\\" in name or ".." in name.split("/")
                    or entry.flag_bits & 1 or entry.file_size > 32 * 1024 * 1024):
                raise ValueError("Unsafe or oversized EPUB entry")
        if archive.read("mimetype") != b"application/epub+zip":
            raise ValueError("Not an EPUB")
        def xml(name):
            if archive.getinfo(name).file_size > 2 * 1024 * 1024:
                raise ValueError("Oversized EPUB package XML")
            raw = archive.read(name)
            if b"<!DOCTYPE" in raw.upper() or b"<!ENTITY" in raw.upper():
                raise ValueError("EPUB package XML declarations are not supported")
            return ET.fromstring(raw)
        container = xml("META-INF/container.xml")
        rootfile = container.find(".//{*}rootfile")
        if rootfile is None:
            raise ValueError("Missing EPUB package")
        opf = rootfile.attrib.get("full-path", "")
        package = xml(opf)
        manifest = {e.attrib.get("id"): e.attrib.get("href", "") for e in package.findall(".//{*}manifest/{*}item")}
        spine = package.findall(".//{*}spine/{*}itemref")
        if not spine:
            raise ValueError("EPUB has no reading order")
        from urllib.parse import unquote
        for item in spine:
            href = manifest.get(item.attrib.get("idref"), "")
            target = posixpath.normpath(posixpath.join(posixpath.dirname(opf), unquote(href.split("#")[0])))
            if not href or target not in names:
                raise ValueError("Missing EPUB chapter")
        if archive.testzip() is not None:
            raise ValueError("Corrupt EPUB entry")


def dither_image_4level(image_bytes):
    """Keep the original PNG/JPEG format and names; leave other assets intact."""
    if not HAS_PIL:
        return image_bytes
    import warnings
    with warnings.catch_warnings():
        warnings.simplefilter("error", Image.DecompressionBombWarning)
        try:
            with Image.open(BytesIO(image_bytes)) as original:
                fmt = original.format
                if fmt not in ("PNG", "JPEG") or original.width * original.height > 16000000:
                    return image_bytes
                image = original.convert("RGBA")
                background = Image.new("RGBA", image.size, "white")
                background.alpha_composite(image)
                image = background.convert("RGB")
                image.thumbnail((TARGET_WIDTH, TARGET_HEIGHT), Image.Resampling.LANCZOS)
                palette = Image.new("P", (1, 1))
                palette.putpalette([v for i in range(256) for v in ([0, 85, 170, 255][i % 4],) * 3])
                image = image.convert("L").convert("RGB").quantize(
                    palette=palette, dither=Image.Dither.FLOYDSTEINBERG).convert("L")
                output = BytesIO()
                image.save(output, format=fmt, **({"quality": 85, "optimize": True} if fmt == "JPEG" else {"optimize": True}))
                return output.getvalue()
        except (OSError, ValueError, Image.DecompressionBombWarning, Image.DecompressionBombError):
            return image_bytes


def optimize_epub(input_path, output_path):
    """Preserve package, chapter IDs, navigation and notes byte-for-byte."""
    validate_epub(input_path)
    output_path = os.path.abspath(output_path)
    fd, temporary = tempfile.mkstemp(prefix=".epub-", dir=os.path.dirname(output_path))
    os.close(fd)
    try:
        with zipfile.ZipFile(input_path) as source, zipfile.ZipFile(temporary, "w") as dest:
            dest.writestr("mimetype", b"application/epub+zip", compress_type=zipfile.ZIP_STORED)
            for entry in source.infolist():
                if entry.filename == "mimetype":
                    continue
                data = source.read(entry)
                if entry.filename.lower().endswith((".png", ".jpg", ".jpeg")):
                    data = dither_image_4level(data)
                dest.writestr(entry.filename, data, compress_type=zipfile.ZIP_DEFLATED)
        validate_epub(temporary)
        os.replace(temporary, output_path)
        return True
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


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
