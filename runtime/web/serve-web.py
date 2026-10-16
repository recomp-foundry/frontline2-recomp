#!/usr/bin/env python3
"""Serve the web build on localhost, with your extracted disc.

    python3 serve-web.py [BUILD_DIR] --disc-dir DIR [--port 8080]

DIR is your own extracted disc (the directory holding sys/ and files/, as
nodtool writes it). It is served only to this machine, at /disc/, with byte
ranges so the page reads files on demand, and listed at /disc/.frontline-manifest.

Pthreads need SharedArrayBuffer, which browsers enable only on cross-origin
isolated pages, so every response carries COOP/COEP/CORP headers. WebGPU needs
a secure context: localhost, or HTTPS elsewhere.

Adapted from mh3-wii (scripts/serve-web.py, GPLv3); see ORIGIN.md.
"""
import argparse
import functools
import http.server
import os
import re
import urllib.parse
from pathlib import Path

MANIFEST = '.frontline-manifest'


class Handler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {**http.server.SimpleHTTPRequestHandler.extensions_map,
                      '.wasm': 'application/wasm', '.js': 'text/javascript', '.data': 'application/octet-stream'}
    disc_dir = None
    manifest = b''

    def end_headers(self):
        self.send_header('Cross-Origin-Opener-Policy', 'same-origin')
        self.send_header('Cross-Origin-Embedder-Policy', 'require-corp')
        self.send_header('Cross-Origin-Resource-Policy', 'same-origin')
        self.send_header('Cache-Control', 'no-cache')
        super().end_headers()

    def log_message(self, fmt, *args):
        if os.environ.get('FRONTLINE_SERVE_LOG'):  # a boot reads thousands of disc files
            super().log_message(fmt, *args)

    def disc_file(self):
        path = urllib.parse.unquote(self.path.split('?')[0])
        if not self.disc_dir or not path.startswith('/disc/'):
            return None
        relative = path[len('/disc/'):].lstrip('/')  # WasmFS's fetch backend joins with a leading slash
        if relative == MANIFEST:
            return MANIFEST
        target = (self.disc_dir / relative).resolve()
        if self.disc_dir not in target.parents or not target.is_file():
            return False
        return target

    def send_disc(self, head):
        target = self.disc_file()
        if target is None:
            return False
        if target is False:
            self.send_error(404)
            return True
        data = self.manifest if target == MANIFEST else None
        size = len(data) if data is not None else target.stat().st_size
        start, end = 0, size - 1
        match = re.match(r'bytes=(\d*)-(\d*)$', self.headers.get('Range', ''))
        ranged = bool(match) and size > 0
        if ranged:
            start = int(match.group(1)) if match.group(1) else 0
            end = min(int(match.group(2)), size - 1) if match.group(2) else size - 1
        self.send_response(206 if ranged else 200)
        self.send_header('Content-Type', 'application/octet-stream')
        self.send_header('Accept-Ranges', 'bytes')
        if ranged:
            self.send_header('Content-Range', f'bytes {start}-{end}/{size}')
        self.send_header('Content-Length', str(end - start + 1 if size else 0))
        self.end_headers()
        if head or size == 0:
            return True
        if data is not None:
            self.wfile.write(data[start:end + 1])
        else:
            with target.open('rb') as source:
                source.seek(start)
                left = end - start + 1
                while left > 0:
                    chunk = source.read(min(left, 4 << 20))
                    if not chunk:
                        break
                    self.wfile.write(chunk)
                    left -= len(chunk)
        return True

    def do_HEAD(self):
        if not self.send_disc(True):
            super().do_HEAD()

    def do_GET(self):
        if not self.send_disc(False):
            super().do_GET()


def build_manifest(disc_dir):
    lines = []
    for root, _, names in os.walk(disc_dir):
        for name in sorted(names):
            path = Path(root) / name
            lines.append(f'{path.stat().st_size}\t{path.relative_to(disc_dir).as_posix()}')
    return ('\n'.join(sorted(lines, key=lambda l: l.split('\t', 1)[1])) + '\n').encode()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('build', nargs='?', default=Path(__file__).resolve().parent, type=Path)
    parser.add_argument('--port', type=int, default=8080)
    parser.add_argument('--disc-dir', type=Path, required=True, help='your extracted disc (sys/, files/)')
    args = parser.parse_args()
    disc_dir = args.disc_dir.resolve()
    if not (disc_dir / 'sys' / 'main.dol').is_file():
        parser.error(f'{disc_dir} has no sys/main.dol')
    Handler.disc_dir = disc_dir
    Handler.manifest = build_manifest(disc_dir)
    handler = functools.partial(Handler, directory=os.fspath(args.build.resolve()))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', args.port), handler)
    print(f'Serving {args.build} at http://localhost:{args.port}/ (disc: {disc_dir})', flush=True)
    server.serve_forever()


if __name__ == '__main__':
    main()
