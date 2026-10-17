#!/usr/bin/env python3
"""Enforce Composition Root wiring integrity and prevent backdoor glue aggregation.

Checks:
1. Composition Root Event Sink Wiring (组合根事件管线装配完整性):
   In product composition roots (`product/*/main.c`), initializers for event-producing
   services and modules (e.g. `ble_weather_init`, `ble_nav_init`, `ble_notifications_init`,
   `firmware_validator_init`, `watch_ui_init`) must NOT pass literal NULL or 0 for the
   `edge_event_sink_t *` parameter. The event pipeline must be explicitly wired to an
   event sink (`&event_sink`) so domain events are delivered.

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

DEFAULT_ROOT = Path(__file__).resolve().parents[2]

# Event-producing services that must receive a valid event sink in product main.c
# Format: function_name -> 0-indexed parameter position for edge_event_sink_t*
REQUIRED_EVENT_PRODUCERS = {
    "ble_weather_init": 2,
    "ble_nav_init": 1,
    "ble_notifications_init": 2,
    "firmware_validator_init": 2,
    "watch_ui_init": 3,
}

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


def check_event_sink_wiring_in_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
    problems = []

    for fn, sink_idx in REQUIRED_EVENT_PRODUCERS.items():
        # Search for calls to fn(...)
        for match in re.finditer(rf"\b{fn}\s*\(([^;]+)\)", text):
            call_content = match.group(1).replace("\n", " ")
            lineno = text[: match.start()].count("\n") + 1

            # Split arguments taking care of simple nested parens/commas
            args = [a.strip() for a in call_content.split(",")]
            if len(args) > sink_idx:
                sink_arg = args[sink_idx]
                if sink_arg in ("NULL", "0", "(void *)0", "((void *)0)"):
                    problems.append(
                        f"{rel}:{lineno}: {fn}() called with NULL event sink '{sink_arg}' "
                        f"(composition root rule: domain event pipeline must be wired)"
                    )

    return problems


def check_glue_state_in_file(path: Path, root: Path) -> list:
    rel = path.relative_to(root)
    text = path.read_text(encoding="utf-8", errors="replace")
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


def check(root: Path) -> list:
    problems = []
    prod_dir = root / "product"
    if not prod_dir.is_dir():
        return problems

    # 1. Check composition root main.c for NULL event sinks
    for main_file in sorted(prod_dir.rglob("main.c")):
        problems.extend(check_event_sink_wiring_in_file(main_file, root))

    # 2. Check product headers for backdoor peer aggregation in glue state
    for header in sorted(prod_dir.rglob("*.h")):
        problems.extend(check_glue_state_in_file(header, root))

    return problems


def main(argv) -> int:
    root = Path(argv[1]).resolve() if len(argv) > 1 else DEFAULT_ROOT
    problems = check(root)
    if problems:
        print("Composition root wiring violation:")
        for problem in problems:
            print(f"  {problem}")
        return 1
    print("composition root wiring: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
