#!/usr/bin/env python3
"""T7a: an app's transitive include closure must stay inside app/<name> + edge_module.

Resolves quoted includes through the app's own public include dir and
`edge_module/include`, then rejects any header in that closure that references a
concrete layer (board/infra/sys/soc/pal/product), another app, an RTOS, or a SoC
vendor header. This catches leakage the direct-include guard cannot see.

Usage: check_app_transitive_includes.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)
FORBIDDEN_LAYER = re.compile(r'^(?:board|infra|sys|soc|pal|product)/')
FORBIDDEN_RTOS = re.compile(r'^(?:FreeRTOS\.h|cmsis_os\.h|rtthread\.h)$|^zephyr/|^task\.h$')
FORBIDDEN_SOC = re.compile(r'^(?:cmsis|core_cm|stm32|gd32|nrf|rn8|mps2|hal/)')


def _resolve(include: str, base: Path, roots) -> Path | None:
    for root in (base, *roots):
        candidate = root / include
        if candidate.is_file():
            return candidate.resolve()
    return None


def _closure(starts, roots) -> set:
    seen = set()
    stack = [path.resolve() for path in starts]
    while stack:
        path = stack.pop()
        if path in seen:
            continue
        seen.add(path)
        try:
            text = path.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        for include in INCLUDE.findall(text):
            resolved = _resolve(include, path.parent, roots)
            if resolved is not None and resolved not in seen:
                stack.append(resolved)
    return seen


def check(root: Path) -> tuple:
    app_root = root / "app"
    if not app_root.is_dir():
        return (0, ["app/: absent"])
    edge_include = root / "edge_module/include"
    app_names = {p.name for p in app_root.iterdir() if p.is_dir()}
    problems = []

    for name_dir in sorted(p for p in app_root.iterdir() if p.is_dir()):
        roots = [name_dir / "include", edge_include]
        starts = list(name_dir.rglob("*.c")) + list((name_dir / "include").rglob("*.h"))
        for path in sorted(_closure(starts, roots)):
            rel = path.relative_to(root)
            text = path.read_text(encoding="utf-8", errors="replace")
            for include in INCLUDE.findall(text):
                segment = include.split("/", 1)[0]
                if (
                    FORBIDDEN_LAYER.match(include)
                    or FORBIDDEN_RTOS.match(include)
                    or FORBIDDEN_SOC.match(include)
                ):
                    problems.append(f"{rel}: transitively includes forbidden '{include}'")
                elif segment in app_names and segment != name_dir.name:
                    problems.append(
                        f"{rel}: app '{name_dir.name}' reaches app '{segment}' via '{include}'"
                    )

    if problems:
        return (1, problems)
    return (0, ["app transitive include check: PASS"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
