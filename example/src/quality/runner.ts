import { FS, openFS, File, type FSContext } from 'react-native-io';
import {
  checkpoint,
  deviceLog,
  settings,
  Stopped,
  type Settings,
  type Group,
  type Profile,
} from './engine';
import { makeCases } from './suites';

export interface Measurement {
  label: string;
  rawMs: number[];
  bytesPerSample: number;
  summary: import('./engine').Distribution;
}
export interface CaseContext {
  fs: FSContext;
  root: string;
  config: Settings;
  networkURL?: string;
  check: () => void;
  measure: (measurement: Measurement) => void;
}
export interface Case {
  name: string;
  group: Group;
  skip?: string;
  run: (context: CaseContext) => Promise<void>;
}
export interface CaseResult {
  name: string;
  group: Group;
  status: 'pass' | 'fail' | 'skipped' | 'cancelled';
  durationMs: number;
  error?: string;
  measurements: Measurement[];
}
export interface Report {
  schemaVersion: 1;
  startedAt: string;
  finishedAt: string;
  config: Settings;
  environment: Record<string, unknown>;
  results: CaseResult[];
}
export interface RunOptions {
  profile: Profile;
  groups?: Group[];
  networkURL?: string;
  cancelled?: () => boolean;
  onResult?: (result: CaseResult) => void;
  environment?: Record<string, unknown>;
}
export async function runQuality(options: RunOptions): Promise<Report> {
  const config = settings(options.profile);
  const startedAt = new Date().toISOString();
  const deadline = Date.now() + config.deadlineMs;
  const cancelled = options.cancelled ?? (() => false);
  const report: Report = {
    schemaVersion: 1,
    startedAt,
    finishedAt: '',
    config,
    environment: options.environment ?? {},
    results: [],
  };
  const root = `${FS.cacheDir}/rn-io-quality-${Date.now()}-${Math.random()
    .toString(16)
    .slice(2)}`;
  deviceLog('[IO_QUALITY] opening filesystem');
  const fs = openFS(4);
  const append = (result: CaseResult) => {
    report.results.push(result);
    options.onResult?.(result);
  };
  try {
    deviceLog('[IO_QUALITY] creating workspace');
    await fs.directory(root).create(true);
    for (const [index, test] of makeCases(options.networkURL).entries()) {
      if (options.groups && !options.groups.includes(test.group)) continue;
      deviceLog(`[IO_QUALITY] START ${test.name}`);
      const start = performance.now();
      const result: CaseResult = {
        name: test.name,
        group: test.group,
        status: 'pass',
        durationMs: 0,
        measurements: [],
      };
      try {
        checkpoint(cancelled, deadline);
        if (test.skip) {
          result.status = 'skipped';
          result.error = test.skip;
        } else {
          const caseRoot = `${root}/${index}`;
          await fs.directory(caseRoot).create(true);
          await test.run({
            fs,
            root: caseRoot,
            config,
            networkURL: options.networkURL,
            check: () => checkpoint(cancelled, deadline),
            measure: (value) => result.measurements.push(value),
          });
          // Keep disk usage bounded across cases, including long stress runs.
          await fs.directory(caseRoot).delete(true);
        }
      } catch (error) {
        result.status = error instanceof Stopped ? 'cancelled' : 'fail';
        result.error =
          error instanceof Error
            ? `${error.message}\n${error.stack ?? ''}`
            : String(error);
      }
      result.durationMs = performance.now() - start;
      deviceLog(
        `[IO_QUALITY] ${result.status} ${result.name} ${result.error ?? ''}`
      );
      append(result);
      if (result.status === 'cancelled') break;
    }
  } catch (error) {
    append({
      name: 'Runner setup',
      group: 'functional',
      status: 'fail',
      durationMs: 0,
      error: String(error),
      measurements: [],
    });
  } finally {
    try {
      if (await fs.directory(root).exists())
        await fs.directory(root).delete(true);
    } catch (error) {
      append({
        name: 'Runner cleanup',
        group: 'functional',
        status: 'fail',
        durationMs: 0,
        error: String(error),
        measurements: [],
      });
    }
    fs.close();
    report.finishedAt = new Date().toISOString();
  }
  return report;
}
/** Reports survive cache cleanup. This function may itself fail (e.g. disk full). */
export async function saveReport(report: Report): Promise<string> {
  const path = `${FS.documentDir}/io-quality/report-${Date.now()}.json`;
  await new File(path).writeString(JSON.stringify(report, null, 2));
  return path;
}
