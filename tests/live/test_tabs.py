#!/usr/bin/env python3
"""Tabs through the real app: requests run at once, and closing one that is running is safe.

  test_tabs.py --ui build/curl-ui
"""
import argparse
import http.server
import os
import subprocess
import sys
import tempfile
import threading
import time

lock = threading.Lock()
active = peak = 0


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        global active, peak
        with lock:
            active += 1
            peak = max(peak, active)
        time.sleep(1.5)
        with lock:
            active -= 1
        reply = ("reply for " + self.path).encode()
        try:
            self.send_response(200)
            self.send_header("Content-Length", str(len(reply)))
            self.end_headers()
            self.wfile.write(reply)
        except OSError:
            pass

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
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    def app(tmp, *args):
        dump = os.path.join(tmp, "dump.txt")
        rc = subprocess.run([ui, "--settings-dir", os.path.join(tmp, "s"), *args, "--dump", dump,
                             "--screenshot", os.path.join(tmp, "s.bmp")], env=env, timeout=60).returncode
        text = ""
        if os.path.exists(dump):
            with open(dump, encoding="utf-8") as f:
                text = f.read()
        return rc, text

    with tempfile.TemporaryDirectory() as tmp:
        rc, text = app(tmp, "--url", base + "/one", "--run", "--tab", "--url", base + "/two", "--run")
        check(rc == 0, "two tabs: exits 0")
        check(peak == 2, "two tabs: the requests overlap at the server (peak %d)" % peak)
        check("tabs=2" in text, "two tabs: both exist")
        check("reply for /one" in text and "reply for /two" in text, "two tabs: each has its response")
        one = text.split("===tab 0")[1].split("===tab 1")[0]
        check("reply for /one" in one and "reply for /two" not in one, "two tabs: responses do not mix")

        rc, text = app(tmp, "--url", base + "/three", "--tab", "--url", base + "/four", "--select-tab", "0")
        check("cur=0" in text, "--select-tab selects")
        check("https" not in text and base + "/three" in text.split("command=")[1].split("\n")[0],
              "the command is the selected tab's")

        rc, text = app(tmp, "--url", base + "/five", "--run", "--no-wait", "--close-tab", "0")
        check(rc == 0, "closing a running tab: exits 0, with no sanitizer report")
        check("tabs=1" in text, "closing the last tab leaves a fresh one")

        rc, text = app(tmp, "--url", base + "/six", "--run", "--no-wait")
        check(rc == 0, "quitting while a tab runs: exits 0, with no sanitizer report")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
