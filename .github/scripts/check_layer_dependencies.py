#!/usr/bin/env python3
"""Enforce the layer dependency matrix (D44/D46/D48/D49/D85).

Every quoted include in a layer's source/header is resolved to the layer that
owns the target file, then checked against the matrix. This makes the topology
executable instead of prose: e.g. `soc -> board`, `app -> infra` and
`sys -> board` are rejected.

`infra -> soc` is denied with **no exception**: a reusable infra module only ever
depends on narrow ports. SoC-bound (register-level) code belongs in `soc/<soc>/`,
OS/RTOS-bound code in `pal/<os>/`, and the binding happens in `product/<name>/glue`.

Usage: check_layer_dependencies.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)

LAYER_DIRS = ("edge_module", "app", "sys", "board", "infra", "soc", "pal", "product")
SUBDIR_LAYERS = ("app", "sys", "board", "infra", "soc", "product")

ALLOWED = {
    "edge_module": {"edge_module"},
    "app": {"app", "edge_module"},
    "sys": {"sys", "edge_module"},
    "board": {"board", "soc", "pal", "edge_module"},
    "infra": {"infra", "edge_module", "pal"},
    "soc": {"soc"},
    "pal": {"pal", "edge_module"},
    "product": set(LAYER_DIRS),
}


def include_roots(root):
    roots = []
    for layer in ("app", "sys", "board", "infra", "soc", "pal"):
        for inc in (root / layer).glob("*/include"):
            if inc.is_dir():
                roots.append(inc.resolve())
    roots.append((root / "edge_module/include").resolve())
    for product in (root / "product").glob("*"):
        if product.is_dir():
            roots.append(product.resolve())
    return roots


def layer_of(path, root):
    try:
        rel = path.relative_to(root)
    except ValueError:
        return None
    return rel.parts[0] if rel.parts and rel.parts[0] in LAYER_DIRS else None


def owner_of(path, root, layer):
    try:
        rel = path.relative_to(root)
    except ValueError:
        return None
    if layer in SUBDIR_LAYERS and len(rel.parts) >= 2:
        return rel.parts[1]
    return None


CMAKE_CALL = re.compile(r"edge_add_[a-z_]+\(((?:[^()]|\([^()]*\))*)\)")


def declared_deps(root):
    """(owner layer, file, dependency layer, dependency name) for every declared DEPS.

    The matrix is enforced above on *includes*. A library can also declare a
    dependency it never includes: the dependency's `PUBLIC` include directories then
    reach the compile line, so the forbidden include would work the day someone
    writes it, and no gate would have noticed until then (#173).
    """
    found = []
    for layer in LAYER_DIRS:
        base = root / layer
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("CMakeLists.txt")):
            text = path.read_text(encoding="utf-8", errors="replace")
            flat = " ".join(line.split("#", 1)[0] for line in text.splitlines())
            for args in CMAKE_CALL.findall(flat):
                tokens = args.split()
                if "DEPS" not in tokens:
                    continue
                for dep in tokens[tokens.index("DEPS") + 1:]:
                    if dep == "edge_module":
                        dep_layer = "edge_module"
                    else:
                        head = dep.split("_", 1)[0]
                        dep_layer = head if head in LAYER_DIRS else None
                    if dep_layer is not None:
                        found.append((layer, path.relative_to(root), dep_layer, dep))
    return found


def check(root):
    root = root.resolve()
    roots = include_roots(root)
    problems = []

    for layer in LAYER_DIRS:
        base = root / layer
        if not base.is_dir():
            continue
        for path in sorted(list(base.rglob("*.c")) + list(base.rglob("*.h"))):
            text = path.read_text(encoding="utf-8", errors="replace")
            rel = path.relative_to(root)
            for include in INCLUDE.findall(text):
                resolved = None
                local = path.parent / include
                if local.is_file():
                    resolved = local.resolve()
                if resolved is None:
                    for inc_root in roots:
                        candidate = inc_root / include
                        if candidate.is_file():
                            resolved = candidate.resolve()
                            break
                if resolved is None:
                    continue  # standard/toolchain/external header

                target = layer_of(resolved, root)
                if target is None:
                    continue
                if target not in ALLOWED[layer]:
                    problems.append(f"{rel}: {layer} -> {target} via '{include}'")
                elif layer == "app" and target == "app":
                    owner = owner_of(path, root, "app")
                    reached = owner_of(resolved, root, "app")
                    if owner != reached:
                        problems.append(f"{rel}: app '{owner}' -> app '{reached}' via '{include}'")

    for layer, rel, dep_layer, dep in declared_deps(root):
        # `edge_module` is bookkeeping, not a dependency: the helper requires a DEPS
        # and puts `edge_module/include` on every target, so naming it says nothing
        # about what the layer may include - the include rule above still does (D49
        # keeps `soc -> soc` for real includes).
        if dep_layer == layer or dep_layer == "edge_module":
            continue
        if dep_layer not in ALLOWED[layer]:
            problems.append(f"{rel}: {layer} -> {dep_layer} declared in DEPS ('{dep}')")

    if problems:
        return (1, problems)
    return (
        0,
        [f"layer dependency check: PASS ({len(LAYER_DIRS)} layers, includes and CMake DEPS)"],
    )


def main(argv):
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
