#!/usr/bin/env python3
"""Sync a computer's Markdown folder with BuddyPoint. Python 3.10+, no packages."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
from http.client import HTTPException
import json
import os
from pathlib import Path, PurePosixPath
import re
import sys
import tempfile
import time
import unicodedata
from urllib.error import HTTPError, URLError
from urllib.parse import urlencode, urlsplit
from urllib.request import build_opener, HTTPRedirectHandler, ProxyHandler, Request
import uuid

MAX_NOTE = 8 * 1024 * 1024
MAX_LIST = 4 * 1024 * 1024
MAX_DEPTH = 32
MAX_ENTRIES = 10000
EXTENSIONS = {".md", ".markdown"}


class SyncError(Exception):
    pass


def digest(data):
    return hashlib.sha256(data).hexdigest() if data is not None else None


def safe_name(name):
    if (not isinstance(name, str) or not name or name.startswith(".") or name.strip() != name
            or name.endswith((".", " ")) or len(name.encode("utf-8")) > 180
            or any(ord(c) < 32 or c in '/\\:"<>|?*' for c in name)
            or name.split(".")[0].upper() in {"CON", "PRN", "AUX", "NUL", *[f"COM{i}" for i in range(10)],
                                               *[f"LPT{i}" for i in range(10)]}):
        raise SyncError(f"Unsupported filename: {name!r}")
    return name


def safe_relative(name):
    if not isinstance(name, str) or not name or len(name.split("/")) > MAX_DEPTH:
        raise SyncError(f"Invalid or excessively deep notes path: {name!r}")
    for part in name.split("/"):
        safe_name(part)
    return name


def sibling(name, basename):
    return str(PurePosixPath(safe_relative(name)).with_name(safe_name(basename)))


def check_collisions(names):
    seen = {}
    files = set(names)
    for name in files:
        safe_relative(name)
        parts = name.split("/")
        for length in range(1, len(parts) + 1):
            prefix = "/".join(parts[:length])
            key = unicodedata.normalize("NFC", prefix).casefold()
            if key in seen and seen[key] != prefix:
                raise SyncError(f"Names differ only by case or Unicode spelling: {seen[key]!r}, {prefix!r}")
            if length < len(parts) and prefix in files:
                raise SyncError(f"A note is also used as a folder: {prefix}")
            seen[key] = prefix


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".buddy-", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as out:
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def save_json(path, value):
    atomic_write(path, (json.dumps(value, ensure_ascii=False, indent=2) + "\n").encode())


def load_json(path, default):
    if not path.exists():
        return default
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (ValueError, OSError) as error:
        raise SyncError(f"Cannot read {path}; keep this file for recovery: {error}") from error


@contextmanager
def sync_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / "lock").open("a+b") as lock:
        lock.seek(0)
        lock.write(b"0")
        lock.flush()
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise SyncError("Another sync is already using this notes folder.") from error
        try:
            yield
        finally:
            if os.name == "nt":
                lock.seek(0)
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, *args, **kwargs):
        return None


class Reader:
    def __init__(self, address, folder="/OneDriveNotes", timeout=60):
        self.address = address.rstrip("/")
        if "://" not in self.address:
            self.address = "http://" + self.address
        parts = urlsplit(self.address)
        if (parts.scheme not in {"http", "https"} or not parts.hostname or parts.username
                or parts.password or parts.path or parts.query or parts.fragment):
            raise SyncError("Device must be a hostname or IP address, optionally with a port.")
        # A single dedicated directory keeps this tool away from firmware/system files.
        self.folder = "/" + safe_name(folder.strip("/"))
        if self.folder.casefold() in {"/buddynotes", "/system", "/books", "/fonts"}:
            raise SyncError("Choose a dedicated notes folder, separate from KOReader exports and books.")
        self.timeout = timeout
        # LAN traffic must not be sent through an environment-configured proxy.
        self.opener = build_opener(ProxyHandler({}), NoRedirects())

    def request(self, route, query=None, form=None, data=None, headers=None, missing=False, limit=MAX_LIST):
        url = self.address + route + ("?" + urlencode(query) if query else "")
        if form is not None:
            data = urlencode(form).encode()
            headers = {"Content-Type": "application/x-www-form-urlencoded"}
        try:
            with self.opener.open(Request(url, data=data, headers=headers or {}), timeout=self.timeout) as response:
                result = response.read(limit + 1)
                if len(result) > limit:
                    raise SyncError(f"Reader response exceeded the size limit ({route}).")
                return result
        except HTTPError as error:
            if missing and error.code == 404:
                return None
            raise SyncError(f"Reader returned HTTP {error.code} for {route}.") from error
        except (URLError, TimeoutError, OSError, HTTPException) as error:
            raise SyncError(f"Cannot reach {self.address}. Open BuddySync on the reader. ({error})") from error

    def listing(self, folder):
        try:
            entries = json.loads(self.request("/api/files", {"path": folder}))
            if not isinstance(entries, list):
                raise ValueError("expected a file list")
            for entry in entries:
                if (not isinstance(entry, dict) or not isinstance(entry.get("name"), str)
                        or not isinstance(entry.get("isDirectory"), bool)):
                    raise ValueError("invalid file entry")
            return entries
        except ValueError as error:
            raise SyncError("The reader returned an invalid file list.") from error

    def status(self):
        try:
            value = json.loads(self.request("/api/status"))
            if not isinstance(value, dict) or not value.get("version") or not value.get("device"):
                raise ValueError()
            return value
        except ValueError as error:
            raise SyncError("This address did not return a BuddyPoint device status.") from error

    def ensure_folder(self, dry_run=False):
        for item in self.listing("/"):
            if item["name"].casefold() == self.folder[1:].casefold():
                if not item["isDirectory"] or item["name"] != self.folder[1:]:
                    raise SyncError("The reader notes folder conflicts with an existing name.")
                return
        if not dry_run:
            self.request("/mkdir", form={"path": "/", "name": self.folder[1:]})

    def ensure_parent(self, name):
        parent = self.folder
        for component in safe_relative(name).split("/")[:-1]:
            key = unicodedata.normalize("NFC", component).casefold()
            matches = [item for item in self.listing(parent)
                       if unicodedata.normalize("NFC", item["name"]).casefold() == key]
            if matches:
                if len(matches) != 1 or matches[0]["name"] != component or not matches[0]["isDirectory"]:
                    raise SyncError(f"Reader folder conflicts with an existing name: {parent}/{component}")
            else:
                self.request("/mkdir", form={"path": parent, "name": component})
            parent += "/" + component

    def notes(self):
        result = set()
        pending = [""]
        entries_seen = 0
        while pending:
            relative = pending.pop()
            folder = self.folder + ("/" + relative if relative else "")
            entries = self.listing(folder)
            entries_seen += len(entries)
            if entries_seen > MAX_ENTRIES:
                raise SyncError("Reader notes tree exceeds the 10,000-entry scan limit.")
            siblings = set()
            for item in entries:
                name = item["name"]
                if name in {".", ".."} or "/" in name or "\\" in name:
                    raise SyncError(f"Invalid reader directory entry: {name!r}")
                if name.startswith("."):
                    continue
                safe_name(name)
                if name in siblings:
                    raise SyncError(f"Duplicate reader filename: {folder}/{name}")
                siblings.add(name)
                path = safe_relative(f"{relative}/{name}" if relative else name)
                if item["isDirectory"]:
                    pending.append(path)
                elif PurePosixPath(name).suffix.lower() in EXTENSIONS:
                    result.add(path)
            check_collisions(siblings)
        check_collisions(result)
        return result

    def read(self, name):
        return self.request("/download", {"path": f"{self.folder}/{safe_relative(name)}"}, missing=True, limit=MAX_NOTE)

    def upload(self, name, data):
        self.ensure_parent(name)
        path = PurePosixPath(safe_relative(name))
        destination = self.folder + ("/" + str(path.parent) if str(path.parent) != "." else "")
        boundary = "BuddySync" + uuid.uuid4().hex
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{path.name}"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode() + data
        body += f"\r\n--{boundary}--\r\n".encode()
        self.request("/api/upload", {"path": destination}, data=body,
                     headers={"Content-Type": f"multipart/form-data; boundary={boundary}"})

    def rename(self, old, new):
        source, destination = PurePosixPath(safe_relative(old)), PurePosixPath(safe_relative(new))
        if source.parent != destination.parent:
            raise SyncError("Sync renames must stay in the same folder.")
        self.request("/rename", form={"path": f"{self.folder}/{old}", "name": destination.name})

    def remove_scratch(self, name):
        path = PurePosixPath(safe_relative(name))
        if not re.fullmatch(r"buddysync-[0-9a-f]{32}\.(part|backup)", path.name):
            raise SyncError("Refusing to remove a file that is not a sync temporary file.")
        self.request("/delete", form={"path": f"{self.folder}/{name}"})


class Sync:
    def __init__(self, folder, reader, state_dir, dry_run=False, report=print):
        self.folder, self.reader, self.state_dir = folder.resolve(), reader, state_dir
        self.dry_run, self.report = dry_run, report
        self.pending_path = state_dir / "pending.json"
        self.state_path = state_dir / "state.json"
        self.identity = {"folder": str(folder), "reader": reader.address, "remote": reader.folder}
        self.state = load_json(self.state_path, {"identity": self.identity, "files": {}})
        if (not isinstance(self.state, dict) or self.state.get("identity") != self.identity
                or not isinstance(self.state.get("files"), dict)
                or any(not isinstance(v, str) or not re.fullmatch(r"[0-9a-f]{64}", v)
                       for v in self.state["files"].values())):
            raise SyncError("Sync history is invalid or belongs to another folder/device. Keep it for recovery.")

    def local_path(self, name):
        path = self.folder
        for component in safe_relative(name).split("/"):
            path = path / component
            if path.is_symlink() or path.resolve() != path.absolute():
                raise SyncError(f"Symbolic links and directory links are not synced: {name}")
        return path

    def local_notes(self):
        result = set()
        pending = [self.folder]
        entries_seen = 0
        while pending:
            directory = pending.pop()
            children = list(directory.iterdir())
            entries_seen += len(children)
            if entries_seen > MAX_ENTRIES:
                raise SyncError("Computer notes tree exceeds the 10,000-entry scan limit.")
            siblings = []
            for child in children:
                if child.name.startswith("."):
                    continue
                relative = child.relative_to(self.folder).as_posix()
                path = self.local_path(relative)
                siblings.append(child.name)
                if path.is_dir():
                    pending.append(path)
                elif path.suffix.lower() in EXTENSIONS:
                    result.add(relative)
            check_collisions(siblings)
        check_collisions(result)
        return result

    def local(self, name):
        path = self.local_path(name)
        try:
            with path.open("rb") as source:
                data = source.read(MAX_NOTE + 1)
        except FileNotFoundError:
            return None
        if len(data) > MAX_NOTE:
            raise SyncError(f"Note exceeds the 8 MiB limit: {name}")
        return data

    def archive(self, data):
        if data is not None and not self.dry_run:
            path = self.state_dir / "backups" / (digest(data) + ".md")
            if not path.exists():
                atomic_write(path, data)

    def remember(self, name, data):
        self.state["files"][name] = digest(data)
        if not self.dry_run:
            save_json(self.state_path, self.state)

    def recover(self):
        pending = load_json(self.pending_path, None)
        if pending is None:
            return
        if self.dry_run:
            raise SyncError("An interrupted upload needs recovery. Run once without --dry-run.")
        if not isinstance(pending, dict) or pending.get("identity") != self.identity:
            raise SyncError("Upload recovery belongs to another device or folder.")
        if (not all(isinstance(pending.get(key), str) for key in ("name", "temporary", "backup", "desired"))
                or not re.fullmatch(r"buddysync-[0-9a-f]{32}\.part", PurePosixPath(pending["temporary"]).name)
                or pending["backup"] != str(PurePosixPath(pending["temporary"]).with_suffix(".backup"))
                or not re.fullmatch(r"[0-9a-f]{64}", pending["desired"])):
            raise SyncError("Upload recovery file is invalid; keep it and the reader backup for recovery.")
        name, temporary, backup = (safe_relative(pending[key]) for key in ("name", "temporary", "backup"))
        if PurePosixPath(name).parent != PurePosixPath(temporary).parent:
            raise SyncError("Upload recovery paths must be in the same folder.")
        current = self.reader.read(name)
        saved = self.reader.read(backup)
        if current is None and saved is not None:
            self.reader.rename(backup, name)
            saved = None
        elif current is not None and saved is not None and digest(current) != pending["desired"]:
            raise SyncError(f"Keep {backup} on the reader: {name} changed during recovery.")
        if saved is not None:
            self.archive(saved)
            self.reader.remove_scratch(backup)
        if self.reader.read(temporary) is not None:
            self.reader.remove_scratch(temporary)
        self.pending_path.unlink()
        self.report(f"Recovered interrupted upload: {name}")

    def put_remote(self, name, data, expected):
        self.archive(data)
        self.archive(expected)
        token = uuid.uuid4().hex
        temporary, backup = sibling(name, f"buddysync-{token}.part"), sibling(name, f"buddysync-{token}.backup")
        # Journal before touching the reader so a dropped connection is recoverable.
        save_json(self.pending_path, {"identity": self.identity, "name": name, "temporary": temporary,
                                      "backup": backup, "desired": digest(data)})
        self.reader.upload(temporary, data)
        if self.reader.read(temporary) != data:
            raise SyncError(f"Upload verification failed: {name}; original preserved.")
        if self.reader.read(name) != expected:
            raise SyncError(f"Reader note changed during sync: {name}; retry to compare both copies.")
        if expected is not None:
            self.reader.rename(name, backup)
            if self.reader.read(backup) != expected:
                raise SyncError(f"Reader note changed during upload: {name}; saved copy will be restored on retry.")
        self.reader.rename(temporary, name)
        if self.reader.read(name) != data:
            raise SyncError(f"Final verification failed: {name}; backup retained.")
        if expected is not None:
            self.reader.remove_scratch(backup)
        self.pending_path.unlink()

    def put_local(self, name, data, expected):
        if self.local(name) != expected:
            raise SyncError(f"Computer note changed during sync: {name}; retry after saving your edits.")
        self.archive(expected)
        self.archive(data)
        atomic_write(self.local_path(name), data)

    def run(self):
        self.reader.status()
        self.reader.ensure_folder(self.dry_run)
        self.recover()
        local_names = self.local_notes()
        remote_names = self.reader.notes()
        names = local_names | remote_names
        check_collisions(names)
        actions = 0
        for name in sorted(names):
            local, remote = self.local(name), self.reader.read(name)
            self.archive(local)
            self.archive(remote)
            if local == remote:
                if local is not None:
                    self.remember(name, local)
                continue
            baseline = self.state["files"].get(name)
            if local is None or (remote is not None and digest(local) == baseline):
                self.report(f"Download: {name}")
                if not self.dry_run:
                    self.put_local(name, remote, local)
                    self.remember(name, remote)
            elif remote is None or digest(remote) == baseline:
                self.report(f"Upload: {name}")
                if not self.dry_run:
                    if self.local(name) != local:
                        raise SyncError(f"Computer note changed during sync: {name}")
                    self.put_remote(name, local, remote)
                    self.remember(name, local)
            else:
                # Leave the computer version at the original name. Publish the
                # reader version as a separate note on both sides before replacing it.
                suffix = Path(name).suffix
                conflict = sibling(name, f"{PurePosixPath(name).stem[:32]}.reader-conflict-{digest(remote)[:16]}{suffix}")
                check_collisions(names | {conflict})
                self.report(f"Conflict: {name} → reader copy saved as {conflict}")
                if not self.dry_run:
                    old_local, old_remote = self.local(conflict), self.reader.read(conflict)
                    if old_local not in (None, remote) or old_remote not in (None, remote):
                        raise SyncError(f"Conflict copy already has different edits: {conflict}")
                    self.put_local(conflict, remote, old_local)
                    if old_remote is None:
                        self.put_remote(conflict, remote, None)
                    self.remember(conflict, remote)
                    names.add(conflict)
                    if self.local(name) != local:
                        raise SyncError(f"Computer note changed during sync: {name}")
                    self.put_remote(name, local, remote)
                    self.remember(name, local)
            actions += 1
        self.report(f"{'Preview' if self.dry_run else 'Sync'} complete: {actions} change(s), {len(names)} note(s).")
        return actions


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--folder", required=True, type=Path, help="Local notes folder, including Markdown files in subfolders")
    parser.add_argument("--device", default="haakanpoint.local", help="Reader hostname or IP shown in BuddySync")
    parser.add_argument("--reader-folder", default="/OneDriveNotes", help="Dedicated SD-card folder")
    parser.add_argument("--dry-run", action="store_true", help="Preview transfers without writing notes or history")
    parser.add_argument("--watch", action="store_true", help="Repeat until Ctrl+C; retry while reader is unavailable")
    parser.add_argument("--interval", type=int, default=60, help="Seconds between repeats (minimum 10)")
    args = parser.parse_args(argv)
    try:
        folder = args.folder.expanduser().resolve(strict=True)
        if not folder.is_dir():
            raise SyncError("Choose an existing notes folder.")
        if args.interval < 10:
            raise SyncError("Use an interval of at least 10 seconds.")
        reader = Reader(args.device, args.reader_folder)
        # Keep history outside OneDrive so another computer cannot share stale baselines.
        folder_key = hashlib.sha256(str(folder).encode()).hexdigest()[:24]
        state_dir = Path.home() / ".buddypoint-sync" / folder_key
        print(f"Notes: {folder}\nReader: {reader.address}{reader.folder}\nHistory/backups: {state_dir}", flush=True)
        with sync_lock(state_dir):
            while True:
                try:
                    Sync(folder, reader, state_dir, args.dry_run).run()
                except (SyncError, OSError) as error:
                    if not args.watch:
                        raise
                    print(f"Waiting: {error}", file=sys.stderr, flush=True)
                if not args.watch:
                    break
                time.sleep(args.interval)
        return 0
    except KeyboardInterrupt:
        print("\nStopped. Any interrupted upload will be recovered on the next run.")
        return 0
    except (SyncError, OSError) as error:
        print(f"Sync stopped: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
