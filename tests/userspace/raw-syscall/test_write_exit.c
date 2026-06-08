/*
 * test_write_exit.c — no-libc test for write(1,...) and exit_group.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_write      = 1
 *   SYS_exit_group = 231
 *
 * Success: exits 0, printing "PASS: write+exit_group\n" to stdout.
 * Failure: exits non-zero.
 */

/* ------------------------------------------------------------------ */
/* Raw syscall helpers                                                  */
/* ------------------------------------------------------------------ */

typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long size_t;

static inline int64_t syscall1(int64_t nr, int64_t a1)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(nr), "D"(a1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall3(int64_t nr, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t ret;
    __asm__ volatile (
        "syscall"
        : "=a"(ret)
        : "0"(nr), "D"(a1), "S"(a2), "d"(a3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

#define SYS_write       1
#define SYS_exit_group  231

static void do_exit(int code)
{
    syscall1(SYS_exit_group, code);
    /* unreachable */
    __builtin_unreachable();
}

static size_t str_len(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int64_t do_write(int fd, const char *buf, size_t len)
{
    return syscall3(SYS_write, fd, (int64_t)(size_t)buf, (int64_t)len);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void _start(void)
{
    /* Test write to stdout */
    const char *msg = "PASS: write+exit_group\n";
    size_t len = str_len(msg);
    int64_t ret = do_write(1, msg, len);
    if (ret != (int64_t)len) {
        const char *fail = "FAIL: write returned unexpected value\n";
        do_write(2, fail, str_len(fail));
        do_exit(1);
    }

    /* Test write to stderr */
    const char *msg2 = "INFO: write to stderr works\n";
    ret = do_write(2, msg2, str_len(msg2));
    if (ret < 0) {
        do_exit(2);
    }

    /* Test write with zero bytes — must return 0 */
    ret = do_write(1, msg, 0);
    if (ret != 0) {
        const char *fail = "FAIL: write(0 bytes) did not return 0\n";
        do_write(2, fail, str_len(fail));
        do_exit(3);
    }

    do_exit(0);
}
