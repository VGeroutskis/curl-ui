#!/usr/bin/env python3
"""The file picker, the window buttons and the right panel, through the real app.

  test_picker.py --ui build/curl-ui
"""
import argparse
import http.server
import os
import subprocess
import sys
import tempfile
import threading

seen = []

# at --size 1440x900; the picker is 760x560 and centred
PATH = "1075,230"  # past the end of the text, where typing goes on
NAME = "600,660"
OK = "1050,704"
CANCEL = "960,704"
ROW0 = "600,297"
BROWSE_BODY = "976,235"


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        seen.append(self.rfile.read(n))
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
    base = "http://127.0.0.1:%d" % server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        home = os.path.join(tmp, "s")
        files = os.path.join(tmp, "αρχεία")
        os.makedirs(os.path.join(files, "sub"))
        body = os.path.join(files, "σώμα.txt")
        with open(body, "w", encoding="utf-8") as f:
            f.write("from the file")
        existing = os.path.join(files, "old.txt")
        with open(existing, "w", encoding="utf-8") as f:
            f.write("old")

        def app(*args, settings=home):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            if os.path.exists(os.path.join(settings, "session.json")):
                os.remove(os.path.join(settings, "session.json"))  # each case its own tab
            subprocess.run([ui, "--settings-dir", settings, "--size", "1440x900", *args, "--dump", dump,
                            "--screenshot", os.path.join(tmp, "s.bmp")], env=env, check=True, timeout=120)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        def field(text, key):
            for line in text.splitlines():
                if line.startswith(key + "="):
                    return line[len(key) + 1:]
            return None

        # Body > File: Browse, type the folder, pick the first file by name, OK; the server gets it
        pick = ["--click", NAME, "--type", files + "\n", "--click", NAME, "--type", "σώμα.txt", "--click", OK]
        text = app("--url", base + "/", "--form", "body", "--set-body", "file", "",
                   "--click", BROWSE_BODY, *pick, "--click", "970,108")
        check(field(text, "picker") == "0", "the picker closes on OK")
        check(seen == [b"from the file"], "the chosen file is what is sent: %s" % seen)
        last = app("--form", "body")
        check(field(last, "last_dir") == os.path.realpath(files), "the folder is remembered: %s" % field(last, "last_dir"))

        # Cancel changes nothing
        text = app("--form", "body", "--set-body", "file", "/keep", "--click", BROWSE_BODY, "--click", CANCEL)
        check("--data-binary @/keep" in text.replace("'", ""), "Cancel keeps the value")
        check(field(text, "picker") == "0", "Cancel closes it")

        # A name that is not there: stays open and says so
        text = app("--form", "body", "--set-body", "file", "", "--click", BROWSE_BODY, *pick[:4],
                   "--click", NAME, "--type", "nope.txt", "--click", OK)
        check(field(text, "picker") == "1", "open refuses a missing file")
        check("No such file" in (field(text, "picker_err") or ""), "and says why")

        # Double click on a folder goes in; on a file it is the choice
        text = app("--form", "body", "--set-body", "file", "", "--click", BROWSE_BODY, *pick[:4],
                   "--double-click", ROW0)
        check(field(text, "picker_dir") == os.path.realpath(os.path.join(files, "sub")), "a double click enters a folder")
        # the row after sub: my.conf is written later, so new.txt never existed and old.txt is it
        text = app("--url", base + "/", "--form", "body", "--set-body", "file", "", "--click", BROWSE_BODY, *pick[:4],
                   "--double-click", "600,323")
        check(field(text, "picker") == "0" and "old.txt" in text, "and on a file it is the choice")

        # The folder field takes typing, Enter goes there
        text = app("--form", "body", "--set-body", "file", "", "--click", BROWSE_BODY, *pick[:4],
                   "--click", PATH, "--type", os.sep + "sub\n")
        check(field(text, "picker_dir") == os.path.realpath(os.path.join(files, "sub")), "the folder field goes where it says")

        # The option cards offer it, in the right mode, and the value keeps its form
        card_browse = "955,227"
        text = app("--form", "curl", "--add", "output=", "--click", card_browse)
        check(field(text, "picker_mode") == "save", "--output saves")
        text = app("--form", "curl", "--add", "output=", "--click", card_browse, *pick[:4],
                   "--click", NAME, "--type", "old.txt", "--click", OK)
        check(field(text, "picker") == "1" and field(text, "picker_confirm") == "1", "saving over a file asks first")
        text = app("--form", "curl", "--add", "output=", "--click", card_browse, *pick[:4],
                   "--click", NAME, "--type", "old.txt", "--click", OK, "--click", OK)
        check(field(text, "picker") == "0" and existing in text, "and the second OK takes it")
        text = app("--form", "curl", "--add", "output=", "--click", card_browse, *pick[:4],
                   "--click", NAME, "--type", "new.txt", "--click", OK)
        check(field(text, "picker") == "0" and os.path.join(os.path.realpath(files), "new.txt") in text, "a new name needs no asking")
        check(not os.path.exists(os.path.join(files, "new.txt")), "and nothing is written yet")

        text = app("--form", "curl", "--add", "capath=", "--click", card_browse)
        check(field(text, "picker_mode") == "dir", "--capath takes a folder")
        text = app("--form", "curl", "--add", "capath=", "--click", card_browse, *pick[:4], "--click", OK)
        check(field(text, "picker") == "0" and os.path.realpath(files) in text, "the current folder is the answer")

        text = app("--form", "curl", "--add", "data=a=1", "--click", card_browse, *pick[:4],
                   "--click", NAME, "--type", "σώμα.txt", "--click", OK)
        check("@" + os.path.realpath(body) in text, "--data gets @path")
        text = app("--form", "curl", "--add", "user-agent=x", "--click", card_browse)
        check(field(text, "picker") == "0", "--user-agent has no Browse")

        # Settings and Import
        conf = os.path.join(files, "my.conf")
        with open(conf, "w", encoding="utf-8") as f:
            f.write("curl http://from-a-file/ -H 'X-F: 1'\n")
        text = app("--import-dialog", "--click", "545,579", *pick[:4], "--click", NAME, "--type", "my.conf", "--click", OK,
                   "--click", "912,579")
        check("http://from-a-file/" in text, "Import reads a command from a file: %s" % field(text, "picker_err"))

        # The window buttons
        s2 = os.path.join(tmp, "s2")
        frame = field(app(settings=s2), "own_frame")
        if frame == "1":
            text = app("--click", "1340,24", settings=s2)
            check(field(text, "win_action") == "min", "the first button minimizes")
            text = app("--click", "1380,24", settings=s2)
            check(field(text, "win_action") == "max", "the second maximizes")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            done = subprocess.run([ui, "--settings-dir", s2, "--size", "1440x900", "--click", "1420,24"], env=env, timeout=30)
            check(done.returncode == 0, "the third closes the app")
            text = app("--native-frame", settings=s2)
            check(field(text, "own_frame") == "0", "the system title bar can be asked for")
        else:
            print("skip window buttons: the system draws them here")

        # The right panel
        text = app("--form", "curl", settings=s2)
        check(field(text, "right") == "docs", "docs on Curl")
        text = app("--form", "curl", "--click", "822,146", settings=s2)
        check(field(text, "right") == "none", "they close")
        check(field(app("--form", "curl", settings=s2), "right") == "none", "and stay closed")
        check(field(app("--form", "headers", settings=s2), "right") == "request", "another view has This request")
        text = app("--form", "curl", "--click", "1240,146", settings=s2)
        check(field(text, "right") == "docs", "and they open again")

        text = app("--form", "headers", "--url", "http://x/{{host}}/{{gone}}", "--var", "host=h1", "--secret", "tok=abc",
                   "--set-header", "A: {{tok}}", settings=s2)
        check(field(text, "panel_vars") == "tok:********,host:h1,gone:missing", "it lists what the request uses: %s" % field(text, "panel_vars"))
        text = app("--form", "headers", "--click", "1363,201", settings=s2)
        check(field(text, "show_vars") == "1", "and opens the table")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
