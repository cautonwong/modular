#!/usr/bin/env python3
"""Detect duplicate numeric event IDs in the central allocation table.

Usage: check_event_ids.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]


def check(root: Path) -> tuple:
    header = root / "edge_module/include/edge/events.h"
    if not header.is_file():
        return (1, [f"missing central event table: {header}"])

    text = header.read_text(encoding="utf-8")
    values = {}
    for name, value in re.findall(r"^#define\s+(EDGE_EVT_[A-Z0-9_]+)\s+(.+)$", text, re.M):
        value = value.split("/*", 1)[0].strip()
        direct = re.fullmatch(r"\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?", value)
        if direct:
            values[name] = int(direct.group(1), 0)
            continue
        relative = re.fullmatch(r"\(?(EDGE_EVT_[A-Z0-9_]+)\s*\+\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\)?", value)
        if relative and relative.group(1) in values:
            values[name] = values[relative.group(1)] + int(relative.group(2), 0)

    seen = {}
    problems = []
    for name, value in values.items():
        if value in seen:
            problems.append(f"event ID collision: {name} and {seen[value]} = 0x{value:04x}")
        else:
            seen[value] = name
    if problems:
        return (1, problems)
    return (0, [f"event ID check: PASS ({len(values)} numeric IDs)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
