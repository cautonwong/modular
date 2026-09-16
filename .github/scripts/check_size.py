#!/usr/bin/env python3
import os
import subprocess
import sys

if len(sys.argv) != 4:
    raise SystemExit("usage: check_size.py <elf> <flash_budget> <ram_budget>")

elf, flash_budget, ram_budget = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
size_tool = os.environ.get("SIZE_TOOL", "size")
result = subprocess.run([size_tool, "-A", elf], check=True, text=True, capture_output=True)
flash = 0
ram = 0
flash_sections = {".isr_vector", ".text", ".rodata", ".data", ".init_array", ".fini_array"}
ram_sections = {".data", ".bss", ".noinit"}

for line in result.stdout.splitlines():
    fields = line.split()
    if len(fields) < 2 or fields[0] == "section":
        continue
    try:
        section_size = int(fields[1])
    except ValueError:
        continue
    if fields[0] in flash_sections:
        flash += section_size
    if fields[0] in ram_sections:
        ram += section_size

print(f"ELF={elf}")
print(f"SIZE_TOOL={size_tool}")
print(f"FLASH={flash} / {flash_budget}")
print(f"RAM={ram} / {ram_budget}")
if flash > flash_budget or ram > ram_budget:
    raise SystemExit("memory budget exceeded")
