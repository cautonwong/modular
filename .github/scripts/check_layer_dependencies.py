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
import re
import sys
import time
from pathlib import Path

from guard_common import is_vendored

DEFAULT_ROOT = Path(__file__).resolve().parents[2]
INCLUDE = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]', re.M)

LAYER_DIRS = ("edge_module", "app", "sys", "board", "infra", "soc", "pal", "product")
SUBDIR_LAYERS = ("app", "sys", "board", "infra", "soc", "product")
# Layers whose subdirectory is an ownership boundary, and the one owner inside a
# layer that a sibling may legitimately sit on (a hub, not a peer). Everything not
# listed is a peer edge and is rejected:
#
#   app   - no peer edges: app/a -> app/b is a backdoor bus (D78/D79)
#   sys   - every family wrapper (meter, watch, keyboard, bldc, example) is built
#           on `runtime`, so sys/<family> -> sys/runtime is the layer's shape
#   board, infra, soc - peers: a board is not built on another board
#   pal   - deliberately absent: pal/os, pal/rtos and pal/eos are three roles of
#           one abstraction, and an implementation including a sibling's port
#           header is the intended form (D46/D85)
#   product - a composition root, not a library: it may reach every layer (ALLOWED)
OWNER_EDGES = {
    "app": frozenset(),
    "sys": frozenset({"runtime"}),
    "board": frozenset(),
    "infra": frozenset(),
    "soc": frozenset(),
}

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


def include_index(roots):
    """basename -> resolved paths, built once instead of one stat per (include, root).

    Resolving an include used to cost one `is_file()` per root, and there are ~100
    roots, so a full run was O(includes x roots) syscalls - measured at 8m51s wall
    on a network filesystem against 65s of CPU. One walk per root makes it
    O(files + includes) (#253). Root order is preserved so the first match is still
    the same file the root-order scan picked.
    """
    index = {}
    for inc_root in roots:
        for path in sorted(inc_root.rglob("*")):
            if path.is_file():
                index.setdefault(path.name, []).append(path.resolve())
    return index


def resolve_include(path, include, index):
    """Resolve an include the way the compiler would, in the historical order."""
    local = path.parent / include
    if local.is_file():
        return local.resolve()
    suffix = "/" + include.replace("\\", "/")
    for candidate in index.get(Path(include).name, ()):
        if candidate.as_posix().endswith(suffix):
            return candidate
    return None


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
    index = include_index(include_roots(root))
    problems = []

    for layer in LAYER_DIRS:
        base = root / layer
        if not base.is_dir():
            continue
        for path in sorted(list(base.rglob("*.c")) + list(base.rglob("*.h"))):
            if is_vendored(path):
                continue
            text = path.read_text(encoding="utf-8", errors="replace")
            rel = path.relative_to(root)
            for include in INCLUDE.findall(text):
                resolved = resolve_include(path, include, index)
                if resolved is None:
                    continue  # standard/toolchain/external header

                target = layer_of(resolved, root)
                if target is None:
                    continue
                if target not in ALLOWED[layer]:
                    problems.append(f"{rel}: {layer} -> {target} via '{include}'")
                elif layer == target and layer in OWNER_EDGES:
                    owner = owner_of(path, root, layer)
                    reached = owner_of(resolved, root, layer)
                    if owner != reached and reached not in OWNER_EDGES[layer]:
                        problems.append(
                            f"{rel}: {layer} '{owner}' -> {layer} '{reached}' via '{include}' "
                            f"(peer edge within a layer; see OWNER_EDGES)"
                        )

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
    timing = "--timing" in argv
    args = [arg for arg in argv[1:] if not arg.startswith("--")]
    root = Path(args[0]).resolve() if args else DEFAULT_ROOT
    started = time.monotonic()
    code, messages = check(root)
    for message in messages:
        print(message)
    if timing:
        # stderr, so the pass/fail line stays byte-identical (#253)
        print(f"elapsed {time.monotonic() - started:.2f}s", file=sys.stderr)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
