#!/usr/bin/env python3
"""Every curl option through the whole UI, against real servers.

Each case starts the real app headless with the option added as a card, presses
Run, and checks what came back: the command shown, the exit code, the status,
the response, the log, the files written, and what a server really received.

Needs network, the `openssl` command, and optionally pyftpdlib (pip install
pyftpdlib) for the FTP upload options. Not part of ctest, public services come
and go.

  live_options.py --ui build/curl-ui [--only NAME,NAME] [--report out.md] [--jobs 4]

Tiers, reported per option:
  effect  the result shows the option did what it says
  ran     curl ran it as expected, but nothing visible from a client proves its
          effect (socket options and the like)
  n/a     this curl build lacks it, and says so
Public services: httpbin.org, badssl.com, test.rebex.net, quic.nginx.org,
nghttp2.org, cloudflare-dns.com, example.com, echo sites. All are meant for this
kind of use or are ordinary GETs. Everything else is local, see servers.py.
"""
import argparse
import concurrent.futures
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import servers  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OPTIONS_DATA = os.path.join(HERE, "..", "..", "src", "core", "options_data.c")
HB = "https://httpbin.org"
REBEX = "test.rebex.net"
CASES = []
# curl's words for "this build cannot do that"
NOT_BUILT = re.compile(r"does not support|doesn't support|not supported|is unknown|not built|was built without|"
                       r"disabled in|not compiled|unsupported", re.I)


def case(name, v=None, url="{HB}/get", x=(), neg=False, code=0, out=None, no_out=None, log=None, no_log=None,
         status=None, rec=None, no_rec=None, file=None, tier="effect", env=None, before=None, local=False,
         allow_na=False, label=None, first=()):
    """x: more options, as "name" or "name=value". code: exit code or tuple.
    file: (path, regex) that must match that file, or (path, None) that it exists.
    first: options added before the one under test, where order matters."""
    CASES.append(dict(name=name, v=v, url=url, x=list(x), neg=neg, code=code, out=out, no_out=no_out, log=log,
                      no_log=no_log, status=status, rec=rec, no_rec=no_rec, file=file, tier=tier, env=env or {},
                      before=before, local=local or rec is not None or no_rec is not None, allow_na=allow_na,
                      label=label or name, first=list(first)))


def ftp_auth(ctx):
    return "user=u:p"


USERPW = "user=demo:password"
SFTP = "sftp://" + REBEX + "/readme.txt"
FTPR = "ftp://" + REBEX + "/readme.txt"
FTPL = "ftp://127.0.0.1:{FTP}/"


def write(name, text):
    def setup(cwd):
        with open(os.path.join(cwd, name), "w") as f:
            f.write(text)
    return setup


def many(*fns):
    def setup(cwd):
        for fn in fns:
            fn(cwd)
    return setup


NETRC = "machine httpbin.org login nu password np\n"
COOKIES = "# Netscape HTTP Cookie File\n127.0.0.1\tFALSE\t/\tFALSE\t0\tsess\tgone\n" \
          "127.0.0.1\tFALSE\t/\tFALSE\t4102444800\tkept\tyes\n"

# ---- HTTP: what reached the server ----
case("user-agent", "curl-ui-test", url="{HB}/headers", out="curl-ui-test", status=200)
case("header", "X-Curl-Ui: yes", url="{HB}/headers", out="X-Curl-Ui", status=200)
case("referer", "https://ref.example/", url="{HB}/headers", out="ref.example", status=200)
case("cookie", "a=b", url="{HB}/cookies", out='"a": "b"', status=200)
case("data", "k=v", url="{HB}/post", out='"k": "v"', status=200)
case("data-ascii", "k=v", url="{HB}/post", out='"k": "v"', status=200)
case("data-binary", "k=v", url="{HB}/post", out='"k": "v"', status=200)
case("data-raw", "@notafile", url="{HB}/post", out="@notafile", status=200)
case("data-urlencode", "k=a b", url="{HB}/post", out='"k": "a b"', status=200)
case("json", '{"j":1}', url="{HB}/post", out='"j": 1', status=200)
case("form", "k=v", url="{HB}/post", out='"k": "v"', status=200)
case("form-string", "k=@v", url="{HB}/post", out='"k": "@v"', status=200)
case("form-escape", x=['form=k"e=v'], url="{HB}/post", out="k%22e|k\\\\\"e", status=200)
case("get", x=["data=k=v"], url="{HB}/get", out='"k": "v"', status=200)
case("head", url="{HB}/get", log=r"HEAD /get", status=200)
case("request", "PUT", url="{HB}/put", out='"url"', log="PUT /put", status=200)
case("request-target", "/get?t=1", url="{HB}/nothing", out='"t": "1"', status=200)
case("url-query", "q=1", url="{HB}/get", out='"q": "1"', status=200)
case("url", "{HB}/get?viaurl=1", url="", out='"viaurl": "1"', status=200)
case("upload-file", "up.txt", url="{HB}/put", out="uploaded body", status=200, before=write("up.txt", "uploaded body"))
case("globoff", url="{HB}/anything?a=[1]", out=r"a=\[1\]|\[1\]", status=200)
case("path-as-is", url="{HB}/a/../get", log=r"GET /a/\.\./get", status=None, code=0)
case("range", "0-4", url="{HB}/range/10", out="^abcde$", status=206)
case("continue-at", "5", url="{HB}/range/10", out="fghij", status=206)
case("compressed", url="{HB}/gzip", out='"gzipped": true', status=200)
case("tr-encoding", url="{HB}/get", log="TE: gzip", status=200)
case("no-buffer", url="{HB}/get", status=200, out="httpbin", tier="ran")
case("ignore-content-length", url="{HB}/get", status=200, out="httpbin", tier="ran")
case("time-cond", "19 Oct 2020 10:00:00 GMT", url="{E}/old", status=304, rec="If-Modified-Since")
case("raw", url="{HB}/get", status=200, out="httpbin", tier="ran")
case("http1.0", url="{HB}/get", log=r"GET /get HTTP/1\.0")
case("http1.1", url="{HB}/get", log=r"GET /get HTTP/1\.1")
case("http2", url="https://nghttp2.org/httpbin/get", log=r"HTTP/2 200|using HTTP/2")
case("http2-prior-knowledge", url="https://nghttp2.org/httpbin/get", log=r"HTTP/2")
case("http3", url="https://quic.nginx.org/", log=r"HTTP/3|h3", allow_na=True)
case("http3-only", url="https://quic.nginx.org/", log=r"HTTP/3|h3", allow_na=True)
case("http0.9", url="http://127.0.0.1:{H09}/", out="plain http0.9 body", local=True)
case("no-alpn", url="{HB}/get", no_log=r"ALPN: curl offers", status=200)
case("no-npn", url="{HB}/get", status=200, tier="ran")
case("expect100-timeout", "1", x=["header=Expect: 100-continue", "data=k=v"], url="{HB}/post", log="Expect: 100-continue")
case("suppress-connect-headers", x=["proxy=http://127.0.0.1:{P}", "proxytunnel", "show-headers"], url="{HB}/get",
     no_out="Connection established", out="HTTP/", status=200)

