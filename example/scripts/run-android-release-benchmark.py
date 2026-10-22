#!/usr/bin/env python3
"""Counterbalanced same-device Release A/B; only touches the io.example test App."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time
from datetime import datetime, timezone


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--baseline-apk", required=True, type=Path)
    parser.add_argument("--optimized-apk", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--pairs", type=int, default=4)
    parser.add_argument("--cooldown", type=float, default=20)
    args = parser.parse_args()
    reports = Path(__file__).resolve().parents[1] / "reports"
    if args.output.resolve().is_relative_to(reports):
        parser.error("example/reports keeps only README.md; write JSON outside the repository, e.g. /tmp")
    if not 2 <= args.pairs <= 20 or not 0 <= args.cooldown <= 300:
        parser.error("pairs must be 2..20; cooldown 0..300 seconds")
    apks = {"baseline": args.baseline_apk.resolve(), "optimized": args.optimized_apk.resolve()}
    for apk in apks.values():
        if not apk.is_file():
            parser.error(f"missing APK: {apk}")
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("output must be absent or empty; existing campaign data is preserved")
    args.output.mkdir(parents=True, exist_ok=True)
    campaign = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S")

    def adb(*command, check=True, timeout=120):
        return subprocess.run(["adb", "-s", args.serial, *command], text=True,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              check=check, timeout=timeout)

    def snapshot():
        return {"at": datetime.now(timezone.utc).isoformat(),
                "battery": adb("shell", "dumpsys", "battery").stdout,
                "thermal": adb("shell", "dumpsys", "thermalservice", check=False).stdout,
                "lowPower": adb("shell", "settings", "get", "global", "low_power").stdout.strip()}

    manifest = {"campaign": campaign, "pairs": args.pairs, "cooldownSeconds": args.cooldown,
                "order": "AB, BA repeated; A=baseline, B=optimized", "runs": [],
                "apkSha256": {key: hashlib.sha256(path.read_bytes()).hexdigest() for key, path in apks.items()},
                "device": {name: adb("shell", "getprop", prop).stdout.strip() for name, prop in {
                    "model": "ro.product.model", "soc": "ro.soc.model", "android": "ro.build.version.release",
                    "sdk": "ro.build.version.sdk", "abi": "ro.product.cpu.abi", "build": "ro.build.version.incremental"}.items()}}
    manifest_path = args.output / "campaign.json"
    try:
        for pair in range(1, args.pairs + 1):
            order = ("baseline", "optimized") if pair % 2 else ("optimized", "baseline")
            for position, variant in enumerate(order, 1):
                run_id = f"{campaign}-p{pair}-{position}-{variant}"
                print(f"INSTALL {run_id}", flush=True)
                adb("install", "-r", str(apks[variant]))
                adb("shell", "am", "force-stop", "io.example")
                print(f"COOLDOWN {args.cooldown}s", flush=True)
                time.sleep(args.cooldown)
                before = snapshot()
                package = adb("shell", "dumpsys", "package", "io.example").stdout
                flags = [line.strip() for line in package.splitlines() if "pkgFlags=" in line or "flags=[" in line]
                if any("DEBUGGABLE" in line for line in flags):
                    raise RuntimeError("Refusing a debuggable APK")
                device_path = f"/storage/emulated/0/Android/data/io.example/files/Documents/io-quality/benchmark-{variant}-{run_id}.json"
                print(f"RUN {run_id}", flush=True)
                adb("shell", "am", "start", "-W", "-n", "io.example/.MainActivity", "--es", "ioExampleAction", "benchmark",
                    "--es", "ioBenchmarkRunId", run_id)
                deadline = time.monotonic() + 180
                local = args.output / f"{run_id}.json"
                while time.monotonic() < deadline:
                    if adb("shell", "ls", device_path, check=False).returncode == 0:
                        adb("pull", device_path, str(local))
                        try:
                            report = json.loads(local.read_text())
                        except json.JSONDecodeError:
                            time.sleep(1)
                            continue
                        break
                    time.sleep(2)
                else:
                    raise TimeoutError(f"No completed report for {run_id}; inspect only this App's logcat")
                build = report["environment"]["nativeBuild"]
                assert report["kind"] == "release-common-api-benchmark"
                assert build == {"type": "release", "debuggable": False, "minified": True, "variant": variant}, build
                assert report["runId"] == run_id and not report["environment"]["jsDev"]
                assert len(report["measurements"]) == 14
                assert all(len(m["rawMs"]) == 30 for m in report["measurements"])
                after = snapshot()
                manifest["runs"].append({"pair": pair, "position": position, "variant": variant,
                                         "report": local.name, "sha256": hashlib.sha256(local.read_bytes()).hexdigest(),
                                         "packageFlags": flags, "before": before, "after": after})
                manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
                print(f"PASS {run_id} ({len(report['measurements'])} metrics, correctness checked)", flush=True)
    finally:
        manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")
        print("RESTORE optimized Release interactive UI", flush=True)
        adb("install", "-r", str(apks["optimized"]), check=False)
        adb("shell", "am", "force-stop", "io.example", check=False)
        adb("shell", "am", "start", "-W", "-n", "io.example/.MainActivity", check=False)
    print(f"DONE {manifest_path}", flush=True)


if __name__ == "__main__":
    main()
