#!/usr/bin/env python3
"""Enforce the D51 module contract: the framework struct carries lifecycle
callbacks only.

`init` / `deinit` must NOT be part of `edge_module_t` -- the composition root
calls `<app>_init(self, deps)` / `<app>_deinit(self)` explicitly at assembly and
shutdown time (ADR 17.11 / 20.3.5). Everything else in the struct is scheduler
bookkeeping or state, so this gate pins the callback set exactly.

Usage: check_module_contract.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
HEADER = "edge_module/include/edge/module.h"
STRUCT = re.compile(r"typedef struct edge_module\s*\{(.*?)\}\s*edge_module_t;", re.S)
CALLBACK_FIELD = re.compile(r"^\s*edge_module_([a-z_]+)_fn\s+([a-z_]+)\s*;", re.M)

EXPECTED = {"poll", "on_event", "power_off", "suspend", "resume"}
FORBIDDEN = {"init", "deinit"}


def check(root: Path) -> tuple:
    header = root / HEADER
    if not header.is_file():
        return (1, [f"missing module contract header: {HEADER}"])

    match = STRUCT.search(header.read_text(encoding="utf-8"))
    if not match:
        return (1, [f"{HEADER}: could not locate the edge_module_t definition"])

    fields = {field for _kind, field in CALLBACK_FIELD.findall(match.group(1))}
    problems = []

    for name in sorted(FORBIDDEN & fields):
        problems.append(
            f"{HEADER}: '{name}' must not be an edge_module_t callback "
            f"(D51: the composition root calls <app>_{name}())"
        )
    missing = sorted(EXPECTED - fields)
    if missing:
        problems.append(f"{HEADER}: missing lifecycle callback(s) {missing}")
    unexpected = sorted(fields - EXPECTED - FORBIDDEN)
    if unexpected:
        problems.append(f"{HEADER}: unexpected callback(s) {unexpected} in edge_module_t")

    if problems:
        return (1, problems)
    return (0, [f"module contract: PASS (callbacks: {', '.join(sorted(fields))})"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    if code != 0:
        print("Module contract violation:")
        for message in messages:
            print(f"  {message}")
        return code
    print(messages[0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
