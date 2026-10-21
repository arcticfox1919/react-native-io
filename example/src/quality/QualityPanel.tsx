import { useRef, useState } from 'react';
import {
  Platform,
  ScrollView,
  Share,
  StyleSheet,
  Text,
  TextInput,
  TouchableOpacity,
  View,
} from 'react-native';
import { runQuality, saveReport, type CaseResult, type Report } from './runner';
import type { Profile } from './engine';

export default function QualityPanel() {
  const [profile, setProfile] = useState<Profile>('quick');
  const [url, setURL] = useState('');
  const [running, setRunning] = useState(false);
  const [results, setResults] = useState<CaseResult[]>([]);
  const [report, setReport] = useState<Report>();
  const [message, setMessage] = useState('');
  const cancelled = useRef(false);
  const active = useRef(false);
  async function start() {
    if (active.current) return;
    active.current = true;
    cancelled.current = false;
    setRunning(true);
    setResults([]);
    setReport(undefined);
    setMessage('');
    try {
      const value = await runQuality({
        profile,
        networkURL: url.trim() || undefined,
        cancelled: () => cancelled.current,
        onResult: (result) => setResults((previous) => [...previous, result]),
        environment: {
          platform: Platform.OS,
          systemVersion: Platform.Version,
          reactNative: Platform.constants.reactNativeVersion,
          debug: __DEV__,
          engine: 'HermesInternal' in globalThis ? 'Hermes' : 'other/unknown',
          networkFixture: url.trim() || null,
          note: 'Memory/RSS requires platform profiler; timing alone does not establish absence of leaks',
        },
      });
      setReport(value);
      try {
        setMessage(`Report saved: ${await saveReport(value)}`);
      } catch (error) {
        setMessage(`Report retained in memory; save failed: ${String(error)}`);
      }
    } catch (error) {
      setMessage(String(error));
    } finally {
      active.current = false;
      setRunning(false);
    }
  }
  return (
    <View style={styles.container}>
      <Text style={styles.title}>I/O quality tests</Text>
      <Text>
        Quick: 40 operations · Full: 500 · Soak: 5000 per stress scenario
      </Text>
      <View style={styles.row}>
        {(['quick', 'full', 'soak'] as const).map((value) => (
          <TouchableOpacity
            key={value}
            disabled={running}
            onPress={() => setProfile(value)}
            style={[styles.button, profile === value && styles.selected]}
          >
            <Text>{value}</Text>
          </TouchableOpacity>
        ))}
      </View>
      <TextInput
        value={url}
        onChangeText={setURL}
        editable={!running}
        autoCapitalize="none"
        autoCorrect={false}
        placeholder="Optional local HTTP fixture URL"
        style={styles.input}
      />
      <View style={styles.row}>
        <TouchableOpacity
          disabled={running}
          onPress={() => {
            void start();
          }}
          style={styles.button}
        >
          <Text>{running ? 'Running…' : 'Run selected profile'}</Text>
        </TouchableOpacity>
        <TouchableOpacity
          disabled={!running}
          onPress={() => {
            cancelled.current = true;
            setMessage('Stopping after in-flight operations settle…');
          }}
          style={styles.button}
        >
          <Text>Cancel</Text>
        </TouchableOpacity>
        <TouchableOpacity
          disabled={!report || running}
          onPress={() => {
            if (report)
              void Share.share({
                message: JSON.stringify(report, null, 2),
              }).catch((error) => setMessage(String(error)));
          }}
          style={styles.button}
        >
          <Text>Share JSON</Text>
        </TouchableOpacity>
      </View>
      <Text>
        {results.filter((r) => r.status === 'pass').length} passed ·{' '}
        {results.filter((r) => r.status === 'fail').length} failed ·{' '}
        {results.filter((r) => r.status === 'skipped').length} skipped
      </Text>
      <Text selectable>{message}</Text>
      <ScrollView style={styles.list}>
        {results.map((r, index) => (
          <View key={`${index}-${r.name}`} style={styles.result}>
            <Text style={r.status === 'fail' ? styles.error : undefined}>
              {r.status.toUpperCase()} [{r.group}] {r.name} (
              {r.durationMs.toFixed(1)} ms)
            </Text>
            {r.error && <Text selectable>{r.error}</Text>}
            {r.measurements.map((m) => (
              <Text key={m.label}>
                {m.label}: median {m.summary.medianMs.toFixed(2)} ms · p95{' '}
                {m.summary.p95Ms.toFixed(2)} ms ·{' '}
                {m.summary.throughputMiBps?.toFixed(1) ?? '—'} MiB/s · n=
                {m.summary.samples}
              </Text>
            ))}
          </View>
        ))}
      </ScrollView>
    </View>
  );
}
const styles = StyleSheet.create({
  container: { flex: 1, padding: 12, backgroundColor: '#fff' },
  title: { fontSize: 22, fontWeight: '600' },
  row: { flexDirection: 'row', flexWrap: 'wrap' },
  button: {
    padding: 10,
    margin: 3,
    backgroundColor: '#e4e8ef',
    borderRadius: 5,
  },
  selected: { backgroundColor: '#a8d1ff' },
  input: { borderWidth: 1, borderColor: '#999', padding: 8, marginVertical: 8 },
  list: { flex: 1, marginTop: 8 },
  result: { paddingVertical: 10, borderBottomWidth: 1, borderColor: '#ddd' },
  error: { color: '#b00020' },
});
