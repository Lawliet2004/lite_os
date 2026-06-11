/*
 * test_kill_pgrp.c — Tests for kill with negative PID (process groups).
 * Tests: kill(0, 0) and kill(-pgid, 0) — existence checks only.
 */
#include <stdint.h>
#include <stddef.h>

static long syscall1(long nr, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1) : "rcx","r11","memory");
    return r;
}
static long syscall3(long nr, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}
static long syscall2(long nr, long a1, long a2) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return r;
}

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_kill       62
#define SYS_getpid     39
#define SYS_getpgid    121

static void write_str(const char *s) {
    size_t len = 0; while(s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n)   { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

/* Print a positive decimal integer followed by a newline */
static void write_long(long v) {
    char buf[20];
    int pos = 0;
    if (v == 0) {
        buf[pos++] = '0';
    } else {
        int start = pos;
        long tmp = v;
        while (tmp > 0) { buf[pos++] = '0' + (tmp % 10); tmp /= 10; }
        /* Reverse the digits */
        for (int i = start, j = pos - 1; i < j; i++, j--) {
            char c = buf[i]; buf[i] = buf[j]; buf[j] = c;
        }
    }
    buf[pos++] = '\n';
    buf[pos]   = '\0';
    syscall3(SYS_write, 1, (long)buf, pos);
}

void _start(void) {
    write_str("=== Kill Process Group Tests ===\n");

    long pid  = syscall1(SYS_getpid, 0);
    long pgid = syscall1(SYS_getpgid, 0);

    /* kill(0, 0) — sig=0 sends to every process in the current process group;
     * used here purely as an existence/permission check. */
    long ret = syscall2(SYS_kill, 0, 0);
    if (ret == 0) write_ok("kill(0, 0) returns 0");
    else write_fail("kill(0, 0)");

    /* kill(-pgid, 0) — address the process group by negated PGID */
    ret = syscall2(SYS_kill, -pgid, 0);
    if (ret == 0) write_ok("kill(-pgid, 0) returns 0");
    else write_fail("kill(-pgid, 0)");

    /* kill(-1, 0) — broadcast; only root can do this; accept 0 or -EPERM */
    ret = syscall2(SYS_kill, -1, 0);
    if (ret == 0 || ret == -1) write_ok("kill(-1, 0) handled");
    else write_fail("kill(-1, 0)");

    write_str("pid=");
    write_long(pid);

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
    (void)pgid; /* suppress unused-variable warning */
}
