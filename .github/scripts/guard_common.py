#!/usr/bin/env python3
"""Shared helpers for the architecture guards (#262 finding 9).

Two things were copied into several checkers and drifted apart under the same name:
one function called `strip_comments` removed C comments and returned lines, another
removed CMake `#` comments and returned text. The names below say which language
they belong to, so a reader who greps for one cannot land in the other.

Importing this module also gives a checker the repository root without repeating
`Path(__file__).resolve().parents[2]` in every script.
"""
from pathlib import Path

# This file lives in <repo>/.github/scripts/, so the repository root is two up.
REPO_ROOT = Path(__file__).resolve().parents[2]

# Third-party trees are checked in but not owned here: a guard must not turn red
# because an upstream header happens to match one of our shapes (#262 finding 4).
VENDOR_PARTS = ("vendor",)


def is_vendored(path: Path) -> bool:
    return any(part in VENDOR_PARTS for part in path.parts)


def strip_c_comments(text: str) -> list:
    """Return the lines of C source with `/* ... */` and `// ...` removed.

    Line numbers are preserved, so a finding still points at the line the reader
    will open. A comment that mentions a call is prose, not code: without this a
    commented-out assembly line fails the composition-root gate (#262 finding 3).
    """
    lines = []
    in_block = False
    for line in text.splitlines():
        out = []
        index = 0
        while index < len(line):
            if in_block:
                end = line.find("*/", index)
                if end < 0:
                    index = len(line)
                    break
                in_block = False
                index = end + 2
                continue
            start = line.find("/*", index)
            slash = line.find("//", index)
            if slash >= 0 and (start < 0 or slash < start):
                out.append(line[index:slash])
                index = len(line)
                break
            if start < 0:
                out.append(line[index:])
                index = len(line)
                break
            out.append(line[index:start])
            in_block = True
            index = start + 2
        lines.append("".join(out))
    return lines


def strip_cmake_comments(text: str) -> str:
    """Return CMake source with `#` comments removed, line numbering preserved."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


def iter_files(root: Path, pattern: str) -> list:
    """Sorted files under `root` matching `pattern`, third-party trees excluded."""
    return [path for path in sorted(root.rglob(pattern)) if not is_vendored(path)]


def split_top_level(text: str) -> list:
    """Split a parameter or argument list on commas that sit at nesting depth 0.

    `foo(a, bar(b, c), NULL)` is three arguments, not four; a plain `.split(",")`
    shifts the index of everything after a nested call (#252).
    """
    parts, current, depth = [], [], 0
    for char in text:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth = max(0, depth - 1)
        if char == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    tail = "".join(current)
    if tail.strip():
        parts.append(tail)
    return parts
