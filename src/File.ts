/**
 * @file File.ts
 * @description File class for React Native IO
 *
 * Represents a file on the filesystem with async and sync operations.
 */

import {
  HashAlgorithm,
  WriteMode,
  type BinaryInput,
  type FileMetadata,
  type IOFileSystem,
} from './types';
import { getDefaultFileSystem } from './NativeStdIO';

/**
 * Represents a file on the filesystem.
 *
 * Provides methods to read, write, copy, move, and delete files.
 * Standalone File instances lazily share the default native FileSystem client,
 * which remains available for the JavaScript runtime lifetime.
 *
 * **Important**: Prefer async methods to avoid blocking the JS thread.
 * Sync methods (ending with `Sync`) should only be used when absolutely necessary,
 * such as in initialization code or when async is not possible.
 *
 * @example
 * ```typescript
 * const file = new File('/path/to/file.txt');
 *
 * // Properties (pure string computation, no I/O)
 * console.log(file.name, file.extension);
 *
 * // Async operations (recommended)
 * if (await file.exists()) {
 *   const content = await file.readString();
 * }
 * await file.writeString('Hello, World!');
 *
 * // Optional: release this object's reference to the shared filesystem.
 * // The runtime-wide default worker remains available for reuse.
 * file.dispose();
 * ```
 */
export class File {
  private readonly _path: string;
  private _fs: IOFileSystem | null = null;
  private readonly _sharedFs: boolean;

  /**
   * Create a File instance
   * @param path File path (absolute or relative)
   * @param fs Optional shared IOFileSystem instance. If provided, this File
   *           will use the shared instance instead of creating its own.
   *           Useful for batch operations to share a single thread pool.
   */
  constructor(path: string, fs?: IOFileSystem) {
    this._path = path;
    if (fs) {
      this._fs = fs;
      this._sharedFs = true;
    } else {
      this._sharedFs = false;
    }
  }

  /**
   * Get or create the underlying IOFileSystem instance (lazy initialization).
   * Standalone operations share one single-threaded pool; use openFS(n) for isolated concurrency.
   */
  private fs(): IOFileSystem {
    if (!this._fs) {
      this._fs = getDefaultFileSystem();
    }
    return this._fs;
  }

  /**
   * Release this object's filesystem reference. The runtime's shared default
   * worker remains available to other standalone paths. An explicit FSContext
   * owns shared children and controls their lifetime through close().
   */
  dispose(): void {
    // Only release if we own the fs (not shared)
    if (!this._sharedFs) {
      this._fs = null;
    }
  }

  // ==========================================================================
  // Properties (Pure string computation - no I/O, safe to use)
  // ==========================================================================

  /** Full file path */
  get path(): string {
    return this._path;
  }

  /** File name (e.g., "file.txt") */
  get name(): string {
    return this.fs().getFileName(this._path);
  }

  /** File extension (e.g., ".txt") */
  get extension(): string {
    return this.fs().getFileExtension(this._path);
  }

  /** File name without extension (e.g., "file") */
  get nameWithoutExtension(): string {
    return this.fs().getFileNameWithoutExtension(this._path);
  }

  /** Parent directory path */
  get parent(): string {
    return this.fs().getParentPath(this._path);
  }

  // ==========================================================================
  // Query Operations (Async - recommended)
  // ==========================================================================

  /** Check if file exists */
  exists(): Promise<boolean> {
    return this.fs().exists(this._path);
  }

  /** Check if this is a regular file */
  isFile(): Promise<boolean> {
    return this.fs().isFile(this._path);
  }

  /** Get file size in bytes */
  size(): Promise<number> {
    return this.fs().getFileSize(this._path);
  }

  /** Get file metadata */
  metadata(): Promise<FileMetadata> {
    return this.fs().getMetadata(this._path);
  }

  /** Get last modified time */
  modifiedTime(): Promise<Date> {
    return this.fs()
      .getModifiedTime(this._path)
      .then((ms) => new Date(ms));
  }

  /** Get absolute path */
  absolutePath(): Promise<string> {
    return this.fs().getAbsolutePath(this._path);
  }

  // ==========================================================================
  // Query Operations (Sync - blocks JS thread, use sparingly!)
  // ==========================================================================

