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
        if name.endswith("_BASE"):
            continue
        value = value.split("/*", 1)[0].strip()
        direct = re.fullmatch(r"\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?", value)
        if direct:
            values[name] = int(direct.group(1), 0)
            continue
        relative = re.fullmatch(r"\(?(EDGE_EVT_[A-Z0-9_]+)\s*\+\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\)?", value)
        if relative and relative.group(1) in values:
            values[name] = values[relative.group(1)] + int(relative.group(2), 0)

    # Decentralized scan across app/ and infra/ headers
    scanned_headers = 0
    domain_event_count = 0
    for search_dir in ("app", "infra", "board"):
        p_dir = root / search_dir
        if not p_dir.is_dir():
            continue
        for h_path in p_dir.glob("**/include/**/*.h"):
            scanned_headers += 1
            h_text = h_path.read_text(encoding="utf-8", errors="replace")
            for name, val_expr in re.findall(r"^#define\s+(EDGE_EVT_[A-Z0-9_]+)\s+(.+)$", h_text, re.M):
                if name.endswith("_BASE"):
                    continue
                val_expr = val_expr.split("/*", 1)[0].strip()
                direct = re.fullmatch(r"\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?", val_expr)
                if direct:
                    val = int(direct.group(1), 0)
                    if name not in values:
                        values[name] = val
                        domain_event_count += 1
                else:
                    alias_match = re.fullmatch(r"\(?([A-Z0-9_]+)\)?", val_expr)
                    if alias_match and alias_match.group(1) in values:
                        continue
                    rel = re.fullmatch(r"\(?([A-Z0-9_]+)\s*\+\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\)?", val_expr)
                    if rel and rel.group(1) in values:
                        val = values[rel.group(1)] + int(rel.group(2), 0)
                        if name not in values:
                            values[name] = val
                            domain_event_count += 1

    seen = {}
    problems = []
    for name, value in values.items():
        if value in seen and seen[value] != name:
            problems.append(f"event ID collision: {name} and {seen[value]} = 0x{value:04x}")
        else:
            seen[value] = name
    if problems:
        return (1, problems)
    return (0, [f"event ID check: PASS ({len(values)} numeric event IDs verified across central and {scanned_headers} domain headers)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
