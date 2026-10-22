import { useEffect, useState } from 'react';
import {
  Platform,
  SafeAreaView,
  ScrollView,
  StyleSheet,
  Text,
} from 'react-native';
import { Directory, File, FS, HashAlgorithm } from 'react-native-io';
import { check, deviceLog, distribution, pattern } from './engine';
import type { DeviceLaunchOptions } from './deviceOptions';

const BYTES = 16 * 1024 * 1024;
const SEED = 0x1a2b3c4d;
const WARMUP = 5;
const SAMPLES = 30;
// Independently computed using Node crypto for the deterministic xorshift fixture.
const EXPECTED_SHA256 =
  '7f285e6cea5646a91b5694d43892755efe7f56beb9a41ad3d56d4c4220307fd2';
interface Measurement {
  label: string;
  bytesPerSample: number;
  rawMs: number[];
  summary: ReturnType<typeof distribution>;
}
function exactBytes(actual: ArrayBuffer, expected: ArrayBuffer) {
  const a = new Uint8Array(actual),
    b = new Uint8Array(expected);
  check(a.length === b.length, 'read length mismatch');
  for (let i = 0; i < a.length; ++i) {
    if (a[i] !== b[i]) throw new Error(`byte mismatch at ${i}`);
  }
}

/** Both APKs use this identical harness and ArrayBuffer inputs. */
export async function runBenchmark(
  options: DeviceLaunchOptions,
  progress: (message: string) => void
) {
  const build = options.ioExampleBuild;
  check(!__DEV__, 'Benchmark requires a JS production bundle');
  if (Platform.OS === 'android') {
    check(
      build?.type === 'release' && !build.debuggable && build.minified,
      'Benchmark requires a non-debuggable Android Release APK with R8'
    );
  }
  const startedAt = new Date().toISOString();
  const runId = options.ioBenchmarkRunId ?? String(Date.now());
  const root = `${FS.cacheDir}/io-benchmark-${Date.now()}`;
  const dir = new Directory(root);
  const measurements: Measurement[] = [];
  async function measure(
    label: string,
    bytesPerSample: number,
    action: () => unknown | Promise<unknown>,
    synchronous = false
  ) {
    progress(label);
    deviceLog(`[IO_BENCH_PHASE] ${runId} ${label}`);
    for (let i = 0; i < WARMUP; ++i) {
      if (synchronous) action();
      else await action();
    }
    const rawMs: number[] = [];
    for (let i = 0; i < SAMPLES; ++i) {
      const start = performance.now();
      if (synchronous) action();
      else await action();
      rawMs.push(performance.now() - start);
    }
    measurements.push({
      label,
      bytesPerSample,
      rawMs,
      summary: distribution(rawMs, bytesPerSample),
    });
  }
  await dir.create(true);
  try {
    const large = new File(`${root}/large`);
    const small = new File(`${root}/small`);
    const bytes = pattern(BYTES, SEED);
    const smallBytes = pattern(4096, SEED);
    await small.writeString('abc');
    check(
      (await small.calcHash(HashAlgorithm.SHA256)) ===
        'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad',
      'SHA-256 known answer failed'
    );
    await large.writeBytes(bytes);
    exactBytes(await large.readBytes(), bytes);
    check(
      (await large.calcHash(HashAlgorithm.SHA256)) === EXPECTED_SHA256,
      'fixture digest mismatch'
    );

    await measure('writeBytes/16MiB', BYTES, () => large.writeBytes(bytes));
    check(
      (await large.calcHash(HashAlgorithm.SHA256)) === EXPECTED_SHA256,
      'write content mismatch'
    );
    await measure(
      'writeBytesSync/16MiB',
      BYTES,
      () => large.writeBytesSync(bytes),
      true
    );
    exactBytes(large.readBytesSync(), bytes);
    await measure('readBytes/16MiB', BYTES, async () => {
      check(
        (await large.readBytes()).byteLength === BYTES,
        'read length mismatch'
      );
    });
    exactBytes(await large.readBytes(), bytes);
    await measure('SHA256/16MiB', BYTES, async () => {
      check(
        (await large.calcHash(HashAlgorithm.SHA256)) === EXPECTED_SHA256,
        'hash mismatch'
      );
    });
    await measure(
      'readBytesSync/16MiB',
      BYTES,
      () => {
        check(
          large.readBytesSync().byteLength === BYTES,
          'sync read length mismatch'
        );
      },
      true
    );
    exactBytes(large.readBytesSync(), bytes);
    const textFile = new File(`${root}/text`);
    const largeText = 'x'.repeat(BYTES);
    await textFile.writeString(largeText);
    await measure('readString/16MiB', BYTES, async () => {
      check((await textFile.readString()).length === BYTES, 'text read length');
    });
    check((await textFile.readString()) === largeText, 'text read content');
    await measure(
      'readStringSync/16MiB',
      BYTES,
      () =>
        check(textFile.readStringSync().length === BYTES, 'sync text length'),
      true
    );
    check(textFile.readStringSync() === largeText, 'sync text content');
    await measure(
      'encodeString/16MiB',
      BYTES,
      () =>
        check(FS.encodeString(largeText).byteLength === BYTES, 'encode size'),
      true
    );
    const encoded = FS.encodeString(largeText);
    check(FS.decodeString(encoded) === largeText, 'encoded text content');
    await measure(
      'decodeString/16MiB',
      BYTES,
      () => check(FS.decodeString(encoded).length === BYTES, 'decode length'),
      true
    );
    check(FS.decodeString(encoded) === largeText, 'decoded text content');
    // Each unit produces ten UTF-8 bytes, including a supplementary character.
    const unicodeText =
      'é中😀x'.repeat(Math.floor(BYTES / 10)) + 'x'.repeat(BYTES % 10);
    await measure(
      'encodeString-UTF8-Unicode/16MiB',
      BYTES,
      () =>
        check(FS.encodeString(unicodeText).byteLength === BYTES, 'UTF-8 size'),
      true
    );
    check(
      FS.decodeString(FS.encodeString(unicodeText)) === unicodeText,
      'Unicode encoding content'
    );
    const latin1Text = 'éÿ\u0000A'.repeat(BYTES / 4);
    await measure(
      'encodeString-Latin1/16MiB',
      BYTES,
      () =>
        check(
          FS.encodeString(latin1Text, 'latin1').byteLength === BYTES,
          'Latin1 size'
        ),
      true
    );
    const latin1Bytes = new Uint8Array(FS.encodeString(latin1Text, 'latin1'));
    const latin1Pattern = [233, 255, 0, 65];
    for (let i = 0; i < latin1Bytes.length; ++i) {
      check(latin1Bytes[i] === latin1Pattern[i % 4], 'Latin1 encoding content');
    }
    await measure('writeBytes/4KiB', 4096, () => small.writeBytes(smallBytes));
    exactBytes(await small.readBytes(), smallBytes);
    await measure('readBytes/4KiB', 4096, async () => {
      check(
        (await small.readBytes()).byteLength === 4096,
        'small read length mismatch'
      );
    });
    exactBytes(await small.readBytes(), smallBytes);
    const text = 'x'.repeat(4096);
    await measure('writeString/4KiB', 4096, () => small.writeString(text));
    check((await small.readString()) === text, 'small string mismatch');
  } finally {
    await dir.delete(true);
  }
  const report = {
    schemaVersion: 1,
    kind: 'release-common-api-benchmark',
    runId,
    startedAt,
    finishedAt: new Date().toISOString(),
    environment: {
      platform: Platform.OS,
      systemVersion: Platform.Version,
      reactNative: Platform.constants.reactNativeVersion,
      engine: 'HermesInternal' in globalThis ? 'Hermes' : 'unknown',
      jsDev: __DEV__,
      nativeBuild: build ?? null,
    },
    methodology: {
      seed: SEED,
      warmup: WARMUP,
      samples: SAMPLES,
      datasetBytes: BYTES,
      datasetSha256: EXPECTED_SHA256,
      cache: 'warm OS page cache; no cache eviction or drop_caches',
      writes: 'overwrite with fclose, no fsync; not durable storage bandwidth',
      timing:
        'JS call through returned Promise/value, includes copies and scheduling',
      correctness:
        'independent SHA-256 fixture; exact byte checks outside timed read loops',
      syncTiming: 'direct synchronous call; no await in timed region',
      order:
        'fixed metric order in both APKs; A/B rounds counterbalance process order',
    },
    measurements,
  };
  // Keep Release reports readable by adb without changing documentDir semantics.
  const reportDir = `${
    Platform.OS === 'android' ? `${FS.externalDir}/Documents` : FS.documentDir
  }/io-quality`;
  await new Directory(reportDir).create(true);
  const path = `${reportDir}/benchmark-${
    build?.variant ?? 'unknown'
  }-${runId}.json`;
  await new File(path).writeString(JSON.stringify(report, null, 2));
  deviceLog(`[IO_BENCH_FINISHED] ${runId} ${path}`);
  return { report, path };
}

