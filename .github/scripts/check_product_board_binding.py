#!/usr/bin/env python3
"""Enforce D86: each product is registered once and bound to exactly one board.

Parses the top-level ``CMakeLists.txt`` (product calls, the legal family/board
list) and the area ``CMakeLists.txt`` files (which boards and families exist) and
checks that:

* every ``edge_add_product(<name> ... family <F> board <B> ...)`` is registered
  exactly once (a product name may not be re-registered or re-bound);
* ``<F>`` and ``<B>`` have a target registry entry;
* ``<F>:<B>`` is listed in ``EDGE_LEGAL_FAMILY_BOARD``;
* ``product/<name>/main.c`` exists;
* every ``edge_add_*_firmware(<target> <product> ...)`` names a registered product.

``edge_add_minimal_variant(<name> <board> <runner> <app>)`` is the T7b neutrality
fixture: it is a test artifact, not a product, and is exempt from D86.

Usage: check_product_board_binding.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Only match real call sites: a call starts its own line. This keeps the
# `message(FATAL_ERROR "edge_add_product(...)...")` diagnostics out.
PRODUCT_CALL = re.compile(r"^\s*edge_add_product\(\s*([^\s)]+)([^)]*)\)", re.M)
FIRMWARE_CALL = re.compile(r"^\s*edge_add_(\w+)_firmware\(\s*([^\s)]+)\s+([^\s)]+)", re.M)
FAMILY_KW = re.compile(r"\bfamily\s+(\S+)")
BOARD_KW = re.compile(r"\bboard\s+(\S+)")
LEGAL_LIST = re.compile(r"set\(\s*EDGE_LEGAL_FAMILY_BOARD\s+\"([^\"]*)\"")


def declared_modules(root: Path, area: str, helper: str, keyword: str = "") -> set:
    """Modules of an area that declare themselves through ``helper`` (D88).

    The registry lives in the area directory now, so a new board is picked up by
    creating ``board/<name>/CMakeLists.txt``; nothing central lists it.
    """
    names = set()
    area_dir = root / area
    if not area_dir.is_dir():
        return names
    for cmake in sorted(area_dir.glob("*/CMakeLists.txt")):
        text = cmake.read_text(encoding="utf-8", errors="replace")
        if f"{helper}(" in text and (not keyword or keyword in text):
            names.add(cmake.parent.name)
    return names


def check(root: Path) -> list:
    cmake_path = root / "CMakeLists.txt"
    if not cmake_path.is_file():
        return [f"missing {cmake_path}"]
    cmake = cmake_path.read_text(encoding="utf-8")

    legal = LEGAL_LIST.search(cmake)
    legal_pairs = set(legal.group(1).split(";")) if legal else set()
    if not legal_pairs:
        return ["CMakeLists.txt: EDGE_LEGAL_FAMILY_BOARD is missing or empty"]

    boards = declared_modules(root, "board", "edge_add_board")
    families = declared_modules(root, "sys", "edge_add_sys", keyword="FAMILY")

    problems = []
    registered = {}

    for name, rest in PRODUCT_CALL.findall(cmake):
        family = FAMILY_KW.search(rest)
        board = BOARD_KW.search(rest)
        if not family or not board:
            problems.append(f"edge_add_product({name}): missing 'family' or 'board'")
            continue
        family, board = family.group(1), board.group(1)

        if name in registered:
            problems.append(
                f"product '{name}' registered more than once "
                f"(first: board '{registered[name]}', again: board '{board}')"
            )
            continue
        registered[name] = board

        if family not in families:
            problems.append(f"edge_add_product({name}): unknown family '{family}'")
        if board not in boards:
            problems.append(f"edge_add_product({name}): unknown board '{board}'")
        if f"{family}:{board}" not in legal_pairs:
            problems.append(
                f"edge_add_product({name}): illegal family/board pair '{family}:{board}'"
            )
        if not (root / "product" / name / "main.c").is_file():
            problems.append(f"product/{name}/main.c is missing")

    for _kind, target, product in FIRMWARE_CALL.findall(cmake):
        if product not in registered:
            problems.append(
                f"firmware '{target}': product '{product}' is not registered by edge_add_product()"
            )

    if not registered:
        problems.append("no edge_add_product() call found in CMakeLists.txt")

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("Product-to-board binding violation (D86):")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("product-to-board binding: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
