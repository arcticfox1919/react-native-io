/**
 * @file Request.ts
 * @description HTTP Request API for React Native IO
 *
 * A simple, Pythonic HTTP client inspired by Python's requests library.
 * Provides easy-to-use methods for making HTTP requests.
 *
 * @example
 * ```typescript
 * import { request } from 'react-native-io';
 *
 * // Simple GET request
 * const res = await request.get('https://api.example.com/users');
 * console.log(res.json());
 *
 * // POST with JSON body
 * const res = await request.post('https://api.example.com/users', {
 *   json: { name: 'John', age: 30 }
 * });
 *
 * // Download file
 * await request.download('https://example.com/file.zip', '/path/to/save.zip');
 *
 * // Upload file
 * await request.upload('https://api.example.com/upload', '/path/to/file.jpg');
 * ```
 */

import { createRequest, installHttpClient } from './NativeStdIO';
import type { IORequest } from './types';

// Track if HTTP client has been installed (Android only)
let httpClientInstalled = false;

// ============================================================================
// Types
// ============================================================================

/**
 * HTTP request options
 */
export interface RequestOptions {
  /** Request headers */
  headers?: Record<string, string>;
  /** Request timeout in milliseconds (default: 30000) */
  timeout?: number;
  /** Whether to follow redirects (default: true) */
  followRedirects?: boolean;
}

/**
 * HTTP request options with body
 */
export interface RequestOptionsWithBody extends RequestOptions {
  /** JSON body (automatically sets Content-Type) */
  json?: object;
  /** String body */
  body?: string;
  /** Binary body */
  data?: ArrayBuffer;
}

/**
 * HTTP response
 */
export interface Response {
  /** Whether the request was successful (no network error) */
  ok: boolean;
  /** HTTP status code (0 if network error) */
  status: number;
  /** HTTP status message */
  statusText: string;
  /** Response headers */
  headers: Record<string, string>;
  /** Final URL after redirects */
  url: string;
  /** Error message (if failed) */
  error?: string;

  /** Get response body as string */
  text(): string;
  /** Get response body as JSON */
  json<T = unknown>(): T;
  /** Get response body as ArrayBuffer */
  arrayBuffer(): ArrayBuffer;
}

/**
 * Download options
 */
export interface DownloadOptions extends RequestOptions {
  /** Resume download from existing file */
  resumable?: boolean;
  /** Progress callback */
  onProgress?: (progress: DownloadProgress) => void;
}

/**
 * Download progress info
 */
export interface DownloadProgress {
  /** Bytes received so far */
  bytesReceived: number;
  /** Total bytes (-1 if unknown) */
  totalBytes: number;
  /** Progress percentage (0-1) */
  progress: number;
}

/**
 * Download result
 */
export interface DownloadResult {
  /** Whether download succeeded */
  ok: boolean;
  /** HTTP status code */
  status: number;
  /** Downloaded file path */
  filePath: string;
  /** File size in bytes */
  fileSize: number;
  /** Error message (if failed) */
  error?: string;
}

/**
 * Upload options
 */
export interface UploadOptions extends RequestOptions {
  /** Form field name for the file (default: "file") */
  fieldName?: string;
  /** Override filename in the form */
  fileName?: string;
  /** MIME type (auto-detected if not specified) */
  mimeType?: string;
  /** Additional form fields */
  formFields?: Record<string, string>;
  /** Progress callback */
  onProgress?: (progress: UploadProgress) => void;
}

/**
 * Upload progress info
 */
export interface UploadProgress {
  /** Bytes sent so far */
  bytesSent: number;
  /** Total bytes */
  totalBytes: number;
  /** Progress percentage (0-1) */
  progress: number;
}

/**
 * Upload result
 */
export interface UploadResult {
  /** Whether upload succeeded */
  ok: boolean;
  /** HTTP status code */
  status: number;
  /** Response body as string */
  responseText: string;
  /** Error message (if failed) */
  error?: string;
}

// ============================================================================
// Native Response Types (internal)
// ============================================================================

/** @internal */
interface NativeResponse {
  success: boolean;
  statusCode: number;
  statusMessage: string;
  url: string;
  errorMessage: string;
  body: ArrayBuffer;
  headerKeys: string[];
  headerValues: string[];
}

// ============================================================================
// Helper Functions
// ============================================================================

function headersToArray(headers?: Record<string, string>): string[] {
  if (!headers) return [];
  const result: string[] = [];
  for (const [key, value] of Object.entries(headers)) {
    result.push(key, value);
  }
  return result;
}

function arrayToHeaders(
  keys: string[],
  values: string[]
): Record<string, string> {
  const headers: Record<string, string> = {};
  for (let i = 0; i < keys.length && i < values.length; i++) {
    headers[keys[i]!] = values[i]!;
  }
  return headers;
}

/**
 * Decode UTF-8 ArrayBuffer to string without TextDecoder (for Hermes compatibility)
 */
