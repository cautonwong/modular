#!/usr/bin/env python3
"""Detect duplicate or misaligned module IDs in the central table (D55).

Usage: check_module_ids.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

HEADER = "edge_module/include/edge/modules.h"
DIRECT = re.compile(r"^#define\s+(EDGE_MOD_[A-Z0-9_]+)\s+(0[xX][0-9A-Fa-f]+|[0-9]+)u?\s*$", re.M)


def check(root: Path) -> tuple:
    header = root / HEADER
    if not header.is_file():
        return (1, [f"missing central module table: {header}"])

    values = {name: int(value, 0) for name, value in DIRECT.findall(header.read_text(encoding="utf-8"))}
    seen = {}
    problems = []
    for name, value in values.items():
        if value == 0:
            problems.append(f"module ID 0 is reserved: {name}")
        if value & 0xFF:
            problems.append(f"module ID not on a 0xNN00 segment: {name} = 0x{value:04x}")
        if value in seen:
            problems.append(f"module ID collision: {name} and {seen[value]} = 0x{value:04x}")
        else:
            seen[value] = name

    if problems:
        return (1, problems)
    return (0, [f"module ID check: PASS ({len(values)} numeric IDs)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
