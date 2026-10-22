#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${REACT_NATIVE_ROOT:=$root/node_modules/react-native}"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-reads.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
python3 - "$build_dir/input" <<'PYTHON'
import sys
with open(sys.argv[1], 'wb') as f:
    for _ in range(32):
        f.write(b'a' * (1024 * 1024 - 1) + b'z')
PYTHON
for header in IOFileSystem IOFileHandle; do
  git -C "$root" show "HEAD:cpp/src/$header.hpp" > "$build_dir/$header.hpp"
done
for mode in baseline current; do
  headers="$build_dir"
  if [[ "$mode" == current ]]; then headers="$root/cpp/src"; fi
  clang++ -std=c++20 -O3 -DNDEBUG -include cstring \
    -I "$root/cpp/src" -I "$root/cpp/hash" -I "$REACT_NATIVE_ROOT/ReactCommon/jsi" \
    "-DIO_FILESYSTEM_HEADER=\"$headers/IOFileSystem.hpp\"" \
    "-DIO_FILE_HANDLE_HEADER=\"$headers/IOFileHandle.hpp\"" \
    "$root/example/tests/native/ReadBenchmark.cpp" -o "$build_dir/$mode"
done
echo 'Native C++ -O3, 32 MiB file; baseline=HEAD, current=working tree. Not mobile app results.'
for mode in baseline current current baseline; do
  echo "$mode"
  "$build_dir/$mode" "$build_dir/input"
done
