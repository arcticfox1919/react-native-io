import {
  FS,
  FileOpenMode,
  SeekOrigin,
  HashAlgorithm,
  WriteMode,
  openFS,
  request,
} from 'react-native-io';
import {
  check,
  mustReject,
  pattern,
  equalBytes,
  parallel,
  distribution,
} from './engine';
import { TEST_NATIVE_BUFFER_LIMIT_BYTES } from './config';
import type { Case, CaseContext } from './runner';

const sizes = [
  0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 255, 256, 257, 4095, 4096, 4097, 65535,
  65536, 65537, 1048575, 1048576, 1048577,
];
async function benchmark(
  c: CaseContext,
  label: string,
  bytes: number,
  action: () => Promise<void>
): Promise<void> {
  for (let i = 0; i < c.config.warmup; ++i) {
    c.check();
    await action();
  }
  const rawMs: number[] = [];
  for (let i = 0; i < c.config.samples; ++i) {
    c.check();
    const start = performance.now();
    await action();
    rawMs.push(performance.now() - start);
  }
  c.measure({
    label,
    bytesPerSample: bytes,
    rawMs,
    summary: distribution(rawMs, bytes),
  });
}
export function makeCases(networkURL?: string): Case[] {
  const fixture = networkURL?.replace(/\/$/, '');
  const networkSkip = fixture
    ? undefined
    : 'No local fixture URL configured; network coverage was NOT executed';
  return [
    {
      name: 'File/directory CRUD, sync/async parity and metadata',
      group: 'functional',
      async run(c) {
        const f = c.fs.file(`${c.root}/nested/中文😀.txt`);
        await f.writeString('first');
        await f.appendString('second');
        check(f.readStringSync() === 'firstsecond', 'append mismatch');
        f.createSync();
        check(
          (await f.readString()) === 'firstsecond',
          'create truncated existing file'
        );
        const copy = await f.copy(`${c.root}/copy`);
        check((await copy.readString()) === 'firstsecond', 'copy mismatch');
        const moved = await copy.move(`${c.root}/moved`);
        check((await moved.exists()) && !(await copy.exists()), 'move state');
        const metadata = await moved.metadata();
        check(metadata.size === 11, 'metadata size');
        check(
          Math.abs(Date.now() - (await moved.modifiedTime()).getTime()) < 60000,
          'timestamp must be Unix milliseconds'
        );
        check(
          (await c.fs.directory(c.root).list(true)).length >= 3,
          'recursive list incomplete'
        );
        await moved.delete();
        check(!moved.existsSync(), 'delete failed');
      },
    },
    {
      name: 'UTF-8, embedded NUL content and native encoding',
      group: 'functional',
      async run(c) {
        for (const text of ['', 'ASCII', '中文😀é\u0000\r\n']) {
          const f = c.fs.file(`${c.root}/text`);
          await f.writeString(text);
          check((await f.readString()) === text, 'UTF-8 round trip');
          check(
            FS.decodeString(FS.encodeString(text)) === text,
            'native encode/decode round trip'
          );
        }
      },
    },
    {
      name: 'Hash known-answer vectors (not just non-empty output)',
      group: 'functional',
      async run(c) {
        const f = c.fs.file(`${c.root}/hash`);
        await f.writeString('abc');
        const vectors: [HashAlgorithm, string][] = [
          [HashAlgorithm.MD5, '900150983cd24fb0d6963f7d28e17f72'],
          [HashAlgorithm.SHA1, 'a9993e364706816aba3e25717850c26c9cd0d89d'],
          [
            HashAlgorithm.SHA256,
            'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
          ],
          [
            HashAlgorithm.SHA3_256,
            '3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532',
          ],
          [HashAlgorithm.CRC32, '352441c2'],
        ];
        for (const [algorithm, expected] of vectors) {
          check(
            (await f.calcHash(algorithm)) === expected,
            `hash vector ${algorithm}`
          );
          check(
            (await f.calcHash(algorithm)) === expected,
            `repeat hash vector ${algorithm}`
          );
        }
      },
    },
    {
      name: 'Binary length/alignment boundaries and exact content',
      group: 'boundary',
      async run(c) {
        for (const size of sizes) {
          if (
            size > c.config.maxBytes ||
            (c.config.profile === 'quick' && size > 65537)
          )
            continue;
          c.check();
          const bytes = pattern(size, c.config.seed + size),
            f = c.fs.file(`${c.root}/binary`);
          await f.writeBytes(bytes);
          equalBytes(await f.readBytes(), bytes);
          f.writeBytesSync(bytes);
          equalBytes(f.readBytesSync(), bytes);
        }
      },
    },
    {
      name: 'Invalid arguments, empty/NUL paths, enum and integer boundaries',
      group: 'boundary',
      async run(c) {
        for (const path of ['', `${c.root}/bad\u0000path`]) {
          await mustReject(() => c.fs.file(path).readString(), 'invalid path');
          await mustReject(
            () => c.fs.file(path).writeStringSync('bad'),
            'invalid sync path'
          );
        }
        for (const threads of [0, -1, 1.5, NaN, Infinity, 33])
          await mustReject(() => openFS(threads), 'worker count');
        const f = c.fs.file(`${c.root}/valid`);
        await f.writeString('abc');
        await mustReject(
          () => f.writeString('bad', 4 as WriteMode),
          'write mode'
        );
        const h = c.fs.open(f.path, FileOpenMode.Read);
        try {
          for (const value of [
            -2,
            0.5,
            NaN,
            Infinity,
            Number.MAX_SAFE_INTEGER + 1,
          ]) {
            await mustReject(() => h.read(value), `read size ${value}`);
            await mustReject(() => h.seek(value), `seek ${value}`);
          }
        } finally {
          h.close();
        }
      },
    },
    {
      name: 'EOF, 64-bit seek, line endings, update-stream transitions',
      group: 'boundary',
      async run(c) {
        const f = c.fs.file(`${c.root}/stream`);
        await f.writeString('a\r\nb\rc\nlast');
        const h = c.fs.open(f.path, FileOpenMode.ReadWrite);
        try {
          check((await h.readLine()) === 'a', 'CRLF');
          check((await h.readLine()) === 'b\rc', 'embedded CR');
          check((await h.readLine()) === 'last', 'unterminated line');
          check((await h.readLine()) === '', 'EOF line');
          await h.seek(2 ** 31, SeekOrigin.Begin);
          check((await h.getPosition()) === 2 ** 31, '64-bit seek narrowed');
          check((await h.read()).byteLength === 0, 'read beyond EOF');
          await h.rewind();
          check((await h.read(0)).byteLength === 0, 'zero read');
          check((await h.readString(1)) === 'a', 'initial read');
          await h.writeString('Z');
          await h.rewind();
          check((await h.readString(2)) === 'aZ', 'read/write transition');
        } finally {
          h.close();
        }
        h.close();
        await mustReject(() => h.read(), 'closed handle');
      },
    },
    {
      name: 'Flush, truncate, append/read mode and filesystem space',
      group: 'functional',
      async run(c) {
        const f = c.fs.file(`${c.root}/truncate`);
        await f.writeString('abcdef');
        const h = c.fs.open(f.path, FileOpenMode.ReadWrite);
        try {
          await h.seek(3);
          await h.truncate();
          await h.flush();
          check((await h.getSize()) === 3, 'truncate size');
        } finally {
          h.close();
        }
        check((await f.readString()) === 'abc', 'truncate content');
        const append = c.fs.open(f.path, FileOpenMode.AppendRead);
        try {
          await append.seek(0);
          await append.writeString('Z');
          await append.flush();
          await append.rewind();
          check(
            (await append.readString()) === 'abcZ',
            'append must ignore seek for writes'
          );
        } finally {
          append.close();
        }
        const available = await FS.getAvailableSpace(c.root),
          total = await FS.getTotalSpace(c.root);
        check(
          available >= 0 && total > 0 && available <= total,
          'filesystem capacity'
        );
      },
    },
    {
      name: 'Atomic string/binary/durable replacements and target type rejection',
      group: 'functional',
      async run(c) {
        const f = c.fs.file(`${c.root}/atomic`);
        for (const mode of [WriteMode.Atomic, WriteMode.AtomicDurable]) {
          await f.writeString('old', mode);
          await f.writeString('new😀', mode);
          check((await f.readString()) === 'new😀', 'atomic text mismatch');
          const bytes = pattern(4097, c.config.seed);
          f.writeBytesSync(bytes, mode);
          equalBytes(await f.readBytes(), bytes);
        }
        await c.fs.directory(`${c.root}/directory`).create();
        await mustReject(
          () =>
            c.fs
              .file(`${c.root}/directory`)
              .writeString('bad', WriteMode.Atomic),
          'directory target'
        );
        check(
          !(await c.fs.directory(c.root).list()).some((entry) =>
            entry.name.startsWith('.rct-io-')
          ),
          'stage leak'
        );
      },
    },
    {
      name: 'Pending completion boundaries 255 / 256 / 257 and recovery',
      group: 'critical',
      async run(c) {
        const f = c.fs.file(`${c.root}/queue`);
        await f.writeString('value');
        for (const count of [255, 256, 257]) {
          const pending: Promise<unknown>[] = [];
          let rejected = 0;
          // Deliberately do not await/yield in this loop: completed native work still
          // occupies a pending completion until the JS callback is consumed.
          for (let i = 0; i < count; ++i) {
            try {
              pending.push(f.exists());
            } catch {
              ++rejected;
            }
          }
          await Promise.all(pending);
          check(
            pending.length === Math.min(count, 256),
            `accepted count ${count}`
          );
          check(
            rejected === Math.max(0, count - 256),
            `rejected count ${count}`
          );
          check((await f.readString()) === 'value', 'context did not recover');
        }
      },
    },
    {
      name: 'Compiled low-memory buffer limit - 1 / limit / limit + 1',
      group: 'critical',
      skip:
        TEST_NATIVE_BUFFER_LIMIT_BYTES === undefined
          ? 'Requires a separate native low-limit test build; see TESTING.md'
          : undefined,
      async run() {
        const limit = TEST_NATIVE_BUFFER_LIMIT_BYTES!;
        check(
          Number.isInteger(limit) && limit >= 16 && limit <= 1024 * 1024,
          'unsafe/mismatched test limit'
        );
        for (const n of [limit - 1, limit])
          check(FS.encodeString('a'.repeat(n)).byteLength === n, `limit ${n}`);
        await mustReject(
          () => FS.encodeString('a'.repeat(limit + 1)),
          'limit+1 encoding'
        );
      },
    },
    {
      name: 'Context close rejects children while started work drains',
      group: 'boundary',
      async run(c) {
        const isolated = openFS(1);
        const f = isolated.file(`${c.root}/close`);
        try {
          const pending = f.writeString('in flight');
          isolated.close();
          await pending;
          await mustReject(() => f.readString(), 'child after context close');
          await mustReject(() => isolated.file(f.path), 'closed context');
          check(
            (await c.fs.file(f.path).readString()) === 'in flight',
            'started operation was lost'
          );
        } finally {
          isolated.close();
        }
      },
    },
    {
      name: 'Seeded concurrent write/read/hash/delete integrity',
      group: 'stress',
      async run(c) {
        await parallel(
          c.config.iterations,
          c.config.concurrency,
          async (index) => {
            c.check();
            const f = c.fs.file(`${c.root}/${index}`),
              data = pattern(4096 + (index % 17), c.config.seed + index);
            await f.writeBytes(data, WriteMode.Atomic);
            equalBytes(await f.readBytes(), data);
            const first = await f.calcHash();
            check(first === (await f.calcHash()), 'hash parity under load');
            await f.delete();
            check(!(await f.exists()), 'delete under load');
          }
        );
        check(
          (await c.fs.directory(c.root).list()).length === 0,
          'stress artifact leak'
        );
      },
    },
    {
      name: 'Shared-handle writes are complete records; close races settle',
      group: 'stress',
      async run(c) {
        const path = `${c.root}/shared`,
          h = c.fs.open(path, FileOpenMode.Write);
        try {
          await parallel(
            c.config.iterations,
            c.config.concurrency,
            async (i) => {
              c.check();
              await h.writeLine(`record:${i}`);
            }
          );
        } finally {
          h.close();
        }
        const lines = (await c.fs.file(path).readString())
          .trimEnd()
          .split('\n');
        check(
          lines.length === c.config.iterations &&
            new Set(lines).size === c.config.iterations,
          'lost/duplicated/interleaved records'
        );
        const race = c.fs.open(path, FileOpenMode.Append);
        const pending: Promise<unknown>[] = [];
        try {
          for (let i = 0; i < c.config.concurrency; ++i)
            pending.push(
              race.writeString('x').then(
                () => true,
                () => false
              )
            );
        } finally {
          race.close();
        }
        await Promise.all(pending);
        await mustReject(() => race.writeString('bad'), 'post-close write');
      },
    },
    {
      name: 'Concurrent readers observe whole atomic versions',
      group: 'stress',
      async run(c) {
        const f = c.fs.file(`${c.root}/versions`),
          a = 'A'.repeat(16384),
          b = 'B'.repeat(16384);
        await f.writeString(a, WriteMode.Atomic);
        await parallel(
          c.config.concurrency,
          c.config.concurrency,
          async (worker) => {
            for (
              let i = 0;
              i < Math.ceil(c.config.iterations / c.config.concurrency);
              ++i
            ) {
              c.check();
              if (worker === 0)
                await f.writeString(i % 2 ? a : b, WriteMode.Atomic);
              else {
                const value = await f.readString();
                check(value === a || value === b, 'partial atomic version');
              }
            }
          }
        );
      },
    },
    {
      name: 'Binary I/O, hashing and atomic durability distributions',
      group: 'performance',
      async run(c) {
        const bytes = pattern(c.config.maxBytes, c.config.seed),
          f = c.fs.file(`${c.root}/bench`);
        await f.writeBytes(bytes);
        const expectedHash = await f.calcHash();
        await benchmark(c, 'writeBytes/overwrite', bytes.byteLength, () =>
          f.writeBytes(bytes)
        );
        equalBytes(await f.readBytes(), bytes);
        await benchmark(c, 'readBytes', bytes.byteLength, async () => {
          const result = await f.readBytes();
          check(
            result.byteLength === bytes.byteLength,
            'read benchmark length'
          );
        });
        await benchmark(c, 'SHA256/streaming', bytes.byteLength, async () => {
          check(
            (await f.calcHash()) === expectedHash,
            'benchmark hash mismatch'
          );
        });
        // Keep fsync sampling small and explicit; it is not part of ordinary overwrite timing.
        const small = pattern(4096, c.config.seed);
        await benchmark(c, 'writeBytes/atomic-4KiB', small.byteLength, () =>
          f.writeBytes(small, WriteMode.Atomic)
        );
        await benchmark(
          c,
          'writeBytes/atomic-durable-4KiB',
          small.byteLength,
          () => f.writeBytes(small, WriteMode.AtomicDurable)
        );
      },
    },
    {
      name: 'Network echo, binary integrity, falsy JSON, redirects and timeout',
      group: 'network',
      skip: networkSkip,
      async run(c) {
        const bytes = pattern(4097, c.config.seed);
        const echo = await request.post(`${fixture}/echo`, { data: bytes });
        check(echo.ok, echo.error ?? 'echo failed');
        equalBytes(echo.arrayBuffer(), bytes);
        for (const value of [false, 0, '', null]) {
          const result = await request.post(`${fixture}/echo`, { json: value });
          check(result.text() === JSON.stringify(value), 'falsy body lost');
        }
        check(
          (await request.get(`${fixture}/redirect`, { followRedirects: true }))
            .status === 200,
          'follow redirect'
        );
        check(
          (await request.get(`${fixture}/redirect`, { followRedirects: false }))
            .status === 302,
          'redirect disabled'
        );
        check(
          !(await request.get(`${fixture}/slow`, { timeout: 100 })).ok,
          'timeout was ignored'
        );
      },
    },
    {
      name: 'Network range validation, ignored ranges and injection rejection',
      group: 'network',
      skip: networkSkip,
      async run(c) {
        const f = c.fs.file(`${c.root}/range`);
        for (const endpoint of ['range', 'range-ignore']) {
          await f.writeString('com');
          const result = await request.download(
            `${fixture}/${endpoint}`,
            f.path,
            { resumable: true, headers: { 'If-Range': '"v1"' } }
          );
          check(
            result.ok && (await f.readString()) === 'complete',
            `resume/restart ${endpoint}`
          );
        }
        if (FS.getPlatform().platform === 'android') {
          await f.writeString('com');
          check(
            !(
              await request.download(`${fixture}/range-invalid`, f.path, {
                resumable: true,
                headers: { 'If-Range': '"v1"' },
              })
            ).ok,
            'invalid Content-Range accepted'
          );
          check(
            (await f.readString()) === 'com',
            'invalid range damaged prefix'
          );
        }
        await mustReject(
          () =>
            request.get(`${fixture}/payload`, {
              headers: { 'X-Test': 'bad\r\nInjected: true' },
            }),
          'header injection'
        );
        await mustReject(
          () =>
            request.upload(`${fixture}/upload`, f.path, {
              fileName: 'bad"name',
            }),
          'multipart metadata injection'
        );
      },
    },
    {
      name: 'Network download failure preserves destination; multipart fields arrive',
      group: 'network',
      skip: networkSkip,
      async run(c) {
        const f = c.fs.file(`${c.root}/download`);
        await f.writeString('old');
        check(
          !(await request.download(`${fixture}/error`, f.path)).ok,
          '404 download accepted'
        );
        check(
          (await f.readString()) === 'old',
          'failed download destroyed destination'
        );
        check(
          (await request.download(`${fixture}/payload`, f.path)).ok,
          'download failed'
        );
        check(
          (await f.readString()) === 'complete',
          'download payload mismatch'
        );
        const upload = await request.upload(`${fixture}/upload`, f.path, {
          formFields: { caption: 'hello' },
        });
        check(
          upload.ok && upload.status === 200,
          upload.error ?? 'multipart fields missing'
        );
      },
    },
  ];
}
