#!/usr/bin/env python3
"""Enforce App Purity: Zero global mutable state and strict Clean Architecture isolation.

Rules:
1. Zero File-Scope Mutable State (聚合状态自治与无全局变量原则):
   All application mutable state must reside in the app instance struct (caller-owned).
   In `app/*/src/*.c`, file-scope static variables must be `const` (e.g., lookup tables,
   descriptors). Non-const static variables (e.g. `static uint32_t count;`, `static app_t *g_inst;`)
   are strictly forbidden because they destroy reentrancy, break multi-instance support,
   and hide state outside the aggregate root.

2. Zero Direct Platform/Hardware Header Infiltration (领域纯净度与零底层依赖):
   Application sources and headers must not directly include concrete hardware or
   platform layer headers (`soc/...`, `board/...`, `pal_rtos/...`, `pal_cortex_m/...`).
   All external dependencies must be abstracted behind consumer-defined ports.

Usage: check_app_purity.py [root]
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Detect static variable declarations:
# Matches file-scope lines containing 'static ' but NOT 'const ', NOT 'inline', NOT '(' (functions)
STATIC_DECL = re.compile(
    r'^\s*static\s+(?!const\b)(?:[a-zA-Z0-9_]+(?:\s*\*)?)\s+([a-zA-Z0-9_]+)\s*(?:\[[^\]]*\])?\s*(?:=[^;]+)?\s*;',
    re.M
)

BANNED_PLATFORM_INCLUDES = re.compile(
    r'^\s*#\s*include\s*[<"](soc/[^">]+|board/[^">]+|pal_rtos/[^">]+|pal_cortex_m/[^">]+)[">]',
    re.M
)


def check_file_scope_statics(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    lines = text.splitlines()
    brace_depth = 0
    in_multiline_comment = False

    for i, line in enumerate(lines, 1):
        clean_line = line.strip()
        # Track multiline comments
        if "/*" in clean_line and "*/" not in clean_line:
            in_multiline_comment = True
            continue
        if "*/" in clean_line:
            in_multiline_comment = False
            clean_line = clean_line.split("*/", 1)[-1].strip()

        if in_multiline_comment:
            continue

        # Strip line comments
        if "//" in clean_line:
            clean_line = clean_line.split("//", 1)[0].strip()

        # Update brace depth
        brace_depth += clean_line.count("{") - clean_line.count("}")

        # Check only file-scope declarations (brace_depth == 0)
        if brace_depth == 0 and "static " in clean_line and "const " not in clean_line:
            # Skip function declarations/definitions (contains parameter parentheses)
            if "(" in clean_line:
                continue
            # Skip inline functions or typedefs
            if "inline " in clean_line or clean_line.startswith("typedef "):
                continue

            m = STATIC_DECL.search(clean_line)
            if m:
                var_name = m.group(1)
                problems.append(
                    f"{rel}:{i}: non-const file-scope static variable '{var_name}' "
                    f"(app purity rule: mutable state must reside in instance struct, zero global variables)"
                )

    return problems


def check_banned_includes(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for m in BANNED_PLATFORM_INCLUDES.finditer(text):
        inc = m.group(1)
        lineno = text[: m.start()].count("\n") + 1
        problems.append(
            f"{rel}:{lineno}: forbidden platform/hardware include '{inc}' "
            f"(clean architecture rule: app domain must not depend directly on board, soc, or concrete pal)"
        )

    return problems


def check(root: Path) -> list:
    problems = []
    app_dir = root / "app"
    if not app_dir.is_dir():
        return problems

    for c_file in sorted(app_dir.glob("*/src/*.c")):
        problems.extend(check_file_scope_statics(c_file, root))
        problems.extend(check_banned_includes(c_file, root))

    for h_file in sorted(app_dir.glob("*/include/*/*.h")):
        problems.extend(check_banned_includes(h_file, root))

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("App purity and clean architecture violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("app purity & clean architecture: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
