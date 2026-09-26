import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from urllib.request import Request, urlopen

from buddy_notes_gui import Controller, DEFAULTS, Server, validate
from buddy_notes_sync import SyncError, sync_lock
from test_buddy_notes_sync import FakeReader


class GuiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve()
        self.folder = self.root / 'notes'
        self.folder.mkdir()
        (self.folder / 'test.md').write_text('# Notes')
        self.settings = dict(DEFAULTS, folder=str(self.folder))
        self.reader = FakeReader()
        self.controller = Controller(self.root / 'history', reader_type=lambda *a, **k: self.reader)

    def finish(self):
        self.controller.thread.join(5)
        self.assertFalse(self.controller.thread.is_alive())
        return self.controller.snapshot()

    def test_one_shot_uses_sync_engine_and_remembers_preferences(self):
        self.controller.start(self.settings, 'once')
        state = self.finish()
        self.assertEqual(state['status'], 'Sync complete')
        self.assertEqual(state['summary']['uploaded'], 1)
        self.assertEqual(self.reader.files['test.md'], b'# Notes')
        restarted = Controller(self.root / 'history')
        self.assertEqual(restarted.settings['folder'], str(self.folder))
        self.assertIsNone(restarted.thread)

    def test_preview_does_not_upload(self):
        self.controller.start(self.settings, 'preview')
        state = self.finish()
        self.assertEqual(state['status'], 'Preview complete')
        self.assertEqual(self.reader.files, {})
        self.assertEqual(state['summary']['uploaded'], 1)

    def test_invalid_settings_and_stale_preferences(self):
        for change in ({'folder': ''}, {'interval': 1}, {'math_size': 99}, {'math': 'true'},
                       {'reader_folder': '../escape'}, {'device': 'https://reader/path'}):
            with self.subTest(change=change), self.assertRaises((ValueError, SyncError)):
                validate(dict(self.settings, **change))
        self.controller.home.mkdir()
        (self.controller.home / 'gui.json').write_text('broken json')
        self.assertEqual(Controller(self.controller.home).settings, DEFAULTS)

    def test_stop_completes_current_transaction_and_rejects_second_start(self):
        entered, release, finished = threading.Event(), threading.Event(), threading.Event()
        class FakeSync:
            def __init__(self, *a, **kw):
                self.report = kw['report']
                self.summary = {'uploaded': 1}
            def run(self):
                entered.set()
                release.wait(5)  # An in-flight upload is not forcibly killed.
                finished.set()
                self.report('Comparing note 2/2: next.md')
        self.controller.sync_type = FakeSync
        self.controller.start(self.settings, 'watch')
        self.assertTrue(entered.wait(3))
        with self.assertRaisesRegex(ValueError, 'already running'):
            self.controller.start(self.settings, 'once')
        self.controller.stop()
        self.assertFalse(finished.is_set())
        release.set()
        state = self.finish()
        self.assertTrue(finished.is_set())
        self.assertEqual(state['status'], 'Stopped')
        self.assertEqual(state['error'], '')

    def test_error_is_visible_and_logs_are_bounded(self):
        self.reader.status = lambda: (_ for _ in ()).throw(SyncError('Reader unavailable'))
        self.controller.start(self.settings, 'once')
        state = self.finish()
        self.assertEqual(state['status'], 'Needs attention')
        self.assertIn('Reader unavailable', state['error'])
        for i in range(600):
            self.controller.report(str(i))
        self.assertEqual(len(self.controller.snapshot()['logs']), 500)

    def test_shared_cli_lock_is_honoured(self):
        import hashlib
        key = hashlib.sha256(str(self.folder).encode()).hexdigest()[:24]
        with sync_lock(self.controller.home / key):
            self.controller.start(self.settings, 'once')
            state = self.finish()
        self.assertIn('Another sync', state['error'])
        self.assertEqual(self.reader.files, {})


class GuiHttpTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.controller = Controller(Path(self.temp.name))
        self.server = Server(self.controller)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()

    def tearDown(self):
        self.controller.stop()
        self.server.shutdown()
        self.server.server_close()
        self.worker.join(3)
        self.temp.cleanup()

    def request(self, path, data=None, token=True, origin=None):
        headers = {}
        if token:
            headers['Authorization'] = 'Bearer ' + self.server.token
        if origin:
            headers['Origin'] = origin
        if data is not None:
            headers['Content-Type'] = 'application/json'
            data = json.dumps(data).encode()
        return urlopen(Request(self.server.origin + path, data=data, headers=headers), timeout=3)

    def test_private_api_rejects_missing_token_and_foreign_origin(self):
        for path, data, token, origin in [('/api/state', None, False, None),
                                         ('/api/stop', {}, False, None),
                                         ('/api/folder', {}, True, 'https://other.example')]:
            with self.subTest(path=path), self.assertRaises(HTTPError) as error:
                self.request(path, data, token, origin)
            self.assertEqual(error.exception.code, 403)
        with self.request('/api/state') as response:
            self.assertEqual(json.load(response)['status'], 'Ready')

    def test_static_ui_guide_and_unknown_paths(self):
        for path, content in [('/', b'Your notes'), ('/app.js', b'Authorization'),
                              ('/guide', b'Flashcards')]:
            with self.request(path, token=False) as response:
                self.assertIn(content, response.read())
                self.assertIn("frame-ancestors 'none'", response.headers['Content-Security-Policy'])
        with self.assertRaises(HTTPError):
            self.request('/../buddy_notes_gui.py', token=False)

    def test_validation_returns_readable_error_and_picker_is_explicit(self):
        with self.assertRaises(HTTPError) as error:
            self.request('/api/start', {'mode': 'once', 'settings': DEFAULTS})
        self.assertEqual(error.exception.code, 400)
        self.assertIn('folder', json.load(error.exception)['error'])
        with patch('buddy_notes_gui.choose_folder', return_value='/chosen/folder') as picker:
            with self.request('/api/folder', {}) as response:
                self.assertEqual(json.load(response)['folder'], '/chosen/folder')
            picker.assert_called_once()
