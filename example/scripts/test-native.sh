#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
: "${REACT_NATIVE_ROOT:=$root/node_modules/react-native}"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-tests.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
common="$REACT_NATIVE_ROOT/ReactCommon"
flags=(-DRCT_IO_MAX_WORKER_THREADS=8 -DRCT_IO_MAX_ASYNC_PAYLOAD_BYTES=65536 -std=c++20 -g -pthread -include cstring -include cmath -I "$root/cpp/src" -I "$root/cpp/hash" -I "$common" -I "$common/jsi" -I "$common/callinvoker")
if [[ "${SANITIZE:-address,undefined}" != none ]]; then flags+=(-fsanitize="${SANITIZE:-address,undefined}" -fno-omit-frame-pointer); fi
clang++ "${flags[@]}" "$root/example/tests/native/NativeTests.cpp" "$common/jsi/jsi/jsi.cpp" "$common/jsc/JSCRuntime.cpp" "$root"/cpp/hash/*.cpp -framework JavaScriptCore -o "$build_dir/native-tests"
"$build_dir/native-tests" "$build_dir/data"
