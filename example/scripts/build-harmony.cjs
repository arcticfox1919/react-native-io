// Build an embedded-bundle HAP. Local signing belongs in the ignored profile.
const path = require('node:path');
const fs = require('node:fs');
const { execFileSync } = require('node:child_process');

const mode = process.argv[2] || 'release';
if (!['debug', 'release'].includes(mode) || process.argv.length > 3) {
  throw new Error('Usage: yarn build:harmony [debug|release]');
}
const example = path.resolve(__dirname, '..');
const harmony = path.join(example, 'harmony');
const bundle = path.join(
  harmony,
  'entry/src/main/resources/rawfile/hermes_bundle.hbc'
);
execFileSync(process.execPath, [path.join(__dirname, 'prepare-harmony.cjs')], {
  cwd: example,
  stdio: 'inherit',
});
// Do not let a CLI diagnostic that exits successfully reuse an older bundle.
fs.rmSync(bundle, { force: true });
fs.rmSync(path.join(path.dirname(bundle), 'bundle.harmony.js'), {
  force: true,
});
execFileSync(
  process.execPath,
  [
    require.resolve('@react-native-community/cli/build/bin.js'),
    'bundle-harmony',
    '--entry-file',
    'index.js',
    '--dev',
    String(mode === 'debug'),
    '--js-engine',
    'hermes',
    '--hermesc-dir',
    path.join(
      path.dirname(require.resolve('hermes-compiler/package.json')),
      'hermesc'
    ),
    ...(mode === 'release' ? ['--hermesc-options', 'O'] : []),
  ],
  { cwd: example, stdio: 'inherit' }
);
if (!fs.existsSync(bundle))
  throw new Error('Harmony Hermes bundle was not generated');

const deveco =
  process.env.DEVECO_HOME || '/Applications/DevEco-Studio.app/Contents';
const hvigor =
  process.env.HVIGOR_BIN || path.join(deveco, 'tools/hvigor/bin/hvigorw.js');
const node =
  process.env.HVIGOR_NODE || path.join(deveco, 'tools/node/bin/node');
const args = [
  '--mode',
  'module',
  '-p',
  'module=entry@default',
  '-p',
  'product=default',
  '-p',
  `buildMode=${mode}`,
  'assembleHap',
  '--no-daemon',
];
execFileSync(
  hvigor.endsWith('.js') ? node : hvigor,
  hvigor.endsWith('.js') ? [hvigor, ...args] : args,
  {
    cwd: harmony,
    stdio: 'inherit',
    env: {
      ...process.env,
      DEVECO_SDK_HOME: process.env.DEVECO_SDK_HOME || path.join(deveco, 'sdk'),
    },
  }
);