# ---- redirects ----
case("location", url="{HB}/redirect/1", status=200, out='"url"')
case("follow", url="{HB}/redirect/1", status=200, out='"url"', allow_na=True)
case("location-trusted", x=["user=a:b", "header=X-T: 1"], url="{HB}/redirect-to?url=https://httpbin.org/headers",
     out="Authorization", status=200)
case("max-redirs", "0", x=["location"], url="{HB}/redirect/2", code=47)
case("post301", x=["location", "data=k=v"], url="{HB}/redirect-to?url=/anything&status_code=301", out='"method": "POST"')
case("post302", x=["location", "data=k=v"], url="{HB}/redirect-to?url=/anything&status_code=302", out='"method": "POST"')
case("post303", x=["location", "data=k=v"], url="{HB}/redirect-to?url=/anything&status_code=303", out='"method": "POST"')
case("proto-redir", "=https", x=["location"], url="{HB}/redirect-to?url=http://example.com/", code=1)

# ---- auth ----
case("user", "a:b", url="{HB}/basic-auth/a/b", status=200, out='"authorized": true')
case("basic", x=["user=a:b"], url="{HB}/basic-auth/a/b", status=200, log="Authorization: Basic")
case("digest", x=["user=a:b"], url="{HB}/digest-auth/auth/a/b", status=200, log="Authorization: Digest")
case("anyauth", x=["user=a:b"], url="{HB}/digest-auth/auth/a/b", status=200, log="Digest")
case("ntlm", x=["user=a:b"], url="{HB}/get", status=200, tier="ran", allow_na=True)
case("ntlm-wb", x=["user=a:b"], url="{HB}/get", tier="ran", code=(0, 2), allow_na=True)
case("negotiate", x=["user=:"], url="{HB}/get", status=200, tier="ran", allow_na=True)
case("oauth2-bearer", "tok123", url="{HB}/bearer", status=200, out="tok123")
case("aws-sigv4", "aws:amz:us-east-1:s3", x=["user=k:s"], url="{HB}/headers", out="AWS4-HMAC-SHA256", status=200)
case("netrc", url="{HB}/basic-auth/nu/np", status=200, env={"HOME": "{CWD}"}, before=write(".netrc", NETRC))
case("netrc-file", "n.txt", url="{HB}/basic-auth/nu/np", status=200, before=write("n.txt", NETRC))
case("netrc-optional", url="{HB}/basic-auth/nu/np", status=200, env={"HOME": "{CWD}"}, before=write(".netrc", NETRC))
case("disallow-username-in-url", url="https://a:b@httpbin.org/get", code=67)
case("delegation", "none", x=["negotiate"], url="{HB}/get", tier="ran", allow_na=True)
case("service-name", "svc", x=["negotiate"], url="{HB}/get", tier="ran", allow_na=True)
case("sasl-authzid", "zid", x=["user=u:p"], url="imap://127.0.0.1:{IMAP}/", rec="AUTHENTICATE", allow_na=True)
case("sasl-ir", x=["user=u:p"], url="imap://127.0.0.1:{IMAP}/", rec=r"AUTHENTICATE PLAIN \S+")
case("login-options", "AUTH=PLAIN", x=["user=u:p"], url="imap://127.0.0.1:{IMAP}/", rec="AUTHENTICATE PLAIN")

# ---- output and files ----
case("output", "o.txt", url="{HB}/get", file=("o.txt", "httpbin"), no_out="httpbin", status=200)
case("output-dir", "od", x=["create-dirs", "output=o.txt"], url="{HB}/get", file=("od/o.txt", "httpbin"), status=200)
case("create-dirs", x=["output=a/b/c.txt"], url="{HB}/get", file=("a/b/c.txt", "httpbin"), status=200)
case("create-file-mode", "0640", x=["upload-file=up.txt"], url="file://{CWD}/m.txt", file=("m.txt:mode", "640"), before=write("up.txt", "x"))
case("remote-name", url="{HB}/get", file=("get", "httpbin"), status=200)
case("remote-name-all", url="{HB}/get", file=("get", "httpbin"), status=200)
case("remote-header-name", x=["remote-name"], url="{HB}/response-headers?Content-Disposition=attachment%3B%20filename%3Dfrom-header.txt",
     file=("from-header.txt", None), status=200)
case("remote-time", x=["output=rt.txt"], url="https://example.com/", file=("rt.txt:old", None), status=200)
case("remove-on-error", x=["output=gone.txt", "fail"], url="{HB}/status/404", code=22, file=("gone.txt:absent", None))
case("no-clobber", x=["output=kept.txt"], url="{HB}/get", file=("kept.txt.1", "httpbin"), before=write("kept.txt", "old"))
case("skip-existing", x=["output=kept.txt"], url="{HB}/get", file=("kept.txt", "^old$"), before=write("kept.txt", "old"))
case("out-null", url="{HB}/get", no_out="httpbin", status=200, allow_na=True)
case("dump-header", "h.txt", url="{HB}/get", file=("h.txt", "HTTP/"), status=200)
case("cookie-jar", "jar.txt", x=["location"], url="{HB}/cookies/set?jarred=1", file=("jar.txt", "jarred"))
case("junk-session-cookies", x=["cookie=c.txt"], url="{E}/", no_rec="sess=gone", before=write("c.txt", COOKIES))
case("etag-save", "e.txt", url="{HB}/etag/abc", file=("e.txt", "abc"), status=200)
case("etag-compare", "e.txt", url="{HB}/etag/abc", status=304, before=write("e.txt", '"abc"\n'))
case("hsts", "hsts.txt", url="{HB}/get", file=("hsts.txt", "httpbin|^# Your HSTS"), tier="ran")
case("alt-svc", "alt.txt", url="https://www.google.com/", file=("alt.txt", "h3"))
case("libcurl", "gen.c", url="{HB}/get", file=("gen.c", "curl_easy_setopt"), status=200)
case("xattr", x=["output=x.txt"], url="{HB}/get", file=("x.txt:xattr", "httpbin"), status=200)
case("write-out", "wo=%{http_code}", url="{HB}/get", out="wo=200")
case("stderr", "err.txt", url="{HB}/get", file=("err.txt", "GET /get"), no_log="GET /get", status=None)
case("trace", "t.txt", url="{HB}/get", file=("t.txt", "Send header"), status=200)
case("trace-ascii", "t.txt", url="{HB}/get", file=("t.txt", "Send header"), status=200)
case("trace-time", x=["trace=t.txt"], url="{HB}/get", file=("t.txt", r"^\d\d:\d\d:\d\d\.\d+"), status=200)
case("trace-ids", x=["trace=t.txt"], url="{HB}/get", file=("t.txt", r"\[\d+-\d+\]"), status=200)
case("trace-config", "http", x=["verbose"], url="{HB}/get", log="HTTP|http", status=200, allow_na=True)
case("verbose", url="{HB}/get", log="> GET /get", status=200)
case("silent", url="{HB}/get", status=200, out="httpbin", tier="ran")
case("show-error", x=["silent"], url="http://127.0.0.1:1/", code=7, log="curl: \\(7\\)")
case("show-headers", url="{HB}/get", out="HTTP/", status=200)
case("no-progress-meter", url="{HB}/get", status=200, no_log="% Total")
case("progress-bar", url="{HB}/get", status=200, tier="ran")
case("styled-output", url="{HB}/get", status=200, tier="ran")
case("fail", url="{HB}/status/404", code=22, status=404)
case("fail-with-body", url="{HB}/status/404", code=22, status=404)
case("fail-early", x=["url={HB}/get"], url="http://127.0.0.1:1/", code=7)
case("max-filesize", "10", url="{HB}/bytes/1000", code=63)

