#!/usr/bin/env python3
"""Saving and reopening through the real app, and the promise that values of variables stay in
the one file meant for them.

  test_store.py --ui build/curl-ui
"""
import argparse
import glob
import json
import os
import subprocess
import sys
import tempfile

SECRET = "UNIQUE-secret-value-7f3a"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    opts = ap.parse_args()
    ui = os.path.abspath(opts.ui)
    fails = 0

    def check(cond, what):
        nonlocal fails
        print(("ok   " if cond else "FAIL ") + what)
        fails += 0 if cond else 1

    with tempfile.TemporaryDirectory() as tmp:
        settings = os.path.join(tmp, "s")

        def app(*args):
            dump = os.path.join(tmp, "dump.txt")
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            subprocess.run([ui, "--settings-dir", settings, *args, "--dump", dump,
                            "--screenshot", os.path.join(tmp, "s.bmp")], env=env, check=True, timeout=60)
            with open(dump, encoding="utf-8") as f:
                return f.read()

        text = app("--secret", "tok=" + SECRET, "--var", "base=http://127.0.0.1:9", "--url", "{{base}}/login",
                   "--method", "POST", "--add", "header=Authorization: Bearer {{tok}}", "--run",
                   "--save-as", "My API/Auth/Login")
        check("dirty=0" in text, "saved: the tab is clean")
        files = glob.glob(os.path.join(settings, "collections", "*.json"))
        check(len(files) == 1 and len(os.path.basename(files[0])) == 21, "one collection, named by its id")
        with open(files[0], encoding="utf-8") as f:
            data = json.load(f)
        check(data["format"] == "curl-ui-collection" and data["version"] == 1 and data["name"] == "My API", "the format")
        login = data["items"][0]["items"][0]
        check(login["name"] == "Login" and login["request"]["url"] == "{{base}}/login", "the request keeps {{base}}")
        check(login["request"]["method"] == "POST", "the method")

        # every file holding anything but the table must be free of the value
        app("--url", "http://127.0.0.1:9/{{tok}}", "--run", "--tab", "--add", "user=me:{{tok}}")
        hits = []
        for root, _, names in os.walk(settings):
            for name in names:
                path = os.path.join(root, name)
                with open(path, "rb") as f:
                    if SECRET.encode() in f.read():
                        hits.append(os.path.relpath(path, settings))
        check(hits == ["variables.json"], "the secret value is only in variables.json: %s" % hits)

        hist = os.path.join(settings, "history.jsonl")
        with open(hist, encoding="utf-8") as f:
            rows = [json.loads(line) for line in f]
        check(len(rows) == 2, "history: a line for each run, even a failed one")
        check(rows[0]["url"] == "{{base}}/login" and rows[0]["method"] == "POST", "history: the request as written")
        check("{{tok}}" in json.dumps(rows[0]["request"]), "history: {{tok}}, not its value")

        text = app()
        check("tabs=2" in text, "tabs come back after a restart")
        text = app("--url", "http://other/", "--open-history", "1")
        check("title=" in text and "/login" in text.split("copy_command=")[1].split("\n")[0], "a history row opens in a tab")

        # open from the tree, change it, and see it as changed
        node = login["id"]
        text = app("--open-node", node)
        check("node=" + node in text and "dirty=0" in text, "opening a saved request: clean, and linked to it")
        text = app("--open-node", node, "--open-node", node)
        check(text.count("node=" + node) == 1, "opening it twice goes to the same tab")
        text = app("--open-node", node, "--add", "location")
        check("dirty=1" in text, "a change marks it")

        # a name no file system should see
        app("--url", "http://x/", "--save-as", "../../evil/r")
        check(not os.path.exists(os.path.join(tmp, "evil")), "a collection name cannot leave the folder")
        check(all(os.path.dirname(p) == os.path.join(settings, "collections")
                  for p in glob.glob(os.path.join(settings, "**", "*.json"), recursive=True) if "collections" in p),
              "all collections are in their folder")

        # damaged, and from the future
        bad = os.path.join(settings, "collections", "damaged.json")
        with open(bad, "w", encoding="utf-8") as f:
            f.write("{ nope")
        text = app()
        check(".bad-1" in text and os.path.exists(bad + ".bad-1"), "a damaged collection is reported and kept")

        # Reopen tabs off
        with open(os.path.join(settings, "settings.ini"), "a", encoding="utf-8") as f:
            f.write("reopen_tabs=0\n")
        app("--url", "http://x/")
        check(not os.path.exists(os.path.join(settings, "session.json")), "Reopen tabs off: no file of open tabs")
        text = app()
        check("tabs=1" in text, "Reopen tabs off: one fresh tab")

        # the old remembered request opens once, then goes
        legacy = os.path.join(tmp, "legacy")
        os.makedirs(legacy)
        with open(os.path.join(legacy, "settings.ini"), "w", encoding="utf-8") as f:
            f.write("remember_request=1\n")
        with open(os.path.join(legacy, "last-request.txt"), "w", encoding="utf-8") as f:
            f.write("method=0\nurl=http://old/\n")
        settings = legacy
        text = app()
        check("http://old/" in text, "an old remembered request opens as a tab")
        check(not os.path.exists(os.path.join(legacy, "last-request.txt")), "and its file goes")
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
