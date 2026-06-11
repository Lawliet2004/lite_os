/*
 * test_socket_ext.c — Tests for extended socket syscalls.
 * Tests: setsockopt, getsockopt, shutdown
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
static long syscall5_r10_r8(long nr, long a1, long a2, long a3, long a4, long a5) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx","r11","memory");
    return r;
}
static long syscall2(long nr, long a1, long a2) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return r;
}

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_socket     41
#define SYS_setsockopt 54
#define SYS_getsockopt 55
#define SYS_shutdown   48

#define AF_INET      2
#define SOCK_STREAM  1
#define SOL_SOCKET   1
#define SO_REUSEADDR 2

static void write_str(const char *s) {
    size_t len = 0; while(s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n)   { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

void _start(void) {
    write_str("=== Extended Socket Tests ===\n");

    /* Create an AF_INET TCP socket */
    long sockfd = syscall3(SYS_socket, AF_INET, SOCK_STREAM, 0);
    if (sockfd >= 0) write_ok("socket(AF_INET, SOCK_STREAM)");
    else { write_fail("socket"); syscall1(SYS_exit_group, 1); }

    /* setsockopt: enable SO_REUSEADDR */
    int val = 1;
    long ret = syscall5_r10_r8(SYS_setsockopt, sockfd, SOL_SOCKET, SO_REUSEADDR, (long)&val, 4);
    if (ret == 0) write_ok("setsockopt SO_REUSEADDR");
    else write_fail("setsockopt");

    /* getsockopt: read SO_REUSEADDR back — value should be non-zero */
    int opt_val = 0;
    unsigned int opt_len = 4;
    ret = syscall5_r10_r8(SYS_getsockopt, sockfd, SOL_SOCKET, SO_REUSEADDR,
                          (long)&opt_val, (long)&opt_len);
    if (ret == 0) write_ok("getsockopt SO_REUSEADDR");
    else write_fail("getsockopt");

    if (opt_val != 0) write_ok("getsockopt: SO_REUSEADDR value is set");
    else write_fail("getsockopt: SO_REUSEADDR value not set");

    /* shutdown: SHUT_RDWR (2) */
    ret = syscall2(SYS_shutdown, sockfd, 2 /* SHUT_RDWR */);
    if (ret == 0) write_ok("shutdown(SHUT_RDWR)");
    else write_fail("shutdown");

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