# ---- several transfers ----
case("next", first=["url={HB}/get?one=1"], url="{HB}/get?two=2", out='"one": "1"[\\s\\S]*"two": "2"')
case("parallel", x=["url={HB}/get?one=1"], url="{HB}/get?two=2", out='"one": "1"')
case("parallel-immediate", x=["parallel", "url={HB}/get?one=1"], url="{HB}/get?two=2", out='"two": "2"')
case("parallel-max", "2", x=["parallel", "url={HB}/get?one=1"], url="{HB}/get?two=2", out='"one": "1"')
case("parallel-max-host", "1", x=["parallel", "url={HB}/get?one=1"], url="{HB}/get?two=2", out='"one": "1"', allow_na=True)
case("rate", "60/m", x=["url={HB}/get?one=1"], url="{HB}/get?two=2", out='"two": "2"', tier="ran")
case("config", "c.txt", url="{HB}/headers", out="from-config", status=200, before=write("c.txt", 'user-agent = "from-config"\n'))
case("disable", url="{HB}/headers", no_out="from-curlrc", status=200, env={"CURL_HOME": "{CWD}"},
     before=write(".curlrc", 'user-agent = "from-curlrc"\n'))
case("variable", "myvar=abc", url="{HB}/get", status=200, log="", tier="ran")
case("help", "all", url="", out="--user-agent")
case("manual", url="", tier="ran", allow_na=True, code=(0, 2))
case("version", url="", out=r"curl \d+\.\d+")
case("dump-ca-embed", url="", tier="ran", code=(0, 2), allow_na=True)
case("metalink", url="{HB}/get", tier="ran", code=(0, 2), allow_na=True)

# ---- timeouts and retries ----
case("max-time", "1", url="{HB}/delay/5", code=28)
case("connect-timeout", "1", url="http://10.255.255.1/", code=(28, 7))
case("happy-eyeballs-timeout-ms", "100", url="{HB}/get", status=200, tier="ran")
case("speed-limit", "1", x=["speed-time=30"], url="{HB}/get", status=200, tier="ran")
case("speed-time", "30", x=["speed-limit=1"], url="{HB}/get", status=200, tier="ran")
case("limit-rate", "100K", url="{HB}/get", status=200, tier="ran")
case("retry", "1", x=["retry-delay=1"], url="{HB}/status/503", log="Will retry|retry", status=503)
case("retry-all-errors", x=["retry=1", "retry-delay=1"], url="http://127.0.0.1:1/", code=7, log="Will retry|retry")
case("retry-connrefused", x=["retry=1", "retry-delay=1"], url="http://127.0.0.1:1/", code=7, log="Will retry|retry")
case("retry-delay", "1", x=["retry=1"], url="{HB}/status/503", log="1 second|retry")
case("retry-max-time", "2", x=["retry=1", "retry-delay=1"], url="{HB}/status/503", log="retry", status=503)
case("keepalive-time", "30", url="{HB}/get", status=200, tier="ran")
case("keepalive-cnt", "3", url="{HB}/get", status=200, tier="ran", allow_na=True)
case("no-keepalive", url="{HB}/get", status=200, tier="ran")
case("no-sessionid", url="{HB}/get", status=200, tier="ran")
case("tcp-nodelay", url="{HB}/get", status=200, tier="ran")
case("tcp-fastopen", url="{HB}/get", status=200, tier="ran", allow_na=True)
case("mptcp", url="{HB}/get", status=200, tier="ran", allow_na=True)
case("ip-tos", "CS1", url="{HB}/get", status=200, tier="ran", allow_na=True)
case("vlan-priority", "1", url="{HB}/get", status=200, tier="ran", allow_na=True)

# ---- name resolution and routing ----
case("resolve", "resolved.example:{EP}:127.0.0.1", url="http://resolved.example:{EP}/", out="resolved.example", local=True)
case("connect-to", "to.example:80:127.0.0.1:{EP}", url="http://to.example/", out="to.example", local=True)
case("ipv4", url="{HB}/get", log=r"(Connected|Established connection) to .*\d+\.\d+\.\d+\.\d+ port", status=200)
case("ipv6", url="https://ipv6.google.com/", log=r"(Connected|Established connection) to .*[0-9a-f]+:[0-9a-f:]+ port", status=200, allow_na=True)
case("interface", "lo", url="{E}/", out="local-echo", local=True)
case("local-port", "30100-30900", url="{E}/", log="Local port|local port|30[1-9]", out="local-echo", local=True, tier="ran")
case("doh-url", "http://127.0.0.1:1/", url="https://example.com/", code=6)  # resolving goes there, and fails
case("doh-url", "https://cloudflare-dns.com/dns-query", url="https://example.com/", status=200, label="doh-url")
case("doh-insecure", x=["doh-url=https://cloudflare-dns.com/dns-query"], url="https://example.com/", status=200, tier="ran")
case("doh-cert-status", x=["doh-url=https://cloudflare-dns.com/dns-query"], url="https://example.com/", tier="ran",
     code=(0, 6, 91), allow_na=True)
