#!/usr/bin/env python3
"""Verify every area directory declares its own build target (D88).

Before D88 every ``app``, ``infra``, ``board``, ``soc``, ``sys`` and ``pal``
target was declared in the top-level ``CMakeLists.txt``, so five areas could not
work in parallel without editing the same lines. The directories now declare
themselves and the top-level file discovers them.

Implicit discovery needs an explicit guard, otherwise "the target exists but is
never built" and "the registry key does not match the directory" become silent.
This checks:

* every directory under an area that holds sources has a ``CMakeLists.txt``;
* that file declares the module through its area helper, so the shape comes from
  ``cmake/EdgeTargets.cmake`` and not from hand-written ``add_library`` calls;
* for the areas a product refers to by name (app, infra, board, soc, sys) the
  helper is called with the directory name, because ``edge_add_product(... apps
  <name>)`` resolves ``EDGE_APP_TARGET_<name>`` and a mismatch would silently
  find nothing. ``pal`` is exempt: ``pal/rtos/freertos`` declares
  ``pal_rtos_freertos``.

Usage: check_area_registration.py [root]
"""
from pathlib import Path
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

AREAS = {
    "app": "edge_add_app",
    "infra": "edge_add_infra",
    "board": "edge_add_board",
    "soc": "edge_add_soc",
    "sys": "edge_add_sys",
    "pal": "edge_add_pal",
}
NAME_MUST_MATCH = {"app", "infra", "board", "soc", "sys"}
FOUNDATION_NAME = "edge_module"
FOUNDATION_HELPER = "edge_add_foundation"


def _is_module_root(directory: Path, area_dir: Path) -> bool:
    """A directory that owns a module, as opposed to a module's own src/ tree.

    Modules keep their sources in ``src/`` and headers in ``include/``. A family
    may instead keep its single source flat in the area directory
    (``sys/example/sys_example.c``), so a direct child of the area counts too.
    """
    if (directory / "src").is_dir() or (directory / "include").is_dir():
        return True
    return directory.parent == area_dir and any(directory.glob("*.c"))


def check(root: Path) -> list:
    problems = []

    foundation = root / FOUNDATION_NAME / "CMakeLists.txt"
    if not foundation.is_file():
        problems.append(f"{FOUNDATION_NAME}/CMakeLists.txt is missing")
    elif f"{FOUNDATION_HELPER}(" not in foundation.read_text(encoding="utf-8", errors="replace"):
        problems.append(f"{FOUNDATION_NAME}/CMakeLists.txt does not call {FOUNDATION_HELPER}()")

    for area, helper in sorted(AREAS.items()):
        area_dir = root / area
        if not area_dir.is_dir():
            continue
        for directory in sorted(path for path in area_dir.rglob("*") if path.is_dir()):
            if not _is_module_root(directory, area_dir):
                continue
            cmake = directory / "CMakeLists.txt"
            relative = directory.relative_to(root).as_posix()
            if not cmake.is_file():
                problems.append(
                    f"{relative}/ is a module but has no CMakeLists.txt: "
                    f"declare it with {helper}() (D88)"
                )
                continue
            text = cmake.read_text(encoding="utf-8", errors="replace").replace(" ", "")
            if f"{helper}(" not in text:
                problems.append(
                    f"{relative}/CMakeLists.txt does not call {helper}(): the module shape "
                    "comes from cmake/EdgeTargets.cmake, not from hand-written add_library()"
                )
                continue
            if area in NAME_MUST_MATCH and f"{helper}({directory.name}" not in text:
                problems.append(
                    f"{relative}/CMakeLists.txt must declare {helper}({directory.name} ...) so the "
                    "registry key matches the directory name"
                )

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("area registration violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("area registration: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
