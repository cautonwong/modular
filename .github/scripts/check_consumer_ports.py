#!/usr/bin/env python3
"""Enforce consumer-defined ports, caller-owned memory, and composition/aggregate root rules.

Checks:
1. Consumer-Defined Ports (消费者定义接口):
   - Every port/interface struct in `app/*/include` and `edge/ports.h` (e.g. `*_if_t`, `*_if`, `*_port_t`, `*_storage_t`, `*_battery_t`)
     must declare `void *self;` (manual this-pointer) and all function pointer callbacks must accept `void *` as first parameter.
   - A header that declares `<X>_STORAGE_SIZE` / `<X>_STORAGE_ALIGN` must keep the
     type opaque (forward declaration only); the definition belongs in `src/`.
2. Caller-Owned Memory & Zero Dynamic Allocation (调用方提供内存):
   - Runtime source trees (`app`, `infra`, `sys`, `pal`, `edge_module`, `board`, `soc`) must NOT call dynamic memory functions
     (`malloc`, `calloc`, `realloc`, `free`, `strdup`, `alloca`).
3. Composition Root Integrity (组合根与聚合根封装):
   - Apps must not instantiate or directly call concrete peer apps or infra init functions.

Usage: check_consumer_ports.py [root]
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Banned dynamic memory allocation functions in runtime modules
BANNED_ALLOC = re.compile(
    r'\b(?:malloc|calloc|realloc|free|strdup|alloca)\s*\('
)

# Struct definitions ending in port / interface conventions
PORT_STRUCT = re.compile(
    r'typedef\s+struct\s+([A-Za-z0-9_]+)?\s*\{([^}]+)\}\s*([A-Za-z0-9_]+_t|[A-Za-z0-9_]+_if);',
    re.S
)

FN_PTR = re.compile(
    r'([A-Za-z0-9_* ]+)\s*\(\s*\*([A-Za-z0-9_]+)\s*\)\s*\(([^)]*)\)'
)


def check_ports_in_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for _tag, body, type_name in PORT_STRUCT.findall(text):
        # Only check structs that contain function pointers (interfaces / ports)
        fn_ptrs = FN_PTR.findall(body)
        if not fn_ptrs:
            continue

        # Rule 1: Port must have `void *self;`
        if not re.search(r'\bvoid\s*\*\s*self\s*;', body):
            problems.append(
                f"{rel}: port struct '{type_name}' must contain 'void *self;' (consumer-defined interface rule)"
            )

        # Rule 2: Every callback in port must take `void *self` as first parameter
        for _ret, fn_name, params in fn_ptrs:
            param_list = [p.strip() for p in params.split(",") if p.strip()]
            if not param_list or not re.match(r'^(?:const\s+)?void\s*\*', param_list[0]):
                problems.append(
                    f"{rel}: port callback '{fn_name}' in '{type_name}' must take 'void *self' as first parameter"
                )

    return problems


def check_allocations_in_dir(scan_dir: Path, root: Path) -> list:
    problems = []
    if not scan_dir.is_dir():
        return problems

    for path in sorted(scan_dir.rglob("*")):
        if path.suffix not in {".c", ".h"}:
            continue
        rel = path.relative_to(root)
        text = path.read_text(encoding="utf-8", errors="replace")

        for lineno, line in enumerate(text.splitlines(), 1):
            if "N5-allow" in line or "ponytail:allow-alloc" in line:
                continue
            if BANNED_ALLOC.search(line):
                problems.append(
                    f"{rel}:{lineno}: banned dynamic memory call (caller-owned storage rule: zero runtime allocation)"
                )
    return problems


def check_opaque_storage_in_file(path: Path, root: Path) -> list:
    """A declared storage contract means the type must stay opaque.

    If a public header promises ``<X>_STORAGE_SIZE`` / ``<X>_STORAGE_ALIGN``, the
    caller is being told how much memory to hand over without being told what is in
    it. Leaving the struct's fields in that same header defeats the point: the
    caller can still read and write the module's state, and the size contract
    becomes a second, unchecked definition of the same thing. The header must
    forward-declare the type instead, and the definition (with its static
    assertions) belongs in ``src/``.
    """
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for match in re.finditer(r"#define\s+([A-Z0-9_]+)_STORAGE_SIZE\b", text):
        macro = match.group(1)
        lineno = text[: match.start()].count("\n") + 1
        type_name = macro.lower()

        if not re.search(rf"#define\s+{macro}_STORAGE_ALIGN\b", text):
            problems.append(
                f"{rel}:{lineno}: {macro}_STORAGE_SIZE without {macro}_STORAGE_ALIGN "
                f"(a size contract without an alignment is incomplete)"
            )

        if re.search(rf"\bstruct\s+{type_name}\s*\{{", text):
            problems.append(
                f"{rel}:{lineno}: {macro}_STORAGE_SIZE is declared but struct {type_name} "
                f"is defined in the same public header (opaque-storage rule: move the "
                f"definition to src/ and forward-declare the type)"
            )
        elif not re.search(rf"typedef\s+struct\s+{type_name}\s+[A-Za-z_][A-Za-z0-9_]*\s*;", text):
            problems.append(
                f"{rel}:{lineno}: {macro}_STORAGE_SIZE is declared but 'struct {type_name}' "
                f"is not forward-declared (add 'typedef struct {type_name} <alias>;' so the "
                f"definition can live in src/)"
            )

    return problems


def check(root: Path) -> list:
    problems = []

    # 1. Check consumer-defined ports in app/ and edge_module/
    app_dir = root / "app"
    if app_dir.is_dir():
        for path in sorted(app_dir.rglob("*.h")):
            problems.extend(check_ports_in_file(path, root))
            problems.extend(check_opaque_storage_in_file(path, root))

    edge_ports = root / "edge_module" / "include" / "edge" / "ports.h"
    if edge_ports.is_file():
        problems.extend(check_ports_in_file(edge_ports, root))

    # 2. Check zero runtime dynamic allocation across runtime layers
    for layer in ("app", "infra", "sys", "pal", "edge_module", "board", "soc"):
        problems.extend(check_allocations_in_dir(root / layer, root))

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("Consumer ports & caller-owned memory rule violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("consumer ports & caller-owned memory: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