case("dns-interface", "lo", url="{HB}/get", tier="ran", allow_na=True)
case("dns-ipv4-addr", "127.0.0.1", url="{HB}/get", tier="ran", allow_na=True)
case("dns-ipv6-addr", "::1", url="{HB}/get", tier="ran", allow_na=True)
case("dns-servers", "1.1.1.1", url="{HB}/get", tier="ran", allow_na=True)
case("unix-socket", "{UNIX}", url="http://localhost/", out="via-unix", local=True)
case("abstract-unix-socket", "{ABSTRACT}", url="http://localhost/", out="via-unix", local=True)
case("noproxy", "httpbin.org", x=["proxy=http://127.0.0.1:1"], url="{HB}/get", status=200, out="httpbin")
case("ipfs-gateway", "{E}", url="ipfs://bafyb/x", out="/ipfs/bafyb", local=True)
case("proto", "=https", url="http://example.com/", code=1)
case("proto-default", "https", url="httpbin.org/get", status=200, log="443")
case("haproxy-protocol", url="http://127.0.0.1:{HAP}/", rec="HAPROXY PROXY TCP4")
case("haproxy-clientip", "1.2.3.4", url="http://127.0.0.1:{HAP}/", rec="HAPROXY PROXY TCP4 1.2.3.4")

# ---- TLS ----
case("insecure", url="https://expired.badssl.com/", status=200, label="insecure")
case("cacert", "{CA}", url="{HB}/get", code=60)
case("capath", "{CADIR}", url="{HB}/get", tier="ran", code=(0, 60))
case("ca-native", url="{HB}/get", status=200, tier="ran", allow_na=True)
case("crlfile", "{CRL}", x=["cacert={CA}"], url="https://127.0.0.1:{TP}/", tier="ran", code=(35, 60, 56, 52, 0), local=True)
case("cert", "{CCERT}", x=["key={CKEY}"], url="https://client.badssl.com/", log="TLS|SSL", tier="ran")
case("cert-type", "PEM", x=["cert={CCERT}", "key={CKEY}"], url="https://client.badssl.com/", tier="ran")
case("key", "{CKEY}", x=["cert={CCERT}"], url="https://client.badssl.com/", tier="ran")
case("key-type", "PEM", x=["cert={CCERT}", "key={CKEY}"], url="https://client.badssl.com/", tier="ran")
case("pass", "secret", x=["cert={CCERTP}", "key={CKEYP}"], url="https://client.badssl.com/", tier="ran")
case("cert-status", url="https://www.cloudflare.com/", tier="ran", code=(0, 91))
case("ciphers", "ECDHE-RSA-AES128-GCM-SHA256", x=["tls-max=1.2"], url="https://tls-v1-2.badssl.com:1012/",
     log="ECDHE-RSA-AES128-GCM-SHA256", status=200)
case("tls13-ciphers", "TLS_AES_128_GCM_SHA256", x=["tlsv1.3"], url="{HB}/get", log="TLS_AES_128_GCM_SHA256|TLSv1.3|rejected",
     code=(0, 35), tier="ran")
case("curves", "X25519", url="https://example.com/", log="(?i)/ x25519 /", status=200)
case("curves", "P-256", url="https://example.com/", log="/ secp256r1 /|/ prime256v1 /", status=200)
case("sigalgs", "ecdsa_secp256r1_sha256:rsa_pss_rsae_sha256", url="https://example.com/", status=200, tier="ran", allow_na=True)
case("tls-max", "1.2", url="{HB}/get", log="TLSv1.2")
case("tlsv1.2", url="{HB}/get", log="TLSv1.[23]")
case("tlsv1.3", url="https://example.com/", log="TLSv1.3", status=200)
case("tlsv1.1", url="{HB}/get", tier="ran", log="TLS")
case("tlsv1.0", url="{HB}/get", tier="ran", log="TLS")
case("tlsv1", url="{HB}/get", tier="ran", log="TLS")
case("sslv2", url="{HB}/get", tier="ran", code=(0, 4), allow_na=True)
case("sslv3", url="{HB}/get", tier="ran", code=(0, 4), allow_na=True)
case("false-start", url="{HB}/get", tier="ran", status=200, allow_na=True)
case("tls-earlydata", url="https://example.com/", tier="ran", status=200, allow_na=True)
case("ssl-allow-beast", url="{HB}/get", tier="ran", status=200)
case("ssl-auto-client-cert", url="{HB}/get", tier="ran", status=200)
case("ssl-no-revoke", url="{HB}/get", tier="ran", status=200)
case("ssl-revoke-best-effort", url="{HB}/get", tier="ran", status=200)
case("pinnedpubkey", "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", url="{HB}/get", code=90)
case("ech", "false", url="{HB}/get", tier="ran", allow_na=True)
case("engine", "list", url="", out="Build-time engines", tier="ran", code=(0,), allow_na=True)
case("egd-file", "x", url="{HB}/get", tier="ran", status=200)
case("random-file", "{CA}", url="{HB}/get", tier="ran", status=200)
case("ssl-sessions", "s.txt", url="{HB}/get", tier="ran", allow_na=True)
case("tlsuser", "u", x=["tlspassword=p", "tlsauthtype=SRP"], url="{HB}/get", tier="ran", code=(0, 1, 4, 35), allow_na=True)
case("tlspassword", "p", x=["tlsuser=u", "tlsauthtype=SRP"], url="{HB}/get", tier="ran", code=(0, 1, 4, 35), allow_na=True)
case("tlsauthtype", "SRP", x=["tlsuser=u", "tlspassword=p"], url="{HB}/get", tier="ran", code=(0, 1, 4, 35), allow_na=True)

