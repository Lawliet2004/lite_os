/*
 * test_all.c — master raw-syscall test runner.
 *
 * Runs each individual test by calling its logic directly (no fork needed
 * since all test helpers call do_exit on failure). This binary is the
 * one placed in initramfs and called from init as a userspace smoke test.
 *
 * Prints "RAWSYSCALL: all tests passed\n" on full success.
 * Exits non-zero on any failure.
 *
 * Syscalls exercised in this file:
 *   write, exit_group, read, openat, close, newfstatat,
 *   brk, mmap, munmap, getpid, gettid, uname,
 *   clock_gettime, nanosleep, getrandom.
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;
typedef unsigned int       uint32_t;
typedef int                int32_t;

/* ================================================================== */
/* Syscall infrastructure                                              */
/* ================================================================== */

static inline int64_t _sc0(int64_t nr)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr):"rcx","r11","memory");
    return r;
}

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

static inline int64_t _sc4(int64_t nr, int64_t a1, int64_t a2, int64_t a3, int64_t a4)
{
    int64_t r;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3),"r"(r10):"rcx","r11","memory");
    return r;
}

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

/* Syscall numbers */
#define SYS_read          0
#define SYS_write         1
#define SYS_close         3
#define SYS_mprotect      10
#define SYS_munmap        11
#define SYS_brk           12
#define SYS_mmap          9
#define SYS_getpid        39
#define SYS_gettid        186
#define SYS_uname         63
#define SYS_clock_gettime 228
#define SYS_nanosleep     35
#define SYS_getrandom     318
#define SYS_newfstatat    262
#define SYS_openat        257
#define SYS_exit_group    231
#define SYS_fork          57
#define SYS_wait4         61

/* Flags / constants */
#define AT_FDCWD      (-100)
#define O_RDONLY      0
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10
#define PROT_NONE     0
#define PROT_READ     1
#define PROT_WRITE    2
#define CLOCK_REALTIME   0
#define CLOCK_MONOTONIC  1
#define GRND_NONBLOCK    1

/* errno values */
#define EBADF   9
#define ENOENT  2
#define EFAULT  14
#define EINVAL  22

/* ================================================================== */
/* Structs                                                             */
/* ================================================================== */

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

/* S_IFREG = 0100000 octal = 0x8000 */
#define S_IFREG  0x8000u
#define S_IFDIR  0x4000u
#define S_IFMT   0xF000u

/* ================================================================== */
/* Utility helpers                                                     */
/* ================================================================== */

static void do_exit(int code)
{
    _sc1(SYS_exit_group, (int64_t)code);
    __builtin_unreachable();
}

static size_t slen(const char *s) { size_t n=0; while(s[n])n++; return n; }

static void puts_fd(int fd, const char *s)
{
    _sc3(SYS_write, (int64_t)fd, (int64_t)(size_t)s, (int64_t)slen(s));
}

/* Print a test section header */
static void test_begin(const char *name)
{
    puts_fd(1, "  Testing: ");
    puts_fd(1, name);
    puts_fd(1, "...\n");
}

static int g_fail = 0;  /* set to 1 if any sub-test fails */

static void check(int cond, const char *msg)
{
    if (!cond) {
        puts_fd(2, "    FAIL: ");
        puts_fd(2, msg);
        puts_fd(2, "\n");
        g_fail = 1;
    }
}

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

/* ================================================================== */
/* Individual sub-tests                                                */
/* ================================================================== */

/* ---- 1. write + exit_group ---- */
static void test_write(void)
{
    test_begin("write");
    int64_t r = _sc3(SYS_write, 1,
                     (int64_t)(size_t)"    [write smoke]\n",
                     18);
    check(r == 18, "write(stdout) did not return 18");

    /* write 0 bytes → 0 */
    r = _sc3(SYS_write, 1, (int64_t)(size_t)"x", 0);
    check(r == 0, "write(0 bytes) != 0");

    /* write to bad fd → -EBADF */
    r = _sc3(SYS_write, 9999, (int64_t)(size_t)"x", 1);
    check(r == -(int64_t)EBADF, "write(bad fd) != -EBADF");
}

