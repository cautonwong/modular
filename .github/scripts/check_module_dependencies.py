#!/usr/bin/env python3
"""Detect cyclic dependencies and missing module dependencies (ADR-005, CI-ARCH-009).

Scans all module descriptor declarations (edge_module_desc_t) across the codebase
and verifies:
1. All declared dependency IDs point to existing valid module IDs.
2. The dependency graph has no directed cycles (DAG validation).

Usage: check_module_dependencies.py [root]
"""
from pathlib import Path
import re
import sys

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Match static const edge_module_desc_t or edge_module_desc_t definition
DESC_PATTERN = re.compile(
    r"(?:const\s+)?edge_module_desc_t\s+([A-Za-z0-9_]+)\s*=\s*\{([^}]+)\};",
    re.M | re.S,
)


def parse_descriptors(root: Path) -> dict:
    """Extract module descriptors: id -> list of dependency ids."""
    descriptors = {}
    dep_arrays = {}

    # Gather dependency array definitions first: static const uint32_t g_deps[] = { ... };
    array_pattern = re.compile(
        r"(?:static\s+const\s+)?uint32_t\s+([A-Za-z0-9_]+)\[\]\s*=\s*\{([^}]+)\};",
        re.M,
    )
    for path in root.glob("**/*.[ch]"):
        if "tests" in path.parts and (root / "tests") == (path.parents[len(path.parents) - 2] if len(path.parents) >= 2 else None):
            continue
        # If testing under repo root, ignore tests/
        if (root / "tests").is_dir() and (root / "tests") in path.parents:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for arr_name, arr_content in array_pattern.findall(text):
            ids = []
            for item in arr_content.split(","):
                item = item.strip()
                if item:
                    m = re.match(r"(0[xX][0-9A-Fa-f]+|\d+)u?", item)
                    if m:
                        ids.append(int(m.group(1), 0))
            if ids:
                dep_arrays[arr_name] = ids

    # Gather descriptors
    for path in root.glob("**/*.[ch]"):
        if (root / "tests").is_dir() and (root / "tests") in path.parents:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        for desc_name, body in DESC_PATTERN.findall(text):
            id_match = re.search(r"\.id\s*=\s*([A-Za-z0-9_]+)", body)
            if not id_match:
                continue
            id_token = id_match.group(1).strip()
            # If literal or macro
            mod_id = None
            if id_token.startswith("0x") or id_token.isdigit():
                mod_id = int(id_token.rstrip("uU"), 0)
            else:
                # Resolve macro if in file
                m_def = re.search(rf"#define\s+{re.escape(id_token)}\s+(0[xX][0-9A-Fa-f]+|\d+)", text)
                if m_def:
                    mod_id = int(m_def.group(1), 0)
                else:
                    mod_id = id_token

            deps = []
            dep_match = re.search(r"\.dependencies\s*=\s*([A-Za-z0-9_]+)", body)
            if dep_match:
                dep_token = dep_match.group(1).strip()
                if dep_token != "NULL" and dep_token in dep_arrays:
                    deps = dep_arrays[dep_token]

            descriptors[desc_name] = {"id": mod_id, "deps": deps, "path": path}

    return descriptors


def check(root: Path) -> tuple:
    descriptors = parse_descriptors(root)
    violations = []

    # Map id -> desc_name
    id_to_desc = {}
    for desc_name, data in descriptors.items():
        if data["id"] is not None:
            id_to_desc[data["id"]] = desc_name

    # Check for cycles using DFS
    adj = {}
    for desc_name, data in descriptors.items():
        adj[desc_name] = []
        for dep in data["deps"]:
            if dep in id_to_desc:
                adj[desc_name].append(id_to_desc[dep])
            else:
                violations.append(
                    f"{data['path']}: descriptor '{desc_name}' declares dependency 0x{dep:04x} not found in registered modules"
                )

    visited = {}

    def dfs(node, path):
        visited[node] = 1  # visiting
        for neighbor in adj.get(node, []):
            if visited.get(neighbor) == 1:
                cycle_str = " -> ".join(path + [neighbor])
                violations.append(f"Cyclic dependency detected: {cycle_str}")
            elif neighbor not in visited:
                dfs(neighbor, path + [neighbor])
        visited[node] = 2  # visited

    for node in adj:
        if node not in visited:
            dfs(node, [node])

    if violations:
        return (1, violations)
    return (0, [f"Module dependency check: PASS ({len(descriptors)} module descriptors analyzed, 0 cycles detected)"])


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    code, messages = check(root)
    for message in messages:
        print(message)
    return code


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
