#!/usr/bin/env python3
"""Values too long for a command line reach the server whole, through the real app.

  test_large.py --ui build/curl-ui
"""
import argparse
import glob
import hashlib
import http.server
import os
import subprocess
import sys
import tempfile
import threading

received = []
FIRST_VALUE_Y = 195  # the value field of the first card, at 1440x900
RUN_Y = 76            # the Run button


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        received.append(body)
        reply = hashlib.sha256(body).hexdigest().encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(reply)))
        self.end_headers()
        self.wfile.write(reply)

    def log_message(self, *args):
        pass


def run_app(ui, tmp, name, port, value):
    value_path = os.path.join(tmp, name + ".txt")
    with open(value_path, "wb") as f:
        f.write(value)
    dump = os.path.join(tmp, name + ".dump")
    env = dict(os.environ, SDL_VIDEODRIVER="dummy")
    subprocess.run([ui, "--settings-dir", os.path.join(tmp, "settings"), "--url", "http://127.0.0.1:%d/" % port,
                    "--add-file", "data-raw=" + value_path, "--run", "--dump", dump,
                    "--screenshot", os.path.join(tmp, name + ".bmp")],
                   env=env, check=True, timeout=120)
    with open(dump, encoding="utf-8", errors="replace") as f:
        return f.read()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    port = server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        # characters the config file has to escape, and ones past ASCII
        unit = 'a"b\\c\td καφές {x} $y '.encode()
        for name, size, text in (("small", 1000, unit), ("spills", 1024 * 1024, unit),
                                 ("near-limit", 9 * 1024 * 1024, b"x")):
            value = (text * (size // len(text) + 1))[:size]
            value = value.decode("utf-8", "ignore").encode()
            del received[:]
            dump = run_app(ui, tmp, name, port, value)
            ok = len(received) == 1 and received[0] == value
            check(ok, "%s: %d bytes reach the server whole" % (name, len(value)))
            check(hashlib.sha256(value).hexdigest() in dump, "%s: the response is shown" % name)
            check("exit=0" in dump, "%s: curl exits 0" % name)

        # a paste far too big to edit in the field still goes into the value
        paste = (unit * 200000)[:2 * 1024 * 1024].decode("utf-8", "ignore").encode()
        paste_path = os.path.join(tmp, "paste.txt")
        with open(paste_path, "wb") as f:
            f.write(paste)
        del received[:]
        dump_path = os.path.join(tmp, "paste.dump")
        subprocess.run([ui, "--settings-dir", os.path.join(tmp, "settings"), "--size", "1440x900",
                        "--url", "http://127.0.0.1:%d/" % port, "--add", "data-raw",
                        "--click", "680,%d" % FIRST_VALUE_Y, "--paste-file", paste_path, "--click", "970,%d" % RUN_Y,
                        "--dump", dump_path,
                        "--screenshot", os.path.join(tmp, "paste.bmp")],
                       env=dict(os.environ, SDL_VIDEODRIVER="dummy"), check=True, timeout=120)
        check(len(received) == 1 and received[0] == paste, "pasting %d bytes into a field sends them whole" % len(paste))

        del received[:]
        dump = run_app(ui, tmp, "too-big", port, b"z" * (11 * 1024 * 1024))
        check(not received, "11 MB: curl does not start")
        check("--data-raw is over the 10 MB" in dump, "11 MB: the log says which option and why")
        check("@file" in dump, "11 MB: the log says what to do instead")
        check(not glob.glob(os.path.join(tmp, "settings", "run-*.conf")), "no argument file is left behind")
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
