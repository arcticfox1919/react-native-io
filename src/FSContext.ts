/**
 * @file FSContext.ts
 * @description File system context for React Native IO
 *
 * Provides a shared context for File and Directory operations,
 * allowing multiple operations to share a single thread pool.
 */

import { File } from './File';
import { Directory } from './Directory';
import { createFileSystem } from './NativeStdIO';
import type { IOFileSystem } from './types';

/**
 * File system context for batch file operations.
 *
 * Creates a shared native FileSystem instance with a thread pool that can be
 * reused across multiple File and Directory operations. This is more efficient
 * than creating individual File/Directory instances when performing many
 * operations, as they all share the same thread pool.
 *
 * The native resources will be automatically released by garbage collection
 * when the FSContext object becomes unreachable.
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
 * // Optional: explicitly release when done
 * // (not required - GC will handle this automatically)
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

  /**
   * Create a FSContext with a shared thread pool.
   * Prefer using `openFS()` function instead of direct constructor.
   *
   * @param numThreads Number of worker threads for async operations.
   *   - Default: 1 (single-threaded pool)
   *   - A single thread is sufficient for most use cases
   *   - Increase for better parallelism in concurrent file operations
   */
  constructor(numThreads?: number) {
    this._fs = createFileSystem(numThreads);
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
   * Release the underlying IOFileSystem instance.
   *
   * **Note**: This method is NOT required to be called.
   * When the FSContext object becomes unreachable, JavaScript's garbage
   * collector will automatically release the native resources.
   *
   * After calling close(), any File or Directory instances created from
   * this FSContext will throw an error when performing operations.
   */
  close(): void {
    this._fs = null;
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
 * fs.close(); // optional
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
