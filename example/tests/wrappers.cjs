const assert = require('node:assert/strict');
const Module = require('node:module');
const fs = require('node:fs');
const ts = require('typescript');
for (const ext of ['.ts', '.tsx'])
  require.extensions[ext] = (module, filename) => {
    module._compile(
      ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
        compilerOptions: {
          module: ts.ModuleKind.CommonJS,
          target: ts.ScriptTarget.ES2022,
        },
      }).outputText,
      filename
    );
  };
let creates = 0;
const calls = [];
const nativeFS = new Proxy(
  {},
  {
    get:
      (_target, name) =>
      (...args) => {
        calls.push([name, ...args]);
        if (name === 'joinPaths') return args.join('/');
        return name === 'dispose' || name.endsWith('Sync')
          ? undefined
          : Promise.resolve();
      },
  }
);
const platform = {
  platform: 'harmony',
  filesDir: '/harmony/files',
  documentsDir: '/harmony/files',
  cacheDir: '/harmony/cache',
  tempDir: '/harmony/temp',
};
const native = {
  createPlatform: () => platform,
  createFileSystem: () => {
    creates++;
    return nativeFS;
  },
  installHttpClient() {},
  createIORequest: () => ({
    request: async (...args) => {
      calls.push(['request', ...args]);
      return {
        success: true,
        statusCode: 200,
        body: new ArrayBuffer(0),
        headerKeys: [],
        headerValues: [],
      };
    },
  }),
};
const originalLoad = Module._load;
Module._load = function (name, ...args) {
  return name === 'react-native'
    ? { TurboModuleRegistry: { getEnforcing: () => native } }
    : originalLoad.call(this, name, ...args);
};
(async () => {
  const { openFS } = require('../../src/FSContext.ts');
  const { File } = require('../../src/File.ts');
  const { FileHandle } = require('../../src/FileHandle.ts');
  const { Directory } = require('../../src/Directory.ts');
  const { Request } = require('../../src/Request.ts');
  for (const threads of [0, -1, NaN, Infinity, 1.5, 33])
    assert.throws(() => openFS(threads), RangeError);
  const context = openFS();
  const file = context.file('/tmp/file');
  const dir = context.directory('/tmp/dir');
  await file.writeString('log');
  context.close();
  assert.equal(calls.filter(([name]) => name === 'dispose').length, 1);
  context.close();
  assert.equal(
    calls.filter(([name]) => name === 'dispose').length,
    1,
    'close must be idempotent'
  );
  assert.throws(() => file.readString(), /closed/);
  assert.throws(() => dir.exists(), /closed/);
  assert.throws(() => context.file('/tmp/next'), /closed/);
  const before = creates;
  new File('/tmp/a').existsSync();
  new File('/tmp/b').existsSync();
  new Directory('/tmp/c').existsSync();
  assert.equal(creates - before, 1, 'standalone paths share one worker');
  let finishClose;
  const closeFS = {
    openFile: () => 7,
    fileCloseAsync: () =>
      new Promise((resolve) => {
        finishClose = resolve;
      }),
  };
  const handle = new FileHandle(closeFS, '/tmp/handle', 0);
  const firstClose = handle.closeAsync();
  let secondCloseFinished = false;
  const secondClose = handle.closeAsync().then(() => {
    secondCloseFinished = true;
  });
  await Promise.resolve();
  assert.equal(
    secondCloseFinished,
    false,
    'repeated closeAsync calls must wait for the native close'
  );
  finishClose();
  await Promise.all([firstClose, secondClose]);
  const request = new Request();
  for (const value of [false, 0, '', null]) {
    await request.post('https://test', { json: value });
    assert.equal(
      calls.at(-1)[4],
      JSON.stringify(value),
      'falsy JSON body must be sent'
    );
  }
  const { FS, Platform, IOPlatformType } = require('../../src/index.tsx');
  assert.equal(Platform.platform, IOPlatformType.Harmony);
  assert.equal(FS.documentDir, '/harmony/files');
  assert.equal(FS.tempDir, '/harmony/temp');
  assert.equal(FS.externalDir, '/harmony/files');
  assert.equal(FS.externalCacheDir, '/harmony/cache');
  assert.equal(FS.downloadDir, '/harmony/files/Download');
  assert.equal(FS.bundleDir, undefined);
  Object.assign(platform, {
    platform: 'android',
    filesDir: '/android/files',
    documentsDir: '/android/external/Documents',
    cacheDir: '/android/cache',
    downloadsDir: '/android/external/Download',
  });
  assert.equal(FS.documentDir, '/android/files');
  assert.equal(FS.tempDir, '/android/cache');
  assert.equal(FS.downloadDir, '/android/external/Download');
  assert.equal(Platform.documentsDir, '/android/external/Documents');
  Object.assign(platform, {
    platform: 'ios',
    documentsDir: '/ios/Documents',
    tempDir: '/ios/tmp',
    bundleDir: '/ios/App.app',
  });
  assert.equal(FS.documentDir, '/ios/Documents');
  assert.equal(FS.tempDir, '/ios/tmp');
  assert.equal(FS.bundleDir, '/ios/App.app');
  console.log(
    'PASS: context lifecycle, thread limits, async close, JSON bodies, Android/iOS/HarmonyOS directory mappings'
  );
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
