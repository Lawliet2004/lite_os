#!/usr/bin/env bash
# Phase 8: Check the AP's LAPIC SVR and IRR.

set -uo pipefail
SOCK="${QEMU_MON_SOCK:-/tmp/qemu-mon-$$.sock}"
SERIAL_LOG="${SERIAL_LOG:-/tmp/qemu-serial-$$.log}"
rm -f "$SOCK" "$SERIAL_LOG"

qemu-system-x86_64 \
    -M pc -m 128M -cdrom build/litenix.iso \
    -serial file:"$SERIAL_LOG" \
    -display none \
    -no-reboot -no-shutdown \
    -netdev user,id=n0 -device virtio-net-pci,netdev=n0 \
    -smp 4 \
    -monitor unix:"$SOCK",server,nowait \
    -daemonize \
    -pidfile /tmp/qemu-mon-pid-$$.pid
sleep 0.5
sleep 5

{
    printf 'cpu 1\n'
    sleep 0.3
    printf 'xp /4 0xfee000F0\n'
    sleep 0.3
    printf 'xp /4 0xfee00080\n'
    sleep 0.3
    printf 'xp /4 0xfee00200\n'
    sleep 0.3
    printf 'xp /4 0xfee00210\n'
    sleep 0.3
    printf 'xp /4 0xfee00020\n'
    sleep 0.3
    printf 'info registers\n'
    sleep 0.3
    printf 'quit\n'
} | nc -U "$SOCK" > /tmp/qemu-mon-out-$$.txt 2>/dev/null

PID=$(cat /tmp/qemu-mon-pid-$$.pid 2>/dev/null)
[ -n "$PID" ] && kill "$PID" 2>/dev/null
rm -f "$SOCK" /tmp/qemu-mon-pid-$$.pid

cat /tmp/qemu-mon-out-$$.txt
