# react-native-io

<div align="center">

**Blazing-fast I/O library for React Native**

Pure C++ TurboModule | Built on C++20 Standard Library | Maximum Performance

[![npm version](https://img.shields.io/npm/v/react-native-io.svg)](https://www.npmjs.com/package/react-native-io)
[![license](https://img.shields.io/npm/l/react-native-io.svg)](https://github.com/arcticfox1919/react-native-io/blob/main/LICENSE)

</div>

---

> ⚠️ **New Architecture Only**: This library requires React Native **New Architecture** (TurboModules + JSI). It has been tested and verified on **React Native 0.76** and **0.83**. The old architecture (Bridge) is **NOT supported**.

---

## Features

| Category | Features |
|----------|----------|
| **File Operations** | Read/Write (text & binary), Copy, Move, Rename, Delete, Exists check, Size, Metadata |
| **Directory Operations** | Create (recursive), List contents, List files only, List directories only, Rename, Delete (recursive) |
| **FileHandle (Streaming)** | Open/Close, Seek, Read at position, Write at position, Truncate, Flush, Lock/Unlock |
| **HTTP Client** | GET/POST/PUT/DELETE/PATCH, Custom headers, Timeout, Binary & text responses |
| **Hash (File)** | MD5, SHA1, SHA256, SHA3, Keccak, CRC32 (via `file.calcHash()`) |
| **Platform Directories** | cacheDir, documentDir, tempDir, libraryDir (iOS), bundleDir (iOS), externalFilesDir (Android) |
| **Performance** | Sync & Async APIs, Configurable thread pool, Pure C++ implementation, Zero JS bridge overhead |

## Why react-native-io?

**react-native-io** is a **pure C++ TurboModule** with all file I/O operations built entirely on the **C++20 standard library**, delivering exceptional performance:

-  **Pure C++ Implementation**: Zero JS overhead, direct native calls via JSI
-  **Dart-Style API**: Intuitive API design inspired by `dart:io`
-  **Dual Mode Support**: Both sync and async APIs for different use cases
-  **Thread Pool Optimization**: Configurable thread pools for high-concurrency async operations
-  **Feature Complete**: Files, directories, HTTP requests, and hashing in one package
-  **FileHandle Streaming**: High-performance file handles for efficient I/O without reopening files

##  Installation

```sh
npm install react-native-io
# or
yarn add react-native-io
```

## Quick Start

### Recommended: Use `openFS()` for Most Scenarios

When you need to operate on **multiple files or directories**, use `openFS()` to create an `FSContext`:

- ✅ Reuses the native thread pool across all File and Directory instances
- ✅ Better performance when working with multiple files/directories  
- ✅ Unified management of File and Directory instances
- ✅ Configurable concurrency with `openFS(threadCount)`

> 💡 **Use standalone `new File()` or `new Directory()` when working with a single file or directory.** Each instance has its own thread pool, so all operations on the same instance share resources efficiently. For example, `new File(path).writeString(content)` is perfectly fine and concise for single-file operations.

```typescript
import { openFS, FS, EntityType } from 'react-native-io';

// ============================================================================
// Step 1: Create FSContext - your entry point for all file system operations
// ============================================================================
// openFS() creates a file system context with a reusable native thread pool.
// Parameter: number of threads (default: 1, increase for heavy concurrent operations)
const fs = openFS();        // Single thread - good for most cases
// const fs = openFS(4);    // 4 threads - for heavy parallel I/O

// ============================================================================
// Step 2: Get platform-specific directories
// ============================================================================
// FS.cacheDir     - For temporary files, may be cleared by system
// FS.documentDir  - For persistent user data, backed up on iOS
// FS.tempDir      - System temp directory
const cacheDir = FS.cacheDir;
const docsDir = FS.documentDir;
console.log('Cache directory:', cacheDir);   // e.g., /data/data/com.app/cache
console.log('Documents directory:', docsDir); // e.g., /data/data/com.app/files

// ============================================================================
// Step 3: Create File and Directory instances from FSContext
// ============================================================================
// IMPORTANT: Always use fs.file() and fs.directory() instead of new File()/Directory()
// This ensures all operations share the same thread pool!

// Create a directory instance for our app's data folder
const appDataDir = fs.directory(`${docsDir}/myAppData`);

// Create a file instance for our config file (inside the data folder)
const configFile = fs.file(`${docsDir}/myAppData/config.json`);

// ============================================================================
// Step 4: Directory operations with detailed explanations
// ============================================================================

// Check if directory exists (returns boolean)
const dirExists = await appDataDir.exists();
console.log('Directory exists?', dirExists);  // false on first run

if (!dirExists) {
  // create(recursive: boolean) - Create the directory
  // - recursive=true:  Creates all parent directories if they don't exist (like mkdir -p)
  // - recursive=false: Only creates if parent directory exists, throws error otherwise
  await appDataDir.create(true);  // Creates /docsDir/myAppData/ and any missing parents
  console.log('Created directory:', appDataDir.path);
}

// ============================================================================
// Step 5: File operations with detailed explanations
// ============================================================================

// Check if file exists
const fileExists = await configFile.exists();

if (fileExists) {
  // File exists - read its content
  // readString() reads the entire file as UTF-8 text
  const jsonString = await configFile.readString();
  const config = JSON.parse(jsonString);
  console.log('Loaded config:', config);
} else {
  // File doesn't exist - create it with default content
  const defaultConfig = {
    theme: 'dark',
    language: 'en',
    version: 1
  };
  // writeString(content) writes text to file, creating it if doesn't exist
  // Overwrites any existing content by default
  await configFile.writeString(JSON.stringify(defaultConfig, null, 2));
  console.log('Created default config at:', configFile.path);
}

// ============================================================================
// Step 6: List directory contents
// ============================================================================

// list() returns all entries (files and directories) in the directory
const entries = await appDataDir.list();
console.log(`Found ${entries.length} items in ${appDataDir.name}:`);

for (const entry of entries) {
  // Each entry has: name, path, type (EntityType.File or EntityType.Directory)
  const typeLabel = entry.type === EntityType.File ? 'File' : 'Directory';
  console.log(`  ${typeLabel}: ${entry.name}`);
  console.log(`    Full path: ${entry.path}`);
}

// Get only files (excludes subdirectories)
const filesOnly = await appDataDir.listFiles();
console.log('Files:', filesOnly.map(f => f.name));

// Get only subdirectories (excludes files)
const dirsOnly = await appDataDir.listDirectories();
console.log('Subdirectories:', dirsOnly.map(d => d.name));

// ============================================================================
// Step 7: More file operations
// ============================================================================

// Get file metadata
const size = await configFile.size();  // File size in bytes
console.log('Config file size:', size, 'bytes');

// File path properties (these are pure string operations, no I/O)
console.log('File name:', configFile.name);                    // 'config.json'
console.log('Extension:', configFile.extension);               // '.json'
console.log('Name without ext:', configFile.nameWithoutExtension); // 'config'
console.log('Parent directory:', configFile.parent);           // '/path/to/myAppData'

// ============================================================================
// Resource cleanup (usually NOT needed - GC handles it automatically)
// ============================================================================
// FSContext is automatically released when garbage collected.
// Only call close() if you need immediate resource release:
// fs.close();
```

### Understanding Standalone `File` and `Directory`

Before diving into `FSContext`, let's understand how standalone `File` and `Directory` work. This knowledge helps you understand why `FSContext` is more efficient for complex scenarios.

#### Standalone `File` - Complete Example

```typescript
import { File, FS, WriteMode } from 'react-native-io';

// ============================================================================
// Creating a File instance
// ============================================================================
// new File(path) creates a File object representing a file at the given path
// NOTE: This does NOT create the file on disk! It's just a reference.
const file = new File(`${FS.documentDir}/standalone-example.txt`);

// ============================================================================
// File path properties (pure string operations - no I/O)
// ============================================================================
console.log('Full path:', file.path);                  // '/data/.../standalone-example.txt'
console.log('File name:', file.name);                  // 'standalone-example.txt'
console.log('Extension:', file.extension);             // '.txt'
console.log('Name without extension:', file.nameWithoutExtension);  // 'standalone-example'
console.log('Parent directory:', file.parent);         // '/data/.../files'

// ============================================================================
// Checking file existence and metadata
// ============================================================================
const exists = await file.exists();       // Returns true if file exists
const isFile = await file.isFile();       // Returns true if path is a file (not directory)
console.log('File exists?', exists);
console.log('Is a file?', isFile);

if (exists) {
  const size = await file.size();         // File size in bytes
  console.log('File size:', size, 'bytes');
}

// ============================================================================
// Writing to files
// ============================================================================

// writeString(content, mode?) - Write text content
// mode options:
//   WriteMode.Overwrite (default) - Replace entire file content
//   WriteMode.Append - Add content to end of file
await file.writeString('Hello, World!');                          // Overwrite mode
await file.writeString('\nAppended line', WriteMode.Append);      // Append mode

// writeBytes(data, mode?) - Write binary data (Uint8Array)
const binaryData = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]); // "Hello" in ASCII
await file.writeBytes(binaryData);

// ============================================================================
// Reading from files
// ============================================================================

// readString() - Read entire file as UTF-8 text
const textContent = await file.readString();
console.log('File content:', textContent);

// readBytes() - Read entire file as binary data (Uint8Array)
const bytes = await file.readBytes();
console.log('Bytes length:', bytes.length);

// ============================================================================
// File operations: copy, move, rename, delete
// ============================================================================

// copy(destinationPath) - Copy file to new location
// Original file remains unchanged, new file created at destination
const copyPath = `${FS.cacheDir}/copied-file.txt`;
await file.copy(copyPath);
console.log('Copied to:', copyPath);

// move(destinationPath) - Move file to new location  
// File is removed from original path, created at destination
// After move, the File object's path is automatically updated!
const movePath = `${FS.cacheDir}/moved-file.txt`;
await file.move(movePath);
console.log('Moved to:', file.path);  // Now shows the new path

// rename(newName) - Rename file (stays in same directory)
// Only changes the filename, not the directory
await file.rename('renamed-file.txt');
console.log('Renamed to:', file.name);  // 'renamed-file.txt'

// delete() - Permanently delete the file
// ⚠️ This cannot be undone!
await file.delete();
console.log('File deleted');

// ============================================================================
// Sync versions (block the JS thread - use only for small/fast operations)
// ============================================================================
const file2 = new File(`${FS.cacheDir}/sync-example.txt`);

file2.writeStringSync('Quick write');       // Sync write
const content = file2.readStringSync();     // Sync read
const existsSync = file2.existsSync();      // Sync exists check
const sizeSync = file2.sizeSync();          // Sync size check
file2.deleteSync();                         // Sync delete
```

#### Standalone `Directory` - Complete Example

```typescript
import { Directory, FS, EntityType } from 'react-native-io';

// ============================================================================
// Creating a Directory instance
// ============================================================================
// new Directory(path) creates a Directory object representing a folder
// NOTE: This does NOT create the directory on disk! It's just a reference.
const dir = new Directory(`${FS.cacheDir}/standalone-dir-example`);

// ============================================================================
// Directory path properties (pure string operations - no I/O)
// ============================================================================
console.log('Full path:', dir.path);        // '/data/.../standalone-dir-example'
console.log('Directory name:', dir.name);   // 'standalone-dir-example'
console.log('Parent path:', dir.parentPath); // '/data/.../cache'

// Get parent as a Directory instance
const parentDir = dir.parent;               // Directory object for parent folder

// ============================================================================
// Creating directories
// ============================================================================

// create(recursive) - Create the directory on disk
// recursive=true:  Creates ALL missing parent directories (like mkdir -p)
// recursive=false: Only creates if parent exists, throws error otherwise
await dir.create(true);   // ✅ Safe - creates parents if needed
console.log('Directory created at:', dir.path);

// ============================================================================
// Checking directory status
// ============================================================================
const exists = await dir.exists();           // Does this path exist?
const isDir = await dir.isDirectory();       // Is it actually a directory?
console.log('Exists?', exists, 'Is directory?', isDir);

// ============================================================================
// Listing directory contents
// ============================================================================

// First, let's create some files in the directory for demonstration
const file1 = new File(`${dir.path}/document.txt`);
const file2 = new File(`${dir.path}/image.png`);
const subDir = new Directory(`${dir.path}/subFolder`);

await file1.writeString('Document content');
await file2.writeBytes(new Uint8Array([0x89, 0x50, 0x4E, 0x47])); // PNG header
await subDir.create(true);

// list() - Get ALL entries (files + subdirectories)
const allEntries = await dir.list();
console.log(`\nDirectory contains ${allEntries.length} items:`);
for (const entry of allEntries) {
  // entry.type is EntityType.File or EntityType.Directory
  const icon = entry.type === EntityType.File ? '📄' : '📁';
  console.log(`  ${icon} ${entry.name}`);
}

// listFiles() - Get ONLY files
const files = await dir.listFiles();
console.log('\nFiles only:', files.map(f => f.name));  // ['document.txt', 'image.png']

// listDirectories() - Get ONLY subdirectories
const subdirs = await dir.listDirectories();
console.log('Folders only:', subdirs.map(d => d.name));  // ['subFolder']

// ============================================================================
// Renaming directories
// ============================================================================
// rename(newName) - Rename directory (stays in same parent folder)
await dir.rename('renamed-directory');
console.log('Renamed to:', dir.name);  // 'renamed-directory'

// ============================================================================
// Deleting directories
// ============================================================================
// delete(recursive) - Delete the directory
// recursive=true:  ⚠️ DANGER! Deletes directory AND ALL contents (rm -rf)
// recursive=false: Only deletes if empty, throws error if not empty

// Clean up our example
// await dir.delete(false);  // Fails - directory is not empty
await dir.delete(true);      // ⚠️ Deletes directory and ALL files inside
console.log('Directory deleted (including all contents)');

// ============================================================================
// Sync versions
// ============================================================================
const dir2 = new Directory(`${FS.cacheDir}/sync-dir-example`);

dir2.createSync(true);                  // Sync create
const existsSync = dir2.existsSync();   // Sync exists check
const entriesSync = dir2.listSync();    // Sync list
dir2.deleteSync(true);                  // Sync delete
```

#### When to Use `FSContext` vs Standalone Classes

Now that you've seen standalone usage, here's when to use each approach:

```typescript
// ✅ Standalone - Perfect for single file/directory operations
// Each File/Directory instance has its own thread pool, and all operations
// on the SAME instance share that pool efficiently.
const file = new File('/path/to/config.json');
await file.writeString('{"key": "value"}');   // Uses file's thread pool
const content = await file.readString();       // Reuses same thread pool
// Simple, concise, and efficient for single-file scenarios!

// ❌ Problem: Multiple standalone instances = multiple thread pools
const file1 = new File('/path/file1.txt');  // Creates thread pool #1
const file2 = new File('/path/file2.txt');  // Creates thread pool #2
const dir = new Directory('/path/dir');      // Creates thread pool #3
// 3 separate thread pools = wasted resources!

// ✅ FSContext - Best for multiple files/directories
const fs = openFS();                         // Creates ONE shared thread pool
const file1 = fs.file('/path/file1.txt');   // Uses shared pool
const file2 = fs.file('/path/file2.txt');   // Uses shared pool
const dir = fs.directory('/path/dir');       // Uses shared pool
// All instances efficiently share the same thread pool!
```

> 💡 **Summary**: 
> - **Single file/directory**: Use `new File()` / `new Directory()` - simple and efficient
> - **Multiple files/directories**: Use `openFS()` - shared thread pool saves resources

### Directory Operations Reference

```typescript
import { openFS, FS, EntityType } from 'react-native-io';

// Always use openFS() for real applications
const fs = openFS();

// Create directory instance from a path
const dir = fs.directory(`${FS.cacheDir}/my-app/data`);

// ============================================================================
// Creating directories
// ============================================================================

// create(recursive: boolean) - Create directory
// 
// When recursive = true (RECOMMENDED):
//   Creates the directory AND all missing parent directories
//   Example: If /cache/my-app/ doesn't exist, it creates both /cache/my-app/ and /cache/my-app/data/
//   Similar to: mkdir -p /cache/my-app/data
//
// When recursive = false:
//   Only creates the final directory
//   Throws error if parent directory doesn't exist
//   Similar to: mkdir /cache/my-app/data (fails if /cache/my-app/ missing)

await dir.create(true);   // ✅ Safe - creates all parent folders if needed
// await dir.create(false); // ⚠️ Risky - fails if parent doesn't exist

// ============================================================================
// Checking directory status
// ============================================================================

const exists = await dir.exists();           // Does this path exist?
const isDir = await dir.isDirectory();       // Is it actually a directory (not a file)?
console.log('Exists:', exists, 'Is directory:', isDir);

// ============================================================================
// Listing directory contents
// ============================================================================

// list() - Get ALL entries (both files and subdirectories)
const allEntries = await dir.list();
console.log(`Total entries: ${allEntries.length}`);

// Each entry contains:
// - name: string       (e.g., 'photo.jpg')
// - path: string       (e.g., '/cache/my-app/data/photo.jpg')  
// - type: EntityType   (EntityType.File or EntityType.Directory)

for (const entry of allEntries) {
  if (entry.type === EntityType.File) {
    console.log(`📄 File: ${entry.name} at ${entry.path}`);
  } else if (entry.type === EntityType.Directory) {
    console.log(`📁 Folder: ${entry.name} at ${entry.path}`);
  }
}

// listFiles() - Get ONLY files (no subdirectories)
const filesOnly = await dir.listFiles();
console.log('Files:', filesOnly.map(f => f.name).join(', '));

// listDirectories() - Get ONLY subdirectories (no files)
const foldersOnly = await dir.listDirectories();
console.log('Subdirectories:', foldersOnly.map(d => d.name).join(', '));

// ============================================================================
// Directory path properties (no I/O - pure string operations)
// ============================================================================

console.log('Full path:', dir.path);        // '/cache/my-app/data'
console.log('Directory name:', dir.name);   // 'data'
console.log('Parent path:', dir.parentPath); // '/cache/my-app'

// Get parent as Directory instance (for chained operations)
const parentDir = dir.parent;  // Directory instance for '/cache/my-app'

// ============================================================================
// Renaming directories
// ============================================================================

// rename(newName) - Rename directory (keeps it in same parent folder)
// Example: '/cache/my-app/data' → '/cache/my-app/archived-data'
await dir.rename('archived-data');
console.log('New path:', dir.path);  // Path is updated automatically

// ============================================================================
// Deleting directories - USE WITH CAUTION!
// ============================================================================

// delete(recursive: boolean) - Delete directory
//
// When recursive = true:
//   ⚠️ DANGEROUS! Deletes the directory AND ALL contents inside (files + subdirectories)
//   Similar to: rm -rf /path/to/dir
//   Use with extreme caution - data cannot be recovered!
//
// When recursive = false:
//   Only deletes if directory is EMPTY
//   Throws error if directory contains any files or subdirectories
//   Similar to: rmdir /path/to/dir

// await dir.delete(false);  // Safe - only works if directory is empty
// await dir.delete(true);   // ⚠️ DANGER - deletes everything inside!

// ============================================================================
// Sync versions (for simple, fast operations only)
// ============================================================================

// All methods have sync versions - use only for small/fast operations
dir.createSync(true);              // Synchronous create
const existsSync = dir.existsSync(); // Synchronous exists check
const entriesSync = dir.listSync(); // Synchronous listing
// dir.deleteSync(true);           // Synchronous delete
```

### Platform-Specific Directories

```typescript
import { openFS, FS, IOPlatformType } from 'react-native-io';

const fs = openFS();

// ============================================================================
// Platform detection
// ============================================================================

console.log('Current platform:', FS.platform);  // 'ios' or 'android'

// ============================================================================
// Cross-platform directories (available on both iOS and Android)
// ============================================================================

// cacheDir - For temporary cached data
// - May be cleared by system when storage is low
// - NOT backed up to iCloud/Google
// - iOS: <App>/Library/Caches
// - Android: /data/data/<package>/cache
console.log('Cache:', FS.cacheDir);

// documentDir - For user documents and persistent data  
// - Persists until app is uninstalled
// - iOS: <App>/Documents (BACKED UP to iCloud!)
// - Android: /data/data/<package>/files
console.log('Documents:', FS.documentDir);

// tempDir - For truly temporary files
// - May be deleted at any time by system
// - iOS: <App>/tmp
// - Android: /data/data/<package>/cache (same as cacheDir)
console.log('Temp:', FS.tempDir);

// ============================================================================
// iOS-specific directories
// ============================================================================

if (FS.platform === IOPlatformType.iOS) {
  // libraryDir - For app support files (not user-facing)
  // - Backed up to iCloud (except Caches subfolder)
  // - <App>/Library
  console.log('Library:', FS.libraryDir);
  
  // bundleDir - Read-only app bundle resources
  // - Contains your app's compiled code and bundled assets
  // - CANNOT write to this directory!
  console.log('Bundle (read-only):', FS.bundleDir);
}

// ============================================================================
// Android-specific directories and info
// ============================================================================

if (FS.platform === IOPlatformType.Android) {
  // externalFilesDir - External storage (if available)
  // - Visible to user via file managers
  // - Deleted when app is uninstalled
  // - /storage/emulated/0/Android/data/<package>/files
  console.log('External files:', FS.externalFilesDir);
  
  // sdkVersion - Android API level (useful for compatibility checks)
  console.log('Android SDK version:', FS.sdkVersion);  // e.g., 33 for Android 13
}

// ============================================================================
// Using platform directories with FSContext
// ============================================================================

// Create files in appropriate directories
const configFile = fs.file(`${FS.documentDir}/config.json`);  // Persistent config
const cacheFile = fs.file(`${FS.cacheDir}/temp_data.bin`);    // Temporary cache
const logsDir = fs.directory(`${FS.cacheDir}/logs`);          // Log files
```

### Path Utilities & Storage Info

```typescript
import { FS } from 'react-native-io';

// ============================================================================
// FS.joinPaths(...segments) - Safely join path segments
// ============================================================================
// Use this instead of manual string concatenation to avoid path separator issues.
// Handles trailing/leading slashes automatically.

const filePath = FS.joinPaths(FS.documentDir, 'subfolder', 'data.json');
// Result: '/data/data/com.app/files/subfolder/data.json'

// Works with multiple segments
const deepPath = FS.joinPaths(FS.cacheDir, 'level1', 'level2', 'level3', 'file.txt');
// Result: '/data/data/com.app/cache/level1/level2/level3/file.txt'

// Comparison with template string (both work, but joinPaths is safer):
const path1 = `${FS.documentDir}/subfolder/data.json`;     // Template string
const path2 = FS.joinPaths(FS.documentDir, 'subfolder', 'data.json');  // joinPaths
// Both produce the same result, but joinPaths handles edge cases better

// ============================================================================
// Storage space information
// ============================================================================

// Get available (free) storage space in bytes
const availableBytes = await FS.getAvailableSpace(FS.documentDir);
const availableGB = availableBytes / (1024 * 1024 * 1024);
console.log(`Available: ${availableGB.toFixed(2)} GB`);

// Get total storage space in bytes
const totalBytes = await FS.getTotalSpace(FS.documentDir);
const totalGB = totalBytes / (1024 * 1024 * 1024);
console.log(`Total: ${totalGB.toFixed(2)} GB`);

// Sync versions (for quick checks)
const availableSync = FS.getAvailableSpaceSync(FS.documentDir);
const totalSync = FS.getTotalSpaceSync(FS.documentDir);

// ============================================================================
// String encoding utilities
// ============================================================================
// Use these when you need to convert between strings and binary data.
// Native C++ implementation - faster than JavaScript's TextEncoder/TextDecoder.
//
// Supported encodings: 'utf8', 'utf16le', 'ascii', 'latin1'

import { openFS, FS } from 'react-native-io';

const fs = openFS();
const file = fs.file(`${FS.cacheDir}/binary-text.bin`);

// ============================================================================
// Example: Write string as binary data to file
// ============================================================================

const originalText = 'Hello, World! 你好世界 🚀';

// Step 1: Convert string to ArrayBuffer (binary data)
const buffer = FS.encodeString(originalText, 'utf8');
console.log('Encoded to', buffer.byteLength, 'bytes');

// Step 2: Write binary data to file
await file.writeBytes(new Uint8Array(buffer));
console.log('Written to file:', file.path);

// ============================================================================
// Example: Read binary data from file and convert to string
// ============================================================================

// Step 1: Read file as binary data
const readBytes = await file.readBytes();  // Returns Uint8Array
console.log('Read', readBytes.length, 'bytes from file');

// Step 2: Convert binary data back to string
const restoredText = FS.decodeString(readBytes.buffer, 'utf8');
console.log('Restored text:', restoredText);  // 'Hello, World! 你好世界 🚀'

// Verify they match
console.log('Match:', originalText === restoredText);  // true
```

## Sync vs Async: Performance Trade-offs

### When to Use Sync APIs?

For **operations under 10ms**, sync APIs are lighter and faster:

```typescript
import { openFS, FS } from 'react-native-io';

const fs = openFS();
const file = fs.file(`${FS.cacheDir}/small.txt`);

// Sync operations - ideal for simple, fast tasks
// These complete in microseconds and don't need thread pool overhead

const exists = file.existsSync();        // ✅ ~0.05ms - extremely fast
const size = file.sizeSync();            // ✅ ~0.05ms - just reads metadata
const name = file.name;                  // ✅ ~0.00ms - pure string, no I/O

if (exists) {
  // ⚠️ Only use sync read for SMALL files (< 10KB)
  // Larger files will block the JS thread and cause UI jank!
  const content = file.readStringSync();
}
```

**Advantages**:
- **Zero Async Overhead**: No thread pool creation, no scheduling, no Promise overhead
-  **Maximum Performance**: Simple operations like `existsSync()` take only ~0.05ms

### When to Use Async APIs?

For **large file I/O** or potentially time-consuming operations, **always use async APIs**:

```typescript
import { openFS, FS } from 'react-native-io';

const fs = openFS();
const largeFile = fs.file(`${FS.cacheDir}/large_video.mp4`);

// Async operations run on native thread pool - JS thread stays free
// This prevents frame drops and UI stuttering

const exists = await largeFile.exists();  // ⚠️ ~10ms (includes thread scheduling)
if (exists) {
  // ✅ Large file read happens on background thread
  // UI remains responsive during the read!
  const data = await largeFile.readBytes();
  
  // Process data...
  const processedData = processVideo(data);
  
  // ✅ Write also happens on background thread
  await largeFile.writeBytes(processedData);
}
```

**Advantages**:
- ✅ **Non-Blocking**: Prevents frame drops and UI stuttering
- ✅ **Concurrency Support**: Thread pool handles multiple operations simultaneously
- ✅ **Configurable Performance**: Increase threads with `openFS(4)`

**Disadvantages**:
- ⚠️ **Async Overhead**: Thread pool creation, scheduling, and JSI async calls can be **hundreds to thousands of times** slower than simple sync operations
- Example: `existsSync()` may take 0.05ms, while `exists()` could take 10ms

###  Best Practices Summary

| Scenario | Recommended API | Reason |
|----------|----------------|--------|
| Check file existence | `existsSync()` | Simple operation, sync is faster |
| Get file size (small files) | `sizeSync()` | Metadata read is fast |
| Read/write small text files (< 10KB) | Case-by-case | Measure and decide |
| Read/write large files (> 100KB) | `readBytes()` / `writeBytes()` | Prevent UI blocking |
| High-frequency concurrent ops | Async + `openFS(N)` | Leverage thread pool |
| Multi-step operations | Async + `FSContext` | Reuse thread pool |

⚠️ **Key Principle**: **Measure carefully and consider the trade-offs**. Performance varies by device, file size, and OS.

##  API Documentation

### File Operations

```typescript
import { openFS, FS, WriteMode, HashAlgorithm } from 'react-native-io';

// Always use openFS() for real applications
const fs = openFS();

// Create file instance - file doesn't need to exist yet
const file = fs.file(`${FS.documentDir}/example.txt`);

// ============================================================================
// Path properties (pure string operations - no disk I/O)
// ============================================================================
// These are instant and don't touch the filesystem

console.log(file.path);                    // Full path: '/data/.../files/example.txt'
console.log(file.name);                    // File name with extension: 'example.txt'
console.log(file.extension);               // Extension with dot: '.txt'
console.log(file.nameWithoutExtension);    // File name only: 'example'
console.log(file.parent);                  // Parent directory path: '/data/.../files'

// ============================================================================
// Checking file status (async)
// ============================================================================

const exists = await file.exists();     // Does file exist? → boolean
const isFile = await file.isFile();     // Is it a file (not directory)? → boolean
const size = await file.size();         // File size in bytes → number
const meta = await file.getMetadata();  // Full metadata object
// meta = { size: 1234, modifiedTime: 1703123456789, accessedTime: ..., createdTime: ... }

// ============================================================================
// Reading files
// ============================================================================

// readString(encoding?) - Read entire file as text
// Default encoding is UTF-8
const textContent = await file.readString();
console.log('File content:', textContent);

// readBytes() - Read entire file as binary data
// Returns Uint8Array - useful for images, audio, or any binary file
const binaryData = await file.readBytes();
console.log('Binary size:', binaryData.length, 'bytes');

// ============================================================================
// Writing files
// ============================================================================

// writeString(content, mode?) - Write text to file
//
// WriteMode.Overwrite (default):
//   Replaces entire file content. Creates file if doesn't exist.
//   Example: File has "Hello" → writeString("World") → File now has "World"
//
// WriteMode.Append:
//   Adds content to END of file. Creates file if doesn't exist.
//   Example: File has "Hello" → writeString(" World", Append) → File now has "Hello World"

await file.writeString('Hello, World!');                      // Overwrite (create or replace)
await file.writeString('\nNew line', WriteMode.Append);       // Append to existing content

// writeBytes(data, mode?) - Write binary data
// Same modes as writeString
const imageBytes = new Uint8Array([0xFF, 0xD8, 0xFF, 0xE0]); // JPEG header bytes
const imageFile = fs.file(`${FS.cacheDir}/image.jpg`);
await imageFile.writeBytes(imageBytes);

// ============================================================================
// Copying files
// ============================================================================

// copy(destinationPath) - Copy file to new location
// - Source file remains unchanged
// - Creates destination file (overwrites if exists)
// - Parent directory of destination must exist

const sourceFile = fs.file(`${FS.documentDir}/original.txt`);
await sourceFile.writeString('Original content');

// Copy from /documents/original.txt → /cache/backup.txt
await sourceFile.copy(`${FS.cacheDir}/backup.txt`);
// Now both files exist with same content

// ============================================================================
// Moving files
// ============================================================================

// move(destinationPath) - Move file to new location
// - Source file is DELETED after successful move
// - Creates destination file (overwrites if exists)
// - Parent directory of destination must exist
// - More efficient than copy+delete for same filesystem

const tempFile = fs.file(`${FS.cacheDir}/temp.txt`);
await tempFile.writeString('Temporary data');

// Move from /cache/temp.txt → /documents/permanent.txt
await tempFile.move(`${FS.documentDir}/permanent.txt`);
// tempFile.path is now updated to the new location
// Original /cache/temp.txt no longer exists

// ============================================================================
// Renaming files
// ============================================================================

// rename(newName) - Rename file (stays in same directory)
// - Only changes the filename, not the directory
// - Source file is renamed (not copied)

const docFile = fs.file(`${FS.documentDir}/old-name.txt`);
await docFile.writeString('Some content');

// Rename from old-name.txt → new-name.txt (same directory)
await docFile.rename('new-name.txt');
console.log('New path:', docFile.path);  // /documents/new-name.txt

// ============================================================================
// Deleting files
// ============================================================================

// delete() - Permanently delete the file
// - File is removed from filesystem
// - No confirmation, no trash bin - IRREVERSIBLE!
// - Throws error if file doesn't exist

const toDelete = fs.file(`${FS.cacheDir}/unwanted.txt`);
await toDelete.writeString('Delete me');
await toDelete.delete();  // File is gone forever

// ============================================================================
// Hash computation
// ============================================================================

// hash(algorithm) - Calculate cryptographic hash of file content
// Useful for: integrity verification, deduplication, checksums

const hashFile = fs.file(`${FS.documentDir}/data.bin`);

const sha256 = await hashFile.hash(HashAlgorithm.SHA256);  // Most common, secure
const md5 = await hashFile.hash(HashAlgorithm.MD5);        // Fast, but not secure
const crc32 = await hashFile.hash(HashAlgorithm.CRC32);    // Checksum, very fast

console.log('SHA256:', sha256);  // '2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c...'
console.log('MD5:', md5);        // '5d41402abc4b2a76b9719d911017c592'
console.log('CRC32:', crc32);    // 'a3830348'

// Available algorithms:
// HashAlgorithm.MD5, SHA1, SHA256 (recommended), SHA3_256, SHA3_512
// HashAlgorithm.Keccak256 (Ethereum compatible), CRC32

// ============================================================================
// Sync versions (use only for small/fast operations!)
// ============================================================================
// ⚠️ Warning: Sync methods block the JS thread. Only use for:
// - Very small files (< 10KB)
// - Simple operations like exists/size checks
// - Initialization code where async isn't available

const existsSync = file.existsSync();          // Sync exists check
const sizeSync = file.sizeSync();              // Sync size check
file.writeStringSync('Quick write');           // Sync write
const contentSync = file.readStringSync();     // Sync read
file.copySync(`${FS.cacheDir}/backup.txt`);    // Sync copy
file.deleteSync();                             // Sync delete
```

### Directory Operations

```typescript
import { Directory, EntityType } from 'react-native-io';

const dir = new Directory('/path/to/dir');

// Basic info (no I/O)
console.log(dir.path);                     // '/path/to/dir'
console.log(dir.name);                     // 'dir'
console.log(dir.parentPath);               // '/path/to'

// Async operations
await dir.create(true);                    // Create recursively
await dir.exists();                        // Check existence
await dir.isDirectory();                   // Check if directory

// List contents
const entries = await dir.list();          // All entries
const files = await dir.listFiles();       // Files only
const subdirs = await dir.listDirectories(); // Subdirectories only

// Iterate through entries
entries.forEach(entry => {
  console.log(entry.name, entry.path);
  if (entry.type === EntityType.File) {
    console.log('This is a file');
  } else if (entry.type === EntityType.Directory) {
    console.log('This is a directory');
  }
});

// Directory operations
await dir.rename('new_name');              // Rename
await dir.delete(true);                    // Recursive delete (use with caution!)

// Sync operations
dir.createSync(true);
dir.existsSync();
const entriesSync = dir.listSync();
dir.deleteSync(true);
```

### FileHandle - Streaming I/O

High-performance streaming I/O for line-by-line reading or frequent large file operations:

```typescript
import { openFS, FileOpenMode, SeekOrigin } from 'react-native-io';

const fs = openFS();

// Open file handle
const handle = fs.open('/path/to/large.txt', FileOpenMode.Read);

// Read line by line (async)
while (!(await handle.isEOF())) {
  const line = await handle.readLine();
  console.log('Line:', line);
}

// Random access
await handle.seek(100, SeekOrigin.Begin);  // Jump to byte 100
const position = await handle.position();  // Get current position

// Read specific number of bytes
const chunk = await handle.read(1024);     // Read 1KB

// Write operations
const writeHandle = fs.open('/path/to/output.txt', FileOpenMode.Write);
await writeHandle.writeLine('First line');
await writeHandle.writeLine('Second line');
await writeHandle.write(new Uint8Array([0x41, 0x42]));

// Sync operations
const line = handle.readLineSync();
handle.seekSync(0, SeekOrigin.Begin);

// ⚠️ Important: Always close handles when done
handle.close();
writeHandle.close();

// Check if closed
console.log(handle.isClosed); // true
```

### HTTP Client - File Download & Upload

Built-in HTTP client optimized for **file transfers**. Pure C++ implementation delivers native-level performance for downloading and uploading files.

```typescript
import { request, openFS, FS, HashAlgorithm } from 'react-native-io';

const fs = openFS();

// ============================================================================
// Download Files - Core Feature
// ============================================================================
// download(url, destinationPath, options?) downloads directly to disk.
// - Streams data to file (memory-efficient for large files)
// - Automatically creates parent directories if needed
// - Returns Response object with status info

// Basic download
await request.download(
  'https://example.com/video.mp4',
  `${FS.cacheDir}/video.mp4`
);

// Download with options
const downloadRes = await request.download(
  'https://example.com/large-file.zip',
  `${FS.documentDir}/downloads/archive.zip`,
  {
    headers: { 'Authorization': 'Bearer token123' },
    timeout: 60000,  // 60 seconds for large files
  }
);

if (downloadRes.ok) {
  console.log('✅ Download complete!');
  
  // Verify file integrity with hash
  const file = fs.file(`${FS.documentDir}/downloads/archive.zip`);
  const hash = await file.calcHash(HashAlgorithm.SHA256);
  console.log('File hash:', hash);
} else {
  console.log('❌ Download failed:', downloadRes.status, downloadRes.error);
}

// ============================================================================
// Upload Files - Core Feature
// ============================================================================
// upload(url, filePath, options?) uploads a file to server.
// - Streams file from disk (memory-efficient)
// - Automatically sets Content-Type based on file extension
// - Returns Response object with server response

// Basic upload
await request.upload(
  'https://api.example.com/upload',
  `${FS.documentDir}/photo.jpg`
);

// Upload with custom headers and options
const uploadRes = await request.upload(
  'https://api.example.com/files/upload',
  `${FS.documentDir}/document.pdf`,
  {
    headers: {
      'Authorization': 'Bearer token123',
      'X-Custom-Header': 'value'
    },
    timeout: 30000,
  }
);

if (uploadRes.ok) {
  // Parse server response (e.g., get uploaded file URL)
  const result = uploadRes.json();
  console.log('✅ Uploaded! File URL:', result.url);
} else {
  console.log('❌ Upload failed:', uploadRes.status, uploadRes.error);
}

// ============================================================================
// Practical Example: Download → Process → Upload
// ============================================================================

async function processRemoteFile() {
  const tempPath = `${FS.cacheDir}/temp_image.jpg`;
  
  // Step 1: Download image
  const downloadRes = await request.download(
    'https://example.com/source-image.jpg',
    tempPath
  );
  if (!downloadRes.ok) throw new Error('Download failed');
  
  // Step 2: Read and process (e.g., get file info)
  const file = fs.file(tempPath);
  const size = await file.size();
  const hash = await file.calcHash(HashAlgorithm.MD5);
  console.log(`Downloaded: ${size} bytes, MD5: ${hash}`);
  
  // Step 3: Upload to another server
  const uploadRes = await request.upload(
    'https://api.example.com/images',
    tempPath
  );
  if (!uploadRes.ok) throw new Error('Upload failed');
  
  // Step 4: Cleanup temp file
  await file.delete();
  
  return uploadRes.json();
}

// ============================================================================
// Standard HTTP Requests (GET, POST, PUT, DELETE, PATCH)
// ============================================================================
// For API calls that don't involve file transfers

// GET request
const response = await request.get('https://api.example.com/users');
console.log('Status:', response.status);  // 200
console.log('OK:', response.ok);          // true

// Parse response
const users = response.json();            // Parse as JSON
const text = response.text();             // Get raw text

// POST with JSON body
const postRes = await request.post('https://api.example.com/users', {
  json: { name: 'John', email: 'john@example.com' },
  headers: { 'Authorization': 'Bearer token123' }
});

// PUT, DELETE, PATCH
await request.put('https://api.example.com/users/1', { json: { name: 'Jane' } });
await request.delete('https://api.example.com/users/1');
await request.patch('https://api.example.com/users/1', { json: { status: 'active' } });

// Custom request with full options
const customRes = await request.request('POST', 'https://api.example.com/data', {
  body: 'raw string data',                 // Raw body (mutually exclusive with json)
  headers: { 'Content-Type': 'text/plain' },
  timeout: 10000,                          // 10 second timeout
  followRedirects: true                    // Follow 3xx redirects (default: true)
});

// ============================================================================
// Response Object Reference
// ============================================================================
// All request methods return a Response object:

console.log(response.status);      // HTTP status code: 200, 404, 500, etc.
console.log(response.statusText);  // Status text: 'OK', 'Not Found', etc.
console.log(response.ok);          // true if status is 2xx
console.log(response.headers);     // Response headers: { 'content-type': '...', ... }
console.log(response.url);         // Final URL (after any redirects)
console.log(response.error);       // Error message if request failed (undefined otherwise)
```

### Hash Computation

```typescript
import { openFS, FS, HashAlgorithm } from 'react-native-io';

const fs = openFS();
const file = fs.file(`${FS.cacheDir}/data.bin`);

// ============================================================================
// Calculate file hash using calcHash(algorithm)
// ============================================================================
// calcHash() computes the hash of the ENTIRE file content.
// It reads the file in chunks internally, so it's memory-efficient for large files.
// Only async version is available (no sync version).

// SHA256 - Most commonly used, good balance of speed and security
const sha256 = await file.calcHash(HashAlgorithm.SHA256);
console.log('SHA256:', sha256);  // e.g., 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855'

// MD5 - Fast but NOT cryptographically secure, good for checksums
const md5 = await file.calcHash(HashAlgorithm.MD5);
console.log('MD5:', md5);  // e.g., 'd41d8cd98f00b204e9800998ecf8427e'

// SHA1 - Legacy, not recommended for security purposes
const sha1 = await file.calcHash(HashAlgorithm.SHA1);

// CRC32 - Very fast, for error detection (not security)
const crc32 = await file.calcHash(HashAlgorithm.CRC32);

// ============================================================================
// All supported hash algorithms
// ============================================================================
// HashAlgorithm.MD5          - 128-bit, fast, insecure
// HashAlgorithm.SHA1         - 160-bit, legacy
// HashAlgorithm.SHA256       - 256-bit, recommended for most uses
// HashAlgorithm.SHA3_224     - SHA-3 family, 224-bit
// HashAlgorithm.SHA3_256     - SHA-3 family, 256-bit
// HashAlgorithm.SHA3_384     - SHA-3 family, 384-bit  
// HashAlgorithm.SHA3_512     - SHA-3 family, 512-bit
// HashAlgorithm.Keccak224    - Original Keccak, 224-bit
// HashAlgorithm.Keccak256    - Used by Ethereum
// HashAlgorithm.Keccak384    - Original Keccak, 384-bit
// HashAlgorithm.Keccak512    - Original Keccak, 512-bit
// HashAlgorithm.CRC32        - 32-bit checksum, very fast

// ============================================================================
// Practical example: Verify file integrity after download
// ============================================================================
const downloadedFile = fs.file(`${FS.cacheDir}/downloaded.zip`);
const expectedHash = 'abc123...';  // Hash provided by server

const actualHash = await downloadedFile.calcHash(HashAlgorithm.SHA256);
if (actualHash === expectedHash) {
  console.log('✅ File integrity verified!');
} else {
  console.log('❌ File corrupted or tampered!');
}
```

## Real-World Examples

### Example 1: Config File Management (Multi-Step)

```typescript
import { openFS, FS } from 'react-native-io';

// Use FSContext to reuse thread pool
const fs = openFS();
const configPath = `${FS.documentDir}/app_config.json`;
const configFile = fs.file(configPath);

// Load or create config
async function loadConfig() {
  if (await configFile.exists()) {
    const json = await configFile.readString();
    return JSON.parse(json);
  } else {
    const defaultConfig = {
      theme: 'light',
      language: 'en',
      notifications: true
    };
    await configFile.writeString(JSON.stringify(defaultConfig, null, 2));
    return defaultConfig;
  }
}

// Save config
async function saveConfig(config: any) {
  await configFile.writeString(JSON.stringify(config, null, 2));
}

const config = await loadConfig();
config.theme = 'dark';
await saveConfig(config);
```

### Example 2: Batch File Processing (Concurrent)

```typescript
import { openFS, FS, HashAlgorithm, request } from 'react-native-io';

// Create 4-thread pool
const fs = openFS(4);

const imageDir = fs.directory(`${FS.cacheDir}/images`);
await imageDir.create();

// Batch download images
const urls = [
  'https://example.com/img1.jpg',
  'https://example.com/img2.jpg',
  'https://example.com/img3.jpg',
  'https://example.com/img4.jpg'
];

await Promise.all(
  urls.map((url, i) => 
    request.download(url, `${imageDir.path}/image_${i}.jpg`)
  )
);

// Concurrently compute hashes for all images
const files = await imageDir.listFiles();
const hashes = await Promise.all(
  files.map(f => fs.file(f.path).hash(HashAlgorithm.MD5))
);

console.log('Image hashes:', hashes);
```

### Example 3: Log File Appending (High-Performance)

```typescript
import { openFS, FS, FileOpenMode } from 'react-native-io';

const fs = openFS();
const logFile = `${FS.cacheDir}/app.log`;

// Use FileHandle for efficient appending
const handle = fs.open(logFile, FileOpenMode.Append);

async function log(message: string) {
  const timestamp = new Date().toISOString();
  await handle.writeLine(`[${timestamp}] ${message}`);
}

await log('App started');
await log('User logged in');
await log('Data loaded');

// Close handle when done
handle.close();
```

## More Examples

Check out the unit tests in [example/src/__tests__](example/src/__tests__) for comprehensive usage:

- [File.harness.ts](example/src/__tests__/File.harness.ts) - Complete file operations
- [Directory.harness.ts](example/src/__tests__/Directory.harness.ts) - Complete directory operations
- [Request.harness.ts](example/src/__tests__/Request.harness.ts) - Complete HTTP requests
- [Platform.harness.ts](example/src/__tests__/Platform.harness.ts) - Platform-specific features

## TypeScript Definitions

```typescript
// Full type exports
import {
  // Classes
  File,
  Directory,
  FileHandle,
  FSContext,
  
  // Functions
  openFS,
  request,
  computeHash,
  
  // Enums
  EntityType,
  WriteMode,
  HashAlgorithm,
  FileOpenMode,
  SeekOrigin,
  IOPlatformType,
  
  // Types
  FileMetadata,
  DirectoryEntry,
  Response,
  RequestOptions,
  
  // Global objects
  FS,
  Platform
} from 'react-native-io';
```

## ⚠️Important Notes

1. **Resource Management**: `FSContext`, `File`, and `Directory` instances are automatically released by GC. Manual cleanup is typically not required.
2. **FileHandle Must Be Closed**: Always call `.close()` on `FileHandle` instances to prevent resource leaks.
3. **Thread Pool Size**: Default of 1 thread is sufficient for most scenarios. Increase only for high concurrency.
4. **Sync Operation Risks**: Sync APIs on large files will block the UI thread, causing jank. Use with caution.
5. **Permissions**: External storage or sensitive directories require appropriate system permissions.


## License

MIT

---

**Made with ❤️ by the React Native community**