function decodeUTF8(buffer: ArrayBuffer): string {
  const bytes = new Uint8Array(buffer);
  let result = '';
  let i = 0;

  while (i < bytes.length) {
    const byte1 = bytes[i]!;

    if (byte1 < 0x80) {
      // 1-byte character (ASCII)
      result += String.fromCharCode(byte1);
      i++;
    } else if ((byte1 & 0xe0) === 0xc0) {
      // 2-byte character
      const byte2 = bytes[i + 1]!;
      result += String.fromCharCode(((byte1 & 0x1f) << 6) | (byte2 & 0x3f));
      i += 2;
    } else if ((byte1 & 0xf0) === 0xe0) {
      // 3-byte character
      const byte2 = bytes[i + 1]!;
      const byte3 = bytes[i + 2]!;
      result += String.fromCharCode(
        ((byte1 & 0x0f) << 12) | ((byte2 & 0x3f) << 6) | (byte3 & 0x3f)
      );
      i += 3;
    } else if ((byte1 & 0xf8) === 0xf0) {
      // 4-byte character (surrogate pair)
      const byte2 = bytes[i + 1]!;
      const byte3 = bytes[i + 2]!;
      const byte4 = bytes[i + 3]!;
      const codePoint =
        ((byte1 & 0x07) << 18) |
        ((byte2 & 0x3f) << 12) |
        ((byte3 & 0x3f) << 6) |
        (byte4 & 0x3f);
      // Convert to surrogate pair
      const adjusted = codePoint - 0x10000;
      result += String.fromCharCode(
        0xd800 + (adjusted >> 10),
        0xdc00 + (adjusted & 0x3ff)
      );
      i += 4;
    } else {
      // Invalid UTF-8, skip
      i++;
    }
  }

  return result;
}

function createResponse(native: NativeResponse): Response {
  const bodyBuffer = native.body;
  let bodyText: string | null = null;

  return {
    ok: native.success && native.statusCode >= 200 && native.statusCode < 300,
    status: native.statusCode,
    statusText: native.statusMessage,
    headers: arrayToHeaders(native.headerKeys, native.headerValues),
    url: native.url,
    error: native.success ? undefined : native.errorMessage,

    text(): string {
      if (bodyText === null) {
        bodyText = decodeUTF8(bodyBuffer);
      }
      return bodyText;
    },

    json<T = unknown>(): T {
      return JSON.parse(this.text()) as T;
    },

    arrayBuffer(): ArrayBuffer {
      return bodyBuffer;
    },
  };
}

// ============================================================================
// Request Class
// ============================================================================

/**
 * HTTP Request API (Python requests-style)
 *
 * @example
 * ```typescript
 * // GET request
 * const res = await request.get('https://api.example.com/users');
 * if (res.ok) {
 *   const users = res.json();
 * }
 *
 * // POST with JSON
 * const res = await request.post('https://api.example.com/users', {
 *   json: { name: 'John' },
 *   headers: { 'Authorization': 'Bearer token' }
 * });
 *
 * // Download file
 * const result = await request.download(
 *   'https://example.com/large-file.zip',
 *   '/path/to/save.zip',
 *   { resumable: true }
 * );
 *
 * // Upload file
 * const result = await request.upload(
 *   'https://api.example.com/upload',
 *   '/path/to/photo.jpg',
 *   { fieldName: 'image', mimeType: 'image/jpeg' }
 * );
 * ```
 */
class RequestClient {
  private _io: IORequest | null = null;

  /**
   * Get or create the underlying IORequest instance (lazy initialization)
   */
  private io(): IORequest {
    if (!this._io) {
      // Install HTTP client on first use (Android only, no-op on iOS)
      if (!httpClientInstalled) {
        installHttpClient();
        httpClientInstalled = true;
      }
      this._io = createRequest();
    }
    return this._io;
  }

  /**
   * Release the underlying IORequest instance.
   * Call this when you're done with the client to free native resources.
   */
  dispose(): void {
    this._io = null;
  }

  // ==========================================================================
  // Core Request Methods
  // ==========================================================================

  /**
   * Execute HTTP request
   * @param method HTTP method
   * @param url Request URL
   * @param options Request options
   */
  async request(
    method: string,
    url: string,
    options?: RequestOptionsWithBody
  ): Promise<Response> {
    const headers = { ...options?.headers };
    let body: string | ArrayBuffer | null = null;

    // Handle JSON body
    if (options?.json) {
      body = JSON.stringify(options.json);
      headers['Content-Type'] = 'application/json';
    } else if (options?.body) {
      body = options.body;
    } else if (options?.data) {
      body = options.data;
    }

    const native = await this.io().request(
      url,
      method,
      headersToArray(headers),
      body,
      options?.timeout ?? 30000,
      options?.followRedirects ?? true
    );

    return createResponse(native);
  }

  // ==========================================================================
  // Convenience Methods
  // ==========================================================================

  /**
   * HTTP GET request
   */
  get(url: string, options?: RequestOptions): Promise<Response> {
    return this.request('GET', url, options);
  }