/* ---- 2. openat + read + close ---- */
static void test_read_openat(void)
{
    test_begin("openat+read+close");
    char buf[64];

    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD,
                      (int64_t)(size_t)"/hello.txt", O_RDONLY, 0);
    check(fd >= 0, "openat /hello.txt failed");
    if (fd < 0) return;

    int64_t nr = _sc3(SYS_read, fd, (int64_t)(size_t)buf, (int64_t)(sizeof(buf)-1));
    check(nr > 0, "read /hello.txt returned <= 0");

    int64_t cr = _sc1(SYS_close, fd);
    check(cr == 0, "close returned non-zero");

    /* read on closed fd → -EBADF */
    int64_t br = _sc3(SYS_read, fd, (int64_t)(size_t)buf, 1);
    check(br == -(int64_t)EBADF, "read closed fd != -EBADF");

    /* openat non-existent → -ENOENT */
    int64_t ne = _sc4(SYS_openat, (int64_t)AT_FDCWD,
                      (int64_t)(size_t)"/no_such_file", O_RDONLY, 0);
    check(ne == -(int64_t)ENOENT, "openat nonexistent != -ENOENT");
}

/* ---- 3. newfstatat ---- */
static void test_stat(void)
{
    test_begin("newfstatat");
    struct stat st;

    int64_t r = _sc4(SYS_newfstatat, (int64_t)AT_FDCWD,
                     (int64_t)(size_t)"/hello.txt",
                     (int64_t)(size_t)&st, 0);
    check(r == 0, "newfstatat /hello.txt failed");
    check(st.st_size > 0, "newfstatat: st_size <= 0");
    check((st.st_mode & S_IFMT) == S_IFREG, "/hello.txt not S_IFREG");

    r = _sc4(SYS_newfstatat, (int64_t)AT_FDCWD,
             (int64_t)(size_t)"/",
             (int64_t)(size_t)&st, 0);
    check(r == 0, "newfstatat / failed");
    check((st.st_mode & S_IFMT) == S_IFDIR, "/ not S_IFDIR");

    r = _sc4(SYS_newfstatat, (int64_t)AT_FDCWD,
             (int64_t)(size_t)"/no_such_file",
             (int64_t)(size_t)&st, 0);
    check(r == -(int64_t)ENOENT, "newfstatat nonexistent != -ENOENT");

    r = _sc4(SYS_newfstatat, (int64_t)AT_FDCWD,
             (int64_t)(size_t)"/hello.txt", 0, 0);
    check(r == -(int64_t)EFAULT, "newfstatat NULL buf != -EFAULT");
}

/* ---- 4. brk ---- */
static void test_brk(void)
{
    test_begin("brk");

    int64_t init_brk = _sc1(SYS_brk, 0);
    check(init_brk > 0, "brk(0) returned <= 0");

    int64_t new_brk = init_brk + 4096;
    int64_t ret = _sc1(SYS_brk, new_brk);
    check(ret == new_brk, "brk grow failed");

    volatile int *ptr = (volatile int *)(size_t)init_brk;
    *ptr = 0xdeadbeef;
    check(*ptr == (int)0xdeadbeef, "brk page not writable");

    ret = _sc1(SYS_brk, init_brk);
    check(ret == init_brk, "brk shrink failed");
}

