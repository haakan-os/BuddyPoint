import copy
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import tempfile
import threading
import unittest
from urllib.parse import parse_qs, urlsplit

from buddy_notes_sync import Reader, Sync, SyncError, check_collisions, digest, safe_name, sync_lock


class FakeReader:
    address = "http://reader.test"
    folder = "/OneDriveNotes"

    def __init__(self):
        self.files = {}
        self.writes = []
        self.fail_promote = False
        self.partial_upload = False

    def status(self):
        return {"device": "X3", "version": "1.6.7"}

    def ensure_folder(self, dry_run=False):
        pass

    def notes(self):
        return {name for name in self.files if Path(name).suffix.lower() in {".md", ".markdown"}}

    def read(self, name):
        return self.files.get(name)

    def upload(self, name, data):
        self.writes.append(("upload", name))
        self.files[name] = data[:1] if self.partial_upload else data

    def rename(self, old, new):
        if self.fail_promote and old.endswith(".part"):
            raise SyncError("Simulated dropped connection")
        if new in self.files:
            raise SyncError("Target exists")
        self.writes.append(("rename", old, new))
        self.files[new] = self.files.pop(old)

    def remove_scratch(self, name):
        self.writes.append(("remove", name))
        del self.files[name]


class SyncTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.notes = self.root / "notes"
        self.notes.mkdir()
        self.history = self.root / "state"
        self.reader = FakeReader()
        self.messages = []

    def run_sync(self, dry=False):
        return Sync(self.notes, self.reader, self.history, dry, self.messages.append).run()

    def write(self, data, name="note.md"):
        (self.notes / name).write_bytes(data)

    def baseline(self):
        self.write(b"- [ ] task\r\n")
        self.run_sync()
        self.reader.writes.clear()

    def test_upload_and_no_changes_next_time(self):
        self.write(b"# Notes\n")
        self.assertEqual(self.run_sync(), 1)
        self.assertEqual(self.reader.files, {"note.md": b"# Notes\n"})
        self.reader.writes.clear()
        self.assertEqual(self.run_sync(), 0)
        self.assertEqual(self.reader.writes, [])

    def test_download_preserves_unicode_and_crlf(self):
        self.reader.files["日本語.md"] = "# Café\r\n- [x] 読む\r\n".encode()
        self.run_sync()
        self.assertEqual((self.notes / "日本語.md").read_bytes(), self.reader.files["日本語.md"])

    def test_reader_checklist_change_downloads(self):
        self.baseline()
        self.reader.files["note.md"] = b"- [x] task\r\n"
        self.run_sync()
        self.assertEqual((self.notes / "note.md").read_bytes(), b"- [x] task\r\n")

    def test_computer_change_uploads(self):
        self.baseline()
        self.write(b"new desktop text")
        self.run_sync()
        self.assertEqual(self.reader.files["note.md"], b"new desktop text")

    def test_both_changed_keep_both_on_both_devices(self):
        self.baseline()
        self.write(b"computer")
        self.reader.files["note.md"] = b"reader"
        self.run_sync()
        copies = [name for name in self.reader.files if "reader-conflict" in name]
        self.assertEqual(len(copies), 1)
        self.assertEqual(self.reader.files[copies[0]], b"reader")
        self.assertEqual((self.notes / copies[0]).read_bytes(), b"reader")
        self.assertEqual(self.reader.files["note.md"], b"computer")
        self.assertEqual(self.run_sync(), 0)

    def test_first_sync_different_copies_is_conflict(self):
        self.write(b"computer")
        self.reader.files["note.md"] = b"reader"
        self.run_sync()
        self.assertEqual(len(self.reader.files), 2)

    def test_deletions_restore_missing_copy(self):
        self.baseline()
        del self.reader.files["note.md"]
        self.run_sync()
        self.assertIn("note.md", self.reader.files)
        (self.notes / "note.md").unlink()
        self.run_sync()
        self.assertTrue((self.notes / "note.md").exists())

    def test_empty_notes_sync(self):
        self.write(b"")
        self.run_sync()
        self.assertEqual(self.reader.files["note.md"], b"")
        self.write(b"content")
        self.run_sync()
        self.assertEqual(self.reader.files["note.md"], b"content")

    def test_dry_run_does_not_write_notes_history_or_reader(self):
        self.write(b"computer")
        self.reader.files["note.md"] = b"reader"
        before = copy.deepcopy(self.reader.files)
        self.run_sync(True)
        self.assertEqual(self.reader.files, before)
        self.assertEqual(self.reader.writes, [])
        self.assertEqual(list(self.notes.iterdir()), [self.notes / "note.md"])
        self.assertFalse(self.history.exists())

    def test_failed_upload_preserves_original_and_recovers(self):
        self.baseline()
        self.write(b"new")
        self.reader.partial_upload = True
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertEqual(self.reader.files["note.md"], b"- [ ] task\r\n")
        self.reader.partial_upload = False
        self.run_sync()
        self.assertEqual(self.reader.files, {"note.md": b"new"})

    def test_disconnect_after_original_rename_recovers(self):
        self.baseline()
        self.write(b"new")
        self.reader.fail_promote = True
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertTrue(any(name.endswith(".backup") for name in self.reader.files))
        self.reader.fail_promote = False
        self.run_sync()
        self.assertEqual(self.reader.files, {"note.md": b"new"})

    def test_reader_change_during_upload_is_not_overwritten(self):
        self.baseline()
        self.write(b"desktop")
        upload = self.reader.upload

        def edit_during_upload(name, data):
            upload(name, data)
            self.reader.files["note.md"] = b"concurrent reader edit"

        self.reader.upload = edit_during_upload
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertEqual(self.reader.files["note.md"], b"concurrent reader edit")

    def test_lost_reply_after_promotion_recovers_completed_upload(self):
        self.baseline()
        self.write(b"new")
        rename = self.reader.rename

        def lose_reply(old, new):
            rename(old, new)
            if old.endswith(".part"):
                raise SyncError("Reply lost after successful rename")

        self.reader.rename = lose_reply
        with self.assertRaises(SyncError):
            self.run_sync()
        self.reader.rename = rename
        self.run_sync()
        self.assertEqual(self.reader.files, {"note.md": b"new"})
        self.assertFalse((self.history / "pending.json").exists())

    def test_dry_run_does_not_recover_interrupted_upload(self):
        self.baseline()
        self.write(b"new")
        self.reader.fail_promote = True
        with self.assertRaises(SyncError):
            self.run_sync()
        before = copy.deepcopy(self.reader.files)
        with self.assertRaises(SyncError):
            self.run_sync(True)
        self.assertEqual(self.reader.files, before)

    def test_note_size_limit_preserves_reader(self):
        self.write(b"x" * (8 * 1024 * 1024 + 1))
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertFalse(self.reader.writes)

    def test_change_between_remote_check_and_rename_is_restored(self):
        self.baseline()
        self.write(b"desktop")
        rename = self.reader.rename

        def edit_before_rename(old, new):
            if old == "note.md":
                self.reader.files[old] = b"last second edit"
            rename(old, new)

        self.reader.rename = edit_before_rename
        with self.assertRaises(SyncError):
            self.run_sync()
        Sync(self.notes, self.reader, self.history, report=self.messages.append).recover()
        self.assertEqual(self.reader.files["note.md"], b"last second edit")

    def test_local_change_during_download_is_not_overwritten(self):
        self.baseline()
        self.reader.files["note.md"] = b"reader"
        read = self.reader.read

        def edit_on_read(name):
            self.write(b"concurrent desktop edit")
            return read(name)

        self.reader.read = edit_on_read
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertEqual((self.notes / "note.md").read_bytes(), b"concurrent desktop edit")

    def test_corrupt_history_fails_before_transfer(self):
        self.history.mkdir()
        (self.history / "state.json").write_text("broken")
        with self.assertRaises(SyncError):
            self.run_sync()
        self.assertFalse(self.reader.writes)

    def test_different_device_does_not_reuse_history(self):
        self.baseline()
        self.reader.address = "http://other.test"
        with self.assertRaises(SyncError):
            self.run_sync()

    def test_backups_contain_both_versions(self):
        self.baseline()
        self.write(b"new")
        self.run_sync()
        self.assertEqual((self.history / "backups" / (digest(b"new") + ".md")).read_bytes(), b"new")
        self.assertTrue((self.history / "backups" / (digest(b"- [ ] task\r\n") + ".md")).exists())

    def test_case_collisions_rejected(self):
        self.write(b"a", "NOTE.md")
        self.reader.files["note.md"] = b"b"
        with self.assertRaises(SyncError):
            self.run_sync()

    def test_symlink_rejected(self):
        target = self.root / "private.md"
        target.write_bytes(b"private")
        try:
            (self.notes / "note.md").symlink_to(target)
        except OSError:
            self.skipTest("Symlinks unavailable")
        with self.assertRaises(SyncError):
            self.run_sync()

    def test_non_markdown_and_subfolders_ignored(self):
        self.write(b"private", "other.txt")
        (self.notes / "subfolder").mkdir()
        (self.notes / "subfolder" / "nested.md").write_bytes(b"nested")
        self.assertEqual(self.run_sync(), 0)

    def test_second_process_lock_rejected(self):
        with sync_lock(self.history):
            with self.assertRaises(SyncError):
                with sync_lock(self.history):
                    pass

    def test_unsafe_names_and_urls_rejected(self):
        for name in ("../x.md", "a/b.md", "a\\b.md", 'a".md', "a\n.md", "CON.md", ".hidden.md", " space.md"):
            with self.subTest(name=name), self.assertRaises(SyncError):
                safe_name(name)
        with self.assertRaises(SyncError):
            Reader("http://user:pass@example.test")
        with self.assertRaises(SyncError):
            Reader("reader.test", "/BuddyNotes")
        with self.assertRaises(SyncError):
            check_collisions({"café.md", "cafe\u0301.md"})


