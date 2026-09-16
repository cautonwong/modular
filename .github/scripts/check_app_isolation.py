#!/usr/bin/env python3
"""Reject app modules that depend on concrete layers, other apps, or RTOSes.

Usage: check_app_isolation.py [root]

The optional ``root`` argument allows the guard self-test to point the checker
at fixtures (see ``tests/guards``).
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Concrete layer prefixes that must never appear in an app include path.
FORBIDDEN_LAYER_PREFIX = re.compile(r'^\s*#\s*include\s*[<"](?:infra|product|board|sys)/', re.M)
# RTOS/framework headers that the foreground/background model does not allow.
FORBIDDEN_RTOS = re.compile(
    r'^\s*#\s*include\s*[<"](?:FreeRTOS\.h|cmsis_os\.h|rtthread\.h|zephyr/|task\.h)',
    re.M,
)
# SoC/vendor headers would leak the platform into an app (N3).
FORBIDDEN_SOC = re.compile(
    r'^\s*#\s*include\s*[<"](?:soc/|cmsis|core_cm|stm32|gd32|nrf|rn8|mps2|hal/)', re.M
)
# Direct register access: a volatile pointer cast on a literal address (N3).
REGISTER_ACCESS = re.compile(
    r'\(\s*volatile\s+[A-Za-z_][A-Za-z0-9_]*\s*\*\s*\)\s*\(?\s*0[xX][0-9A-Fa-f]+'
)
# Heavy libc facilities an app must not depend on without a whitelist (N5).
HEAVY_LIBC = re.compile(
    r'\b(?:printf|sprintf|snprintf|vprintf|vfprintf|malloc|calloc|realloc|free|strdup|abort|exit)\s*\('
)
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)


def collect_public_roots(root: Path) -> set:
    """Public header roots exposed by concrete layers (e.g. ``flash``)."""
    roots = set()
    for pattern in ("infra/*/include/*", "board/*/include/*", "sys/*/include/*"):
        for incdir in root.glob(pattern):
            if incdir.is_dir():
                roots.add(incdir.name)
    return roots


def check(root: Path) -> list:
    app_root = root / "app"
    violations = []
    if not app_root.is_dir():
        return violations

    app_names = {p.name for p in app_root.iterdir() if p.is_dir()}
    forbidden_roots = collect_public_roots(root)

    for path in sorted(app_root.rglob("*")):
        if path.suffix not in {".c", ".h", ".cc", ".cpp"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(root)

        if FORBIDDEN_LAYER_PREFIX.search(text):
            violations.append(f"{rel}: includes a concrete infra/product/board/sys path")
        if FORBIDDEN_RTOS.search(text):
            violations.append(f"{rel}: includes a forbidden RTOS header")
        if FORBIDDEN_SOC.search(text):
            violations.append(f"{rel}: includes a SoC/vendor header")
        for lineno, line in enumerate(text.splitlines(), 1):
            if "N5-allow" in line:
                continue
            if REGISTER_ACCESS.search(line):
                violations.append(f"{rel}:{lineno}: app touches a raw register address")
            if HEAVY_LIBC.search(line):
                violations.append(f"{rel}:{lineno}: app uses a heavy libc call")

        owner = path.relative_to(app_root).parts[0]
        for include in INCLUDE.findall(text):
            segment = include.split("/", 1)[0]
            if segment in app_names and segment != owner:
                violations.append(f"{rel}: app '{owner}' includes app '{segment}' header")
            elif segment in forbidden_roots:
                violations.append(
                    f"{rel}: app '{owner}' includes concrete layer header '{include}'"
                )
    return violations


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    violations = check(root)
    if violations:
        print("Application dependency boundary violation:")
        for violation in violations:
            print(f"  {violation}")
        return 1
    print("Application dependency boundary: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
