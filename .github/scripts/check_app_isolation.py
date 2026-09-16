#!/usr/bin/env python3
"""Reject app modules that include concrete infrastructure/product/board headers."""
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]
APP_ROOT = ROOT / "app"
FORBIDDEN = re.compile(r'^\s*#\s*include\s*[<\"](?:infra|product|board|sys)/', re.M)

if not APP_ROOT.exists():
    print("app/: not present; isolation check is vacuously satisfied")
    raise SystemExit(0)

violations = []
for path in APP_ROOT.rglob("*"):
    if path.suffix not in {".c", ".h", ".cc", ".cpp"}:
        continue
    text = path.read_text(encoding="utf-8")
    if FORBIDDEN.search(text):
        violations.append(path.relative_to(ROOT))

if violations:
    print("Application dependency boundary violation:")
    for path in violations:
        print(f"  {path}")
    raise SystemExit(1)

print("Application dependency boundary: PASS")
