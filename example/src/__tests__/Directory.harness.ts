/**
 * @file Directory.harness.ts
 * @description Harness tests for Directory class - runs on real device/emulator
 */

import {
  describe,
  it,
  expect,
  beforeAll,
  afterAll,
} from 'react-native-harness';
import { EntityType, FS, openFS, type FSContext } from 'react-native-io';

describe('Directory', () => {
  let tempDir: string;
  let baseTestDir: string;
  let fs: FSContext;
  let testCounter = 0;

  // Helper to get unique test directory for each test
  const getTestDir = () => {
    testCounter++;
    return `${baseTestDir}/test_${testCounter}`;
  };

  // Create shared file system context with thread pool
  beforeAll(async () => {
    fs = openFS(2);
    tempDir = FS.cacheDir;
    baseTestDir = `${tempDir}/rn-io-dir-tests`;

    // Clean up any leftover from previous runs
    const dir = fs.directory(baseTestDir);
    try {
      if (await dir.exists()) {
        await dir.delete(true);
      }
    } catch {
      // Ignore cleanup errors
    }
    // Create base test directory
    await dir.create();
  });

  afterAll(async () => {
    // Clean up all test directories
    try {
      const dir = fs.directory(baseTestDir);
      if (await dir.exists()) {
        await dir.delete(true);
      }
    } catch {
      // Ignore cleanup errors
    }
    // Note: We don't close fs here as other test suites might still be running
    // The FSContext will be cleaned up by GC
  });

  describe('Path properties', () => {
    it('should return correct path', () => {
      const dirPath = `${tempDir}/mydir`;
      const dir = fs.directory(dirPath);
      expect(dir.path).toBe(dirPath);
    });

    it('should return correct name', () => {
      const dir = fs.directory(`${tempDir}/mydir`);
      expect(dir.name).toBe('mydir');
    });

    it('should return correct parent path', () => {
      const dir = fs.directory(`${tempDir}/mydir`);
      expect(dir.parentPath).toBe(tempDir);
    });

    it('should return correct parent directory', () => {
      const dir = fs.directory(`${tempDir}/mydir`);
      expect(dir.parent.path).toBe(tempDir);
    });
  });

  describe('Directory operations', () => {
    it('should check if directory exists (false)', async () => {
      const dir = fs.directory(`${tempDir}/non-existent-dir-12345`);
      const exists = await dir.exists();
      expect(exists).toBe(false);
    });

    it('should create directory', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);

      await dir.create();
      expect(await dir.exists()).toBe(true);
    });

    it('should create nested directories recursively', async () => {
      const testDir = getTestDir();
      const nestedDir = fs.directory(`${testDir}/nested/deep`);

      await nestedDir.create(true);
      expect(await nestedDir.exists()).toBe(true);
    });

    it('should delete empty directory', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();
      expect(await dir.exists()).toBe(true);

      await dir.delete();
      expect(await dir.exists()).toBe(false);
    });

    it('should delete directory recursively', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      // Create a file inside
      const file = fs.file(`${testDir}/test.txt`);
      await file.writeString('Test');

      // Create a nested directory
      const nestedDir = fs.directory(`${testDir}/nested`);
      await nestedDir.create();

      // Delete recursively
      await dir.delete(true);
      expect(await dir.exists()).toBe(false);
    });

    it('should get directory metadata', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      const meta = await dir.metadata();
      expect(meta.type).toBe(EntityType.Directory);
    });
  });

  describe('List operations', () => {
    it('should list directory contents', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      // Create some files and directories
      const file1 = fs.file(`${testDir}/file1.txt`);
      const file2 = fs.file(`${testDir}/file2.txt`);
      const subDir = fs.directory(`${testDir}/subdir`);

      await file1.writeString('File 1');
      await file2.writeString('File 2');
      await subDir.create();

      const entries = await dir.list();
      expect(entries.length).toBe(3);

      const names = entries.map((e) => e.name).sort();
      expect(names).toEqual(['file1.txt', 'file2.txt', 'subdir']);
    });

    it('should list only files', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      const file1 = fs.file(`${testDir}/file1.txt`);
      const subDir = fs.directory(`${testDir}/subdir`);
      await file1.writeString('File 1');
      await subDir.create();

      const files = await dir.listFiles();
      expect(files.length).toBe(1);
      expect(files[0]!.name).toBe('file1.txt');
      expect(files[0]!.type).toBe(EntityType.File);
    });

    it('should list only directories', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      const file1 = fs.file(`${testDir}/file1.txt`);
      const subDir = fs.directory(`${testDir}/subdir`);
      await file1.writeString('File 1');
      await subDir.create();

      const dirs = await dir.listDirectories();
      expect(dirs.length).toBe(1);
      expect(dirs[0]!.name).toBe('subdir');
      expect(dirs[0]!.type).toBe(EntityType.Directory);
    });
  });

  describe('Sync operations', () => {
    it('should create and check directory synchronously', () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);

      dir.createSync();
      expect(dir.existsSync()).toBe(true);
    });

    it('should list directory contents synchronously', () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      dir.createSync();

      const file = fs.file(`${testDir}/sync-file.txt`);
      file.writeStringSync('Sync content');

      const entries = dir.listSync();
      expect(entries.length).toBe(1);
      expect(entries[0]!.name).toBe('sync-file.txt');
    });
  });

  describe('File creation helpers', () => {
    it('should create child file using file() method', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      const file = dir.file('child.txt');
      expect(file.path).toBe(`${testDir}/child.txt`);

      await file.writeString('Child content');
      expect(await file.exists()).toBe(true);
    });

    it('should create child directory using directory() method', async () => {
      const testDir = getTestDir();
      const dir = fs.directory(testDir);
      await dir.create();

      const subDir = dir.directory('subdir');
      expect(subDir.path).toBe(`${testDir}/subdir`);

      await subDir.create();
      expect(await subDir.exists()).toBe(true);
    });
  });
});