  /**
   * Check if file exists (sync)
   * @warning Blocks JS thread. Prefer `exists()` async version.
   */
  existsSync(): boolean {
    return this.fs().existsSync(this._path);
  }

  /**
   * Check if this is a regular file (sync)
   * @warning Blocks JS thread. Prefer `isFile()` async version.
   */
  isFileSync(): boolean {
    return this.fs().isFileSync(this._path);
  }

  /**
   * Get file size in bytes (sync)
   * @warning Blocks JS thread. Prefer `size()` async version.
   */
  sizeSync(): number {
    return this.fs().getFileSizeSync(this._path);
  }

  /**
   * Get file metadata (sync)
   * @warning Blocks JS thread. Prefer `metadata()` async version.
   */
  metadataSync(): FileMetadata {
    return this.fs().getMetadataSync(this._path);
  }

  /**
   * Get last modified time (sync)
   * @warning Blocks JS thread. Prefer `modifiedTime()` async version.
   */
  modifiedTimeSync(): Date {
    return new Date(this.fs().getModifiedTimeSync(this._path));
  }

  /**
   * Get absolute path (sync)
   * @warning Blocks JS thread. Prefer `absolutePath()` async version.
   */
  absolutePathSync(): string {
    return this.fs().getAbsolutePathSync(this._path);
  }

  // ==========================================================================
  // Read Operations (Async - recommended)
  // ==========================================================================

  /** Read file as UTF-8 string */
  readString(): Promise<string> {
    return this.fs().readString(this._path);
  }

  /** Read file as binary data */
  readBytes(): Promise<ArrayBuffer> {
    return this.fs().readBytes(this._path);
  }

  /** Read file as string lines */
  readLines(): Promise<string[]> {
    return this.readString().then((text) =>
      text.split('\n').map((line) => line.replace(/\r$/, ''))
    );
  }

  // ==========================================================================
  // Read Operations (Sync)
  // ==========================================================================

  /**
   * Read file as UTF-8 string (sync)
   * @warning Blocks JS thread. Prefer `readString()` async version.
   */
  readStringSync(): string {
    return this.fs().readStringSync(this._path);
  }

  /**
   * Read file as binary data (sync)
   * @warning Blocks JS thread. Prefer `readBytes()` async version.
   */
  readBytesSync(): ArrayBuffer {
    return this.fs().readBytesSync(this._path);
  }

  /**
   * Read file as string lines (sync)
   * @warning Blocks JS thread. Prefer `readLines()` async version.
   */
  readLinesSync(): string[] {
    return this.readStringSync()
      .split('\n')
      .map((line) => line.replace(/\r$/, ''));
  }

  // ==========================================================================
  // Write Operations (Async)
  // ==========================================================================

  /**
   * Write string to file
   * @param content String content to write
   * @param mode Write mode (default: Overwrite)
   */
  writeString(
    content: string,
    mode: WriteMode = WriteMode.Overwrite
  ): Promise<void> {
    return this.fs().writeString(this._path, content, mode, true);
  }

  /**
   * Write binary data to file
   * @param data Binary data to write
   * @param mode Write mode (default: Overwrite)
   */
  writeBytes(
    data: BinaryInput,
    mode: WriteMode = WriteMode.Overwrite
  ): Promise<void> {
    return this.fs().writeBytes(this._path, data, mode, true);
  }

  /**
   * Append string to file
   * @param content String content to append
   */
  appendString(content: string): Promise<void> {
    return this.writeString(content, WriteMode.Append);
  }

  /**
   * Append binary data to file
   * @param data Binary data to append
   */
  appendBytes(data: BinaryInput): Promise<void> {
    return this.writeBytes(data, WriteMode.Append);
  }

  // ==========================================================================
  // Write Operations (Sync)
  // ==========================================================================

  /**
   * Write string to file (sync)
   * @param content String content to write
   * @param mode Write mode (default: Overwrite)
   * @warning Blocks JS thread. Prefer `writeString()` async version.
   */
  writeStringSync(
    content: string,
    mode: WriteMode = WriteMode.Overwrite
  ): void {
    this.fs().writeStringSync(this._path, content, mode, true);
  }

