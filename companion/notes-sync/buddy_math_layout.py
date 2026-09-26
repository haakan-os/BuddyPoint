"""Desktop-only layout of display maths and wrapped text containing inline maths."""
from html import escape
from io import BytesIO
import math
import re
import xml.etree.ElementTree as ET

WIDTH = 420
HEIGHT = 500
PADDING = 6
SIZE = 26
MAX_EXPRESSION = 4096
MAX_EQUATIONS = 512
MAX_IMAGES = 2048


def safe_mathml(convert, expression, inline):
    """Escape literal text emitted by latex2mathml before parsing it as XML."""
    # TeX's \\ followed by an escaped newline is still a row break.
    expression = re.sub(r'(?<!\\)\\{3}[ \t]*\r?\n', lambda match: r'\\' + '\n', expression)
    markup = convert(expression, display="inline" if inline else "block")
    # latex2mathml retains literal < and & inside \text{...}. Preserve its
    # character entities while quoting literal characters in those text nodes.
    def text_node(match):
        text = re.sub(r'&(?!#(?:x[0-9a-fA-F]+|[0-9]+);|(?:amp|lt|gt|quot|apos);)', '&amp;', match[1])
        return '<mtext>' + text.replace('<', '&lt;').replace('>', '&gt;') + '</mtext>'
    markup = re.sub(r'<mtext>(.*?)</mtext>', text_node, markup, flags=re.DOTALL)
    root = ET.fromstring(markup)
    # Python 3.9 uses latex2mathml 3.78, before cancellation support was added.
    # Complete that MathML representation while leaving newer converter output
    # untouched. Scripts following the cancelled atom stay outside its strike.
    strikes = {r'\cancel': 'updiagonalstrike', r'\bcancel': 'downdiagonalstrike',
               r'\xcancel': 'updiagonalstrike downdiagonalstrike'}
    tag = lambda node: node.tag.rsplit('}', 1)[-1]
    for parent in list(root.iter()):
        index = 0
        while index < len(parent):
            node = parent[index]
            if tag(node) == 'mi' and node.text in strikes:
                if index + 1 == len(parent):
                    raise ValueError('Cancellation needs an expression')
                target = parent[index + 1]
                enclosed = ET.Element('menclose', notation=strikes[node.text])
                if tag(target) in {'msup', 'msub', 'msubsup'}:
                    base = target[0]
                    target.remove(base)
                    enclosed.append(base)
                    target.insert(0, enclosed)
                    parent.remove(node)
                else:
                    parent.remove(target)
                    enclosed.append(target)
                    parent[index] = enclosed
            elif tag(node) == 'mtable':
                for adjacent in (index - 1, index + 1):
                    if 0 <= adjacent < len(parent) and tag(parent[adjacent]) == 'mo':
                        parent[adjacent].set('stretchy', 'true')
            index += 1
    # Unknown commands are otherwise silently drawn as their literal name.
    for node in root.iter():
        if node.tag.rsplit('}', 1)[-1] != 'mtext' and node.text and '\\' in node.text:
            raise ValueError(f"Unsupported LaTeX command: {node.text}")
    return root


