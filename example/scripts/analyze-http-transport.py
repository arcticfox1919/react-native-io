#!/usr/bin/env python3
"""Summarize Release transport strategies by process-round medians, retaining all samples."""
import argparse
import json
from pathlib import Path
import statistics as stats


def summarize(report):
    assert report["success"] and not report["nativeDebuggable"] and report["r8"]
    output = []
    for protocol in ["http", "https"]:
        for direction in ["download", "upload"]:
            subset = [r for r in report["rows"] if (r["protocol"], r["direction"]) == (protocol, direction)]
            baselines = {r["round"]: r for r in subset if r["strategy"] == "stream-8k"}
            for strategy in sorted({r["strategy"] for r in subset}):
                rows = sorted([r for r in subset if r["strategy"] == strategy], key=lambda x: x["round"])
                assert len(rows) == report["rounds"]
                wall = [stats.median(r["wallMs"]) for r in rows]
                cpu = [stats.median(r["cpuMs"]) for r in rows]
                cpu_change = [100 * (value / stats.median(baselines[row["round"]]["cpuMs"]) - 1) for row, value in zip(rows, cpu)] if baselines else []
                wall_change = [100 * (value / stats.median(baselines[row["round"]]["wallMs"]) - 1) for row, value in zip(rows, wall)] if baselines else []
                output.append({"protocol": protocol, "direction": direction, "strategy": strategy,
                               "wallMedianMs": stats.median(wall), "cpuMedianMs": stats.median(cpu),
                               "roundWallMs": wall, "roundCpuMs": cpu,
                               "pairedCpuChangePercent": cpu_change, "pairedWallChangePercent": wall_change})
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.output and args.output.resolve().is_relative_to(Path(__file__).resolve().parents[1] / "reports"):
        parser.error("example/reports keeps only README.md; write raw data outside the repository")
    result = summarize(json.loads(args.report.read_text()))
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("protocol direction strategy | wall ms | client-thread CPU ms | paired CPU change range")
    for r in result:
        changes = r['pairedCpuChangePercent']
        change_range = f"{min(changes):+.1f}%..{max(changes):+.1f}%" if changes else "n/a (production API only)"
        print(f"{r['protocol']} {r['direction']} {r['strategy']} | {r['wallMedianMs']:.3f} | {r['cpuMedianMs']:.3f} | "
              f"{change_range}")


if __name__ == "__main__":
    main()
