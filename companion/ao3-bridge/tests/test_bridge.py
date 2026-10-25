import importlib.util
import io
import json
import os
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request
import zipfile
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT), str(ROOT.parent)]
from PIL import Image
from bs4 import BeautifulSoup
from ao3 import Ao3, BridgeError, metadata, signed_in, trusted
from bridge import Server, Service, byte_range
from optimizer import optimize_epub, validate_epub


def epub(path, extra=None):
    data = {
        "mimetype": b"application/epub+zip",
        "META-INF/container.xml": b'<container><rootfiles><rootfile full-path="OPS/book.opf"/></rootfiles></container>',
        "OPS/book.opf": b'<package><manifest><item id="one" href="chapter.xhtml"/><item id="pic" href="pic.png" media-type="image/png"/></manifest><spine><itemref idref="one"/></spine></package>',
        "OPS/chapter.xhtml": b'<html><body><h1 id="chapter-1">Chapter one</h1><p>Author notes</p><a href="#chapter-1">Back</a></body></html>',
    }
    image = io.BytesIO()
    Image.new("RGBA", (1000, 2000), (255, 0, 0, 128)).save(image, format="PNG")
    data["OPS/pic.png"] = image.getvalue()
    data.update(extra or {})
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as archive:
        for name, contents in data.items():
            archive.writestr(name, contents)
    return data


