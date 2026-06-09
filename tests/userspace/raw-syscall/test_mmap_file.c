/*
 * test_mmap_file.c — file-backed mmap tests (MAP_PRIVATE, PROT_READ/PROT_WRITE)
 *
 * Tests:
 *   a. test_mmap_file_read    — mmap file, verify contents match
 *   b. test_mmap_file_offset  — mmap with page-aligned offset, verify offset
 *   c. test_mmap_file_beyond_eof — bytes beyond EOF are zero
 *   d. test_mmap_private_write — private write doesn't affect underlying file
 *   e. test_mmap_bad_fd       — mmap with fd=-1 returns MAP_FAILED, EBADF
 *   f. test_mmap_misaligned_offset — offset=1 returns MAP_FAILED, EINVAL
 *
 * Syscalls: openat, write, read, close, mmap, munmap, exit_group, newfstatat
 */

typedef long long          int64_t;
typedef unsigned long long uint64_t;
typedef unsigned long      size_t;
typedef unsigned int       uint32_t;
typedef unsigned char      uint8_t;

/* ------------------------------------------------------------------ */
/* Syscall helpers (Linux x86_64 ABI)                                 */
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

static inline int64_t _sc4(int64_t nr, int64_t a1, int64_t a2, int64_t a3, int64_t a4)
{
    int64_t r;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall":"=a"(r):"0"(nr),"D"(a1),"S"(a2),"d"(a3),"r"(r10):"rcx","r11","memory");
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

/* ------------------------------------------------------------------ */
/* Syscall numbers & constants                                         */
/* ------------------------------------------------------------------ */

#define SYS_openat      257
#define SYS_read         0
#define SYS_write        1
#define SYS_close        3
#define SYS_mmap         9
#define SYS_munmap      11
#define SYS_exit_group 231
#define SYS_newfstatat 262

#define AT_FDCWD      (-100)
#define O_RDONLY        0
#define O_WRONLY        1
#define O_CREAT        64
#define O_TRUNC        512

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define PROT_NONE      0
#define PROT_READ      1
#define PROT_WRITE     2

#define EBADF          9
#define EINVAL        22

/* ------------------------------------------------------------------ */
/* Structs                                                             */
/* ------------------------------------------------------------------ */

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

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static void do_exit(int code)
{
    _sc1(SYS_exit_group, (int64_t)code);
    __builtin_unreachable();
}

static size_t slen(const char *s) { size_t n=0; while(s[n]) n++; return n; }

static void puts(const char *s)
{
    _sc3(SYS_write, 1, (int64_t)(size_t)s, (int64_t)slen(s));
}

static void puts_fail(const char *s)
{
    _sc3(SYS_write, 2, (int64_t)(size_t)s, (int64_t)slen(s));
}

static void print_result(int pass, const char *name)
{
    if (pass) {
        puts("  PASS: ");
        puts(name);
        puts("\n");
    } else {
        puts_fail("  FAIL: ");
        puts_fail(name);
        puts_fail("\n");
    }
}

/* Write a NUL-terminated string to a file, return bytes written (excl NUL) */
static int64_t write_str(int64_t fd, const char *s)
{
    size_t len = slen(s);
    return _sc3(SYS_write, fd, (int64_t)(size_t)s, (int64_t)len);
}

/* Compare memory region with expected byte pattern */
static int memeq(const volatile uint8_t *p, uint8_t val, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (p[i] != val) return 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Test: mmap file read (MAP_PRIVATE | PROT_READ)                     */
/* ------------------------------------------------------------------ */
static int test_mmap_file_read(void)
{
    puts("  Testing: mmap_file_read...\n");

    const char *path = "/test_mmap_file.txt";
    const char *content = "Hello, LiteNix File-backed mmap!\n";

    /* Create and write file */
    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        puts_fail("    FAIL: openat O_CREAT failed\n");
        return 0;
    }

    int64_t written = write_str(fd, content);
    _sc1(SYS_close, fd);

    if (written != (int64_t)slen(content)) {
        puts_fail("    FAIL: write bytes != expected\n");
        return 0;
    }

    /* Re-open for reading */
    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) {
        puts_fail("    FAIL: openat O_RDONLY failed\n");
        return 0;
    }

    /* Get file size */
    struct stat st;
    int64_t r = _sc4(SYS_newfstatat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                      (int64_t)(size_t)&st, 0);
    if (r < 0) {
        puts_fail("    FAIL: fstatat failed\n");
        _sc1(SYS_close, fd);
        return 0;
    }

    /* mmap the file */
    int64_t map_addr = _sc6(SYS_mmap, 0, (int64_t)st.st_size,
                             PROT_READ, MAP_PRIVATE, fd, 0);
    _sc1(SYS_close, fd);  /* close fd, mapping should remain valid */

    if (map_addr < 0) {
        puts_fail("    FAIL: mmap returned < 0\n");
        return 0;
    }

    /* Verify contents match */
    const volatile char *mapped = (const volatile char *)(size_t)map_addr;
    int ok = 1;
    size_t i = 0;
    while (content[i] && i < (size_t)st.st_size) {
        if (mapped[i] != content[i]) {
            ok = 0;
            break;
        }
        i++;
    }
    if (i != (size_t)st.st_size || content[i] != '\0') {
        ok = 0;
    }

    /* Cleanup */
    _sc2(SYS_munmap, map_addr, (int64_t)st.st_size);

    /* Remove test file */
    /* unlink would be nice but not critical for test */

    return ok;
}

