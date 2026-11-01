#!/usr/bin/env python3
"""Generate ui/src/core/options_data.c from docs/cmdline-opts/*.md.

Usage: gen_options.py [DOCS_DIR] [OUTPUT_FILE]
Standard library only, so it runs anywhere CPython does.
"""
import os
import re
import shlex
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DOCS = os.path.join(HERE, "..", "..", "docs", "cmdline-opts")
DEFAULT_OUT = os.path.join(HERE, "..", "src", "core", "options_data.c")
EXAMPLE_URL = "https://example.com"
# Where the docs' example does not work: --limit-rate 123.45K needs a newer curl
# than many people have, and curl rejects --upload-flags Flagged,!Seen outright.
PLACEHOLDERS = {"limit-rate": "e.g. 100K", "upload-flags": "e.g. answered,seen"}
KINDS = {
    "single": "OPT_KIND_SINGLE",
    "boolean": "OPT_KIND_BOOLEAN",
    "append": "OPT_KIND_APPEND",
    "mutex": "OPT_KIND_MUTEX",
    "custom": "OPT_KIND_CUSTOM",
    "per-URL": "OPT_KIND_PER_URL",
}


# Pairs a real curl refuses that the docs do not declare, in either order. Found by
# `test_args <echoargs> <curl> --pairs`, which tries every pair.
_POST = ("data", "data-ascii", "data-binary", "data-raw", "data-urlencode")
EXTRA_CONFLICTS = [("continue-at", o) for o in ("no-clobber", "range", "remote-header-name", "remove-on-error")]
EXTRA_CONFLICTS += [(d, o) for d in _POST for o in ("form", "form-string", "head", "upload-file")]
EXTRA_CONFLICTS += [("form", "head"), ("form", "json"), ("form", "upload-file"), ("form-string", "head"),
                    ("form-string", "json"), ("form-string", "upload-file"), ("head", "json"),
                    ("head", "upload-file"), ("json", "upload-file")]


def getparam_table(docs):
    """name -> type from curl's own option table: the truth about what parses.
    The docs' Multi: boolean is not, it is wrong for http2-prior-knowledge."""
    path = os.path.join(docs, "..", "..", "src", "tool_getparam.c")
    with open(path, encoding="utf-8") as f:
        text = f.read()
    return {m[0]: m[1] for m in re.findall(r'\{\s*"([a-z0-9.-]+)",\s*(ARG_[A-Z]+)', text)}


def parse(path):
    """Return (front matter dict, body text); list fields become lists."""
    with open(path, encoding="utf-8") as f:
        text = f.read()
    m = re.match(r"---\n(.*?)\n---\n(.*)", text, re.S)
    if not m:
        return {}, ""  # MANPAGE.md and friends are not option docs
    meta, key = {}, None
    for line in m.group(1).split("\n"):
        item = re.match(r"\s+-\s+(.*)", line)
        if item and key:
            if not isinstance(meta[key], list):
                meta[key] = []
            meta[key].append(item.group(1))
            continue
        kv = re.match(r"([A-Za-z-]+):\s*(.*)", line)
        if kv:
            key = kv.group(1)
            meta[key] = kv.group(2)
    return meta, m.group(2)


def as_list(value):
    if not value:
        return []
    return value if isinstance(value, list) else value.split()


def clean_body(name, body):
    body = re.sub(r"^#\s+`?-*%s`?\s*\n" % re.escape(name), "", body.lstrip(), count=1)
    body = body.replace("%VERSION", "VERSION").replace("%DATE", "DATE")
    body = body.replace("%PROTOCOLS", "PROTOCOLS").replace("%GLOBALS", "the global options")
    return body.strip()


def placeholder(name, short, examples, arg):
    """Pull a realistic value for this option out of curl's own examples."""
    flags = {"--" + name}
    if short:
        flags.add("-" + short)
    for example in examples:
        try:
            tokens = shlex.split(example)
        except ValueError:
            continue
        for i, tok in enumerate(tokens[:-1]):
            if tok in flags:
                value = tokens[i + 1]
                if value and value not in ("$URL", EXAMPLE_URL) and len(value) <= 48:
                    return "e.g. " + value
    return arg.strip("<>[]") if arg else ""


