#!/usr/bin/env python3
"""Enforce Domain Aggregate Root invariants, validation functions, and encapsulation.

Checks:
1. Domain Settings Validator (领域聚合设置校验器规则):
   If an application domain header declares a settings or configuration data struct
   (e.g. `<domain>_settings_t`, `<domain>_settings_data_t`), the public interface
   MUST declare an invariant validator function:
   `bool <domain>_validate_<name>(const ...)` or `edge_status_t <domain>_validate_<name>(const ...)`.
   Domain entities must not accept arbitrary, unvalidated configurations.

2. Encapsulated State Snapshot Getter (领域聚合状态快照访问规则):
   An application aggregate managing domain settings or configuration must declare an
   encapsulated getter function (e.g. `<domain>_get_<name>(...)`) to provide consumers
   and tests with a safe snapshot without exposing mutable internal members.

3. Non-Void Bounded Setters (有界状态变更禁止忽略校验结果):
   Domain mutation functions that modify temporal or bounded invariant fields
   (e.g. `<domain>_set_time(...)`, `<domain>_set_date(...)`) must NOT return `void`.
   They must return `edge_status_t` or `bool` so that out-of-range arguments can be
   rejected and the failure communicated to the caller.

Usage: check_aggregate_invariants.py [root]
"""
import re
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Struct names representing domain configuration/settings aggregates
STRUCT_PATTERN = re.compile(
    r"typedef\s+struct\s+([A-Za-z0-9_]+)?\s*\{[^}]+\}\s*([A-Za-z0-9_]+_t)\s*;",
    re.S,
)

# Setter functions that mutate bounded fields (like time, hour, minute)
BOUNDED_SETTER_PATTERN = re.compile(
    r"\bvoid\s+([A-Za-z0-9_]+_set_(?:time|date))\s*\("
)

# Validator function pattern
VALIDATOR_PATTERN = re.compile(
    r"(?:bool|edge_status_t)\s+([A-Za-z0-9_]*validate[A-Za-z0-9_]*)\s*\("
)

# Getter function pattern
GETTER_PATTERN = re.compile(
    r"(?:[A-Za-z0-9_* ]+)\s+([A-Za-z0-9_]+_get_[A-Za-z0-9_]+)\s*\("
)


def check_app_invariants(app_dir: Path, root: Path) -> list:
    problems = []
    headers = list(app_dir.rglob("*.h"))
    if not headers:
        return problems

    # Concatenate all header texts for this app to see complete public contract
    combined_header_text = ""
    header_map = {}
    for h in headers:
        rel = h.relative_to(root)
        txt = h.read_text(encoding="utf-8", errors="replace")
        header_map[h] = (rel, txt)
        combined_header_text += "\n" + txt

    # Check for domain settings structs
    found_settings = []
    for h, (rel, txt) in header_map.items():
        for _tag, struct_name in STRUCT_PATTERN.findall(txt):
            # Only target domain settings data structs, excluding app, module, interface, storage
            if not ("settings" in struct_name or "settings_data" in struct_name):
                continue
            if struct_name.endswith(
                ("_app_t", "_if_t", "_port_t", "_storage_t", "_module_t")
            ):
                continue
            found_settings.append((rel, struct_name))

    if found_settings:
        # Rule 1: Must have a validator function in the app's headers
        if not VALIDATOR_PATTERN.search(combined_header_text):
            for rel, struct_name in found_settings:
                problems.append(
                    f"{rel}: domain settings struct '{struct_name}' declared without a validator "
                    f"function (aggregate invariant rule: must declare '<domain>_validate_*' returning bool or edge_status_t)"
                )

        # Rule 2: Must have a snapshot getter function in the app's headers
        if not GETTER_PATTERN.search(combined_header_text):
            for rel, struct_name in found_settings:
                problems.append(
                    f"{rel}: domain settings struct '{struct_name}' declared without an encapsulated "
                    f"getter (aggregate encapsulation rule: must declare '<domain>_get_*')"
                )

    # Rule 3: Check for bounded setters returning void
    for h, (rel, txt) in header_map.items():
        for match in BOUNDED_SETTER_PATTERN.finditer(txt):
            fn_name = match.group(1)
            lineno = txt[: match.start()].count("\n") + 1
            problems.append(
                f"{rel}:{lineno}: setter '{fn_name}' returns void (aggregate invariant rule: "
                f"bounded setters must return edge_status_t or bool to reject invalid arguments)"
            )

    return problems


def check(root: Path) -> list:
    problems = []
    apps_dir = root / "app"
    if not apps_dir.is_dir():
        return problems

    for app_dir in sorted(apps_dir.iterdir()):
        if app_dir.is_dir():
            problems.extend(check_app_invariants(app_dir, root))

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("Aggregate invariant rule violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("aggregate invariants: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
