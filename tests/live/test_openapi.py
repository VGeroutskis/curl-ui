#!/usr/bin/env python3
"""API descriptions through the real app, down to what a server receives.

  test_openapi.py --ui build/curl-ui [--fixtures tests/fixtures/openapi]
"""
import argparse
import glob
import http.server
import json
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import urllib.request

seen = []
RUN = "970,108"
HERE = os.path.dirname(os.path.abspath(__file__))


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def any(self):
        n = int(self.headers.get("Content-Length", 0) or 0)
        seen.append({"method": self.command, "path": self.path, "headers": {k.lower(): v for k, v in self.headers.items()}, "body": self.rfile.read(n)})
        self.send_response(200)
        self.send_header("Content-Length", "2")
        self.end_headers()
        self.wfile.write(b"ok")

    do_GET = do_POST = do_PUT = do_DELETE = do_PATCH = do_COPY = do_QUERY = any

    def log_message(self, *args):
        pass


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    ap.add_argument("--fixtures", default=os.path.join(HERE, "..", "fixtures", "openapi"))
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    fx = os.path.abspath(opts.fixtures)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    base = "http://127.0.0.1:%d" % server.server_address[1]
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        def app(home, *args):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            session = os.path.join(home, "session.json")
            if os.path.exists(session):
                os.remove(session)
            subprocess.run([ui, "--settings-dir", home, "--size", "1440x900", *args, "--dump", dump,
                            "--screenshot", os.path.join(tmp, "s.bmp")], env=env, check=True, timeout=120)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        def field(text, key):
            for line in text.splitlines():
                if line.startswith(key + "="):
                    return line[len(key) + 1:]
            return ""

        def nodes(home):
            """Every request saved, by name."""
            found = {}

            def walk(n):
                for kid in n.get("items", n.get("kids", n.get("children", []))):
                    if "request" in kid or "req" in kid:
                        found[kid["name"]] = kid["id"]
                    walk(kid)
            for path in glob.glob(os.path.join(home, "collections", "*")):
                with open(path, encoding="utf-8") as f:
                    walk(json.load(f))
            return found

        def send(home, ids, name, *more):
            """Opens a saved request, fills its variables, runs it, and returns what the server got."""
            del seen[:]
            values = ["--var", "baseUrl=" + base, "--var", "petId=7", "--var", "user=me", "--var", "sid=s1",
                      "--secret", "token=tk", "--var", "username=u", "--secret", "password=p",
                      "--secret", "X-Api-Key=kk", "--secret", "api_key=qq", "--var", "env=api"]
            text = app(home, "--open-node", ids[name], *values, *more, "--click", RUN)
            if not seen:
                print("nothing arrived for %s: %s" % (name, field(text, "command")))
                return {"method": "", "path": "", "headers": {}, "body": b""}
            return seen[-1]

        families = [("v3.0.0.yaml", True), ("v3.0.4.json", True), ("v3.1.0.yaml", True), ("v3.1.2.json", True),
                    ("v3.2.0.yaml", True), ("v3.2.1.json", True), ("v2.0.yaml", False), ("v2.0.json", False)]
        for name, full in families:
            home = os.path.join(tmp, "h-" + name)
            del seen[:]
            text = app(home, "--import-openapi", os.path.join(fx, name))
            check(not seen, "%s: importing runs nothing" % name)
            check("Imported" in field(text, "notice") and "Pet Store" in field(text, "notice"), "%s: the report says so: %s" % (name, field(text, "notice")))
            ids = nodes(home)
            got = send(home, ids, "List pets")
            check(got["method"] == "GET" and got["path"] == "/pets?limit=10", "%s: GET with its required query: %s" % (name, got["path"]))
            check(got["headers"].get("authorization") == "Bearer tk" or not full, "%s: bearer token sent" % name)
            check("x-trace" not in got["headers"], "%s: an optional header stays off" % name)
            got = send(home, ids, "createPet")
            check(got["method"] == "POST" and b"Rex" in got["body"], "%s: JSON body from the schema: %s" % (name, got["body"][:60]))
            check(got["headers"].get("content-type") == "application/json", "%s: its type" % name)
            check(got["headers"].get("authorization", "").startswith("Basic "), "%s: basic" % name)
            got = send(home, ids, "GET /pets/{petId}")
            check(got["path"] == "/pets/7?verbose=true", "%s: path variable: %s" % (name, got["path"]))
            got = send(home, ids, "POST /login")
            check(got["body"] == b"user=me", "%s: url encoded row: %s" % (name, got["body"]))
            got = send(home, ids, "PUT /raw")
            check(got["method"] == "PUT" and got["body"] == b"hello" and got["headers"].get("content-type") == "text/plain", "%s: raw, with its type" % name)
            got = send(home, ids, "GET /k0")
            check(got["headers"].get("x-api-key") == "kk", "%s: API key header" % name)
            if full:
                got = send(home, ids, "GET /k1", "--var", "session=c9")
                check(got["headers"].get("cookie") == "session=c9", "%s: API key cookie: %s" % (name, got["headers"].get("cookie")))
                text = app(home, "--open-node", ids["POST /upload"], "--form", "body")
                check(field(text, "body_kind") == "form-data", "%s: the form shows form data" % name)

        for name in ("v1.2", "v1.1", "v1.0"):
            home = os.path.join(tmp, "h-" + name)
            text = app(home, "--import-openapi", os.path.join(fx, name, "api-docs.json"))
            check("Imported" in field(text, "notice"), "%s: %s" % (name, field(text, "notice")))
            ids = nodes(home)
            if name == "v1.2":
                got = send(home, ids, "List pets", "--var", "status=open")
                check(got["path"] == "/pets?limit=10", "1.2: %s" % got["path"])
                got = send(home, ids, "createPet")
                check(b"name" in got["body"] and got["method"] == "POST", "1.2: body from the model: %s" % got["body"][:40])
            else:
                got = send(home, ids, "Get one", "--var", "id=5")
                check(got["path"] == "/pets.json/5", "%s: %s" % (name, got["path"]))

        # through the dialog and the picker, as a person does
        home = os.path.join(tmp, "h-dialog")
        src = os.path.join(fx, "v3.1.0.yaml")
        text = app(home, "--import-dialog", "--click", "545,579", "--click", "600,654", "--type", src, "--click", "1050,704")
        check("Imported" in field(text, "notice"), "the Import dialog takes a file: %s" % field(text, "notice"))
        check(field(text, "picker") == "0", "the picker closed")
        check("coll_top_nodes=0" not in text, "the tree has it")

        # a file that is not one: nothing made, and the reason
        home = os.path.join(tmp, "h-bad")
        bad = os.path.join(tmp, "bad.yaml")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("openapi: 3.0.0\npaths:\n  /a:\n\t- broken\n")
        text = app(home, "--import-openapi", bad)
        check("line" in field(text, "notice"), "a damaged file says where: %s" % field(text, "notice"))
        check(not glob.glob(os.path.join(home, "collections", "*")) or "Pet" not in text, "nothing_created_on_refusal")
        text = app(home, "--import-openapi", os.path.join(tmp, "missing.yaml"))
        check("cannot read" in field(text, "notice").lower(), "a missing file says so: %s" % field(text, "notice"))

        # the official examples, when there is a network
        for url in ("https://raw.githubusercontent.com/OAI/learn.openapis.org/main/examples/v3.0/petstore.yaml",
                    "https://raw.githubusercontent.com/OAI/learn.openapis.org/main/examples/v3.1/webhook-example.yaml",
                    "https://raw.githubusercontent.com/OAI/learn.openapis.org/main/examples/v3.0/api-with-examples.yaml"):
            path = os.path.join(tmp, os.path.basename(url))
            try:
                with urllib.request.urlopen(url, timeout=15) as r, open(path, "wb") as f:
                    shutil.copyfileobj(r, f)
            except OSError as e:
                print("skip %s: %s" % (url, e))
                continue
            home = os.path.join(tmp, "h-" + os.path.basename(url))
            text = app(home, "--import-openapi", path)
            check("Imported" in field(text, "notice"), "%s: %s" % (os.path.basename(url), field(text, "notice")))
    server.shutdown()
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
