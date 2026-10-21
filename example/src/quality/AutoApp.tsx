/** Standalone device entry: uses the same runner, no Metro/bridge required after bundling. */
import { useEffect, useState } from 'react';
import { Platform, ScrollView, Settings, Text } from 'react-native';
import { AUTO_PROFILE, TEST_HTTP_BASE_URL } from './config';
import { deviceLog, setDeviceLogger } from './engine';
import type { DeviceLaunchOptions } from './deviceOptions';

export default function AutoApp({ ioExampleBuild }: DeviceLaunchOptions) {
  const [status, setStatus] = useState('Running quality tests…');
  useEffect(() => {
    if (Platform.OS === 'ios')
      setDeviceLogger((message) =>
        Settings.set({ IOQualityProgress: message })
      );
    const run = async () => {
      deviceLog('[IO_QUALITY] JS automatic entry mounted');
      let networkURL: string | undefined = TEST_HTTP_BASE_URL || undefined;
      const host: unknown =
        Platform.OS === 'ios'
          ? Settings.get('IOQualityMetroHost')
          : Platform.OS === 'android'
          ? '127.0.0.1' // adb reverse tcp:8765 tcp:8765 for the local fixture.
          : undefined;
      if (typeof host === 'string' && host) {
        if (Platform.OS === 'ios') {
          const controller = new AbortController();
          const timer = setTimeout(() => controller.abort(), 5000);
          try {
            const response = await fetch(`http://${host}:8081/status`, {
              signal: controller.signal,
            });
            deviceLog(
              `[IO_METRO_PROBE] ${host}:8081 status=${
                response.status
              } body=${await response.text()}`
            );
          } catch (error) {
            deviceLog(`[IO_METRO_PROBE] ${host}:8081 failed: ${String(error)}`);
          } finally {
            clearTimeout(timer);
          }
        }
        if (!networkURL) {
          const discovery = new AbortController();
          const discoveryTimer = setTimeout(() => discovery.abort(), 2000);
          try {
            const localURL = `http://${host}:8765`;
            const health = await fetch(`${localURL}/health`, {
              signal: discovery.signal,
            });
            if (
              health.ok &&
              (await health.text()) === 'io-quality-fixture-v1'
            ) {
              networkURL = localURL;
              deviceLog(`[IO_QUALITY] local fixture verified ${localURL}`);
            }
          } catch {
            /* Optional fixture is absent; network cases report skipped. */
          } finally {
            clearTimeout(discoveryTimer);
          }
        }
      }
      // Keep native-module linking failures visible in the test UI and device log.
      const { runQuality, saveReport } =
        require('./runner') as typeof import('./runner');
      const report = await runQuality({
        profile: AUTO_PROFILE,
        networkURL,
        environment: {
          nativeBuild: ioExampleBuild ?? null,
          platform: Platform.OS,
          systemVersion: Platform.Version,
          reactNative: Platform.constants.reactNativeVersion,
          debug: __DEV__,
          engine: 'HermesInternal' in globalThis ? 'Hermes' : 'other/unknown',
          entry: 'automatic',
          networkFixture: networkURL ?? null,
        },
      });
      const path = await saveReport(report);
      const failures = report.results.filter(
        (r) => r.status === 'fail' || r.status === 'cancelled'
      );
      deviceLog('[IO_QUALITY_REPORT] ' + JSON.stringify(report));
      deviceLog(
        `[IO_QUALITY_FINISHED] failures=${failures.length} path=${path}`
      );
      setStatus(
        `${failures.length ? 'FAILED' : 'COMPLETED'}\n${path}\n${JSON.stringify(
          report,
          null,
          2
        )}`
      );
    };
    void run().catch((error) => {
      deviceLog('[IO_QUALITY_FATAL] ' + String(error));
      setStatus(String(error));
    });
  }, [ioExampleBuild]);
  return (
    <ScrollView contentContainerStyle={{ padding: 24 }}>
      <Text selectable>{status}</Text>
    </ScrollView>
  );
}
