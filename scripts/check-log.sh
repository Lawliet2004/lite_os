#!/usr/bin/env bash
set -e
LOG="$1"
if [ ! -f "$LOG" ]; then
    echo "no log: $LOG"
    exit 0
fi
echo "--- log: $LOG ($(wc -l < "$LOG") lines) ---"
grep -E "TLB|SMP|self-test|panic|FAIL|passed|error|Kernel|PIC|Phase" "$LOG" | head -80
echo "--- end ---"
