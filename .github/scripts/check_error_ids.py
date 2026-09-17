#!/usr/bin/env python3
"""Detect duplicate or out-of-range error codes (D19/D68).

Mirrors ``check_event_ids.py`` / ``check_module_ids.py`` for the error model:

* framework ``edge_status_t`` members must be unique, non-positive and inside the
  reserved framework range ``-99 .. 0``;
* ``EDGE_ERR(<segment>, <code>)`` usages must not land in the framework range
  (a zero segment collides) and must not collide with each other.

Usage: check_error_ids.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
FRAMEWORK_MIN = -99
FRAMEWORK_MAX = 0

ENUM_BODY = re.compile(r"edge_status\s*\{([^}]*)\}", re.S)
ENUM_MEMBER = re.compile(r"(EDGE_[A-Z0-9_]+)\s*=\s*(-?\d+)")
MOD_MEMBER = re.compile(r"^#define\s+(EDGE_MOD_[A-Z0-9_]+)\s+(0[xX][0-9A-Fa-f]+|\d+)u?\s*$", re.M)
ERR_USE = re.compile(r"EDGE_ERR\(\s*([A-Za-z0-9_]+)\s*,\s*([A-Za-z0-9_]+)\s*\)")


def _int(token):
    try:
        return int(token.strip().rstrip("uUlL"), 0)
    except ValueError:
        return None


def check(root: Path) -> tuple:
    errors_path = root / "edge_module/include/edge/errors.h"
    if not errors_path.is_file():
        return (1, [f"missing central error table: {errors_path}"])

    framework = {}
    problems = []
    body = ENUM_BODY.search(errors_path.read_text(encoding="utf-8"))
    for name, value in (ENUM_MEMBER.findall(body.group(1)) if body else []):
        number = int(value)
        if name == "EDGE_OK":
            if number != 0:
                problems.append(f"EDGE_OK must be 0, got {number}")
            continue
        if number >= FRAMEWORK_MAX:
            problems.append(f"{name} = {number} must be negative")
        if number < FRAMEWORK_MIN:
            problems.append(f"{name} = {number} escapes the framework range (> {FRAMEWORK_MIN})")
        if number in framework:
            problems.append(f"error collision: {name} and {framework[number]} = {number}")
        else:
            framework[number] = name

    modules_path = root / "edge_module/include/edge/modules.h"
    modules = {}
    if modules_path.is_file():
        modules = {n: int(v, 0) for n, v in MOD_MEMBER.findall(modules_path.read_text(encoding="utf-8"))}

    usages = {}
    for layer in ("edge_module", "app", "sys", "board", "infra", "product", "pal"):
        base = root / layer
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in {".c", ".h"} or path.name in {"errors.h", "modules.h"}:
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            for line in text.splitlines():
                # Only `#define` lines allocate a code; expressions merely use one.
                if "EDGE_ERR(" not in line or not line.lstrip().startswith("#define"):
                    continue
                match = ERR_USE.search(line)
                if not match:
                    continue
                segment_token, code_token = match.group(1), match.group(2)
                segment = _int(segment_token)
                if segment is None:
                    segment = modules.get(segment_token)
                code = _int(code_token)
                if segment is None or code is None:
                    continue  # unresolvable token: a macro we do not track
                if segment & 0xFF00 == 0:
                    problems.append(
                        f"{path.relative_to(root)}: EDGE_ERR segment 0x{segment:04x} collides with the framework range"
                    )
                    continue
                value = -((segment & 0xFF00) | (code & 0xFF))
                if value in framework:
                    problems.append(
                        f"{path.relative_to(root)}: EDGE_ERR(0x{segment:04x}, {code}) = {value} "
                        f"collides with {framework[value]}"
                    )
                elif value in usages:
                    problems.append(
                        f"{path.relative_to(root)}: EDGE_ERR(0x{segment:04x}, {code}) = {value} "
                        f"collides with {usages[value]}"
                    )
                else:
                    usages[value] = f"EDGE_ERR(0x{segment:04x}, {code})"

    if problems:
        return (1, problems)
    return (0, [f"error ID check: PASS ({len(framework)} framework, {len(usages)} module codes)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
