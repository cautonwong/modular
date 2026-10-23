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
# The match is by path component, so it also skips a driver we vendored and do
# patch (`infra/imu/vendor/Fusion`): narrowing that needs a per-checker allow-list,
# not a shared helper.
VENDOR_PARTS = ("vendor",)


def is_vendored(path: Path) -> bool:
    return any(part in VENDOR_PARTS for part in path.parts)


def strip_c_comments(text: str) -> list:
    """Return the lines of C source with `/* ... */` and `// ...` removed.

    Line numbers are preserved, so a finding still points at the line the reader
    will open. A comment that mentions a call is prose, not code: without this a
    commented-out assembly line fails the composition-root gate (#262 finding 3).

    A quote is not a comment: `"http://host"` contains `//`, and a URL literal cut
    there takes the rest of its line with it - including a call the guard exists to
    find. So the scan tracks which quote it is inside and leaves those bytes alone.
    """
    lines = []
    in_block = False
    for line in text.splitlines():
        out = []
        index = 0
        quote = ""  # string or character literal open at this point; C cannot span lines
        while index < len(line):
            if in_block:
                end = line.find("*/", index)
                if end < 0:
                    index = len(line)
                    break
                in_block = False
                index = end + 2
                continue
            char = line[index]
            if quote:
                if char == "\\":
                    out.append(line[index:index + 2])
                    index += 2
                    continue
                if char == quote:
                    quote = ""
            elif char in "\"'":
                quote = char
            elif line.startswith("//", index):
                index = len(line)
                break
            elif line.startswith("/*", index):
                in_block = True
                index += 2
                continue
            out.append(char)
            index += 1
        lines.append("".join(out))
    return lines


def strip_cmake_comments(text: str) -> str:
    """Return CMake source with `#` comments removed, line numbering preserved."""
    return "\n".join(line.split("#", 1)[0] for line in text.splitlines())


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
