"""Optional, offline display-math rendering for BuddyPoint (no JavaScript or TeX)."""
from html import escape
from io import BytesIO
from pathlib import Path, PurePosixPath
import sys
import shlex
import struct
import zipfile

# Bump when generated output changes so unchanged notes refresh on the next sync.
RENDERER_VERSION = 3
MAGIC = b"BUDMATH1"
HEADER = struct.Struct("<8sIQIQ")
MAX_BUNDLE = 8 * 1024 * 1024


def fnv(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return value


def sidecar_name(name):
    path = PurePosixPath(name)
    return str(path.with_name(f"buddy-math-{fnv(path.name.encode('utf-8')):016x}.bmath"))


def dependencies():
    try:
        from markdown_it import MarkdownIt
        import ziamath
        from resvg_py import svg_to_bytes
        from latex2mathml.converter import convert
        from PIL import Image
    except ImportError as error:
        command = shlex.join([sys.executable, "-m", "pip", "install", "-r",
                              str(Path(__file__).resolve().with_name("requirements-math.txt"))])
        raise ValueError(f"Math rendering needs the optional packages. Run: {command}") from error
    return MarkdownIt, ziamath, svg_to_bytes, convert, Image


def closing_math(source, delimiter, start, inline=False):
    end = source.find(delimiter, start)
    while end >= 0:
        slashes = 0
        pos = end - 1
        while pos >= 0 and source[pos] == "\\":
            slashes += 1
            pos -= 1
        if slashes % 2 == 0:
            if not inline or (end > start and not source[end-1].isspace()
                              and (end + 1 == len(source) or not source[end+1].isdigit())):
                if not inline or "\n" not in source[start:end]:
                    return end
        end = source.find(delimiter, end + len(delimiter))
    return -1


def math_inline(state, silent):
    start = state.pos
    if state.src[start] != '$':
        return False
    delimiter = '$$' if state.src.startswith('$$', start) else '$'
    begin = start + len(delimiter)
    if begin >= state.posMax or (delimiter == '$' and state.src[begin].isspace()):
        return False
    end = closing_math(state.src[:state.posMax], delimiter, begin, delimiter == '$')
    if end < 0 or end == begin:
        return False
    if not silent:
        token = state.push('buddy_math_display' if delimiter == '$$' else 'buddy_math_inline', '', 0)
        token.content, token.markup = state.src[begin:end].strip(), delimiter
    state.pos = end + len(delimiter)
    return True


def math_block(state, start, end, silent):
    # Capture display blocks before Markdown interprets their interior as lists,
    # emphasis, or horizontal rules. The inline rule splits adjacent expressions
    # and preserves any prose following the closing delimiter.
    if state.sCount[start] - state.blkIndent >= 4:
        return False
    begin = state.bMarks[start] + state.tShift[start]
    if not state.src.startswith('$$', begin):
        return False
    close = closing_math(state.src[:state.eMarks[end-1]], '$$', begin + 2)
    if close < 0:
        return False
    stop = start + 1
    while True:
        while stop < end and state.eMarks[stop-1] < close + 2:
            stop += 1
        # A second adjacent display may start here and close on another line.
        tail = state.src[close + 2:state.eMarks[stop-1]]
        if not tail.lstrip().startswith('$$'):
            break
        following = close + 2 + len(tail) - len(tail.lstrip())
        next_close = closing_math(state.src[:state.eMarks[end-1]], '$$', following + 2)
        if next_close < 0:
            break
        close = next_close
    if silent:
        return True
    opening = state.push('paragraph_open', 'p', 1)
    opening.map = [start, stop]
    token = state.push('inline', '', 0)
    token.content = state.getLines(start, stop, state.blkIndent, False).strip()
    token.map, token.children = [start, stop], []
    state.push('paragraph_close', 'p', -1)
    state.line = stop
    return True


def render_note(source, title, report=lambda message: None):
    """Return a checked EPUB sidecar or None. The source bytes are never rewritten."""
    from buddy_math_layout import MathLayout
    MarkdownIt, ziamath, rasterize, convert, Image = dependencies()
    try:
        text = source.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("Math notes must use UTF-8 text") from error
    md = MarkdownIt("commonmark", {"html": False, "xhtmlOut": True}).enable("table")
    md.block.ruler.before("fence", "buddy_math", math_block,
                          {"alt": ["paragraph", "reference", "blockquote", "list"]})
    md.inline.ruler.before("escape", "buddy_math", math_inline)
    tokens = md.parse(text)
    if not any(child.type.startswith('buddy_math_') for token in tokens for child in (token.children or [])):
        return None
    layout = MathLayout(ziamath, rasterize, convert, Image, report)
    headings = []
    md.renderer.rules["image"] = lambda ts, i, opts, env: "[" + escape(ts[i].content) + "]"
    md.renderer.rules["link_open"] = lambda *args: ""
    md.renderer.rules["link_close"] = lambda *args: ""
    md.renderer.rules["buddy_rendered"] = lambda ts, i, opts, env: ts[i].content
    table_depth = 0
    for index, token in enumerate(tokens):
        if token.type == "heading_open":
            anchor = f"heading-{len(headings)}"
            token.attrSet("id", anchor)
            headings.append((anchor, tokens[index + 1].content))
        elif token.type == 'table_open':
            table_depth += 1
        elif token.type == 'table_close':
            table_depth -= 1
        elif token.type == 'inline' and any(t.type.startswith('buddy_math_') for t in (token.children or [])):
            token.content = layout.render(token.children, table=bool(table_depth))
            token.type, token.children = 'buddy_rendered', None
    body = md.renderer.render(tokens, md.options, {})
    images = layout.images
    title = escape(title)
    xhtml = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + title +
             '</title></head><body>' + body + '</body></html>')
    manifest = ''.join(f'<item id="math-{i}" href="{name}" media-type="image/jpeg"/>'
                       for i, (name, _) in enumerate(images))
    opf = ('<?xml version="1.0" encoding="UTF-8"?>'
           '<package xmlns="http://www.idpf.org/2007/opf" version="2.0" unique-identifier="id">'
           '<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="id">buddy-math</dc:identifier>'
           '<dc:title>' + title + '</dc:title><dc:language>en</dc:language></metadata><manifest>'
           '<item id="text" href="content.xhtml" media-type="application/xhtml+xml"/>'
           '<item id="toc" href="toc.ncx" media-type="application/x-dtbncx+xml"/>' + manifest +
           '</manifest><spine toc="toc"><itemref idref="text"/></spine></package>')
    navigation = [("", title)] + [("#" + anchor, escape(label)) for anchor, label in headings]
    toc = ('<?xml version="1.0" encoding="UTF-8"?>'
           '<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head>'
           '<meta name="dtb:uid" content="buddy-math"/></head><docTitle><text>' + title +
           '</text></docTitle><navMap>' + ''.join(
               f'<navPoint id="n{i}" playOrder="{i+1}"><navLabel><text>{label}</text></navLabel>'
               f'<content src="content.xhtml{anchor}"/></navPoint>'
               for i, (anchor, label) in enumerate(navigation)) + '</navMap></ncx>')
    container = ('<?xml version="1.0"?><container version="1.0" '
                 'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
                 '<rootfile full-path="content.opf" media-type="application/oebps-package+xml"/>'
                 '</rootfiles></container>')
    output = BytesIO()
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_STORED) as book:
        for name, data in [("mimetype", "application/epub+zip"), ("META-INF/container.xml", container),
                           ("content.opf", opf), ("toc.ncx", toc), ("content.xhtml", xhtml), *images]:
            book.writestr(zipfile.ZipInfo(name), data)  # Fixed timestamps make output reproducible.
    payload = output.getvalue()
    if len(payload) + HEADER.size > MAX_BUNDLE:
        raise ValueError("Rendered note exceeds the 8 MiB limit")
    return HEADER.pack(MAGIC, len(source), fnv(source), len(payload), fnv(payload)) + payload
