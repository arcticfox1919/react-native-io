#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-apple.XXXXXX")"
server_pid=""
cleanup() { if [[ -n "$server_pid" ]]; then kill "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true; fi; rm -rf "$build_dir"; }
trap cleanup EXIT
clang++ -std=c++20 -g -fobjc-arc -fsanitize=address,undefined -I "$root/cpp/network" "$root/example/tests/native/AppleHttpTests.mm" "$root/ios/IOHttpClientIOS.mm" -framework Foundation -o "$build_dir/tests"
python3 "$root/example/tests/http_fixture.py" "$build_dir/port" &
server_pid=$!
for ((i=0;i<100;++i)); do [[ -s "$build_dir/port" ]] && break; sleep 0.05; done
"$build_dir/tests" "http://127.0.0.1:$(cat "$build_dir/port")" "$build_dir"
