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
        return name === 'dispose' || name.endsWith('Sync')
          ? undefined
          : Promise.resolve();
      },
  }
);
const native = {
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
  console.log(
    'PASS: closed contexts invalidate children, thread limits, shared default worker, repeated async close, falsy JSON bodies'
  );
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
