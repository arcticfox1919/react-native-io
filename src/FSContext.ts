/**
 * @file FSContext.ts
 * @description File system context for React Native IO
 *
 * Provides a shared context for File and Directory operations,
 * allowing multiple operations to share a single thread pool.
 */

import { File } from './File';
import { Directory } from './Directory';
import { FileHandle } from './FileHandle';
import { createFileSystem } from './NativeStdIO';
import type { IOFileSystem } from './types';
import { FileOpenMode } from './types';

/**
 * File system context for batch file operations.
 *
 * Creates a shared native FileSystem instance whose worker pool starts on its
 * first async operation and is reused across File and Directory operations. This is more efficient
 * than creating individual File/Directory instances when performing many
 * operations, as they all share the same thread pool.
 *
 * Call close() when the batch is complete. Garbage collection remains a
 * fallback, but explicit close starts native cleanup deterministically.
 *
 * @example
 * ```typescript
 * // Create a shared context using openFS()
 * const fs = openFS();
 *
 * // Create File and Directory instances that share the thread pool
 * const file1 = fs.file('/path/to/file1.txt');
 * const file2 = fs.file('/path/to/file2.txt');
 * const dir = fs.directory('/path/to/dir');
 *
 * // Perform operations - all share the same native thread pool
 * await Promise.all([
 *   file1.readString(),
 *   file2.writeString('content'),
 *   dir.list(),
 * ]);
 *
 * // Stop new work and release this context after submitted work drains.
 * fs.close();
 * ```
 *
 * @example
 * ```typescript
 * // With custom thread pool size for heavy parallel operations
 * const fs = openFS(4); // 4 worker threads
 *
 * const files = paths.map(p => fs.file(p));
 * const contents = await Promise.all(files.map(f => f.readString()));
 * ```
 */
export class FSContext {
  private _fs: IOFileSystem | null;
  private readonly state: { native: IOFileSystem | null };

  /**
   * Create a FSContext with a shared thread pool.
   * Prefer using `openFS()` function instead of direct constructor.
   *
   * @param numThreads Number of worker threads created on first async operation.
   *   - Default: 1 (single-threaded pool)
   *   - A single thread is sufficient for most use cases
   *   - Increase for better parallelism in concurrent file operations
   */
  constructor(numThreads?: number) {
    this.state = { native: createFileSystem(numThreads) };
    const state = this.state;
    const methods = new Map<PropertyKey, unknown>();
    this._fs = new Proxy({} as IOFileSystem, {
      get(_target, property) {
        const native = state.native;
        if (!native) throw new Error('FSContext has been closed');
        const value = Reflect.get(native, property);
        if (typeof value !== 'function') return value;
        if (!methods.has(property)) {
          methods.set(property, (...args: unknown[]) => {
            if (!state.native) throw new Error('FSContext has been closed');
            return Reflect.apply(
              Reflect.get(state.native, property),
              state.native,
              args
            );
          });
        }
        return methods.get(property);
      },
    });
  }

  /**
   * Get the underlying IOFileSystem instance.
   */
  private get fs(): IOFileSystem {
    if (!this._fs) {
      throw new Error('FSContext has been closed');
    }
    return this._fs;
  }

  /**
   * Create a File instance that shares this context's thread pool.
   *
   * @param path File path (absolute or relative)
   * @returns File instance using the shared IOFileSystem
   */
  file(path: string): File {
    return new File(path, this.fs);
  }

  /**
   * Create a Directory instance that shares this context's thread pool.
   *
   * @param path Directory path (absolute or relative)
   * @returns Directory instance using the shared IOFileSystem
   */
  directory(path: string): Directory {
    return new Directory(path, this.fs);
  }

  /**
   * Open a file and return a handle for streaming operations.
   *
   * File handles allow multiple read/write operations without reopening
   * the file each time. Remember to close the handle when done.
   *
   * @param path File path
   * @param mode Open mode (default: Read)
   * @returns FileHandle instance
   *
   * @example
   * ```typescript
   * const fs = openFS();
   * const handle = fs.open('/path/to/file.txt', FileOpenMode.Read);
   *
   * // Read line by line
   * while (!handle.isEOF()) {
   *   const line = await handle.readLine();
   *   console.log(line);
   * }
   *
   * handle.close();
   * ```
   */
  open(path: string, mode: FileOpenMode = FileOpenMode.Read): FileHandle {
    return new FileHandle(this.fs, path, mode);
  }

  /**
   * Release the underlying IOFileSystem instance.
   *
   * After calling close(), any File or Directory instances created from
   * this FSContext will throw an error when performing operations. Work that
   * was already submitted is allowed to finish. Native pool destruction and
   * file-handle cleanup happen in the background, so close() does not wait for
   * slow storage operations.
   *
   * Calling close() more than once is harmless. Garbage collection remains a
   * fallback when explicit cleanup is omitted.
   */
  close(): void {
    const native = this.state.native;
    if (!native) return;
    try {
      native.dispose();
    } finally {
      this.state.native = null;
      this._fs = null;
    }
  }
}

/**
 * Open a file system context with a shared thread pool.
 *
 * This is the preferred way to create an FSContext for batch file operations.
 * All File and Directory instances created from this context will share the
 * same native thread pool.
 *
 * @param numThreads Number of worker threads for async operations.
 *   When you need better concurrency for parallel file operations
 *   (e.g., reading/writing multiple files simultaneously with Promise.all),
 *   increase this value to enable true parallelism in the native layer.
 *   Default is 1, which is sufficient for sequential operations.
 *
 * @returns FSContext instance
 *
 * @example
 * ```typescript
 * // Default single-threaded - good for sequential operations
 * const fs = openFS();
 * const file = fs.file('/path/to/file.txt');
 * const content = await file.readString();
 * fs.close();
 * ```
 *
 * @example
 * ```typescript
 * // Multi-threaded for parallel file operations
 * const fs = openFS(4);
 * const files = paths.map(p => fs.file(p));
 * const contents = await Promise.all(files.map(f => f.readString()));
 * ```
 */
export function openFS(numThreads?: number): FSContext {
  return new FSContext(numThreads);
}
