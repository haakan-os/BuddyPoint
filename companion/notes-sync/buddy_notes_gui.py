#!/usr/bin/env python3
"""A private, local browser interface for BuddyPoint notes sync. Python 3.9+."""
from collections import deque
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import hmac
import json
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import threading
import webbrowser

from buddy_notes_sync import Reader, Sync, SyncError, atomic_write, sync_lock

ROOT = Path(__file__).resolve().parent
DEFAULTS = dict(folder='', device='haakanpoint.local', reader_folder='Obsidian',
                math=False, math_size=26, interval=30)


class Cancelled(Exception):
    pass


def validate(settings):
    if not isinstance(settings, dict):
        raise ValueError('Choose your sync settings first.')
    result = {key: settings.get(key, value) for key, value in DEFAULTS.items()}
    for key in ('folder', 'device', 'reader_folder'):
        if not isinstance(result[key], str) or not result[key].strip():
            raise ValueError('Choose a notes folder, reader address and destination folder.')
        result[key] = result[key].strip()
    if type(result['math']) is not bool:
        raise ValueError('Choose whether to render maths and images.')
    for key, low, high in (('math_size', 18, 40), ('interval', 10, 86400)):
        if type(result[key]) is not int or not low <= result[key] <= high:
            raise ValueError(f'{"Maths size" if key == "math_size" else "Sync interval"} must be {low}–{high}.')
    folder = Path(result['folder']).expanduser().resolve(strict=True)
    if not folder.is_dir():
        raise ValueError('Choose an existing notes folder.')
    result['folder'] = str(folder)
    Reader(result['device'], result['reader_folder'])  # Validation only; no network access.
    return result


class Controller:
    def __init__(self, home=None, sync_type=Sync, reader_type=Reader):
        self.home = Path(home) if home is not None else Path.home() / '.buddypoint-sync'
        self.sync_type, self.reader_type = sync_type, reader_type
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.thread = None
        self.closing = False
        self.logs = deque(maxlen=500)
        self.sequence = 0
        self.status = 'Ready'
        self.summary = {}
        self.error = ''
        self.settings = DEFAULTS.copy()
        try:
            saved = json.loads((self.home / 'gui.json').read_text(encoding='utf-8'))
            if isinstance(saved, dict):
                self.settings.update({key: value for key, value in saved.items()
                                      if key in DEFAULTS and type(value) is type(DEFAULTS[key])})
        except (OSError, ValueError):
            pass

    def report(self, message):
        # These messages are issued between note transactions, never during a rename.
        if self.stop_event.is_set() and message.startswith(('Comparing note ', 'Checking equations:',
                                                          'Checking links and flashcards:', 'Scanning ')):
            raise Cancelled()
        with self.lock:
            self.sequence += 1
            self.logs.append(dict(id=self.sequence, text=str(message)))

    def snapshot(self):
        with self.lock:
            return dict(status=self.status, running=bool(self.thread and self.thread.is_alive()),
                        error=self.error, summary=self.summary.copy(), logs=list(self.logs),
                        settings=self.settings.copy(), stopping=self.stop_event.is_set())

    def start(self, settings, mode):
        if mode not in {'once', 'watch', 'preview'}:
            raise ValueError('Unknown sync mode.')
        settings = validate(settings)
        with self.lock:
            if self.closing:
                raise ValueError('The app is closing. Reopen the launcher to sync again.')
            if self.thread and self.thread.is_alive():
                raise ValueError('Sync is already running. Stop it before changing settings.')
            self.home.mkdir(parents=True, exist_ok=True)
            atomic_write(self.home / 'gui.json', json.dumps(settings, indent=2).encode())
            self.settings = settings
            self.summary, self.error = {}, ''
            self.stop_event.clear()
            self.status = 'Preparing preview…' if mode == 'preview' else 'Connecting…'
            self.thread = threading.Thread(target=self.run, args=(settings.copy(), mode), daemon=True)
            self.thread.start()

    def stop(self):
        self.stop_event.set()
        with self.lock:
            if self.thread and self.thread.is_alive():
                self.status = 'Stopping after the current operation…'

    def run(self, settings, mode):
        try:
            if settings['math']:
                from buddy_math import dependencies
                dependencies()
            folder = Path(settings['folder'])
            reader = self.reader_type(settings['device'], settings['reader_folder'], report=self.report)
            key = hashlib.sha256(str(folder).encode()).hexdigest()[:24]
            with sync_lock(self.home / key):
                while not self.stop_event.is_set():
                    try:
                        with self.lock:
                            self.status, self.error = ('Previewing…' if mode == 'preview' else 'Syncing…'), ''
                        sync = self.sync_type(folder, reader, self.home / key, dry_run=mode == 'preview',
                                              report=self.report, math=settings['math'], math_size=settings['math_size'])
                        sync.run()
                        with self.lock:
                            self.summary = sync.summary.copy()
                            self.status = 'Preview complete' if mode == 'preview' else 'Sync complete'
                    except (SyncError, OSError) as error:
                        if mode != 'watch':
                            raise
                        self.report(f'Waiting for reader: {error}')
                        with self.lock:
                            self.error = str(error)
                    if mode != 'watch':
                        break
                    with self.lock:
                        self.status = f'Next check in {settings["interval"]} seconds'
                    if self.stop_event.wait(settings['interval']):
                        break
        except Cancelled:
            pass
        except Exception as error:
            self.report(f'Sync stopped: {error}')
            with self.lock:
                self.error, self.status = str(error), 'Needs attention'
        finally:
            if self.stop_event.is_set():
                with self.lock:
                    self.status = 'Stopped'


