/*
 * test_process.c — no-libc test for getpid, gettid, uname.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_getpid = 39
 *   SYS_gettid = 186
 *   SYS_uname  = 63
 *   SYS_write  = 1
 *   SYS_exit_group = 231
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* ------------------------------------------------------------------ */
/* Syscall helpers                                                      */
/* ------------------------------------------------------------------ */

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

static inline int64_t _sc3(int64_t nr, int64_t a1, int64_t a2, int64_t a3)
{
    int64_t r;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3):"rcx","r11","memory");
    return r;
}

#define SYS_getpid      39
#define SYS_gettid      186
#define SYS_uname       63
#define SYS_write       1
#define SYS_exit_group  231

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

static int streq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static int starts_with(const char *haystack, const char *needle) {
    while (*needle) {
        if (*haystack++ != *needle++) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void _start(void)
{
    /* 1. getpid must return a positive value */
    int64_t pid = _sc0(SYS_getpid);
    if (pid <= 0) fail("getpid returned <= 0", 1);

    /* 2. gettid must return a positive value */
    int64_t tid = _sc0(SYS_gettid);
    if (tid <= 0) fail("gettid returned <= 0", 2);

    /* 3. For a single-threaded process, gettid >= getpid (LiteNix: may equal) */
    if (tid < pid) fail("gettid < getpid in single-threaded process", 3);

    /* 4. uname fills the struct correctly */
    struct utsname uts;
    int64_t r = _sc1(SYS_uname, (int64_t)(size_t)&uts);
    if (r != 0) fail("uname failed", 4);

    /* 5. Check sysname — must be "LiteNix" or "Linux" */
    if (!streq(uts.sysname, "LiteNix") && !starts_with(uts.sysname, "Linux")) {
        fail("uname: sysname is not LiteNix or Linux", 5);
    }

    /* 6. Check machine — must be "x86_64" */
    if (!streq(uts.machine, "x86_64")) {
        fail("uname: machine is not x86_64", 6);
    }

    /* 7. uname with NULL pointer must return -EFAULT */
    r = _sc1(SYS_uname, 0);
    if (r != -(int64_t)EFAULT) fail("uname(NULL) did not return -EFAULT", 7);

    puts_fd(1, "PASS: getpid+gettid+uname\n");
    do_exit(0);
}
