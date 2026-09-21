#!/usr/bin/env python3
"""Every quality-gate exemption is registered, owned and has an exit condition.

`cmake/EdgeTargets.cmake` can declare a target `RAW`, which keeps it out of the
warning and static-analysis gate. That is the right tool for third-party code and
the wrong tool for anything else: an unregistered `RAW` target is a silent hole in
the quality gate, and a registered one whose directory no longer declares `RAW` is
decoration.

This checks in both directions:

* every ``edge_add_*(... RAW ...)`` call in the tree has an entry in
  ``ci/exemptions.json``;
* every entry names an existing directory that really does declare a ``RAW``
  target, and carries an owner, a reason, an exit condition and a ticket.

Usage: check_exemptions.py [root]
"""
from pathlib import Path
import json
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
REGISTER = "ci/exemptions.json"

# `tests/` holds the fixtures for this very check, so scanning it from the repo
# root would let the negative fixture fail the real repository.
SKIP_DIRS = {
    ".git",
    ".ccache",
    ".fetchcontent",
    "_deps",
    "tests",
    "build",
}

HELPERS = (
    "edge_add_app(",
    "edge_add_infra(",
    "edge_add_board(",
    "edge_add_soc(",
    "edge_add_sys(",
    "edge_add_pal(",
    "edge_add_layer_module(",
)

REQUIRED_FIELDS = ("target", "where", "owner", "reason", "exit", "issue")


def strip_comments(text: str) -> str:
    """Comments mention RAW (see cmake/EdgeTargets.cmake); only real calls count."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def raw_calls(text: str) -> list:
    """Names passed to a helper call whose arguments contain RAW as a bare word."""
    flat = strip_comments(text)
    names = []
    for helper in HELPERS:
        start = 0
        while True:
            at = flat.find(helper, start)
            if at < 0:
                break
            depth = 0
            end = at + len(helper) - 1
            while end < len(flat):
                if flat[end] == "(":
                    depth += 1
                elif flat[end] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                end += 1
            args = flat[at + len(helper):end].split()
            if args and "RAW" in args[1:]:
                names.append(args[0])
            start = at + 1
    return names


def check(root: Path) -> tuple:
    root = root.resolve()
    register_path = root / REGISTER
    if not register_path.is_file():
        return (1, [f"missing {REGISTER}"])

    try:
        register = json.loads(register_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return (1, [f"{REGISTER}: invalid JSON: {exc}"])

    entries = register.get("exemptions", [])
    problems = []
    by_where = {}
    for index, entry in enumerate(entries):
        missing = [field for field in REQUIRED_FIELDS if not str(entry.get(field, "")).strip()]
        if missing:
            problems.append(f"{REGISTER} entry {index}: missing {', '.join(missing)}")
            continue
        where = entry["where"]
        if not (root / where).is_dir():
            problems.append(
                f"{REGISTER} entry {index} ({entry['target']}): '{where}' does not exist "
                "(a stale exemption is worse than none: it hides the next RAW target)"
            )
            continue
        by_where.setdefault(where, []).append(entry)

    declared = {}
    for path in sorted(root.rglob("CMakeLists.txt")):
        relative = path.relative_to(root)
        if any(part in SKIP_DIRS for part in relative.parts[:-1]):
            continue
        names = raw_calls(path.read_text(encoding="utf-8", errors="replace"))
        if names:
            declared[str(relative.parent)] = sorted(set(names))

    for where, names in declared.items():
        if where not in by_where:
            problems.append(
                f"{where}: {'/'.join(names)} is declared RAW but has no entry in {REGISTER}"
            )
    for where, matching in by_where.items():
        if where not in declared:
            problems.append(
                f"{REGISTER}: '{where}' is registered but that directory declares no RAW "
                "target; remove the entry or restore the exemption's reason"
            )

    if problems:
        return (1, problems)
    registered = ", ".join(sorted(f"{e['target']} ({e['where']})" for group in by_where.values() for e in group))
    return (0, [f"exemption register: PASS ({len(entries)} registered: {registered or 'none'})"])


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    stream = sys.stdout if code == 0 else sys.stderr
    for message in messages:
        print(message, file=stream)
    return code


if __name__ == "__main__":
    sys.exit(main())
