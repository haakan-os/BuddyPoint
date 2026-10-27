"""Single-account AO3 client. Only the job worker touches its session."""
import hashlib
import http.cookiejar
import json
import os
import re
import time
from pathlib import Path
from urllib.parse import parse_qs, quote, urlencode, urljoin, urlsplit

import requests
from bs4 import BeautifulSoup

from optimizer import MAX_ARCHIVE, OPTIMIZER_VERSION, optimize_epub, validate_epub

ORIGIN = "https://archiveofourown.org"


class BridgeError(Exception):
    def __init__(self, code, message):
        super().__init__(message)
        self.code = code


def trusted(url):
    p = urlsplit(url)
    return (len(url) <= 4096 and p.scheme == "https" and p.netloc == "archiveofourown.org"
            and not any(ord(c) < 32 for c in url) and "\\" not in url)


def work_id(value):
    if not isinstance(value, str) or not re.fullmatch(r"[1-9][0-9]{0,11}", value):
        raise BridgeError("parse", "Invalid AO3 work ID")
    return value


def text(node, limit=192):
    return node.get_text(" ", strip=True)[:limit] if node else ""


def number(value):
    return int(re.sub(r"[^0-9]", "", value) or "0")


def signed_in(soup):
    for node in soup.select("#greeting a[href], #header a[href], #greeting form[action]"):
        href = urljoin(ORIGIN, node.get("href", node.get("action", "")))
        if trusted(href) and urlsplit(href).path == "/users/logout":
            return True
    return False


def metadata(node, identifier=None):
    heading = node.select_one("h2.title") if identifier else node.select_one("h4.heading")
    link = heading.select_one('a[href^="/works/"]') if heading and identifier is None else None
    if identifier is None:
        match = re.fullmatch(r"/works/([1-9][0-9]{0,11})", urlsplit(link.get("href", "")).path) if link else None
        if not match:
            return None  # series/external bookmarks
        identifier = match[1]
    chapters = text(node.select_one("dd.chapters"), 32).split("/")
    download = next((urljoin(ORIGIN, a["href"]) for a in node.select('a[href]')
                     if urlsplit(a["href"]).path.endswith(".epub")), "")
    return {
        "id": work_id(identifier), "title": text(link or heading),
        "author": ", ".join(text(a) for a in (node.select('.byline a[rel~=author]') or (heading.select('a[rel~=author]') if heading else [])))[:192],
        "summary": text(node.select_one("blockquote.summary, .summary blockquote"), 384),
        "fandom": text(node.select_one("h5.fandoms, dd.fandom")),
        "rating": text(node.select_one("dd.rating, .rating"), 80),
        "warnings": text(node.select_one("dd.warning, .warnings"), 384),
        "tags": text(node.select_one("ul.tags, dd.freeform")),
        "updated": text(node.select_one("dd.status, dd.published, p.datetime"), 32),
        "chapters": number(chapters[0]),
        "total": number(chapters[1]) if len(chapters) > 1 and chapters[1] != "?" else 0,
        "words": number(text(node.select_one("dd.words"), 24)), "download": download,
    }


