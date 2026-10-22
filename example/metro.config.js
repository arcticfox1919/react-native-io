const path = require('path');
const { getDefaultConfig } = require('@react-native/metro-config');
const { withMetroConfig } = require('react-native-monorepo-config');
const { withRnHarness } = require('react-native-harness/metro');
const {
  createHarmonyMetroConfig,
} = require('@react-native-oh/react-native-harmony/metro.config');
const { mergeConfig } = require('@react-native/metro-config');

const root = path.resolve(__dirname, '..');

/**
 * Metro configuration
 * https://facebook.github.io/metro/docs/configuration
 *
 * @type {import('metro-config').MetroConfig}
 */
// Keep the monorepo resolver outside the Harmony resolver so source exports
// remain available. The official resolver redirects RN only for Harmony builds.
const config = withMetroConfig(
  mergeConfig(
    getDefaultConfig(__dirname),
    createHarmonyMetroConfig({
      reactNativeHarmonyPackageName: '@react-native-oh/react-native-harmony',
    })
  ),
  {
    root,
    dirname: __dirname,
  }
);
config.resolver.nodeModulesPaths = [path.join(__dirname, 'node_modules')];
module.exports = withRnHarness(config);
