#!/usr/bin/env python3
# Find the longest .rodata lines in the kernel ELF
import subprocess

result = subprocess.run(
    ['objdump', '-s', '-j', '.rodata', 'build/litenix.elf'],
    capture_output=True, text=True, cwd='/mnt/c/Users/Papan Ghosh/Desktop/Projects/Operating System'
)
lines = result.stdout.split('\n')

# Find longest lines
ranked = sorted([(len(l), i, l.strip()[:160]) for i, l in enumerate(lines) if l.startswith(' ffff')], reverse=True)
for length, idx, line in ranked[:10]:
    print(f"line {idx} (len {length}): {line}")
