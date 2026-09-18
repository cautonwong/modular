#!/usr/bin/env python3
"""Every checker must ship a positive AND a negative fixture (ticket #40).

Reads the CASES table in ``tests/guards/run_guard_selftest.py`` and asserts:

* every ``.github/scripts/check_*.py`` appears there with at least one
  ``expect_pass=True`` case and one ``expect_pass=False`` case;
* every fixture a case references actually exists (``{root}`` is expanded).

This turns "a new guard must come with a refutation test" from prose into a gate,
so the coverage gap this ticket closed cannot reopen.

Usage: check_guard_coverage.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
SELFTEST = "tests/guards/run_guard_selftest.py"
SELFTEST_DIR = "tests/guards"
CASE = re.compile(r'\(\s*"(check_[a-z_]+\.py)"\s*,\s*"([^"]+)"\s*,\s*(True|False)')


def check(root: Path) -> tuple:
    root = root.resolve()
    selftest = root / SELFTEST
    if not selftest.is_file():
        return (1, [f"missing self-test entry point: {SELFTEST}"])

    cases = CASE.findall(selftest.read_text(encoding="utf-8"))
    if not cases:
        return (1, [f"no checker cases found in {SELFTEST}"])

    checkers = sorted(p.name for p in (root / ".github" / "scripts").glob("check_*.py"))
    problems = []

    for checker in checkers:
        results = [flag for name, _fixture, flag in cases if name == checker]
        if not results:
            problems.append(f"{checker}: no self-test case at all")
            continue
        if "True" not in results:
            problems.append(f"{checker}: no positive fixture (expect_pass=True)")
        if "False" not in results:
            problems.append(f"{checker}: no negative fixture (expect_pass=False)")

    for name, fixture, _flag in cases:
        if name not in checkers:
            problems.append(f"{name}: listed in the self-test but not a checker script")
        target = root / fixture.replace("{root}", str(root))
        if not target.exists():
            problems.append(f"{name}: fixture '{fixture}' does not exist")

    if problems:
        return (1, problems)
    return (0, [f"guard coverage: PASS ({len(checkers)} checkers, {len(cases)} cases)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    if code != 0:
        print("Guard coverage violation:")
        for message in messages:
            print(f"  {message}")
        return code
    print(messages[0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
