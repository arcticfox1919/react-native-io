#!/usr/bin/env python3
"""Summarize independent process rounds without pretending samples are independent runs."""
import argparse
import json
from pathlib import Path
import statistics


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("campaign", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    reports = Path(__file__).resolve().parents[1] / "reports"
    if args.output.resolve().is_relative_to(reports):
        parser.error("example/reports keeps only README.md; write JSON outside the repository, e.g. /tmp")
    campaign = json.loads(args.campaign.read_text())
    runs = [(meta, json.loads((args.campaign.parent / meta["report"]).read_text()))
            for meta in campaign["runs"]]
    assert len(runs) == 2 * campaign["pairs"], "incomplete campaign"
    labels = [m["label"] for m in runs[0][1]["measurements"]]
    summary = {"campaign": campaign["campaign"], "processRoundsPerVariant": campaign["pairs"],
               "note": "Process-round medians are the replication units. p95 values describe each 30-sample round; no statistical significance or general device claim.",
               "metrics": []}
    for label in labels:
        grouped = {"baseline": [], "optimized": []}
        paired = {}
        for meta, report in runs:
            measurement = next(m for m in report["measurements"] if m["label"] == label)
            grouped[meta["variant"]].append(measurement["summary"])
            paired.setdefault(meta["pair"], {})[meta["variant"]] = measurement["summary"]["medianMs"]
        metric = {"label": label}
        for variant, values in grouped.items():
            medians = [x["medianMs"] for x in values]
            metric[variant] = {"roundMediansMs": medians, "medianOfRoundMediansMs": statistics.median(medians),
                               "minRoundMedianMs": min(medians), "maxRoundMedianMs": max(medians),
                               "roundP95Ms": [x["p95Ms"] for x in values],
                               "medianRoundThroughputMiBps": statistics.median(x["throughputMiBps"] for x in values)}
        changes = [100 * (values["optimized"] / values["baseline"] - 1) for _, values in sorted(paired.items())]
        metric["pairedLatencyChangePercent"] = changes
        metric["medianPairedLatencyChangePercent"] = statistics.median(changes)
        metric["directionAcrossPairs"] = ("all faster" if all(x < 0 for x in changes) else
                                           "all slower" if all(x > 0 for x in changes) else "mixed")
        summary["metrics"].append(metric)
    args.output.write_text(json.dumps(summary, indent=2) + "\n")
    print("metric | baseline ms | optimized ms | paired change | per-pair direction")
    for m in summary["metrics"]:
        print(f"{m['label']} | {m['baseline']['medianOfRoundMediansMs']:.3f} | "
              f"{m['optimized']['medianOfRoundMediansMs']:.3f} | "
              f"{m['medianPairedLatencyChangePercent']:+.1f}% | {m['directionAcrossPairs']}")


if __name__ == "__main__":
    main()
