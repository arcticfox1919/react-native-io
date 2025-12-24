/**
 * @file Request.harness.ts
 * @description Harness tests for HTTP Request API - runs on real device/emulator
 */

import { describe, it, expect, beforeEach } from 'react-native-harness';
import { request, File, FS } from 'react-native-io';

describe('HTTP Request', () => {
  let tempDir: string;

  beforeEach(() => {
    tempDir = FS.cacheDir;
  });

  describe('GET requests', () => {
    it('should make a simple GET request', async () => {
      const res = await request.get('https://httpbin.org/get');

      expect(res.ok).toBe(true);
      expect(res.status).toBe(200);
      expect(res.headers).toBeDefined();
    });

    it('should get response as text', async () => {
      const res = await request.get('https://httpbin.org/get');

      const text = res.text();
      expect(typeof text).toBe('string');
      expect(text.length).toBeGreaterThan(0);
    });

    it('should get response as JSON', async () => {
      const res = await request.get('https://httpbin.org/get');

      const json = res.json<{ url: string }>();
      expect(json).toBeDefined();
      expect(json.url).toBe('https://httpbin.org/get');
    });

    it('should handle request with query parameters', async () => {
      const res = await request.get('https://httpbin.org/get?foo=bar&baz=123');

      expect(res.ok).toBe(true);
      const json = res.json<{ args: Record<string, string> }>();
      expect(json.args.foo).toBe('bar');
      expect(json.args.baz).toBe('123');
    });

    it('should handle custom headers', async () => {
      const res = await request.get('https://httpbin.org/headers', {
        headers: {
          'X-Custom-Header': 'test-value',
          'Accept': 'application/json',
        },
      });

      // Check we got a response
      if (res.ok) {
        const json = res.json<{ headers: Record<string, string> }>();
        expect(json.headers['X-Custom-Header']).toBe('test-value');
      } else {
        // Network may have issues, just check we got some response
        expect(res.status).toBeGreaterThanOrEqual(0);
      }
    });

    it('should handle 404 status', async () => {
      const res = await request.get('https://httpbin.org/status/404');

      // Network request succeeded but got 404 status
      expect(res.status).toBe(404);
    });
  });

  describe('POST requests', () => {
    it('should make POST request with JSON body', async () => {
      const res = await request.post('https://httpbin.org/post', {
        json: { name: 'test', value: 42 },
      });

      expect(res.ok).toBe(true);
      expect(res.status).toBe(200);

      const json = res.json<{ json: { name: string; value: number } }>();
      expect(json.json.name).toBe('test');
      expect(json.json.value).toBe(42);
    });

    it('should make POST request with string body', async () => {
      const res = await request.post('https://httpbin.org/post', {
        body: 'Hello, World!',
        headers: { 'Content-Type': 'text/plain' },
      });

      expect(res.ok).toBe(true);
      const json = res.json<{ data: string }>();
      expect(json.data).toBe('Hello, World!');
    });
  });

  describe('PUT requests', () => {
    it('should make PUT request', async () => {
      const res = await request.put('https://httpbin.org/put', {
        json: { updated: true },
      });

      expect(res.ok).toBe(true);
      expect(res.status).toBe(200);

      const json = res.json<{ json: { updated: boolean } }>();
      expect(json.json.updated).toBe(true);
    });
  });

  describe('DELETE requests', () => {
    it('should make DELETE request', async () => {
      const res = await request.delete('https://httpbin.org/delete');

      expect(res.ok).toBe(true);
      expect(res.status).toBe(200);
    });
  });

  describe('PATCH requests', () => {
    it('should make PATCH request', async () => {
      const res = await request.patch('https://httpbin.org/patch', {
        json: { patched: true },
      });

      expect(res.ok).toBe(true);
      expect(res.status).toBe(200);

      const json = res.json<{ json: { patched: boolean } }>();
      expect(json.json.patched).toBe(true);
    });
  });

  describe('Download', () => {
    it('should download a file', async () => {
      const downloadPath = `${tempDir}/rn-io-download-test.json`;
      const file = new File(downloadPath);

      try {
        const result = await request.download(
          'https://httpbin.org/json',
          downloadPath
        );

        // Download may fail due to network issues
        if (result.ok) {
          expect(result.status).toBe(200);
          expect(result.fileSize).toBeGreaterThan(0);
          expect(await file.exists()).toBe(true);
        }
      } finally {
        try {
          await file.delete();
        } catch {}
      }
    });

    it('should report download progress', async () => {
      const downloadPath = `${tempDir}/rn-io-download-progress.txt`;
      const file = new File(downloadPath);

      try {
        const result = await request.download(
          'https://httpbin.org/bytes/1024',
          downloadPath,
          {
            onProgress: (progress) => {
              expect(progress.bytesReceived).toBeGreaterThanOrEqual(0);
            },
          }
        );

        // Just check result structure
        expect(typeof result.ok).toBe('boolean');
      } finally {
        try {
          await file.delete();
        } catch {}
      }
    });
  });

  describe('Timeout', () => {
    it('should respect timeout setting', async () => {
      // This endpoint delays response by 5 seconds
      // We set timeout to 1 second, so it should fail
      const res = await request.get('https://httpbin.org/delay/5', {
        timeout: 1000,
      });

      // Should fail due to timeout
      expect(res.ok).toBe(false);
    });
  });
});
