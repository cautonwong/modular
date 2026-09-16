#!/usr/bin/env python3
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[2]
header = root / "edge_module/include/edge/events.h"
if not header.is_file():
    print(f"missing central event table: {header}")
    raise SystemExit(1)

text = header.read_text(encoding="utf-8")
names = {}
values = {}
for name, value in re.findall(r'^#define\s+(EDGE_EVT_[A-Z0-9_]+)\s+(.+)$', text, re.M):
    value = value.split('/*', 1)[0].strip()
    m = re.fullmatch(r'\(?\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\s*\)?', value)
    if m:
        values[name] = int(m.group(1), 0)
        continue
    m = re.fullmatch(r'\(?(EDGE_EVT_[A-Z0-9_]+)\s*\+\s*(0x[0-9A-Fa-f]+|[0-9]+)u?\)?', value)
    if m and m.group(1) in values:
        values[name] = values[m.group(1)] + int(m.group(2), 0)

for name, value in values.items():
    if value in names:
        print(f"event ID collision: {name} and {names[value]} = 0x{value:04x}")
        raise SystemExit(1)
    names[value] = name

print(f"event ID check: PASS ({len(values)} numeric IDs)")
