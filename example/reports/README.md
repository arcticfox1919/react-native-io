# Release Performance

2026-10-22 · Xiaomi MIX Fold (M2011J18C), Android 13, ARM64, React Native 0.84.1, Hermes, non-debuggable Release with R8.

## File APIs

Current working tree. Four rounds, five warmups and 30 samples per API per round. Results are medians of the round medians.

| API                                | Mode  | Payload |     Median |
| ---------------------------------- | ----- | ------- | ---------: |
| `File.writeBytes`                  | Async | 16 MiB  |  18.418 ms |
| `File.writeBytesSync`              | Sync  | 16 MiB  |  15.116 ms |
| `File.readBytes`                   | Async | 16 MiB  |  10.806 ms |
| `File.readBytesSync`               | Sync  | 16 MiB  |   8.803 ms |
| `File.readString`                  | Async | 16 MiB  |  12.923 ms |
| `File.readStringSync`              | Sync  | 16 MiB  |   6.872 ms |
| `FS.encodeString` (UTF-8, ASCII)   | Sync  | 16 MiB  |  19.287 ms |
| `FS.encodeString` (UTF-8, Unicode) | Sync  | 16 MiB  |  73.767 ms |
| `FS.encodeString` (Latin1)         | Sync  | 16 MiB  | 183.443 ms |
| `FS.decodeString` (UTF-8)          | Sync  | 16 MiB  |   2.835 ms |
| `File.calcHash(SHA256)`            | Async | 16 MiB  |  71.768 ms |
| `File.writeBytes`                  | Async | 4 KiB   |   0.146 ms |
| `File.readBytes`                   | Async | 4 KiB   |   0.128 ms |
| `File.writeString`                 | Async | 4 KiB   |   0.171 ms |

Async timings include scheduling and Promise completion; sync timings measure the direct call without `await`. Binary inputs are `ArrayBuffer`. Encoding payloads are output byte counts; Unicode input contains accented text, CJK and emoji, and Latin1 includes high bytes and NUL. Other text is ASCII. Reads use a warm page cache; writes overwrite and close without `fsync`.

## HTTP Transport

Java `IOHttpClient.download` and multipart `upload`, 4 MiB over ADB-forwarded localhost. Four rounds, two warmups and nine samples per round; medians of round medians. Wall time includes connection setup and file handling, without `fsync`; JavaScript/JSI are excluded.

| Scenario       | Median wall time | Median client worker CPU |
| -------------- | ---------------: | -----------------------: |
| HTTP download  |       153.357 ms |                61.511 ms |
| HTTP upload    |       184.278 ms |                19.432 ms |
| HTTPS download |       154.255 ms |                85.876 ms |
| HTTPS upload   |       185.391 ms |                30.416 ms |

Client CPU covers the benchmark worker thread and excludes server CPU.
