/**
 * @file index.tsx
 * @description React Native IO library - Dart dart:io style file system API
 *
 * A high-performance file system library for React Native using pure C++ and JSI.
 * Provides File and Directory classes similar to Dart's dart:io library.
 *
 * All I/O operations have both async (Promise-based) and sync variants.
 * The async operations use a native thread pool to avoid blocking the JS thread.
 *
 * @example
 * ```typescript
 * import { File, Directory, EntityType, WriteMode } from 'react-native-io';
 *
 * // File operations
 * const file = new File('/path/to/file.txt');
 * await file.writeString('Hello, World!');
 * const content = await file.readString();
 *
 * // Binary operations
 * const bytes = await file.readBytes();
 * await file.writeBytes(new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]));
 *
 * // File info
 * const metadata = await file.getMetadata();
 * console.log('Size:', metadata.size, 'Modified:', new Date(metadata.modifiedTime));
 *
 * // Directory operations
 * const dir = new Directory('/path/to/dir');
 * await dir.create(true); // recursive
 *
 * // List contents
 * const entries = await dir.list();
 * for (const entry of entries) {
 *   console.log(entry.name, entry.type === EntityType.File ? 'file' : 'dir');
 * }
 *
 * // Get files and directories separately
 * const files = await dir.listFiles();
 * const subdirs = await dir.listDirectories();
 * ```
 */

// Export types
export {
  IOPlatformType,
  EntityType,
  WriteMode,
  HashAlgorithm,
  type FileMetadata,
  type DirectoryEntry,
  type IOFileSystem,
  type IORequest,
  type IOPlatform,
  type IOPlatformAndroid,
  type IOPlatformIOS,
} from './types';
export type { StringEncoding } from './NativeStdIO';

// Export classes
export { File } from './File';
export { Directory } from './Directory';
export { FSContext, openFS } from './FSContext';

// Export HTTP Request API
export {
  request,
  Request,
  type RequestOptions,
  type RequestOptionsWithBody,
  type Response,
  type DownloadOptions,
  type DownloadProgress,
  type DownloadResult,
  type UploadOptions,
  type UploadProgress,
  type UploadResult,
} from './Request';

// Re-export IO utilities from native module
import {
  createFileSystem,
  createPlatform,
  decodeString,
  encodeString,
  type StringEncoding,
} from './NativeStdIO';
import type { IOPlatform, IOPlatformAndroid, IOPlatformIOS } from './types';
import { IOPlatformType } from './types';

// Internal fs instance for utility functions (lazy initialized)
let _fs: ReturnType<typeof createFileSystem> | null = null;
const _getFs = () => {
  if (!_fs) {
    _fs = createFileSystem(1);
  }
  return _fs;
};

// Internal platform instance (lazy initialized, singleton)
let _platform: IOPlatform | null = null;
const _getPlatform = () => {
  if (!_platform) {
    _platform = createPlatform();
  }
  return _platform;
};

/**
 * Get the platform directories instance.
 * This is a singleton - the same instance is returned on each call.
 *
 * @returns Platform-specific directory paths
 *
 * @example
 * ```typescript
 * import { Platform, IOPlatformType } from 'react-native-io';
 *
 * // Common usage
 * const cacheDir = Platform.cacheDir;
 * const docsDir = Platform.documentsDir;
 *
 * // Platform-specific
 * if (Platform.platform === IOPlatformType.Android) {
 *   console.log('External files:', Platform.externalFilesDir);
 *   console.log('SDK Version:', Platform.sdkVersion);
 * } else {
 *   console.log('Bundle:', Platform.bundleDir);
 *   console.log('Temp:', Platform.tempDir);
 * }
 * ```
 */
export const Platform: IOPlatform = new Proxy({} as IOPlatform, {
  get(_target, prop) {
    const platform = _getPlatform();
    const value = (platform as any)[prop];
    if (typeof value === 'function') {
      return value.bind(platform);
    }
    return value;
  },
});

