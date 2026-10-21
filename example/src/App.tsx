import { useState } from 'react';
import {
  SafeAreaView,
  StatusBar,
  StyleSheet,
  Text,
  TouchableOpacity,
  View,
} from 'react-native';
import SmokeApp from './SmokeApp';
import QualityPanel from './quality/QualityPanel';
import BenchmarkApp from './quality/BenchmarkApp';
import AutoApp from './quality/AutoApp';
import type { DeviceLaunchOptions } from './quality/deviceOptions';

export default function App(options: DeviceLaunchOptions) {
  const [screen, setScreen] = useState<'quality' | 'legacy'>();
  if (options.ioExampleAction === 'benchmark')
    return <BenchmarkApp {...options} />;
  if (options.ioExampleAction === 'quality') return <AutoApp {...options} />;
  // Choose once per launch, so unmounting cannot leave an active suite behind.
  return (
    <SafeAreaView style={styles.container}>
      <StatusBar barStyle="dark-content" />
      {screen === 'quality' ? (
        <QualityPanel />
      ) : screen === 'legacy' ? (
        <SmokeApp />
      ) : (
        <View style={styles.home}>
          <Text style={styles.eyebrow}>REACT NATIVE IO</Text>
          <Text style={styles.title}>原生 I/O 测试实验室</Text>
          <Text style={styles.description}>
            在当前设备上验证文件、网络、边界、并发与性能。选择入口后手动开始测试。
          </Text>
          <TouchableOpacity
            accessibilityRole="button"
            onPress={() => setScreen('quality')}
            style={styles.card}
          >
            <Text style={styles.cardTitle}>质量与性能测试 →</Text>
            <Text style={styles.cardDescription}>
              Quick / Full / Soak · 逐项结果 · 取消 · 导出报告
            </Text>
          </TouchableOpacity>
          <TouchableOpacity
            accessibilityRole="button"
            onPress={() => setScreen('legacy')}
            style={[styles.card, styles.secondaryCard]}
          >
            <Text style={styles.cardTitle}>原始 API 示例 →</Text>
            <Text style={styles.cardDescription}>
              文件、目录、句柄与请求的基础冒烟测试
            </Text>
          </TouchableOpacity>
          <Text style={styles.note}>重新打开 App 可重新选择测试入口。</Text>
        </View>
      )}
    </SafeAreaView>
  );
}

const styles = StyleSheet.create({
  container: { flex: 1, backgroundColor: '#f3f6fb' },
  home: { flex: 1, justifyContent: 'center', padding: 24 },
  eyebrow: {
    color: '#375ed3',
    fontSize: 12,
    fontWeight: '700',
    letterSpacing: 2,
  },
  title: { color: '#17243d', fontSize: 28, fontWeight: '700', marginTop: 12 },
  description: {
    color: '#53627a',
    fontSize: 15,
    lineHeight: 24,
    marginVertical: 20,
  },
  card: {
    padding: 20,
    borderRadius: 16,
    backgroundColor: '#dfe9ff',
    marginBottom: 14,
    borderWidth: 1,
    borderColor: '#bacffc',
  },
  secondaryCard: { backgroundColor: '#fff', borderColor: '#dce3ee' },
  cardTitle: { color: '#17243d', fontSize: 18, fontWeight: '600' },
  cardDescription: {
    color: '#53627a',
    fontSize: 13,
    lineHeight: 21,
    marginTop: 8,
  },
  note: { color: '#65738a', fontSize: 12, marginTop: 6 },
});
