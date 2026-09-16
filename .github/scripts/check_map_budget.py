#!/usr/bin/env python3
"""Attribute firmware image size to architectural layers from a GNU ld map.

Usage:
    check_map_budget.py <map-file> [--config ci/size-budget.json] [--report out.json]

The map's "Linker script and memory map" section is parsed and each allocated
input section is attributed to a layer by its object/archive name. `.data`
counts against both Flash (load image) and RAM (runtime). Budgets are absolute
per layer; exceeding any budget fails the gate.
"""
import argparse
import json
import re
import sys

DEFAULT_CONFIG = "ci/size-budget.json"

# Output section -> which budget(s) it consumes.
OUTPUT_SECTIONS = {
    ".isr_vector": "flash",
    ".text": "flash",
    ".data": "both",
    ".bss": "ram",
    ".noinit": "ram",
}
OUTPUT_HEADER = re.compile(r"^(\.\w[\w.]*)\s+0x[0-9a-fA-F]+\s+0x[0-9a-fA-F]+")
ARTIFACT = re.compile(r"0x[0-9a-fA-F]+\s+0x([0-9a-fA-F]+)\s+(\S+)")
OBJECT = re.compile(r"\.(?:o|obj)\)?$")


def layer_of(artifact):
    if "libedge_module.a" in artifact:
        return "edge_module"
    if "libsys_" in artifact:
        return "sys"
    if "libboard_" in artifact:
        return "board"
    if "libapp_" in artifact:
        return "app"
    if "libinfra_" in artifact:
        return "infra"
    if "product/" in artifact:
        return "product"
    if "startup.c" in artifact:
        return "startup"
    return "other"


def parse_map(map_path):
    sizes = {}
    started = False
    current = None
    with open(map_path, encoding="utf-8", errors="replace") as handle:
        for line in handle:
            if not started:
                if "Linker script and memory map" in line:
                    started = True
                continue
            header = OUTPUT_HEADER.match(line)
            if header:
                current = header.group(1)
                continue
            if current not in OUTPUT_SECTIONS:
                continue
            match = ARTIFACT.search(line)
            if not match:
                continue
            artifact = match.group(2)
            if not OBJECT.search(artifact):
                continue
            size = int(match.group(1), 16)
            bucket = sizes.setdefault(layer_of(artifact), {"flash": 0, "ram": 0})
            kind = OUTPUT_SECTIONS[current]
            if kind in ("flash", "both"):
                bucket["flash"] += size
            if kind in ("ram", "both"):
                bucket["ram"] += size
    return sizes


def load_config(path):
    with open(path, encoding="utf-8") as handle:
        return json.load(handle)


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("map_file")
    parser.add_argument("--config", default=DEFAULT_CONFIG)
    parser.add_argument("--report")
    args = parser.parse_args(argv)

    sizes = parse_map(args.map_file)
    config = load_config(args.config)

    total_flash = sum(v["flash"] for v in sizes.values())
    total_ram = sum(v["ram"] for v in sizes.values())

    print(f"MAP={args.map_file}")
    print(f"{'layer':<14} {'flash':>8} {'ram':>8}")
    for layer in sorted(sizes):
        print(f"{layer:<14} {sizes[layer]['flash']:>8} {sizes[layer]['ram']:>8}")
    print(f"{'TOTAL':<14} {total_flash:>8} {total_ram:>8}")

    failures = []
    for layer, budget in config.get("layers", {}).items():
        actual = sizes.get(layer, {"flash": 0, "ram": 0})
        if actual["flash"] > budget.get("flash", 0):
            failures.append(
                f"{layer} flash {actual['flash']} > {budget['flash']} ({actual['flash'] - budget['flash']:+d})"
            )
        if actual["ram"] > budget.get("ram", 0):
            failures.append(
                f"{layer} ram {actual['ram']} > {budget['ram']} ({actual['ram'] - budget['ram']:+d})"
            )
    for name, key, actual in (
        ("total", "flash_total", total_flash),
        ("total", "ram_total", total_ram),
    ):
        budget = config.get(key)
        if budget is not None and actual > budget:
            failures.append(f"{name} {key} {actual} > {budget} ({actual - budget:+d})")

    if args.report:
        report = {
            "map": args.map_file,
            "layers": sizes,
            "total": {"flash": total_flash, "ram": total_ram},
            "failures": failures,
        }
        with open(args.report, "w", encoding="utf-8") as handle:
            json.dump(report, handle, indent=2, sort_keys=True)
            handle.write("\n")

    if failures:
        print("map budget exceeded:")
        for failure in failures:
            print(f"  {failure}")
        return 1
    print("map budget: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
