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

If React Native 0.83 reports a fmt consteval error, the optional test-build
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
