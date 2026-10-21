/** Pure helpers shared by the device runner and host regression tests. */
export type Group =
  | 'functional'
  | 'boundary'
  | 'critical'
  | 'stress'
  | 'performance'
  | 'network';
export type Profile = 'quick' | 'full' | 'soak';
export interface Settings {
  profile: Profile;
  seed: number;
  iterations: number;
  concurrency: number;
  samples: number;
  warmup: number;
  maxBytes: number;
  deadlineMs: number;
}
export function settings(profile: Profile): Settings {
  const presets = {
    quick: {
      iterations: 40,
      concurrency: 4,
      samples: 5,
      maxBytes: 1024 * 1024,
      deadlineMs: 120000,
    },
    full: {
      iterations: 500,
      concurrency: 16,
      samples: 20,
      maxBytes: 16 * 1024 * 1024,
      deadlineMs: 600000,
    },
    soak: {
      iterations: 5000,
      concurrency: 32,
      samples: 30,
      maxBytes: 32 * 1024 * 1024,
      deadlineMs: 1800000,
    },
  };
  return { profile, seed: 0x1a2b3c4d, warmup: 2, ...presets[profile] };
}
export function check(condition: unknown, message: string): asserts condition {
  if (!condition) throw new Error(message);
}
export async function mustReject(
  action: () => unknown,
  label: string
): Promise<void> {
  let failed = false;
  try {
    await action();
  } catch {
    failed = true;
  }
  check(failed, `${label}: expected a throw or rejection`);
}
export function pattern(size: number, seed: number): ArrayBuffer {
  const bytes = new Uint8Array(size);
  let state = seed >>> 0;
  for (let i = 0; i < size; ++i) {
    state ^= state << 13;
    state ^= state >>> 17;
    state ^= state << 5;
    bytes[i] = state & 255;
  }
  return bytes.buffer;
}
export function equalBytes(actual: ArrayBuffer, expected: ArrayBuffer): void {
  const a = new Uint8Array(actual),
    b = new Uint8Array(expected);
  check(a.length === b.length, `byte length: ${a.length} != ${b.length}`);
  for (let i = 0; i < a.length; ++i)
    check(a[i] === b[i], `byte ${i}: ${a[i]} != ${b[i]}`);
}
export interface Distribution {
  samples: number;
  minMs: number;
  medianMs: number;
  p95Ms: number;
  maxMs: number;
  meanMs: number;
  standardDeviationMs: number;
  totalMs: number;
  throughputMiBps?: number;
}
/** Nearest-rank percentile; retain raw observations in the report too. */
export function distribution(raw: number[], bytesPerSample = 0): Distribution {
  check(
    raw.length > 0 && raw.every((x) => Number.isFinite(x) && x >= 0),
    'invalid timings'
  );
  const ordered = [...raw].sort((a, b) => a - b);
  const totalMs = raw.reduce((a, b) => a + b, 0),
    meanMs = totalMs / raw.length;
  const at = (p: number) =>
    ordered[Math.max(0, Math.ceil(p * ordered.length) - 1)]!;
  return {
    samples: raw.length,
    minMs: ordered[0]!,
    medianMs: at(0.5),
    p95Ms: at(0.95),
    maxMs: ordered[ordered.length - 1]!,
    meanMs,
    totalMs,
    standardDeviationMs: Math.sqrt(
      raw.reduce((sum, x) => sum + (x - meanMs) ** 2, 0) / raw.length
    ),
    ...(bytesPerSample > 0 && totalMs > 0
      ? {
          throughputMiBps:
            (bytesPerSample * raw.length * 1000) / (1024 * 1024 * totalMs),
        }
      : {}),
  };
}
export class Stopped extends Error {}
export function checkpoint(cancelled: () => boolean, deadline: number): void {
  if (cancelled())
    throw new Stopped(
      'Cancelled; queued native work is drained before cleanup'
    );
  if (Date.now() >= deadline) throw new Stopped('Run deadline reached');
}
/** Await every started worker even after failure. Never race cleanup against native I/O. */
export async function parallel(
  count: number,
  concurrency: number,
  action: (index: number) => Promise<void>
): Promise<void> {
  check(Number.isInteger(count) && count >= 0, 'invalid count');
  check(
    Number.isInteger(concurrency) && concurrency >= 1 && concurrency <= 256,
    'invalid concurrency'
  );
  let next = 0;
  let failed = false;
  let failure: unknown;
  const worker = async () => {
    while (!failed) {
      const index = next++;
      if (index >= count) return;
      try {
        await action(index);
      } catch (error) {
        if (!failed) failure = error;
        failed = true;
      }
    }
  };
  await Promise.all(
    Array.from({ length: Math.min(count, concurrency) }, worker)
  );
  if (failed) throw failure;
}

let deviceLogger: ((message: string) => void) | undefined;
export function setDeviceLogger(logger: (message: string) => void): void {
  deviceLogger = logger;
}
/** Keep device progress observable even without a Metro console connection. */
export function deviceLog(message: string): void {
  deviceLogger?.(message);
  const hook = (
    globalThis as unknown as {
      nativeLoggingHook?: (text: string, level: number) => void;
    }
  ).nativeLoggingHook;
  if (typeof hook === 'function') hook(message, 1);
  else console.info(message);
}
