# react-native-io

<div align="center">

**A high-performance React Native TurboModule powered by pure C++20**

File systems · Streaming I/O · File hashing · HTTP · Android, iOS, and HarmonyOS

[![npm version](https://img.shields.io/npm/v/react-native-io.svg)](https://www.npmjs.com/package/react-native-io)
[![license](https://img.shields.io/npm/l/react-native-io.svg)](https://github.com/arcticfox1919/react-native-io/blob/main/LICENSE)

</div>

`react-native-io` provides straightforward, type-safe file and network APIs for React Native. Its file, directory, `FileHandle`, hashing, and JSI binding layers are implemented in pure C++20 and exposed through a TurboModule. Asynchronous file operations run on native worker pools and do not block the JavaScript thread. HTTP operations use the system networking stack on Android, iOS, and HarmonyOS.

This library requires React Native 0.84 or later with TurboModules and JSI. Development and the example use React Native 0.84.1, React 19.2.3, and Node.js 22.11 or later. The legacy Bridge architecture is not supported.

## Features

| Area            | Capabilities                                                             |
| --------------- | ------------------------------------------------------------------------ |
| Files           | Text and binary I/O, append, copy, move, delete, and metadata            |
| Directories     | Create, list, move, recursive deletion, and size calculation             |
| Streaming I/O   | Reuse an open file for read, write, seek, flush, and truncate operations |
| Safe writes     | Overwrite, append, atomic replacement, and durable atomic replacement    |
| File hashing    | MD5, SHA-1, SHA-2, SHA-3, Keccak, and CRC32                              |
| HTTP            | GET, POST, PUT, PATCH, DELETE, downloads, and multipart uploads          |
| Platform paths  | Cache, documents, temporary, downloads, external, and bundle directories |
| Execution modes | Promise-based asynchronous APIs and synchronous APIs                     |

## Installation

```sh
yarn add react-native-io
```

or:

```sh
npm install react-native-io
```

Install iOS pods after adding the package:

```sh
cd ios
pod install
```

Ensure that the host application has enabled the React Native New Architecture. The package supports autolinking and does not require manual module registration.

## HarmonyOS support

HarmonyOS uses the same pure C++20 TurboModule core for files, directories,
streaming, hashing, and encoding. HTTP uses Huawei's Remote Communication Kit.
Use the same JavaScript imports and APIs across all three platforms.

HarmonyOS 5.0.4 (API 16) or later is required. The example uses React Native
0.84.1 and RNOH/Harmony CLI 0.84.3. The npm package includes a source HAR and
supports official RNOH autolinking.

See the **[HarmonyOS guide](harmony/README.md)** for setup, directory mappings,
HTTP differences, and the example app.

## Quick start

The following example writes text into the application documents directory and reads it back:

```typescript
import { File, FS } from 'react-native-io';

const file = new File(FS.joinPaths(FS.documentDir, 'hello.txt'));

await file.writeString('Hello from C++20');
const content = await file.readString();

console.log(content);
```

Common platform paths are available through `FS`:

```typescript
FS.cacheDir; // Cache that may be removed by the operating system
FS.documentDir; // Persistent application documents
FS.tempDir; // Temporary files
FS.downloadDir; // Application-specific downloads
FS.externalDir; // Android external app directory; documentDir on iOS
FS.externalCacheDir; // Android external cache; cacheDir on iOS
FS.bundleDir; // iOS application bundle; undefined on Android
```

## File operations

```typescript
import { File, FS, WriteMode } from 'react-native-io';

const file = new File(FS.joinPaths(FS.documentDir, 'config.json'));

await file.writeString(JSON.stringify({ theme: 'dark' }));

if (await file.exists()) {
  console.log(await file.readString());
  console.log('size:', await file.size());
  console.log('modified:', await file.modifiedTime());
}

await file.appendString('\n');
await file.writeString('new content', WriteMode.Atomic);

const copy = await file.copy(
  FS.joinPaths(FS.documentDir, 'config.backup.json')
);
await copy.delete();
```

Binary methods accept and return `ArrayBuffer` values:

```typescript
const file = new File(FS.joinPaths(FS.cacheDir, 'data.bin'));
const input = new Uint8Array([0, 1, 2, 255]);

await file.writeBytes(input);

const output = new Uint8Array(await file.readBytes());
console.log(output);
```

### Write modes

| Mode                      | Behavior                                                                               |
| ------------------------- | -------------------------------------------------------------------------------------- |
| `WriteMode.Overwrite`     | Replaces the file contents. This is the default.                                       |
| `WriteMode.Append`        | Appends data to the end of the file.                                                   |
| `WriteMode.Atomic`        | Writes a temporary file and atomically replaces the destination.                       |
| `WriteMode.AtomicDurable` | Performs an atomic replacement and requests file and parent-directory synchronization. |

Use `Atomic` for configuration, indexes, and other files that must never expose partially written contents. Use `AtomicDurable` only when stronger power-loss durability is required because synchronization can add significant latency.

## Directory operations

```typescript
import { Directory, EntityType, FS } from 'react-native-io';

const dir = new Directory(FS.joinPaths(FS.documentDir, 'logs'));
await dir.create(true);

const entries = await dir.list();
for (const entry of entries) {
  const type = entry.type === EntityType.File ? 'file' : 'directory';
  console.log(type, entry.name, entry.size);
}

const files = await dir.listFiles();
const subdirectories = await dir.listDirectories();
const totalBytes = await dir.getTotalSize(true);

console.log(files, subdirectories, totalBytes);
```

Recursive deletion returns the number of deleted entries:

```typescript
const deletedCount = await dir.delete(true);
```

## Worker pools and `openFS()`

Creating a worker pool for every path object would quickly create unnecessary threads. The following example illustrates the problem with that design:

```typescript
// ❌ Problem: Multiple standalone instances = multiple thread pools
const file1 = new File('/path/file1.txt'); // Creates thread pool #1
const file2 = new File('/path/file2.txt'); // Creates thread pool #2
const dir = new Directory('/path/dir'); // Creates thread pool #3
```

`react-native-io` does not create pools this way. Standalone `File` and `Directory` objects share one lazily started default worker:

```typescript
// ✅ These objects share one lazy default worker.
const file1 = new File('/path/file1.txt');
const file2 = new File('/path/file2.txt');
const dir = new Directory('/path/dir');
```

Use standalone objects for normal single-file operations. Use `openFS()` when a batch needs an isolated lifetime, controlled concurrency, or a shared context for several objects:

```typescript
import { openFS } from 'react-native-io';

const fs = openFS(4);

try {
  const files = paths.map((path) => fs.file(path));
  const contents = await Promise.all(files.map((file) => file.readString()));
  console.log(contents);
} finally {
  fs.close();
}
```

- `openFS()` uses one worker by default.
- `openFS(n)` accepts between 1 and 32 workers.
- Workers start on the first asynchronous file operation. Sync-only use does not start them.
- `close()` rejects new operations immediately and lets already submitted operations finish.
- Pool destruction continues in the background, so `close()` does not block the JavaScript thread.
- Calling `close()` more than once is safe.

More threads are not always faster. Keep the default for sequential I/O and small batches. Increase the worker count only for independent parallel operations after measuring the workload on target devices.

## `FileHandle` streaming

`FileHandle` keeps a file open across multiple operations, avoiding repeated open and close calls:

```typescript
import { FileOpenMode, FS, openFS } from 'react-native-io';

const fs = openFS();
const handle = fs.open(
  FS.joinPaths(FS.documentDir, 'events.log'),
  FileOpenMode.AppendRead
);

try {
  await handle.writeLine('app started');
  await handle.flush();
  await handle.rewind();

  while (!(await handle.isEOF())) {
    console.log(await handle.readLine());
  }
} finally {
  await handle.closeAsync();
  fs.close();
}
```

Operations on the same handle are serialized for safety. Await dependent operations in the required order. Prefer `closeAsync()`; use synchronous `close()` only when no operation can still be in flight.

## File hashing

Hashing streams the file in native code and does not load the complete file into JavaScript memory:

```typescript
import { File, FS, HashAlgorithm } from 'react-native-io';

const archive = new File(FS.joinPaths(FS.documentDir, 'archive.zip'));

const sha256 = await archive.calcHash(HashAlgorithm.SHA256);
const crc32 = await archive.calcHash(HashAlgorithm.CRC32);

console.log({ sha256, crc32 });
```

The following example verifies a downloaded file before moving it into its final location:

```typescript
const temporary = new File(FS.joinPaths(FS.cacheDir, 'package.tmp'));
const expectedSha256 = 'f00d...';

if ((await temporary.calcHash(HashAlgorithm.SHA256)) !== expectedSha256) {
  await temporary.delete();
  throw new Error('Downloaded file failed SHA-256 verification');
}

await temporary.move(FS.joinPaths(FS.documentDir, 'package.zip'));
```

Available algorithms are `MD5`, `SHA1`, `SHA256`, `SHA3_224`, `SHA3_256`, `SHA3_384`, `SHA3_512`, `Keccak224`, `Keccak256`, `Keccak384`, `Keccak512`, and `CRC32`.

## HTTP requests

### GET and response parsing

```typescript
import { request } from 'react-native-io';

const response = await request.get('https://api.example.com/users', {
  headers: { Authorization: 'Bearer token' },
  timeout: 15_000,
});

if (response.ok) {
  const users = response.json<Array<{ id: number; name: string }>>();
  console.log(users);
} else {
  console.warn(response.status, response.error);
}
```

Use `response.text()` for UTF-8 text, `response.json<T>()` for JSON, and `response.arrayBuffer()` for binary content.

### JSON, text, and binary request bodies

```typescript
const jsonResponse = await request.post('https://api.example.com/users', {
  json: { name: 'Narwal', enabled: true },
});

const textResponse = await request.put('https://api.example.com/config', {
  body: 'mode=production',
  headers: { 'Content-Type': 'text/plain' },
});

const binaryResponse = await request.post('https://api.example.com/binary', {
  data: new Uint8Array([1, 2, 3]).buffer,
  headers: { 'Content-Type': 'application/octet-stream' },
});
```

The client also provides `patch`, `delete`, `head`, and `options` methods.

### File downloads

```typescript
import { FS, request } from 'react-native-io';

const destination = FS.joinPaths(FS.downloadDir, 'manual.pdf');
const result = await request.download(
  'https://example.com/manual.pdf',
  destination,
  { timeout: 60_000 }
);

if (!result.ok) {
  throw new Error(result.error ?? `Download failed with ${result.status}`);
}

console.log(result.filePath, result.fileSize);
```

Android resumable downloads require both `resumable: true` and an `If-Range` request header that identifies the remote entity:

```typescript
await request.download(url, destination, {
  resumable: true,
  headers: { 'If-Range': etag },
});
```

Without a validator, the download restarts from the beginning. iOS downloads also restart rather than resume.

### Multipart file uploads

```typescript
const result = await request.upload(
  'https://api.example.com/upload',
  FS.joinPaths(FS.documentDir, 'photo.jpg'),
  {
    fieldName: 'image',
    fileName: 'profile.jpg',
    mimeType: 'image/jpeg',
    headers: { Authorization: 'Bearer token' },
    formFields: {
      category: 'profile',
      description: 'User profile photo',
    },
  }
);

if (!result.ok) {
  throw new Error(result.error ?? `Upload failed with ${result.status}`);
}
```

## Asynchronous and synchronous APIs

Most file APIs provide both asynchronous and synchronous variants:

```typescript
await file.writeString('async'); // Runs on a native worker and returns a Promise
file.writeStringSync('sync'); // Calls C++ directly and finishes before returning
```

Both variants use the same C++ file operations. The difference is where the work
runs and how the result returns to JavaScript.

| Aspect            | Synchronous (`Sync`)                                      | Asynchronous (`await`)                                               |
| ----------------- | --------------------------------------------------------- | -------------------------------------------------------------------- |
| Execution         | C++ runs directly on the calling JS thread                | I/O runs on a native worker                                          |
| Scheduling cost   | No worker queue, thread handoff, or Promise completion    | Queues the task and delivers its result back to JS through a Promise |
| Return value      | The result is available immediately when the call returns | The result is available when the Promise settles                     |
| JS responsiveness | JS waits until the operation finishes                     | JS can run other work while native I/O is in progress                |

### When synchronous calls can be faster

For a few small operations, such as checking whether a file exists, reading its
size, creating a directory, or reading and writing a tiny configuration file,
thread scheduling can cost more than the file operation itself. The `Sync`
variants skip that scheduling and Promise overhead, so they can finish sooner.
Sync-only file access also does not start worker threads; workers start when
the first asynchronous file task is submitted.

For example, a small local settings file can be created and read synchronously:

```typescript
import { Directory, File, FS } from 'react-native-io';

const settingsDir = new Directory(FS.joinPaths(FS.documentDir, 'settings'));
settingsDir.createSync();

const settings = new File(FS.joinPaths(settingsDir.path, 'preferences.json'));
settings.writeStringSync(JSON.stringify({ theme: 'dark' }));

console.log(settings.existsSync());
console.log(settings.sizeSync());
console.log(settings.readStringSync());
```

The corresponding asynchronous calls use the same paths and data:

```typescript
await settingsDir.create();
await settings.writeString(JSON.stringify({ theme: 'dark' }));

console.log(await settings.exists());
console.log(await settings.size());
console.log(await settings.readString());
```

### When to use asynchronous calls

Use asynchronous APIs for large reads and writes, file copies, hashing large
files, directory traversal, recursive deletion, and batches. They keep the JS
thread available while the native work runs.

Choose based on the total time spent blocking JS. A few quick synchronous calls
can be a good fit for small setup tasks; thousands of those calls in a loop can
still delay rendering and input. A busy disk or a cold cache can also make a
small operation take longer, so synchronous calls are not guaranteed to be faster.

## Paths, storage, and encoding

```typescript
const path = FS.joinPaths(FS.documentDir, 'data', 'file.txt');

const available = await FS.getAvailableSpace(FS.documentDir);
const total = await FS.getTotalSpace(FS.documentDir);

const bytes = FS.encodeString('Hello', 'utf8');
const text = FS.decodeString(bytes, 'utf8');
```

String encoding supports `utf8`, `ascii`, and `latin1`.

## API reference

See the **[complete API reference](docs/API.md)** for every public class,
method, option, enum, return type, lifecycle rule, and extended example.

| Object       | Common APIs                                                                                                   |
| ------------ | ------------------------------------------------------------------------------------------------------------- |
| `File`       | `exists`, `readString`, `readBytes`, `writeString`, `writeBytes`, `copy`, `move`, `delete`, `calcHash`        |
| `Directory`  | `create`, `list`, `listFiles`, `listDirectories`, `move`, `delete`, `getTotalSize`                            |
| `FileHandle` | `read`, `readString`, `readLine`, `write`, `writeString`, `seek`, `rewind`, `flush`, `truncate`, `closeAsync` |
| `FSContext`  | `file`, `directory`, `open`, `close`                                                                          |
| `FS`         | Platform paths, `joinPaths`, storage information, and string encoding                                         |
| `request`    | `get`, `post`, `put`, `patch`, `delete`, `head`, `options`, `download`, `upload`                              |

Asynchronous methods generally have no suffix. Synchronous methods end in `Sync`. TypeScript declarations provide the complete parameter and return types.

## Usage guidelines

- Prefer application sandbox paths such as `FS.documentDir` and `FS.cacheDir`.
- Process large files in chunks through `FileHandle` instead of creating large JavaScript and native buffers.
- Use `WriteMode.Atomic` when readers must never observe a partial replacement.
- Await operations sequentially when work on the same file requires a defined order.
- Call `close()` after finishing with an explicitly created `FSContext`.
- Await `closeAsync()` after finishing with an explicitly opened `FileHandle`.
- Use `download()` for large HTTP payloads instead of retaining complete response bodies in memory.

## Implementation

- The file system, `FileHandle`, hashing algorithms, and JSI/TurboModule core use pure C++20.
- JSI transfers calls and binary results directly without the legacy React Native Bridge.
- Asynchronous file operations use bounded native worker pools.
- Standalone `File` and `Directory` objects share one default worker.
- Android HTTP uses the platform Java networking stack. iOS HTTP uses Foundation networking.
- HarmonyOS HTTP uses the official Remote Communication Kit C API with system TLS validation.
- Native buffers and queued asynchronous payloads have explicit safety limits. Calls fail with an error when a limit is exceeded instead of allocating without bounds.

See [Release performance data](example/reports/README.md) for measured results and [example testing](example/TESTING.md) for the test suite.

## License

MIT
