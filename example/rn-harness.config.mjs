import {
  androidPlatform,
  physicalAndroidDevice,
} from '@react-native-harness/platform-android';
import {
  applePlatform,
  appleSimulator,
} from '@react-native-harness/platform-apple';

const config = {
  entryPoint: './index.js',
  appRegistryComponentName: 'IoExample',
  bridgeTimeout: 120000, // 2 minutes for slower devices

  runners: [
    androidPlatform({
      name: 'android',
      // Note: physicalAndroidDevice converts to lowercase internally,
      // but adb returns original case, causing mismatch. Use lowercase here.
      device: physicalAndroidDevice('xiaomi', '2312draabc'),
      bundleId: 'io.example',
    }),
    applePlatform({
      name: 'ios',
      device: appleSimulator('iPhone 16 Pro', '18.0'),
      bundleId: 'org.reactjs.native.example.IoExample',
    }),
  ],
};

export default config;
