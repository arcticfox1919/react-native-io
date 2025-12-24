module.exports = {
  projects: [
    // Classic Jest tests
    {
      preset: 'react-native',
      testMatch: ['**/__tests__/**/*.test.{js,ts,tsx}'],
    },
    // Harness tests (run on real device/emulator)
    {
      preset: 'react-native-harness',
      testMatch: ['**/__tests__/**/*.harness.{js,ts,tsx}'],
    },
  ],
};
