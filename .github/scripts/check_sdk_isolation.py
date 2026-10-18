#!/usr/bin/env python3
"""Enforce Vendor SDK and Precompiled Library Isolation (Issue #149, D44/D49).

Rules:
1. Silicon Vendor & Platform SDK Header Sandbox (厂商SDK与硬件头文件沙盒隔离):
   Vendor SDK headers (e.g. `stm32*.h`, `nrf*.h`, `pico/*.h`, `hardware/*.h`, `sam*.h`,
   `fsl_*.h`, `core_cm*.h`, `cmsis_*.h`, `gd32*.h`, `ch.h`, `hal.h`) must NEVER be directly
   included in pure architectural layers:
   - `app/` (domain business state machines)
   - `infra/` (reusable peripheral drivers)
   - `edge_module/` (core framework primitives)
   - `sys/` (system scheduling policies)
   - Any consumer-defined port header (`include/*/ports.h`)
   All hardware and vendor SDK headers must be strictly confined to `soc/<soc>/` and `pal/<name>/`.

2. Precompiled Library Sandboxing (预编译二进制库沙盒化):
   Precompiled binary archives (`.a`, `.lib`, `.so`, `.dylib`) cannot be directly linked
   into `app` or `infra` CMake targets. Vendor binaries must be wrapped inside `soc` or `pal`.

Usage: check_sdk_isolation.py [root]
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

BANNED_VENDOR_HEADERS = re.compile(
    r'^\s*#\s*include\s*[<"](?:'
    r'stm32[a-zA-Z0-9_]*\.h|'
    r'nrf[a-zA-Z0-9_]*\.h|'
    r'nrfx[a-zA-Z0-9_]*\.h|'
    r'pico/[a-zA-Z0-9_]+\.h|'
    r'hardware/[a-zA-Z0-9_]+\.h|'
    r'sam[a-zA-Z0-9_]*\.h|'
    r'fsl_[a-zA-Z0-9_]+\.h|'
    r'core_cm[a-zA-Z0-9_]*\.h|'
    r'cmsis_[a-zA-Z0-9_]*\.h|'
    r'gd32[a-zA-Z0-9_]*\.h|'
    r'ch\.h|hal\.h'
    r')[">]',
    re.M,
)

PRECOMPILED_LIB = re.compile(
    r'\.(?:a|lib|so|dylib)\b',
    re.I,
)


def check_source_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for match in BANNED_VENDOR_HEADERS.finditer(text):
        line = text[: match.start()].count("\n") + 1
        header = match.group(0).strip()
        problems.append(
            f"{rel}:{line}: banned vendor SDK header '{header}' in pure layer "
            f"(Issue #149: vendor SDKs must be sandboxed inside soc/ or pal/)"
        )
    return problems


def check_cmake_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for match in PRECOMPILED_LIB.finditer(text):
        line = text[: match.start()].count("\n") + 1
        problems.append(
            f"{rel}:{line}: direct reference to precompiled binary library in pure layer "
            f"(Issue #149: vendor precompiled libraries must be wrapped in soc/ or pal/)"
        )
    return problems


def check(root: Path) -> list:
    root = root.resolve()
    problems = []

    # Pure layers where vendor SDK headers are forbidden
    pure_layers = ("app", "infra", "sys", "edge_module")
    for layer in pure_layers:
        layer_dir = root / layer
        if not layer_dir.is_dir():
            continue
        for src_path in layer_dir.rglob("*"):
            if src_path.is_file() and src_path.suffix in (".c", ".h"):
                problems.extend(check_source_file(src_path, root))
            elif src_path.is_file() and src_path.name == "CMakeLists.txt":
                problems.extend(check_cmake_file(src_path, root))

    # Also verify all consumer port headers in any layer
    for port_header in root.glob("**/include/**/ports.h"):
        if port_header.is_file():
            problems.extend(check_source_file(port_header, root))

    return problems


def main() -> int:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print(f"vendor SDK isolation: FAIL ({len(problems)} problem(s)):", file=sys.stderr)
        for p in problems:
            print(f"  {p}", file=sys.stderr)
        return 1
    print("vendor SDK isolation: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
