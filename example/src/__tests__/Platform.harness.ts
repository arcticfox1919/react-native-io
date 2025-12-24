/**
 * @file Platform.harness.ts
 * @description Harness tests for Platform API - runs on real device/emulator
 */

import { describe, it, expect } from 'react-native-harness';
import { Platform } from 'react-native';

// Import platform utilities from react-native-io
import { IOPlatformType } from 'react-native-io';

describe('Platform API', () => {
  describe('Platform detection', () => {
    it('should detect correct platform', () => {
      expect(Platform.OS).toMatch(/ios|android/);
    });

    it('should have platform version', () => {
      expect(Platform.Version).toBeDefined();
    });
  });

  describe('IOPlatformType enum', () => {
    it('should have iOS type', () => {
      expect(IOPlatformType.iOS).toBeDefined();
    });

    it('should have Android type', () => {
      expect(IOPlatformType.Android).toBeDefined();
    });
  });
});
