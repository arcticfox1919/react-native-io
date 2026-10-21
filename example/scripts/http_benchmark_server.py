#!/usr/bin/env python3
"""Bounded localhost HTTP/TLS transport fixture; no external service or global TLS bypass."""
import argparse
from email.parser import BytesParser
from email.policy import default
import hashlib
import http.server
from pathlib import Path
import ssl
import threading

PAYLOAD = bytes(range(256)) * (4 * 1024 * 1024 // 256)
DIGEST = hashlib.sha256(PAYLOAD).hexdigest()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, fmt, *args):
        pass

    def reply(self, body, status=200):
        self.send_response(status)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Payload-SHA256", DIGEST)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/chunked":
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for offset in range(0, len(PAYLOAD), 65521):
                block = PAYLOAD[offset:offset + 65521]
                self.wfile.write(f"{len(block):x}\r\n".encode() + block + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        elif self.path == "/truncated":
            self.send_response(200)
            self.send_header("Content-Length", str(len(PAYLOAD) + 17))
            self.end_headers()
            self.wfile.write(PAYLOAD[:1024 * 1024 + 5])
            self.close_connection = True
        elif self.path == "/payload":
            self.reply(PAYLOAD)
        else:
            self.reply(b"not found", 404)

    def do_POST(self):
        if self.path == "/multipart":
            size = int(self.headers.get("Content-Length", "-1"))
            if size < 0 or size > len(PAYLOAD) + 65536:
                self.close_connection = True
                self.reply(b"wrong length", 400)
                return
            body = self.rfile.read(size)
            envelope = ("Content-Type: " + self.headers.get("Content-Type", "") + "\r\nMIME-Version: 1.0\r\n\r\n").encode() + body
            message = BytesParser(policy=default).parsebytes(envelope)
            parts = {part.get_param("name", header="content-disposition"): part.get_payload(decode=True)
                     for part in message.iter_parts()} if message.is_multipart() else {}
            valid = parts.get("file") == PAYLOAD
            self.reply(DIGEST.encode() if valid else b"multipart mismatch", 200 if valid else 400)
            return
        if self.path != "/upload":
            self.reply(b"not found", 404)
            return
        size = int(self.headers.get("Content-Length", "-1"))
        if size != len(PAYLOAD):
            self.close_connection = True
            self.reply(b"wrong length", 400)
            return
        digest = hashlib.sha256()
        left = size
        while left:
            chunk = self.rfile.read(min(left, 65536))
            if not chunk:
                self.close_connection = True
                return
            digest.update(chunk)
            left -= len(chunk)
        actual = digest.hexdigest()
        self.reply(actual.encode(), 200 if actual == DIGEST else 400)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--http-port", type=int, default=8876)
    parser.add_argument("--https-port", type=int, default=8877)
    args = parser.parse_args()
    certs = Path(__file__).resolve().parents[1] / "tests/certs"
    plain = http.server.ThreadingHTTPServer(("127.0.0.1", args.http_port), Handler)
    secure = http.server.ThreadingHTTPServer(("127.0.0.1", args.https_port), Handler)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(certs / "localhost-benchmark.pem", certs / "localhost-benchmark.key")
    secure.socket = context.wrap_socket(secure.socket, server_side=True)
    thread = threading.Thread(target=plain.serve_forever, daemon=True)
    thread.start()
    print(f"HTTP :{args.http_port}, TLS :{args.https_port}, bytes={len(PAYLOAD)}, sha256={DIGEST}", flush=True)
    try:
        secure.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        plain.shutdown()
        plain.server_close()
        secure.server_close()


if __name__ == "__main__":
    main()
