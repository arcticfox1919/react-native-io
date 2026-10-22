# Example Test and Performance Guide

The tests under example/src/quality call the library's real native APIs. They
cover functional behavior, boundary values, critical resource limits,
concurrent stress, performance distributions, and a controlled local HTTP
fixture. The interactive UI, Harness tests, and standalone device entry point
share the same quality runner. The original API demo is available through
src/SmokeApp.tsx.

iOS device tests use a connected physical device by default. Results from
desktop sanitizers, macOS Foundation, and iOS simulators must be recorded
separately from physical-device results.

## 1. Test coverage

| Group       | Coverage                                                                                                                                                         | Failure criteria                                                              |
| ----------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------- |
| functional  | File and directory CRUD, sync/async consistency, UTF-8, emoji, NUL content, encodings, metadata, timestamps, flush/truncate, AppendRead, and storage information | Content, position, metadata, or expected success/failure does not match       |
| functional  | Known MD5, SHA-1, SHA-256, SHA3-256, and CRC32 digests for abc                                                                                                   | Digest differs from the fixed expected value                                  |
| boundary    | Binary lengths 0/1/2/3/4/5/7/8, 16/256/4096/65536, and 1 MiB neighborhoods                                                                                       | Returned length or any byte differs                                           |
| boundary    | Empty and NUL paths, NaN, infinity, fractional values, unsafe integers, invalid enums, and thread counts 0/33                                                    | No synchronous throw or Promise rejection is observed                         |
| boundary    | CRLF, standalone CR, missing final newline, EOF, empty reads, 2 GiB seeks, and read/write direction changes                                                      | Characters are lost, positions are narrowed, or results diverge               |
| boundary    | Child objects and handles after close, and work already in flight when a context closes                                                                          | Work succeeds after close, submitted work is lost, or completion is unsettled |
| critical    | 255, 256, and 257 unconsumed completions followed by retry                                                                                                       | Acceptance/rejection count is wrong or released quota is not reusable         |
| critical    | limit - 1, limit, and limit + 1 payloads in a small-limit build                                                                                                  | Boundary length is wrong; default builds explicitly skip this case            |
| stress      | Fixed-seed concurrent write, read, hash, and delete operations                                                                                                   | Byte corruption, digest mismatch, or undeleted entry                          |
| stress      | Concurrent line writes and close on one handle                                                                                                                   | Wrong count, duplicate/incomplete records, or a successful write after close  |
| stress      | Atomic replacement with multiple readers                                                                                                                         | A reader observes anything other than a complete old or new version           |
| performance | Regular write, full read, SHA-256, 4 KiB atomic write, and durable atomic write                                                                                  | Correctness is checked first; timing and throughput are reported              |
| network     | Binary echo, falsy JSON, redirects, timeout, Range variants, header/multipart injection, failed downloads preserving the old file, and upload fields             | A deterministic local-fixture assertion fails                                 |

The default device suite does not deliberately fill the disk or force short
writes. Disk-full and short-write behavior is injected with native
RLIMIT_FSIZE tests; thread and payload quotas use a separate small-limit build.
Symlinks, permissions, atomic-write cleanup, callbacks after runtime
destruction, and sanitizers are covered by
example/tests/native/NativeTests.cpp. Device and native results must be
reviewed together; a green UI alone is not sufficient evidence.

## 2. Profiles and safe execution

| Profile | Major stress iterations | Concurrency | Performance samples | Fixture size |   Deadline |
| ------- | ----------------------: | ----------: | ------------------: | -----------: | ---------: |
| quick   |                      40 |           4 |   5 plus 2 warm-ups |        1 MiB |  2 minutes |
| full    |                     500 |          16 |  20 plus 2 warm-ups |       16 MiB | 10 minutes |
| soak    |                    5000 |          32 |  30 plus 2 warm-ups |       32 MiB | 30 minutes |

A deadline is an upper bound; a run may finish earlier. Each run uses a unique
cache workspace and removes it after every case. Cleanup failures are recorded.

Cancellation is cooperative: the runner stops dispatching work, waits for
started operations to settle, and then cleans up. Blocking native I/O is not
forcefully interrupted. Cancellation and timeout therefore do not immediately
abort a native call and must not be interpreted as transaction rollback.

## 3. Installation and type checking

From the repository root:

```sh
yarn install --immutable
yarn typecheck
yarn test:example-engine
```