class OptimizerTest(unittest.TestCase):
    def test_preserves_chapters_package_and_image_format(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "original.epub", Path(directory) / "optimized.epub"
            contents = epub(source)
            original = source.read_bytes()
            self.assertTrue(optimize_epub(source, output))
            self.assertEqual(source.read_bytes(), original)
            validate_epub(output)
            with zipfile.ZipFile(output) as archive:
                self.assertEqual(archive.infolist()[0].filename, "mimetype")
                self.assertEqual(archive.infolist()[0].compress_type, zipfile.ZIP_STORED)
                for name in ("OPS/chapter.xhtml", "OPS/book.opf"):
                    self.assertEqual(archive.read(name), contents[name])
                with Image.open(io.BytesIO(archive.read("OPS/pic.png"))) as image:
                    self.assertEqual(image.format, "PNG")
                    self.assertLessEqual(image.width, 480)
                    self.assertLessEqual(image.height, 800)

    def test_rejects_paths_outside_archive_and_preserves_existing_output(self):
        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "bad.epub", Path(directory) / "good.epub"
            epub(source, {"../escape.txt": b"bad"})
            output.write_bytes(b"existing")
            with self.assertRaises(ValueError):
                optimize_epub(source, output)
            self.assertEqual(output.read_bytes(), b"existing")
            self.assertFalse((Path(directory).parent / "escape.txt").exists())

    def test_rejects_missing_spine_target(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "bad.epub"
            epub(source, {"OPS/book.opf": b'<package><manifest/><spine><itemref idref="absent"/></spine></package>'})
            with self.assertRaises(ValueError):
                validate_epub(source)


GREETING = '<div id="header"><div id="greeting"><a href="/users/logout?return_to=x">Log out</a></div></div>'
def blurb(identifier):
    return f'<li class="bookmark blurb"><h4 class="heading"><a href="/works/{identifier}">Fic {identifier}</a> by <a rel="author">Writer</a></h4><dl class="stats"><dd class="chapters">3/4</dd><dd class="words">1,234</dd></dl></li>'


class Ao3Test(unittest.TestCase):
    def test_login_uses_csrf_and_persists_session_without_password(self):
        import requests
        with tempfile.TemporaryDirectory() as directory:
            client = Ao3(directory)
            calls = []
            def page(url, method="GET", data=None):
                calls.append(method)
                if method == "GET":
                    return BeautifulSoup('<input name="authenticity_token" value="csrf-value">', "html.parser")
                self.assertEqual(data["authenticity_token"], "csrf-value")
                self.assertEqual(data["user[password]"], "private-password")
                client.session.cookies.set_cookie(requests.cookies.create_cookie("_session", "authenticated", domain="archiveofourown.org", secure=True))
                return BeautifulSoup(GREETING, "html.parser")
            with patch.object(client, "html", side_effect=page):
                self.assertEqual(client.login("Reader", "private-password"), {"username": "Reader"})
            self.assertEqual(calls, ["GET", "POST"])
            restored = Ao3(directory)
            self.assertEqual(restored.username, "Reader")
            self.assertEqual(list(restored.session.cookies)[0].value, "authenticated")
            self.assertTrue(restored.session.verify)
            for file in Path(directory).iterdir():
                self.assertNotIn(b'private-password', file.read_bytes())

    def test_current_logout_url_and_cross_host_rejection(self):
        self.assertTrue(signed_in(BeautifulSoup(GREETING, "html.parser")))
        self.assertFalse(signed_in(BeautifulSoup(GREETING.replace('/users/logout', 'https://evil.example/users/logout'), "html.parser")))
        self.assertFalse(trusted("https://archiveofourown.org.evil.example/"))
        self.assertFalse(trusted("http://archiveofourown.org/"))
        self.assertFalse(trusted("https://user@archiveofourown.org/"))

    def test_metadata_and_series_skip(self):
        item = metadata(BeautifulSoup(blurb(123), "html.parser"))
        self.assertEqual(item["title"], "Fic 123")
        self.assertEqual(item["chapters"], 3)
        self.assertEqual(item["words"], 1234)
        self.assertIsNone(metadata(BeautifulSoup(blurb(123).replace('/works/', '/series/'), "html.parser")))

    def test_private_bookmarks_have_small_cached_reader_pages(self):
        with tempfile.TemporaryDirectory() as directory:
            client = Ao3(directory)
            client.username = "Reader"
            soup = BeautifulSoup(GREETING + '<ol class="bookmark">' + ''.join(blurb(i) for i in range(1, 21)) + '</ol>', "html.parser")
            with patch.object(client, "html", return_value=soup) as fetch:
                first = client.listing("https://archiveofourown.org/users/bridge/bookmarks?page=1")
                self.assertEqual([w["id"] for w in first["works"]], [str(i) for i in range(1, 6)])
                second = client.listing(first["next"])
                self.assertEqual(second["works"][0]["id"], "6")
                self.assertEqual(fetch.call_count, 1)
                self.assertIn('/users/Reader/bookmarks', fetch.call_args.args[0])

    def test_expired_session_does_not_fall_back_to_public_bookmarks(self):
        with tempfile.TemporaryDirectory() as directory:
            client = Ao3(directory)
            client.username = "Reader"
            with patch.object(client, "html", return_value=BeautifulSoup('<ol class="bookmark"></ol>', "html.parser")):
                with self.assertRaises(BridgeError) as failure:
                    client.listing("https://archiveofourown.org/users/bridge/bookmarks?page=1")
                self.assertEqual(failure.exception.code, "sign_in")
                self.assertEqual(client.username, "")

    def test_cross_origin_redirect_is_rejected_before_second_request(self):
        with tempfile.TemporaryDirectory() as directory:
            client = Ao3(directory)
            response = requests_response(302, {"Location": "https://evil.example/"})
            with patch.object(client.session, 'request', return_value=response) as request, patch('ao3.time.sleep'):
                with self.assertRaises(BridgeError):
                    client.request("https://archiveofourown.org/users/login")
                self.assertEqual(request.call_count, 1)

    def test_post_is_never_replayed_for_server_error(self):
        with tempfile.TemporaryDirectory() as directory:
            client = Ao3(directory)
            with patch.object(client.session, 'request', return_value=requests_response(525)) as request, patch('ao3.time.sleep'):
                with self.assertRaises(BridgeError):
                    client.request("https://archiveofourown.org/users/login", "POST", {"secret": "value"})
                self.assertEqual(request.call_count, 1)


def requests_response(status, headers=None):
    from unittest.mock import Mock
    response = Mock()
    response.status_code = status
    response.headers = headers or {}
    return response


class FakeAo3:
    username = "Reader"
    def __init__(self, directory):
        self.path = Path(directory) / "book.epub"
        epub(self.path)
    def work(self, identifier):
        return {"id": identifier, "title": "Fic", "chapters": 1}
    def prepare(self, identifier, optimize):
        import hashlib
        return {"work": self.work(identifier), "file": self.path, "size": self.path.stat().st_size,
                "sha256": hashlib.sha256(self.path.read_bytes()).hexdigest()}
    def login(self, user, password):
        return {"username": user}
    def logout(self):
        self.username = ""
        return {"signed_in": False}


class HttpTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.service = Service(self.temp.name, FakeAo3(self.temp.name))
        self.server = Server(("127.0.0.1", 0), self.service)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.base = 'http://127.0.0.1:' + str(self.server.server_port)

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.temp.cleanup()

    def request(self, path, body=None, headers=None, auth=True):
        h = {"Content-Type": "application/json"}
        if auth:
            h["Authorization"] = "Bearer " + self.service.token
        h.update(headers or {})
        request = urllib.request.Request(self.base + path, data=json.dumps(body).encode() if body else None, headers=h)
        try:
            return urllib.request.urlopen(request, timeout=3)
        except urllib.error.HTTPError as error:
            return error

    def test_requires_token_and_rejects_cross_origin(self):
        with self.request('/api/v1/status', auth=False) as response:
            self.assertEqual(response.status, 401)
        with self.request('/api/v1/status', headers={"Origin": "http://evil.example"}) as response:
            self.assertEqual(response.status, 403)
        with self.request('/api/v1/status') as response:
            self.assertEqual(json.load(response)["username"], "Reader")

    def test_job_file_has_length_etag_and_supports_range(self):
        with self.request('/api/v1/jobs', {"operation": "prepare", "id": "123"}) as response:
            identifier = json.load(response)["job"]
            self.assertEqual(response.status, 202)
        self.service.queue.join()
        with self.request('/api/v1/jobs/' + identifier) as response:
            result = json.load(response)["result"]
            self.assertNotIn('file', result)
        with self.request(result['path']) as response:
            data = response.read()
            self.assertEqual(len(data), int(response.headers['Content-Length']))
            etag = response.headers['ETag']
        with self.request(result['path'], headers={"Range": "bytes=10-29", "If-Range": etag}) as response:
            self.assertEqual(response.status, 206)
            self.assertEqual(response.read(), data[10:30])
        with self.request(result['path'], headers={"Range": "bytes=10-29", "If-Range": '"old"'}) as response:
            self.assertEqual(response.status, 200)
            self.assertEqual(response.read(), data)
        with self.request(result['path'], headers={"Range": "bytes=999999-"}) as response:
            self.assertEqual(response.status, 416)

    def test_login_job_never_persists_password(self):
        identifier = self.service.submit({"operation": "login", "username": "Reader", "password": "not-to-be-saved"})
        self.service.queue.join()
        self.assertNotIn('not-to-be-saved', json.dumps(self.service.job(identifier)))
        for file in Path(self.temp.name).iterdir():
            self.assertNotIn(b'not-to-be-saved', file.read_bytes())

    def test_logout_revokes_prepared_private_file_access(self):
        identifier = self.service.submit({"operation": "prepare", "id": "123"})
        self.service.queue.join()
        self.service.submit({"operation": "logout"})
        self.service.queue.join()
        with self.request('/api/v1/files/' + identifier) as response:
            self.assertEqual(response.status, 403)

    def test_range_parser(self):
        self.assertEqual(byte_range('bytes=-5', 20), (15, 19, True))
        for value in ('bytes=4-2', 'bytes=-0', 'bytes=0-1,2-3', 'garbage'):
            with self.assertRaises(ValueError):
                byte_range(value, 20)


if __name__ == '__main__':
    unittest.main()
