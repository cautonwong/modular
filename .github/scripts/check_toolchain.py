#!/usr/bin/env python3
"""Check the tools present on this host against the toolchain manifest (D38).

`ci/toolchain.json` records what CI runs. This script makes that record
executable: a tool that is **present** must not be older than its declared
minimum, so a runner-image change or a stale local install fails loudly instead
of producing a confusing failure somewhere downstream (clang-format is the one
that has actually bitten: formatting with 14 and checking with 18 disagrees).

A tool that is **absent** is skipped, not failed: a host job has no ARM toolchain
and a plain checkout has no clang-tidy. The point is to catch drift in what is
installed, not to demand a full toolchain everywhere.

Usage: check_toolchain.py [root] [--manifest ci/toolchain.json]
"""
from pathlib import Path
import argparse
import json
import re
import shutil
import subprocess
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_MANIFEST = "ci/toolchain.json"
VERSION = re.compile(r"(\d+)\.(\d+)(?:\.(\d+))?")

COMMANDS = {
    "gcc": ["gcc", "-dumpfullversion"],
    "cmake": ["cmake", "--version"],
    "ninja": ["ninja", "--version"],
    "clang": ["clang", "--version"],
    "clang-format": ["clang-format", "--version"],
    "clang-tidy": ["clang-tidy", "--version"],
    "cppcheck": ["cppcheck", "--version"],
    "python3": ["python3", "--version"],
    "arm-none-eabi-gcc": ["arm-none-eabi-gcc", "-dumpfullversion"],
    "qemu-system-arm": ["qemu-system-arm", "--version"],
}


def parse(text: str):
    match = VERSION.search(text)
    if not match:
        return None
    return tuple(int(part) if part else 0 for part in match.groups())


def measure(tool: str):
    command = COMMANDS.get(tool)
    if command is None:
        return None, "no probe defined"
    if shutil.which(command[0]) is None:
        return None, "not installed"
    try:
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError) as exc:
        return None, f"probe failed: {exc}"
    output = f"{result.stdout}\n{result.stderr}"
    return parse(output), output.strip().splitlines()[0] if output.strip() else ""


def check(manifest_path: Path) -> tuple:
    if not manifest_path.is_file():
        return (1, [f"missing toolchain manifest: {manifest_path}"])
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))

    rows = []
    problems = []
    for tool, spec in sorted(manifest.get("tools", {}).items()):
        found, detail = measure(tool)
        minimum = parse(spec["minimum"])
        if found is None:
            rows.append(f"  skip  {tool:<20} {detail}")
            continue
        state = "ok" if found >= minimum else "TOO OLD"
        rows.append(f"  {state:<7} {tool:<20} found {'.'.join(map(str, found))} "
                    f"(minimum {spec['minimum']})")
        if found < minimum:
            problems.append(
                f"{tool}: {'.'.join(map(str, found))} < required {spec['minimum']}"
            )

    print(f"toolchain manifest: {manifest_path}")
    print(f"declared runner: {manifest.get('runs_on', 'unknown')}")
    print("\n".join(rows))

    if problems:
        return (1, problems)
    return (0, [f"toolchain check: PASS ({len(manifest.get('tools', {}))} tools declared)"])


def main(argv) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("root", nargs="?", default=str(DEFAULT_ROOT))
    parser.add_argument("--manifest", default=DEFAULT_MANIFEST)
    args = parser.parse_args(argv[1:])

    root = Path(args.root).resolve()
    manifest = Path(args.manifest)
    if not manifest.is_absolute():
        manifest = root / manifest

    code, messages = check(manifest)
    if code != 0:
        print("Toolchain drift:")
        for message in messages:
            print(f"  {message}")
        return code
    print(messages[0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