The development baseline is React Native 0.84.1, React 19.2.3, and Node.js
22.11 or later. HarmonyOS uses RNOH and Harmony CLI 0.84.3 with the same
JavaScript application and quality runner.

test:example-engine checks statistics, seed reproducibility, maximum
concurrency, error draining, and cancellation deadlines. It does not mock
device cases and call them native successes. yarn typecheck includes example
and Harness TypeScript sources.

Install iOS pods with the repository's Ruby environment:

```sh
cd example/ios
bundle exec pod install
```

Do not upgrade locked Gem dependencies merely to run a test.

## 4. Interactive iOS physical-device entry

```sh
# From the example directory
xcrun xcdevice list
yarn ios --udid <UDID>
```

On the home screen, select **Quality and Performance Tests**, choose quick,
full, or soak, and start the run. A local fixture URL is optional. If no
fixture is configured, network cases are skipped; that is not network
validation.

The quality report is saved under the app's Documents directory and can be
exported with **Share JSON**. The original API smoke examples are available
from the other home-screen entry. Use one entry per installation so an earlier
automatic run cannot continue in the background.

Interactive and automatic entries:

- index.js → App.tsx: interactive quality tests and API examples.
- index.quality.js → AutoApp.tsx: automatic profile execution and JSON report.

Both entries use the same application identifier. A white screen containing JSON
means the automatic report entry is installed; rebuild with index.js for
interactive use.

Build an interactive bundle without Metro:

```sh
# From example/ios; use the connected device UDID.
ENTRY_FILE=index.js FORCE_BUNDLING=1 RCT_NO_LAUNCH_PACKAGER=1 \
  xcodebuild -workspace IoExample.xcworkspace -scheme IoExample \
  -configuration Debug -destination "id=$DEVICE_UDID" \
  ENABLE_DEBUG_DYLIB=NO 'SWIFT_ACTIVE_COMPILATION_CONDITIONS=$(inherited) IO_EXAMPLE_BUNDLED' \
  -derivedDataPath /tmp/react-native-io-interactive \
  -allowProvisioningUpdates build
```

IO_EXAMPLE_BUNDLED selects the embedded JavaScript bundle. The entry file
still determines whether the app is interactive or automatic.

## 5. Standalone iOS automation

index.quality.js uses AUTO_PROFILE='full' by default. It shares the quality
runner with the UI, logs [IO_QUALITY_FINISHED], saves a JSON report, and shows
the report on screen.

```sh
# From example/ios; use the connected device UDID.
ENTRY_FILE=index.quality.js FORCE_BUNDLING=1 RCT_NO_LAUNCH_PACKAGER=1 \
  xcodebuild -workspace IoExample.xcworkspace -scheme IoExample \
  -configuration Debug -destination "id=$DEVICE_UDID" \
  ENABLE_DEBUG_DYLIB=NO 'SWIFT_ACTIVE_COMPILATION_CONDITIONS=$(inherited) IO_QUALITY_BUNDLED' \
  -derivedDataPath /tmp/react-native-io-device \
  -allowProvisioningUpdates build
```

For iOS 16 and earlier, when ios-deploy is available:

```sh
APP_PATH=/tmp/react-native-io-device/Build/Products/Debug-iphoneos/IoExample.app
ios-deploy --id "$DEVICE_UDID" --bundle "$APP_PATH" --nostart
idevicedebug --udid "$DEVICE_UDID" --detach run io.example.rct
ios-deploy --id "$DEVICE_UDID" --bundle_id io.example.rct \
  --download=/Documents/io-quality \
  --to /tmp/react-native-io-device-reports
```

On iOS 17 and later, use xcrun devicectl device install app and device process
launch for the installed Xcode. If an older device exits under newer LLDB with
lldb_image_notifier or SIGTRAP, use the legacy runner or idevicedebug rather
than ios-deploy --justlaunch.

Release comparisons must use a rebuilt -configuration Release app. Debug data
is useful for regression discovery and test-flow validation, but it is not
release-performance evidence and must not be compared directly with Release.

If a source-built iOS dependency reports a fmt consteval error, the optional test-build
compatibility step is:

```sh
cd example
node scripts/prepare-xcode26.cjs
```

Then add this xcodebuild flag:

```sh
'OTHER_CPLUSPLUSFLAGS=$(inherited) -DFMT_USE_CONSTEVAL=0'
```

This changes only the example Pod feature-detection guard and disables fmt's
compile-time format validation for that test build. It does not modify the
published library source.