/* ---- 5. mmap + munmap ---- */
#define SYS_mprotect      10
static void test_mmap(void)
{
    test_begin("mmap+munmap");

    /* 1. Basic anonymous private mmap (verifies lazy page allocation) */
    int64_t maddr = _sc6(SYS_mmap, 0, 65536,
                          PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS,
                          -1LL, 0);
    check(maddr > 0, "mmap anon failed");
    if (maddr <= 0) return;

    /* Write to pages, triggering demand paging */
    volatile uint32_t *p1 = (volatile uint32_t *)(size_t)maddr;
    volatile uint32_t *p2 = (volatile uint32_t *)(size_t)(maddr + 65532);
    *p1 = 0xcafebabe;
    *p2 = 0x12345678;
    check(*p1 == 0xcafebabe, "mmap write p1 failed");
    check(*p2 == 0x12345678, "mmap write p2 failed");

    /* 2. mmap with length=0 must return -EINVAL */
    int64_t r = _sc6(SYS_mmap, 0, 0, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1LL, 0);
    check(r == -(int64_t)EINVAL, "mmap(len=0) != -EINVAL");

    /* 3. munmap the region */
    r = _sc2(SYS_munmap, maddr, 65536);
    check(r == 0, "munmap failed");

    /* 4. Verify munmap then access kills child with SIGSEGV */
    int64_t child_pid = _sc0(SYS_fork);
    check(child_pid >= 0, "fork for munmap access test failed");
    if (child_pid == 0) {
        /* Child: access unmapped memory */
        volatile uint32_t *bad_ptr = (volatile uint32_t *)(size_t)maddr;
        uint32_t val = *bad_ptr;
        (void)val;
        _sc1(SYS_exit_group, 42); /* should not reach here */
    } else {
        int status = 0;
        int64_t wait_ret = _sc4(SYS_wait4, child_pid, (int64_t)(size_t)&status, 0, 0);
        check(wait_ret == child_pid, "wait4 munmap access test failed");
        check(status == 11, "munmap then access did not crash child with SIGSEGV");
    }

    /* 5. Verify mprotect read-only then write fault kills child with SIGSEGV */
    maddr = _sc6(SYS_mmap, 0, 4096,
                  PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS,
                  -1LL, 0);
    check(maddr > 0, "mmap for mprotect test failed");
    
    r = _sc3(SYS_mprotect, maddr, 4096, PROT_READ);
    check(r == 0, "mprotect to PROT_READ failed");

    child_pid = _sc0(SYS_fork);
    check(child_pid >= 0, "fork for mprotect test failed");
    if (child_pid == 0) {
        /* Child: write to read-only memory */
        volatile uint32_t *p_ro = (volatile uint32_t *)(size_t)maddr;
        *p_ro = 0xabcdef;
        _sc1(SYS_exit_group, 42); /* should not reach here */
    } else {
        int status = 0;
        int64_t wait_ret = _sc4(SYS_wait4, child_pid, (int64_t)(size_t)&status, 0, 0);
        check(wait_ret == child_pid, "wait4 mprotect test failed");
        check(status == 11, "write to read-only did not crash child with SIGSEGV");
    }

    /* 6. Verify PROT_NONE fault kills child with SIGSEGV */
    r = _sc3(SYS_mprotect, maddr, 4096, PROT_NONE);
    check(r == 0, "mprotect to PROT_NONE failed");

    child_pid = _sc0(SYS_fork);
    check(child_pid >= 0, "fork for PROT_NONE test failed");
    if (child_pid == 0) {
        /* Child: read from PROT_NONE memory */
        volatile uint32_t *p_none = (volatile uint32_t *)(size_t)maddr;
        uint32_t val = *p_none;
        (void)val;
        _sc1(SYS_exit_group, 42); /* should not reach here */
    } else {
        int status = 0;
        int64_t wait_ret = _sc4(SYS_wait4, child_pid, (int64_t)(size_t)&status, 0, 0);
        check(wait_ret == child_pid, "wait4 PROT_NONE test failed");
        check(status == 11, "read from PROT_NONE did not crash child with SIGSEGV");
    }

    /* Clean up mprotect region */
    r = _sc2(SYS_munmap, maddr, 4096);
    check(r == 0, "munmap after mprotect test failed");

    /* 7. Overlapping mmap with MAP_FIXED replacement */
    /* Map a 2-page region */
    int64_t r1 = _sc6(SYS_mmap, 0, 8192,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS,
                      -1LL, 0);
    check(r1 > 0, "mmap region 1 failed");

    volatile uint32_t *r1_p1 = (volatile uint32_t *)(size_t)r1;
    volatile uint32_t *r1_p2 = (volatile uint32_t *)(size_t)(r1 + 4096);
    *r1_p1 = 0x11111111;
    *r1_p2 = 0x22222222;

    /* Map a new page with MAP_FIXED at region 1's second page address */
    int64_t r2 = _sc6(SYS_mmap, r1 + 4096, 4096,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED,
                      -1LL, 0);
    check(r2 == r1 + 4096, "mmap MAP_FIXED failed or placed at wrong address");

    /* Verify first page is still intact */
    check(*r1_p1 == 0x11111111, "MAP_FIXED corrupted adjacent page");

    /* Verify second page is zero-filled (as a fresh lazy mapping) */
    check(*r1_p2 == 0, "MAP_FIXED did not zero/replace page content");

    /* Write to the new page and verify */
    *r1_p2 = 0x33333333;
    check(*r1_p2 == 0x33333333, "write to MAP_FIXED page failed");

    /* Clean up overlapping mappings */
    r = _sc2(SYS_munmap, r1, 8192);
    check(r == 0, "munmap overlapping failed");
}

