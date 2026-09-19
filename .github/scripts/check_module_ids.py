#!/usr/bin/env python3
"""Detect duplicate, misaligned or out-of-block module IDs (D55).

The central table is an X-macro list (``EDGE_MODULE_IDS``), so allocating an ID is
one line and the C side generates its own segment and block assertions from it.
This guard catches the two things a C compiler will not:

* a duplicate value -- legal C, silent bug at run time;
* a value outside the block allocated to the entry's owning layer, or a layer
  token that has no block at all.

A declared block may legitimately be empty: reserving blocks is the point, since
it is what lets parallel authors allocate without competing for one number.

Usage: check_module_ids.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

HEADER = "edge_module/include/edge/modules.h"
ENTRY = re.compile(
    r"^\s*X\(\s*([A-Za-z0-9_]+)\s*,\s*(0[xX][0-9A-Fa-f]+|\d+)\s*,\s*([A-Za-z0-9_]+)\s*\)",
    re.M,
)
BLOCK = re.compile(
    r"^#define\s+EDGE_MODULE_BLOCK_([A-Za-z0-9_]+)_(LO|HI)\s+(0[xX][0-9A-Fa-f]+|\d+)\s*u?\s*$",
    re.M,
)


def check(root: Path) -> tuple:
    header = root / HEADER
    if not header.is_file():
        return (1, [f"missing central module table: {header}"])
    text = header.read_text(encoding="utf-8")

    entries = [(name, int(segment, 0), layer) for name, segment, layer in ENTRY.findall(text)]
    if not entries:
        return (1, [f"{HEADER}: no EDGE_MODULE_IDS() entries found"])

    blocks = {}
    for layer, bound, value in BLOCK.findall(text):
        blocks.setdefault(layer, {})[bound] = int(value, 0)

    seen = {}
    problems = []
    for name, segment, layer in entries:
        if segment == 0:
            problems.append(f"module ID 0 is reserved: {name}")
        if segment & 0xFF:
            problems.append(f"module ID not on a 0xNN00 segment: {name} = 0x{segment:04x}")
        if segment in seen:
            problems.append(f"module ID collision: {name} and {seen[segment]} = 0x{segment:04x}")
        else:
            seen[segment] = name

        bounds = blocks.get(layer)
        if not bounds or "LO" not in bounds or "HI" not in bounds:
            problems.append(
                f"{name} declares layer '{layer}', which has no "
                f"EDGE_MODULE_BLOCK_{layer}_LO/HI: every layer needs an allocated block"
            )
        elif not (bounds["LO"] <= segment <= bounds["HI"]):
            problems.append(
                f"{name} = 0x{segment:04x} is outside the block allocated to layer "
                f"'{layer}' (0x{bounds['LO']:04x}-0x{bounds['HI']:04x}); allocate it in the "
                "block of the layer that owns the module"
            )

    if problems:
        return (1, problems)
    return (0, [f"module ID check: PASS ({len(entries)} IDs in {len(blocks)} layer blocks)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
