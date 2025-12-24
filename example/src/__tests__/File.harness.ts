/**
 * @file File.harness.ts
 * @description Harness tests for File class - runs on real device/emulator
 */

import {
  describe,
  it,
  expect,
  beforeAll,
  afterAll,
} from 'react-native-harness';
import {
  EntityType,
  HashAlgorithm,
  FS,
  openFS,
  type FSContext,
} from 'react-native-io';

describe('File', () => {
  // Use app's cache directory which has write permission
  let tempDir: string;
  let testFilePath: string;
  let testBinaryPath: string;
  let fs: FSContext;

  // Create shared file system context with thread pool
  beforeAll(() => {
    console.log('[File.harness] beforeAll started');
    fs = openFS(2);
    tempDir = FS.cacheDir;
    testFilePath = `${tempDir}/rn-io-test-file.txt`;
    testBinaryPath = `${tempDir}/rn-io-test-binary.bin`;
    console.log('[File.harness] beforeAll completed, tempDir:', tempDir);
  });

  afterAll(async () => {
    console.log('[File.harness] afterAll started');
    // Clean up test files
    try {
      const file = fs.file(testFilePath);
      const binaryFile = fs.file(testBinaryPath);
      if (await file.exists()) {
        await file.delete();
      }
      if (await binaryFile.exists()) {
        await binaryFile.delete();
      }
    } catch {
      // Ignore cleanup errors
    }
    console.log('[File.harness] afterAll completed');
    // Note: We don't close fs here as harness framework might call afterAll
    // before all tests are complete. The FSContext will be cleaned up by GC.
  });

  describe('Path properties', () => {
    it('should return correct path', () => {
      console.log('[File.harness] Test: should return correct path');
      const filePath = `${tempDir}/myfile.txt`;
      const file = fs.file(filePath);
      expect(file.path).toBe(filePath);
      console.log('[File.harness] Test: should return correct path - DONE');
    });

    it('should return correct name', () => {
      console.log('[File.harness] Test: should return correct name');
      const file = fs.file(`${tempDir}/myfile.txt`);
      expect(file.name).toBe('myfile.txt');
      console.log('[File.harness] Test: should return correct name - DONE');
    });

    it('should return correct extension with dot', () => {
      console.log('[File.harness] Test: should return correct extension');
      const file = fs.file(`${tempDir}/myfile.txt`);
      expect(file.extension).toBe('.txt');
      console.log(
        '[File.harness] Test: should return correct extension - DONE'
      );
    });

    it('should return empty extension for files without extension', () => {
      console.log('[File.harness] Test: empty extension');
      const file = fs.file(`${tempDir}/myfile`);
      expect(file.extension).toBe('');
      console.log('[File.harness] Test: empty extension - DONE');
    });

    it('should return correct nameWithoutExtension', () => {
      console.log('[File.harness] Test: nameWithoutExtension');
      const file = fs.file(`${tempDir}/myfile.txt`);
      expect(file.nameWithoutExtension).toBe('myfile');
      console.log('[File.harness] Test: nameWithoutExtension - DONE');
    });

    it('should return correct parent path', () => {
      console.log('[File.harness] Test: parent path');
      const file = fs.file(`${tempDir}/myfile.txt`);
      expect(file.parent).toBe(tempDir);
      console.log('[File.harness] Test: parent path - DONE');
    });
  });

  describe('File operations', () => {
    it('should check if file exists (false)', async () => {
      console.log('[File.harness] Test: file exists (false)');
      const file = fs.file(`${tempDir}/non-existent-file-12345.txt`);
      const exists = await file.exists();
      expect(exists).toBe(false);
      console.log('[File.harness] Test: file exists (false) - DONE');
    });

    it('should write and read string content', async () => {
      console.log('[File.harness] Test: write and read string');
      const file = fs.file(testFilePath);
      const content = 'Hello, React Native IO!';

      await file.writeString(content);
      expect(await file.exists()).toBe(true);

      const readContent = await file.readString();
      expect(readContent).toBe(content);
      console.log('[File.harness] Test: write and read string - DONE');
    });

    it('should write and read binary content', async () => {
      console.log('[File.harness] Test: write and read binary');
      const file = fs.file(testBinaryPath);
      const data = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]); // "Hello"

      await file.writeBytes(data.buffer);
      expect(await file.exists()).toBe(true);

      const buffer = await file.readBytes();
      const readData = new Uint8Array(buffer);
      expect(readData.length).toBe(5);
      expect(readData[0]).toBe(0x48);
      expect(readData[4]).toBe(0x6f);
      console.log('[File.harness] Test: write and read binary - DONE');
    });

    it('should get file metadata', async () => {
      console.log('[File.harness] Test: get file metadata');
      const file = fs.file(testFilePath);
      await file.writeString('Test content');

      const metadata = await file.metadata();
      expect(metadata.size).toBeGreaterThan(0);
      expect(metadata.type).toBe(EntityType.File);
      expect(metadata.modifiedTime).toBeGreaterThan(0);
      console.log('[File.harness] Test: get file metadata - DONE');
    });

    it('should get file size', async () => {
      console.log('[File.harness] Test: get file size');
      const file = fs.file(testFilePath);
      const content = 'Test content';
      await file.writeString(content);

      const size = await file.size();
      expect(size).toBe(content.length);
      console.log('[File.harness] Test: get file size - DONE');
    });

    it('should delete file', async () => {
      console.log('[File.harness] Test: delete file');
      const deletePath = `${tempDir}/rn-io-test-delete.txt`;
      const file = fs.file(deletePath);
      await file.writeString('To be deleted');
      expect(await file.exists()).toBe(true);

      await file.delete();
      expect(await file.exists()).toBe(false);
      console.log('[File.harness] Test: delete file - DONE');
    });

    it('should copy file', async () => {
      console.log('[File.harness] Test: copy file');
      const source = fs.file(testFilePath);
      const destPath = `${tempDir}/rn-io-test-copy.txt`;

      try {
        await source.writeString('Copy me');
        const dest = await source.copy(destPath);

        expect(await dest.exists()).toBe(true);
        expect(await dest.readString()).toBe('Copy me');
        console.log('[File.harness] Test: copy file - DONE');
      } finally {
        try {
          const destFile = fs.file(destPath);
          await destFile.delete();
        } catch {}
      }
    });

    it('should rename/move file', async () => {
      console.log('[File.harness] Test: rename/move file');
      const sourcePath = `${tempDir}/rn-io-test-move-src.txt`;
      const destPath = `${tempDir}/rn-io-test-renamed.txt`;
      const source = fs.file(sourcePath);

      try {
        await source.writeString('Move me');
        const dest = await source.move(destPath);

        expect(await source.exists()).toBe(false);
        expect(await dest.exists()).toBe(true);
        expect(await dest.readString()).toBe('Move me');
        console.log('[File.harness] Test: rename/move file - DONE');
      } finally {
        try {
          const destFile = fs.file(destPath);
          await destFile.delete();
        } catch {}
      }
    });

    it('should append content', async () => {
      console.log('[File.harness] Test: append content');
      const file = fs.file(testFilePath);
      await file.writeString('Hello');
      await file.appendString(' World');

      const content = await file.readString();
      expect(content).toBe('Hello World');
      console.log('[File.harness] Test: append content - DONE');
    });
  });

  describe('Hash operations', () => {
    it('should calculate MD5 hash', async () => {
      console.log('[File.harness] Test: MD5 hash');
      const file = fs.file(testFilePath);
      await file.writeString('Hello');

      const hash = await file.calcHash(HashAlgorithm.MD5);
      expect(hash).toBe('8b1a9953c4611296a827abf8c47804d7');
      console.log('[File.harness] Test: MD5 hash - DONE');
    });

    it('should calculate SHA256 hash', async () => {
      console.log('[File.harness] Test: SHA256 hash');
      const file = fs.file(testFilePath);
      await file.writeString('Hello');

      const hash = await file.calcHash(HashAlgorithm.SHA256);
      expect(hash).toBe(
        '185f8db32271fe25f561a6fc938b2e264306ec304eda518007d1764826381969'
      );
      console.log('[File.harness] Test: SHA256 hash - DONE');
    });
  });

  describe('Sync operations', () => {
    it('should write and read string synchronously', () => {
      console.log('[File.harness] Test: sync write and read');
      const file = fs.file(testFilePath);
      const content = 'Sync content';

      file.writeStringSync(content);
      expect(file.existsSync()).toBe(true);

      const readContent = file.readStringSync();
      expect(readContent).toBe(content);
      console.log('[File.harness] Test: sync write and read - DONE');
    });

    it('should get metadata synchronously', () => {
      console.log('[File.harness] Test: sync metadata');
      const file = fs.file(testFilePath);
      file.writeStringSync('Test');

      const metadata = file.metadataSync();
      expect(metadata.type).toBe(EntityType.File);
      console.log('[File.harness] Test: sync metadata - DONE');
    });
  });
});
