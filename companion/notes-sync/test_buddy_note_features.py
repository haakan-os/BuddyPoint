from io import BytesIO
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from buddy_math import HEADER, render_note, sidecar_name
from buddy_note_assets import collect_assets, local_asset, asset_fingerprint
from buddy_note_tools import render_tools, tools_name, resolve_note
from buddy_notes_sync import Sync
from test_buddy_notes_sync import FakeReader


class NoteFeaturesTests(unittest.TestCase):
    def image(self, color='black'):
        from PIL import Image
        out = BytesIO()
        Image.new('RGBA', (900, 600), color).save(out, 'PNG')
        return out.getvalue()

    def test_images_standard_wiki_nested_and_code(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'assets').mkdir()
            (root / 'assets' / 'a b.png').write_bytes(self.image())
            source = b'![Chart](../assets/a%20b.png)\n\n![[assets/a b.png|300]]\n\n`![No](missing.png)`'
            messages = []
            assets = collect_assets(source, root, 'School/Note.md', messages.append)
            self.assertEqual(messages, [])
            self.assertEqual(len(assets), 2)
            bundle = render_note(source, 'Image only', assets=assets)
            with zipfile.ZipFile(BytesIO(bundle[HEADER.size:])) as book:
                from PIL import Image
                pictures = [n for n in book.namelist() if n.endswith('.jpg')]
                self.assertEqual(len(pictures), 2)
                for name in pictures:
                    im = Image.open(BytesIO(book.read(name)))
                    self.assertLessEqual(im.width, 420)
                    self.assertLessEqual(im.height, 500)
                    self.assertEqual(im.mode, 'L')
                    self.assertFalse(im.info.get('progressive'))

    def test_remote_traversal_hidden_and_symlinks_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for target in ('https://example.com/a.png', '../out.png', '/out.png', '.secret/a.png', 'file:///tmp/a.png'):
                with self.subTest(target=target), self.assertRaises(ValueError):
                    local_asset(root, 'Note.md', target)
            (root / 'real.png').write_bytes(self.image())
            try:
                (root / 'link.png').symlink_to(root / 'real.png')
            except OSError:
                return  # Windows runners may not have symlink privileges.
            with self.assertRaises(ValueError):
                local_asset(root, 'Note.md', 'link.png')

    def test_image_with_inline_math_remains_an_image(self):
        source = b'A $x$ diagram ![Picture](a.png)'
        bundle = render_note(source, 'Mixed', assets={'a.png': self.image()})
        with zipfile.ZipFile(BytesIO(bundle[32:])) as book:
            self.assertIn(b'asset-', book.read('content.xhtml'))

    def test_changed_image_and_size_refresh_unchanged_note(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder, state = root / 'notes', root / 'state'
            folder.mkdir()
            source = b'![Image](a.png)\n\n$x^2$'
            (folder / 'note.md').write_bytes(source)
            (folder / 'a.png').write_bytes(self.image())
            reader = FakeReader()
            messages = []
            def sync(size=26):
                Sync(folder, reader, state, math=True, math_size=size, report=messages.append).run()
                return reader.files[sidecar_name('note.md')]
            first = sync()
            self.assertEqual(first, sync())
            (folder / 'a.png').write_bytes(self.image('white'))
            second = sync()
            self.assertNotEqual(first, second)
            self.assertNotEqual(second, sync(34))
            self.assertEqual((folder / 'note.md').read_bytes(), source)
            self.assertTrue(any(m.startswith('Summary:') for m in messages))

    def test_links_cards_metadata_and_code_exclusion(self):
        names = {'School/A.md', 'School/B.md', 'Other/C.md'}
        source = b'[[B|Next]] [C](../Other/C.md) ![image](B.md)\nWhat is 2+2? :: 4\n```\nHidden :: Answer\n[[C]]\n```'
        bundle = render_tools(source, 'School/A.md', names, '/Obsidian')
        payload = bundle[32:]
        self.assertEqual(struct.unpack('<H', payload[:2])[0], 3)
        self.assertIn(b'/Obsidian/School/B.md', payload)
        self.assertIn(b'/Obsidian/Other/C.md', payload)
        self.assertNotIn(b'Hidden', payload)
        self.assertEqual(resolve_note('School/A.md', 'https://x/B.md', names), None)
        self.assertEqual(resolve_note('A.md', 'B', {'X/B.md', 'Y/B.md'}), None)
        self.assertTrue(tools_name('School/A.md').startswith('School/'))

    def test_tools_sync_update_and_dry_run(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            folder, state = root / 'notes', root / 'state'
            folder.mkdir()
            (folder / 'note.md').write_text('Question :: Answer')
            reader = FakeReader()
            Sync(folder, reader, state, dry_run=True, report=lambda _: None).run()
            self.assertEqual(reader.files, {})
            Sync(folder, reader, state, report=lambda _: None).run()
            self.assertIn(tools_name('note.md'), reader.files)
            (folder / 'note.md').write_text('No cards')
            Sync(folder, reader, state, report=lambda _: None).run()
            self.assertEqual(len(reader.files[tools_name('note.md')]), 34)

    def test_unique_attachment_subfolder_and_ambiguity(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'Attachments').mkdir()
            image = self.image()
            (root / 'Attachments/chart.png').write_bytes(image)
            self.assertEqual(local_asset(root, 'Note.md', 'chart.png'), image)
            (root / 'Other').mkdir()
            (root / 'Other/chart.png').write_bytes(image)
            with self.assertRaisesRegex(ValueError, 'Ambiguous'):
                local_asset(root, 'Note.md', 'chart.png')

    def test_corrupt_image_keeps_description_and_valid_math(self):
        warnings = []
        bundle = render_note(b'![Diagram](bad.png)\n\n$$x=1$$', 'Bad image', warnings.append,
                             assets={'bad.png': b'not an image'})
        with zipfile.ZipFile(BytesIO(bundle[32:])) as book:
            self.assertIn(b'[Diagram]', book.read('content.xhtml'))
            self.assertIn('math-0.jpg', book.namelist())
        self.assertTrue(warnings)

    def test_card_inline_code_preserved_but_code_links_ignored(self):
        bundle = render_tools(b'What is `x`? :: A variable\n`[[Next]]`', 'A.md', {'A.md', 'Next.md'}, '/Notes')
        self.assertEqual(struct.unpack('<H', bundle[32:34])[0], 1)
        self.assertIn(b'What is `x`?', bundle)
