#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
deveco="${DEVECO_HOME:-/Applications/DevEco-Studio.app/Contents}"
sdk="${DEVECO_SDK_HOME:-$deveco/sdk}/default"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-harmony-http.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
# Use the real public RCP declarations with the host standard library. Only
# platform availability annotations are removed for the desktop test double.
clang++ -std=c++20 -g -pthread -fsanitize="${SANITIZE:-address,undefined}" \
  -fno-omit-frame-pointer -DRCT_IO_MAX_BUFFER_BYTES=65536 \
  '-D__availability__(...)=' -Wno-macro-redefined \
  -I "$root/cpp/src" -I "$root/cpp/network" \
  -idirafter "$sdk/hms/native/sysroot/usr/include" \
  -idirafter "$sdk/openharmony/native/sysroot/usr/include" \
  "$root/example/tests/native/HarmonyHttpTests.cpp" \
  "$root/harmony/io/src/main/cpp/IOHttpClientHarmony.cpp" \
  -o "$build_dir/harmony-http-tests"
"$build_dir/harmony-http-tests" "$build_dir/data"
