/*
 * test_epoll.c - Raw syscall tests for epoll syscalls.
 */
#include <stdint.h>
#include <stddef.h>

static long syscall0(long nr)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr) : "rcx","r11","memory");
    return r;
}

static long syscall1(long nr, long a1)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1) : "rcx","r11","memory");
    return r;
}

static long syscall2(long nr, long a1, long a2)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return r;
}

static long syscall3(long nr, long a1, long a2, long a3)
{
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}

static long syscall4_r10(long nr, long a1, long a2, long a3, long a4)
{
    long r;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx","r11","memory");
    return r;
}

#define SYS_write        1
#define SYS_close        3
#define SYS_nanosleep   35
#define SYS_fork        57
#define SYS_wait4       61
#define SYS_exit_group  231
#define SYS_epoll_create1 291
#define SYS_epoll_ctl   233
#define SYS_epoll_wait  232
#define SYS_pipe2       293
#define SYS_socketpair   53

#define AF_UNIX   1
#define SOCK_STREAM 1

#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

struct epoll_event {
    uint32_t events;
    uint64_t data;
} __attribute__((packed));

struct timespec {
    long tv_sec;
    long tv_nsec;
};

static void write_str(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}

static void write_ok(const char *n) { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

static void sleep_ms(long ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    syscall2(SYS_nanosleep, (long)&req, 0);
}

void _start(void)
{
    write_str("=== Epoll Syscall Tests ===\n");

    long epfd = syscall1(SYS_epoll_create1, 0);
    if (epfd < 0) {
        write_fail("epoll_create1");
        syscall1(SYS_exit_group, 1);
    }
    write_ok("epoll_create1");

    int pipefd[2] = { -1, -1 };
    if (syscall2(SYS_pipe2, (long)pipefd, 0) != 0) {
        write_fail("pipe2");
        syscall1(SYS_exit_group, 1);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data = 0x1111;
    long ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_ADD, pipefd[0], (long)&ev);
    if (ret == 0) write_ok("epoll_ctl ADD pipe read end");
    else write_fail("epoll_ctl ADD pipe read end");

    ev.events = EPOLLIN | EPOLLOUT;
    ev.data = 0x2222;
    ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_MOD, pipefd[0], (long)&ev);
    if (ret == 0) write_ok("epoll_ctl MOD pipe read end");
    else write_fail("epoll_ctl MOD pipe read end");

    struct epoll_event evs[4];
    ret = syscall4_r10(SYS_epoll_wait, epfd, (long)evs, 4, 0);
    if (ret == 0) {
        write_ok("epoll_wait timeout returns 0");
    } else {
        write_fail("epoll_wait timeout returns 0");
    }

    int child = (int)syscall0(SYS_fork);
    if (child == 0) {
        sleep_ms(50);
        const char *msg = "ready";
        syscall3(SYS_write, pipefd[1], (long)msg, 5);
        syscall1(SYS_exit_group, 0);
    }

    ret = syscall4_r10(SYS_epoll_wait, epfd, (long)evs, 4, 1000);
    if (ret == 1 && evs[0].data == 0x2222 && (evs[0].events & EPOLLIN)) {
        write_ok("epoll_wait pipe readiness");
    } else {
        write_fail("epoll_wait pipe readiness");
    }

    int status = 0;
    syscall4_r10(SYS_wait4, child, (long)&status, 0, 0);

    ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_DEL, pipefd[0], 0);
    if (ret == 0) write_ok("epoll_ctl DEL");
    else write_fail("epoll_ctl DEL");

    int sv[2] = { -1, -1 };
    if (syscall4_r10(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, (long)sv) == 0) {
        struct epoll_event sev;
        sev.events = EPOLLIN;
        sev.data = 0x3333;
        ret = syscall4_r10(SYS_epoll_ctl, epfd, EPOLL_CTL_ADD, sv[1], (long)&sev);
        if (ret == 0) {
            const char *payload = "x";
            syscall3(SYS_write, sv[0], (long)payload, 1);
            ret = syscall4_r10(SYS_epoll_wait, epfd, (long)evs, 4, 1000);
            if (ret >= 1 && evs[0].data == 0x3333 && (evs[0].events & EPOLLIN)) {
                write_ok("epoll_wait socket readiness");
            } else {
                write_fail("epoll_wait socket readiness");
            }
        } else {
            write_fail("epoll_ctl ADD socket end");
        }
    } else {
        write_fail("socketpair");
    }

    syscall1(SYS_close, pipefd[0]);
    syscall1(SYS_close, pipefd[1]);
    syscall1(SYS_close, sv[0]);
    syscall1(SYS_close, sv[1]);
    syscall1(SYS_close, epfd);

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
