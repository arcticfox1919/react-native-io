/**
 * @file index.tsx
 * @description React Native IO library - Dart dart:io style file system API
 *
 * File system APIs for React Native using C++ and JSI.
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
  FileOpenMode,
  SeekOrigin,
  type FileHandleId,
  type FileMetadata,
  type DirectoryEntry,
  type IOFileSystem,
  type IORequest,
  type IOPlatform,
  type IOPlatformAndroid,
  type IOPlatformIOS,
  type IOPlatformHarmony,
} from './types';
export type { StringEncoding } from './NativeStdIO';

// Export classes
export { File } from './File';
export { Directory } from './Directory';
export { FileHandle } from './FileHandle';
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
import type { IOPlatform } from './types';
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
 * } else if (Platform.platform === IOPlatformType.iOS) {
 *   console.log('Bundle:', Platform.bundleDir);
 *   console.log('Temp:', Platform.tempDir);
 * } else {
 *   console.log('Files:', Platform.filesDir);
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
   * import { FS, IOPlatformType } from 'react-native-io';
   *
   * const platform = FS.getPlatform();
   * if (platform.platform === IOPlatformType.Android) {
   *   console.log(platform.sdkVersion, platform.externalFilesDir);
   * } else if (platform.platform === IOPlatformType.iOS) {
   *   console.log(platform.bundleDir, platform.tempDir);
   * } else {
   *   console.log(platform.filesDir, platform.tempDir);
   * }
   * ```
   */
  getPlatform<T extends IOPlatform = IOPlatform>(): T {
    return _getPlatform() as T;
  },

  // ==========================================================================
  // Directory Paths (cross-platform)
  // ==========================================================================

  /**
   * Cache directory path
   * - iOS: <App>/Library/Caches
   * - Android: /data/data/<pkg>/cache
   * - HarmonyOS: ApplicationContext.cacheDir
   */
  get cacheDir(): string {
    return _getPlatform().cacheDir;
  },

  /**
   * Document directory path (for persistent application files)
   * - iOS: <App>/Documents (backed up by iCloud)
   * - Android: /data/data/<pkg>/files (internal app storage)
   * - HarmonyOS: ApplicationContext.filesDir (internal app storage)
   */
  get documentDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.Android ? p.filesDir : p.documentsDir;
  },

  /**
   * Temporary directory path (may be purged by system)
   * - iOS: <App>/tmp
   * - Android: /data/data/<pkg>/cache (same as cacheDir)
   * - HarmonyOS: ApplicationContext.tempDir
   */
  get tempDir(): string {
    const p = _getPlatform();
    return p.platform === IOPlatformType.Android ? p.cacheDir : p.tempDir;
  },

  /**
   * Main bundle path (iOS only, read-only)
   * - iOS: <App>.app
   * - Android: undefined
   * - HarmonyOS: undefined
   */
  get bundleDir(): string | undefined {
    const p = _getPlatform();
    return p.platform === IOPlatformType.iOS ? p.bundleDir : undefined;
  },

  /**
   * External files directory (Android only)
   * - iOS: same as documentDir
   * - Android: /storage/emulated/0/Android/data/<pkg>/files
   * - HarmonyOS: same as documentDir (no external storage alias)
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
   * - HarmonyOS: same as cacheDir
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
   * - HarmonyOS: <filesDir>/Download (app-specific)
   */
  get downloadDir(): string {
    const p = _getPlatform();
    if (p.platform === IOPlatformType.Android) return p.downloadsDir;
    if (p.platform === IOPlatformType.Harmony)
      return FS.joinPaths(p.filesDir, 'Download');
    return p.documentsDir;
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
