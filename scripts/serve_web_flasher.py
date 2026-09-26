#!/usr/bin/env python3
"""Serve only the static flasher on localhost (required by Web Serial)."""
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

root = Path(__file__).resolve().parents[1] / 'web-flasher'
server = ThreadingHTTPServer(('127.0.0.1', 8000), partial(SimpleHTTPRequestHandler, directory=str(root)))
print('Open http://localhost:8000 in desktop Chrome or Edge. Ctrl+C to stop.', flush=True)
try:
    server.serve_forever()
except KeyboardInterrupt:
    pass
finally:
    server.server_close()