/* ------------------------------------------------------------------ */
/* Test: mmap with page-aligned offset                                 */
/* ------------------------------------------------------------------ */
static int test_mmap_file_offset(void)
{
    puts("  Testing: mmap_file_offset...\n");

    const char *path = "/test_mmap_offset.txt";
    /* Simple 2-page file: page 0 = 'X', page 1 = 'Y' */
    char buf[4096 * 2];
    for (size_t i = 0; i < 4096; i++) buf[i] = 'X';
    for (size_t i = 4096; i < 4096 * 2; i++) buf[i] = 'Y';

    /* Create file */
    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        puts_fail("    FAIL: openat O_CREAT failed\n");
        return 0;
    }

    /* Write in 4 KiB chunks because kernel READ_MAX_BUF is 4096 */
    size_t written = 0;
    while (written < sizeof(buf)) {
        size_t chunk = sizeof(buf) - written;
        if (chunk > 4096) chunk = 4096;
        int64_t n = _sc3(SYS_write, fd, (int64_t)(size_t)(buf + written), (int64_t)chunk);
        if (n < 0) {
            puts_fail("    FAIL: write failed\n");
            _sc1(SYS_close, fd);
            return 0;
        }
        written += (size_t)n;
    }
    _sc1(SYS_close, fd);

    /* Re-open */
    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) {
        puts_fail("    FAIL: openat O_RDONLY failed\n");
        return 0;
    }

    /* mmap starting at offset 4096 (page 1) - should read 'Y's */
    int64_t page_size = 4096;
    int64_t map_addr = _sc6(SYS_mmap, 0, (int64_t)page_size,
                             PROT_READ, MAP_PRIVATE, fd, (int64_t)page_size);
    _sc1(SYS_close, fd);

    if (map_addr < 0) {
        puts("    FAIL: mmap with offset returned < 0\n");
        return 0;
    }

    const volatile uint8_t *mapped = (const volatile uint8_t *)(size_t)map_addr;

    /* First byte of mapping should be 'Y' (page 1 of file) */
    int ok = (mapped[0] == 'Y');
    if (!ok) {
        puts_fail("    FAIL: mapped[0] != 'Y' (got: ");
        /* Print first 8 bytes as hex */
        for (int i = 0; i < 8; i++) {
            uint8_t b = mapped[i];
            char hex[4];
            const char *hex_chars = "0123456789ABCDEF";
            hex[0] = hex_chars[b >> 4];
            hex[1] = hex_chars[b & 0xF];
            hex[2] = ' ';
            hex[3] = '\0';
            puts_fail(hex);
        }
        puts_fail("...)\n");
    }

    ok = ok && memeq(mapped, 'Y', page_size);

    _sc2(SYS_munmap, map_addr, (int64_t)page_size);

    return ok;
}

/* ------------------------------------------------------------------ */
/* Test: mmap beyond EOF returns zero-filled pages                     */
/* ------------------------------------------------------------------ */
static int test_mmap_file_beyond_eof(void)
{
    puts("  Testing: mmap_file_beyond_eof...\n");

    const char *path = "/test_mmap_eof.txt";
    const char *short_content = "SHORT";
    size_t content_len = 5;

    /* Create small file */
    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    write_str(fd, short_content);
    _sc1(SYS_close, fd);

    /* mmap more than file size */
    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) return 0;

    int64_t page_size = 4096;
    int64_t map_len = page_size * 2;  /* 2 pages, file is only 5 bytes */
    int64_t map_addr = _sc6(SYS_mmap, 0, map_len, PROT_READ, MAP_PRIVATE, fd, 0);
    _sc1(SYS_close, fd);

    if (map_addr < 0) {
        puts_fail("    FAIL: mmap beyond EOF returned < 0\n");
        return 0;
    }

    const volatile uint8_t *mapped = (const volatile uint8_t *)(size_t)map_addr;

    /* First 5 bytes should be file content */
    int ok = 1;
    ok = ok && (mapped[0] == 'S');
    ok = ok && (mapped[1] == 'H');
    ok = ok && (mapped[2] == 'O');
    ok = ok && (mapped[3] == 'R');
    ok = ok && (mapped[4] == 'T');

    /* Rest should be zero */
    ok = ok && memeq(mapped + content_len, 0, (size_t)(map_len - content_len));

    _sc2(SYS_munmap, map_addr, map_len);

    return ok;
}