## 6. Harness tests on physical devices

The quality command selects only Quality.harness.ts and does not depend on the
legacy Request.harness.ts public httpbin.org endpoints.

For iOS 17 and later:

```sh
IO_IOS_DEVICE_NAME='Your iPhone name' yarn test:quality:ios
```

For older iOS devices:

```sh
IO_IOS_LEGACY_UDID="$DEVICE_UDID" IO_IOS_APP_PATH="$APP_PATH" \
  yarn test:quality:ios
```

Harness builds and installs the normal Debug index.js entry. It does not set
ENTRY_FILE=index.quality.js or IO_QUALITY_BUNDLED. The adapter manages only this
test app's launch and termination; signing and installation remain the caller's
responsibility.

HARNESS_PROFILE in src/quality/config.ts defaults to quick. Set it to full or
soak for longer runs. Increase bridgeTimeout in rn-harness.config.mjs when
needed, and do not let the outer framework time out before native work settles.

Android Harness:

```sh
yarn test:quality:android
```

The command retains the existing Android runner configuration. Use the next
section for direct physical-device execution.

## 7. Standalone Android physical-device run

Confirm authorization and device state:

```sh
adb devices -l
export ANDROID_SERIAL=<device-serial>
```

For local fixture tests, keep this service running in another terminal:

```sh
# From the example directory
python3 scripts/http_server.py --host 127.0.0.1 --port 8765
adb -s "$ANDROID_SERIAL" reverse tcp:8765 tcp:8765
```

Build and install the standalone automatic entry:

```sh
# From example/android; adjust the ABI for the connected device.
./gradlew :app:assembleDebug -PreactNativeArchitectures=arm64-v8a \
  -PioExampleStandalone=true -PioExampleEntry=index.quality.js \
  --no-daemon --console=plain
adb -s "$ANDROID_SERIAL" install -r app/build/outputs/apk/debug/app-debug.apk
adb -s "$ANDROID_SERIAL" shell am force-stop io.example
adb -s "$ANDROID_SERIAL" shell am start -W -n io.example/.MainActivity
```

The automatic entry probes http://127.0.0.1:8765/health. Through adb reverse,
that address reaches the computer. Network cases run only when the fixture
returns the exact marker io-quality-fixture-v1; otherwise they are skipped.

Watch for the completion marker and report path:

```sh
adb -s "$ANDROID_SERIAL" logcat --pid=<test-process-pid>
```

Debug standalone builds retain run-as so the internal report can be exported:

```sh
# Wait for [IO_QUALITY_FINISHED], then use the filename from path=.
adb -s "$ANDROID_SERIAL" exec-out run-as io.example \
  cat files/io-quality/<report-file-name>.json \
  > /tmp/io-android-report.json
```

FS.documentDir maps to the app-internal files directory on Android.
A non-debuggable Release build normally cannot export this private path with
run-as. The embedded bundle uses dev=false, so environment.debug is false even
though this APK is a Debug native build; that field is not Release evidence.

Reinstall the interactive entry after an automatic run:

```sh
./gradlew :app:assembleDebug -PreactNativeArchitectures=arm64-v8a \
  -PioExampleStandalone=true -PioExampleEntry=index.js \
  --no-daemon --console=plain
adb -s "$ANDROID_SERIAL" install -r app/build/outputs/apk/debug/app-debug.apk
adb -s "$ANDROID_SERIAL" shell am force-stop io.example
adb -s "$ANDROID_SERIAL" shell am start -W -n io.example/.MainActivity
```

Remove only the fixture port when finished:

```sh
adb -s "$ANDROID_SERIAL" reverse --remove tcp:8765
```

## 8. Local HTTP fixture

Start from the example directory:

```sh
yarn test:fixture
```

For a physical device using the local network:

```sh
python3 scripts/http_server.py --host <computer-LAN-IP> --port 8765
```

Use http://<computer-hostname>.local:8765 or a reachable LAN address.
127.0.0.1 refers to the device itself unless adb reverse is active. The iOS
example declares local-network usage and follows the system permission prompt.
Do not disable global TLS verification to make a test pass.

The fixture exposes /health, /echo, /payload, /redirect, /slow, /error,
/range, /range-ignore, /range-invalid, and /upload. It is a controlled test
service without authentication or production hosting.

For Harness network cases, set TEST_HTTP_BASE_URL in
src/quality/config.ts to the same URL and rebuild. Verify the fixture contract:

