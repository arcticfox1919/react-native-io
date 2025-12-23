/**
 * @type {import('@react-native-community/cli-types').UserDependencyConfig}
 */
module.exports = {
  dependency: {
    platforms: {
      /**
       * @type {import('@react-native-community/cli-types').IOSDependencyParams}
       */
      ios: {},
      /**
       * @type {import('@react-native-community/cli-types').AndroidDependencyParams}
       */
      android: {
        sourceDir: 'android',
        cmakeListsPath: '../cpp/noop/CMakeLists.txt',
        // C++ module configuration
        libraryName: 'StdIOSpec',
        cxxModuleCMakeListsModuleName: 'react-native-io',
        cxxModuleCMakeListsPath: '../cpp/CMakeLists.txt',
        cxxModuleHeaderName: 'NativeStdIO',
      },
    },
  },
};