class MathLayout:
    def __init__(self, ziamath, rasterize, convert, Image, report):
        self.zm, self.rasterize, self.convert, self.Image = ziamath, rasterize, convert, Image
        self.report = report
        self.images = []
        self.image_lookup = {}
        self.image_bytes = 0
        self.equations = 0
        self.math_cache = {}
        self.text_cache = {}

    def text(self, value, style='normal'):
        key = (value, style)
        if key not in self.text_cache:
            root = ET.Element('math')
            ET.SubElement(root, 'mtext', mathvariant=style).text = value
            if len(self.text_cache) >= 2048:
                self.text_cache.clear()
            self.text_cache[key] = self.zm.Math(root, size=SIZE)
        return self.text_cache[key]

    def equation(self, value, inline, count=True):
        self.equations += int(count)
        if self.equations > MAX_EQUATIONS or len(value) > MAX_EXPRESSION:
            raise ValueError('equation length/count limit exceeded')
        key = (value, inline)
        if key not in self.math_cache:
            root = safe_mathml(self.convert, value, inline)
            obj = self.zm.Math(root, size=SIZE)
            w, h = obj.getsize()
            if not all(math.isfinite(n) and 0 <= n <= 4096 for n in (w, h)):
                raise ValueError('equation is too large')
            self.math_cache[key] = obj
        return self.math_cache[key]

    def display_rows(self, value, obj):
        """Break wide equality chains at outer = signs, preserving the operators."""
        if obj.getsize()[0] <= WIDTH or any(marker in value for marker in (r'\begin', r'\left', r'\right')):
            return [(value, obj)]
        depth, starts = 0, [0]
        for index, char in enumerate(value):
            if char in '{([':
                depth += 1
            elif char in '})]':
                depth -= 1
            elif char == '=' and depth == 0 and index > 0:
                starts.append(index)
        if len(starts) == 1:
            return [(value, obj)]
        starts.append(len(value))
        pieces = [value[a:b] for a, b in zip(starts, starts[1:])]
        rows, current = [], pieces[0]
        current_obj = self.equation(current, False, count=False)
        for piece in pieces[1:]:
            combined = self.equation(current + piece, False, count=False)
            if combined.getsize()[0] <= WIDTH:
                current, current_obj = current + piece, combined
            else:
                rows.append((current, current_obj))
                current, current_obj = piece, self.equation(piece, False, count=False)
        rows.append((current, current_obj))
        return rows

    def image(self, atoms, label, full_width=False):
        # An atom is (math/text object, scale). All atoms share one baseline.
        ascent = max((obj.getsize()[1] + obj.getyofst()) * scale for obj, scale in atoms)
        descent = max(-obj.getyofst() * scale for obj, scale in atoms)
        width = WIDTH if full_width else sum(obj.getsize()[0] * scale for obj, scale in atoms)
        width, height = max(1, math.ceil(width)), max(1, math.ceil(ascent + descent))
        svg = ET.Element('svg', xmlns='http://www.w3.org/2000/svg',
                         width=str(width + PADDING * 2), height=str(height + PADDING * 2))
        x = PADDING
        for obj, scale in atoms:
            group = ET.SubElement(svg, 'g', transform=f'translate({x} {PADDING + ascent}) scale({scale})')
            obj.drawon(group, 0, 0)
            x += obj.getsize()[0] * scale
        pixels = self.rasterize(svg_string=ET.tostring(svg, encoding='unicode'),
                                background='white', skip_system_fonts=True)
        picture = self.Image.open(BytesIO(pixels)).convert('L')
        picture = picture.point(lambda pixel: 255 if pixel >= 160 else 0)
        output = BytesIO()
        picture.save(output, format='JPEG', quality=98, progressive=False, optimize=False)
        data = output.getvalue()
        name = self.image_lookup.get(data)
        if name is None:
            if len(self.images) >= MAX_IMAGES or self.image_bytes + len(data) > 7 * 1024 * 1024:
                raise ValueError('Rendered note exceeds the image size/count limit')
            name = f'math-{len(self.images)}.jpg'
            self.images.append((name, data))
            self.image_lookup[data] = name
            self.image_bytes += len(data)
        return f'<img src="{name}" alt="{escape(label, quote=True)}" />\n'

    @staticmethod
    def scale(obj):
        w, h = obj.getsize()
        return min(1.0, WIDTH / max(1, w), HEIGHT / max(1, h))

    def render(self, children, table=False):
        """Keep prose and inline formulas on shared baselines; allow page breaks per row."""
        if table:
            self.report('Math in a table is kept as text; the reader does not display images in table cells.')
            return ''.join(escape(t.content) for t in children if t.type in
                           {'text', 'code_inline', 'buddy_math_inline', 'buddy_math_display'})
        output, row, labels = [], [], []
        width = 0.0
        bold = italic = 0

        def flush():
            nonlocal row, labels, width
            if row:
                output.append(self.image(row, ''.join(labels), full_width=True))
            row, labels, width = [], [], 0.0

        def add(obj, label, space=False):
            nonlocal width
            if space and not row:
                return
            scale = self.scale(obj)
            advance = obj.getsize()[0] * scale
            if row and width + advance > WIDTH:
                flush()
                if space:
                    return
            row.append((obj, scale))
            labels.append(label)
            width += advance

        def prose(value, style):
            for word in re.findall(r'\s+|\S+', value):
                blank = word.isspace()
                word = ' ' if blank else word
                obj = self.text(word, style)
                # Long URLs/words wrap at characters instead of becoming tiny.
                if obj.getsize()[0] > WIDTH:
                    for char in word:
                        add(self.text(char, style), char)
                else:
                    add(obj, word, blank)

        for token in children:
            style = 'bold-italic' if bold and italic else 'bold' if bold else 'italic' if italic else 'normal'
            if token.type in {'buddy_math_inline', 'buddy_math_display'}:
                display = token.type == 'buddy_math_display'
                try:
                    obj = self.equation(token.content, not display)
                except MemoryError:
                    raise
                except Exception as error:
                    # Optional rendering must not stop syncing because of malformed
                    # input; latex2mathml uses several custom Exception subclasses.
                    self.report(f'Equation kept as text: {str(error).strip() or type(error).__name__}')
                    flush()
                    output.append('<code>' + escape(token.markup + token.content + token.markup) + '</code>')
                    continue
                if display:
                    flush()
                    for label, part in self.display_rows(token.content, obj):
                        output.append(self.image([(part, self.scale(part))], label))
                else:
                    add(obj, token.content)
            elif token.type == 'text':
                prose(token.content, style)
            elif token.type == 'code_inline':
                prose(token.content, 'monospace')
            elif token.type == 'image':
                prose('[' + token.content + ']', style)
            elif token.type == 'softbreak':
                prose(' ', style)
            elif token.type == 'hardbreak':
                flush()
            elif token.type == 'strong_open':
                bold += 1
            elif token.type == 'strong_close':
                bold -= 1
            elif token.type == 'em_open':
                italic += 1
            elif token.type == 'em_close':
                italic -= 1
        flush()
        return ''.join(output)
