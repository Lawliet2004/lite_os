/*
 * test_getrandom.c — no-libc test for getrandom.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_getrandom  = 318
 *   SYS_write      = 1
 *   SYS_exit_group = 231
 *
 * Checks:
 *   - getrandom fills the buffer (returns requested length)
 *   - Two calls return different data (probabilistic — 1 in 2^32 chance of failure)
 *   - getrandom(0 bytes) returns 0
 *   - getrandom(NULL) returns -EFAULT
 *   - getrandom with invalid flags returns -EINVAL
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;
typedef unsigned int       uint32_t;

/* ------------------------------------------------------------------ */
/* Syscall helpers                                                      */
/* ------------------------------------------------------------------ */

static inline int64_t _sc1(int64_t nr, int64_t a1)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1):"rcx","r11","memory");
    return r;
}

static inline int64_t _sc3(int64_t nr, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3):"rcx","r11","memory");
    return r;
}

#define SYS_getrandom   318
#define SYS_write       1
#define SYS_exit_group  231

/* getrandom flags */
#define GRND_NONBLOCK 0x0001
#define GRND_RANDOM   0x0002

#define EFAULT  14
#define EINVAL  22

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
    uint32_t buf1[4];
    uint32_t buf2[4];

    /* 1. Fill 16 bytes */
    int64_t r = _sc3(SYS_getrandom,
                     (int64_t)(size_t)buf1,
                     sizeof(buf1),
                     0);
    if (r != (int64_t)sizeof(buf1))
        fail("getrandom: first call did not return 16", 1);

    /* 2. Fill 16 more bytes */
    r = _sc3(SYS_getrandom,
             (int64_t)(size_t)buf2,
             sizeof(buf2),
             0);
    if (r != (int64_t)sizeof(buf2))
        fail("getrandom: second call did not return 16", 2);

    /* 3. The two buffers must differ somewhere (probabilistic) */
    int same = 1;
    for (int i = 0; i < 4; i++) {
        if (buf1[i] != buf2[i]) { same = 0; break; }
    }
    if (same)
        fail("getrandom: two calls returned identical 16-byte sequences", 3);

    /* 4. getrandom(0 bytes) must return 0 */
    r = _sc3(SYS_getrandom, (int64_t)(size_t)buf1, 0, 0);
    if (r != 0)
        fail("getrandom(0 bytes) did not return 0", 4);

    /* 5. getrandom(NULL, 16, 0) must return -EFAULT */
    r = _sc3(SYS_getrandom, 0, 16, 0);
    if (r != -(int64_t)EFAULT)
        fail("getrandom(NULL) did not return -EFAULT", 5);

    /* 6. Invalid flags must return -EINVAL */
    r = _sc3(SYS_getrandom,
             (int64_t)(size_t)buf1,
             sizeof(buf1),
             0xFFFF);  /* invalid flags */
    if (r != -(int64_t)EINVAL)
        fail("getrandom(bad flags) did not return -EINVAL", 6);

    puts_fd(1, "PASS: getrandom\n");
    do_exit(0);
}
