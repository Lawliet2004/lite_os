/*
 * test_mmap.c — no-libc test for mmap, munmap, brk.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_brk    = 12
 *   SYS_mmap   = 9
 *   SYS_munmap = 11
 *   SYS_write  = 1
 *   SYS_exit_group = 231
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;

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

/* mmap: 6 args — rdi,rsi,rdx,r10,r8,r9 */
static inline int64_t _sc6(int64_t nr,
                            int64_t a1, int64_t a2, int64_t a3,
                            int64_t a4, int64_t a5, int64_t a6)
{
    int64_t r;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    register int64_t r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3),
                     "r"(r10),"r"(r8),"r"(r9):"rcx","r11","memory");
    return r;
}

/* munmap: 2 args */
static inline int64_t _sc2(int64_t nr, int64_t a1, int64_t a2)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2):"rcx","r11","memory");
    return r;
}

#define SYS_brk         12
#define SYS_mmap        9
#define SYS_munmap      11
#define SYS_write       1
#define SYS_exit_group  231

#define MAP_PRIVATE     0x02
#define MAP_ANONYMOUS   0x20
#define PROT_READ       1
#define PROT_WRITE      2
#define MAP_FAILED      ((void *)-1LL)

#define EINVAL 22
#define ENOMEM 12

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
    /* ---- BRK tests ---- */

    /* 1. brk(0) returns current brk (must be > 0) */
    int64_t init_brk = _sc1(SYS_brk, 0);
    if (init_brk <= 0) fail("brk(0) returned <= 0", 1);

    /* 2. Grow brk by one page */
    int64_t new_brk = init_brk + 4096;
    int64_t ret = _sc1(SYS_brk, new_brk);
    if (ret != new_brk) fail("brk grow failed", 2);

    /* 3. Write/read the newly allocated page */
    volatile int *ptr = (volatile int *)(size_t)init_brk;
    *ptr = 0xdeadbeef;
    if (*ptr != (int)0xdeadbeef) fail("brk page not writable", 3);

    /* 4. Shrink brk back */
    ret = _sc1(SYS_brk, init_brk);
    if (ret != init_brk) fail("brk shrink failed", 4);

    /* ---- MMAP anonymous tests ---- */

    /* 5. Basic anonymous private mmap */
    int64_t maddr = _sc6(SYS_mmap,
                          0,               /* addr = NULL */
                          65536,           /* 16 pages */
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1LL,            /* fd = -1 */
                          0);              /* offset = 0 */
    if (maddr < 0) fail("mmap anon failed", 5);

    /* 6. Write to first and last page */
    volatile unsigned int *p1 = (volatile unsigned int *)(size_t)maddr;
    volatile unsigned int *p2 = (volatile unsigned int *)(size_t)(maddr + 65536 - 4);
    *p1 = 0xcafebabe;
    *p2 = 0x12345678;
    if (*p1 != 0xcafebabe) fail("mmap write/read p1 failed", 6);
    if (*p2 != 0x12345678) fail("mmap write/read p2 failed", 7);

    /* 7. munmap the region */
    ret = _sc2(SYS_munmap, maddr, 65536);
    if (ret != 0) fail("munmap failed", 8);

    /* 8. mmap with length=0 must return -EINVAL */
    int64_t bad = _sc6(SYS_mmap, 0, 0,
                        PROT_READ|PROT_WRITE,
                        MAP_PRIVATE|MAP_ANONYMOUS,
                        -1LL, 0);
    if (bad != -(int64_t)EINVAL) fail("mmap(len=0) did not return -EINVAL", 9);

    /* 9. munmap with unaligned addr must return -EINVAL */
    ret = _sc2(SYS_munmap, 0x401001LL, 4096);
    if (ret != -(int64_t)EINVAL) fail("munmap(unaligned) did not return -EINVAL", 10);

    puts_fd(1, "PASS: mmap+munmap+brk\n");
    do_exit(0);
}
