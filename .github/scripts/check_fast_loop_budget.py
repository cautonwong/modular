#!/usr/bin/env python3
"""Static instruction budget for one call tree, from a cross-built ELF (D4).

The reference firmware runs its fast control loop in 15 microseconds at 168 MHz - 2520 cycles.
This measures the same loop here by counting the instructions the linker left in the image for it
and for everything it calls, and then turning that into cycles under a model stated out loud:

* **One cycle per instruction.** That is the Cortex-M4 baseline; loads take two, branches one to
  three, and neither is modelled separately. So the cycle figure is an *underestimate* while the
  instruction count is exact.
* **No flash wait states, no memory stalls, no interrupts.** A part with wait states runs slower
  than this, never faster, so the figure is a floor for the cycles rather than a promise about the
  deadline.
* **Every call in the closure is counted once, as if it were taken.** Branches that skip work make
  the real run shorter, so this is the upper bound on the instructions *reached* while the count
  itself is exact.

What that buys is a bound that can be checked in CI on every change, and a number that can be
compared with the reference's. What it does not buy is a measurement of the loop on the part: there
is no STM32F4 to run it on here, and this file does not pretend the model is the hardware.

Usage:
    check_fast_loop_budget.py <elf> [--max-cycles N] [--cpu-hz HZ] [--root SYMBOL]
                              [--objdump TOOL] [--report FILE]
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = "foc_core_fast_loop"
DEFAULT_CPU_HZ = 168_000_000
DEFAULT_OBJDUMP = "arm-none-eabi-objdump"

SYMBOL = re.compile(r"^([0-9a-fA-F]+) <(.+)>:\s*$")
INSN = re.compile(r"^\s+[0-9a-fA-F]+:\s+(\S+)(?:\s+(.*))?$")
CALL = re.compile(r"<([^>+]+)(?:\+0x[0-9a-fA-F]+)?>")


def disassemble(elf: Path, objdump: str) -> dict:
    """Symbol name -> (instruction count, set of symbols it calls)."""
    result = subprocess.run(
        [objdump, "-d", "--no-show-raw-insn", str(elf)],
        check=True,
        capture_output=True,
        text=True,
    )

    functions: dict = {}
    current = None
    for line in result.stdout.splitlines():
        header = SYMBOL.match(line)
        if header:
            current = header.group(2)
            functions.setdefault(current, [0, set()])
            continue
        if current is None:
            continue
        body = INSN.match(line)
        if not body:
            continue
        mnemonic, operands = body.group(1), body.group(2) or ""
        functions[current][0] += 1
        if mnemonic.startswith("bl"):
            called = CALL.search(operands)
            if called:
                functions[current][1].add(called.group(1))
    return {name: (count, calls) for name, (count, calls) in functions.items()}


def closure(functions: dict, root: str) -> tuple:
    """The root, everything it reaches, and the total instructions in that set.

    `symbol.cold` is the same function: the compiler splits the unlikely half out and the linker
    keeps it under a dotted name, so it belongs to the symbol it came from.
    """
    def canonical(name: str) -> str:
        return name.split(".")[0]

    by_name = {canonical(name): (name, entry) for name, entry in functions.items()}
    reached: dict = {}
    pending = [canonical(root)]
    while pending:
        name = pending.pop()
        if name in reached or name not in by_name:
            continue
        original, (count, calls) = by_name[name]
        reached[name] = (original, count)
        pending.extend(canonical(called) for called in calls)
    total = sum(count for _, count in reached.values())
    return reached, total


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("elf", type=Path)
    parser.add_argument("--root", default=DEFAULT_ROOT)
    parser.add_argument("--cpu-hz", type=int, default=DEFAULT_CPU_HZ)
    parser.add_argument("--max-cycles", type=int, default=None)
    parser.add_argument("--objdump", default=DEFAULT_OBJDUMP)
    parser.add_argument("--report", type=Path, default=None)
    args = parser.parse_args()

    if not args.elf.is_file():
        print(f"fast-loop budget: missing {args.elf}", file=sys.stderr)
        return 2

    functions = disassemble(args.elf, args.objdump)
    if args.root not in functions and f"{args.root}.cold" not in functions:
        print(f"fast-loop budget: {args.root} not in {args.elf}", file=sys.stderr)
        return 2

    reached, instructions = closure(functions, args.root)
    nanoseconds = instructions * 1_000_000_000 / args.cpu_hz

    biggest = sorted(reached.items(), key=lambda item: item[1][1], reverse=True)[:8]
    print(f"fast-loop budget: {args.root} and {len(reached) - 1} callee(s)")
    for name, (_, count) in biggest:
        print(f"  {count:6d} instructions  {name}")
    print(f"  {instructions:6d} instructions total, {nanoseconds / 1000.0:.2f} us at "
          f"{args.cpu_hz / 1e6:.0f} MHz (one cycle each, no wait states)")

    report = {
        "elf": str(args.elf),
        "root": args.root,
        "cpu_hz": args.cpu_hz,
        "instructions": instructions,
        "microseconds": nanoseconds / 1000.0,
        "functions": {name: count for name, (_, count) in sorted(reached.items())},
    }
    if args.report:
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(f"fast-loop budget: wrote {args.report}")

    if args.max_cycles is not None and instructions > args.max_cycles:
        print(f"fast-loop budget exceeded: {instructions} instructions > {args.max_cycles}",
              file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
