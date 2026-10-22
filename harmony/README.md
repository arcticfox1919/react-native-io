# HarmonyOS guide

Use the same JavaScript imports and APIs on Android, iOS, and HarmonyOS.
HarmonyOS runs the shared C++20 file, directory, streaming, hashing, and encoding
code through RNOH. HTTP uses Huawei's Remote Communication Kit with system
certificate validation.

## Requirements

- HarmonyOS 5.0.4 (API 16) or later.
- An RNOH application with official autolinking enabled.
- Huawei's HarmonyOS SDK with Remote Communication Kit.

The example uses React Native 0.84.1, React 19.2.3, RNOH/Harmony CLI 0.84.3,
and Node.js 22.11 or later. It targets API 24 and supports API 16 or later.

## Add the library to your app

Run these commands from your React Native project:

```sh
yarn add react-native-io
npx react-native link-harmony --harmony-project-path ./harmony
cd harmony
ohpm install --all
```

If your native project directory is named `ohos`, use `./ohos` and `cd ohos`
instead. Set `compatibleSdkVersion` to `"5.0.4(16)"` or later in the native
project's `build-profile.json5`.

For HTTP requests, add `ohos.permission.INTERNET` to `module.requestPermissions`
in the app's `entry/src/main/module.json5`:

```json
{
  "name": "ohos.permission.INTERNET"
}
```

The npm package includes `harmony/io.har`. This archive contains the native
source code, which your app compiles with its own RNOH runtime. Autolinking
registers the module for you.

## Use it

```typescript
import { File, FS, HashAlgorithm, request } from 'react-native-io';

const file = new File(FS.joinPaths(FS.documentDir, 'settings.json'));
await file.writeString(JSON.stringify({ enabled: true }));
console.log(await file.readString());
console.log(await file.calcHash(HashAlgorithm.SHA256));

const response = await request.get('https://api.example.com/status');
console.log(response.status, response.text());
```

See the [API reference](../docs/API.md) for all methods and options.

## Where files are stored

| API                   | HarmonyOS location       |
| --------------------- | ------------------------ |
| `FS.documentDir`      | The app's `filesDir`     |
| `FS.cacheDir`         | The app's `cacheDir`     |
| `FS.tempDir`          | The app's `tempDir`      |
| `FS.downloadDir`      | `<filesDir>/Download`    |
| `FS.externalDir`      | Same as `FS.documentDir` |
| `FS.externalCacheDir` | Same as `FS.cacheDir`    |
| `FS.bundleDir`        | `undefined`              |

These are private app directories. Pass filesystem paths to the APIs, rather
than `file://` or `datashare://` URIs. `FS.getPlatform().platform` returns
`"harmony"`; its `documentsDir` is an alias for `filesDir`.

## HTTP differences

- Downloads start from byte zero, including when `resumable` is enabled.
- Upload paths and filenames are limited to 127 UTF-8 bytes each; MIME types
  are limited to 63 bytes. Longer values return an error.
- Multipart uploads do not follow redirects. Other requests honor
  `followRedirects`.
- `Response.statusText` is empty. Use `status` and `ok` to check the response.
- `onProgress` callbacks are not supported. `Request.dispose()` does not
  cancel a transfer already in progress.

## Try the example

From this repository's root directory:

```sh
yarn install --immutable
yarn example build:harmony
```

This builds the library HAR, installs native dependencies, and builds a Release
HAP with an embedded Hermes bundle. Open `example/harmony` in DevEco Studio
to configure signing for your device. The app provides API examples and
Quick, Full, and Soak tests.

See [HarmonyOS testing](../example/TESTING.md#14-harmonyos-validation) for
installation, Metro setup, and local HTTP tests.

## Maintainer build

To rebuild only the library HAR, run from the repository root:

```sh
cd harmony
ohpm install --all
cd ..
yarn harmony:har
```

Edit shared C++ code under `cpp/`. The build copies it into the generated
`harmony/io/src/main/cpp/io-core/` directory and creates `harmony/io.har`.

The scripts use `/Applications/DevEco-Studio.app/Contents` by default. Set
`DEVECO_HOME` if DevEco Studio is installed elsewhere. For a custom native SDK
layout, set CMake's `IO_HMS_NATIVE` to its `hms/native` directory.

Update `version` in `package.json` manually before running `yarn release`.
The release command keeps that version and rebuilds the HAR before publishing.
A failed build stops the release. For direct `npm pack` or `npm publish`, run
`yarn harmony:har` first. `prepack` only checks that the HAR exists.
