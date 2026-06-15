#!/usr/bin/env python3
# Find the longest .rodata bytes in the kernel ELF.
import subprocess
import sys

result = subprocess.run(
    ['objdump', '-s', '-j', '.rodata', 'build/litenix.elf'],
    capture_output=True, text=True
)
lines = result.stdout.split('\n')
# Each line is "addr hex1 hex2 ... ascii" — the hex part is 16+ bytes per line.
# Look for runs of zero bytes which might indicate padding, or for big constants.
# Easier: find lines where the ASCII column is short but the hex is long.
big = []
for i, line in enumerate(lines):
    parts = line.split()
    if len(parts) >= 2 and parts[1] != '(...':
        # Skip the header line
        if ':' in parts[0] or 'section' in line:
            continue
        big.append((len(line), i, line.strip()))

big.sort(reverse=True)
for length, idx, line in big[:10]:
    print(f"line {idx} (len {length}): {line[:200]}")
