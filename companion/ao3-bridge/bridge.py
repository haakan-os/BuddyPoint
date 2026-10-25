"""BuddyPoint single-user bridge: bounded jobs, authenticated JSON and EPUBs."""
import hashlib
import json
import os
import queue
import re
import secrets
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

import requests
from ao3 import Ao3, BridgeError, work_id


class Service:
    def __init__(self, data, client=None):
        self.data = Path(data)
        self.data.mkdir(parents=True, exist_ok=True)
        self.token = os.environ.get("BRIDGE_TOKEN", "")
        token_file = self.data / "device-token"
        if not self.token:
            if not token_file.exists():
                fd = os.open(token_file, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
                with os.fdopen(fd, "w") as output:
                    output.write(secrets.token_hex(32))
            self.token = token_file.read_text().strip()
        if not re.fullmatch(r"[A-Za-z0-9_-]{32,128}", self.token):
            raise ValueError("BRIDGE_TOKEN must contain 32–128 ASCII letters, digits, '-' or '_'")
        self.client = client or Ao3(self.data)
        self.jobs = {}
        self.lock = threading.Lock()
        self.queue = queue.Queue(maxsize=8)
        self.optimize = os.environ.get("OPTIMIZE_EPUB", "true").lower() != "false"
        self.worker = threading.Thread(target=self.run, daemon=True)
        self.worker.start()

    def submit(self, args):
        operation = args.get("operation")
        if operation not in ("login", "logout", "list", "work", "prepare"):
            raise BridgeError("parse", "Unknown operation")
        if operation in ("work", "prepare"):
            work_id(args.get("id"))
        if operation == "list" and not isinstance(args.get("url"), str):
            raise BridgeError("parse", "Missing listing URL")
        with self.lock:
            now = time.monotonic()
            self.jobs = {key: value for key, value in self.jobs.items()
                         if value["state"] in ("queued", "running") or now - value["created"] < 1800}
            if len(self.jobs) >= 64 or self.queue.full():
                raise BridgeError("busy", "Bridge queue is full; try again shortly")
            identifier = secrets.token_hex(16)
            self.jobs[identifier] = {"state": "queued", "created": now}
            self.queue.put_nowait((identifier, dict(args)))
        return identifier

    def run(self):
        while True:
            identifier, args = self.queue.get()
            with self.lock:
                self.jobs[identifier]["state"] = "running"
            try:
                operation = args.pop("operation")
                if operation == "login":
                    result = self.client.login(args.get("username"), args.get("password"))
                elif operation == "logout":
                    result = self.client.logout()
                elif operation == "list":
                    result = self.client.listing(args["url"])
                elif operation == "work":
                    result = self.client.work(args["id"])
                else:
                    result = self.client.prepare(args["id"], self.optimize)
                    result["path"] = "/api/v1/files/" + identifier
                    result["owner"] = self.client.username
                with self.lock:
                    self.jobs[identifier].update(state="done", result=result)
            except BridgeError as error:
                self.fail(identifier, error.code, str(error))
            except requests.Timeout:
                self.fail(identifier, "timeout", "AO3 timed out; try again later")
            except requests.RequestException:
                self.fail(identifier, "network", "Could not connect securely to AO3")
            except OSError:
                self.fail(identifier, "storage", "Bridge storage operation failed")
            except Exception:
                self.fail(identifier, "parse", "Unexpected AO3 page or invalid EPUB")
            finally:
                args.clear()  # Passwords are never persisted with job state.
                self.queue.task_done()

    def fail(self, identifier, code, message):
        with self.lock:
            self.jobs[identifier].update(state="error", error=code, message=message)

    def job(self, identifier, internal=False):
        with self.lock:
            value = self.jobs.get(identifier)
            if not value:
                return None
            result = dict(value.get("result", {}))
            if not internal:
                result.pop("file", None)
                result.pop("owner", None)
            return {key: item for key, item in value.items() if key not in ("created", "result")} | {"result": result}


def byte_range(value, size):
    if not value:
        return 0, size - 1, False
    match = re.fullmatch(r"bytes=(\d*)-(\d*)", value)
    if not match or not any(match.groups()):
        raise ValueError("Invalid range")
    start, end = match.groups()
    if not start:
        start, end = max(0, size - int(end)), size - 1
    else:
        start, end = int(start), min(int(end), size - 1) if end else size - 1
    if start > end or start >= size:
        raise ValueError("Unsatisfiable range")
    return start, end, True


class Server(ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self, address, service):
        self.service = service
        self.slots = threading.BoundedSemaphore(16)
        super().__init__(address, Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            super().process_request_thread(request, address)
        finally:
            self.slots.release()


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def setup(self):
        super().setup()
        self.connection.settimeout(15)

    def log_message(self, *_):
        pass  # Never log request URLs, account details or credentials.

    def response_headers(self, status, length, content_type):
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(length))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; form-action 'none'")
        self.send_header("Connection", "close")
        self.close_connection = True

    def json(self, status, value):
        data = json.dumps(value, ensure_ascii=True).encode()
        self.response_headers(status, len(data), "application/json")
        self.end_headers()
        self.wfile.write(data)

    def authorized(self):
        expected = ("Bearer " + self.server.service.token).encode()
        supplied = self.headers.get("Authorization", "").encode()
        if not secrets.compare_digest(expected, supplied):
            self.json(401, {"error": "sign_in", "message": "Enter the bridge device token"})
            return False
        origin = self.headers.get("Origin")
        if origin and urlsplit(origin).netloc != self.headers.get("Host"):
            self.json(403, {"error": "sign_in", "message": "Cross-origin requests are not allowed"})
            return False
        return True

    def do_GET(self):
        path = urlsplit(self.path).path
        if path == "/healthz":
            self.json(200, {"ok": True, "api": 1})
        elif path in ("/", "/app.js"):
            file = Path(__file__).parent / ("index.html" if path == "/" else "app.js")
            data = file.read_bytes()
            self.response_headers(200, len(data), "text/html; charset=utf-8" if path == "/" else "text/javascript")
            self.end_headers()
            self.wfile.write(data)
        elif self.authorized():
            if path == "/api/v1/status":
                self.json(200, {"signed_in": bool(self.server.service.client.username),
                                "username": self.server.service.client.username,
                                "optimized": self.server.service.optimize})
            elif re.fullmatch(r"/api/v1/jobs/[a-f0-9]{32}", path):
                value = self.server.service.job(path.rsplit("/", 1)[1])
                self.json(200 if value else 404, value or {"error": "missing"})
            elif re.fullmatch(r"/api/v1/files/[a-f0-9]{32}", path):
                self.file(path.rsplit("/", 1)[1])
            else:
                self.json(404, {"error": "missing"})

    def do_POST(self):
        if not self.authorized():
            return
        if self.path != "/api/v1/jobs":
            self.json(404, {"error": "missing"})
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 0 < size <= 8192 or self.headers.get("Transfer-Encoding"):
                raise ValueError()
            if self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise ValueError()
            args = json.loads(self.rfile.read(size))
            if not isinstance(args, dict):
                raise ValueError()
            identifier = self.server.service.submit(args)
            self.json(202, {"job": identifier})
        except (ValueError, UnicodeError):
            self.json(400, {"error": "parse", "message": "Invalid JSON request"})
        except BridgeError as error:
            self.json(429 if error.code == "busy" else 400, {"error": error.code, "message": str(error)})

    def file(self, identifier):
        job = self.server.service.job(identifier, internal=True)
        result = job.get("result", {}) if job else {}
        path = result.get("file")
        if not path or not path.is_file():
            self.json(404, {"error": "missing"})
            return
        if result.get("owner") != self.server.service.client.username:
            self.json(403, {"error": "sign_in"})
            return
        size = path.stat().st_size
        etag = '"' + result["sha256"] + '"'
        requested = self.headers.get("Range", "")
        if self.headers.get("If-Range", etag) != etag:
            requested = ""
        try:
            start, end, partial = byte_range(requested, size)
        except ValueError:
            self.response_headers(416, 0, "application/epub+zip")
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return
        self.response_headers(206 if partial else 200, end - start + 1, "application/epub+zip")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("ETag", etag)
        if partial:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with path.open("rb") as stream:
            stream.seek(start)
            remaining = end - start + 1
            while remaining:
                data = stream.read(min(65536, remaining))
                if not data:
                    break
                self.wfile.write(data)
                remaining -= len(data)


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument("command", nargs="?", choices=("serve", "token"), default="serve")
    parser.add_argument("--data", default=os.environ.get("BRIDGE_DATA", "/data"))
    parser.add_argument("--port", type=int, default=8080)
    args = parser.parse_args()
    if args.command == "token":
        print(os.environ.get("BRIDGE_TOKEN") or (Path(args.data) / "device-token").read_text().strip())
    else:
        os.umask(0o077)
        service = Service(args.data)
        print("BuddyPoint AO3 bridge listening on port", args.port, flush=True)
        Server(("0.0.0.0", args.port), service).serve_forever()
