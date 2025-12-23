/**
 * @file Directory.ts
 * @description Directory class for React Native IO
 *
 * Represents a directory on the filesystem with async and sync operations.
 * **Async methods are recommended** to avoid blocking the JS thread.
 */

import { File } from './File';
import {
  EntityType,
  type DirectoryEntry,
  type FileMetadata,
  type IOFileSystem,
} from './types';
import { createFileSystem } from './NativeStdIO';

/**
 * Represents a directory on the filesystem.
 *
 * Provides methods to create, list, and manage directories.
 * Each Directory instance lazily creates its own native FileSystem client,
 * which will be automatically released by garbage collection when
 * the Directory object becomes unreachable.
 *
 * **Recommendation**: Use async methods (no suffix) for I/O operations.
 * Sync methods block the JS thread and should only be used when necessary.
 *
 * @example
 * ```typescript
 * const dir = new Directory('/path/to/directory');
 *
 * // Getters (pure string computation - no I/O)
 * console.log(dir.name, dir.parent);
 *
 * // Async operations (recommended)
 * if (await dir.exists()) {
 *   const entries = await dir.list();
 * }
 *
 * // Optional: explicitly release native resources immediately
 * // (not required - GC will handle this automatically)
 * dir.dispose();
 * ```
 */
export class Directory {
  private readonly _path: string;
  private _fs: IOFileSystem | null = null;
  private readonly _sharedFs: boolean;

  /**
   * Create a Directory instance
   * @param path Directory path (absolute or relative)
   * @param fs Optional shared IOFileSystem instance. If provided, this Directory
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
   * Always uses a single-threaded pool for individual Directory operations.
   */
  private fs(): IOFileSystem {
    if (!this._fs) {
      this._fs = createFileSystem(1);
    }
    return this._fs;
  }

  /**
   * Release the underlying IOFileSystem instance.
   *
   * **Note**: This method is NOT required to be called.
   * When the Directory object becomes unreachable, JavaScript's garbage collector
   * will automatically release the native IOFileSystem and its thread pool.
   *
   * If this Directory was created with a shared IOFileSystem (passed via constructor),
   * calling dispose() will NOT release it - the shared instance is managed
   * by its owner (e.g., a FileSystem context).
   *
   * However, calling `dispose()` explicitly on a Directory with its own IOFileSystem
   * allows you to release native resources immediately, which can be useful when:
   * - You want deterministic resource cleanup
   * - You're done with directory operations and want to free the thread pool sooner
   * - Memory pressure is a concern
   */
  dispose(): void {
    // Only release if we own the fs (not shared)
    if (!this._sharedFs) {
      this._fs = null;
    }
  }

  // ==========================================================================
  // Getters (Pure computation - no I/O)
  // ==========================================================================

  /** Full directory path */
  get path(): string {
    return this._path;
  }

  /** Directory name (extracted from path) */
  get name(): string {
    return this.fs().getFileName(this._path);
  }

  /** Parent directory path */
  get parentPath(): string {
    return this.fs().getParentPath(this._path);
  }

  /** Parent directory instance */
  get parent(): Directory {
    return new Directory(this.parentPath);
  }

  // ==========================================================================
  // Query Operations (Async - recommended)
  // ==========================================================================

  /** Check if directory exists */
  exists(): Promise<boolean> {
    return this.fs().exists(this._path);
  }

  /** Check if this is a directory */
  isDirectory(): Promise<boolean> {
    return this.fs().isDirectory(this._path);
  }

  /** Get directory metadata */
  metadata(): Promise<FileMetadata> {
    return this.fs().getMetadata(this._path);
  }

  /** Get absolute path (resolves symlinks) */
  absolutePath(): Promise<string> {
    return this.fs().getAbsolutePath(this._path);
  }

  // ==========================================================================
  // Query Operations (Sync)
  // ==========================================================================

  /**
   * Check if directory exists (sync)
   * @warning Blocks JS thread. Prefer `exists()` async version.
   */
  existsSync(): boolean {
    return this.fs().existsSync(this._path);
  }

  /**
   * Check if this is a directory (sync)
   * @warning Blocks JS thread. Prefer `isDirectory()` async version.
   */
  isDirectorySync(): boolean {
    return this.fs().isDirectorySync(this._path);
  }

  /**
   * Get directory metadata (sync)
   * @warning Blocks JS thread. Prefer `metadata()` async version.
   */
  metadataSync(): FileMetadata {
    return this.fs().getMetadataSync(this._path);
  }

  /**
   * Get absolute path (sync)
   * @warning Blocks JS thread. Prefer `absolutePath()` async version.
   */
  absolutePathSync(): string {
    return this.fs().getAbsolutePathSync(this._path);
  }

  // ==========================================================================
  // Directory Operations (Async - recommended)
  // ==========================================================================

  /**
   * Create the directory
   * @param recursive Create parent directories if needed
   */
  create(recursive: boolean = true): Promise<Directory> {
    return this.fs()
      .createDirectory(this._path, recursive)
      .then(() => this);
  }

  /**
   * Delete the directory
   * @param recursive Delete contents recursively
   * @returns Number of items deleted
   */
  delete(recursive: boolean = false): Promise<number> {
    return this.fs().deleteDirectory(this._path, recursive);
  }

  /**
   * Move/rename directory
   * @param destinationPath Destination path
   * @returns New Directory instance at new location
   */
  move(destinationPath: string): Promise<Directory> {
    const fs = this._sharedFs ? this._fs! : undefined;
    return this.fs()
      .moveDirectory(this._path, destinationPath)
      .then(() => new Directory(destinationPath, fs));
  }

