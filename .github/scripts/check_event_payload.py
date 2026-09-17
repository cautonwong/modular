#!/usr/bin/env python3
"""Assert the event payload is fixed scalar fields only (D18).

The event struct must stay `{id, source, arg0, arg1, timestamp}` with unsigned
scalar types, so no bare pointer can ever be smuggled through an event. The
`sizeof` assert alone cannot catch a field being retyped to a pointer.

Usage: check_event_payload.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
EXPECTED = {
    "id": "uint32_t",
    "source": "uint32_t",
    "arg0": "uint32_t",
    "arg1": "uint32_t",
    "timestamp": "uint64_t",
}


def check(root: Path) -> tuple:
    header = root / "edge_module/include/edge/event.h"
    if not header.is_file():
        return (1, [f"missing event header: {header}"])

    match = re.search(r"struct\s+edge_event\s*\{([^}]*)\}", header.read_text(encoding="utf-8"))
    if not match:
        return (1, ["struct edge_event not found"])

    fields = {}
    for declaration in match.group(1).split(";"):
        tokens = declaration.split()
        if len(tokens) >= 2:
            fields[tokens[-1]] = tokens[0]

    problems = []
    if set(fields) != set(EXPECTED):
        problems.append(f"event fields {sorted(fields)} != expected {sorted(EXPECTED)}")
    for name, expected_type in EXPECTED.items():
        actual = fields.get(name)
        if actual is None:
            continue
        if "*" in actual:
            problems.append(f"event field '{name}' must not be a pointer")
        elif actual != expected_type:
            problems.append(f"event field '{name}' type '{actual}' != '{expected_type}'")

    if problems:
        return (1, problems)
    return (0, [f"event payload check: PASS ({len(fields)} scalar fields)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
