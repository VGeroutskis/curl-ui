#!/usr/bin/env python3
"""Variables through the real app: nothing is sent while one is missing, and the table is the
only source of values.

  test_vars.py --ui build/curl-ui
"""
import argparse
import http.server
import json
import os
import stat
import subprocess
import sys
import tempfile
import threading

seen = []


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        seen.append((self.path, self.headers.get("X-Token")))
        reply = b"ok"
        self.send_response(200)
        self.send_header("Content-Length", str(len(reply)))
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
    base = "http://127.0.0.1:%d" % server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        settings = os.path.join(tmp, "s")
        store = os.path.join(settings, "variables.json")

        def app(*args, extra_env=None, settings_dir=settings):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy", **(extra_env or {}))
            subprocess.run([ui, "--settings-dir", settings_dir, *args, "--dump", dump,
                            "--screenshot", os.path.join(tmp, "s.bmp")], env=env, check=True, timeout=60)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        req = ["--url", base + "/{{name}}", "--add", "header=X-Token: {{api_token}}"]

        text = app(*req, "--run")
        check(not seen, "missing variables: nothing reaches the server")
        check("modal=1" in text and "modal_missing=api_token,name" in text, "missing variables: the prompt names them")
        check("modal_secret=1,0" in text, "the prompt suggests Secret for a token, not for a name")

        text = app(*req, "--run", "--modal-value", "name=x", "--modal-action", "cancel")
        check(not seen and "modal=0" in text, "Cancel: nothing is sent")
        check(not os.path.exists(store), "Cancel: nothing is written")

        text = app(*req, "--run", "--modal-action", "open")
        check(not seen and "modal=0" in text and "show_vars=1" in text, "Open Variables: opens the table, sends nothing")

        text = app(*req, "--run", "--modal-value", "name=path1", "--modal-value", "api_token=abc123",
                   "--modal-action", "save")
        check(len(seen) == 1 and seen[0][1] == "abc123", "Save & Run: the request goes out with the values")
        check("modal=0" in text, "Save & Run: the prompt closes")
        check("X-Token: ********" in text.split("---stdout---")[0], "the command on screen hides the secret")
        with open(store, encoding="utf-8") as f:
            data = json.load(f)
        rows = {v["name"]: v for v in data["environments"][0]["variables"]}
        check(rows["api_token"]["value"] == "abc123" and rows["api_token"]["secret"] is True, "the value is in the table")
        if os.name != "nt":
            check(stat.S_IMODE(os.stat(store).st_mode) == 0o600, "the file is private")

        del seen[:]
        text = app(*req, "--run")
        check("modal=0" in text and len(seen) == 1, "next time: no prompt, the table has them")

        del seen[:]
        other = os.path.join(tmp, "s2")
        text = app("--url", base + "/{{FROM_OS}}", "--run", extra_env={"FROM_OS": "leak"}, settings_dir=other)
        check(not seen and "modal_missing=FROM_OS" in text, "the operating system's environment gives no values")

        if os.name != "nt":
            # the variable must not reach curl's environment either
            probe = os.path.join(tmp, "probe.sh")
            out = os.path.join(tmp, "env.txt")
            with open(probe, "w", encoding="utf-8") as f:
                f.write("#!/bin/sh\nenv > '%s'\n" % out)
            os.chmod(probe, 0o700)
            third = os.path.join(tmp, "s3")
            os.makedirs(third)
            with open(os.path.join(third, "settings.ini"), "w", encoding="utf-8") as f:
                f.write("curl_program=%s\n" % probe)
            app("--var", "UNIQUE_VAR_NAME=UNIQUE_VAR_VALUE", "--url", "http://x/{{UNIQUE_VAR_NAME}}", "--run",
                settings_dir=third)
            with open(out, encoding="utf-8") as f:
                check("UNIQUE_VAR" not in f.read(), "variables stay out of curl's environment")

        with open(store, "w", encoding="utf-8") as f:
            f.write("{ damaged")
        text = app("--url", base + "/", "--run")
        check("notice=" in text and ".bad-" in text, "a damaged file is reported")
        check(os.path.exists(store + ".bad-1"), "a damaged file is kept aside")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
