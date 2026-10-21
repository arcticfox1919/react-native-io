/** Harness adapter for USB iOS < 17; avoids newer LLDB image-notifier traps. */
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';
const execute = promisify(execFile);
export default async function legacyIOSRunner(config) {
  if (!config.udid || !config.bundleId) {
    throw new Error(
      'Set IO_IOS_LEGACY_UDID and install the signed test app first. See TESTING.md'
    );
  }
  const base = ['--udid', config.udid];
  let launched = false;
  const stopApp = async () => {
    if (!launched) return;
    await execute('idevicedebug', [...base, 'kill', config.bundleId], {
      timeout: 30000,
    });
    launched = false;
  };
  const startApp = async () => {
    await execute(
      'idevicedebug',
      [...base, '--detach', 'run', config.bundleId],
      { timeout: 120000, maxBuffer: 4 * 1024 * 1024 }
    );
    launched = true;
  };
  return {
    startApp,
    stopApp,
    restartApp: async () => {
      await stopApp();
      await startApp();
    },
    dispose: stopApp,
  };
}
