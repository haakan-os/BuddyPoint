"""Bounded local image collection for rendered notes; never fetch remote content."""
from io import BytesIO
from pathlib import Path, PurePosixPath
from urllib.parse import unquote, urlsplit
import hashlib
import json
import os

MAX_ASSET = 8 * 1024 * 1024
EXTENSIONS = {'.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp'}


def wiki_image(state, silent):
    start = state.pos
    if not state.src.startswith('![[', start):
        return False
    end = state.src.find(']]', start + 3, state.posMax)
    if end < 0:
        return False
    target = state.src[start + 3:end].split('|')[0]
    if PurePosixPath(target).suffix.lower() not in EXTENSIONS:
        return False
    if not silent:
        token = state.push('image', 'img', 0)
        token.attrSet('src', target)
        token.content = PurePosixPath(target).name
        token.children = []
    state.pos = end + 2
    return True


def local_asset(root, note, target):
    parts = urlsplit(target)
    if parts.scheme or parts.netloc or parts.query:
        raise ValueError('Only local image paths are supported')
    name = unquote(parts.path)
    if not name or '\\' in name or name.startswith('/') or '\x00' in name:
        raise ValueError('Invalid image path')
    if PurePosixPath(name).suffix.lower() not in EXTENSIONS:
        raise ValueError('Unsupported image format')
    root = Path(root).resolve()
    # Note-relative first, then vault-relative (Obsidian attachment folders).
    for candidate in (root / PurePosixPath(note).parent / name, root / name):
        relative = candidate.relative_to(root)
        current = root
        for component in relative.parts:
            current = current / component
            if current.is_symlink() or (hasattr(current, 'is_junction') and current.is_junction()):
                raise ValueError('Linked image paths are not supported')
        resolved = candidate.resolve()
        if root not in resolved.parents:
            raise ValueError('Image path leaves the notes folder')
        if any(part.startswith('.') for part in resolved.relative_to(root).parts):
            raise ValueError('Hidden image paths are not supported')
        if resolved.is_file():
            with resolved.open('rb') as stream:
                data = stream.read(MAX_ASSET + 1)
            if len(data) > MAX_ASSET:
                raise ValueError('Image exceeds 8 MiB')
            return data
    if '/' not in name:
        matches = []
        visited = 0
        for directory, folders, files in os.walk(root, followlinks=False):
            folders[:] = [part for part in folders if not part.startswith('.')
                          and not (Path(directory) / part).is_symlink()
                          and not (hasattr(Path(directory) / part, 'is_junction')
                                   and (Path(directory) / part).is_junction())]
            visited += len(files) + len(folders)
            if visited > 10000:
                raise ValueError('Attachment search exceeds 10000 entries; use a relative path')
            if name in files:
                matches.append(Path(directory) / name)
        if len(matches) == 1:
            relative = matches[0].relative_to(root).as_posix()
            if relative != name:
                return local_asset(root, note, relative)
        if len(matches) > 1:
            raise ValueError('Ambiguous attachment name; use a relative path')
    raise ValueError('Image not found')


def collect_assets(source, root, note, report):
    from markdown_it import MarkdownIt
    md = MarkdownIt('commonmark')
    md.inline.ruler.before('image', 'buddy_wiki_image', wiki_image)
    assets = {}
    total = 0
    for token in md.parse(source.decode('utf-8-sig')):
        for child in token.children or []:
            if child.type != 'image':
                continue
            target = child.attrGet('src')
            if target in assets:
                continue
            if len(assets) >= 128:
                raise ValueError('At most 128 image references per note')
            try:
                data = local_asset(root, note, target)
                total += len(data)
                if total > 32 * 1024 * 1024:
                    raise ValueError('Combined source images exceed 32 MiB')
                assets[target] = data
            except (ValueError, OSError) as error:
                assets[target] = None
                report(f'{target}: {error}')
    return assets


def asset_fingerprint(assets):
    return json.dumps({name: hashlib.sha256(data).hexdigest() if data is not None else None
                       for name, data in assets.items()}, sort_keys=True).encode()


def jpeg_image(data):
    from PIL import Image, ImageOps
    try:
        with Image.open(BytesIO(data)) as source:
            if source.width * source.height > 16_000_000:
                raise ValueError('Image exceeds 16 megapixels')
            picture = ImageOps.exif_transpose(source).convert('RGBA')
    except Image.DecompressionBombError as error:
        raise ValueError('Image exceeds the pixel limit') from error
    picture.thumbnail((420, 500), Image.Resampling.LANCZOS)
    background = Image.new('RGBA', picture.size, 'white')
    background.alpha_composite(picture)
    picture = background.convert('L')
    output = BytesIO()
    picture.save(output, format='JPEG', quality=98, progressive=False, optimize=False)
    return output.getvalue()
