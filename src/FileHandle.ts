/**
 * @file FileHandle.ts
 * @description File handle for streaming I/O operations
 *
 * Provides a handle-based API for efficient file operations without
 * reopening the file for each read/write.
 */

import type { IOFileSystem } from './types';
import { FileOpenMode, SeekOrigin } from './types';

/**
 * File handle for streaming I/O operations.
 *
 * Allows multiple read/write operations on a file without reopening it.
 * The handle must be closed when done to release native resources.
 *
 * @example
 * ```typescript
 * const fs = openFS();
 * const handle = fs.open('/path/to/file.txt', FileOpenMode.Read);
 *
 * // Read line by line
 * while (!(await handle.isEOF())) {
 *   const line = await handle.readLine();
 *   console.log(line);
 * }
 *
 * handle.close();
 * ```
 *
 * @example
 * ```typescript
 * // Write multiple lines
 * const handle = fs.open('/path/to/output.txt', FileOpenMode.Write);
 * await handle.writeLine('First line');
 * await handle.writeLine('Second line');
 * handle.close();
 * ```
 */
export class FileHandle {
  private _fs: IOFileSystem;
  private _handle: number;
  private _closed: boolean = false;

  /**
   * @internal
   * Use FSContext.openFile() to create a FileHandle.
   */
  constructor(fs: IOFileSystem, path: string, mode: FileOpenMode) {
    this._fs = fs;
    this._handle = fs.openFile(path, mode);
  }

  private ensureOpen(): void {
    if (this._closed) {
      throw new Error('FileHandle has been closed');
    }
  }

  /**
   * Close the file handle.
   *
   * Must be called when done to release native resources.
   * After closing, all other methods will throw an error.
   */
  close(): void {
    if (!this._closed) {
      this._fs.fileClose(this._handle);
      this._closed = true;
    }
  }

  /**
   * Check if the handle is closed.
   */
  get isClosed(): boolean {
    return this._closed;
  }

  // ==========================================================================
  // Position Operations (Async)
  // ==========================================================================

  /**
   * Seek to a position in the file.
   *
   * @param offset Byte offset
   * @param origin Seek origin (default: Begin)
   * @returns New position in bytes
   */
  seek(offset: number, origin: SeekOrigin = SeekOrigin.Begin): Promise<number> {
    this.ensureOpen();
    return this._fs.fileSeek(this._handle, offset, origin);
  }

  /**
   * Rewind to the beginning of the file.
   */
  rewind(): Promise<void> {
    this.ensureOpen();
    return this._fs.fileRewind(this._handle);
  }

  /**
   * Get current file position.
   *
   * @returns Current position in bytes
   */
  getPosition(): Promise<number> {
    this.ensureOpen();
    return this._fs.fileGetPosition(this._handle);
  }

  /**
   * Get file size.
   *
   * @returns File size in bytes
   */
  getSize(): Promise<number> {
    this.ensureOpen();
    return this._fs.fileGetSize(this._handle);
  }

  /**
   * Check if at end of file.
   *
   * @returns true if at EOF
   */
  isEOF(): Promise<boolean> {
    this.ensureOpen();
    return this._fs.fileIsEOF(this._handle);
  }

  // ==========================================================================
  // I/O Operations (Async - disk bound)
  // ==========================================================================

  /**
   * Flush file buffer to disk.
   */
  flush(): Promise<void> {
    this.ensureOpen();
    return this._fs.fileFlush(this._handle);
  }

  /**
   * Truncate file at current position.
   */
  truncate(): Promise<void> {
    this.ensureOpen();
    return this._fs.fileTruncate(this._handle);
  }

  /**
   * Read bytes from file.
   *
   * @param size Number of bytes to read (default: read all remaining)
   * @returns ArrayBuffer containing read data
   */
  read(size?: number): Promise<ArrayBuffer> {
    this.ensureOpen();
    return this._fs.fileRead(this._handle, size);
  }

  /**
   * Read file as string.
   *
   * @param size Number of bytes to read (default: read all remaining)
   * @returns String content
   */
  readString(size?: number): Promise<string> {
    this.ensureOpen();
    return this._fs.fileReadString(this._handle, size);
  }

  /**
   * Read a single line from file.
   *
   * @returns Line content (without newline)
   */
  readLine(): Promise<string> {
    this.ensureOpen();
    return this._fs.fileReadLine(this._handle);
  }

  /**
   * Write bytes to file.
   *
   * @param data Data to write
   * @returns Number of bytes written
   */
  write(data: ArrayBuffer): Promise<number> {
    this.ensureOpen();
    return this._fs.fileWrite(this._handle, data);
  }

  /**
   * Write string to file.
   *
   * @param content String to write
   * @returns Number of bytes written
   */
  writeString(content: string): Promise<number> {
    this.ensureOpen();
    return this._fs.fileWriteString(this._handle, content);
  }

  /**
   * Write a line to file (appends newline).
   *
   * @param line Line content
   * @returns Number of bytes written
   */
  writeLine(line: string): Promise<number> {
    this.ensureOpen();
    return this._fs.fileWriteLine(this._handle, line);
  }
}
