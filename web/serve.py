#!/usr/bin/env python3 -I
"""Local test server for the web build: serves a folder on localhost with the headers the page needs.
Usage: serve.py <folder> [port]

The game's threads use SharedArrayBuffer, which browsers only allow on a cross-origin isolated page
(Cross-Origin-Opener-Policy: same-origin, Cross-Origin-Embedder-Policy: require-corp). The production server sets the
same headers (tools/deploy_web.sh)."""
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map, ".wasm": "application/wasm",
                      ".js": "text/javascript", ".o2r": "application/octet-stream"}

    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cross-Origin-Resource-Policy", "same-origin")
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()


folder = sys.argv[1]
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8642
server = http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=folder))
print(f"serving {folder} on http://localhost:{port}", flush=True)
server.serve_forever()
