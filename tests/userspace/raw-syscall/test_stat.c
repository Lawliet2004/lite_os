/*
 * test_stat.c — no-libc test for newfstatat.
 *
 * Verifies that newfstatat returns correct st_size and st_mode
 * for a known file (/hello.txt) from initramfs.
 *
 * Linux x86_64 syscall numbers:
 *   SYS_newfstatat = 262
 *   SYS_write      = 1
 *   SYS_exit_group = 231
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;
typedef unsigned int       uint32_t;

/* Linux stat struct (x86_64) */
struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    int64_t  __unused[3];
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

#define SYS_write       1
#define SYS_newfstatat  262
#define SYS_exit_group  231

#define AT_FDCWD     (-100)
#define AT_SYMLINK_NOFOLLOW 0x100

#define ENOENT  2
#define EBADF   9
#define EFAULT  14

/* S_IFREG = 0100000, S_IFDIR = 0040000 */
#define S_IFREG 0100000u
#define S_IFDIR 0040000u
#define S_IFMT  0170000u

static void do_exit(int code) {
    _sc1(SYS_exit_group, code);
    __builtin_unreachable();
}

static size_t slen(const char *s) { size_t n=0; while(s[n])n++; return n; }
static void puts_fd(int fd, const char *s) {
    _sc3(SYS_write, fd, (int64_t)(size_t)s, (int64_t)slen(s));
}

static void fail(const char *msg, int code) {
    puts_fd(2, "FAIL: "); puts_fd(2, msg); puts_fd(2, "\n");
    do_exit(code);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

void _start(void)
{
    struct stat st;

    /* 1. Stat /hello.txt — must succeed and have st_size > 0 */
    int64_t r = _sc4(SYS_newfstatat,
                     (int64_t)(int)AT_FDCWD,
                     (int64_t)(size_t)"/hello.txt",
                     (int64_t)(size_t)&st,
                     0);
    if (r != 0) fail("newfstatat /hello.txt failed", 1);
    if (st.st_size <= 0) fail("newfstatat: st_size <= 0 for /hello.txt", 2);
    if ((st.st_mode & S_IFMT) != S_IFREG) fail("newfstatat: /hello.txt not S_IFREG", 3);

    /* 2. Stat / — must be a directory */
    r = _sc4(SYS_newfstatat,
             (int64_t)(int)AT_FDCWD,
             (int64_t)(size_t)"/",
             (int64_t)(size_t)&st,
             0);
    if (r != 0) fail("newfstatat / failed", 4);
    if ((st.st_mode & S_IFMT) != S_IFDIR) fail("newfstatat: / is not S_IFDIR", 5);

    /* 3. Stat non-existent file must return -ENOENT */
    r = _sc4(SYS_newfstatat,
             (int64_t)(int)AT_FDCWD,
             (int64_t)(size_t)"/no_such_file_xyz",
             (int64_t)(size_t)&st,
             0);
    if (r != -(int64_t)ENOENT) fail("newfstatat: non-existent did not return -ENOENT", 6);

    /* 4. Stat with NULL statbuf must return -EFAULT */
    r = _sc4(SYS_newfstatat,
             (int64_t)(int)AT_FDCWD,
             (int64_t)(size_t)"/hello.txt",
             0,
             0);
    if (r != -(int64_t)EFAULT) fail("newfstatat: NULL statbuf did not return -EFAULT", 7);

    puts_fd(1, "PASS: newfstatat\n");
    do_exit(0);
}