/* ---- 6. getpid + gettid ---- */
static void test_pid_tid(void)
{
    test_begin("getpid+gettid");

    int64_t pid = _sc0(SYS_getpid);
    check(pid > 0, "getpid returned <= 0");

    int64_t tid = _sc0(SYS_gettid);
    check(tid > 0, "gettid returned <= 0");
    check(tid >= pid, "gettid < getpid");
}

/* ---- 7. uname ---- */
static void test_uname(void)
{
    test_begin("uname");
    struct utsname uts;

    int64_t r = _sc1(SYS_uname, (int64_t)(size_t)&uts);
    check(r == 0, "uname failed");
    check(streq(uts.machine, "x86_64"), "uname: machine != x86_64");
    /* sysname should be LiteNix */
    check(streq(uts.sysname, "LiteNix") || streq(uts.sysname, "Linux"),
          "uname: sysname not LiteNix or Linux");

    r = _sc1(SYS_uname, 0);
    check(r == -(int64_t)EFAULT, "uname(NULL) != -EFAULT");
}

/* ---- 8. clock_gettime + nanosleep ---- */
static void test_clock(void)
{
    test_begin("clock_gettime+nanosleep");
    struct timespec ts, mono1, mono2;

    int64_t r = _sc2(SYS_clock_gettime, CLOCK_REALTIME, (int64_t)(size_t)&ts);
    check(r == 0, "clock_gettime(REALTIME) failed");
    check(ts.tv_sec >= 1577836800LL, "CLOCK_REALTIME: tv_sec too small");
    check(ts.tv_nsec >= 0 && ts.tv_nsec < 1000000000LL, "CLOCK_REALTIME: tv_nsec out of range");

    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, (int64_t)(size_t)&mono1);
    check(r == 0, "clock_gettime(MONOTONIC) failed");

    /* Sleep 50ms */
    struct timespec req; req.tv_sec=0; req.tv_nsec=50000000LL;
    _sc2(SYS_nanosleep, (int64_t)(size_t)&req, 0);

    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, (int64_t)(size_t)&mono2);
    check(r == 0, "clock_gettime(MONOTONIC) second call failed");

    int64_t diff = (mono2.tv_sec - mono1.tv_sec) * 1000000000LL
                 + (mono2.tv_nsec - mono1.tv_nsec);
    check(diff > 0, "monotonic clock did not advance after nanosleep");

    r = _sc2(SYS_clock_gettime, CLOCK_MONOTONIC, 0);
    check(r == -(int64_t)EFAULT || r == -(int64_t)EINVAL,
          "clock_gettime(NULL) did not return -EFAULT/-EINVAL");
}

/* ---- 9. getrandom ---- */
static void test_getrandom(void)
{
    test_begin("getrandom");
    uint32_t buf1[4], buf2[4];

    int64_t r = _sc3(SYS_getrandom, (int64_t)(size_t)buf1, sizeof(buf1), 0);
    check(r == (int64_t)sizeof(buf1), "getrandom: first call bad return");

    r = _sc3(SYS_getrandom, (int64_t)(size_t)buf2, sizeof(buf2), 0);
    check(r == (int64_t)sizeof(buf2), "getrandom: second call bad return");

    int same = 1;
    for (int i = 0; i < 4; i++) if (buf1[i] != buf2[i]) { same=0; break; }
    check(!same, "getrandom: two calls returned identical data");

    r = _sc3(SYS_getrandom, (int64_t)(size_t)buf1, 0, 0);
    check(r == 0, "getrandom(0 bytes) != 0");

    r = _sc3(SYS_getrandom, 0, 16, 0);
    check(r == -(int64_t)EFAULT, "getrandom(NULL) != -EFAULT");

    r = _sc3(SYS_getrandom, (int64_t)(size_t)buf1, sizeof(buf1), 0xFFFF);
    check(r == -(int64_t)EINVAL, "getrandom(bad flags) != -EINVAL");
}

/* ================================================================== */
/* Entry point                                                         */
/* ================================================================== */

void _start(void)
{
    puts_fd(1, "\n=== raw-syscall test suite ===\n");

    test_write();
    test_read_openat();
    test_stat();
    test_brk();
    test_mmap();
    test_pid_tid();
    test_uname();
    test_clock();
    test_getrandom();

    puts_fd(1, "=== raw-syscall tests done ===\n");

    if (g_fail) {
        puts_fd(2, "RAWSYSCALL: SOME TESTS FAILED\n");
        do_exit(1);
    } else {
        puts_fd(1, "RAWSYSCALL: all tests passed\n");
        do_exit(0);
    }
}
