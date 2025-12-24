import { useEffect, useState, useCallback } from 'react';
import {
  Text,
  View,
  StyleSheet,
  ScrollView,
  TouchableOpacity,
} from 'react-native';
import { FS, HashAlgorithm, request, openFS } from 'react-native-io';

type TestResult = {
  name: string;
  status: 'pending' | 'pass' | 'fail';
  error?: string;
  duration?: number;
};

type TestDef = {
  name: string;
  setup?: () => Promise<void>;
  fn: () => Promise<void> | void;
  verify?: () => Promise<void> | void;
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
    const testDir = `${tempDir}/io_test_${Date.now()}`;
    const fs = openFS(5);
    const file = (path: string) => fs.file(path);
    const dir = (path: string) => fs.directory(path);

    // Warm up
    const warmupDir = dir(`${tempDir}/warmup_${Date.now()}`);
    await warmupDir.create();
    await warmupDir.delete();

    // Shared test data
    let testFileContent = '';
    let testFileBytes: ArrayBuffer;

    const tests: TestDef[] = [
      // ==================== Directory Tests ====================
      {
        name: 'Directory.create() async',
        fn: async () => {
          await dir(testDir).create(true);
        },
        verify: async () => {
          if (!(await dir(testDir).exists())) throw new Error('Not created');
        },
      },
      {
        name: 'Directory.create() sync',
        fn: () => {
          dir(`${testDir}/sync_dir`).createSync(true);
        },
        verify: () => {
          if (!dir(`${testDir}/sync_dir`).existsSync())
            throw new Error('Not created');
        },
      },
      {
        name: 'Directory.exists() async',
        fn: async () => {
          await dir(testDir).exists();
        },
      },
      {
        name: 'Directory.exists() sync',
        fn: () => {
          dir(testDir).existsSync();
        },
      },
      {
        name: 'Directory.isDirectory() async',
        fn: async () => {
          await dir(testDir).isDirectory();
        },
      },
      {
        name: 'Directory.isDirectory() sync',
        fn: () => {
          dir(testDir).isDirectorySync();
        },
      },
      {
        name: 'Directory.metadata() async',
        fn: async () => {
          await dir(testDir).metadata();
        },
      },
      {
        name: 'Directory.metadata() sync',
        fn: () => {
          dir(testDir).metadataSync();
        },
      },
      {
        name: 'Directory.absolutePath() async',
        fn: async () => {
          await dir(testDir).absolutePath();
        },
      },
      {
        name: 'Directory.absolutePath() sync',
        fn: () => {
          dir(testDir).absolutePathSync();
        },
      },
      {
        name: 'Directory.list() async',
        setup: async () => {
          await file(`${testDir}/list_test/a.txt`).writeString('a');
          await file(`${testDir}/list_test/b.txt`).writeString('b');
        },
        fn: async () => {
          await dir(`${testDir}/list_test`).list();
        },
      },
      {
        name: 'Directory.list() sync',
        fn: () => {
          dir(`${testDir}/list_test`).listSync();
        },
      },
      {
        name: 'Directory.delete() async',
        setup: async () => {
          await dir(`${testDir}/del_async`).create();
        },
        fn: async () => {
          await dir(`${testDir}/del_async`).delete();
        },
      },
      {
        name: 'Directory.delete() sync',
        setup: async () => {
          await dir(`${testDir}/del_sync`).create();
        },
        fn: () => {
          dir(`${testDir}/del_sync`).deleteSync();
        },
      },
      {
        name: 'Directory.move() async',
        setup: async () => {
          await dir(`${testDir}/move_src`).create();
        },
        fn: async () => {
          await dir(`${testDir}/move_src`).move(`${testDir}/move_dst`);
        },
      },
      {
        name: 'Directory.move() sync',
        setup: async () => {
          await dir(`${testDir}/move_src2`).create();
        },
        fn: () => {
          dir(`${testDir}/move_src2`).moveSync(`${testDir}/move_dst2`);
        },
      },

      // ==================== File Write Tests ====================
      {
        name: 'File.create() async',
        fn: async () => {
          await file(`${testDir}/create_async.txt`).create();
        },
      },
      {
        name: 'File.create() sync',
        fn: () => {
          file(`${testDir}/create_sync.txt`).createSync();
        },
      },
      {
        name: 'File.writeString() async',
        fn: async () => {
          await file(`${testDir}/write_async.txt`).writeString('Hello');
        },
      },
      {
        name: 'File.writeString() sync',
        fn: () => {
          file(`${testDir}/write_sync.txt`).writeStringSync('Hello');
        },
      },
      {
        name: 'File.appendString() async',
        setup: async () => {
          await file(`${testDir}/append_async.txt`).writeString('A');
        },
        fn: async () => {
          await file(`${testDir}/append_async.txt`).appendString('B');
        },
      },
      {
        name: 'File.appendString() sync',
        setup: async () => {
          await file(`${testDir}/append_sync.txt`).writeString('A');
        },
        fn: () => {
          file(`${testDir}/append_sync.txt`).appendStringSync('B');
        },
      },
      {
        name: 'File.writeBytes() async',
        fn: async () => {
          const data = new Uint8Array([1, 2, 3, 4, 5]).buffer;
          await file(`${testDir}/bytes_async.bin`).writeBytes(data);
        },
      },
      {
        name: 'File.writeBytes() sync',
        fn: () => {
          const data = new Uint8Array([1, 2, 3, 4, 5]).buffer;
          file(`${testDir}/bytes_sync.bin`).writeBytesSync(data);
        },
      },

      // ==================== File Read Tests ====================
      {
        name: 'File.readString() async',
        setup: async () => {
          await file(`${testDir}/read_str.txt`).writeString('Content');
        },
        fn: async () => {
          testFileContent = await file(`${testDir}/read_str.txt`).readString();
        },
        verify: () => {
          if (testFileContent !== 'Content') throw new Error('Mismatch');
        },
      },
      {
        name: 'File.readString() sync',
        fn: () => {
          testFileContent = file(`${testDir}/read_str.txt`).readStringSync();
        },
        verify: () => {
          if (testFileContent !== 'Content') throw new Error('Mismatch');
        },
      },
      {
        name: 'File.readBytes() async',
        setup: async () => {
          await file(`${testDir}/read_bytes.bin`).writeBytes(
            new Uint8Array([1, 2, 3]).buffer
          );
        },
        fn: async () => {
          testFileBytes = await file(`${testDir}/read_bytes.bin`).readBytes();
        },
        verify: () => {
          if (new Uint8Array(testFileBytes)[0] !== 1)
            throw new Error('Mismatch');
        },
      },
      {
        name: 'File.readBytes() sync',
        fn: () => {
          testFileBytes = file(`${testDir}/read_bytes.bin`).readBytesSync();
        },
        verify: () => {
          if (new Uint8Array(testFileBytes)[0] !== 1)
            throw new Error('Mismatch');
        },
      },
      {
        name: 'File.readLines() async',
        setup: async () => {
          await file(`${testDir}/lines.txt`).writeString('L1\nL2\nL3');
        },
        fn: async () => {
          await file(`${testDir}/lines.txt`).readLines();
        },
      },
      {
        name: 'File.readLines() sync',
        fn: () => {
          file(`${testDir}/lines.txt`).readLinesSync();
        },
      },

      // ==================== File Property Tests ====================
      {
        name: 'File.exists() async',
        setup: async () => {
          await file(`${testDir}/exists.txt`).writeString('x');
        },
        fn: async () => {
          await file(`${testDir}/exists.txt`).exists();
        },
      },
      {
        name: 'File.exists() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).existsSync();
        },
      },
      {
        name: 'File.isFile() async',
        fn: async () => {
          await file(`${testDir}/exists.txt`).isFile();
        },
      },
      {
        name: 'File.isFile() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).isFileSync();
        },
      },
      {
        name: 'File.size() async',
        fn: async () => {
          await file(`${testDir}/exists.txt`).size();
        },
      },
      {
        name: 'File.size() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).sizeSync();
        },
      },
      {
        name: 'File.metadata() async',
        fn: async () => {
          await file(`${testDir}/exists.txt`).metadata();
        },
      },
      {
        name: 'File.metadata() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).metadataSync();
        },
      },
      {
        name: 'File.modifiedTime() async',
        fn: async () => {
          await file(`${testDir}/exists.txt`).modifiedTime();
        },
      },
      {
        name: 'File.modifiedTime() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).modifiedTimeSync();
        },
      },
      {
        name: 'File.absolutePath() async',
        fn: async () => {
          await file(`${testDir}/exists.txt`).absolutePath();
        },
      },
      {
        name: 'File.absolutePath() sync',
        fn: () => {
          file(`${testDir}/exists.txt`).absolutePathSync();
        },
      },

      // ==================== File Operation Tests ====================
      {
        name: 'File.copy() async',
        setup: async () => {
          await file(`${testDir}/copy_src.txt`).writeString('copy');
        },
        fn: async () => {
          await file(`${testDir}/copy_src.txt`).copy(`${testDir}/copy_dst.txt`);
        },
      },
      {
        name: 'File.copy() sync',
        setup: async () => {
          await file(`${testDir}/copy_src2.txt`).writeString('copy');
        },
        fn: () => {
          file(`${testDir}/copy_src2.txt`).copySync(`${testDir}/copy_dst2.txt`);
        },
      },
      {
        name: 'File.move() async',
        setup: async () => {
          await file(`${testDir}/move_src.txt`).writeString('move');
        },
        fn: async () => {
          await file(`${testDir}/move_src.txt`).move(`${testDir}/move_dst.txt`);
        },
      },
      {
        name: 'File.move() sync',
        setup: async () => {
          await file(`${testDir}/move_src2.txt`).writeString('move');
        },
        fn: () => {
          file(`${testDir}/move_src2.txt`).moveSync(`${testDir}/move_dst2.txt`);
        },
      },
      {
        name: 'File.delete() async',
        setup: async () => {
          await file(`${testDir}/del_async.txt`).writeString('del');
        },
        fn: async () => {
          await file(`${testDir}/del_async.txt`).delete();
        },
      },
      {
        name: 'File.delete() sync',
        setup: async () => {
          await file(`${testDir}/del_sync.txt`).writeString('del');
        },
        fn: () => {
          file(`${testDir}/del_sync.txt`).deleteSync();
        },
      },

      // ==================== Hash Tests ====================
      {
        name: 'File.calcHash(md5)',
        setup: async () => {
          await file(`${testDir}/hash.txt`).writeString('hash content');
        },
        fn: async () => {
          await file(`${testDir}/hash.txt`).calcHash(HashAlgorithm.MD5);
        },
      },
      {
        name: 'File.calcHash(sha256)',
        fn: async () => {
          await file(`${testDir}/hash.txt`).calcHash(HashAlgorithm.SHA256);
        },
      },

      // ==================== FS Utility Tests ====================
      {
        name: 'FS.joinPaths()',
        fn: () => {
          FS.joinPaths('/a', 'b', 'c.txt');
        },
      },
      {
        name: 'FS.getAvailableSpace() async',
        fn: async () => {
          await FS.getAvailableSpace(tempDir);
        },
      },
      {
        name: 'FS.getAvailableSpace() sync',
        fn: () => {
          FS.getAvailableSpaceSync(tempDir);
        },
      },
      {
        name: 'FS.getTotalSpace() async',
        fn: async () => {
          await FS.getTotalSpace(tempDir);
        },
      },
      {
        name: 'FS.getTotalSpace() sync',
        fn: () => {
          FS.getTotalSpaceSync(tempDir);
        },
      },
      {
        name: 'FS.decodeString()',
        fn: () => {
          const buf = new Uint8Array([72, 101, 108, 108, 111]).buffer;
          const s = FS.decodeString(buf);
          if (s !== 'Hello') throw new Error('Decode failed');
        },
      },
      {
        name: 'FS.encodeString()',
        fn: () => {
          const buf = FS.encodeString('Hello');
          if (new Uint8Array(buf)[0] !== 72) throw new Error('Encode failed');
        },
      },

      // ==================== HTTP Tests ====================
      {
        name: 'request.get()',
        fn: async () => {
          const res = await request.get('https://httpbin.org/get');
          if (!res.ok) throw new Error(`HTTP ${res.status}`);
        },
      },
      {
        name: 'request.post()',
        fn: async () => {
          const res = await request.post('https://httpbin.org/post', {
            json: { test: 123 },
          });
          if (!res.ok) throw new Error(`HTTP ${res.status}`);
        },
      },
    ];

    setResults(tests.map((t) => ({ name: t.name, status: 'pending' })));
    setRunning(true);

    for (const test of tests) {
      try {
        if (test.setup) await test.setup();
        const start = performance.now();
        await test.fn();
        const duration = performance.now() - start;
        if (test.verify) await test.verify();
        updateResult(test.name, 'pass', undefined, duration);
      } catch (e: any) {
        updateResult(test.name, 'fail', e.message || String(e));
      }
    }

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
  container: { flex: 1, backgroundColor: '#f5f5f5' },
  header: {
    padding: 20,
    paddingTop: 50,
    backgroundColor: '#fff',
    alignItems: 'center',
    borderBottomWidth: 1,
    borderBottomColor: '#e0e0e0',
  },
  title: {
    fontSize: 24,
    fontWeight: 'bold',
    color: '#333',
    marginBottom: 10,
  },
  summary: { fontSize: 18, color: '#666', marginBottom: 15 },
  button: {
    backgroundColor: '#007AFF',
    paddingHorizontal: 30,
    paddingVertical: 12,
    borderRadius: 8,
  },
  buttonDisabled: { opacity: 0.5 },
  buttonText: { color: '#fff', fontSize: 16, fontWeight: '600' },
  list: { flex: 1, padding: 10 },
  item: {
    flexDirection: 'row',
    backgroundColor: '#fff',
    padding: 12,
    borderRadius: 8,
    marginBottom: 8,
    shadowColor: '#000',
    shadowOffset: { width: 0, height: 1 },
    shadowOpacity: 0.1,
    shadowRadius: 2,
    elevation: 2,
  },
  icon: { fontSize: 18, marginRight: 12 },
  itemContent: { flex: 1 },
  itemHeader: {
    flexDirection: 'row',
    justifyContent: 'space-between',
    alignItems: 'center',
  },
  itemName: { color: '#333', fontSize: 14, flex: 1 },
  duration: { color: '#4CAF50', fontSize: 12, marginLeft: 8 },
  error: { color: '#F44336', fontSize: 12, marginTop: 4 },
});
