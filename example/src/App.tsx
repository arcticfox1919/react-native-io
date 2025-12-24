import { useEffect, useState, useCallback } from 'react';
import {
  Text,
  View,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
} from 'react-native';
import {
  FS,
  EntityType,
  WriteMode,
  HashAlgorithm,
  request,
  openFS,
} from 'react-native-io';

// Test result type
type TestResult = {
  name: string;
  status: 'pending' | 'pass' | 'fail';
  error?: string;
  duration?: number; // milliseconds
};

export default function App() {
  const [results, setResults] = useState<TestResult[]>([]);
  const [running, setRunning] = useState(false);

  const updateResult = (
    name: string,
    status: 'pass' | 'fail',
    error?: string,
    duration?: number
  ) => {
    setResults((prev) =>
      prev.map((r) => (r.name === name ? { ...r, status, error, duration } : r))
    );
  };

  const runTests = useCallback(async () => {
    const tempDir = FS.tempDir;
    console.log('Using temp directory:', tempDir);
    const testDir = `${tempDir}/io_test_${Date.now()}`;

    // Create shared file system context with thread pool
    const fs = openFS(2); // 2 worker threads for parallel operations

    // Helper to create File/Directory using shared context
    const file = (path: string) => fs.file(path);
    const dir = (path: string) => fs.directory(path);

    // Warm up: ensure thread pool, native module, and write paths are fully initialized
    // This operation is not counted in any test timing
    const warmupDir = dir(`${tempDir}/warmup_${Date.now()}`);
    await warmupDir.create();
    await warmupDir.delete();

    // Define all tests
    const tests: { name: string; fn: () => Promise<void> }[] = [
      // ========== Directory Tests ==========
      {
        name: 'Directory.create() async',
        fn: async () => {
          const d = dir(testDir);
          await d.create(true);
          if (!(await d.exists())) throw new Error('Directory not created');
        },
      },
      {
        name: 'Directory.create() sync',
        fn: async () => {
          const d = dir(`${testDir}_sync`);
          d.createSync(true);
          if (!d.existsSync()) throw new Error('Directory not created');
        },
      },
      {
        name: 'Directory.exists() async',
        fn: async () => {
          const d = dir(testDir);
          if (!(await d.exists())) throw new Error('Should exist');
        },
      },
      {
        name: 'Directory.exists() sync',
        fn: async () => {
          const d = dir(testDir);
          if (!d.existsSync()) throw new Error('Should exist');
        },
      },
      {
        name: 'Directory.isDirectory() async',
        fn: async () => {
          const d = dir(testDir);
          if (!(await d.isDirectory())) throw new Error('Should be directory');
        },
      },
      {
        name: 'Directory.isDirectory() sync',
        fn: async () => {
          const d = dir(testDir);
          if (!d.isDirectorySync()) throw new Error('Should be directory');
        },
      },
      {
        name: 'Directory.name',
        fn: async () => {
          const d = dir(testDir);
          if (!d.name.startsWith('io_test_')) throw new Error('Invalid name');
        },
      },
      {
        name: 'Directory.parent',
        fn: async () => {
          const d = dir(testDir);
          if (!d.parentPath) throw new Error('Should have parent');
        },
      },
      {
        name: 'Directory.absolutePath()',
        fn: async () => {
          const d = dir(testDir);
          const abs = await d.absolutePath();
          if (!abs.includes('io_test_'))
            throw new Error('Invalid absolute path');
        },
      },
      {
        name: 'Directory.metadata()',
        fn: async () => {
          const d = dir(testDir);
          const meta = await d.metadata();
          if (meta.type !== EntityType.Directory) throw new Error('Wrong type');
        },
      },

      // ========== File Creation & Write Tests ==========
      {
        name: 'File.create() async',
        fn: async () => {
          const f = file(`${testDir}/test.txt`);
          await f.create();
          if (!(await f.exists())) throw new Error('File not created');
        },
      },
      {
        name: 'File.create() sync',
        fn: async () => {
          const f = file(`${testDir}/test_sync.txt`);
          f.createSync();
          if (!f.existsSync()) throw new Error('File not created');
        },
      },
      {
        name: 'File.writeString() async',
        fn: async () => {
          const f = file(`${testDir}/write_string.txt`);
          await f.writeString('Hello, World!');
          const content = await f.readString();
          if (content !== 'Hello, World!') throw new Error('Content mismatch');
        },
      },
      {
        name: 'File.writeString() sync',
        fn: async () => {
          const f = file(`${testDir}/write_string_sync.txt`);
          f.writeStringSync('Hello, World!');
          const content = await f.readString();
          if (content !== 'Hello, World!') throw new Error('Content mismatch');
        },
      },
      {
        name: 'File.writeString() Append async',
        fn: async () => {
          const f = file(`${testDir}/append.txt`);
          await f.writeString('Hello');
          await f.writeString(', World!', WriteMode.Append);
          const content = await f.readString();
          if (content !== 'Hello, World!') throw new Error('Append failed');
        },
      },
      {
        name: 'File.appendString()',
        fn: async () => {
          const f = file(`${testDir}/append2.txt`);
          await f.writeString('A');
          await f.appendString('B');
          if ((await f.readString()) !== 'AB') throw new Error('Append failed');
        },
      },
      {
        name: 'File.writeBytes() async',
        fn: async () => {
          const f = file(`${testDir}/bytes.bin`);
          const data = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]).buffer;
          await f.writeBytes(data);
          const read = new Uint8Array(await f.readBytes());
          if (read.length !== 5 || read[0] !== 0x48)
            throw new Error('Bytes mismatch');
        },
      },
      {
        name: 'File.writeBytes() sync',
        fn: async () => {
          const f = file(`${testDir}/bytes_sync.bin`);
          const data = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]).buffer;
          f.writeBytesSync(data);
          const read = new Uint8Array(await f.readBytes());
          if (read.length !== 5 || read[0] !== 0x48)
            throw new Error('Bytes mismatch');
        },
      },

      // ========== File Read Tests ==========
      {
        name: 'File.readString() async',
        fn: async () => {
          const f = file(`${testDir}/read.txt`);
          await f.writeString('Test Content');
          const content = await f.readString();
          if (content !== 'Test Content') throw new Error('Read failed');
        },
      },
      {
        name: 'File.readString() sync',
        fn: async () => {
          const f = file(`${testDir}/read_sync.txt`);
          await f.writeString('Test Content');
          const content = f.readStringSync();
          if (content !== 'Test Content') throw new Error('Read failed');
        },
      },
      {
        name: 'File.readBytes() async',
        fn: async () => {
          const f = file(`${testDir}/read_bytes.bin`);
          await f.writeString('ABC');
          const bytes = new Uint8Array(await f.readBytes());
          if (bytes[0] !== 65) throw new Error('Read bytes failed');
        },
      },
      {
        name: 'File.readBytes() sync',
        fn: async () => {
          const f = file(`${testDir}/read_bytes_sync.bin`);
          await f.writeString('ABC');
          const bytes = new Uint8Array(f.readBytesSync());
          if (bytes[0] !== 65) throw new Error('Read bytes failed');
        },
      },
      {
        name: 'File.readLines() async',
        fn: async () => {
          const f = file(`${testDir}/lines.txt`);
          await f.writeString('Line1\nLine2\nLine3');
          const lines = await f.readLines();
          if (lines.length !== 3 || lines[1] !== 'Line2')
            throw new Error('Lines mismatch');
        },
      },
      {
        name: 'File.readLines() sync',
        fn: async () => {
          const f = file(`${testDir}/lines_sync.txt`);
          await f.writeString('Line1\nLine2\nLine3');
          const lines = f.readLinesSync();
          if (lines.length !== 3 || lines[1] !== 'Line2')
            throw new Error('Lines mismatch');
        },
      },

      // ========== File Properties Tests ==========
      {
        name: 'File.exists() async',
        fn: async () => {
          const f = file(`${testDir}/exists.txt`);
          await f.writeString('test');
          if (!(await f.exists())) throw new Error('Should exist');
        },
      },
      {
        name: 'File.exists() sync',
        fn: async () => {
          const f = file(`${testDir}/exists_sync.txt`);
          await f.writeString('test');
          if (!f.existsSync()) throw new Error('Should exist');
        },
      },
      {
        name: 'File.isFile() async',
        fn: async () => {
          const f = file(`${testDir}/isfile.txt`);
          await f.writeString('test');
          if (!(await f.isFile())) throw new Error('Should be file');
        },
      },
      {
        name: 'File.isFile() sync',
        fn: async () => {
          const f = file(`${testDir}/isfile_sync.txt`);
          await f.writeString('test');
          if (!f.isFileSync()) throw new Error('Should be file');
        },
      },
      {
        name: 'File.name',
        fn: async () => {
          const f = file(`${testDir}/myfile.txt`);
          if (f.name !== 'myfile.txt') throw new Error('Wrong name');
        },
      },
      {
        name: 'File.extension',
        fn: async () => {
          const f = file(`${testDir}/myfile.txt`);
          if (f.extension !== '.txt') throw new Error('Wrong extension');
        },
      },
      {
        name: 'File.nameWithoutExtension',
        fn: async () => {
          const f = file(`${testDir}/myfile.txt`);
          if (f.nameWithoutExtension !== 'myfile')
            throw new Error('Wrong name');
        },
      },
      {
        name: 'File.parent',
        fn: async () => {
          const f = file(`${testDir}/myfile.txt`);
          if (!f.parent.includes('io_test_')) throw new Error('Wrong parent');
        },
      },
      {
        name: 'File.size() async',
        fn: async () => {
          const f = file(`${testDir}/size.txt`);
          await f.writeString('12345');
          const size = await f.size();
          if (size !== 5) throw new Error(`Wrong size: ${size}`);
        },
      },
      {
        name: 'File.size() sync',
        fn: async () => {
          const f = file(`${testDir}/size_sync.txt`);
          await f.writeString('12345');
          const size = f.sizeSync();
          if (size !== 5) throw new Error(`Wrong size: ${size}`);
        },
      },
      {
        name: 'File.metadata()',
        fn: async () => {
          const f = file(`${testDir}/meta.txt`);
          await f.writeString('test');
          const meta = await f.metadata();
          if (meta.type !== EntityType.File) throw new Error('Wrong type');
          if (meta.size !== 4) throw new Error('Wrong size');
        },
      },
      {
        name: 'File.modifiedTime()',
        fn: async () => {
          const f = file(`${testDir}/time.txt`);
          await f.writeString('test');
          const time = await f.modifiedTime();
          const diff = Date.now() - time.getTime();
          if (diff > 60000) throw new Error('Time too old');
        },
      },
      {
        name: 'File.absolutePath()',
        fn: async () => {
          const f = file(`${testDir}/abs.txt`);
          await f.writeString('test');
          const abs = await f.absolutePath();
          if (!abs.includes('abs.txt')) throw new Error('Invalid path');
        },
      },

      // ========== File Operations Tests ==========
      {
        name: 'File.copy()',
        fn: async () => {
          const src = file(`${testDir}/copy_src.txt`);
          await src.writeString('copy me');
          const dst = await src.copy(`${testDir}/copy_dst.txt`);
          if ((await dst.readString()) !== 'copy me')
            throw new Error('Copy failed');
        },
      },
      {
        name: 'File.move()',
        fn: async () => {
          const src = file(`${testDir}/move_src.txt`);
          await src.writeString('move me');
          const dst = await src.move(`${testDir}/move_dst.txt`);
          if (await src.exists()) throw new Error('Source should not exist');
          if ((await dst.readString()) !== 'move me')
            throw new Error('Move failed');
        },
      },
      {
        name: 'File.delete() async',
        fn: async () => {
          const f = file(`${testDir}/delete.txt`);
          await f.writeString('delete me');
          await f.delete();
          if (await f.exists()) throw new Error('Should be deleted');
        },
      },
      {
        name: 'File.delete() sync',
        fn: async () => {
          const f = file(`${testDir}/delete_sync.txt`);
          await f.writeString('delete me');
          f.deleteSync();
          if (f.existsSync()) throw new Error('Should be deleted');
        },
      },
      {
        name: 'File.calcHash() MD5',
        fn: async () => {
          const f = file(`${testDir}/hash.txt`);
          await f.writeString('hello');
          const hash = await f.calcHash(HashAlgorithm.MD5);
          if (hash !== '5d41402abc4b2a76b9719d911017c592')
            throw new Error('Wrong MD5');
        },
      },
      {
        name: 'File.calcHash() SHA256',
        fn: async () => {
          const f = file(`${testDir}/hash256.txt`);
          await f.writeString('hello');
          const hash = await f.calcHash(HashAlgorithm.SHA256);
          if (!hash.startsWith('2cf24dba')) throw new Error('Wrong SHA256');
        },
      },

      // ========== Directory List Tests ==========
      {
        name: 'Directory.list() async',
        fn: async () => {
          const d = dir(testDir);
          const entries = await d.list();
          if (entries.length === 0) throw new Error('Should have entries');
        },
      },
      {
        name: 'Directory.list() sync',
        fn: async () => {
          const d = dir(testDir);
          const entries = d.listSync();
          if (entries.length === 0) throw new Error('Should have entries');
        },
      },
      {
        name: 'Directory.listFiles() async',
        fn: async () => {
          const d = dir(testDir);
          const files = await d.listFiles();
          if (files.length === 0) throw new Error('Should have files');
        },
      },
      {
        name: 'Directory.listFiles() sync',
        fn: async () => {
          const d = dir(testDir);
          const files = d.listFilesSync();
          if (files.length === 0) throw new Error('Should have files');
        },
      },
      {
        name: 'Directory.listDirectories()',
        fn: async () => {
          const subDir = dir(`${testDir}/subdir`);
          await subDir.create();
          const d = dir(testDir);
          const dirs = await d.listDirectories();
          if (dirs.length === 0) throw new Error('Should have subdirectory');
        },
      },
      {
        name: 'Directory.isEmpty()',
        fn: async () => {
          const empty = dir(`${testDir}/empty`);
          await empty.create();
          if (!(await empty.isEmpty())) throw new Error('Should be empty');
        },
      },
      {
        name: 'Directory.getTotalSize()',
        fn: async () => {
          const d = dir(testDir);
          const size = await d.getTotalSize();
          if (size === 0) throw new Error('Should have size > 0');
        },
      },

      // ========== Directory Child Access Tests ==========
      {
        name: 'Directory.file()',
        fn: async () => {
          const d = dir(testDir);
          const f = d.file('child.txt');
          await f.writeString('child');
          if ((await f.readString()) !== 'child')
            throw new Error('Child file failed');
        },
      },
      {
        name: 'Directory.directory()',
        fn: async () => {
          const d = dir(testDir);
          const sub = d.directory('nested');
          await sub.create();
          if (!(await sub.exists())) throw new Error('Nested dir failed');
        },
      },

      // ========== FS Utility Tests ==========
      {
        name: 'FS.joinPaths()',
        fn: async () => {
          const path = FS.joinPaths('/a', 'b', 'c.txt');
          if (!path.includes('b')) throw new Error('Join failed');
        },
      },
      {
        name: 'FS.getAvailableSpace()',
        fn: async () => {
          const space = await FS.getAvailableSpace(tempDir);
          console.log('Available space:', space);
          if (space <= 0) throw new Error('Should have space');
        },
      },
      {
        name: 'FS.getTotalSpace()',
        fn: async () => {
          const space = await FS.getTotalSpace(tempDir);
          console.log('Total space:', space);
          if (space <= 0) throw new Error('Should have total space');
        },
      },

      // ========== HTTP Request Tests ==========
      {
        name: 'request.get()',
        fn: async () => {
          console.log('[HTTP Test] Starting GET request to httpbin.org/get');
          try {
            const res = await request.get('https://httpbin.org/get?foo=bar');
            const bodyBuffer = res.arrayBuffer();
            console.log(
              '[HTTP Test] GET response:',
              JSON.stringify({
                ok: res.ok,
                status: res.status,
                statusText: res.statusText,
                error: res.error,
                bodyByteLength: bodyBuffer.byteLength,
              })
            );
            if (!res.ok) {
              throw new Error(
                `HTTP error: ${res.status}, statusText: ${res.statusText}, err: ${res.error}`
              );
            }
            // Verify we received body data
            if (bodyBuffer.byteLength === 0) {
              throw new Error('Response body is empty');
            }
            const data = res.json<{ args: { foo: string } }>();
            if (data.args.foo !== 'bar') throw new Error('GET params mismatch');
          } catch (e) {
            console.error('[HTTP Test] GET request failed:', e);
            throw e;
          }
        },
      },
      {
        name: 'request.post() JSON',
        fn: async () => {
          console.log('[HTTP Test] Starting POST request to httpbin.org/post');
          try {
            const res = await request.post('https://httpbin.org/post', {
              json: { name: 'test', value: 123 },
            });
            const bodyBuffer = res.arrayBuffer();
            console.log(
              '[HTTP Test] POST response:',
              JSON.stringify({
                ok: res.ok,
                status: res.status,
                statusText: res.statusText,
                error: res.error,
                bodyByteLength: bodyBuffer.byteLength,
              })
            );
            if (!res.ok) {
              throw new Error(
                `HTTP error: ${res.status}, statusText: ${res.statusText}, err: ${res.error}`
              );
            }
            // Verify we received body data
            if (bodyBuffer.byteLength === 0) {
              throw new Error('Response body is empty');
            }
            const text = res.text();
            console.log(
              '[HTTP Test] POST text (first 200 chars):',
              text.substring(0, 200)
            );
            const data = res.json<{ json: { name: string; value: number } }>();
            console.log('[HTTP Test] POST parsed JSON:', JSON.stringify(data));
            if (data.json.name !== 'test' || data.json.value !== 123) {
              throw new Error('POST JSON mismatch');
            }
          } catch (e) {
            console.error('[HTTP Test] POST request failed:', e);
            throw e;
          }
        },
      },

      // ========== Cleanup Test ==========
      {
        name: 'Directory.delete() recursive',
        fn: async () => {
          const d = dir(testDir);
          const count = await d.delete(true);
          if (await d.exists()) throw new Error('Should be deleted');
          if (count === 0) throw new Error('Should delete items');
        },
      },
    ];

    // Initialize results
    setResults(tests.map((t) => ({ name: t.name, status: 'pending' })));
    setRunning(true);

    // Run tests sequentially
    for (const test of tests) {
      const startTime = performance.now();
      try {
        await test.fn();
        const duration = performance.now() - startTime;
        updateResult(test.name, 'pass', undefined, duration);
      } catch (e: any) {
        const duration = performance.now() - startTime;
        updateResult(test.name, 'fail', e.message || String(e), duration);
      }
    }

    // Close the shared file system context
    fs.close();

    setRunning(false);
  }, []);

  useEffect(() => {
    runTests();
  }, [runTests]);

  const passCount = results.filter((r) => r.status === 'pass').length;
  const failCount = results.filter((r) => r.status === 'fail').length;

  return (
    <View style={styles.container}>
      <View style={styles.header}>
        <Text style={styles.title}>react-native-io Tests</Text>
        <Text style={styles.summary}>
          ✅ {passCount} | ❌ {failCount} | 📊 {results.length}
        </Text>
        <TouchableOpacity
          style={[styles.button, running && styles.buttonDisabled]}
          onPress={runTests}
          disabled={running}
        >
          <Text style={styles.buttonText}>
            {running ? 'Running...' : 'Run Tests'}
          </Text>
        </TouchableOpacity>
      </View>

      <ScrollView style={styles.list}>
        {results.map((r, i) => (
          <View key={i} style={styles.item}>
            <Text style={styles.icon}>
              {r.status === 'pass' ? '✅' : r.status === 'fail' ? '❌' : '⏳'}
            </Text>
            <View style={styles.itemContent}>
              <View style={styles.itemHeader}>
                <Text style={styles.itemName}>{r.name}</Text>
                {r.duration !== undefined && (
                  <Text style={styles.duration}>{r.duration.toFixed(2)}ms</Text>
                )}
              </View>
              {r.error && <Text style={styles.error}>{r.error}</Text>}
            </View>
          </View>
        ))}
      </ScrollView>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {
    flex: 1,
    backgroundColor: '#f5f5f5',
  },
  header: {
    padding: 20,
    paddingTop: 50,
    backgroundColor: '#fff',
    borderBottomWidth: 1,
    borderBottomColor: '#ddd',
  },
  title: {
    fontSize: 20,
    fontWeight: 'bold',
    marginBottom: 8,
  },
  summary: {
    fontSize: 16,
    color: '#666',
    marginBottom: 12,
  },
  button: {
    backgroundColor: '#007AFF',
    padding: 12,
    borderRadius: 8,
    alignItems: 'center',
  },
  buttonDisabled: {
    backgroundColor: '#ccc',
  },
  buttonText: {
    color: '#fff',
    fontWeight: '600',
  },
  list: {
    flex: 1,
    padding: 12,
  },
  item: {
    flexDirection: 'row',
    backgroundColor: '#fff',
    padding: 12,
    marginBottom: 8,
    borderRadius: 8,
    alignItems: 'flex-start',
  },
  icon: {
    fontSize: 16,
    marginRight: 10,
  },
  itemContent: {
    flex: 1,
  },
  itemHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  itemName: {
    fontSize: 14,
    fontWeight: '500',
    flex: 1,
  },
  duration: {
    fontSize: 12,
    color: '#888',
    marginLeft: 8,
  },
  error: {
    fontSize: 12,
    color: '#ff3b30',
    marginTop: 4,
  },
});
