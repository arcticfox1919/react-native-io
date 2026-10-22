# API Reference

This document describes the complete public TypeScript API exported by
`react-native-io`. Examples use the Promise-based APIs unless a synchronous
operation is the subject of the example.

## Contents

- [Imports](#imports)
- [Common conventions](#common-conventions)
- [`FS`](#fs)
- [`Platform`](#platform)
- [`openFS()` and `FSContext`](#openfs-and-fscontext)
- [`File`](#file)
- [`Directory`](#directory)
- [`FileHandle`](#filehandle)
- [HTTP](#http)
- [Enums and data types](#enums-and-data-types)
- [Low-level interfaces](#low-level-interfaces)
- [Complete examples](#complete-examples)

## Imports

```typescript
import {
  Directory,
  EntityType,
  File,
  FileHandle,
  FileOpenMode,
  FS,
  FSContext,
  HashAlgorithm,
  IOPlatformType,
  Platform,
  Request,
  SeekOrigin,
  WriteMode,
  openFS,
  request,
  type DirectoryEntry,
  type DownloadOptions,
  type DownloadProgress,
  type DownloadResult,
  type FileHandleId,
  type FileMetadata,
  type IOFileSystem,
  type IOPlatform,
  type IOPlatformAndroid,
  type IOPlatformIOS,
  type IOPlatformHarmony,
  type IORequest,
  type RequestOptions,
  type RequestOptionsWithBody,
  type Response,
  type StringEncoding,
  type UploadOptions,
  type UploadProgress,
  type UploadResult,
} from 'react-native-io';
```

`IOFileSystem` and `IORequest` describe the native JSI objects and are exported
for typing integrations. Applications normally use `File`, `Directory`,
`FileHandle`, `FSContext`, and `Request` instead of invoking those native
interfaces directly. `FileHandleId` is the numeric identifier used by the
low-level `IOFileSystem` handle methods.

## Common conventions

### Async and sync methods

Methods without a `Sync` suffix return a `Promise` and schedule filesystem work
on a native worker pool. Methods ending in `Sync` run on the JavaScript thread
and block it until the operation finishes.

```typescript
await file.writeString('async');
file.writeStringSync('sync');
```

Use async methods for normal application work. Sync methods are suitable only
for small, bounded operations when the result must be available immediately.

### Binary data

Binary APIs accept and return `ArrayBuffer`:

```typescript
const input = new Uint8Array([0, 1, 2, 255]);
await file.writeBytes(input.buffer);

const output = new Uint8Array(await file.readBytes());
```

### Paths

Constructors create path objects; they do not create files or directories on
disk. Use `FS.joinPaths()` instead of manually selecting `/` or `\` separators.

```typescript
const path = FS.joinPaths(FS.documentDir, 'profiles', 'current.json');
const file = new File(path);
```

## `FS`

`FS` provides cross-platform locations, storage information, path joining, and
native string conversion.

### Directory properties

| Property              | iOS                             | Android                                    |
| --------------------- | ------------------------------- | ------------------------------------------ |
| `FS.documentDir`      | Application `Documents`         | Internal application `files` directory     |
| `FS.cacheDir`         | `Library/Caches`                | Internal application cache                 |
| `FS.tempDir`          | Application temporary directory | Same path as `FS.cacheDir`                 |
| `FS.bundleDir`        | Read-only application bundle    | `undefined`                                |
| `FS.externalDir`      | Same path as `FS.documentDir`   | App-specific external files root           |
| `FS.externalCacheDir` | Same path as `FS.cacheDir`      | App-specific external cache                |
| `FS.downloadDir`      | Same path as `FS.documentDir`   | App-specific external `Download` directory |

These are application-scoped paths. `FS.downloadDir` is not the shared public
Downloads collection.

On HarmonyOS API 16+, `documentDir` maps to the application `filesDir`,
`cacheDir` and `tempDir` use their corresponding AbilityKit directories, and
`downloadDir` is `<filesDir>/Download`. `externalDir` and `externalCacheDir`
fall back to the private files and cache directories; `bundleDir` is undefined.
See [HarmonyOS integration](../harmony/README.md) for installation and limits.

```typescript
console.log('documents:', FS.documentDir);
console.log('cache:', FS.cacheDir);
console.log('temporary:', FS.tempDir);
console.log('downloads:', FS.downloadDir);
```

### `FS.getPlatform()`

Returns the complete platform-specific object used by `Platform`.

```typescript
const platform = FS.getPlatform();

if (platform.platform === IOPlatformType.Android) {
  console.log(platform.sdkVersion);
  console.log(platform.externalFilesDir);
} else if (platform.platform === IOPlatformType.iOS) {
  console.log(platform.libraryDir);
  console.log(platform.bundleDir);
}
```

Signature:

```typescript
FS.getPlatform<T extends IOPlatform = IOPlatform>(): T;
```

Prefer discriminating on `platform.platform` as shown above. The optional
generic parameter is a TypeScript assertion and does not change the runtime
platform.

### `FS.joinPaths()`

Joins path segments using native filesystem rules.

```typescript
const path = FS.joinPaths(FS.documentDir, 'data', 'file.json');
```

Signature:

```typescript
FS.joinPaths(...segments: string[]): string;
```

### Storage information

```typescript
const available = await FS.getAvailableSpace(FS.documentDir);
const total = await FS.getTotalSpace(FS.documentDir);

const availableNow = FS.getAvailableSpaceSync(FS.documentDir);
const totalNow = FS.getTotalSpaceSync(FS.documentDir);
```

| Method                        | Returns                                      |
| ----------------------------- | -------------------------------------------- |
| `getAvailableSpace(path)`     | `Promise<number>` containing available bytes |
| `getAvailableSpaceSync(path)` | Available bytes                              |
| `getTotalSpace(path)`         | `Promise<number>` containing total bytes     |
| `getTotalSpaceSync(path)`     | Total bytes                                  |

### String encoding

Supported encodings are `utf8`, `ascii`, and `latin1`. The default is `utf8`.

```typescript
const bytes = FS.encodeString('Hello', 'utf8');
const text = FS.decodeString(bytes, 'utf8');
```

Signatures:

```typescript
FS.encodeString(value: string, encoding?: StringEncoding): ArrayBuffer;
FS.decodeString(value: ArrayBuffer, encoding?: StringEncoding): string;
```

## `Platform`

`Platform` is a lazy singleton that exposes native platform directories. Use
it when a platform-specific path is required; use `FS` for portable code.

### Common properties

```typescript
Platform.platform;
Platform.filesDir;
Platform.cacheDir;
Platform.documentsDir;
```

On Android, `Platform.documentsDir` is the app-specific external `Documents`
directory. It differs from `FS.documentDir`, which is the internal `files`
directory.

### Android properties and methods

Available after narrowing with `IOPlatformType.Android`:

| Member                   | Meaning                                             |
| ------------------------ | --------------------------------------------------- |
| `filesDir`               | Internal application files directory                |
| `cacheDir`               | Internal application cache directory                |
| `codeCacheDir`           | Internal code cache directory                       |
| `noBackupFilesDir`       | Internal directory excluded from backup             |
| `dataDir`                | Application data root; available on Android API 24+ |
| `externalFilesDir`       | Primary app-specific external files root            |
| `externalCacheDir`       | Primary app-specific external cache                 |
| `obbDir`                 | OBB directory                                       |
| `downloadsDir`           | App-specific external `Download` directory          |
| `picturesDir`            | App-specific external `Pictures` directory          |
| `moviesDir`              | App-specific external `Movies` directory            |
| `musicDir`               | App-specific external `Music` directory             |
| `documentsDir`           | App-specific external `Documents` directory         |
| `dcimDir`                | App-specific external `DCIM` directory              |
| `sdkVersion`             | Android SDK integer                                 |
| `getExternalCacheDirs()` | External cache paths on all available volumes       |
| `getExternalFilesDirs()` | External file roots on all available volumes        |
| `getObbDirs()`           | OBB paths on all available volumes                  |

```typescript
if (Platform.platform === IOPlatformType.Android) {
  console.log(Platform.filesDir);
  console.log(Platform.documentsDir);
  console.log(Platform.getExternalFilesDirs());
}
```

### iOS properties

Available after narrowing with `IOPlatformType.iOS`:

| Property                | Meaning                         |
| ----------------------- | ------------------------------- |
| `documentsDir`          | Application Documents directory |
| `filesDir`              | Alias of `documentsDir`         |
| `libraryDir`            | Application Library directory   |
| `cacheDir`              | `Library/Caches`                |
| `tempDir`               | Temporary directory             |
| `applicationSupportDir` | Application Support directory   |
| `bundleDir`             | Read-only application bundle    |
| `homeDir`               | Application sandbox home        |

### HarmonyOS properties

After narrowing with `IOPlatformType.Harmony`, `IOPlatformHarmony` exposes
`filesDir`, `documentsDir` (an alias of `filesDir`), `cacheDir`, and `tempDir`.
These paths come from the native application context rather than a hard-coded
sandbox location.

## `openFS()` and `FSContext`

`openFS()` creates an isolated filesystem context. Files, directories, and
handles created by the context share its native worker pool.

```typescript
const fs = openFS(); // One worker by default.
const parallelFs = openFS(4); // Four workers for measured parallel workloads.
```

Signatures:

```typescript
openFS(numThreads?: number): FSContext;
new FSContext(numThreads?: number);
```

`openFS()` is the preferred spelling; the public constructor has the same
behavior.

The thread count must be an integer from 1 through 32. Workers are created
lazily when the first async operation is submitted. Sync-only use does not
start a worker.

### Why contexts exist

Standalone paths share one process-wide lazy default worker. They do not each
create a pool. The following code illustrates the design problem the shared
worker avoids:

```typescript
// ❌ Problem: Multiple standalone instances = multiple thread pools
const file1 = new File('/path/file1.txt'); // Creates thread pool #1
const file2 = new File('/path/file2.txt'); // Creates thread pool #2
const dir = new Directory('/path/dir'); // Creates thread pool #3
```

The actual implementation shares one default worker:

```typescript
const file1 = new File('/path/file1.txt');
const file2 = new File('/path/file2.txt');
const dir = new Directory('/path/dir');
// All three use the same lazy default worker.
```

Use `openFS()` when a batch needs independent concurrency or explicit lifetime
control:

```typescript
const fs = openFS(4);

try {
  const files = paths.map((path) => fs.file(path));
  const contents = await Promise.all(files.map((file) => file.readString()));
  console.log(contents);
} finally {
  fs.close();
}
```

### `FSContext` methods

| Method              | Description                                                          |
| ------------------- | -------------------------------------------------------------------- |
| `file(path)`        | Creates a `File` bound to this context                               |
| `directory(path)`   | Creates a `Directory` bound to this context                          |
| `open(path, mode?)` | Opens a `FileHandle`; mode defaults to `Read`                        |
| `close()`           | Rejects new work and releases the context after accepted work drains |

`close()` is idempotent and returns immediately. Native shutdown completes in
the background. After it is called, objects created by that context reject new
operations. Standalone `File` and `Directory` objects use the runtime-wide
default worker and are not affected.

## `File`

### Construction and path properties

```typescript
const file = new File(FS.joinPaths(FS.documentDir, 'profile.json'));

console.log(file.path);
console.log(file.name); // profile.json
console.log(file.extension); // .json
console.log(file.nameWithoutExtension); // profile
console.log(file.parent);
```

Constructor signature:

```typescript
new File(path: string, fs?: IOFileSystem);
```

The second argument is mainly for integrations that already own a native
filesystem object. Prefer `context.file(path)` when using an `FSContext`.

`path` is immutable. `copy()` and `move()` return a new `File`; they do not
change the original object's `path`.

### Query methods

| Async            | Sync                 | Result                             |
| ---------------- | -------------------- | ---------------------------------- |
| `exists()`       | `existsSync()`       | Whether the path exists            |
| `isFile()`       | `isFileSync()`       | Whether the path is a regular file |
| `size()`         | `sizeSync()`         | File size in bytes                 |
| `metadata()`     | `metadataSync()`     | `FileMetadata`                     |
| `modifiedTime()` | `modifiedTimeSync()` | JavaScript `Date`                  |
| `absolutePath()` | `absolutePathSync()` | Absolute, normalized path          |

```typescript
if (await file.exists()) {
  const metadata = await file.metadata();
  console.log(metadata.size, new Date(metadata.modifiedTime), metadata.type);
}
```

### Read methods

| Async          | Sync               | Result                                         |
| -------------- | ------------------ | ---------------------------------------------- |
| `readString()` | `readStringSync()` | Entire file as UTF-8 text                      |
| `readBytes()`  | `readBytesSync()`  | Entire file as `ArrayBuffer`                   |
| `readLines()`  | `readLinesSync()`  | Text split into lines with trailing CR removed |

These methods load the complete file into memory. Use `FileHandle` for bounded
chunk processing of large files.

```typescript
const text = await file.readString();
const lines = await file.readLines();
const bytes = new Uint8Array(await file.readBytes());
```

### Write methods

| Async                         | Sync                              |
| ----------------------------- | --------------------------------- |
| `writeString(content, mode?)` | `writeStringSync(content, mode?)` |
| `writeBytes(data, mode?)`     | `writeBytesSync(data, mode?)`     |
| `appendString(content)`       | `appendStringSync(content)`       |
| `appendBytes(data)`           | `appendBytesSync(data)`           |

Write methods create missing parent directories. The default mode is
`WriteMode.Overwrite`.

```typescript
await file.writeString(JSON.stringify({ theme: 'dark' }));
await file.appendString('\n');

const bytes = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]);
await file.writeBytes(bytes.buffer);
```

#### Write modes

| Mode                      | Behavior                                                              |
| ------------------------- | --------------------------------------------------------------------- |
| `WriteMode.Overwrite`     | Creates or truncates the destination and writes new content           |
| `WriteMode.Append`        | Creates the destination or appends at the end                         |
| `WriteMode.Atomic`        | Writes a temporary file and atomically replaces a regular destination |
| `WriteMode.AtomicDurable` | Atomic replacement plus file and parent-directory synchronization     |

Atomic modes reject an existing symlink or directory destination. Use
`Atomic` for configuration and indexes that readers must never observe in a
partially written state. `AtomicDurable` adds latency and is intended for data
that needs stronger protection against power loss.

```typescript
await file.writeString(serializedConfig, WriteMode.Atomic);
```

### File management

| Async                                 | Sync                                      | Behavior                                            |
| ------------------------------------- | ----------------------------------------- | --------------------------------------------------- |
| `create(createParents = true)`        | `createSync(createParents = true)`        | Creates an empty file if needed                     |
| `delete()`                            | `deleteSync()`                            | Deletes the file and returns whether it was deleted |
| `copy(destination, overwrite = true)` | `copySync(destination, overwrite = true)` | Copies and returns a new destination `File`         |
| `move(destination)`                   | `moveSync(destination)`                   | Moves and returns a new destination `File`          |

The destination parent for `copy()` and `move()` must already exist.

```typescript
const source = new File(FS.joinPaths(FS.documentDir, 'original.txt'));
await source.writeString('Original content');

const backup = await source.copy(FS.joinPaths(FS.cacheDir, 'backup.txt'), true);

const moved = await backup.move(FS.joinPaths(FS.documentDir, 'permanent.txt'));

console.log(source.path); // Still points to original.txt.
console.log(moved.path); // Points to permanent.txt.
```

Rename a file by moving it to a new name in the same parent directory:

```typescript
const renamed = await file.move(FS.joinPaths(file.parent, 'new-name.txt'));
```

### Hashing

`calcHash()` streams the file through native code and returns a lowercase
hexadecimal string. It defaults to SHA-256.

```typescript
const sha256 = await file.calcHash();
const md5 = await file.calcHash(HashAlgorithm.MD5);
const crc32 = await file.calcHash(HashAlgorithm.CRC32);
```

Supported algorithms:

- `MD5`
- `SHA1`
- `SHA256`
- `SHA3_224`, `SHA3_256`, `SHA3_384`, `SHA3_512`
- `Keccak224`, `Keccak256`, `Keccak384`, `Keccak512`
- `CRC32`

MD5, SHA-1, and CRC32 are useful for compatibility or accidental-corruption
checks, but should not be used as proof against malicious modification.

```typescript
const archive = new File(FS.joinPaths(FS.cacheDir, 'archive.tmp'));
const actual = await archive.calcHash(HashAlgorithm.SHA256);

if (actual !== expectedSha256) {
  await archive.delete();
  throw new Error('Downloaded file failed SHA-256 verification');
}
```

### Utility and lifetime methods

| Member       | Result                                                                                                 |
| ------------ | ------------------------------------------------------------------------------------------------------ |
| `dispose()`  | Releases this standalone object's reference to the default filesystem; the shared worker remains alive |
| `toString()` | The path string                                                                                        |
| `toJSON()`   | `{ path, name, extension }`                                                                            |

For a `File` created by an `FSContext`, the context owns the filesystem
lifetime and `FSContext.close()` performs cleanup.

## `Directory`

### Construction and properties

```typescript
const dir = new Directory(FS.joinPaths(FS.documentDir, 'logs'));

console.log(dir.path);
console.log(dir.name);
console.log(dir.parentPath);
console.log(dir.parent.path);
console.log(dir.childPath('today/app.log'));
```

Constructor signature:

```typescript
new Directory(path: string, fs?: IOFileSystem);
```

The second argument is mainly for integrations. Prefer
`context.directory(path)` for paths owned by an `FSContext`.

### Query and management methods

| Async                                | Sync                                     | Behavior                                                      |
| ------------------------------------ | ---------------------------------------- | ------------------------------------------------------------- |
| `exists()`                           | `existsSync()`                           | Tests whether the path exists                                 |
| `isDirectory()`                      | `isDirectorySync()`                      | Tests whether the path is a directory                         |
| `metadata()`                         | `metadataSync()`                         | Returns `FileMetadata`                                        |
| `absolutePath()`                     | `absolutePathSync()`                     | Returns the absolute path                                     |
| `create(recursive = true)`           | `createSync(recursive = true)`           | Creates the directory and optionally missing parents          |
| `delete(recursive = false)`          | `deleteSync(recursive = false)`          | Deletes the directory and returns the deleted-entry count     |
| `move(destination)`                  | `moveSync(destination)`                  | Moves the directory and returns a new destination `Directory` |
| `list(recursive = false)`            | `listSync(recursive = false)`            | Returns every matching `DirectoryEntry`                       |
| `listFiles(recursive = false)`       | `listFilesSync(recursive = false)`       | Returns file entries only                                     |
| `listDirectories(recursive = false)` | `listDirectoriesSync(recursive = false)` | Returns directory entries only                                |
| `isEmpty()`                          | `isEmptySync()`                          | Tests whether the immediate directory is empty                |
| `getTotalSize(recursive = true)`     | `getTotalSizeSync(recursive = true)`     | Sums file sizes                                               |

`listFilePaths(recursive = false)` is async-only and returns `string[]`.

```typescript
await dir.create(true);

const entries = await dir.list();
for (const entry of entries) {
  switch (entry.type) {
    case EntityType.File:
      console.log('file', entry.name, entry.size);
      break;
    case EntityType.Directory:
      console.log('directory', entry.name);
      break;
  }
}

const files = await dir.listFiles(true);
const directories = await dir.listDirectories(true);
const filePaths = await dir.listFilePaths(true);
const totalBytes = await dir.getTotalSize(true);
```

Deleting without `recursive: true` requires an empty directory:

```typescript
await emptyDir.delete();
await populatedDir.delete(true);
```

Rename a directory by moving it to another path:

```typescript
const renamed = await dir.move(
  FS.joinPaths(dir.parentPath, 'renamed-directory')
);
```

### Utility and lifetime methods

| Member                    | Result                                                 |
| ------------------------- | ------------------------------------------------------ |
| `childPath(relativePath)` | Joins a child path without filesystem I/O              |
| `dispose()`               | Releases this standalone object's filesystem reference |
| `toString()`              | The path string                                        |
| `toJSON()`                | `{ path, name }`                                       |

## `FileHandle`

Use `FileHandle` for repeated reads or writes without reopening the file and
for bounded processing of large files. Handles are created by `FSContext.open()`.

### Open modes

| Mode                      | Behavior                             |
| ------------------------- | ------------------------------------ |
| `FileOpenMode.Read`       | Read only; the file must exist       |
| `FileOpenMode.Write`      | Write only; creates or truncates     |
| `FileOpenMode.Append`     | Append only; creates if missing      |
| `FileOpenMode.ReadWrite`  | Read and write; the file must exist  |
| `FileOpenMode.WriteRead`  | Write and read; creates or truncates |
| `FileOpenMode.AppendRead` | Append and read; creates if missing  |

### Methods

| Member                         | Result                                                        |
| ------------------------------ | ------------------------------------------------------------- |
| `seek(offset, origin = Begin)` | Moves the cursor and resolves to its byte position            |
| `rewind()`                     | Moves the cursor to byte zero                                 |
| `getPosition()`                | Resolves to the current byte position                         |
| `getSize()`                    | Resolves to file size in bytes                                |
| `isEOF()`                      | Resolves to whether the cursor is at end of file              |
| `read(size?)`                  | Reads bytes; omitted size reads the remainder                 |
| `readString(size?)`            | Reads UTF-8 text; omitted size reads the remainder            |
| `readLine()`                   | Reads one line without the newline                            |
| `write(data)`                  | Writes an `ArrayBuffer` and resolves to bytes written         |
| `writeString(content)`         | Writes text and resolves to bytes written                     |
| `writeLine(line)`              | Writes text plus a newline and resolves to bytes written      |
| `flush()`                      | Flushes buffered data                                         |
| `truncate()`                   | Truncates at the current cursor position                      |
| `closeAsync()`                 | Closes safely through the native worker queue                 |
| `close()`                      | Closes synchronously; use only when no operation is in flight |
| `isClosed`                     | Boolean property indicating local closed state                |

All positioning and I/O methods are asynchronous. There are no
`readLineSync()`, `seekSync()`, or similar handle methods.

### Line-by-line reading

```typescript
const fs = openFS();
const handle = fs.open(path, FileOpenMode.Read);

try {
  while (!(await handle.isEOF())) {
    console.log(await handle.readLine());
  }
} finally {
  await handle.closeAsync();
  fs.close();
}
```

### Random access and truncation

```typescript
const fs = openFS();
const handle = fs.open(path, FileOpenMode.ReadWrite);

try {
  await handle.seek(1024, SeekOrigin.Begin);
  const chunk = await handle.read(4096);

  await handle.seek(-128, SeekOrigin.End);
  console.log(await handle.getPosition(), new Uint8Array(chunk).length);

  await handle.truncate();
  await handle.flush();
} finally {
  await handle.closeAsync();
  fs.close();
}
```

Operations submitted on the same handle are serialized by the native layer,
but dependent work should still be awaited in program order.

## HTTP

The HTTP API uses platform networking on Android and iOS. Use the global
`request` object for independent calls or construct `Request` when explicit
client lifetime control is useful.

### Request options

```typescript
interface RequestOptions {
  headers?: Record<string, string>;
  timeout?: number;
  followRedirects?: boolean;
}

interface RequestOptionsWithBody extends RequestOptions {
  json?: object | string | number | boolean | null;
  body?: string;
  data?: ArrayBuffer;
}
```

The standard request timeout is 30,000 ms. `followRedirects` defaults to
`true`. If more than one body property is supplied, precedence is `json`, then
`body`, then `data`. Supplying `json` serializes the value and sets
`Content-Type: application/json`.

### Request methods

```typescript
request.request(method, url, options?);
request.get(url, options?);
request.post(url, options?);
request.put(url, options?);
request.patch(url, options?);
request.delete(url, options?);
request.head(url, options?);
request.options(url, options?);
```

`post`, `put`, and `patch` accept `RequestOptionsWithBody`; the other
convenience methods accept `RequestOptions`. Use `request.request()` for other
HTTP methods or a body on a custom method.

```typescript
const response = await request.get('https://api.example.com/users', {
  headers: { Authorization: 'Bearer token' },
  timeout: 15_000,
});

const created = await request.post('https://api.example.com/users', {
  json: { name: 'Narwal', enabled: true },
});

await request.put('https://api.example.com/config', {
  body: 'mode=production',
  headers: { 'Content-Type': 'text/plain' },
});

await request.post('https://api.example.com/binary', {
  data: new Uint8Array([1, 2, 3]).buffer,
  headers: { 'Content-Type': 'application/octet-stream' },
});
```

### `Response`

```typescript
interface Response {
  ok: boolean;
  status: number;
  statusText: string;
  headers: Record<string, string>;
  url: string;
  error?: string;
  text(): string;
  json<T = unknown>(): T;
  arrayBuffer(): ArrayBuffer;
}
```

The Promise resolves after the complete response body is available. Body
accessors are synchronous conversions of that in-memory body. `ok` is `true`
only for a successful transport and a 2xx status. Check `status` for HTTP
errors; `error` primarily describes transport failures.

```typescript
if (response.ok) {
  const users = response.json<Array<{ id: number; name: string }>>();
  console.log(users);
} else {
  console.warn(response.status, response.error);
}
```

Use `download()` for large response bodies instead of retaining the entire
body in memory.

### Downloads

```typescript
interface DownloadOptions extends RequestOptions {
  resumable?: boolean;
  onProgress?: (progress: DownloadProgress) => void;
}

interface DownloadProgress {
  bytesReceived: number;
  totalBytes: number; // -1 when unknown.
  progress: number; // Range 0 through 1.
}

interface DownloadResult {
  ok: boolean;
  status: number;
  filePath: string;
  fileSize: number;
  error?: string;
}
```

Signature:

```typescript
request.download(
  url: string,
  destinationPath: string,
  options?: DownloadOptions
): Promise<DownloadResult>;
```

```typescript
const result = await request.download(url, destinationPath, {
  headers: { Authorization: 'Bearer token' },
  timeout: 60_000,
});
```

Download timeout defaults to 60,000 ms. Missing destination parent directories
are created. `DownloadResult` contains `ok`, `status`, `filePath`, `fileSize`,
and an optional `error`.

```typescript
const destination = FS.joinPaths(FS.downloadDir, 'manual.pdf');
const result = await request.download(
  'https://example.com/manual.pdf',
  destination
);

if (!result.ok) {
  throw new Error(result.error ?? `Download failed with ${result.status}`);
}

console.log(result.filePath, result.fileSize);
```

On Android, resuming requires `resumable: true`, an existing partial file, and
an `If-Range` validator such as an ETag or Last-Modified value:

```typescript
await request.download(url, destination, {
  resumable: true,
  headers: { 'If-Range': etag },
});
```

Without the validator, Android restarts the download. iOS and HarmonyOS restart
instead of resuming. `onProgress` is present in the public option type but is
not yet connected to the native transfer implementation; do not rely on it.

### Multipart uploads

```typescript
interface UploadOptions extends RequestOptions {
  fieldName?: string;
  fileName?: string;
  mimeType?: string;
  formFields?: Record<string, string>;
  onProgress?: (progress: UploadProgress) => void;
}

interface UploadProgress {
  bytesSent: number;
  totalBytes: number;
  progress: number; // Range 0 through 1.
}

interface UploadResult {
  ok: boolean;
  status: number;
  responseText: string;
  error?: string;
}
```

Signature:

```typescript
request.upload(
  url: string,
  filePath: string,
  options?: UploadOptions
): Promise<UploadResult>;
```

```typescript
const result = await request.upload(url, filePath, {
  fieldName: 'image',
  fileName: 'profile.jpg',
  mimeType: 'image/jpeg',
  headers: { Authorization: 'Bearer token' },
  formFields: {
    category: 'profile',
    description: 'User profile photo',
  },
  timeout: 60_000,
});
```

The defaults are `fieldName: "file"` and a 60,000 ms timeout. The native
client constructs a `multipart/form-data` request. `UploadResult` contains
`ok`, `status`, `responseText`, and an optional `error`.

```typescript
if (!result.ok) {
  throw new Error(result.error ?? `Upload failed with ${result.status}`);
}

const payload = JSON.parse(result.responseText) as { url: string };
console.log(payload.url);
```

`onProgress` is declared in `UploadOptions` but is not yet connected to the
native implementation.

HarmonyOS uses file-backed RCP multipart uploads. If MIME type is omitted, it
uses `application/octet-stream`. RCP limits the source path and remote filename
to 127 UTF-8 bytes each and MIME type to 63 bytes; larger values fail explicitly.
Multipart uploads do not automatically follow redirects on this backend.

### Explicit `Request` lifetime

The global proxy creates a client for each method call. A custom `Request`
instance can be reused and manually released:

```typescript
const client = new Request();

try {
  const first = await client.get('https://api.example.com/first');
  const second = await client.post('https://api.example.com/second', {
    json: { enabled: true },
  });
  console.log(first.status, second.status);
} finally {
  client.dispose();
}
```

## Enums and data types

### `EntityType`

```typescript
enum EntityType {
  NotFound = 0,
  File = 1,
  Directory = 2,
}
```

### `FileMetadata`

```typescript
interface FileMetadata {
  size: number;
  modifiedTime: number; // Milliseconds since Unix epoch.
  type: EntityType;
}
```

### `DirectoryEntry`

```typescript
interface DirectoryEntry {
  path: string;
  name: string;
  type: EntityType;
  size: number; // Zero for directories.
}
```

### Other enums

- `IOPlatformType`: `iOS`, `Android`, `Harmony`
- `WriteMode`: `Overwrite`, `Append`, `Atomic`, `AtomicDurable`
- `HashAlgorithm`: all algorithms listed under [Hashing](#hashing)
- `FileOpenMode`: all modes listed under [Open modes](#open-modes)
- `SeekOrigin`: `Begin`, `Current`, `End`

## Low-level interfaces

`IOFileSystem`, `IORequest`, and `FileHandleId` describe native JSI objects.
They support library integrations and advanced wrappers. The package root does
not export factories for these interfaces, so normal application code should
use the higher-level APIs documented above.

### `IOFileSystem`

The higher-level mapping is:

| Low-level members                                                                             | Preferred wrapper                                          |
| --------------------------------------------------------------------------------------------- | ---------------------------------------------------------- |
| `exists`, `isFile`, `getMetadata`, `getFileSize`, `getModifiedTime` and their `Sync` forms    | `File` query methods                                       |
| `readString`, `readBytes` and their `Sync` forms                                              | `File` read methods                                        |
| `writeString`, `writeBytes` and their `Sync` forms                                            | `File` write methods                                       |
| `createFile`, `deleteFile`, `copyFile`, `moveFile` and their `Sync` forms                     | `File` management methods                                  |
| `isDirectory` and `isDirectorySync`                                                           | `Directory.isDirectory()` and `isDirectorySync()`          |
| `createDirectory`, `deleteDirectory`, `listDirectory`, `moveDirectory` and their `Sync` forms | `Directory` methods                                        |
| `getParentPath`, `getFileName`, `getFileExtension`, `getFileNameWithoutExtension`             | `File` and `Directory` path properties                     |
| `joinPaths`                                                                                   | `FS.joinPaths()`                                           |
| `getAbsolutePath`, `normalizePath` and their `Sync` forms                                     | `absolutePath()` wrappers; normalization is low-level only |
| `getAvailableSpace`, `getTotalSpace` and their `Sync` forms                                   | `FS` storage methods                                       |
| `calcHash`                                                                                    | `File.calcHash()`                                          |
| `openFile` and all `file*` methods                                                            | `FSContext.open()` and `FileHandle`                        |
| `dispose`                                                                                     | `FSContext.close()`                                        |

The low-level write methods additionally expose `createParents?: boolean`.
`File.writeString()` and `File.writeBytes()` always enable it.

The low-level handle API uses a numeric `FileHandleId`:

```typescript
type FileHandleId = number;
```

Its methods are `openFile`, `fileClose`, `fileCloseAsync`, `fileSeek`,
`fileRewind`, `fileGetPosition`, `fileGetSize`, `fileIsEOF`, `fileFlush`,
`fileTruncate`, `fileRead`, `fileReadString`, `fileReadLine`, `fileWrite`,
`fileWriteString`, and `fileWriteLine`.

### `IORequest`

`IORequest` is the native transport shape wrapped by `Request`. Headers and
multipart form values use flat arrays because they cross the JSI boundary.

```typescript
interface IORequest {
  request(
    url: string,
    method: string,
    headers: string[],
    body: string | ArrayBuffer | null,
    timeout: number,
    followRedirects: boolean
  ): Promise<NativeHttpResponse>;

  download(
    url: string,
    destinationPath: string,
    headers: string[],
    timeout: number,
    resumable: boolean
  ): Promise<NativeDownloadResult>;

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
```

The `Native*` result names shown here are implementation types and are not
exported from the package root. Use `Response`, `DownloadResult`, and
`UploadResult` in application code.

## Complete examples

### Standalone file workflow

```typescript
import { File, FS, WriteMode } from 'react-native-io';

const file = new File(FS.joinPaths(FS.documentDir, 'example.txt'));

await file.writeString('Hello, World!');
await file.writeString('\nAppended line', WriteMode.Append);

if (await file.exists()) {
  console.log(await file.readString());
  console.log(await file.size());
  console.log(await file.modifiedTime());
}

const copied = await file.copy(FS.joinPaths(FS.cacheDir, 'example.txt'));
const moved = await copied.move(FS.joinPaths(FS.cacheDir, 'renamed.txt'));
await moved.delete();
```

### Standalone directory workflow

```typescript
import { Directory, EntityType, File, FS } from 'react-native-io';

const dir = new Directory(FS.joinPaths(FS.cacheDir, 'standalone-example'));
await dir.create(true);

await new File(dir.childPath('document.txt')).writeString('Document content');
await new File(dir.childPath('image.bin')).writeBytes(
  new Uint8Array([0, 1, 2, 3]).buffer
);
await new Directory(dir.childPath('nested')).create();

for (const entry of await dir.list()) {
  const kind = entry.type === EntityType.File ? 'file' : 'directory';
  console.log(kind, entry.name, entry.path);
}

console.log('files:', await dir.listFiles());
console.log('directories:', await dir.listDirectories());
console.log('total bytes:', await dir.getTotalSize(true));

await dir.delete(true);
```

### Atomic configuration file

```typescript
import { FS, WriteMode, openFS } from 'react-native-io';

type AppConfig = {
  theme: 'light' | 'dark';
  language: string;
  notifications: boolean;
};

const fs = openFS();
const config = fs.file(FS.joinPaths(FS.documentDir, 'app_config.json'));

async function loadConfig(): Promise<AppConfig> {
  if (await config.exists()) {
    return JSON.parse(await config.readString()) as AppConfig;
  }

  const defaults: AppConfig = {
    theme: 'light',
    language: 'en',
    notifications: true,
  };
  await config.writeString(JSON.stringify(defaults, null, 2), WriteMode.Atomic);
  return defaults;
}

async function saveConfig(value: AppConfig): Promise<void> {
  await config.writeString(JSON.stringify(value, null, 2), WriteMode.Atomic);
}

try {
  const value = await loadConfig();
  value.theme = 'dark';
  await saveConfig(value);
} finally {
  fs.close();
}
```

### Concurrent batch processing

```typescript
import { FS, HashAlgorithm, openFS, request } from 'react-native-io';

const fs = openFS(4);
const imageDir = fs.directory(FS.joinPaths(FS.cacheDir, 'images'));

try {
  await imageDir.create(true);

  const urls = [
    'https://example.com/img1.jpg',
    'https://example.com/img2.jpg',
    'https://example.com/img3.jpg',
    'https://example.com/img4.jpg',
  ];

  await Promise.all(
    urls.map((url, index) =>
      request.download(url, imageDir.childPath(`image-${index}.jpg`))
    )
  );

  const entries = await imageDir.listFiles();
  const hashes = await Promise.all(
    entries.map((entry) => fs.file(entry.path).calcHash(HashAlgorithm.SHA256))
  );

  console.log(hashes);
} finally {
  fs.close();
}
```

### Append-only log handle

```typescript
import { FS, FileOpenMode, openFS } from 'react-native-io';

const fs = openFS();
const handle = fs.open(
  FS.joinPaths(FS.cacheDir, 'app.log'),
  FileOpenMode.Append
);

async function log(message: string): Promise<void> {
  await handle.writeLine(`[${new Date().toISOString()}] ${message}`);
}

try {
  await log('App started');
  await log('User logged in');
  await log('Data loaded');
  await handle.flush();
} finally {
  await handle.closeAsync();
  fs.close();
}
```

### Download, verify, and upload

```typescript
import { File, FS, HashAlgorithm, request } from 'react-native-io';

const temporaryPath = FS.joinPaths(FS.cacheDir, 'package.tmp');
const temporary = new File(temporaryPath);

try {
  const downloaded = await request.download(downloadUrl, temporaryPath, {
    timeout: 60_000,
  });
  if (!downloaded.ok) {
    throw new Error(downloaded.error ?? `HTTP ${downloaded.status}`);
  }

  const actualHash = await temporary.calcHash(HashAlgorithm.SHA256);
  if (actualHash !== expectedHash) {
    throw new Error('Hash verification failed');
  }

  const uploaded = await request.upload(uploadUrl, temporaryPath, {
    fieldName: 'package',
    fileName: 'package.zip',
    mimeType: 'application/zip',
  });
  if (!uploaded.ok) {
    throw new Error(uploaded.error ?? `HTTP ${uploaded.status}`);
  }

  console.log(uploaded.responseText);
} finally {
  if (await temporary.exists()) {
    await temporary.delete();
  }
}
```
