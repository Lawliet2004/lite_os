/*
 * test_socketpair_msg.c - Tests for socketpair(), sendmsg(), recvmsg().
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

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_socketpair 53
#define SYS_sendmsg    46
#define SYS_recvmsg    47

#define AF_UNIX      1
#define SOCK_STREAM  1

struct iovec {
    void *iov_base;
    unsigned long iov_len;
};

struct msghdr_linux {
    void *msg_name;
    unsigned int msg_namelen;
    unsigned int _pad0;
    struct iovec *msg_iov;
    unsigned long msg_iovlen;
    void *msg_control;
    unsigned long msg_controllen;
    unsigned int msg_flags;
};

static void write_str(const char *s) {
    size_t len = 0; while (s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n)   { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

void _start(void) {
    int sv[2] = { -1, -1 };
    long ret = syscall4_r10(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, (long)sv);
    if (ret == 0) write_ok("socketpair(AF_UNIX, SOCK_STREAM)");
    else {
        write_fail("socketpair");
        syscall1(SYS_exit_group, 1);
    }

    char msg[] = "hello over socketpair";
    struct iovec tx_iov = { msg, sizeof(msg) - 1 };
    struct msghdr_linux tx = { 0, 0, 0, &tx_iov, 1, 0, 0, 0 };
    ret = syscall3(SYS_sendmsg, sv[0], (long)&tx, 0);
    if (ret == (long)(sizeof(msg) - 1)) write_ok("sendmsg");
    else write_fail("sendmsg");

    char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 0;
    struct iovec rx_iov = { buf, sizeof(buf) };
    struct msghdr_linux rx = { 0, 0, 0, &rx_iov, 1, 0, 0, 0 };
    ret = syscall3(SYS_recvmsg, sv[1], (long)&rx, 0);
    if (ret == (long)(sizeof(msg) - 1)) write_ok("recvmsg");
    else write_fail("recvmsg");

    int match = 1;
    for (int i = 0; i < (int)(sizeof(msg) - 1); i++) {
        if (buf[i] != msg[i]) {
            match = 0;
            break;
        }
    }
    if (match) write_ok("socketpair payload matches");
    else write_fail("socketpair payload mismatch");

    syscall1(SYS_exit_group, 0);
}
