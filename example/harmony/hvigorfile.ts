import { appTasks } from '@ohos/hvigor-ohos-plugin';
import { createRNOHProjectPlugin } from '@rnoh/hvigor-plugin';

export default {
  system: appTasks,
  // The example build script supplies optimized Hermes V1 bytecode. Avoid
  // generating an additional unused JavaScript bundle during HAP packaging.
  plugins: [createRNOHProjectPlugin({ bundler: { enabled: false } })],
};