```sh
yarn test:fixture-contract
```

## 9. Critical limits and fault injection

The default device suite does not allocate hundreds of MiB merely to hit a
buffer limit. For an exact boundary build, compile every relevant native
translation unit with the same -DRCT_IO_MAX_BUFFER_BYTES=65536 and set
TEST_NATIVE_BUFFER_LIMIT_BYTES to 65536. Run only the critical group.

The runner rejects configured limits above 1 MiB. A small-limit build verifies
boundary logic only; it does not prove that the default limit fits every
device. Do not mix ordinary full/soak large-file cases with this build.

Native regression and sanitizer tests:

```sh
yarn test:native
SANITIZE=thread yarn test:native
```

The native script covers limit and limit-plus-one behavior, quota return,
rejection under real backlog, concurrent close, callbacks after runtime
destruction, and atomic-write preservation after injected failure. ASan/UBSan
and TSan are separate runs and detect different memory and race classes.

## 10. Release performance reproduction

For a focused native-core read comparison on macOS, run:

```sh
bash example/scripts/benchmark-reads.sh
```

Run this command from the repository root. It compares committed `HEAD` with
the working tree using C++20 `-O3 -DNDEBUG`, a 32 MiB file, three warmups and
31 samples per API in ABBA order. It measures whole-file text reads,
file-handle text reads and binary reads with a warm OS page cache. These are
native-core measurements; they exclude JSI, worker scheduling and mobile
storage behavior. Use the device workflow below for app performance claims.

Release/R8 measurements require a physical device and a non-debuggable Release
APK. The Android app can launch the quality action or common-API benchmark
action from the same Release bundle; a normal launch opens the interactive UI.
Build type, debuggability, minification, and other metadata are recorded in
the report. **DEV** is not the only build indicator.

Release enables R8 and resource shrinking by default. Set ioExampleMinify=true
for the formal comparison. Release permits cleartext HTTP only for the local
fixture; other cleartext URLs remain blocked.

Reproducible tools are under example/scripts:

- prepare-android-release-baseline.py
- run-android-release-benchmark.py
- analyze-release-benchmark.py
- analyze-http-transport.py

The file benchmark includes synchronous binary writes, synchronous/asynchronous
binary and text reads, ASCII and non-ASCII UTF-8 encoding, Latin1 encoding,
UTF-8 decoding, and the existing hash/small-file cases. Both APKs must contain
the same `BenchmarkApp.tsx`. Encoding datasets produce 16 MiB of output;
Unicode input includes accented text, CJK and supplementary characters, while
Latin1 input includes high bytes and NUL. Synchronous samples
time the direct call without `await`; asynchronous samples include Promise
completion. Android reports are exported under
`FS.externalDir/Documents/io-quality` so `adb pull` works for Release builds.

To measure the library's actual Java download and multipart-upload APIs:

```sh
# Start the fixture on the computer, then forward both ports to the device.
python3 example/scripts/http_benchmark_server.py
adb reverse tcp:8876 tcp:8876
adb reverse tcp:8877 tcp:8877
adb shell am force-stop io.example
adb shell am start -W -n io.example/.HttpBenchmarkActivity \
  --ez productionBenchmark true
```

Use the current Release/R8 APK. The activity runs four rounds per protocol and
direction, with two warmups and nine measured transfers per round. It checks
download hashes and the server's multipart-upload hash. Pull the report from
`<externalFilesDir>/Documents/io-quality/http-transport-*.json` and summarize it
with `analyze-http-transport.py`. This measures the Java transport; it excludes
JSI and JavaScript scheduling.

Use [the release results](reports/README.md) as the measured reference. Do
not substitute Debug JSON for the Release baseline.

## 11. Performance, leaks, and acceptance

Discard the two warm-up samples and retain raw measurements. Reports calculate
median, p95, min, max, mean, standard deviation, and throughput as total
processed bytes divided by total elapsed time. They record system, React
Native, engine, debug state, seed, profile, concurrency, and timing. Five
quick-profile samples are smoke evidence, not a stable tail-latency result.

Compare baseline and change on the same physical device, Release mode, and
dataset. Alternate multiple rounds and record power state, temperature,
low-power mode, and background load. Set regression budgets from repeated
baseline noise rather than one threshold for every device.

