/**
 * @file types.ts
 * @description Type definitions for React Native IO library
 *
 * Optimized for mobile platforms (iOS/Android)
 */

// ============================================================================
// Enums
// ============================================================================

/**
 * Platform type identifier
 */
export enum IOPlatformType {
  /** iOS platform */
  iOS = 'ios',
  /** Android platform */
  Android = 'android',
}

/**
 * File system entity type
 */
export enum EntityType {
  /** Path does not exist */
  NotFound = 0,
  /** Regular file */
  File = 1,
  /** Directory */
  Directory = 2,
}

/**
 * File write mode
 */
export enum WriteMode {
  /** Overwrite existing file (truncate) */
  Overwrite = 0,
  /** Append to existing file */
  Append = 1,
}

/**
 * Hash algorithm for file integrity check
 */
export enum HashAlgorithm {
  /** MD5 - fast but less secure */
  MD5 = 0,
  /** SHA-1 */
  SHA1 = 1,
  /** SHA-256 - recommended (default) */
  SHA256 = 2,
  /** SHA3-224 */
  SHA3_224 = 3,
  /** SHA3-256 */
  SHA3_256 = 4,
  /** SHA3-384 */
  SHA3_384 = 5,
  /** SHA3-512 */
  SHA3_512 = 6,
  /** Keccak-224 */
  Keccak224 = 7,
  /** Keccak-256 (used in Ethereum) */
  Keccak256 = 8,
  /** Keccak-384 */
  Keccak384 = 9,
  /** Keccak-512 */
  Keccak512 = 10,
  /** CRC32 - checksum, not cryptographic */
  CRC32 = 11,
}

// ============================================================================
// Data Types
// ============================================================================

/**
 * File or directory metadata
 */
export interface FileMetadata {
  /** Size in bytes (0 for directories) */
  size: number;
  /** Last modified time in milliseconds since epoch */
  modifiedTime: number;
  /** Entity type */
  type: EntityType;
}

/**
 * Directory entry information
 */
export interface DirectoryEntry {
  /** Full path */
  path: string;
  /** File/directory name */
  name: string;
  /** Entity type */
  type: EntityType;
  /** Size in bytes (0 for directories) */
  size: number;
}

// ============================================================================
// Native Module Interface
// ============================================================================

/**
 * Native IOFileSystem interface (exposed via JSI)
 *
 * All methods ending with 'Sync' are synchronous and block the JS thread.
 * Methods without 'Sync' suffix return Promises and run on a thread pool.
 */
export interface IOFileSystem {
  // ========================================================================
  // Query Operations
  // ========================================================================

  /** Check if path exists */
  exists(path: string): Promise<boolean>;
  existsSync(path: string): boolean;

  /** Check if path is a regular file */
  isFile(path: string): Promise<boolean>;
  isFileSync(path: string): boolean;

  /** Check if path is a directory */
  isDirectory(path: string): Promise<boolean>;
  isDirectorySync(path: string): boolean;

  /** Get file/directory metadata */
  getMetadata(path: string): Promise<FileMetadata>;
  getMetadataSync(path: string): FileMetadata;

  /** Get file size in bytes */
  getFileSize(path: string): Promise<number>;
  getFileSizeSync(path: string): number;

  /** Get last modified time (milliseconds since epoch) */
  getModifiedTime(path: string): Promise<number>;
  getModifiedTimeSync(path: string): number;

  // ========================================================================
  // File Read Operations
  // ========================================================================

  /** Read file as UTF-8 string */
  readString(path: string): Promise<string>;
  readStringSync(path: string): string;

  /** Read file as binary data */
  readBytes(path: string): Promise<ArrayBuffer>;
  readBytesSync(path: string): ArrayBuffer;

  // ========================================================================
  // File Write Operations
  // ========================================================================

  /**
   * Write string to file
   * @param path File path
   * @param content String content
   * @param mode Write mode (default: Overwrite)
   * @param createParents Create parent directories if needed
   */
  writeString(
    path: string,
    content: string,
    mode?: WriteMode,
    createParents?: boolean
  ): Promise<void>;
  writeStringSync(
    path: string,
    content: string,
    mode?: WriteMode,
    createParents?: boolean
  ): void;

