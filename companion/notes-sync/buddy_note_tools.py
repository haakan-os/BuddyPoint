"""Reader navigation and simple Question :: Answer cards, without optional packages."""
import posixpath
import re
import struct
from pathlib import PurePosixPath
from urllib.parse import unquote, urlsplit
from buddy_math import HEADER, fnv

MAGIC = b'BUDNOTE1'


def tools_name(name):
    path = PurePosixPath(name)
    return str(path.with_name(f'buddy-notes-{fnv(path.name.encode()):016x}.bnotes'))


def prose_lines(source):
    fence = None
    for line in source.decode('utf-8-sig').splitlines():
        match = re.match(r'^ {0,3}(`{3,}|~{3,})', line)
        if match:
            marker = match[1]
            if fence is None:
                fence = marker
            elif marker[0] == fence[0] and len(marker) >= len(fence) and not line[match.end():].strip():
                fence = None
            continue
        if fence or line.startswith(('    ', '\t')):
            continue
        yield line


def resolve_note(name, target, names):
    try:
        parts = urlsplit(target)
    except ValueError:
        return None
    if parts.scheme or parts.netloc or parts.query:
        return None
    target = unquote(parts.path)
    if not target or target.startswith('/') or '\\' in target:
        return None
    candidates = [posixpath.normpath(posixpath.join(posixpath.dirname(name), target)), target]
    for candidate in candidates:
        for option in (candidate, candidate + '.md', candidate + '.markdown'):
            if option in names:
                return option
    matches = [n for n in names if PurePosixPath(n).stem == target]
    return matches[0] if len(matches) == 1 else None


def render_tools(source, name, names, reader_folder, report=lambda message: None):
    records = []
    seen = set()
    for line in prose_lines(source):
        visible = re.sub(r'(`+).*?\1', lambda m: ' ' * len(m[0]), line)
        if ' :: ' in visible:
            split = visible.index(' :: ')
            question, answer = line[:split], line[split + 4:]
            question, answer = question.strip(), answer.strip()
            if question and answer:
                records.append((2, question, answer))
        for match in re.finditer(r'(?<!!)\[\[([^\]\n]+)\]\]|(?<!!)\[([^\]\n]+)\]\(([^)\n]+)\)', visible):
            if match[1]:
                target, _, label = match[1].partition('|')
                label = label or target
            else:
                label, target = match[2], match[3].strip()
                if target.startswith('<') and target.endswith('>'):
                    target = target[1:-1]
            resolved = resolve_note(name, target, names)
            if resolved and resolved not in seen:
                records.append((1, label, reader_folder.rstrip('/') + '/' + resolved))
                seen.add(resolved)
    payload = bytearray()
    count = 0
    for kind, label, value in records:
        label, value = label.encode(), value.encode()
        if len(label) > 180 or len(value) > (768 if kind == 1 else 2048):
            report('Link/card skipped: question/label limit 180 bytes; answer limit 2048 bytes')
            continue
        if count == 64:
            report('Only the first 64 links/cards are included')
            break
        payload += struct.pack('<BHH', kind, len(label), len(value)) + label + value
        count += 1
    payload = struct.pack('<H', count) + payload
    return HEADER.pack(MAGIC, len(source), fnv(source), len(payload), fnv(payload)) + payload