/* ------------------------------------------------------------------ */
/* Test: MAP_PRIVATE write doesn't modify underlying file (COW)        */
/* ------------------------------------------------------------------ */
static int test_mmap_private_write(void)
{
    puts("  Testing: mmap_private_write...\n");

    const char *path = "/test_mmap_priv_write.txt";
    const char *original = "Original content for COW test\n";

    /* Create file */
    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    write_str(fd, original);
    _sc1(SYS_close, fd);

    /* Re-open */
    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) return 0;

    int64_t map_addr = _sc6(SYS_mmap, 0, (int64_t)slen(original),
                             PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    _sc1(SYS_close, fd);

    if (map_addr < 0) {
        puts_fail("    FAIL: mmap PROT_READ|PROT_WRITE failed\n");
        return 0;
    }

    volatile uint8_t *mapped = (volatile uint8_t *)(size_t)map_addr;
    size_t len = slen(original);

    /* Modify first byte */
    mapped[0] = 'X';

    /* Verify we can read the modification in our mapping */
    if (mapped[0] != 'X') {
        puts_fail("    FAIL: write to private mapping not visible\n");
        _sc2(SYS_munmap, map_addr, (int64_t)len);
        return 0;
    }

    /* Re-open file and verify original content is unchanged */
    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) {
        _sc2(SYS_munmap, map_addr, (int64_t)len);
        return 0;
    }

    char read_buf[64];
    int64_t nr = _sc3(SYS_read, fd, (int64_t)(size_t)read_buf, (int64_t)(len + 1));
    _sc1(SYS_close, fd);
    _sc2(SYS_munmap, map_addr, (int64_t)len);

    /* File should still have original content */
    int ok = (nr == (int64_t)len);
    if (ok) {
        read_buf[len] = '\0';
        for (size_t i = 0; i < len; i++) {
            if (read_buf[i] != original[i]) {
                ok = 0;
                break;
            }
        }
    }

    return ok;
}

/* ------------------------------------------------------------------ */
/* Test: mmap with fd=-1 returns MAP_FAILED and EBADF                  */
/* ------------------------------------------------------------------ */
static int test_mmap_bad_fd(void)
{
    puts("  Testing: mmap_bad_fd...\n");

    /* mmap with valid flags but fd=-1 (not MAP_ANONYMOUS) should fail */
    int64_t map_addr = _sc6(SYS_mmap, 0, 4096, PROT_READ,
                             MAP_PRIVATE, -1LL, 0);

    /* Should return -EBADF (MAP_FAILED = (void*)-1) */
    int ok = (map_addr == -((int64_t)EBADF));

    if (!ok) {
        puts_fail("    FAIL: mmap with fd=-1 did not return -EBADF\n");
    }

    return ok;
}

/* ------------------------------------------------------------------ */
/* Test: mmap with misaligned offset (offset=1) returns EINVAL         */
/* ------------------------------------------------------------------ */
static int test_mmap_misaligned_offset(void)
{
    puts("  Testing: mmap_misaligned_offset...\n");

    const char *path = "/test_mmap_misaligned.txt";
    const char *content = "X";

    /* Create a small file */
    int64_t fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path,
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 0;
    write_str(fd, content);
    _sc1(SYS_close, fd);

    fd = _sc4(SYS_openat, (int64_t)AT_FDCWD, (int64_t)(size_t)path, O_RDONLY, 0);
    if (fd < 0) return 0;

    /* mmap with offset=1 (not page-aligned) */
    int64_t map_addr = _sc6(SYS_mmap, 0, 4096, PROT_READ,
                             MAP_PRIVATE, fd, 1);
    _sc1(SYS_close, fd);

    /* Should return -EINVAL */
    int ok = (map_addr == -((int64_t)EINVAL));

    if (!ok) {
        puts_fail("    FAIL: mmap with offset=1 did not return -EINVAL\n");
    }

    return ok;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void _start(void)
{
    puts("\n=== file-backed mmap test suite ===\n");

    int pass = 1;
    int result;

    result = test_mmap_file_read();
    print_result(result, "mmap_file_read");
    pass = pass && result;

    result = test_mmap_file_offset();
    print_result(result, "mmap_file_offset");
    pass = pass && result;

    result = test_mmap_file_beyond_eof();
    print_result(result, "mmap_file_beyond_eof");
    pass = pass && result;

    result = test_mmap_private_write();
    print_result(result, "mmap_private_write");
    pass = pass && result;

    result = test_mmap_bad_fd();
    print_result(result, "mmap_bad_fd");
    pass = pass && result;

    result = test_mmap_misaligned_offset();
    print_result(result, "mmap_misaligned_offset");
    pass = pass && result;

    puts("=== file-backed mmap tests done ===\n");

    if (pass) {
        puts("FILE_MMAP: all tests passed\n");
        do_exit(0);
    } else {
        puts_fail("FILE_MMAP: SOME TESTS FAILED\n");
        do_exit(1);
    }
}