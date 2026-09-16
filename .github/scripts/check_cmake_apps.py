#!/usr/bin/env python3
"""Verify product app lists are consistent across CMake and ``main()``.

For every ``edge_add_product(name ... apps ...)`` in the top-level
``CMakeLists.txt`` this checks that:

* the referenced ``app/<name>/`` directory exists,
* a matching ``add_library(app_<name> ...)`` target exists,
* the apps declared in CMake match the apps actually constructed in
  ``product/<name>/main.c`` (via ``<app>_construct(`` calls).

Usage: check_cmake_apps.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

PRODUCT_CALL = re.compile(r"^\s*edge_add_product\s*\((.*?)\)", re.S | re.M)
CONSTRUCT = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)_construct\s*\(")


def check(root: Path) -> list:
    cmake_path = root / "CMakeLists.txt"
    if not cmake_path.is_file():
        return [f"missing {cmake_path}"]
    cmake = cmake_path.read_text(encoding="utf-8")
    problems = []

    for body in PRODUCT_CALL.findall(cmake):
        tokens = body.split()
        if not tokens:
            continue
        name = tokens[0]
        if "apps" not in tokens:
            problems.append(f"edge_add_product({name}): missing 'apps'")
            continue
        apps = tokens[tokens.index("apps") + 1 :]
        if not apps:
            problems.append(f"edge_add_product({name}): empty app list")
            continue

        main_path = root / "product" / name / "main.c"
        if not main_path.is_file():
            problems.append(f"product/{name}/main.c is missing")
            continue

        for app in apps:
            if not (root / "app" / app).is_dir():
                problems.append(f"edge_add_product({name}): app/{app}/ is missing")
            if f"add_library(app_{app}" not in cmake:
                problems.append(f"edge_add_product({name}): no add_library(app_{app}) target")

        constructed = set(CONSTRUCT.findall(main_path.read_text(encoding="utf-8")))
        constructed_apps = {c for c in constructed if (root / "app" / c).is_dir()}
        declared_apps = set(apps)
        if declared_apps != constructed_apps:
            problems.append(
                f"product/{name}: CMake declares {sorted(declared_apps)} "
                f"but main.c constructs {sorted(constructed_apps)}"
            )

    if not PRODUCT_CALL.search(cmake):
        problems.append("no edge_add_product() call found in CMakeLists.txt")

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("CMake/main product consistency violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("CMake/main product consistency: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
