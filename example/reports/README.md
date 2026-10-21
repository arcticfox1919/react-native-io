# Release Performance

Test device: Xiaomi MIX Fold, Android 13, Hermes, non-debuggable Release build with R8 enabled.

## File APIs

The benchmark used four same-device AB/BA pairs, five warm-up iterations, and 30 measured samples per API. The table reports medians. Except for `readBytesSync`, each value is end-to-end JavaScript Promise latency and includes JSI calls, worker scheduling, native I/O, and Promise completion.

| API / payload                    |  Baseline |   Current | Change |
| -------------------------------- | --------: | --------: | -----: |
| `File.writeBytes` / 16 MiB       | 18.396 ms | 18.829 ms |  +2.1% |
| `File.readBytes` / 16 MiB        | 20.468 ms | 13.931 ms | -32.8% |
| `File.calcHash(SHA256)` / 16 MiB | 72.964 ms | 71.664 ms |  -1.8% |
| `File.readBytesSync` / 16 MiB    | 15.249 ms | 11.288 ms | -25.9% |
| `File.writeBytes` / 4 KiB        |  0.144 ms |  0.137 ms |  -4.4% |
| `File.readBytes` / 4 KiB         |  0.138 ms |  0.128 ms |  -7.1% |
| `File.writeString` / 4 KiB       |  0.146 ms |  0.147 ms |  +1.4% |

Reads used a warmed page cache. Regular writes used overwrite and close without `fsync`. Sync and async results run in different execution contexts and must not be subtracted to estimate worker-dispatch overhead.

## HTTP Transport

The native transport benchmark transferred 4 MiB over a local connection and ran each strategy four times. Wall time includes connection setup, system HTTP/TLS processing, transfer, and close. It excludes `fsync`.

| Scenario       | Median wall time | Median client worker CPU |
| -------------- | ---------------: | -----------------------: |
| HTTP download  |       160.286 ms |                60.350 ms |
| HTTP upload    |       137.650 ms |                16.057 ms |
| HTTPS download |       165.145 ms |                88.048 ms |
| HTTPS upload   |       141.665 ms |                30.337 ms |

The HTTP table measures the native transport layer and excludes JavaScript, JSI, and Promise scheduling. It is not directly comparable with the file API table.