export default function BenchmarkApp(options: DeviceLaunchOptions) {
  const [status, setStatus] = useState('Preparing Release benchmark…');
  const [result, setResult] =
    useState<Awaited<ReturnType<typeof runBenchmark>>>();
  useEffect(() => {
    void runBenchmark(options, setStatus)
      .then((value) => {
        setResult(value);
        setStatus('Completed · correctness checks passed');
      })
      .catch((error) => setStatus(`FAILED: ${String(error)}`));
    // Launch options are immutable for this mounted root. Do not restart a run on render.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);
  return (
    <SafeAreaView style={styles.container}>
      <ScrollView contentContainerStyle={styles.content}>
        <Text style={styles.title}>Release performance</Text>
        <Text>{options.ioExampleBuild?.variant} · 5 warmups / 30 samples</Text>
        <Text selectable style={styles.status}>
          {status}
        </Text>
        {result?.report.measurements.map((m) => (
          <Text key={m.label} style={styles.metric}>
            {m.label}: median {m.summary.medianMs.toFixed(3)} ms · p95{' '}
            {m.summary.p95Ms.toFixed(3)} ms
          </Text>
        ))}
        <Text selectable>{result?.path}</Text>
      </ScrollView>
    </SafeAreaView>
  );
}
const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#f3f6fb' },
  content: { padding: 24 },
  title: { fontSize: 24, fontWeight: '600', marginBottom: 12 },
  status: { marginVertical: 20 },
  metric: { paddingVertical: 12 },
});
