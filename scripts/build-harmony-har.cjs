// Maintainer-only source HAR build. Test tools belong under example/scripts.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const root = path.resolve(__dirname, '..');
const harmony = path.join(root, 'harmony');
const moduleRoot = path.join(harmony, 'io');
const core = path.join(moduleRoot, 'src/main/cpp/io-core');
// Only this generated source mirror is removed. cpp/ remains the source of truth.
fs.rmSync(core, { recursive: true, force: true });
fs.mkdirSync(core, { recursive: true });
for (const directory of ['src', 'hash', 'network']) {
  fs.cpSync(path.join(root, 'cpp', directory), path.join(core, directory), {
    recursive: true,
    filter: (source) => !['build', '.DS_Store'].includes(path.basename(source)),
  });
}
const manifestPath = path.join(moduleRoot, 'oh-package.json5');
const original = fs.readFileSync(manifestPath);
const manifest = JSON.parse(original.toString('utf8'));
manifest.version = require('../package.json').version;
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
  'module=io@default',
  '-p',
  'product=default',
  '-p',
  'buildMode=release',
  'assembleHar',
  '--no-daemon',
];
try {
  fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
  execFileSync(
    hvigor.endsWith('.js') ? node : hvigor,
    hvigor.endsWith('.js') ? [hvigor, ...args] : args,
    {
      cwd: harmony,
      stdio: 'inherit',
      env: {
        ...process.env,
        DEVECO_SDK_HOME:
          process.env.DEVECO_SDK_HOME || path.join(deveco, 'sdk'),
      },
    }
  );
  fs.copyFileSync(
    path.join(moduleRoot, 'build/default/outputs/default/io.har'),
    path.join(harmony, 'io.har')
  );
  console.log(
    'Built harmony/io.har (C++ sources; the consumer supplies RNOH and Hermes).'
  );
} finally {
  fs.writeFileSync(manifestPath, original);
}