export const FS = {
  // ==========================================================================
  // Platform Access
  // ==========================================================================

  /**
   * Get the platform instance with full type information.
   *
   * @returns Platform-specific interface with all properties and methods
   *
   * @example
   * ```typescript
   * import { FS, IOPlatformType, IOPlatformAndroid, IOPlatformIOS } from 'react-native-io';
   *
   * const platform = FS.getPlatform();
   * if (platform.platform === IOPlatformType.Android) {
   *   const p = FS.getPlatform<IOPlatformAndroid>();
   *   console.log(p.sdkVersion, p.externalFilesDir);
   * } else {
   *   const p = FS.getPlatform<IOPlatformIOS>();
   *   console.log(p.bundleDir, p.tempDir);
   * }
   * ```
   */
  getPlatform<T extends IOPlatformAndroid | IOPlatformIOS = IOPlatform>(): T {
    return _getPlatform() as T;
  },

  // ==========================================================================
  // Directory Paths (cross-platform)
  // ==========================================================================

  /**
   * Cache directory path
   * - iOS: <App>/Library/Caches
   * - Android: /data/data/<pkg>/cache
   */
  get cacheDir(): string {
    return _getPlatform().cacheDir;
  },

  /**
   * Document directory path (for user-visible files)
   * - iOS: <App>/Documents (backed up by iCloud)
   * - Android: /data/data/<pkg>/files
   */
  get documentDir(): string {
    return _getPlatform().documentsDir;
  },

  /**
   * Temporary directory path (may be purged by system)
   * - iOS: <App>/tmp
   * - Android: /data/data/<pkg>/cache (same as cacheDir)
   */
  get tempDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.iOS ? p.tempDir : p.cacheDir;
  },

  /**
   * Main bundle path (iOS only, read-only)
   * - iOS: <App>.app
   * - Android: undefined
   */
  get bundleDir(): string | undefined {
    const p = _getPlatform();
    return p.platform === IOPlatformType.iOS ? p.bundleDir : undefined;
  },

  /**
   * External files directory (Android only)
   * - iOS: same as documentDir
   * - Android: /storage/emulated/0/Android/data/<pkg>/files
   */
  get externalDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.Android
      ? p.externalFilesDir
      : p.documentsDir;
  },

  /**
   * External cache directory (Android only)
   * - iOS: same as cacheDir
   * - Android: /storage/emulated/0/Android/data/<pkg>/cache
   */
  get externalCacheDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.Android
      ? p.externalCacheDir
      : p.cacheDir;
  },

  /**
   * Download directory (app-specific)
   * - iOS: <App>/Documents (no separate download folder)
   * - Android: /storage/emulated/0/Android/data/<pkg>/files/Download
   */
  get downloadDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.Android
      ? p.downloadsDir
      : p.documentsDir;
  },

  // ==========================================================================
  // Path Utilities
  // ==========================================================================

  /**
   * Join path segments
   */
  joinPaths: (...segments: string[]): string => {
    return _getFs().joinPaths(...segments);
  },

  // ==========================================================================
  // Storage Information
  // ==========================================================================

  /**
   * Get available storage space in bytes
   */
  getAvailableSpace: (path: string): Promise<number> => {
    return _getFs().getAvailableSpace(path);
  },

  /**
   * Get available storage space in bytes (sync)
   */
  getAvailableSpaceSync: (path: string): number => {
    return _getFs().getAvailableSpaceSync(path);
  },

  /**
   * Get total storage space in bytes
   */
  getTotalSpace: (path: string): Promise<number> => {
    return _getFs().getTotalSpace(path);
  },

  /**
   * Get total storage space in bytes (sync)
   */
  getTotalSpaceSync: (path: string): number => {
    return _getFs().getTotalSpaceSync(path);
  },

  // ==========================================================================
  // String Encoding/Decoding
  // ==========================================================================

  /**
   * Decode binary data to string using native implementation.
   * Much faster than JavaScript implementation for large buffers.
   *
   * @param buffer Binary data to decode
   * @param encoding String encoding (default: 'utf8')
   * @returns Decoded string
   *
   * @example
   * ```typescript
   * const buffer = new ArrayBuffer(5);
   * new Uint8Array(buffer).set([72, 101, 108, 108, 111]); // "Hello"
   * const str = FS.decodeString(buffer); // "Hello"
   * ```
   */
  decodeString: (
    buffer: ArrayBuffer,
    encoding: StringEncoding = 'utf8'
  ): string => {
    return decodeString(buffer, encoding);
  },

  /**
   * Encode string to binary data using native implementation.
   *
   * @param str String to encode
   * @param encoding String encoding (default: 'utf8')
   * @returns Encoded binary data
   *
   * @example
   * ```typescript
   * const buffer = FS.encodeString("Hello");
   * new Uint8Array(buffer); // [72, 101, 108, 108, 111]
   * ```
   */
  encodeString: (
    str: string,
    encoding: StringEncoding = 'utf8'
  ): ArrayBuffer => {
    return encodeString(str, encoding);
  },
};
