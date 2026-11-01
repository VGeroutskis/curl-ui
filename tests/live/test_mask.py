#!/usr/bin/env python3
"""Secrets are sent, and hidden in everything the app shows.

  test_mask.py --ui build/curl-ui
"""
import argparse
import base64
import http.server
import os
import subprocess
import sys
import tempfile
import threading

TOKEN = "s3cret-token-value"
PASSWORD = "hunter22-unique"
seen = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        auth = self.headers.get("Authorization", "")
        seen.append(auth)
        # a server that repeats what it was sent
        reply = ('{"you_sent": "%s", "token": "%s"}' % (auth, TOKEN)).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(reply)))
        self.send_header("X-Echo", auth)
        self.end_headers()
        self.wfile.write(reply)

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d/" % server.server_address[1]
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

        text = app("--secret", "t=" + TOKEN, "--url", base, "--add", "oauth2-bearer={{t}}", "--run")
        check(seen == ["Bearer " + TOKEN], "the real value is what the server receives")
        shown = text.split("copy_command=")[0]
        check(TOKEN not in shown, "the command, response and log do not show it")
        check("oauth2-bearer '********'" in shown or "oauth2-bearer ********" in shown or "********" in shown,
              "the command shows stars")
        check("copy_command=" in text and TOKEN in text.split("copy_command=")[1].split("\n")[0],
              "Copy has the real value, so the command runs")
        check("you_sent" in shown and TOKEN not in shown.split("---stdout---")[1], "a response that repeats it is covered")

        text = app("--secret", "t=" + TOKEN, "--url", base, "--add", "oauth2-bearer={{t}}", "--reveal")
        check(TOKEN in text.split("copy_command=")[0], "the eye shows it in the command")

        del seen[:]
        wire = base64.b64encode(("bob:" + PASSWORD).encode()).decode()
        text = app("--secret", "p=" + PASSWORD, "--url", base, "--add", "user=bob:{{p}}", "--run", "--log")
        check(seen == ["Basic " + wire], "Basic goes out as base64(username:password)")
        shown = text.split("copy_command=")[0]
        check(PASSWORD not in shown and wire not in shown, "the log hides the password and its base64")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