  /**
   * Write binary data to file
   * @param path File path
   * @param data Binary data
   * @param mode Write mode (default: Overwrite)
   * @param createParents Create parent directories if needed
   */
  writeBytes(
    path: string,
    data: ArrayBuffer,
    mode?: WriteMode,
    createParents?: boolean
  ): Promise<void>;
  writeBytesSync(
    path: string,
    data: ArrayBuffer,
    mode?: WriteMode,
    createParents?: boolean
  ): void;

  // ========================================================================
  // File Management
  // ========================================================================

  /** Create empty file */
  createFile(path: string, createParents?: boolean): Promise<void>;
  createFileSync(path: string, createParents?: boolean): void;

  /** Delete file (returns true if deleted) */
  deleteFile(path: string): Promise<boolean>;
  deleteFileSync(path: string): boolean;

  /** Copy file */
  copyFile(
    sourcePath: string,
    destinationPath: string,
    overwrite?: boolean
  ): Promise<void>;
  copyFileSync(
    sourcePath: string,
    destinationPath: string,
    overwrite?: boolean
  ): void;

  /** Move/rename file */
  moveFile(sourcePath: string, destinationPath: string): Promise<void>;
  moveFileSync(sourcePath: string, destinationPath: string): void;

  // ========================================================================
  // Directory Operations
  // ========================================================================

  /** Create directory */
  createDirectory(path: string, recursive?: boolean): Promise<void>;
  createDirectorySync(path: string, recursive?: boolean): void;

  /** Delete directory (returns number of items deleted) */
  deleteDirectory(path: string, recursive?: boolean): Promise<number>;
  deleteDirectorySync(path: string, recursive?: boolean): number;

  /** List directory contents */
  listDirectory(path: string, recursive?: boolean): Promise<DirectoryEntry[]>;
  listDirectorySync(path: string, recursive?: boolean): DirectoryEntry[];

  /** Move/rename directory */
  moveDirectory(sourcePath: string, destinationPath: string): Promise<void>;
  moveDirectorySync(sourcePath: string, destinationPath: string): void;

  // ========================================================================
  // Path Operations (Pure - no I/O)
  // ========================================================================

  /** Get parent directory path */
  getParentPath(path: string): string;

  /** Get filename from path */
  getFileName(path: string): string;

  /** Get file extension (e.g., ".txt") */
  getFileExtension(path: string): string;

  /** Get filename without extension */
  getFileNameWithoutExtension(path: string): string;

  /** Join path components */
  joinPaths(...paths: string[]): string;

  /** Get absolute path */
  getAbsolutePath(path: string): Promise<string>;
  getAbsolutePathSync(path: string): string;

  /** Normalize path (resolve . and ..) */
  normalizePath(path: string): Promise<string>;
  normalizePathSync(path: string): string;

  // ========================================================================
  // Storage Information
  // ========================================================================

  /** Get available storage space in bytes */
  getAvailableSpace(path: string): Promise<number>;
  getAvailableSpaceSync(path: string): number;

  /** Get total storage space in bytes */
  getTotalSpace(path: string): Promise<number>;
  getTotalSpaceSync(path: string): number;

  // ========================================================================
  // Utility
  // ========================================================================

  /**
   * Calculate file hash
   * @param path File path
   * @param algorithm Hash algorithm (default: SHA256)
   */
  calcHash(path: string, algorithm?: HashAlgorithm): Promise<string>;
}

// ============================================================================
// HTTP Request Types
// ============================================================================

/**
 * Native HTTP response
 * @internal
 */
export interface NativeHttpResponse {
  success: boolean;
  statusCode: number;
  statusMessage: string;
  url: string;
  errorMessage: string;
  body: ArrayBuffer;
  headerKeys: string[];
  headerValues: string[];
}

/**
 * Native download result
 * @internal
 */
export interface NativeDownloadResult {
  success: boolean;
  statusCode: number;
  filePath: string;
  fileSize: number;
  errorMessage: string;
}

/**
 * Native upload result
 * @internal
 */
