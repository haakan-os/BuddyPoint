#!/usr/bin/env python3
"""
HaakanPoint Mock Server
Simulates both the Xteink X3 HTTP receiver and KOSync REST server for local testing.
Runs on port 8080 by default.
"""

from http.server import HTTPServer, BaseHTTPRequestHandler
import json
import time
import cgi
import os

PORT = 8080
progress_db = {}
uploaded_books = []

class HaakanPointMockHandler(BaseHTTPRequestHandler):
    def _send_json(self, status, payload):
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Accept", "application/vnd.koreader.v1+json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(json.dumps(payload).encode('utf-8'))

    def do_GET(self):
        print(f"[GET] {self.path}")
        
        # 1. Health check
        if self.path == "/healthcheck":
            self._send_json(200, {"state": "OK"})
            return
            
        # 2. Xteink X3 Device Status
        if self.path == "/api/status":
            self._send_json(200, {
                "device": "HaakanPoint X3 (Mock)",
                "version": "1.0.0",
                "battery_pct": 92,
                "battery_volts": 4.12,
                "charging": False,
                "ip": "127.0.0.1",
                "mdns": "haakanpoint.local",
                "sd_total_mb": 15200,
                "sd_free_mb": 14320,
                "books_count": len(uploaded_books)
            })
            return
            
        # 3. KOSync Pull Progress (/users/progress/{hash})
        if self.path.startswith("/users/progress/"):
            doc_hash = self.path.split("/users/progress/")[1]
            if doc_hash in progress_db:
                self._send_json(200, progress_db[doc_hash])
            else:
                self._send_json(404, {"error": "Document progress not found"})
            return
            
        self._send_json(404, {"error": "Not Found"})

    def do_POST(self):
        print(f"[POST] {self.path}")
        
        # 1. Direct Book Upload from KOReader (/api/upload)
        if self.path == "/api/upload":
            ctype, pdict = cgi.parse_header(self.headers.get('content-type'))
            if ctype == 'multipart/form-data':
                pdict['boundary'] = bytes(pdict['boundary'], "utf-8")
                fields = cgi.parse_multipart(self.rfile, pdict)
                if 'file' in fields:
                    file_content = fields['file'][0]
                    uploaded_books.append({
                        "size": len(file_content),
                        "timestamp": int(time.time())
                    })
                    print(f"  [✓] Received book file upload ({len(file_content)} bytes)")
                    self._send_json(200, {"status": "success", "message": "Book uploaded successfully"})
                    return
            self._send_json(400, {"error": "Invalid upload format"})
            return

        # 2. Trigger Sync (/api/sync)
        if self.path == "/api/sync":
            self._send_json(200, {"status": "synced", "message": "Triggered manual sync"})
            return
            
        # 3. KOSync User Create (/users/create)
        if self.path == "/users/create":
            self._send_json(201, {"status": "created", "message": "User registered"})
            return
            
        self._send_json(404, {"error": "Not Found"})

    def do_PUT(self):
        print(f"[PUT] {self.path}")
        
        # KOSync Push Progress (/users/progress)
        if self.path == "/users/progress":
            content_len = int(self.headers.get('content-length', 0))
            body = self.rfile.read(content_len)
            try:
                data = json.loads(body.decode('utf-8'))
                doc_hash = data.get("document")
                if doc_hash:
                    progress_db[doc_hash] = data
                    print(f"  [✓] Synced progress for {doc_hash}: {data.get('percentage', 0):.1f}%")
                    self._send_json(200, {"status": "ok", "message": "Progress updated"})
                    return
            except Exception as e:
                print(f"  [!] Error parsing progress update: {e}")
                
        self._send_json(400, {"error": "Invalid progress payload"})

def run():
    server = HTTPServer(("0.0.0.0", PORT), HaakanPointMockHandler)
    print("==================================================")
    print(f"  HaakanPoint Mock Server running on port {PORT}")
    print(f"  - Health check:     http://localhost:{PORT}/healthcheck")
    print(f"  - Device Status:    http://localhost:{PORT}/api/status")
    print(f"  - Book Upload:      http://localhost:{PORT}/api/upload")
    print(f"  - KOSync Progress:  http://localhost:{PORT}/users/progress")
    print("==================================================")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopping server...")
        server.server_close()

if __name__ == "__main__":
    run()
