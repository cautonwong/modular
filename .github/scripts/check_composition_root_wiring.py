#!/usr/bin/env python3
"""Enforce Composition Root wiring integrity and prevent backdoor glue aggregation.

Checks:
1. Composition Root Port Injection (组合根端口注入完整性):
   A port that must be injected is recognised by its **parameter type**, not by a
   list of function names. Every function declared in `app/*/include/**/*.h` or
   `product/*/include/**/*.h` whose parameter list mentions a type in
   `REQUIRED_PORT_TYPES` must be called in `product/*/main.c` with a real object at
   that position - never `NULL`. Because the rule is the type, a new app that takes
   an `edge_event_sink_t *` is covered the day its header lands (#252).

2. Anti-Backdoor Glue Aggregation (禁止胶水层聚合多个对等应用后门总线):
   In product glue headers (`product/*/glue.h` or `product/*/*.h`), glue state structs
   must NOT store raw direct pointer handles to multiple peer applications
   (e.g. `watch_time_t *`, `step_counter_t *`, `heart_rate_t *`). Multi-app telemetry
   and status aggregation must be implemented cleanly via an `edge_module_t` telemetry
   adapter subscribing to domain events or via consumer-defined ports.

Usage: check_composition_root_wiring.py [root]
"""
import re
import sys
from pathlib import Path

from guard_common import strip_c_comments

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Port types that must receive a real object in a composition root. Add a type here
# when a port is proven to be mandatory for every assembly; the functions follow.
REQUIRED_PORT_TYPES = ("edge_event_sink_t",)

# Peer application aggregate types that must not be bundled together in glue state
PEER_APP_TYPES = {
    "watch_time_t",
    "step_counter_t",
    "heart_rate_t",
    "watch_power_t",
    "alarm_app_t",
    "alarm_t",
    "watch_settings_app_t",
    "ble_weather_t",
}

STRUCT_DEF = re.compile(
    r"typedef\s+struct\s+([A-Za-z0-9_]+)?\s*\{([^}]+)\}\s*([A-Za-z0-9_]+);",
    re.S,
)

NOT_A_CALL = {"if", "for", "while", "switch", "return", "sizeof", "typeof", "do"}


def split_top_level(text: str) -> list:
    """Split a parameter/argument list on commas that sit at nesting depth 0.

    `foo(a, bar(b, c), NULL)` is three arguments, not four: the old plain
    `.split(",")` shifted the index of every argument after a nested call, which
    is how a NULL sink could survive the gate (#252).
    """
    parts, current, depth = [], [], 0
    for char in text:
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth = max(0, depth - 1)
        if char == "," and depth == 0:
            parts.append("".join(current))
            current = []
        else:
            current.append(char)
    tail = "".join(current)
    if tail.strip():
        parts.append(tail)
    return parts


def iter_arg_lists(text: str, name: str):
    """Yield (line, [stripped args]) for every call of `name`, nesting-aware."""
    pattern = re.compile(rf"\b{re.escape(name)}\s*\(")
    for match in pattern.finditer(text):
        if match.group(0).split("(")[0].strip() in NOT_A_CALL:
            continue
        open_paren = match.end() - 1
        depth = 0
        for index in range(open_paren, len(text)):
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    args = [arg.strip() for arg in split_top_level(text[open_paren + 1 : index])]
                    yield text.count("\n", 0, match.start()) + 1, args
                    break


def iter_declarations(text: str):
    """Yield (name, params) for `ret_t name(params);` declarations in a header."""
    pattern = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
    for match in pattern.finditer(text):
        name = match.group(1)
        if name in NOT_A_CALL:
            continue
        open_paren = match.end() - 1
        depth = 0
        for index in range(open_paren, len(text)):
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    after = text[index + 1 :].lstrip()
                    if after.startswith(";"):
                        yield name, text[open_paren + 1 : index]
                    break


def required_port_positions(params: str) -> list:
    positions = []
    for position, param in enumerate(split_top_level(params)):
        if any(port_type in param for port_type in REQUIRED_PORT_TYPES):
            positions.append(position)
    return positions


def discover_required_sinks(root: Path) -> dict:
    """function name -> parameter positions carrying a required port type."""
    sinks = {}
    for layer in ("app", "product"):
        for header in sorted((root / layer).glob("*/include/**/*.h")):
            text = header.read_text(encoding="utf-8", errors="replace")
            for name, params in iter_declarations(text):
                positions = required_port_positions(params)
                if positions:
                    sinks.setdefault(name, set()).update(positions)
    return {name: sorted(positions) for name, positions in sinks.items()}


def is_null_literal(arg: str) -> bool:
    value = re.sub(r"\s+", " ", arg.strip())
    value = re.sub(r"^\(\s*[A-Za-z_][\w\s]*\*+\s*\)\s*", "", value)  # strip a cast
    value = value.rstrip(")").strip() if value.count("(") != value.count(")") else value
    return value in ("NULL", "0", "nullptr")


def check_event_sink_wiring_in_file(path: Path, root: Path, sinks: dict) -> list:
    rel = path.relative_to(root)
    raw = path.read_text(encoding="utf-8", errors="replace")
    # A commented-out call is prose, not assembly (#262 finding 3). Stripping keeps
    # line numbers, so a finding still points at the line to open.
    text = "\n".join(strip_c_comments(raw))
    problems = []

    for name, positions in sinks.items():
        for lineno, args in iter_arg_lists(text, name):
            for position in positions:
                if position >= len(args):
                    continue
                if is_null_literal(args[position]):
                    problems.append(
                        f"{rel}:{lineno}: {name}() called with NULL for "
                        f"{'/'.join(REQUIRED_PORT_TYPES)} at position {position} "
                        f"('{args[position]}') (composition root rule: every required "
                        f"port must be injected)"
                    )
    return problems


def check_glue_state_in_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = "\n".join(strip_c_comments(raw))
    problems = []

    for _tag, body, struct_name in STRUCT_DEF.findall(text):
        if not ("state" in struct_name.lower() or "glue" in struct_name.lower()):
            continue

        found_peers = []
        for line in body.splitlines():
            line = line.strip()
            # Match member pointer declarations like `watch_time_t *time_app;`
            for app_type in PEER_APP_TYPES:
                if re.search(rf"\b{app_type}\s*\*\s*([A-Za-z0-9_]+)\s*;", line):
                    found_peers.append(app_type)

        if len(found_peers) > 1:
            problems.append(
                f"{rel}: glue struct '{struct_name}' aggregates multiple peer application pointers "
                f"({', '.join(found_peers)}) (anti-backboard rule: use an edge_module_t event "
                f"subscription or consumer-defined ports instead of backdoor glue references)"
            )

    return problems


def check(root: Path) -> tuple:
    problems = []
    prod_dir = root / "product"
    if not prod_dir.is_dir():
        return 0, []

    sinks = discover_required_sinks(root)

    # 1. Check composition root main.c for NULL required ports
    for main_file in sorted(prod_dir.rglob("main.c")):
        problems.extend(check_event_sink_wiring_in_file(main_file, root, sinks))

    # 2. Check product headers for backdoor peer aggregation in glue state
    for header in sorted(prod_dir.rglob("*.h")):
        problems.extend(check_glue_state_in_file(header, root))

    return len(sinks), problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    sink_count, problems = check(root)
    if problems:
        print("Composition root wiring violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print(f"composition root wiring: PASS ({sink_count} port-injecting functions)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