  /**
   * List directory contents
   * @param recursive List recursively
   * @returns Array of directory entries
   */
  list(recursive: boolean = false): Promise<DirectoryEntry[]> {
    return this.fs().listDirectory(this._path, recursive);
  }

  /**
   * List only files in directory
   * @param recursive List recursively
   */
  listFiles(recursive: boolean = false): Promise<File[]> {
    const fs = this._sharedFs ? this._fs! : undefined;
    return this.list(recursive).then((entries) =>
      entries
        .filter((e) => e.type === EntityType.File)
        .map((e) => new File(e.path, fs))
    );
  }

  /**
   * List only subdirectories
   * @param recursive List recursively
   */
  listDirectories(recursive: boolean = false): Promise<Directory[]> {
    const fs = this._sharedFs ? this._fs! : undefined;
    return this.list(recursive).then((entries) =>
      entries
        .filter((e) => e.type === EntityType.Directory)
        .map((e) => new Directory(e.path, fs))
    );
  }

  /**
   * List only file paths
   * @param recursive List recursively
   */
  listFilePaths(recursive: boolean = false): Promise<string[]> {
    return this.list(recursive).then((entries) =>
      entries.filter((e) => e.type === EntityType.File).map((e) => e.path)
    );
  }

  /**
   * Check if directory is empty
   */
  isEmpty(): Promise<boolean> {
    return this.list().then((entries) => entries.length === 0);
  }

  /**
   * Get total size of all files in directory
   * @param recursive Include subdirectories
   */
  getTotalSize(recursive: boolean = true): Promise<number> {
    return this.list(recursive).then((entries) =>
      entries
        .filter((e) => e.type === EntityType.File)
        .reduce((sum, e) => sum + e.size, 0)
    );
  }

  // ==========================================================================
  // Directory Operations (Sync)
  // ==========================================================================

  /**
   * Create the directory (sync)
   * @param recursive Create parent directories if needed
   * @warning Blocks JS thread. Prefer `create()` async version.
   */
  createSync(recursive: boolean = true): void {
    this.fs().createDirectorySync(this._path, recursive);
  }

  /**
   * Delete the directory (sync)
   * @param recursive Delete contents recursively
   * @returns Number of items deleted
   * @warning Blocks JS thread. Prefer `delete()` async version.
   */
  deleteSync(recursive: boolean = false): number {
    return this.fs().deleteDirectorySync(this._path, recursive);
  }

  /**
   * Move/rename directory (sync)
   * @param destinationPath Destination path
   * @returns New Directory instance at new location
   * @warning Blocks JS thread. Prefer `move()` async version.
   */
  moveSync(destinationPath: string): Directory {
    const fs = this._sharedFs ? this._fs! : undefined;
    this.fs().moveDirectorySync(this._path, destinationPath);
    return new Directory(destinationPath, fs);
  }

  /**
   * List directory contents (sync)
   * @param recursive List recursively
   * @returns Array of directory entries
   * @warning Blocks JS thread. Prefer `list()` async version.
   */
  listSync(recursive: boolean = false): DirectoryEntry[] {
    return this.fs().listDirectorySync(this._path, recursive);
  }

  /**
   * List only files in directory (sync)
   * @param recursive List recursively
   * @warning Blocks JS thread. Prefer `listFiles()` async version.
   */
  listFilesSync(recursive: boolean = false): File[] {
    const fs = this._sharedFs ? this._fs! : undefined;
    return this.listSync(recursive)
      .filter((e) => e.type === EntityType.File)
      .map((e) => new File(e.path, fs));
  }

  /**
   * List only subdirectories (sync)
   * @param recursive List recursively
   * @warning Blocks JS thread. Prefer `listDirectories()` async version.
   */
  listDirectoriesSync(recursive: boolean = false): Directory[] {
    const fs = this._sharedFs ? this._fs! : undefined;
    return this.listSync(recursive)
      .filter((e) => e.type === EntityType.Directory)
      .map((e) => new Directory(e.path, fs));
  }

  /**
   * Check if directory is empty (sync)
   * @warning Blocks JS thread. Prefer `isEmpty()` async version.
   */
  isEmptySync(): boolean {
    return this.listSync().length === 0;
  }

  /**
   * Get total size of all files in directory (sync)
   * @param recursive Include subdirectories
   * @warning Blocks JS thread. Prefer `getTotalSize()` async version.
   */
  getTotalSizeSync(recursive: boolean = true): number {
    return this.listSync(recursive)
      .filter((e) => e.type === EntityType.File)
      .reduce((sum, e) => sum + e.size, 0);
  }

  // ==========================================================================
  // Child Access
  // ==========================================================================

  /**
   * Get a file in this directory
   * @param name File name
   */
  file(name: string): File {
    const fs = this._sharedFs ? this._fs! : undefined;
    return new File(this.fs().joinPaths(this._path, name), fs);
  }

  /**
   * Get a subdirectory
   * @param name Subdirectory name
   */
  directory(name: string): Directory {
    const fs = this._sharedFs ? this._fs! : undefined;
    return new Directory(this.fs().joinPaths(this._path, name), fs);
  }

  /**
   * Get child path
   * @param relativePath Relative path
   */
  childPath(relativePath: string): string {
    return this.fs().joinPaths(this._path, relativePath);
  }

  // ==========================================================================
  // Utility
  // ==========================================================================

  /** String representation */
  toString(): string {
    return this._path;
  }

  /** JSON representation */
  toJSON(): { path: string; name: string } {
    return {
      path: this._path,
      name: this.name,
    };
  }
}
