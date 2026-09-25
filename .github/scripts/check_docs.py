#!/usr/bin/env python3
"""Check markdown documents for format health and dead relative links.

Rules (all of them are already true of a healthy doc, so the gate cannot drift
from the written convention):

* every relative link target resolves to an existing file;
* no trailing whitespace;
* the file ends with exactly one newline.

Fenced code blocks are stripped before link scanning, so code samples never
produce false positives. The guard fixtures under ``tests/guards/`` are excluded.

Usage: check_docs.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
# tests/guards holds deliberate negative fixtures; .pi holds agent goal state,
# which is not repository documentation and is written by tooling we do not own.
EXCLUDED_PARTS = ("tests/guards", ".pi")
FENCE = re.compile(r"^```.*?^```", re.M | re.S)
LINK = re.compile(r"\[[^\]]*\]\(\s*<?([^)\s>]+)>?[^)]*\)")
EXTERNAL = ("http://", "https://", "mailto:", "tel:", "#")


def strip_fences(text: str) -> str:
    return FENCE.sub("", text)


def check(root: Path) -> tuple:
    root = root.resolve()
    problems = []
    documents = sorted(
        p
        for p in root.rglob("*.md")
        if not p.relative_to(root).as_posix().startswith(EXCLUDED_PARTS)
    )
    if not documents:
        return (1, ["no markdown documents found"])

    for path in documents:
        rel = path.relative_to(root)
        text = path.read_text(encoding="utf-8", errors="replace")

        if text and not text.endswith("\n"):
            problems.append(f"{rel}: missing final newline")
        if text.endswith("\n\n"):
            problems.append(f"{rel}: trailing blank line at end of file")

        for number, line in enumerate(text.splitlines(), 1):
            if line != line.rstrip():
                problems.append(f"{rel}:{number}: trailing whitespace")

        for number, line in enumerate(strip_fences(text).splitlines(), 1):
            for target in LINK.findall(line):
                if target.startswith(EXTERNAL):
                    continue
                local = target.split("#", 1)[0]
                if not local:
                    continue
                if not (path.parent / local).exists():
                    problems.append(f"{rel}:{number}: dead link '{target}'")

    if problems:
        return (1, problems)
    return (0, [f"docs check: PASS ({len(documents)} documents)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    if code != 0:
        print("Documentation check violation:")
        for message in messages:
            print(f"  {message}")
        return code
    print(messages[0])
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