A completed stress report proves only that selected cases completed; it does
not prove absence of leaks. On iOS use Instruments Allocations, Leaks, and VM
Tracker. On Android use the profiler, dumpsys meminfo, and thread/fd counts.
JavaScript heap size does not include the complete C++ vector, Hermes external
ArrayBuffer, NSData, or Java byte[] footprint.

Acceptance review must include failures, skip reasons, cleanup failures,
cancellation state, and environment metadata. A run with every network case
skipped cannot be marked network validation. The current suite does not cover
every TLS/proxy/certificate matrix, OS-kill/power-loss scenario, storage volume,
all hash known answers, or multi-hour memory and GC curves; record those as
separate device-acceptance work.

## 12. Startup diagnosis and Metro connectivity

The standalone entry mounts its UI before loading the library under test.
Missing native modules and startup exceptions remain visible in the UI and
device logs. On iOS, startup diagnostics use the system Settings bridge and
UserDefaults (IOQualityProgress and IOQualityMetroHost), not the library's file
API, so an early filesystem failure cannot hide the diagnostic.

The standalone entry probes the host recorded in ip.txt. A physical device is
considered connected to Metro only when it receives HTTP 200 and the exact
packager-status:running response. A successful computer-side curl is not proof
of device connectivity.

Start Metro on a reachable interface:

```sh
yarn start --host <computer-LAN-IP>
```

When no fixture URL is configured, the automatic entry also probes
<host>:8765/health. Only the exact body io-quality-fixture-v1 enables network
cases; otherwise the report records a clear skip. The fixture URL is recorded
in the report environment.

The bundled standalone build disables the Debug dylib and uses
IO_QUALITY_BUNDLED. Its performance numbers remain Debug numbers unless built
with the Release configuration.

## 13. HTTP transport notes

The current Release transport measurements are summarized in
[reports/README.md](reports/README.md). The benchmark tools under
example/scripts separate transfer wall time from client worker CPU time and do
not include fsync unless a benchmark case explicitly says so.

HTTP transport results and file API results measure different layers. Do not
compare their raw milliseconds as if they represented the same operation, and
do not use a local fixture result as evidence for production TLS, proxy, or
certificate behavior.

## 14. HarmonyOS validation

The checked-in `example/harmony` application uses the official Harmony CLI
0.84.3 template, RNAbility/RNApp, the RNOH worker, and official autolinking.
It targets HarmonyOS 6.1.1 (API 24), requires 5.0.4 (API 16), and builds arm64
device binaries. The library's source HAR is consumed through the example's
workspace dependency, rather than through a separate copy of its C++ sources.
See the [integration guide](../harmony/README.md) for directory mappings and
HTTP limitations.

### Prepare and build

```sh
# From the repository root
yarn install --immutable
yarn example prepare:harmony
yarn example build:harmony
```

The preparation command installs the HAR project's OHPM dependencies, rebuilds
`harmony/io.har`, invokes official `link-harmony`, and installs the example's
OHPM dependencies. It creates `example/harmony/build-profile.json5` from the
tracked template only when no local profile exists. Local signatures, generated
factories, raw bundles, dependency directories, and build outputs are ignored.

`build:harmony` defaults to Release, generates an optimized Hermes V1 bytecode
bundle, and calls official Hvigor `assembleHap`. The template provides no
personal signing credentials. Without local signing configuration, the output
is `example/harmony/entry/build/default/outputs/default/entry-default-unsigned.hap`.
Physical devices require a signed HAP. Some simulators also accept the unsigned
HAP for local validation.

The scripts default to DevEco Studio at
`/Applications/DevEco-Studio.app/Contents`. Override `DEVECO_HOME`,
`DEVECO_SDK_HOME`, `OHPM_BIN`, `HVIGOR_BIN`, or `HVIGOR_NODE` for other tool paths.

### Sign and run on a physical device

Open `example/harmony` in DevEco Studio. Sign in and configure automatic
signing under **File > Project Structure > Signing Configs** for the connected
device. Keep signing information in the ignored local build profile.
Rebuild the Release HAP after configuring signing:

```sh
# From the repository root; use the hdc binary from your SDK.
yarn example build:harmony
hdc list targets
hdc install example/harmony/entry/build/default/outputs/default/entry-default-signed.hap
hdc shell aa start -b xyz.bczl.ioexample -a EntryAbility
```