export interface NativeUploadResult {
  success: boolean;
  statusCode: number;
  responseBody: ArrayBuffer;
  errorMessage: string;
}

// ============================================================================
// Platform Interface
// ============================================================================

/**
 * Base platform interface (common properties)
 */
interface IOBasePlatform {
  /** Current platform identifier */
  readonly platform: IOPlatformType;
  /** Primary files directory */
  readonly filesDir: string;
  /** Cache directory */
  readonly cacheDir: string;
  /** Documents directory */
  readonly documentsDir: string;
}

/**
 * Android-specific platform interface
 */
export interface IOPlatformAndroid extends IOBasePlatform {
  readonly platform: IOPlatformType.Android;
  /** Internal files directory (e.g., /data/data/<pkg>/files) */
  readonly filesDir: string;
  /** Internal cache directory (e.g., /data/data/<pkg>/cache) */
  readonly cacheDir: string;
  /** Code cache directory (e.g., /data/data/<pkg>/code_cache) */
  readonly codeCacheDir: string;
  /** No-backup files directory (e.g., /data/data/<pkg>/no_backup) */
  readonly noBackupFilesDir: string;
  /** Data directory (e.g., /data/data/<pkg>), API 24+ only */
  readonly dataDir: string;
  /** External files directory root */
  readonly externalFilesDir: string;
  /** External cache directory */
  readonly externalCacheDir: string;
  /** OBB directory for expansion files */
  readonly obbDir: string;
  /** External Downloads directory */
  readonly downloadsDir: string;
  /** External Pictures directory */
  readonly picturesDir: string;
  /** External Movies directory */
  readonly moviesDir: string;
  /** External Music directory */
  readonly musicDir: string;
  /** External Documents directory */
  readonly documentsDir: string;
  /** External DCIM directory */
  readonly dcimDir: string;
  /** Android SDK version */
  readonly sdkVersion: number;
  /** Get all external cache directories (multiple storage volumes) */
  getExternalCacheDirs(): string[];
  /** Get all external files directories (multiple storage volumes) */
  getExternalFilesDirs(): string[];
  /** Get all OBB directories (multiple storage volumes) */
  getObbDirs(): string[];
}

/**
 * iOS-specific platform interface
 */
export interface IOPlatformIOS extends IOBasePlatform {
  readonly platform: IOPlatformType.iOS;
  /** Documents directory (backed up by iCloud) */
  readonly documentsDir: string;
  /** Library directory */
  readonly libraryDir: string;
  /** Caches directory (not backed up) */
  readonly cacheDir: string;
  /** Temporary directory (may be purged by system) */
  readonly tempDir: string;
  /** Application Support directory */
  readonly applicationSupportDir: string;
  /** App bundle directory (read-only) */
  readonly bundleDir: string;
  /** Home directory */
  readonly homeDir: string;
  /** Alias for documentsDir (Android compatibility) */
  readonly filesDir: string;
}

/**
 * Platform union type
 */
export type IOPlatform = IOPlatformAndroid | IOPlatformIOS;

/**
 * Native IORequest interface (exposed via JSI)
 * @internal
 */
export interface IORequest {
  /**
   * Execute HTTP request
   * @param url Request URL
   * @param method HTTP method
   * @param headers Header keys and values as flat array [key1, val1, key2, val2, ...]
   * @param body Request body (string or ArrayBuffer)
   * @param timeout Timeout in milliseconds
   * @param followRedirects Whether to follow redirects
   */
  request(
    url: string,
    method: string,
    headers: string[],
    body: string | ArrayBuffer | null,
    timeout: number,
    followRedirects: boolean
  ): Promise<NativeHttpResponse>;

  /**
   * Download file
   */
  download(
    url: string,
    destinationPath: string,
    headers: string[],
    timeout: number,
    resumable: boolean
  ): Promise<NativeDownloadResult>;

  /**
   * Upload file
   */
  upload(
    url: string,
    filePath: string,
    fieldName: string,
    fileName: string,
    mimeType: string,
    headers: string[],
    formKeys: string[],
    formValues: string[],
    timeout: number
  ): Promise<NativeUploadResult>;
}
