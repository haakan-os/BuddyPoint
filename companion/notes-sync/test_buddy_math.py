from io import BytesIO
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import xml.etree.ElementTree as ET
import zipfile

from buddy_math import HEADER, MAGIC, fnv, render_note, sidecar_name
from buddy_notes_sync import Sync, SyncError, atomic_write, digest
from test_buddy_notes_sync import FakeReader


class MathTests(unittest.TestCase):
    def book(self, source, messages=None):
        bundle = render_note(source, "Notes & equations", (messages if messages is not None else []).append)
        magic, size, source_hash, length, payload_hash = HEADER.unpack(bundle[:32])
        self.assertEqual((magic, size, source_hash), (MAGIC, len(source), fnv(source)))
        payload = bundle[32:]
        self.assertEqual((length, payload_hash), (len(payload), fnv(payload)))
        return bundle, zipfile.ZipFile(BytesIO(payload))

    def test_equations_are_baseline_grayscale_jpegs_in_valid_epub_with_navigation(self):
        from PIL import Image
        source = b'# Equations\n\n$$\nx = \\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}\n$$\n\n$$E=mc^2$$\n'
        bundle, book = self.book(source)
        self.assertEqual(book.namelist()[0], "mimetype")
        self.assertEqual(book.read("mimetype"), b"application/epub+zip")
        for name in ("content.xhtml", "content.opf", "toc.ncx", "META-INF/container.xml"):
            ET.fromstring(book.read(name))
        self.assertIn(b"content.xhtml#heading-0", book.read("toc.ncx"))
        for name in ("math-0.jpg", "math-1.jpg"):
            image = Image.open(BytesIO(book.read(name)))
            self.assertEqual(image.mode, "L")
            self.assertLessEqual(image.width, 432)
            self.assertEqual(image.format, "JPEG")
            self.assertFalse(image.info.get("progressive"))
            self.assertFalse(image.info.get("progression"))
            self.assertEqual(image.getextrema(), (0, 255))
            pixels = list(image.getdata())
            self.assertTrue(all(pixel <= 8 or pixel >= 247 for pixel in pixels))
        self.assertIn(b'media-type="image/jpeg"', book.read("content.opf"))
        self.assertEqual(render_note(source, "Notes & equations"), bundle)

    def test_code_and_inline_math_and_unclosed_blocks_are_unchanged(self):
        for source in (b'```tex\n$$x^2$$\n```', b'    $$x^2$$\n', b'Text $x^2$ and `$$x$$`.',
                       b'$$\nx^2\n', b'Costs $$5 today.'):
            self.assertIsNone(render_note(source, "Code"))

    def test_unsupported_math_falls_back_and_external_content_is_not_loaded(self):
        messages = []
        _, book = self.book(b'$$\\notacommand{x}$$\n\n![pic](assets/local.png)\n'
                            b'[label](https://example.com)\n<script>alert(1)</script>\n', messages)
        body = book.read("content.xhtml")
        self.assertTrue(messages)
        self.assertIn(b"\\notacommand", body)
        self.assertIn(b"[pic]", body)
        self.assertNotIn(b"assets/local.png", body)
        self.assertNotIn(b"<script>", body)
        ET.fromstring(body)

    def test_filename_protocol_vector_and_nested_names(self):
        self.assertEqual(sidecar_name("Notes.md"), "buddy-math-e8cbc25883199015.bmath")
        self.assertEqual(sidecar_name("Work/Notes.md"), "Work/" + sidecar_name("Notes.md"))

    def test_indented_lists_and_quotes(self):
        for source in (b'> $$x^2$$\n', b'- item\n\n  $$x^2$$\n'):
            _, book = self.book(source)
            self.assertIn("math-0.jpg", book.namelist())

    def test_expression_limit_preserves_text(self):
        messages = []
        _, book = self.book(b'$$' + b'x' * 4097 + b'$$', messages)
        self.assertTrue(messages)
        self.assertNotIn("math-0.jpg", book.namelist())


class MathSyncTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.notes, self.history = self.root / "notes", self.root / "state"
        (self.notes / "Work").mkdir(parents=True)
        self.name = "Work/note.md"
        self.source = b'# Note\n\n$$x^2$$\n\n- [ ] task\n'
        (self.notes / self.name).write_bytes(self.source)
        self.reader = FakeReader()
        self.messages = []

    def sync(self, dry=False):
        return Sync(self.notes, self.reader, self.history, dry, self.messages.append, math=True).run()

    def test_sync_preserves_source_and_caches_math(self):
        self.sync()
        self.assertEqual((self.notes / self.name).read_bytes(), self.source)
        self.assertEqual(self.reader.files[self.name], self.source)
        self.assertIn(sidecar_name(self.name), self.reader.files)
        self.assertFalse((self.notes / sidecar_name(self.name)).exists())
        self.reader.writes.clear()
        with patch("buddy_math.render_note", side_effect=AssertionError("should use cache")):
            self.sync()
        self.assertEqual(self.reader.writes, [])

    def test_checklist_edit_refreshes_equations_and_downloads_original(self):
        self.sync()
        old = self.reader.files[sidecar_name(self.name)]
        changed = self.source.replace(b'[ ]', b'[x]')
        self.reader.files[self.name] = changed
        self.sync()
        self.assertEqual((self.notes / self.name).read_bytes(), changed)
        self.assertNotEqual(old, self.reader.files[sidecar_name(self.name)])

    def test_dry_run_writes_nothing(self):
        self.sync(dry=True)
        self.assertEqual(self.reader.files, {})
        self.assertFalse(self.history.exists())

    def test_partial_sidecar_upload_is_recoverable(self):
        Sync(self.notes, self.reader, self.history, report=self.messages.append).run()
        self.reader.partial_upload = True
        with self.assertRaises(SyncError):
            self.sync()
        self.assertNotIn(sidecar_name(self.name), self.reader.files)
        self.assertEqual(self.reader.files[self.name], self.source)
        self.reader.partial_upload = False
        self.sync()
        self.assertTrue(self.reader.files[sidecar_name(self.name)].startswith(MAGIC))
        self.assertFalse((self.history / 'pending.json').exists())

    def test_conflicting_versions_both_get_math(self):
        self.reader.files[self.name] = b'$$y^2$$\n'
        self.sync()
        for name in self.reader.notes():
            self.assertIn(sidecar_name(name), self.reader.files)

    def test_old_renderer_cache_refreshes_unchanged_note_automatically(self):
        self.sync()
        target = sidecar_name(self.name)
        # Model a v1 PNG bundle left on the reader and in the old local cache.
        old_bundle = MAGIC + b"old generated PNG reading copy"
        self.reader.files[target] = old_bundle
        for cache in (self.history / "math").glob("*.bmath"):
            cache.unlink()
        old_key = digest(b"buddy-math-v1\0" + self.name.encode() + b"\0" + self.source)
        atomic_write(self.history / "math" / (old_key + ".bmath"), old_bundle)
        self.reader.writes.clear()
        self.assertEqual(self.sync(), 0)  # Original note does not need a transfer.
        updated = self.reader.files[target]
        self.assertNotEqual(updated, old_bundle)
        with zipfile.ZipFile(BytesIO(updated[32:])) as book:
            self.assertIn("math-0.jpg", book.namelist())
        self.assertEqual(self.reader.files[self.name], self.source)
        self.assertTrue(any("Upload equations" in message for message in self.messages))