  /**
   * Write binary data to file (sync)
   * @param data Binary data to write
   * @param mode Write mode (default: Overwrite)
   * @warning Blocks JS thread. Prefer `writeBytes()` async version.
   */
  writeBytesSync(
    data: BinaryInput,
    mode: WriteMode = WriteMode.Overwrite
  ): void {
    this.fs().writeBytesSync(this._path, data, mode, true);
  }

  /**
   * Append string to file (sync)
   * @param content String content to append
   * @warning Blocks JS thread. Prefer `appendString()` async version.
   */
  appendStringSync(content: string): void {
    this.writeStringSync(content, WriteMode.Append);
  }

  /**
   * Append binary data to file (sync)
   * @param data Binary data to append
   * @warning Blocks JS thread. Prefer `appendBytes()` async version.
   */
  appendBytesSync(data: BinaryInput): void {
    this.writeBytesSync(data, WriteMode.Append);
  }

  // ==========================================================================
  // File Management (Async)
  // ==========================================================================

  /**
   * Create the file if it doesn't exist
   * @param createParents Create parent directories if needed
   */
  create(createParents: boolean = true): Promise<File> {
    return this.fs()
      .createFile(this._path, createParents)
      .then(() => this);
  }

  /**
   * Delete the file
   * @returns true if file was deleted
   */
  delete(): Promise<boolean> {
    return this.fs().deleteFile(this._path);
  }

  /**
   * Copy file to destination
   * @param destinationPath Destination path
   * @param overwrite Overwrite if exists (default: true)
   * @returns New File instance for the copy (shares the same IOFileSystem)
   */
  copy(destinationPath: string, overwrite: boolean = true): Promise<File> {
    const fsInstance = this.fs();
    return fsInstance
      .copyFile(this._path, destinationPath, overwrite)
      .then(() => new File(destinationPath, fsInstance));
  }

  /**
   * Move/rename file
   * @param destinationPath Destination path
   * @returns New File instance at new location (shares the same IOFileSystem)
   */
  move(destinationPath: string): Promise<File> {
    const fsInstance = this.fs();
    return fsInstance
      .moveFile(this._path, destinationPath)
      .then(() => new File(destinationPath, fsInstance));
  }

  /**
   * Calculate file hash
   * @param algorithm Hash algorithm (default: SHA256)
   */
  calcHash(algorithm: HashAlgorithm = HashAlgorithm.SHA256): Promise<string> {
    return this.fs().calcHash(this._path, algorithm);
  }

  // ==========================================================================
  // File Management (Sync)
  // ==========================================================================

  /**
   * Create the file if it doesn't exist (sync)
   * @param createParents Create parent directories if needed
   * @warning Blocks JS thread. Prefer `create()` async version.
   */
  createSync(createParents: boolean = true): void {
    this.fs().createFileSync(this._path, createParents);
  }

  /**
   * Delete the file (sync)
   * @returns true if file was deleted
   * @warning Blocks JS thread. Prefer `delete()` async version.
   */
  deleteSync(): boolean {
    return this.fs().deleteFileSync(this._path);
  }

  /**
   * Copy file to destination (sync)
   * @param destinationPath Destination path
   * @param overwrite Overwrite if exists (default: true)
   * @returns New File instance for the copy (shares the same IOFileSystem)
   * @warning Blocks JS thread. Prefer `copy()` async version.
   */
  copySync(destinationPath: string, overwrite: boolean = true): File {
    const fsInstance = this.fs();
    fsInstance.copyFileSync(this._path, destinationPath, overwrite);
    return new File(destinationPath, fsInstance);
  }

  /**
   * Move/rename file (sync)
   * @param destinationPath Destination path
   * @returns New File instance at new location (shares the same IOFileSystem)
   * @warning Blocks JS thread. Prefer `move()` async version.
   */
  moveSync(destinationPath: string): File {
    const fsInstance = this.fs();
    fsInstance.moveFileSync(this._path, destinationPath);
    return new File(destinationPath, fsInstance);
  }

  // ==========================================================================
  // Utility
  // ==========================================================================

  /** String representation */
  toString(): string {
    return this._path;
  }

  /** JSON representation */
  toJSON(): { path: string; name: string; extension: string } {
    return {
      path: this._path,
      name: this.name,
      extension: this.extension,
    };
  }
}
