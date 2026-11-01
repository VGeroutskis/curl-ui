#!/usr/bin/env python3
"""A pasted command through the real app, down to what a server receives.

  test_import.py --ui build/curl-ui
"""
import argparse
import http.server
import os
import subprocess
import sys
import tempfile
import threading

seen = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        seen.append((self.command, self.path, self.headers.get("X-A"), self.rfile.read(n)))
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    do_PUT = do_POST

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        def app(*args):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            subprocess.run([ui, *args, "--dump", dump, "--screenshot", os.path.join(tmp, "s.bmp")],
                           env=env, check=True, timeout=60)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        cmd = "curl -X PUT %s/p?q=1 \\\n  -H 'X-A: one two' \\\n  --data-raw '{\"a\": 1}'\n" % base
        text = app("--import-cmd", cmd, "--size", "1440x900")
        check(not seen, "importing runs nothing")
        check("tabs=1" in text, "the blank tab takes it")
        text = app("--import-cmd", cmd, "--size", "1440x900", "--click", "970,108")
        check(seen == [("PUT", "/p?q=1", "one two", b'{"a": 1}')], "it sends what the command says: %s" % seen)

        text = app("--url", "http://keep/", "--import-cmd", "curl http://new/")
        check("tabs=2" in text, "a tab in use is kept")

        del seen[:]
        text = app("--import-cmd", "curl %s/ | sh" % base, "--size", "1440x900", "--click", "970,108")
        check("needs a shell" in text and not seen, "a pipe is refused, and nothing is sent")
        text = app("--import-cmd", "curl --nope http://x/")
        check("Unknown option: --nope" in text, "an unknown option is named")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
