// Build the source HAR and resolve it through the official RNOH autolinker.
const fs = require('node:fs');
const path = require('node:path');
const { execFileSync } = require('node:child_process');

const example = path.resolve(__dirname, '..');
const root = path.resolve(example, '..');
const harmony = path.join(example, 'harmony');
const deveco =
  process.env.DEVECO_HOME || '/Applications/DevEco-Studio.app/Contents';
const ohpm = process.env.OHPM_BIN || path.join(deveco, 'tools/ohpm/bin/ohpm');
const profile = path.join(harmony, 'build-profile.json5');
if (!fs.existsSync(profile)) {
  fs.copyFileSync(path.join(harmony, 'build-profile.template.json5'), profile);
}

// A clean checkout also needs the RNOH dependency used by the HAR project.
execFileSync(ohpm, ['install', '--all'], {
  cwd: path.join(root, 'harmony'),
  stdio: 'inherit',
});
execFileSync(
  process.execPath,
  [path.join(root, 'scripts/build-harmony-har.cjs')],
  {
    cwd: root,
    stdio: 'inherit',
  }
);
execFileSync(
  process.execPath,
  [
    require.resolve('@react-native-community/cli/build/bin.js'),
    'link-harmony',
    '--harmony-project-path',
    harmony,
    '--include-npm-packages',
    'react-native-io',
  ],
  { cwd: example, stdio: 'inherit' }
);

execFileSync(ohpm, ['install', '--all'], { cwd: harmony, stdio: 'inherit' });
