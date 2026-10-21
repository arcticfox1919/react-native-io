#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${REACT_NATIVE_ROOT:=$root/node_modules/react-native}"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-bench.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
python3 - "$build_dir/input" <<'PYTHON'
import sys
with open(sys.argv[1], 'wb') as f:
    chunk=bytes(range(256))*256
    for _ in range(512): f.write(chunk)
PYTHON
git -C "$root" show HEAD:cpp/src/IOFileSystem.hpp > "$build_dir/baseline.hpp"
for mode in baseline current; do
  header="$build_dir/baseline.hpp"
  if [[ "$mode" == current ]]; then header="$root/cpp/src/IOFileSystem.hpp"; fi
  clang++ -std=c++20 -O3 -DNDEBUG -include cstring -I "$root/cpp/src" -I "$root/cpp/hash" -I "$REACT_NATIVE_ROOT/ReactCommon/jsi" "-DIO_FILESYSTEM_HEADER=\"$header\"" "$root/example/tests/native/HashBenchmark.cpp" "$root"/cpp/hash/*.cpp -o "$build_dir/$mode"
  echo "$mode (32 MiB file, 3 SHA-256 runs)"
  /usr/bin/time -l "$build_dir/$mode" "$build_dir/input"
done