The Release application loads `hermes_bundle.hbc` from its own resources and
does not need Metro. The example's `BytecodeBundleProvider` supplies bytes to
RNOH's native buffer loader. RNOH 0.84.3's raw-resource mmap loader can report
`Buffer misaligned` when the HBC offset in the HAP is not aligned. This adds
a startup buffer copy and does not change the library's file or HTTP paths.
The home screen offers the existing interactive quality
tests and API examples. Run Quick first, then Full and Soak, and record device,
system API, native build mode, JavaScript development mode, and test profile.

### Debug installation and Metro

Run `yarn example prepare:harmony` once after a clean checkout, and again when
the library's native sources change. `run-harmony` builds and installs the app,
but does not build the library's source HAR. Set the SDK path before invoking
the official CLI:

```sh
export DEVECO_SDK_HOME=/Applications/DevEco-Studio.app/Contents/sdk
yarn example harmony
```

This is `react-native run-harmony` from the example directory. It defaults to
Debug, starts Metro when needed, installs the HAP, launches EntryAbility, and
sets up `hdc rport tcp:8081 tcp:8081`. A physical device requires the local
signing configuration described above. With multiple connected devices, pass
`--simulator <hdc-target>` to select the target; the official CLI uses this
option for both devices and simulators.

To keep Metro in an existing terminal, use two terminals from the repository
root:

```sh
# Terminal 1
yarn example start

# Terminal 2 (the currently connected simulator)
export DEVECO_SDK_HOME=/Applications/DevEco-Studio.app/Contents/sdk
yarn example harmony --no-packager --simulator 127.0.0.1:5555
```

Harmony needs both a platform-aware Metro resolver and a reachable bundle URL:

- `example/metro.config.js` merges the official `createHarmonyMetroConfig`
  with the monorepo configuration. For `platform=harmony`, imports of
  `react-native` resolve to `@react-native-oh/react-native-harmony`.
- The Debug page uses the official `MetroJSBundleProvider`. Its default URL is
  `http://localhost:8081/index.bundle?platform=harmony&dev=true&minify=false`.
  Here, `localhost` is the device; HDC reverse forwarding connects it to Metro
  on the computer. There is no need to replace the app's React Native dependency
  or manually substitute the computer's LAN address for this USB/simulator flow.
- For a different port, pass the same `--port` to `start` and `harmony`.
  Keep the CLI's packager option enabled, for example
  `yarn example harmony --port 8082`. It detects an already running Metro and
  updates the Metro provider in `pages/Index.ets` for non-default ports; review
  that source change before committing. In CLI 0.84.3, `--no-packager` skips
  this source update, so that combination requires setting the provider's port
  manually as well.

The Debug page falls back to embedded bytecode when Metro cannot be reached,
if an embedded bundle has already been generated. Seeing the home screen alone
does not prove Metro connectivity. Check that Metro receives a Harmony bundle
request and that a JavaScript edit appears through Fast Refresh. If forwarding
is lost after reconnecting a device, restore it with
`hdc -t <target> rport tcp:8081 tcp:8081` and reload the app.

A Debug HAP with an embedded fallback can be built with
`yarn example build:harmony debug`; its measurements must not be included in
Release performance results.

### HTTP fixture and native contract checks

Start the fixture and forward its port:

```sh
yarn example test:fixture
hdc rport tcp:8765 tcp:8765
```

Enter `http://127.0.0.1:8765` in the quality panel's fixture URL field before
starting the profile. Alternatively, use a reachable PC LAN address. Network
cases are skipped when no fixture URL is configured; a skipped group does
not validate HTTP. On HarmonyOS, resumable downloads restart from byte zero,
and multipart fields have the RCP limits documented in the integration guide.

From the repository root, run the desktop transport-contract checks:

```sh
yarn test:harmony-http
SANITIZE=thread yarn test:harmony-http
```

This test compiles against the real HarmonyOS SDK RCP declarations with an RCP
test double and ASan/UBSan or TSan. It checks binary data, bounded responses, redirect
configuration, failed downloads preserving old content, symlink rejection,
multipart field lifetime, concurrent transfers, and native resource cleanup.
It does not exercise HarmonyOS networking, certificate validation, or device I/O.

Physical-device acceptance must separately cover the shared functional,
boundary, critical, stress, and local-fixture network groups. The interactive
panel accepts an explicit fixture URL. The automatic entry's discovery covers
Android USB forwarding and iOS Settings, so configure its fixture explicitly
when using it on HarmonyOS.

Use a Release HAP and production JavaScript bundle for performance evaluation.
Do not label successful source-HAR packaging, native cross-compilation, or
desktop contract tests as successful device or TLS validation.
