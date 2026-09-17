#!/usr/bin/env python3
"""Verify the ADR -> gate matrix.

A decision that claims to be implemented must have an automatable gate; ADRs
without one are reported as paper-only (tracked debt). `must_gate` lists the
core invariants that may never be paper-only.

Usage: check_adr_gates.py [root]
"""
from pathlib import Path
import json
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
ADR_RE = re.compile(r"\bD\d+\b")
ADR_RANGE_RE = re.compile(r"\bD(\d+)\s*[–-]\s*D(\d+)\b")


def collect_adrs(text: str) -> set:
    adrs = set(ADR_RE.findall(text))
    for low, high in ADR_RANGE_RE.findall(text):
        for number in range(int(low), int(high) + 1):
            adrs.add(f"D{number}")
    return adrs


def check(root: Path) -> tuple:
    config_path = root / "ci/adr-gates.json"
    conformance = root / "docs/adr-conformance.md"
    if not config_path.is_file():
        return (1, [f"missing {config_path}"])

    config = json.loads(config_path.read_text(encoding="utf-8"))
    gates = config.get("gates", {})
    must_gate = config.get("must_gate", [])

    adrs = set()
    if conformance.is_file():
        adrs = collect_adrs(conformance.read_text(encoding="utf-8"))

    problems = []
    for adr, gate in gates.items():
        path = root / gate["file"]
        if not path.is_file():
            problems.append(f"{adr}: gate file missing: {gate['file']}")
            continue
        marker = gate.get("marker")
        if marker and marker not in path.read_text(encoding="utf-8", errors="replace"):
            problems.append(f"{adr}: marker '{marker}' not found in {gate['file']}")

    for adr in must_gate:
        if adr not in gates:
            problems.append(f"{adr}: listed in must_gate but has no gate entry")

    if adrs:
        for adr in sorted(gates):
            if adr not in adrs:
                problems.append(f"{adr}: gate references an ADR absent from docs/adr-conformance.md")

    paper_only = sorted(adrs - set(gates))
    if problems:
        return (1, problems)
    return (
        0,
        [
            f"ADR gate check: PASS ({len(gates)} gated, {len(paper_only)} paper-only)",
            "paper-only: " + (", ".join(paper_only) if paper_only else "none"),
        ],
    )


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
