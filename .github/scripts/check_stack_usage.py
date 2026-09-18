#!/usr/bin/env python3
"""Static stack-usage gate.

GCC `-fstack-usage` emits one `.su` file per object with the per-function stack
frame size. This gate fails if any function exceeds a byte budget and prints the
worst offenders. It is a *sound per-function* bound, not a call-graph worst case
(function-pointer dispatch makes a static call graph unsound here).

Usage:
    check_stack_usage.py <file-or-build-dir> [--max-bytes N] [--report out.json]
                         [--exclude-substr S]...
"""
import argparse
import json
import re
import sys
from pathlib import Path

LINE = re.compile(
    r"^(?P<file>.*?):(?P<line>\d+):(?P<col>\d+):(?P<func>[^\t]+)\t"
    r"(?P<bytes>\d+)\t(?P<qual>[^\t]+)\s*$"
)


def parse(path):
    entries = []
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = LINE.match(raw)
        if match:
            entries.append(
                {
                    "bytes": int(match.group("bytes")),
                    "function": match.group("func"),
                    "file": match.group("file"),
                    "line": int(match.group("line")),
                    "qualifier": match.group("qual"),
                }
            )
    return entries


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("path")
    parser.add_argument("--max-bytes", type=int, default=512)
    parser.add_argument("--report")
    parser.add_argument("--exclude-substr", action="append", default=[])
    args = parser.parse_args(argv)

    root = Path(args.path)
    if not root.exists():
        print(f"path not found: {root}")
        return 2
    su_files = [root] if root.is_file() else sorted(root.rglob("*.su"))

    entries = []
    for su_file in su_files:
        for entry in parse(su_file):
            if any(token in entry["file"] for token in args.exclude_substr):
                continue
            entries.append(entry)

    if not entries:
        print(f"stack usage check: no .su entries under {root}")
        return 2

    worst = sorted(entries, key=lambda e: e["bytes"], reverse=True)[:10]
    over = [e for e in entries if e["bytes"] > args.max_bytes]

    print(f"stack usage: {len(entries)} functions, max budget {args.max_bytes} bytes")
    print(f"{'bytes':>6}  function")
    for entry in worst:
        print(f"{entry['bytes']:>6}  {entry['function']}  ({entry['file']}:{entry['line']})")

    if args.report:
        report = {
            "max_budget": args.max_bytes,
            "function_count": len(entries),
            "worst": worst,
            "over_budget": over,
        }
        Path(args.report).write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")
        print(f"wrote {args.report}")

    if over:
        print(f"stack usage exceeded by {len(over)} function(s):")
        for entry in over:
            print(f"  {entry['bytes']} > {args.max_bytes}  {entry['function']} ({entry['file']})")
        return 1
    print("stack usage check: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
