#!/usr/bin/env python3
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: check_size.py <elf> <flash_budget> <ram_budget>")

elf, flash_budget, ram_budget = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
result = subprocess.run(["size", "-A", elf], check=True, text=True, capture_output=True)
flash = 0
ram = 0
flash_sections = {".text", ".rodata", ".data", ".init_array", ".fini_array"}
ram_sections = {".data", ".bss", ".noinit"}

for line in result.stdout.splitlines():
    fields = line.split()
    if len(fields) < 2 or fields[0] == "section":
        continue
    try:
        size = int(fields[1])
    except ValueError:
        continue
    if fields[0] in flash_sections:
        flash += size
    if fields[0] in ram_sections:
        ram += size

print(f"FLASH={flash} / {flash_budget}")
print(f"RAM={ram} / {ram_budget}")
if flash > flash_budget or ram > ram_budget:
    raise SystemExit("memory budget exceeded")
