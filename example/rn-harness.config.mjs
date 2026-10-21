import {
  androidPlatform,
  androidEmulator,
} from '@react-native-harness/platform-android';
import {
  applePlatform,
  applePhysicalDevice,
} from '@react-native-harness/platform-apple';

const bundleId = process.env.IO_IOS_BUNDLE_ID ?? 'io.example.rct';
const ios = process.env.IO_IOS_LEGACY_UDID
  ? {
      name: 'ios',
      config: {
        udid: process.env.IO_IOS_LEGACY_UDID,
        appPath: process.env.IO_IOS_APP_PATH,
        bundleId,
      },
      runner: new URL('./scripts/ios-legacy-runner.mjs', import.meta.url).href,
    }
  : applePlatform({
      name: 'ios',
      device: applePhysicalDevice(process.env.IO_IOS_DEVICE_NAME ?? 'iPhone'),
      bundleId,
    });
export default {
  entryPoint: './index.js',
  appRegistryComponentName: 'IoExample',
  bridgeTimeout: 120000,
  runners: [
    androidPlatform({
      name: 'android',
      device: androidEmulator('Pixel_6_API_35'),
      bundleId: 'io.example',
    }),
    ios,
  ],
};
