#!/usr/bin/env python3
"""Every fetched third-party component is pinned to an immutable revision (#89, D38).

A git tag can be moved, so a tag is not a pin. `ci/dependencies.json` records each
fetched dependency with the tag (for humans) and the commit (the truth), and this
check makes that record load-bearing:

* **the manifest is well formed** - every entry names a 40-hex commit, a tag, a url,
  a licence, the module that uses it, and that module exists;
* **the build agrees with it** - the CMake variable the entry names must carry
  exactly that commit, so the pin cannot drift between the manifest and the build
  (two records that disagree are worse than one);
* **nothing is declared twice**, and the manifest is not empty (an empty record is
  how "we have no dependencies" quietly becomes "we stopped looking").

The revision is *also* verified where it is fetched: `pal/rtos/freertos` compares
the resolved checkout against the same variable at configure time, which is what
catches a tag that moved after the fact.

Usage: check_dependencies.py [root]
"""
from pathlib import Path
import json
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
MANIFEST = "ci/dependencies.json"
REQUIRED = ("name", "version", "commit", "url", "licence", "used_by", "pin_variable", "pin_file")
SHA = re.compile(r"^[0-9a-f]{40}$")


def check(root: Path) -> tuple:
    root = root.resolve()
    path = root / MANIFEST
    if not path.is_file():
        return (1, [f"missing {MANIFEST}"])

    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        return (1, [f"{MANIFEST}: invalid JSON: {exc}"])

    entries = document.get("dependencies", [])
    problems = []
    if not entries:
        problems.append(
            f"{MANIFEST} declares no dependency; if that is true, say so explicitly - "
            "an empty record and a forgotten record look the same to every reader"
        )

    seen = set()
    for index, entry in enumerate(entries):
        missing = [field for field in REQUIRED if not str(entry.get(field, "")).strip()]
        if missing:
            problems.append(f"{MANIFEST} entry {index}: missing {', '.join(missing)}")
            continue

        name = entry["name"]
        if name in seen:
            problems.append(f"{name}: declared twice")
        seen.add(name)

        if not SHA.match(entry["commit"]):
            problems.append(
                f"{name}: commit '{entry['commit']}' is not a 40-hex sha - a tag is not a pin"
            )
        if not (root / entry["used_by"]).is_dir():
            problems.append(f"{name}: used_by '{entry['used_by']}' is not a directory")

        pin_file = root / entry["pin_file"]
        if not pin_file.is_file():
            problems.append(f"{name}: pin_file '{entry['pin_file']}' does not exist")
            continue
        expected = f'set({entry["pin_variable"]} "{entry["commit"]}"'
        if expected not in pin_file.read_text(encoding="utf-8", errors="replace"):
            problems.append(
                f"{name}: {entry['pin_file']} does not carry "
                f'{entry["pin_variable"]} "{entry["commit"]}" - the manifest and the '
                "build disagree about what is pinned"
            )

    if problems:
        return (1, problems)
    names = ", ".join(f"{e['name']}@{e['version']}" for e in entries)
    return (0, [f"dependency pins: PASS ({len(entries)} declared: {names})"])


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    stream = sys.stdout if code == 0 else sys.stderr
    for message in messages:
        print(message, file=stream)
    return code


if __name__ == "__main__":
    sys.exit(main())
