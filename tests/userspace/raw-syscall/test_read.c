/*
 * test_read.c — no-libc test for openat, read, close.
 *
 * Opens /hello.txt from initramfs, reads it, verifies content.
 * Tests that close on a bad fd returns -EBADF.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_read    = 0
 *   SYS_write   = 1
 *   SYS_close   = 3
 *   SYS_openat  = 257
 *   SYS_exit_group = 231
 */

typedef long long int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long size_t;

/* ------------------------------------------------------------------ */
/* Raw syscall helpers                                                  */
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

/* openat uses r10 as arg4, need a 4-arg helper */
static inline int64_t _sc4(int64_t nr, int64_t a1, int64_t a2, int64_t a3, int64_t a4)
{
    int64_t r;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3),"r"(r10):"rcx","r11","memory");
    return r;
}

#define SYS_read        0
#define SYS_write       1
#define SYS_close       3
#define SYS_openat      257
#define SYS_exit_group  231

#define AT_FDCWD        (-100)
#define O_RDONLY        0
#define O_RDWR          2
#define O_CREAT         64

#define EBADF   9
#define ENOENT  2

static void do_exit(int code) {
    _sc1(SYS_exit_group, code);
    __builtin_unreachable();
}

static size_t slen(const char *s) { size_t n=0; while(s[n])n++; return n; }

static void puts_fd(int fd, const char *s) {
    _sc3(SYS_write, fd, (int64_t)(size_t)s, (int64_t)slen(s));
}

static void assert_or_die(int cond, const char *msg, int code) {
    if (!cond) {
        puts_fd(2, "FAIL: ");
        puts_fd(2, msg);
        puts_fd(2, "\n");
        do_exit(code);
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void _start(void)
{
    /* 1. Open /hello.txt */
    int64_t fd = _sc4(SYS_openat, (int64_t)(int)AT_FDCWD,
                      (int64_t)(size_t)"/hello.txt", O_RDONLY, 0);
    assert_or_die(fd >= 0, "openat /hello.txt failed", 1);

    /* 2. Read contents */
    char buf[64];
    int64_t nr = _sc3(SYS_read, fd, (int64_t)(size_t)buf, sizeof(buf)-1);
    assert_or_die(nr > 0, "read /hello.txt returned <= 0", 2);
    buf[nr] = '\0';

    /* 3. Verify content contains "Hello" */
    int found = 0;
    for (int64_t i = 0; i + 4 < nr; i++) {
        if (buf[i]=='H' && buf[i+1]=='e' && buf[i+2]=='l'
            && buf[i+3]=='l' && buf[i+4]=='o') { found=1; break; }
    }
    assert_or_die(found, "read: /hello.txt does not contain 'Hello'", 3);

    /* 4. Close the fd */
    int64_t cr = _sc1(SYS_close, fd);
    assert_or_die(cr == 0, "close returned non-zero", 4);

    /* 5. Read after close must return EBADF */
    int64_t br = _sc3(SYS_read, fd, (int64_t)(size_t)buf, 1);
    assert_or_die(br == -(int64_t)EBADF, "read on closed fd did not return -EBADF", 5);

    /* 6. Close bad fd must return EBADF */
    int64_t bcr = _sc1(SYS_close, 9999);
    assert_or_die(bcr == -(int64_t)EBADF, "close(9999) did not return -EBADF", 6);

    /* 7. Open non-existent file must return ENOENT */
    int64_t ne = _sc4(SYS_openat, (int64_t)(int)AT_FDCWD,
                      (int64_t)(size_t)"/no_such_file_xyz", O_RDONLY, 0);
    assert_or_die(ne == -(int64_t)ENOENT, "openat non-existent did not return -ENOENT", 7);

    puts_fd(1, "PASS: read+openat+close\n");
    do_exit(0);
}