# ---- proxies, all local so that what they received can be read ----
PX = "http://127.0.0.1:{P}"
TPX = "https://localhost:{TP}"
case("proxy", PX, url="http://example.com/", out="local-proxy", rec="PROXY GET http://example.com/")
case("preproxy", "socks5://127.0.0.1:{S}", x=["proxy=" + PX], url="http://example.com/", out="local-proxy", rec="SOCKS5")
case("proxy1.0", "127.0.0.1:{P}", url="https://example.com/", status=200, rec=r"PROXY CONNECT example.com:443")
case("proxytunnel", x=["proxy=" + PX], url="{HB}/get", rec="PROXY CONNECT httpbin.org:443", status=200)
case("proxy-user", "pu:pp", x=["proxy=" + PX], url="http://example.com/", rec="Proxy-Authorization: Basic")
case("proxy-basic", x=["proxy=" + PX, "proxy-user=pu:pp"], url="http://example.com/", rec="Proxy-Authorization: Basic")
case("proxy-digest", x=["proxy=http://127.0.0.1:{PD}", "proxy-user=pu:pp"], url="http://example.com/", rec="Proxy-Authorization: Digest")
case("proxy-anyauth", x=["proxy=http://127.0.0.1:{PD}", "proxy-user=pu:pp"], url="http://example.com/", rec="Proxy-Authorization: Digest")
case("proxy-ntlm", x=["proxy=" + PX, "proxy-user=pu:pp"], url="http://example.com/", tier="ran", allow_na=True, local=True)
case("proxy-negotiate", x=["proxy=" + PX, "proxy-user=:"], url="http://example.com/", tier="ran", allow_na=True, local=True)
case("proxy-header", "X-To-Proxy: 1", x=["proxy=" + PX], url="http://example.com/", rec="X-To-Proxy: 1")
case("proxy-service-name", "svc", x=["proxy=" + PX, "proxy-negotiate", "proxy-user=:"], url="http://example.com/", tier="ran", allow_na=True, local=True)
case("socks4", "127.0.0.1:{S}", url="http://127.0.0.1:{EP}/", out="local-echo", rec="SOCKS4 127.0.0.1")
case("socks4a", "127.0.0.1:{S}", url="http://localhost:{EP}/", out="local-echo", rec="SOCKS4A localhost")
case("socks5", "127.0.0.1:{S}", url="http://127.0.0.1:{EP}/", out="local-echo", rec="SOCKS5 ip 127.0.0.1")
case("socks5-hostname", "127.0.0.1:{S}", url="http://localhost:{EP}/", out="local-echo", rec="SOCKS5 hostname localhost")
case("socks5-basic", x=["proxy=socks5://su:sp@127.0.0.1:{S}"], url="http://127.0.0.1:{EP}/", out="local-echo", rec="SOCKS5 AUTH user=su")
case("socks5-gssapi", x=["proxy=socks5://127.0.0.1:{S}"], url="http://127.0.0.1:{EP}/", tier="ran", allow_na=True, local=True)
case("socks5-gssapi-nec", x=["proxy=socks5://127.0.0.1:{S}"], url="http://127.0.0.1:{EP}/", tier="ran", allow_na=True, local=True)
case("socks5-gssapi-service", "svc", x=["proxy=socks5://127.0.0.1:{S}"], url="http://127.0.0.1:{EP}/", tier="ran", allow_na=True, local=True)
case("proxy-insecure", x=["proxy=" + TPX], url="http://example.com/", out="local-proxy", rec="PROXY GET")
case("proxy-cacert", "{CA}", x=["proxy=" + TPX], url="http://example.com/", out="local-proxy", rec="PROXY GET")
case("proxy-capath", "{CADIR}", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", out="local-proxy", tier="ran", local=True)
case("proxy-ca-native", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", local=True, allow_na=True)
case("proxy-crlfile", "{CRL}", x=["proxy=" + TPX, "proxy-cacert={CA}"], url="http://example.com/", tier="ran", code=(0, 35, 60, 56), local=True)
case("proxy-cert", "{CCERT}", x=["proxy=" + TPX, "proxy-insecure", "proxy-key={CKEY}"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-key", "{CKEY}", x=["proxy=" + TPX, "proxy-insecure", "proxy-cert={CCERT}"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-cert-type", "PEM", x=["proxy=" + TPX, "proxy-insecure", "proxy-cert={CCERT}", "proxy-key={CKEY}"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-key-type", "PEM", x=["proxy=" + TPX, "proxy-insecure", "proxy-cert={CCERT}", "proxy-key={CKEY}"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-pass", "secret", x=["proxy=" + TPX, "proxy-insecure", "proxy-cert={CCERTP}", "proxy-key={CKEYP}"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-ciphers", "ECDHE-RSA-AES128-GCM-SHA256", x=["proxy=" + TPX, "proxy-insecure", "proxy-tlsv1"], url="http://example.com/", tier="ran", code=(0, 35), local=True)
case("proxy-tls13-ciphers", "TLS_AES_128_GCM_SHA256", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-tlsv1", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-http2", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", allow_na=True, out="local-proxy", local=True)
case("proxy-http3", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", allow_na=True, local=True, code=(0, 2, 7, 35))
case("proxy-pinnedpubkey", "sha256//AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", code=90, local=True)
case("proxy-ssl-allow-beast", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-ssl-auto-client-cert", x=["proxy=" + TPX, "proxy-insecure"], url="http://example.com/", tier="ran", out="local-proxy", local=True)
case("proxy-tlsuser", "u", x=["proxy=" + TPX, "proxy-insecure", "proxy-tlspassword=p", "proxy-tlsauthtype=SRP"], url="http://example.com/", tier="ran", code=(0, 1, 4, 35), allow_na=True, local=True)
case("proxy-tlspassword", "p", x=["proxy=" + TPX, "proxy-insecure", "proxy-tlsuser=u", "proxy-tlsauthtype=SRP"], url="http://example.com/", tier="ran", code=(0, 1, 4, 35), allow_na=True, local=True)
case("proxy-tlsauthtype", "SRP", x=["proxy=" + TPX, "proxy-insecure", "proxy-tlsuser=u", "proxy-tlspassword=p"], url="http://example.com/", tier="ran", code=(0, 1, 4, 35), allow_na=True, local=True)

# ---- FTP: reading from a public server, writing to a local one ----
L = ["user=u:p"]
case("list-only", x=[USERPW], url="ftp://" + REBEX + "/", out="readme.txt", no_out="drwx|-rw")
case("use-ascii", x=[USERPW], url=FTPR, log="TYPE A", out="Welcome")
case("ftp-pasv", x=[USERPW], url=FTPR, log="EPSV|PASV", out="Welcome")
case("disable-epsv", x=[USERPW], url=FTPR, log="PASV", no_log="> EPSV", out="Welcome")
case("disable-eprt", x=L + ["ftp-port=127.0.0.1"], url=FTPL, log="PORT", no_log="> EPRT", local=True)
case("ftp-port", "127.0.0.1", x=L, url=FTPL, log="EPRT|PORT", local=True)
case("ftp-method", "nocwd", x=L, url=FTPL + "seed.txt", no_log="> CWD", log="seed|RETR", local=True)
case("ftp-skip-pasv-ip", x=L + ["disable-epsv"], url=FTPL, log="PASV", local=True)
case("ftp-pret", x=L, url=FTPL, tier="ran", log="PRET", code=(0, 84), local=True)
case("ftp-create-dirs", x=L + ["upload-file=up.txt"], url=FTPL + "nd/up.txt", file=("{FTPROOT}/nd/up.txt", "uploaded"), before=write("up.txt", "uploaded"), local=True)
case("append", x=L + ["upload-file=up.txt"], url=FTPL + "app.txt", log="APPE", before=write("up.txt", "uploaded"), local=True)
case("crlf", x=L + ["upload-file=up.txt"], url=FTPL + "crlf.txt", file=("{FTPROOT}/crlf.txt:crlf", None), before=write("up.txt", "a\nb\n"), local=True)
case("quote", "NOOP", x=L, url=FTPL, log="> NOOP", local=True)
case("ftp-account", "acct1", x=L, url=FTPL, tier="ran", code=(0, 11, 67), local=True)
case("ftp-alternative-to-user", "NOOP", x=["user=bad:bad"], url=FTPL, log="NOOP", code=(0, 13, 67), local=True)
case("ssl", x=[USERPW], url=FTPR, log="AUTH TLS|AUTH SSL", out="Welcome")
case("ftp-ssl-control", x=[USERPW], url=FTPR, log="AUTH TLS|AUTH SSL")
case("ssl-reqd", x=[USERPW], url=FTPR, log="AUTH TLS|AUTH SSL", out="Welcome")
case("ftp-ssl-ccc", x=[USERPW, "ssl-reqd"], url=FTPR, log="> CCC", code=(0, 80, 56, 35, 78, 9))
case("ftp-ssl-ccc-mode", "active", x=[USERPW, "ssl-reqd", "ftp-ssl-ccc"], url=FTPR, log="> CCC", code=(0, 80, 56, 35, 78, 9))
case("krb", "clear", x=L, url=FTPL, tier="ran", code=(0, 2, 67), allow_na=True, local=True)

# ---- SSH ----
K = "insecure"
case("compressed-ssh", x=[USERPW, K], url=SFTP, out="Welcome", tier="ran")
case("hostpubmd5", "00000000000000000000000000000000", x=[USERPW], url=SFTP, code=60)
case("hostpubsha256", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", x=[USERPW], url=SFTP, code=60, allow_na=True)
case("knownhosts", "empty.txt", x=[USERPW], url=SFTP, code=60, before=write("empty.txt", ""))
case("pubkey", "{SSHPUB}", x=["key={SSHKEY}", "user=demo:", K], url=SFTP, tier="ran", code=(0, 67))

# ---- mail, TFTP, telnet and more, local ----
case("mail-from", "a@x.example", x=["mail-rcpt=b@x.example", "upload-file=m.txt"], url="smtp://127.0.0.1:{SMTP}/", rec="MAIL FROM:<a@x.example>", before=write("m.txt", "hello\n"))
case("mail-rcpt", "b@x.example", x=["mail-from=a@x.example", "upload-file=m.txt"], url="smtp://127.0.0.1:{SMTP}/", rec="RCPT TO:<b@x.example>", before=write("m.txt", "hello\n"))
case("mail-rcpt-allowfails", x=["mail-from=a@x.example", "mail-rcpt=bad@x.example", "mail-rcpt=ok@x.example", "upload-file=m.txt"], url="smtp://127.0.0.1:{SMTP}/", rec="RCPT TO:<ok@x.example>", before=write("m.txt", "hello\n"))
case("mail-auth", "auth@x.example", x=["user=u:p", "mail-from=a@x.example", "mail-rcpt=b@x.example", "upload-file=m.txt"], url="smtp://127.0.0.1:{SMTP}/", rec="MAIL FROM:<a@x.example> AUTH=<?auth@x.example", before=write("m.txt", "hello\n"))
case("upload-flags", "answered,seen", x=["user=u:p", "upload-file=m.txt"], url="imap://127.0.0.1:{IMAP}/INBOX", rec=r"APPEND .*\\Answered", before=write("m.txt", "hello\n"), allow_na=True)
case("tftp-blksize", "1024", url="tftp://127.0.0.1:{TFTP}/f", rec="blksize 1024", out="tftp body")
case("tftp-no-options", x=["tftp-blksize=1024"], url="tftp://127.0.0.1:{TFTP}/f", no_rec="blksize", out="tftp body")
case("telnet-option", "TTYPE=vt100", url="telnet://127.0.0.1:{TEL}/", out="hello from telnet", rec="TELNET", code=(0, 28))
case("httpsig-algo", "ed25519", x=["httpsig-keyid=k", "httpsig-key=abc"], url="{HB}/headers", allow_na=True, tier="ran", code=(0, 2))
case("httpsig-headers", "@method", x=["httpsig-keyid=k", "httpsig-key=abc"], url="{HB}/headers", allow_na=True, tier="ran", code=(0, 2))
case("httpsig-key", "abc", x=["httpsig-keyid=k"], url="{HB}/headers", allow_na=True, tier="ran", code=(0, 2))
case("httpsig-keyid", "k", x=["httpsig-key=abc"], url="{HB}/headers", allow_na=True, tier="ran", code=(0, 2))

# ---- --no- forms, and protocols only a public server has ----
case("location", neg=True, url="{HB}/redirect/1", status=302, label="no-location")
case("verbose", neg=True, url="{HB}/get", status=200, label="no-verbose")
case("compressed", neg=True, url="{HB}/get", status=200, no_log="Accept-Encoding", label="no-compressed")
case("remote-name", neg=True, url="{HB}/get", file=("get:absent", None), out="httpbin", label="no-remote-name")


def prepare_assets(root):
    """Certificates, keys and a CA for the TLS proxy and client certificate cases."""
    def run(*cmd):
        subprocess.run(cmd, check=True, cwd=root, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-keyout", "ca.key", "-out", "ca.pem",
        "-subj", "/CN=curlui test CA", "-addext", "basicConstraints=critical,CA:TRUE")
    run("openssl", "req", "-newkey", "rsa:2048", "-nodes", "-keyout", "srv.key", "-out", "srv.csr", "-subj", "/CN=localhost")
    with open(os.path.join(root, "san.ext"), "w") as f:
        f.write("subjectAltName=DNS:localhost,IP:127.0.0.1\n")
    run("openssl", "x509", "-req", "-in", "srv.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-CAcreateserial", "-days", "2",
        "-out", "srv.pem", "-extfile", "san.ext")
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "2", "-keyout", "client.key", "-out", "client.pem",
        "-subj", "/CN=curlui client")
    run("openssl", "req", "-x509", "-newkey", "rsa:2048", "-passout", "pass:secret", "-days", "2", "-keyout", "clientp.key",
        "-out", "clientp.pem", "-subj", "/CN=curlui client pw")
    with open(os.path.join(root, "ca.cnf"), "w") as f:
        f.write("[ca]\ndefault_ca=d\n[d]\ndatabase=index.txt\ncrlnumber=crlnumber\ndefault_md=sha256\ndefault_crl_days=2\n")
    open(os.path.join(root, "index.txt"), "w").close()
    with open(os.path.join(root, "crlnumber"), "w") as f:
        f.write("01\n")
    run("openssl", "ca", "-config", "ca.cnf", "-gencrl", "-keyfile", "ca.key", "-cert", "ca.pem", "-out", "crl.pem")
    run("ssh-keygen", "-q", "-t", "ed25519", "-N", "", "-f", "ssh.key")
    os.rename(os.path.join(root, "ssh.key.pub"), os.path.join(root, "ssh.pub"))
    os.mkdir(os.path.join(root, "cadir"))
    shutil.copy(os.path.join(root, "ca.pem"), os.path.join(root, "cadir", "ca.pem"))
    subprocess.run(["openssl", "rehash", os.path.join(root, "cadir")], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def start_servers(root):
    import ssl as sslmod
    record = servers.Record()
    ctx = {"HB": HB, "CWD": "{CWD}"}

    def http_server(handler, server_class=servers.ThreadedHTTP):
        server = server_class(("127.0.0.1", 0), handler)
        server.record = record
        return servers.start(server)

    ctx["EP"] = str(http_server(servers.Echo))
    ctx["E"] = "http://127.0.0.1:" + ctx["EP"]
    ctx["P"] = str(http_server(servers.Proxy))

    class Digest(servers.Proxy):
        want_auth = "Digest"
    ctx["PD"] = str(http_server(Digest))

    tls = sslmod.SSLContext(sslmod.PROTOCOL_TLS_SERVER)
    tls.load_cert_chain(os.path.join(root, "srv.pem"), os.path.join(root, "srv.key"))
    tls_server = servers.TLSProxy(("127.0.0.1", 0), servers.Proxy)
    tls_server.context = tls
    tls_server.record = record
    ctx["TP"] = str(servers.start(tls_server))

    ctx["S"] = str(servers.serve(servers.Socks, record))
    ctx["SMTP"] = str(servers.serve(servers.SMTP, record))
    ctx["IMAP"] = str(servers.serve(servers.IMAP, record))
    ctx["POP3"] = str(servers.serve(servers.POP3, record))
    ctx["TEL"] = str(servers.serve(servers.Telnet, record))
    ctx["H09"] = str(servers.serve(servers.RawHTTP09, record))
    ctx["HAP"] = str(servers.serve(servers.HAProxy, record))
    ctx["TFTP"] = str(servers.serve(servers.TFTP, record, servers.ThreadedUDP))

    unix_path = os.path.join(root, "u.sock")
    unix = servers.UnixServer(unix_path, servers.UnixEcho)
    unix.record = record
    servers.start(unix)
    ctx["UNIX"] = unix_path
    abstract_name = "curlui-%d" % os.getpid()
    abstract = servers.UnixServer("\0" + abstract_name, servers.UnixEcho)
    abstract.record = record
    servers.start(abstract)
    ctx["ABSTRACT"] = abstract_name

    ftproot = os.path.join(root, "ftproot")
    os.mkdir(ftproot)
    with open(os.path.join(ftproot, "seed.txt"), "w") as f:
        f.write("seed file\n")
    ftp_port = servers.make_ftp(ftproot, record)
    ctx["FTP"] = str(ftp_port) if ftp_port else ""
    ctx["FTPROOT"] = ftproot
    ctx.update(CA=os.path.join(root, "ca.pem"), CADIR=os.path.join(root, "cadir"), CRL=os.path.join(root, "crl.pem"),
               CCERT=os.path.join(root, "client.pem"), CKEY=os.path.join(root, "client.key"),
               CCERTP=os.path.join(root, "clientp.pem"), CKEYP=os.path.join(root, "clientp.key"),
               SSHKEY=os.path.join(root, "ssh.key"), SSHPUB=os.path.join(root, "ssh.pub"))
    return ctx, record


def fill(text, ctx):
    return re.sub(r"\{([A-Z0-9]+)\}", lambda m: ctx.get(m.group(1), m.group(0)), text)


def parse_dump(path):
    with open(path, errors="replace") as f:
        text = f.read()
    head, _, rest = text.partition("---stdout---\n")
    out, _, log = rest.partition("\n---log---\n")
    info = dict(line.split("=", 1) for line in head.splitlines() if "=" in line)
    return info, out, log


def check_file(spec, cwd, ctx):
    path, want = spec
    path = fill(path, ctx)
    kind = None
    if ":" in os.path.basename(path):
        path, kind = path.rsplit(":", 1)
    full = path if os.path.isabs(path) else os.path.join(cwd, path)
    if kind == "absent":
        return None if not os.path.exists(full) else "%s should not exist" % path
    if not os.path.exists(full):
        return "%s was not written" % path
    if kind == "mode":
        mode = "%o" % stat.S_IMODE(os.stat(full).st_mode)
        return None if mode == want else "%s has mode %s" % (path, mode)
    if kind == "old":
        return None if os.stat(full).st_mtime < time.time() - 3600 else "%s has a current time" % path
    if kind == "crlf":
        with open(full, "rb") as f:
            return None if b"\r\n" in f.read() else "%s has no CRLF" % path
    if kind == "xattr":
        value = os.getxattr(full, "user.xdg.origin.url").decode()
        return None if re.search(want, value) else "xattr is %r" % value
    if want:
        with open(full, errors="replace") as f:
            content = f.read()
        if not re.search(want, content, re.M):
            return "%s does not match %r: %.100r" % (path, want, content)
    return None


def run_case(c, opts, ctx, record, cwd):
    local_ctx = dict(ctx, CWD=cwd)
    if c["before"]:
        c["before"](cwd)
    dump = os.path.join(cwd, ".dump")
    argv = [opts.ui, "--run", "--dump", dump, "--screenshot", os.path.join(cwd, ".shot.bmp")]
    url = fill(c["url"], local_ctx)
    if url:
        argv += ["--url", url]
    for extra in c["first"]:
        argv += ["--add", fill(extra, local_ctx)]
    argv += ["--neg", c["name"]] if c["neg"] else ["--add", c["name"] + ("=" + fill(c["v"], local_ctx) if c["v"] else "")]
    for extra in c["x"]:
        argv += ["--add", fill(extra, local_ctx)]
    env = {k: v for k, v in os.environ.items() if k not in ("LD_LIBRARY_PATH", "CURL_HOME")}
    env["SDL_VIDEODRIVER"] = "dummy"
    if opts.curl_dir:
        env["PATH"] = opts.curl_dir + os.pathsep + env["PATH"]
    env.setdefault("HOME", cwd)
    for key, value in c["env"].items():
        env[key] = fill(value, local_ctx)
    if not c["env"]:
        env["CURL_HOME"] = cwd  # keep the user's ~/.curlrc out of it
    before_rec = len(record.text()) if c["local"] else 0
    try:
        p = subprocess.run(argv, cwd=cwd, env=env, capture_output=True, timeout=90)
    except subprocess.TimeoutExpired:
        return "fail", "the app did not finish in 90 s", ""
    if p.returncode or not os.path.exists(dump):
        return "fail", "the app exited %d: %s" % (p.returncode, p.stderr.decode(errors="replace")[:200]), ""
    info, out, log = parse_dump(dump)
    command = info.get("command", "")
    code = int(info.get("exit", -1))
    status = int(info.get("http", 0))
    problems = []
    flag = "--" + (c["label"] if c["neg"] else c["name"])
    if flag not in command:
        problems.append("%s is not in the command: %s" % (flag, command))
    if not info.get("had_result") == "1":
        problems.append("the run produced no result")
    if not c["allow_na"] and NOT_BUILT.search(log) and code not in (c["code"] if isinstance(c["code"], tuple) else (c["code"],)):
        return "n/a", log.strip().splitlines()[-1][:120] if log.strip() else "", command
    if c["allow_na"] and NOT_BUILT.search(log) and code != 0:
        return "n/a", [l for l in log.splitlines() if NOT_BUILT.search(l)][-1][:120], command
    want_codes = c["code"] if isinstance(c["code"], tuple) else (c["code"],)
    if code not in want_codes:
        problems.append("exit %d, wanted %s" % (code, want_codes))
    if c["status"] is not None and status != c["status"]:
        problems.append("HTTP %d, wanted %d" % (status, c["status"]))
    for label, text, want, invert in (("stdout", out, c["out"], False), ("stdout", out, c["no_out"], True),
                                      ("log", log, c["log"], False), ("log", log, c["no_log"], True)):
        if want:
            found = re.search(fill(want, local_ctx), text, re.M)
            if bool(found) == invert:
                problems.append("%s %s %r" % (label, "has" if invert else "lacks", want))
    if c["rec"] or c["no_rec"]:
        seen = record.text()[before_rec:]
        if c["rec"] and not re.search(c["rec"], seen):
            problems.append("no server saw %r" % c["rec"])
        if c["no_rec"] and re.search(c["no_rec"], seen):
            problems.append("a server saw %r" % c["no_rec"])
    if c["file"]:
        problem = check_file(c["file"], cwd, local_ctx)
        if problem:
            problems.append(problem)
    if problems:
        tail = [l for l in log.splitlines() if l.startswith("curl:")][-1:]
        return "fail", "; ".join(problems) + (" | " + tail[0] if tail else ""), command
    return c["tier"], "", command


def run_case_clean(c, opts, ctx, record):
    """Every case leaves a screenshot of several MB: keep the disk free as we go."""
    cwd = tempfile.mkdtemp(prefix="c-", dir=opts.work)
    try:
        return run_case(c, opts, ctx, record, cwd)
    finally:
        shutil.rmtree(cwd, ignore_errors=True)


def run_with_retry(c, opts, ctx, record):
    """Public services have bad moments: a failure gets a second look before it counts."""
    result = run_case_clean(c, opts, ctx, record)
    for attempt in range(2):
        if result[0] != "fail":
            break
        time.sleep(5 * (attempt + 1))
        result = run_case_clean(c, opts, ctx, record)
    return c, result


def all_option_names():
    with open(OPTIONS_DATA, encoding="utf-8") as f:
        return set(re.findall(r'^\{ "([a-z0-9.-]+)",', f.read(), re.M))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ui", required=True)
    ap.add_argument("--only")
    ap.add_argument("--report")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--curl-dir", help="directory holding the curl to use")
    opts = ap.parse_args()
    opts.ui = os.path.abspath(opts.ui)

    missing = all_option_names() - {c["name"] for c in CASES}
    if missing:
        print("NOT COVERED:", " ".join(sorted(missing)))
        return 2

    work = tempfile.mkdtemp(prefix="curlui-live-")
    opts.work = work
    assets = os.path.join(work, "assets")
    os.mkdir(assets)
    prepare_assets(assets)
    ctx, record = start_servers(assets)
    if not ctx["FTP"]:
        print("pyftpdlib is missing: local FTP cases will fail")

    wanted = set(opts.only.split(",")) if opts.only else None
    cases = [c for c in CASES if not wanted or c["name"] in wanted or c["label"] in wanted]
    public = [c for c in cases if not c["local"]]
    local = [c for c in cases if c["local"]]
    results = []
    lock = threading.Lock()

    def note(c, result):
        with lock:
            results.append((c, result))
            if result[0] == "fail":
                print("FAIL %-28s %s" % (c["label"], result[1][:220]), flush=True)
            print("[%d/%d]" % (len(results), len(cases)), end="\r", file=sys.stderr, flush=True)

    with concurrent.futures.ThreadPoolExecutor(opts.jobs) as pool:
        for c, result in pool.map(lambda c: run_with_retry(c, opts, ctx, record), public):
            note(c, result)
    # one at a time: these read what the servers recorded during the run
    for c in local:
        c, result = run_with_retry(c, opts, ctx, record)
        note(c, result)

    tally = {}
    for _, result in results:
        tally[result[0]] = tally.get(result[0], 0) + 1
    covered = {c["name"] for c, _ in results}
    print("\n%d cases over %d options: %s" % (len(results), len(covered), ", ".join("%s %d" % kv for kv in sorted(tally.items()))))
    na = sorted(c["label"] for c, r in results if r[0] == "n/a")
    print("n/a:", " ".join(na))
    ran = sorted(c["label"] for c, r in results if r[0] == "ran")
    print("ran:", " ".join(ran))
    if opts.report:
        with open(opts.report, "w") as f:
            f.write("| Option | Result | Detail | Command |\n|---|---|---|---|\n")
            for c, r in sorted(results, key=lambda x: x[0]["label"]):
                f.write("| `--%s` | %s | %s | `%s` |\n" % (c["label"], r[0], r[1].replace("|", "\\|"), r[2].replace("|", "\\|")))
    shutil.rmtree(work, ignore_errors=True)
    return 1 if tally.get("fail") else 0


if __name__ == "__main__":
    sys.exit(main())