class Ao3:
    def __init__(self, data):
        self.data = Path(data)
        self.data.mkdir(parents=True, exist_ok=True)
        self.session = requests.Session()
        self.session.headers.update({"User-Agent": "BuddyPoint-AO3-Bridge/1.0", "Accept-Encoding": "identity"})
        self.session.cookies = http.cookiejar.MozillaCookieJar(str(self.data / "cookies.txt"))
        self.username = ""
        self.last_request = 0
        self.pages = {}
        try:
            self.session.cookies.load(ignore_discard=True)
            self.username = json.loads((self.data / "account.json").read_text())["username"]
        except (OSError, ValueError, KeyError, http.cookiejar.LoadError):
            self.session.cookies.clear()

    def save(self):
        self.session.cookies.save(str(self.data / "cookies.tmp"), ignore_discard=True)
        os.chmod(self.data / "cookies.tmp", 0o600)
        os.replace(self.data / "cookies.tmp", self.data / "cookies.txt")
        temporary = self.data / "account.tmp"
        temporary.write_text(json.dumps({"username": self.username}))
        os.chmod(temporary, 0o600)
        os.replace(temporary, self.data / "account.json")

    def request(self, url, method="GET", data=None):
        retried = False
        for _ in range(7):
            if not trusted(url):
                raise BridgeError("parse", "AO3 redirected outside its trusted origin")
            time.sleep(max(0, 2 - (time.monotonic() - self.last_request)))
            self.last_request = time.monotonic()
            response = self.session.request(method, url, data=data, stream=True, timeout=(15, 30), allow_redirects=False)
            if response.status_code == 525 and method == "GET" and not retried:
                response.close()
                retried = True
                time.sleep(5)
                continue
            if response.status_code in (301, 302, 303, 307, 308):
                url = urljoin(url, response.headers.get("Location", ""))
                if response.status_code in (301, 302, 303):
                    method, data = "GET", None
                response.close()
                continue
            if response.status_code != 200:
                status = response.status_code
                response.close()
                code = "rate_limit" if status == 429 else "missing" if status == 404 else "sign_in" if status in (401, 403) else "server"
                raise BridgeError(code, f"AO3 returned HTTP {status}. Try again later." )
            return response
        raise BridgeError("server", "Too many AO3 redirects")

    def html(self, url, method="GET", data=None):
        with self.request(url, method, data) as response:
            content = bytearray()
            deadline = time.monotonic() + 120
            for chunk in response.iter_content(8192):
                if time.monotonic() > deadline:
                    raise BridgeError("timeout", "AO3 page took too long to download")
                content.extend(chunk)
                if len(content) > 2 * 1024 * 1024:
                    raise BridgeError("parse", "AO3 page exceeded the size limit")
        soup = BeautifulSoup(bytes(content), "html.parser")
        if self.username:
            self.save()
        return soup

    def login(self, username, password):
        if not isinstance(username, str) or not 1 <= len(username) <= 80 or not isinstance(password, str) or not 1 <= len(password) <= 256:
            raise BridgeError("sign_in", "Enter an AO3 username and password")
        self.logout()
        soup = self.html(ORIGIN + "/users/login")
        token = soup.select_one('input[name="authenticity_token"]')
        if not token:
            raise BridgeError("sign_in", "AO3 login form unavailable; check AO3 in your browser")
        soup = self.html(ORIGIN + "/users/login", "POST", {
            "authenticity_token": token.get("value", ""), "user[login]": username,
            "user[password]": password, "user[remember_me]": "1", "commit": "Log in"})
        if not signed_in(soup) or not list(self.session.cookies):
            self.logout()
            raise BridgeError("sign_in", "AO3 did not confirm sign-in; check your account in a browser")
        self.username = username
        self.save()
        return {"username": username}

    def logout(self):
        self.session.cookies.clear()
        self.username = ""
        self.pages.clear()
        self.save()
        return {"signed_in": False}

    def work(self, identifier):
        soup = self.html(ORIGIN + "/works/" + work_id(identifier) + "?view_adult=true")
        if soup.select_one('input[name="user[password]"]'):
            raise BridgeError("sign_in", "Sign in on the bridge to access this work")
        result = metadata(soup, identifier)
        if not result["title"] or not result["chapters"]:
            raise BridgeError("parse", "AO3 did not return a readable work")
        return result

    def listing(self, url):
        if not trusted(url):
            raise BridgeError("parse", "Invalid listing URL")
        parsed = urlsplit(url)
        query = parse_qs(parsed.query)
        try:
            page = int(query.get("page", ["1"])[0])
        except ValueError:
            raise BridgeError("parse", "Invalid page")
        if not 1 <= page <= 2000:
            raise BridgeError("parse", "Page limit reached")
        # Small reader pages bound JSON/document memory even over HTTPS.
        source_page, offset = (page - 1) // 4 + 1, (page - 1) % 4 * 5
        bookmarks = re.fullmatch(r"/users/[^/]+/bookmarks", parsed.path) is not None
        if bookmarks:
            if not self.username:
                raise BridgeError("sign_in", "Sign in using the bridge web page first")
            source_url = ORIGIN + "/users/" + quote(self.username, safe="") + "/bookmarks?" + urlencode({"page": source_page})
        elif parsed.path == "/works/search":
            term = query.get("work_search[query]", [""])[0][:256]
            source_url = ORIGIN + "/works/search?" + urlencode({"work_search[query]": term, "page": source_page})
        else:
            raise BridgeError("parse", "Unsupported AO3 listing")
        cached = self.pages.get(source_url)
        if cached and time.monotonic() - cached[0] < 60:
            items, more = cached[1:]
        else:
            soup = self.html(source_url)
            if bookmarks and not signed_in(soup):
                self.logout()
                raise BridgeError("sign_in", "AO3 session expired; sign in on the bridge again")
            listing = soup.select_one("ol.bookmark, ol.work, ul.bookmark, ul.work")
            if not listing and not soup.select_one("#main .notice, #main h2.heading"):
                raise BridgeError("parse", "AO3 returned an unexpected listing")
            items = [result for node in (listing.select("li.blurb") if listing else [])
                     if (result := metadata(node)) is not None][:20]
            more = soup.select_one("li.next a[href]") is not None
            self.pages = {source_url: (time.monotonic(), items, more)}
        next_url = ""
        if len(items) > offset + 5 or more:
            query["page"] = [str(page + 1)]
            next_url = ORIGIN + parsed.path + "?" + urlencode(query, doseq=True)
        return {"works": items[offset:offset + 5], "next": next_url}

    def prepare(self, identifier, optimize=True):
        work = self.work(identifier)  # Revalidate access even for a cached private work.
        fingerprint = hashlib.sha256(json.dumps(work, sort_keys=True).encode()).hexdigest()[:24]
        account = hashlib.sha256(self.username.encode()).hexdigest()[:16]
        directory = self.data / "books" / account / identifier / fingerprint
        directory.mkdir(parents=True, exist_ok=True)
        original = directory / "original.epub"
        if not original.exists():
            temporary = directory / "download.part"
            url = work["download"] or ORIGIN + f"/downloads/{identifier}/work.epub"
            try:
                with self.request(url) as response, temporary.open("wb") as output:
                    count = 0
                    deadline = time.monotonic() + 600
                    for chunk in response.iter_content(65536):
                        if time.monotonic() > deadline:
                            raise BridgeError("timeout", "AO3 EPUB took too long to download")
                        count += len(chunk)
                        if count > MAX_ARCHIVE:
                            raise BridgeError("parse", "EPUB exceeds 64 MiB")
                        output.write(chunk)
                validate_epub(temporary)
                os.replace(temporary, original)
            finally:
                temporary.unlink(missing_ok=True)
        optimized = directory / (OPTIMIZER_VERSION + ".epub")
        if optimize and not optimized.exists():
            optimize_epub(original, optimized)
        artifact = optimized if optimize else original
        return {"work": work, "file": artifact, "size": artifact.stat().st_size,
                "sha256": hashlib.sha256(artifact.read_bytes()).hexdigest(), "optimized": optimize}
