/*
 * test_epoll.c — Tests for epoll syscalls.
 * Tests: epoll_create1, epoll_ctl, epoll_wait
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
static long syscall4_r10(long nr, long a1, long a2, long a3, long a4) {
    long r;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx","r11","memory");
    return r;
}

#define SYS_write        1
#define SYS_exit_group   231
#define SYS_epoll_create1 291
#define SYS_epoll_ctl    233
#define SYS_epoll_wait   232
#define SYS_pipe2        293

#define EPOLLIN  0x001
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

static void write_str(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n) { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

void _start(void) {
    write_str("=== Epoll Syscall Tests ===\n");

    /* epoll_create1(0) */
    long epfd = syscall1(SYS_epoll_create1, 0);
    if (epfd >= 0) write_ok("epoll_create1(0)");
    else write_fail("epoll_create1(0)");

    /* epoll_ctl: add stdin (fd=0) to the epoll set */
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data = 42;
    long ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_ADD, 0 /* stdin */, (long)&ev);
    if (ret == 0) write_ok("epoll_ctl ADD fd=0");
    else write_fail("epoll_ctl ADD fd=0");

    /* epoll_wait: timeout=0 so it returns immediately */
    struct epoll_event evs[4];
    ret = syscall4_r10(SYS_epoll_wait, epfd, (long)evs, 4, 0 /* timeout=0 */);
    if (ret >= 0) write_ok("epoll_wait returns non-negative");
    else write_fail("epoll_wait");

    /* epoll_ctl DEL */
    ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_DEL, 0, 0);
    if (ret == 0) write_ok("epoll_ctl DEL");
    else write_fail("epoll_ctl DEL");

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