class HttpTests(unittest.TestCase):
    def test_real_http_routes_encoding_and_multipart(self):
        received = []

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def do_GET(self):
                parsed = urlsplit(self.path)
                received.append((parsed.path, parse_qs(parsed.query)))
                if parsed.path == "/api/status":
                    body = b'{"device":"X3","version":"1.6.7"}'
                elif parsed.path == "/api/files":
                    body = b'[]'
                elif parsed.path == "/download":
                    body = b"# Unicode note"
                else:
                    self.send_error(404)
                    return
                self.send_response(200)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def do_POST(self):
                received.append((self.path, self.rfile.read(int(self.headers["Content-Length"]))))
                self.send_response(200)
                self.send_header("Content-Length", "2")
                self.end_headers()
                self.wfile.write(b"OK")

        server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            reader = Reader(f"127.0.0.1:{server.server_port}")
            self.assertEqual(reader.status()["device"], "X3")
            reader.ensure_folder()
            self.assertEqual(reader.read("日本語.md"), b"# Unicode note")
            reader.upload("café.md", b"- [x] done\r\n")
            reader.rename("old.md", "new.md")
            self.assertIn(("/download", {"path": ["/OneDriveNotes/日本語.md"]}), received)
            upload = next(body for path, body in received if path.startswith("/api/upload"))
            self.assertIn('filename="café.md"'.encode(), upload)
            self.assertIn(b"- [x] done\r\n", upload)
            self.assertTrue(any(path == "/mkdir" for path, _ in received))
        finally:
            server.shutdown()
            server.server_close()
            worker.join()


if __name__ == "__main__":
    unittest.main()
