#!/usr/bin/env python3
"""Test the local service contract so malformed fixtures do not hide native regressions."""
import http.client
import threading
import unittest
from http.server import ThreadingHTTPServer
from http_server import Handler


class FixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()

    def call(self, method, path, body=None, headers=None):
        connection = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=2)
        try:
            connection.request(method, path, body=body, headers=headers or {})
            result = connection.getresponse()
            return result.status, dict(result.getheaders()), result.read()
        finally:
            connection.close()

    def test_echo_boundaries(self):
        for size in (0, 1, 3, 4097):
            body = bytes(i % 256 for i in range(size))
            self.assertEqual(self.call('POST', '/echo', body)[2], body)

    def test_ranges(self):
        status, headers, body = self.call('GET', '/range', headers={'Range': 'bytes=3-'})
        self.assertEqual((status, headers['Content-Range'], body), (206, 'bytes 3-7/8', b'plete'))
        self.assertEqual(self.call('GET', '/range-ignore', headers={'Range': 'bytes=3-'})[0], 200)
        self.assertEqual(self.call('GET', '/range-invalid', headers={'Range': 'bytes=3-'})[1]['Content-Range'], 'bytes 4-7/8')
        self.assertEqual(self.call('GET', '/range', headers={'Range': 'bytes=8-'})[0], 416)

    def test_redirect_and_error(self):
        self.assertEqual(self.call('GET', '/redirect')[1]['Location'], '/payload')
        self.assertEqual(self.call('GET', '/error')[0], 404)

    def test_multipart_requires_real_parts(self):
        headers = {'Content-Type': 'multipart/form-data; boundary=fixture'}
        self.assertEqual(self.call('POST', '/upload', b'caption hello file complete', headers)[0], 400)
        body = (b'--fixture\r\nContent-Disposition: form-data; name="caption"\r\n\r\nhello\r\n'
                b'--fixture\r\nContent-Disposition: form-data; name="file"; filename="x"\r\n\r\ncomplete\r\n--fixture--\r\n')
        self.assertEqual(self.call('POST', '/upload', body, headers)[0], 200)


if __name__ == '__main__':
    unittest.main()
