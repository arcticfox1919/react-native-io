'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const ts = require('typescript');
const vm = require('node:vm');
const filename = path.join(__dirname, '../src/quality/engine.ts');
const source = ts.transpileModule(fs.readFileSync(filename, 'utf8'), {
  compilerOptions: {
    module: ts.ModuleKind.CommonJS,
    target: ts.ScriptTarget.ES2022,
  },
}).outputText;
const instance = { exports: {} };
vm.runInNewContext(source, {
  exports: instance.exports,
  module: instance,
  setTimeout,
  Date,
  Error,
  Uint8Array,
  Number,
  Math,
});
const e = instance.exports;
(async () => {
  const d = e.distribution([4, 1, 3, 2, 100], 1024 * 1024);
  assert.equal(d.medianMs, 3);
  assert.equal(d.p95Ms, 100);
  assert.equal(d.totalMs, 110);
  assert.equal(d.throughputMiBps, 5000 / 110);
  assert.throws(() => e.distribution([]));
  assert.throws(() => e.distribution([NaN]));
  assert.deepEqual(
    new Uint8Array(e.pattern(4097, 123)),
    new Uint8Array(e.pattern(4097, 123))
  );
  assert.notDeepEqual(
    new Uint8Array(e.pattern(4097, 123)),
    new Uint8Array(e.pattern(4097, 124))
  );
  assert.throws(() =>
    e.equalBytes(new Uint8Array([1]).buffer, new Uint8Array([2]).buffer)
  );
  let active = 0,
    maximum = 0,
    completed = 0;
  await e.parallel(73, 7, async () => {
    active++;
    maximum = Math.max(maximum, active);
    await new Promise((resolve) => setTimeout(resolve, 1));
    active--;
    completed++;
  });
  assert.equal(completed, 73);
  assert.equal(active, 0);
  assert.equal(maximum, 7);
  let drained = false;
  await assert.rejects(
    () =>
      e.parallel(10, 2, async (i) => {
        if (i === 0) {
          await new Promise((resolve) => setTimeout(resolve, 1));
          throw Error('injected');
        }
        await new Promise((resolve) => setTimeout(resolve, 10));
        drained = true;
      }),
    /injected/
  );
  assert.equal(drained, true, 'parallel failure must wait for started work');
  await assert.rejects(
    () =>
      e.parallel(2, 2, async (i) => {
        await new Promise((resolve) => setTimeout(resolve, i === 0 ? 1 : 10));
        throw Error(i === 0 ? 'first failure' : 'later failure');
      }),
    /first failure/
  );
  await assert.rejects(() => e.parallel(1, 0, async () => {}));
  await e.mustReject(() => {
    throw Error('sync');
  }, 'sync');
  await e.mustReject(() => Promise.reject(Error('async')), 'async');
  await assert.rejects(() =>
    e.mustReject(() => Promise.resolve(), 'false positive')
  );
  assert.throws(
    () => e.checkpoint(() => true, Date.now() + 10000),
    /Cancelled|deadline/
  );
  assert.throws(
    () => e.checkpoint(() => false, Date.now() - 1),
    /Cancelled|deadline/
  );
  console.log(
    'PASS: example statistics, deterministic patterns, bounded concurrency, failure drain, cancellation and deadlines'
  );
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
