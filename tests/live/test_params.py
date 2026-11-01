#!/usr/bin/env python3
"""The Params table through the real app: plain text in, encoded on the wire.

  test_params.py --ui build/curl-ui
"""
import argparse
import http.server
import os
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs, urlsplit

seen = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        seen.append(self.path)
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d/p" % server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        def app(*args):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            subprocess.run([ui, "--size", "1440x900", "--url", base, *args, "--dump", dump, "--screenshot", os.path.join(tmp, "s.bmp")],
                           env=env, check=True, timeout=60)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        value = "καφές & γάλα=1+1"
        app("--form", "params", "--set-param", "q=" + value, "--set-param", "off=x", "--param-off", "off",
            "--set-param", "v={{v}}", "--var", "v=a b", "--click", "970,108")
        check(len(seen) == 1, "the request is sent")
        query = parse_qs(urlsplit(seen[0]).query, keep_blank_values=True)
        check(query.get("q") == [value], "the value arrives as it was typed: %r" % query.get("q"))
        check("%CE%BA" in seen[0] and " " not in seen[0], "on the wire it is percent encoded")
        check("off" not in query, "a row switched off is not sent")
        check(query.get("v") == ["a b"], "a variable in a value is encoded too")

        text = app("--form", "params", "--url", base + "?a=x%20y#top")
        check("param=a:x y" in text, "a URL fills the table, decoded")
        check("command=curl %s?a=x%%20y#top" % base in text or "?a=x%20y#top" in text, "the URL is not rewritten")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
