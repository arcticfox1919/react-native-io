#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/react-native-io-http.XXXXXX")"
trap 'rm -rf "$build_dir"' EXIT
javac -d "$build_dir" "$root/android/src/main/java/xyz/bczl/io/IOHttpClient.java" "$root/example/tests/AndroidHttpTest.java"
java -cp "$build_dir" AndroidHttpTest
