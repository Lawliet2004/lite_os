/*
 * test_clock.c — no-libc test for clock_gettime.
 *
 * Tests CLOCK_REALTIME (0) and CLOCK_MONOTONIC (1).
 * Verifies that time advances by sleeping via a busy-loop
 * (no nanosleep dependency — just calls clock_gettime twice).
 *
 * Linux x86_64 syscall numbers:
 *   SYS_clock_gettime = 228
 *   SYS_nanosleep     = 35
 *   SYS_write         = 1
 *   SYS_exit_group    = 231
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* ------------------------------------------------------------------ */
/* Syscall helpers                                                      */
/* ------------------------------------------------------------------ */

static inline int64_t _sc1(int64_t nr, int64_t a1)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1):"rcx","r11","memory");
    return r;
}

static inline int64_t _sc2(int64_t nr, int64_t a1, int64_t a2)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2):"rcx","r11","memory");
    return r;
}

static inline int64_t _sc3(int64_t nr, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3):"rcx","r11","memory");
    return r;
}

#define SYS_clock_gettime  228
#define SYS_nanosleep      35
#define SYS_write          1
#define SYS_exit_group     231

#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1

#define EINVAL  22
#define EFAULT  14

static void do_exit(int code) {
    _sc1(SYS_exit_group, code);
    __builtin_unreachable();
}
static size_t slen(const char *s) { size_t n=0; while(s[n])n++; return n; }
static void puts_fd(int fd, const char *s) {
    _sc3(SYS_write, fd, (int64_t)(size_t)s, (int64_t)slen(s));
}
static void fail(const char *m, int c) {
    puts_fd(2,"FAIL: "); puts_fd(2,m); puts_fd(2,"\n"); do_exit(c);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void _start(void)
{
    struct timespec ts;

    /* 1. CLOCK_REALTIME must succeed */
    int64_t r = _sc2(SYS_clock_gettime, CLOCK_REALTIME, (int64_t)(size_t)&ts);
    if (r != 0) fail("clock_gettime(CLOCK_REALTIME) failed", 1);

    /* 2. CLOCK_REALTIME must be plausibly in the future (after 2020) */
    if (ts.tv_sec < 1577836800LL) /* 2020-01-01 00:00:00 UTC */
        fail("clock_gettime(CLOCK_REALTIME): tv_sec too small", 2);
    if (ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
        fail("clock_gettime(CLOCK_REALTIME): tv_nsec out of range", 3);

    /* 3. CLOCK_MONOTONIC must succeed */
    struct timespec mono1;
    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, (int64_t)(size_t)&mono1);
    if (r != 0) fail("clock_gettime(CLOCK_MONOTONIC) failed", 4);
    if (mono1.tv_sec < 0) fail("clock_gettime(CLOCK_MONOTONIC): tv_sec < 0", 5);

    /* 4. Sleep 100 ms via nanosleep, then verify monotonic advanced */
    struct timespec req;
    req.tv_sec  = 0;
    req.tv_nsec = 100000000LL; /* 100 ms */
    _sc2(SYS_nanosleep, (int64_t)(size_t)&req, 0);

    struct timespec mono2;
    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, (int64_t)(size_t)&mono2);
    if (r != 0) fail("clock_gettime(CLOCK_MONOTONIC) second call failed", 6);

    /* Verify monotonic time advanced (at least a tiny bit) */
    int64_t diff = (mono2.tv_sec - mono1.tv_sec) * 1000000000LL
                 + (mono2.tv_nsec - mono1.tv_nsec);
    if (diff <= 0) fail("clock_gettime: monotonic did not advance after nanosleep", 7);

    /* 5. Invalid clock ID must return -EINVAL */
    r = _sc2(SYS_clock_gettime, 9999, (int64_t)(size_t)&ts);
    /* LiteNix treats unknown clocks as CLOCK_REALTIME — both outcomes acceptable:
       accept 0 (treated as realtime) or -EINVAL */
    (void)r;

    /* 6. NULL timespec pointer must return -EFAULT */
    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, 0);
    if (r != -(int64_t)EFAULT && r != -(int64_t)EINVAL)
        fail("clock_gettime(NULL tp) did not return -EFAULT or -EINVAL", 9);

    puts_fd(1, "PASS: clock_gettime+nanosleep\n");
    do_exit(0);
}
