#!/bin/sh
# Fixture for check_size.py: stands in for `size -A` because the gate only reads
# the tool's stdout. The numbers are fixed so the budgets in the self-test are
# deterministic: flash = 64 + 512 + 32 = 608, ram = 32 + 128 = 160.
cat <<'TABLE'
section              size      addr
.isr_vector            64   0x8000000
.text                 512   0x8000040
.data                  32  0x20000000
.bss                  128  0x20000020
TABLE
