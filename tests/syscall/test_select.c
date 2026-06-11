/*
 * test_select.c — Tests for select() and pselect6() syscalls.
 */
#include <stdint.h>
#include <stddef.h>

static long syscall3(long nr, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}
static long syscall5_r10_r8(long nr, long a1, long a2, long a3, long a4, long a5) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx","r11","memory");
    return r;
}
static long syscall1(long nr, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1) : "rcx","r11","memory");
    return r;
}

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_select     23
#define SYS_pselect6   270

typedef struct { uint64_t fds_bits[16]; } fd_set_t;

struct timeval {
    long tv_sec;
    long tv_usec;
};

static void write_str(const char *s) {
    size_t len = 0; while(s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n)   { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

void _start(void) {
    write_str("=== Select Syscall Tests ===\n");

    fd_set_t rfds;
    /* Zero out the fd_set */
    for (int i = 0; i < 16; i++) rfds.fds_bits[i] = 0;
    /* Set fd 0 (stdin) */
    rfds.fds_bits[0] |= 1ULL;

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    /* select(nfds=1, readfds, writefds=NULL, exceptfds=NULL, timeout=0) */
    long ret = syscall5_r10_r8(SYS_select, 1, (long)&rfds, 0, 0, (long)&tv);
    if (ret >= 0) write_ok("select() returns non-negative");
    else write_fail("select()");

    /* pselect6: rebuild rfds (select() may have modified it) */
    for (int i = 0; i < 16; i++) rfds.fds_bits[i] = 0;
    rfds.fds_bits[0] |= 1ULL;
    /* pselect6(nfds=1, readfds, writefds=NULL, exceptfds=NULL, timeout=NULL, sigmask=NULL) */
    ret = syscall5_r10_r8(SYS_pselect6, 1, (long)&rfds, 0, 0, 0);
    if (ret >= 0) write_ok("pselect6() returns non-negative");
    else write_fail("pselect6()");

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
