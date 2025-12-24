import {
  androidPlatform,
  androidEmulator,
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
      device: androidEmulator('Pixel_6_API_35'),
      bundleId: 'io.example',
    }),
    applePlatform({
      name: 'ios',
      device: appleSimulator('iPhone 16 Pro', '18.5'),
      bundleId: 'io.example.rct',
    }),
  ],
};

export default config;
