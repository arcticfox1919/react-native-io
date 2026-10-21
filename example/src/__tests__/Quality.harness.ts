/** Execute the SAME scenarios as the on-device UI through the existing Harness. */
import { describe, it, expect } from 'react-native-harness';
import { runQuality } from '../quality/runner';
import { TEST_HTTP_BASE_URL, HARNESS_PROFILE } from '../quality/config';
import type { Group } from '../quality/engine';

const groups: Group[] = [
  'functional',
  'boundary',
  'critical',
  'stress',
  'performance',
  'network',
];
describe('Quality regression', () => {
  for (const group of groups) {
    it(`${group}: ${HARNESS_PROFILE} profile (inspect skipped cases in report)`, async () => {
      const report = await runQuality({
        profile: HARNESS_PROFILE,
        groups: [group],
        networkURL: TEST_HTTP_BASE_URL || undefined,
      });
      console.log('[io-quality-report]', JSON.stringify(report));
      const failures = report.results.filter(
        (r) => r.status === 'fail' || r.status === 'cancelled'
      );
      if (failures.length) throw new Error(JSON.stringify(failures));
      expect(report.results.length > 0).toBe(true);
    });
  }
});