  /**
   * HTTP POST request
   */
  post(url: string, options?: RequestOptionsWithBody): Promise<Response> {
    return this.request('POST', url, options);
  }

  /**
   * HTTP PUT request
   */
  put(url: string, options?: RequestOptionsWithBody): Promise<Response> {
    return this.request('PUT', url, options);
  }

  /**
   * HTTP PATCH request
   */
  patch(url: string, options?: RequestOptionsWithBody): Promise<Response> {
    return this.request('PATCH', url, options);
  }

  /**
   * HTTP DELETE request
   */
  delete(url: string, options?: RequestOptions): Promise<Response> {
    return this.request('DELETE', url, options);
  }

  /**
   * HTTP HEAD request
   */
  head(url: string, options?: RequestOptions): Promise<Response> {
    return this.request('HEAD', url, options);
  }

  /**
   * HTTP OPTIONS request
   */
  options(url: string, options?: RequestOptions): Promise<Response> {
    return this.request('OPTIONS', url, options);
  }

  // ==========================================================================
  // File Operations
  // ==========================================================================

  /**
   * Download file from URL
   *
   * @param url Download URL
   * @param destinationPath Local file path to save
   * @param options Download options
   *
   * @example
   * ```typescript
   * // Simple download
   * const result = await request.download(
   *   'https://example.com/file.zip',
   *   '/path/to/file.zip'
   * );
   *
   * // Resumable download
   * const result = await request.download(
   *   'https://example.com/large-file.zip',
   *   '/path/to/file.zip',
   *   { resumable: true }
   * );
   * ```
   */
  async download(
    url: string,
    destinationPath: string,
    options?: DownloadOptions
  ): Promise<DownloadResult> {
    // Note: Progress callback is not yet implemented in native layer
    const native = await this.io().download(
      url,
      destinationPath,
      headersToArray(options?.headers),
      options?.timeout ?? 60000,
      options?.resumable ?? false
    );

    return {
      ok: native.success,
      status: native.statusCode,
      filePath: native.filePath,
      fileSize: native.fileSize,
      error: native.success ? undefined : native.errorMessage,
    };
  }

  /**
   * Upload file to URL (multipart/form-data)
   *
   * @param url Upload URL
   * @param filePath Local file path to upload
   * @param options Upload options
   *
   * @example
   * ```typescript
   * // Simple upload
   * const result = await request.upload(
   *   'https://api.example.com/upload',
   *   '/path/to/photo.jpg'
   * );
   *
   * // Upload with options
   * const result = await request.upload(
   *   'https://api.example.com/upload',
   *   '/path/to/photo.jpg',
   *   {
   *     fieldName: 'image',
   *     fileName: 'my-photo.jpg',
   *     mimeType: 'image/jpeg',
   *     formFields: { description: 'My photo' }
   *   }
   * );
   * ```
   */
  async upload(
    url: string,
    filePath: string,
    options?: UploadOptions
  ): Promise<UploadResult> {
    const formKeys: string[] = [];
    const formValues: string[] = [];

    if (options?.formFields) {
      for (const [key, value] of Object.entries(options.formFields)) {
        formKeys.push(key);
        formValues.push(value);
      }
    }

    const native = await this.io().upload(
      url,
      filePath,
      options?.fieldName ?? 'file',
      options?.fileName ?? '',
      options?.mimeType ?? '',
      headersToArray(options?.headers),
      formKeys,
      formValues,
      options?.timeout ?? 60000
    );

    return {
      ok: native.success,
      status: native.statusCode,
      responseText: new TextDecoder().decode(native.responseBody),
      error: native.success ? undefined : native.errorMessage,
    };
  }
}

// ============================================================================
// Export
// ============================================================================

/**
 * Request proxy that creates a new RequestClient for each method call.
 * Each HTTP request uses a fresh native client and releases it after completion.
 *
 * @example
 * ```typescript
 * import { request } from 'react-native-io';
 *
 * // Each call creates a new client internally
 * const res = await request.get('https://api.example.com/data');
 * ```
 */
export const request = new Proxy({} as RequestClient, {
  get(_target, prop: keyof RequestClient) {
    // Create a new client for each method access
    const client = new RequestClient();
    const value = client[prop];
    if (typeof value === 'function') {
      // Return a wrapper that calls the method and disposes the client
      return async (...args: unknown[]) => {
        try {
          return await (value as Function).apply(client, args);
        } finally {
          client.dispose();
        }
      };
    }
    return value;
  },
});

/**
 * Request class for creating custom instances with manual lifecycle control.
 *
 * @example
 * ```typescript
 * import { Request } from 'react-native-io';
 *
 * const client = new Request();
 * const res1 = await client.get('https://api.example.com/users');
 * const res2 = await client.post('https://api.example.com/users', { json: { name: 'John' } });
 * client.dispose(); // Release native resources
 * ```
 */
export { RequestClient as Request };
