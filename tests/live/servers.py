"""Local stand-ins for what has no public test service: proxies, mail, TFTP,
telnet, a writable FTP, sockets. They exist to observe what curl really sends."""
import http.server
import json
import os
import select
import socket
import socketserver
import ssl
import struct
import threading


class Record:
    """Everything the servers saw, by case-insensitive search."""

    def __init__(self):
        self.lock = threading.Lock()
        self.lines = []

    def add(self, text):
        with self.lock:
            self.lines.append(text)

    def text(self):
        with self.lock:
            return "\n".join(self.lines)


class Threaded(socketserver.ThreadingMixIn, socketserver.TCPServer):
    allow_reuse_address = True
    daemon_threads = True


class ThreadedHTTP(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def start(server):
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.server_address[1] if isinstance(server.server_address, tuple) else server.server_address


class Echo(http.server.BaseHTTPRequestHandler):
    """Replies with the request as JSON. /old answers 304 to If-Modified-Since."""
    protocol_version = "HTTP/1.1"
    tag = "local-echo"

    def log_message(self, *args):
        pass

    def reply(self):
        length = int(self.headers.get("Content-Length") or 0)
        body = self.rfile.read(length) if length else b""
        info = {"via": self.tag, "method": self.command, "path": self.path, "version": self.request_version,
                "headers": dict(self.headers.items()), "body": body.decode("latin1")}
        self.server.record.add("%s %s %s" % (self.command, self.path, self.headers.as_string()))
        data = json.dumps(info, indent=1).encode()
        if self.path.startswith("/old") and self.headers.get("If-Modified-Since"):
            self.send_response(304)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Set-Cookie", "session=1; Path=/")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_HEAD = do_DELETE = do_OPTIONS = do_PATCH = reply


class UnixEcho(Echo):
    tag = "via-unix"

    def address_string(self):
        return "unix"


class UnixServer(socketserver.ThreadingMixIn, socketserver.UnixStreamServer):
    daemon_threads = True

    def get_request(self):
        request, _ = super().get_request()
        return request, ("unix", 0)


def relay(a, b):
    socks = [a, b]
    try:
        while True:
            ready, _, _ = select.select(socks, [], [], 20)
            if not ready:
                return
            for s in ready:
                data = s.recv(65536)
                if not data:
                    return
                (b if s is a else a).sendall(data)
    except OSError:
        pass
    finally:
        a.close()
        b.close()


class Proxy(http.server.BaseHTTPRequestHandler):
    """A forward proxy: plain requests and CONNECT. With want_auth set it asks for
    credentials the way the scheme under test expects."""
    protocol_version = "HTTP/1.1"
    want_auth = None

    def log_message(self, *args):
        pass

    def check_auth(self):
        given = self.headers.get("Proxy-Authorization", "")
        if self.want_auth and not given.lower().startswith(self.want_auth.lower()):
            self.send_response(407)
            self.send_header("Proxy-Authenticate", self.want_auth + ' realm="t", nonce="abc"')
            self.send_header("Content-Length", "0")
            self.end_headers()
            return False
        return True

    def do_CONNECT(self):
        self.server.record.add("PROXY CONNECT %s %s" % (self.path, self.headers.as_string()))
        if not self.check_auth():
            return
        host, _, port = self.path.rpartition(":")
        try:
            upstream = socket.create_connection((host, int(port)), 15)
        except OSError:
            self.send_error(502)
            return
        self.send_response(200, "Connection established")
        self.send_header("X-Proxy-Hello", "tunnel")
        self.end_headers()
        relay(self.connection, upstream)
        self.close_connection = True

    def forward(self):
        self.server.record.add("PROXY %s %s %s %s" % (self.command, self.path, self.request_version,
                                                      self.headers.as_string()))
        if not self.check_auth():
            return
        info = {"via": "local-proxy", "method": self.command, "url": self.path, "version": self.request_version,
                "headers": dict(self.headers.items())}
        data = json.dumps(info, indent=1).encode()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(data)

    do_GET = do_POST = do_PUT = do_HEAD = forward


class TLSProxy(ThreadedHTTP):
    """The same proxy behind TLS, for the --proxy-* TLS options."""
    context = None

    def get_request(self):
        sock, addr = super().get_request()
        try:
            return self.context.wrap_socket(sock, server_side=True), addr
        except (ssl.SSLError, OSError):
            sock.close()
            raise OSError("tls handshake failed")

    def handle_error(self, request, client_address):
        pass


class Socks(socketserver.BaseRequestHandler):
    """SOCKS4, 4a and 5 with optional username/password; connects for real."""

    def recvn(self, n):
        data = b""
        while len(data) < n:
            part = self.request.recv(n - len(data))
            if not part:
                raise OSError("eof")
            data += part
        return data

    def cstring(self):
        data = b""
        while True:
            c = self.recvn(1)
            if c == b"\0":
                return data.decode()
            data += c

    def handle(self):
        rec = self.server.record
        try:
            version = self.recvn(1)[0]
            if version == 4:
                _, port = struct.unpack(">BH", self.recvn(3))
                ip = socket.inet_ntoa(self.recvn(4))
                self.cstring()
                host = ip
                if ip.startswith("0.0.0.") and ip != "0.0.0.0":
                    host = self.cstring()
                    rec.add("SOCKS4A %s:%d" % (host, port))
                else:
                    rec.add("SOCKS4 %s:%d" % (host, port))
                upstream = socket.create_connection((host, port), 15)
                self.request.sendall(b"\0\x5a" + b"\0" * 6)
            else:
                methods = self.recvn(self.recvn(1)[0])
                if 2 in methods:
                    self.request.sendall(b"\5\2")
                    self.recvn(1)
                    user = self.recvn(self.recvn(1)[0]).decode()
                    self.recvn(self.recvn(1)[0])
                    rec.add("SOCKS5 AUTH user=%s" % user)
                    self.request.sendall(b"\1\0")
                else:
                    self.request.sendall(b"\5\0")
                _, _, _, atyp = self.recvn(4)
                if atyp == 1:
                    host = socket.inet_ntoa(self.recvn(4))
                    rec.add("SOCKS5 ip %s" % host)
                elif atyp == 3:
                    host = self.recvn(self.recvn(1)[0]).decode()
                    rec.add("SOCKS5 hostname %s" % host)
                else:
                    host = socket.inet_ntop(socket.AF_INET6, self.recvn(16))
                port = struct.unpack(">H", self.recvn(2))[0]
                upstream = socket.create_connection((host, port), 15)
                self.request.sendall(b"\5\0\0\1" + b"\0" * 6)
            relay(self.request, upstream)
        except OSError:
            pass


class Line(socketserver.StreamRequestHandler):
    def say(self, text):
        self.wfile.write(text.encode() + b"\r\n")
        self.wfile.flush()

    def lines(self):
        for raw in self.rfile:
            line = raw.decode("latin1").rstrip("\r\n")
            self.server.record.add(self.prefix + " " + line)
            yield line


class SMTP(Line):
    prefix = "SMTP"

    def handle(self):
        self.say("220 local ESMTP")
        lines = self.lines()
        for line in lines:
            verb = line.split(" ")[0].upper()
            if verb == "EHLO":
                self.wfile.write(b"250-local\r\n250-AUTH PLAIN LOGIN\r\n250 SMTPUTF8\r\n")
                self.wfile.flush()
            elif verb == "DATA":
                self.say("354 go")
                for body in lines:
                    if body == ".":
                        break
                self.say("250 ok")
            elif verb == "AUTH":
                if len(line.split(" ")) < 3:  # no initial response: ask for it
                    self.say("334 ")
                    self.rfile.readline()
                self.say("235 ok")
            elif verb == "RCPT" and "bad@" in line:
                self.say("550 no such user")
            elif verb == "QUIT":
                self.say("221 bye")
                return
            else:
                self.say("250 ok")


class IMAP(Line):
    prefix = "IMAP"

    def handle(self):
        self.say("* OK local IMAP")
        for line in self.lines():
            parts = line.split(" ")
            tag = parts[0]
            verb = parts[1].upper() if len(parts) > 1 else ""
            if verb == "CAPABILITY":
                self.say("* CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR")
                self.say(tag + " OK")
            elif verb == "SELECT" or verb == "EXAMINE":
                self.say("* 1 EXISTS")
                self.say(tag + " OK")
            elif verb == "APPEND":
                self.say("+ ready")
                size = int(line.rsplit("{", 1)[1].split("}")[0])
                self.rfile.read(size + 2)
                self.say(tag + " OK")
            elif verb == "AUTHENTICATE" and len(parts) == 3:
                self.say("+ ")
                self.rfile.readline()
                self.say(tag + " OK")
            elif verb == "LOGOUT":
                self.say("* BYE")
                self.say(tag + " OK")
                return
            else:
                self.say(tag + " OK")


class POP3(Line):
    prefix = "POP3"

    def handle(self):
        self.say("+OK local POP3")
        for line in self.lines():
            verb = line.split(" ")[0].upper()
            if verb == "CAPA":
                self.say("+OK")
                self.say("SASL PLAIN")
                self.say("USER")
                self.say(".")
            elif verb == "LIST":
                self.say("+OK")
                self.say("1 5")
                self.say(".")
            elif verb == "QUIT":
                self.say("+OK")
                return
            else:
                self.say("+OK")


class Telnet(socketserver.BaseRequestHandler):
    """Records the negotiation, greets and closes."""

    def handle(self):
        self.request.settimeout(1.5)
        self.request.sendall(b"\xff\xfd\x18hello from telnet\r\n")
        seen = b""
        try:
            while len(seen) < 4096:
                data = self.request.recv(1024)
                if not data:
                    break
                seen += data
        except OSError:
            pass
        self.server.record.add("TELNET " + seen.decode("latin1"))


class RawHTTP09(socketserver.BaseRequestHandler):
    """Answers with no status line or headers: HTTP/0.9."""

    def handle(self):
        self.request.recv(4096)
        self.request.sendall(b"plain http0.9 body\n")


class HAProxy(socketserver.StreamRequestHandler):
    def handle(self):
        first = self.rfile.readline().decode("latin1").strip()
        self.server.record.add("HAPROXY " + first)
        while self.rfile.readline().strip():
            pass
        body = first.encode()
        self.wfile.write(b"HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %d\r\n\r\n%s" % (len(body), body))


class TFTP(socketserver.BaseRequestHandler):
    """Read requests only: one short block."""

    def handle(self):
        data, sock = self.request
        opcode = struct.unpack(">H", data[:2])[0]
        if opcode != 1:
            return
        fields = data[2:].split(b"\0")
        self.server.record.add("TFTP " + " ".join(f.decode() for f in fields))
        reply = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        reply.settimeout(3)
        if len(fields) > 3 and fields[2]:
            options = fields[2:-1]
            for i in range(0, len(options) - 1, 2):
                if options[i] == b"tsize":  # the real size: curl rejects 0
                    options[i + 1] = b"10"
            oack = b"\0\6" + b"\0".join(options) + b"\0"
            reply.sendto(oack, self.client_address)
            try:
                reply.recvfrom(516)
            except OSError:
                return
        reply.sendto(b"\0\3\0\1tftp body\n", self.client_address)
        try:
            reply.recvfrom(516)
        except OSError:
            pass
        reply.close()


class ThreadedUDP(socketserver.ThreadingMixIn, socketserver.UDPServer):
    allow_reuse_address = True
    daemon_threads = True


def serve(handler, record, server_class=Threaded):
    server = server_class(("127.0.0.1", 0), handler)
    server.record = record
    return start(server)


def make_ftp(root, record):
    """A writable FTP server, or None without pyftpdlib."""
    try:
        from pyftpdlib.authorizers import DummyAuthorizer
        from pyftpdlib.handlers import FTPHandler
        from pyftpdlib.servers import ThreadedFTPServer
    except ImportError:
        return None
    import logging
    logging.getLogger("pyftpdlib").setLevel(logging.CRITICAL)
    authorizer = DummyAuthorizer()
    authorizer.add_user("u", "p", root, perm="elradfmwMT")

    class Handler(FTPHandler):
        def on_incomplete_file_received(self, file):
            pass

        def ftp_ACCT(self, line):
            record.add("FTP ACCT " + line)
            self.respond("230 ok")

    Handler.authorizer = authorizer
    Handler.proto_cmds = dict(FTPHandler.proto_cmds)
    Handler.proto_cmds["ACCT"] = dict(perm=None, auth=False, arg=True, help="account")
    server = ThreadedFTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server.address[1]
