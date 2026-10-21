#!/usr/bin/env python3
"""Local, deterministic fixture. No external service dependencies. Bind loopback by default."""
import argparse
import http.server
import json
import time
from email.parser import BytesParser
from email.policy import default
from urllib.parse import urlparse


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'

    def respond(self, status, body=b'', headers=None):
        self.send_response(status)
        self.send_header('Content-Length', str(len(body)))
        for key, value in (headers or {}).items():
            self.send_header(key, value)
        self.end_headers()
        try:
            self.wfile.write(body)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def do_GET(self):
        path = urlparse(self.path).path
        if path == '/health':
            self.respond(200, b'io-quality-fixture-v1')
        elif path == '/redirect':
            self.respond(302, headers={'Location': '/payload'})
        elif path == '/error':
            self.respond(404, b'not found')
        elif path == '/slow':
            time.sleep(0.75)
            self.respond(200, b'late')
        elif path in ('/payload', '/range', '/range-ignore', '/range-invalid'):
            payload = b'complete'
            requested = self.headers.get('Range', '')
            if path != '/range-ignore' and requested.startswith('bytes='):
                try:
                    start = int(requested[6:].split('-')[0])
                except ValueError:
                    self.respond(400)
                    return
                if start < 0 or start >= len(payload):
                    self.respond(416, headers={'Content-Range': 'bytes */8'})
                    return
                range_start = start + 1 if path == '/range-invalid' else start
                self.respond(206, payload[start:], {'Content-Range': f'bytes {range_start}-7/8', 'ETag': '"v1"'})
            else:
                self.respond(200, payload, {'ETag': '"v1"'})
        else:
            self.respond(404)

    def do_POST(self):
        length = int(self.headers.get('Content-Length', '0'))
        if length < 0 or length > 4 * 1024 * 1024:
            self.close_connection = True
            self.respond(413)
            return
        body = self.rfile.read(length)
        if self.path == '/echo':
            self.respond(200, body)
        elif self.path == '/upload':
            envelope = ("Content-Type: " + self.headers.get('Content-Type', '') + "\r\nMIME-Version: 1.0\r\n\r\n").encode() + body
            message = BytesParser(policy=default).parsebytes(envelope)
            parts = {part.get_param('name', header='content-disposition'): part.get_payload(decode=True)
                     for part in message.iter_parts()} if message.is_multipart() else {}
            valid = parts.get('caption') == b'hello' and parts.get('file') == b'complete'
            self.respond(200 if valid else 400, json.dumps({'valid': valid}).encode())
        else:
            self.respond(404)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    server = http.server.ThreadingHTTPServer((args.host, args.port), Handler)
    print(f'Fixture http://{args.host}:{server.server_port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
