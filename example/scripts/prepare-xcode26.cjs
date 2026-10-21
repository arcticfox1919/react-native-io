'use strict';
// Optional test-workspace workaround for fmt 11.0.2 + current Apple Clang.
// fmt 11.0.2 unconditionally redefines FMT_USE_CONSTEVAL, ignoring a build flag.
// Make its feature detection honor the explicit flag; never touches the library sources.
const fs = require('node:fs');
const path = require('node:path');
const header = path.resolve(__dirname, '../ios/Pods/fmt/include/fmt/base.h');
const before = '#if !defined(__cpp_lib_is_constant_evaluated)';
const after =
  '#if defined(FMT_USE_CONSTEVAL)\n// Honor the explicit example build override.\n#elif !defined(__cpp_lib_is_constant_evaluated)';
const source = fs.readFileSync(header, 'utf8');
if (source.includes(after)) console.log('fmt override guard already installed');
else {
  if (!source.includes(before))
    throw Error('Unsupported fmt source; inspect before applying a workaround');
  fs.chmodSync(header, fs.statSync(header).mode | 0o200);
  fs.writeFileSync(header, source.replace(before, after));
  console.log(
    'Patched example Pods/fmt feature guard. Build with -DFMT_USE_CONSTEVAL=0; pod reinstall may reset it.'
  );
}
