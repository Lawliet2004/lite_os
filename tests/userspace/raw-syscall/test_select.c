/*
 * test_select.c - Raw syscall tests for select() and pselect6().
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

static long syscall5_r10_r8(long nr, long a1, long a2, long a3, long a4, long a5)
{
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8") = a5;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx","r11","memory");
    return r;
}

static long syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8") = a5;
    register long r9  __asm__("r9") = a6;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx","r11","memory");
    return r;
}

#define SYS_write       1
#define SYS_read        0
#define SYS_nanosleep   35
#define SYS_kill        62
#define SYS_pipe2       293
#define SYS_select      23
#define SYS_pselect6    270
#define SYS_rt_sigaction 13
#define SYS_fork        57
#define SYS_wait4       61
#define SYS_exit_group  231
#define SYS_getpid      39
#define SYS_getppid     110

#define SIGUSR1 10
#define EINVAL 22
#define EINTR 4

typedef struct { uint64_t fds_bits[16]; } fd_set_t;

struct timeval {
    long tv_sec;
    long tv_usec;
};

struct timespec {
    long tv_sec;
    long tv_nsec;
};

struct sigaction_linux {
    void (*sa_handler)(int);
    uint64_t sa_flags;
    void (*sa_restorer)(void);
    uint64_t sa_mask;
};

static void write_str(const char *s)
{
    size_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}

static void write_ok(const char *n) { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

static void zero_fdset(fd_set_t *set)
{
    for (int i = 0; i < 16; i++) set->fds_bits[i] = 0;
}

static void set_fd(fd_set_t *set, int fd)
{
    set->fds_bits[fd / 64] |= (1ULL << (fd % 64));
}

static int isset(const fd_set_t *set, int fd)
{
    return (set->fds_bits[fd / 64] >> (fd % 64)) & 1U;
}

static void noop_handler(int sig)
{
    (void)sig;
}

__asm__(
".global __restore_rt\n"
"__restore_rt:\n"
"    movq $15, %rax\n"
"    syscall\n"
);
void __restore_rt(void);

static int install_usr1_handler(void)
{
    struct sigaction_linux act;
    for (size_t i = 0; i < sizeof(act); i++) ((unsigned char *)&act)[i] = 0;
    act.sa_handler = noop_handler;
    act.sa_flags = 0x04000000; // SA_RESTORER
    act.sa_restorer = __restore_rt;
    return (int)syscall4_r10(SYS_rt_sigaction, SIGUSR1, (long)&act, 0, sizeof(uint64_t));
}

static void sleep_ms(long ms)
{
    struct timespec req;
    req.tv_sec = ms / 1000;
    req.tv_nsec = (ms % 1000) * 1000000L;
    syscall2(SYS_nanosleep, (long)&req, 0);
}

void _start(void)
{
    write_str("=== Select Syscall Tests ===\n");

    int pipefd[2] = { -1, -1 };
    if (syscall2(SYS_pipe2, (long)pipefd, 0) != 0) {
        write_fail("pipe2");
        syscall1(SYS_exit_group, 1);
    }

    fd_set_t rfds;
    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);

    struct timeval tv0;
    tv0.tv_sec = 0;
    tv0.tv_usec = 0;
    long ret = syscall5_r10_r8(SYS_select, pipefd[0] + 1, (long)&rfds, 0, 0, (long)&tv0);
    if (ret == 0 && !isset(&rfds, pipefd[0])) {
        write_ok("select timeout returns 0");
    } else {
        write_fail("select timeout returns 0");
    }

    int child = (int)syscall0(SYS_fork);
    if (child == 0) {
        sleep_ms(50);
        const char *msg = "wake";
        syscall3(SYS_write, pipefd[1], (long)msg, 4);
        syscall1(SYS_exit_group, 0);
    }

    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);
    struct timeval tv1;
    tv1.tv_sec = 1;
    tv1.tv_usec = 0;
    ret = syscall5_r10_r8(SYS_select, pipefd[0] + 1, (long)&rfds, 0, 0, (long)&tv1);
    if (ret == 1 && isset(&rfds, pipefd[0])) {
        write_ok("select pipe read readiness");
    } else {
        write_fail("select pipe read readiness");
    }

    char drain[8];
    syscall3(SYS_read, pipefd[0], (long)drain, sizeof(drain));

    int status = 0;
    syscall4_r10(SYS_wait4, child, (long)&status, 0, 0);

    zero_fdset(&rfds);
    set_fd(&rfds, pipefd[0]);
    if (install_usr1_handler() != 0) {
        write_fail("rt_sigaction");
        syscall1(SYS_exit_group, 1);
    }

    child = (int)syscall0(SYS_fork);
    if (child == 0) {
        sleep_ms(50);
        syscall2(SYS_kill, syscall0(SYS_getppid), SIGUSR1);
        syscall1(SYS_exit_group, 0);
    }

    struct timespec ts;
    ts.tv_sec = 1;
    ts.tv_nsec = 0;
    ret = syscall6(SYS_pselect6, pipefd[0] + 1, (long)&rfds, 0, 0, (long)&ts, 0);
    if (ret == -(long)EINTR) {
        write_ok("pselect6 signal interruption");
    } else {
        write_fail("pselect6 signal interruption");
    }

    syscall4_r10(SYS_wait4, child, (long)&status, 0, 0);

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