def choose_folder():
    """Ask the OS for a folder without requiring Tk or interpolating shell input."""
    if sys.platform == 'darwin':
        command = ['osascript', '-e', 'POSIX path of (choose folder with prompt "Choose your notes folder")']
    elif sys.platform == 'win32':
        command = ['powershell.exe', '-NoProfile', '-STA', '-Command',
                   'Add-Type -AssemblyName System.Windows.Forms; '
                   '$picker = New-Object System.Windows.Forms.FolderBrowserDialog; '
                   '$picker.Description = "Choose your notes folder"; '
                   'if ($picker.ShowDialog() -eq "OK") { [Console]::OutputEncoding = [Text.Encoding]::UTF8; '
                   '[Console]::Write($picker.SelectedPath) }']
    elif shutil.which('zenity'):
        command = ['zenity', '--file-selection', '--directory', '--title=Choose your notes folder']
    elif shutil.which('kdialog'):
        command = ['kdialog', '--getexistingdirectory', str(Path.home())]
    else:
        raise ValueError('No folder picker is available. Paste your notes folder path into the field.')
    result = subprocess.run(command, capture_output=True, encoding='utf-8', timeout=180)
    if result.returncode:
        return ''  # The user cancelled the picker.
    return result.stdout.strip()


class Server(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, controller, port=0):
        super().__init__(('127.0.0.1', port), Handler)
        self.controller = controller
        self.token = secrets.token_urlsafe(32)
        self.origin = f'http://127.0.0.1:{self.server_port}'
        self.picker_lock = threading.Lock()

    def quit(self):
        with self.controller.lock:
            self.controller.closing = True
            worker = self.controller.thread
        self.controller.stop()
        if worker:
            worker.join()
        self.shutdown()


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, code, data, content_type='application/json; charset=utf-8'):
        if not isinstance(data, bytes):
            data = json.dumps(data).encode()
        self.send_response(code)
        self.send_header('Content-Type', content_type)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.send_header('Referrer-Policy', 'no-referrer')
        self.send_header('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def authorised(self):
        return (self.headers.get('Host') == self.server.origin.split('//')[1]
                and self.headers.get('Origin', self.server.origin) == self.server.origin
                and hmac.compare_digest(self.headers.get('Authorization', ''), 'Bearer ' + self.server.token))

    def do_GET(self):
        if self.headers.get('Host') != self.server.origin.split('//')[1]:
            self.send(403, dict(error='Open the local link printed by the launcher.'))
            return
        assets = {'/': ('gui/index.html', 'text/html; charset=utf-8'),
                  '/app.js': ('gui/app.js', 'text/javascript; charset=utf-8'),
                  '/style.css': ('gui/style.css', 'text/css; charset=utf-8'),
                  '/guide': ('gui/guide.html', 'text/html; charset=utf-8')}
        if self.path in assets:
            name, mime = assets[self.path]
            self.send(200, (ROOT / name).read_bytes(), mime)
        elif self.path == '/api/state' and self.authorised():
            self.send(200, self.server.controller.snapshot())
        else:
            self.send(403, dict(error='Reopen BuddyPoint Sync using its launcher.'))

    def do_POST(self):
        if not self.authorised() or self.headers.get('Content-Type') != 'application/json':
            self.send(403, dict(error='This request must come from your local sync window.'))
            return
        try:
            length = int(self.headers.get('Content-Length', '0'))
            if not 0 < length <= 8192:
                raise ValueError('Invalid request size.')
            self.connection.settimeout(5)
            request = json.loads(self.rfile.read(length))
            if not isinstance(request, dict):
                raise ValueError('Invalid request.')
            if self.path == '/api/start':
                self.server.controller.start(request.get('settings'), request.get('mode'))
            elif self.path == '/api/stop':
                self.server.controller.stop()
            elif self.path == '/api/folder':
                if not self.server.picker_lock.acquire(blocking=False):
                    raise ValueError('The folder picker is already open.')
                try:
                    self.send(200, dict(folder=choose_folder()))
                finally:
                    self.server.picker_lock.release()
                return
            elif self.path == '/api/quit':
                threading.Thread(target=self.server.quit, daemon=True).start()
            else:
                self.send(404, dict(error='Unknown action.'))
                return
            self.send(200, dict(ok=True))
        except (ValueError, SyncError, OSError, subprocess.SubprocessError) as error:
            self.send(400, dict(error=str(error)))


def main():
    server = Server(Controller())
    url = server.origin + '/#' + server.token
    print('BuddyPoint Sync — open this link if a browser does not appear:\n' + url, flush=True)
    print('Use Quit in the window to stop the local app. Closing the browser tab alone does not stop sync.', flush=True)
    webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        server.controller.stop()
        print('Finishing the current operation before closing…', flush=True)
        if server.controller.thread:
            server.controller.thread.join()
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
