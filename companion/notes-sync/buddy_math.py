"""Optional, offline display-math rendering for BuddyPoint (no JavaScript or TeX)."""
from html import escape
from io import BytesIO
from pathlib import Path, PurePosixPath
import sys
import shlex
import struct
import zipfile

MAGIC = b"BUDMATH1"
HEADER = struct.Struct("<8sIQIQ")
MAX_BUNDLE = 8 * 1024 * 1024
MAX_EQUATIONS = 128
MAX_EXPRESSION = 4096


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
        from matplotlib.mathtext import MathTextParser
        from matplotlib.font_manager import FontProperties
        from PIL import Image, ImageOps
        import numpy
    except ImportError as error:
        command = shlex.join([sys.executable, "-m", "pip", "install", "-r",
                              str(Path(__file__).resolve().with_name("requirements-math.txt"))])
        raise ValueError(f"Math rendering needs the optional packages. Run: {command}") from error
    return MarkdownIt, MathTextParser, FontProperties, Image, ImageOps, numpy


def math_block(state, start, end, silent):
    """Only standalone $$ blocks; markdown-it shields fenced/indented code."""
    if state.sCount[start] - state.blkIndent >= 4:
        return False
    line = state.src[state.bMarks[start] + state.tShift[start]:state.eMarks[start]].strip()
    if not line.startswith("$$"):
        return False
    if len(line) > 4 and line.endswith("$$"):
        expression, stop = line[2:-2], start + 1
    elif line == "$$":
        stop = start + 1
        while stop < end:
            if state.sCount[stop] < state.blkIndent:
                return False
            closing = state.src[state.bMarks[stop] + state.tShift[stop]:state.eMarks[stop]].strip()
            if closing == "$$":
                break
            stop += 1
        if stop == end:
            return False
        expression = state.getLines(start + 1, stop, state.blkIndent, False)
        stop += 1
    else:
        return False
    if silent:
        return True
    token = state.push("buddy_math", "", 0)
    token.block, token.content, token.map = True, expression.strip(), [start, stop]
    state.line = stop
    return True


def render_note(source, title, report=lambda message: None):
    """Return a checked EPUB sidecar or None. The source bytes are never rewritten."""
    MarkdownIt, MathTextParser, FontProperties, Image, ImageOps, numpy = dependencies()
    try:
        text = source.decode("utf-8-sig")
    except UnicodeDecodeError as error:
        raise ValueError("Math notes must use UTF-8 text") from error
    md = MarkdownIt("commonmark", {"html": False, "xhtmlOut": True}).enable("table")
    md.block.ruler.before("fence", "buddy_math", math_block, {"alt": ["paragraph", "reference", "blockquote", "list"]})
    tokens = md.parse(text)
    if not any(token.type == "buddy_math" for token in tokens):
        return None
    images, headings = [], []
    parser = MathTextParser("agg")
    measure = MathTextParser("path")
    prop = FontProperties(size=20)

    def render_math(tokens, index, options, env):
        expression = tokens[index].content
        try:
            if len(expression) > MAX_EXPRESSION or len(images) >= MAX_EQUATIONS:
                raise ValueError("equation length/count limit exceeded")
            # No usetex/subprocess/network: MathText parses a bounded TeX subset.
            math = "$" + " ".join(expression.splitlines()) + "$"
            size = measure.parse(math, dpi=100, prop=prop)
            if size.width > 4096 or size.height > 4096:
                raise ValueError("equation is too large")
            result = parser.parse(math, dpi=100, prop=prop)
            image = Image.fromarray(numpy.asarray(result.image)).convert("L")
            image = ImageOps.invert(image)
            image.thumbnail((420, 500))
            image = ImageOps.expand(image, border=6, fill=255)
            image = image.point(lambda pixel: 255 if pixel >= 160 else 0)
            png = BytesIO()
            image.save(png, format="PNG")  # 8-bit grayscale, supported by firmware.
            name = f"math-{len(images)}.png"
            images.append((name, png.getvalue()))
            return f'<p><img src="{name}" alt="{escape(expression, quote=True)}" /></p>\n'
        except (ValueError, RuntimeError, OverflowError, RecursionError) as error:
            report(f"Equation kept as text: {str(error).strip()}")
            return "<pre>" + escape("$$\n" + expression + "\n$$") + "</pre>\n"

    md.renderer.rules["buddy_math"] = render_math
    # Match the offline reader: don't load remote/local pictures or activate links.
    md.renderer.rules["image"] = lambda ts, i, opts, env: "[" + escape(ts[i].content) + "]"
    md.renderer.rules["link_open"] = lambda *args: ""
    md.renderer.rules["link_close"] = lambda *args: ""
    for index, token in enumerate(tokens):
        if token.type == "heading_open":
            anchor = f"heading-{len(headings)}"
            token.attrSet("id", anchor)
            headings.append((anchor, tokens[index + 1].content))
    body = md.renderer.render(tokens, md.options, {})
    title = escape(title)
    xhtml = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>' + title +
             '</title></head><body>' + body + '</body></html>')
    manifest = ''.join(f'<item id="math-{i}" href="{name}" media-type="image/png"/>'
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
