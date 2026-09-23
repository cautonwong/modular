#!/usr/bin/env python3
"""Fail if a firmware image references dynamic-memory symbols (D21).

A `.txt`/`.nm` positional argument is treated as captured `nm` output so the
guard self-test can exercise the checker without an ELF.

Usage:
    check_no_dynamic_memory.py <elf> [--nm-output <file>]
"""
import os
import subprocess
import sys

FORBIDDEN = {
    "malloc",
    "calloc",
    "realloc",
    "free",
    "strdup",
    "aligned_alloc",
    "posix_memalign",
    "_malloc_r",
    "_calloc_r",
    "_realloc_r",
    "_free_r",
    "_strdup_r",
    # Kernel-level allocators: a kernel's own malloc family is still allocation (#172).
    "pvPortMalloc",
    "vPortFree",
    "pvPortCalloc",
    "pvPortRealloc",
}


def symbol_names(text):
    names = []
    for line in text.splitlines():
        parts = line.split()
        if parts:
            names.append(parts[-1].split("@", 1)[0])
    return names


def main(argv) -> int:
    elf = None
    nm_output = None
    args = argv[1:]
    index = 0
    while index < len(args):
        if args[index] == "--nm-output" and index + 1 < len(args):
            nm_output = args[index + 1]
            index += 2
        else:
            elf = args[index]
            index += 1

    if nm_output is None and elf is not None and elf.endswith((".txt", ".nm")):
        nm_output, elf = elf, None

    if nm_output is not None:
        text = open(nm_output, encoding="utf-8", errors="replace").read()
    elif elf is not None:
        tool = os.environ.get("NM_TOOL", "nm")
        text = subprocess.run([tool, elf], capture_output=True, text=True, check=False).stdout
    else:
        print("usage: check_no_dynamic_memory.py <elf> [--nm-output <file>]")
        return 2

    names = symbol_names(text)
    hits = sorted({name for name in names if name in FORBIDDEN})
    if hits:
        print("dynamic-memory symbols referenced by the image:")
        for hit in hits:
            print(f"  {hit}")
        return 1
    print(f"dynamic-memory check: PASS ({len(names)} symbols)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
