#!/usr/bin/env python3
"""Headers, Body and Auth through the real app and real curl, judged by what a server receives.

  test_forms.py --ui build/curl-ui
"""
import argparse
import base64
import http.server
import os
import subprocess
import sys
import tempfile
import threading
from urllib.parse import parse_qs

seen = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def handle_any(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        seen.append({"path": self.path, "headers": self.headers, "body": self.rfile.read(n), "method": self.command})
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    do_GET = do_POST = do_PUT = handle_any

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
            del seen[:]
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            subprocess.run([ui, "--url", base, *args, "--run", "--dump", dump, "--screenshot", os.path.join(tmp, "s.bmp")],
                           env=env, check=True, timeout=60)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        app("--set-auth", "basic", "bob", "p:w")
        want = "Basic " + base64.b64encode(b"bob:p:w").decode()
        check(seen[0]["headers"]["Authorization"] == want, "Basic: exactly base64(username:password)")

        user = "βασίλης"
        app("--set-auth", "basic", user, "κωδ")
        want = "Basic " + base64.b64encode((user + ":κωδ").encode()).decode()
        check(seen[0]["headers"]["Authorization"] == want, "Basic: UTF-8 username and password")

        app("--set-auth", "bearer", "tok", "")
        check(seen[0]["headers"]["Authorization"] == "Bearer tok", "Bearer")
        app("--set-auth", "apikey-header", "X-Key", "k1")
        check(seen[0]["headers"]["X-Key"] == "k1", "API key in a header")
        app("--set-auth", "apikey-query", "key", "a b")
        check(seen[0]["path"] == "/p?key=a%20b", "API key in the query")

        app("--set-header", "X-A: 1", "--set-header", "X-B: two words")
        check(seen[0]["headers"]["X-A"] == "1" and seen[0]["headers"]["X-B"] == "two words", "Headers")

        app("--set-body", "raw", "@not-a-file")
        check(seen[0]["body"] == b"@not-a-file" and seen[0]["method"] == "POST", "raw: @ is just text")
        app("--set-body", "json", '{"a":1}')
        check(seen[0]["body"] == b'{"a":1}' and seen[0]["headers"]["Content-Type"] == "application/json", "JSON")
        app("--set-body", "urlencoded", "a b=c d&e")
        check(parse_qs(seen[0]["body"].decode()) == {"a b": ["c d&e"]}, "urlencoded: %r" % seen[0]["body"])
        path = os.path.join(tmp, "up.bin")
        with open(path, "wb") as f:
            f.write(b"\x00\x01data\r\n")
        app("--set-body", "file", path)
        check(seen[0]["body"] == b"\x00\x01data\r\n", "file: the bytes as they are")
        app("--set-body", "form-data", "a=@y")
        body = seen[0]["body"]
        check(b"multipart/form-data" in seen[0]["headers"]["Content-Type"].encode() and b'name="a"' in body and
              b"@y" in body, "form-data: text starting with @ stays text")
        app("--set-body", "form-data-file", "f=" + path)
        check(b'name="f"' in seen[0]["body"] and b"data" in seen[0]["body"], "form-data: a file")

        text = app("--add", "data=x", "--add", "form=y=1", "--form", "body")
        check("body_kind=options" in text, "what a form cannot say is reported as set by options")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
