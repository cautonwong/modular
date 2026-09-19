#!/usr/bin/env python3
"""Compare the current build's footprint against a recorded baseline (#104).

`check_map_budget.py` answers "is this build within budget?". That is an absolute
gate: an image can drift upward for months and still pass, until the day it does
not. This script answers the other question -- "did *this* change make it bigger?"
-- by comparing against a recorded baseline and failing when the growth exceeds a
configured threshold.

It reuses the existing `--report` JSON from `check_map_budget.py` (no new
infrastructure), and writes a Markdown table that the workflow appends to
`$GITHUB_STEP_SUMMARY`, so the numbers are visible on the pull request without
posting comments from CI.

Usage:
    report_size_trend.py <size-report.json> [--target NAME] [--baseline FILE]
                         [--budget FILE] [--summary FILE]

With no baseline entry for the target, it prints a ready-to-paste snippet and
passes: recording a baseline needs a real build of that target, which is exactly
what the ARM job does.
"""
from pathlib import Path
import argparse
import json
import sys

DEFAULT_BASELINE = "ci/size-baseline.json"
DEFAULT_BUDGET = "ci/size-budget.json"
DEFAULT_TARGET = "example_cortex_m0"


def load(path: Path, default):
    if not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def main(argv) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("report")
    parser.add_argument("--target", default=DEFAULT_TARGET)
    parser.add_argument("--baseline", default=DEFAULT_BASELINE)
    parser.add_argument("--budget", default=DEFAULT_BUDGET)
    parser.add_argument("--summary", help="append a Markdown table here (CI step summary)")
    args = parser.parse_args(argv[1:])

    report = load(Path(args.report), None)
    if report is None:
        print(f"missing size report: {args.report}")
        return 1

    totals = report.get("total", {})
    layers = report.get("layers", {})

    baseline = load(Path(args.baseline), {}) or {}
    entry = (baseline.get("targets", {}) or {}).get(args.target)

    if not entry:
        snippet = json.dumps({
            "targets": {args.target: {"recorded_from": Path(args.report).name,
                                      "totals": totals, "layers": layers}}
        }, indent=2, sort_keys=True)
        message = (f"no baseline recorded for target '{args.target}'.\n"
                   f"Record one by pasting this into {args.baseline}:\n{snippet}")
        print(message)
        if args.summary:
            with open(args.summary, "a", encoding="utf-8") as handle:
                handle.write(f"\n### Size trend: {args.target}\n\n"
                             "No baseline recorded yet, so no comparison was made. "
                             "Paste this into `ci/size-baseline.json`:\n\n"
                             f"```json\n{snippet}\n```\n")
        return 0

    budget = load(Path(args.budget), {}) or {}
    limits = budget.get("trend", {})
    max_flash = limits.get("total_flash_bytes", 512)
    max_ram = limits.get("total_ram_bytes", 256)
    max_layer_pct = limits.get("layer_percent", 20)

    rows = []
    failures = []

    base_totals = entry.get("totals", {})
    for kind, limit in (("flash", max_flash), ("ram", max_ram)):
        now = totals.get(kind, 0)
        was = base_totals.get(kind, 0)
        delta = now - was
        pct = (100.0 * delta / was) if was else 0.0
        rows.append((f"total {kind}", was, now, delta, pct))
        if delta > limit:
            failures.append(f"total {kind} grew {delta} bytes (limit {limit})")

    base_layers = entry.get("layers", {})
    for name in sorted(set(layers) | set(base_layers)):
        for kind in ("flash", "ram"):
            now = layers.get(name, {}).get(kind, 0)
            was = base_layers.get(name, {}).get(kind, 0)
            delta = now - was
            if delta == 0:
                continue
            pct = (100.0 * delta / was) if was else float("inf")
            rows.append((f"{name} {kind}", was, now, delta, pct))
            if was and pct > max_layer_pct:
                failures.append(
                    f"{name} {kind} grew {delta} bytes ({pct:.1f}%, limit {max_layer_pct}%)"
                )

    lines = [
        "| metric | baseline | current | delta | % |",
        "|---|---:|---:|---:|---:|",
    ]
    for name, was, now, delta, pct in rows:
        shown = "n/a" if pct == float("inf") else f"{pct:+.1f}%"
        lines.append(f"| {name} | {was} | {now} | {delta:+d} | {shown} |")
    table = "\n".join(lines)
    print(f"size trend for target '{args.target}' (baseline {args.baseline}):")
    print(table)

    if args.summary:
        with open(args.summary, "a", encoding="utf-8") as handle:
            handle.write(f"\n### Size trend: {args.target}\n\n{table}\n")

    if failures:
        print("\nsize regression beyond the configured trend thresholds:")
        for failure in failures:
            print(f"  {failure}")
        print("\nIf the growth is intentional, update ci/size-baseline.json in this change.")
        return 1
    print("\nsize trend: PASS (within thresholds)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
