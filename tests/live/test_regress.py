#!/usr/bin/env python3
"""Bugs found in review, each kept from coming back, driven with real clicks at 1440x900.

  test_regress.py --ui build/curl-ui
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile


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
        n = [0]

        def fresh():
            n[0] += 1
            return os.path.join(tmp, "s%d" % n[0])

        def app(settings, *args, shot=True):
            dump = os.path.join(tmp, "dump.txt")
            if os.path.exists(dump):
                os.remove(dump)
            env = dict(os.environ, SDL_VIDEODRIVER="dummy")
            cmd = [ui, "--settings-dir", settings, "--size", "1440x900", *args]
            if shot:
                cmd += ["--dump", dump, "--screenshot", os.path.join(tmp, "s.bmp")]
            rc = subprocess.run(cmd, env=env, timeout=60).returncode
            text = ""
            if os.path.exists(dump):
                with open(dump, encoding="utf-8") as f:
                    text = f.read()
            return rc, text

        rc, text = app(fresh(), "--form", "body", "--click", "779,176")
        check(rc == 0, "Body > File on a new tab does not crash (rc %d)" % rc)

        s = fresh()
        app(s, "--env", "A", "--env", "B", "--env", "Default", "--var", "x=y", "--open-vars",
            "--click", "904,143", "--click", "904,143")
        with open(os.path.join(s, "variables.json"), encoding="utf-8") as f:
            names = [e["name"] for e in json.load(f)["collections"]["0000000000000000"]["environments"]]
        check(names == ["A", "B"], "deleting an environment leaves the others' names: %s" % names)

        rc, text = app(fresh(), "--url", "http://h/", "--form", "auth", "--click", "717,176",
                       "--click", "700,214", "--type", "X-Key", "--click", "700,250", "--type", "abc",
                       "--click", "440,146", "--click", "398,214")
        check("X-Key: abc" in text.split("copy_command=")[1], "editing Headers keeps an API key set in Auth")
        check("X;" not in text.split("copy_command=")[1], "no junk header for each key typed")

        s = fresh()
        app(s, "--var", "a=1", "--open-vars", "--click", "700,227", "--type", "abc")
        with open(os.path.join(s, "variables.json"), encoding="utf-8") as f:
            value = json.load(f)["collections"]["0000000000000000"]["environments"][0]["variables"][0]["value"]
        check("abc" in value, "a value being typed is saved at exit: %r" % value)

        s = fresh()
        app(s, "--url", "http://first/")
        app(s, "--url", "http://other/", "--quit", shot=False)
        with open(os.path.join(s, "session.json"), encoding="utf-8") as f:
            saved = f.read()
        check("http://first/" in saved and "http://other/" in saved, "starting with --url keeps the open tabs")

        rc, text = app(fresh(), "--add", "user=bob:p", "--form", "auth", "--click", "700,214",
                       "--click", "567,146", "--click", "680,227", "--type", "x", "--click", "529,146")
        real = text.split("copy_command=")[1].split("\n")[0]
        check("--user" in real and "auth=" in text, "Auth follows a change made under Curl")
        shown = text.split("auth=")[1].split("\n")[0]
        check(shown.replace(":", "") in real.replace(":", ""), "the form shows it: %s in %s" % (shown, real))

        s = fresh()
        app(s, "--url", "http://x/", "--set-body-off", "off=z", "--save-as", "C/R")
        rc, text = app(s)
        check("body_row=off:z:0" in text, "a body row switched off survives saving and reopening")

        rc, text = app(fresh(), "--secret", "t=SECRETVALUE", "--url", "http://x/{{t}}", "--reveal", "--click", "197,64",
                       "--select-tab", "0")
        check("SECRETVALUE" not in text.split("copy_command=")[0], "the eye closes after leaving the tab by +")

        # the way to a collection's variables, and to its environments, with clicks
        rc, text = app(fresh(), "--mouse", "200,203", "--click", "328,203", "--click", "353,229")
        check("show_vars=1" in text and "vars_for=0000000000000000" in text, "a collection's menu opens its variables")
        rc, text = app(fresh(), "--env", "One", "--env", "Two", "--open-vars", "--click", "420,143", "--click", "420,216")
        check("env=One" in text, "the table's menu changes the environment: %s" % text.split("env=")[1].split("\n")[0])

        s = fresh()
        os.makedirs(s)
        with open(os.path.join(s, "session.json"), "w", encoding="utf-8") as f:
            f.write("{ damaged")
        rc, text = app(s)
        check(os.path.exists(os.path.join(s, "session.json.bad-1")), "a damaged session is kept aside")
        check("notice=" in text and ".bad-1" in text.split("notice=")[1].split("\n")[0], "and reported")

        s = fresh()
        os.makedirs(s)
        with open(os.path.join(s, "collections"), "w", encoding="utf-8") as f:
            f.write("in the way")
        rc, text = app(s, "--url", "http://x/", "--save-as", "C/R")
        check("coll_top_nodes=0" in text, "a failed save leaves no empty entry in the tree")

        s = fresh()
        os.makedirs(s)
        with open(os.path.join(s, "settings.ini"), "w", encoding="utf-8") as f:
            f.write("curl_program=/no/such/curl\n")
        big = os.path.join(tmp, "big.txt")
        with open(big, "w", encoding="utf-8") as f:
            f.write("z" * 300000)
        app(s, "--url", "http://x/", "--add-file", "data-raw=" + big, "--run")
        left = [x for x in os.listdir(s) if x.startswith("run-")]
        check(not left, "no argument file stays when curl cannot start: %s" % left)
    print("%d failed" % fails)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