def c_string(value):
    """C literal split per line: MSVC limits one literal to 16 KB."""
    if value is None or value == "":
        return '""'
    parts = []
    for line in value.splitlines(True):
        out = []
        for ch in line:
            code = ord(ch)
            if ch == "\\":
                out.append("\\\\")
            elif ch == '"':
                out.append('\\"')
            elif ch == "\n":
                out.append("\\n")
            elif ch == "\t":
                out.append("\\t")
            elif ch == "?":
                out.append("\\?")  # no accidental trigraphs
            elif code < 0x20 or code > 0x7e:
                for byte in ch.encode("utf-8"):
                    out.append('""\\x%02x""' % byte)
            else:
                out.append(ch)
        parts.append('"' + "".join(out) + '"')
    return "\n    ".join(parts)


def c_list(symbol, items, out):
    if not items:
        return "NULL"
    out.append("static const char *const %s[] = {" % symbol)
    for item in items:
        out.append("  %s," % c_string(item))
    out.append("  NULL\n};")
    return symbol


def main():
    docs = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DOCS
    target = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT
    table = getparam_table(docs)
    records = []
    for fname in sorted(os.listdir(docs)):
        if not fname.endswith(".md") or fname.startswith("_"):
            continue
        meta, body = parse(os.path.join(docs, fname))
        if "Long" not in meta:
            continue
        name = meta["Long"]
        short = meta.get("Short", "")
        arg = meta.get("Arg", "")
        multi = meta.get("Multi", "single")
        if multi not in KINDS:
            raise SystemExit("%s: unknown Multi: %s" % (fname, multi))
        raw = as_list(meta.get("Example"))
        if isinstance(meta.get("Example"), str):
            raw = [meta["Example"]]
        examples = ["curl " + e.replace("$URL", EXAMPLE_URL) for e in raw]
        # --no-foo is documented under its own name, but the table lists foo
        base = name[3:] if name.startswith("no-") and name not in table else name
        kind = table.get(base)
        if not kind:
            raise SystemExit("%s: --%s is not in tool_getparam.c" % (fname, name))
        if bool(arg) != (kind not in ("ARG_NONE", "ARG_BOOL")):
            raise SystemExit("%s: docs and tool_getparam.c disagree about the argument" % fname)
        neg = ""
        # --no-version and --no-manual parse, but there is nothing to turn off
        if kind == "ARG_BOOL" and name not in ("version", "manual"):
            neg = base if base != name else "no-" + name
        repeat = bool(arg) and multi in ("append", "per-URL")
        description = clean_body(name, body)
        if len(description) > 60000:
            raise SystemExit("%s: description too long for MSVC" % fname)
        records.append(dict(
            name=name, short=short, arg=arg, help=meta.get("Help", ""),
            categories=meta.get("Category", ""),
            protocols=meta.get("Protocols", ""), added=meta.get("Added", ""),
            kind=KINDS[multi], neg=neg, repeat=repeat,
            mutex=as_list(meta.get("Mutexed")),
            see=as_list(meta.get("See-also")), examples=examples,
            placeholder=PLACEHOLDERS.get(name) or placeholder(name, short, raw, arg),
            description=description,
            deprecated="deprecated" in meta.get("Category", "").split(),
        ))
    records.sort(key=lambda r: r["name"])
    by_name = {r["name"]: r for r in records}
    for a, b in EXTRA_CONFLICTS:
        for x, y in ((a, b), (b, a)):
            if y not in by_name[x]["mutex"]:
                by_name[x]["mutex"].append(y)

    out = ["/* GENERATED by ui/tools/gen_options.py from docs/cmdline-opts. "
           "Do not edit. */", '#include "options.h"', "#include <stddef.h>", ""]
    for i, r in enumerate(records):
        r["mutex_sym"] = c_list("mx_%d" % i, r["mutex"], out)
        r["see_sym"] = c_list("see_%d" % i, r["see"], out)
        r["ex_sym"] = c_list("ex_%d" % i, r["examples"], out)
    out.append("const opt_def g_options[] = {")
    for r in records:
        out.append("{ %s, %s, %s, %s,\n  %s, %s, %s,\n  %s, %s, %d, %d,\n  %s, %s, %s, %s,\n  %s\n}," % (
            c_string(r["name"]), c_string(r["short"]), c_string(r["arg"]), c_string(r["help"]),
            c_string(r["categories"]), c_string(r["protocols"]), c_string(r["added"]),
            r["kind"], c_string(r["neg"]), int(r["repeat"]), int(r["deprecated"]),
            r["mutex_sym"], r["see_sym"], r["ex_sym"], c_string(r["placeholder"]),
            c_string(r["description"])))
    out.append("};")
    out.append("const int g_option_count = %d;" % len(records))
    with open(target, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(out) + "\n")
    print("wrote %d options to %s" % (len(records), target))


if __name__ == "__main__":
    main()
