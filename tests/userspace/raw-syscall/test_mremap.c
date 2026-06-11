/*
 * test_mremap.c — Tests for mremap() and madvise() syscalls.
 */
#include <stdint.h>
#include <stddef.h>

static long syscall3(long nr, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}
static long syscall1(long nr, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1) : "rcx","r11","memory");
    return r;
}
static long syscall5_r10_r8(long nr, long a1, long a2, long a3, long a4, long a5) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx","r11","memory");
    return r;
}
static long syscall6(long nr, long a1, long a2, long a3, long a4, long a5, long a6) {
    long r;
    register long r10 __asm__("r10") = a4;
    register long r8  __asm__("r8")  = a5;
    register long r9  __asm__("r9")  = a6;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx","r11","memory");
    return r;
}

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_mmap       9
#define SYS_munmap     11
#define SYS_mremap     25
#define SYS_madvise    28

#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define PROT_READ  1
#define PROT_WRITE 2
#define MREMAP_MAYMOVE 1
#define MADV_DONTNEED  4

static void write_str(const char *s) {
    size_t len = 0; while(s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}
static void write_ok(const char *n)   { write_str("[ OK ] "); write_str(n); write_str("\n"); }
static void write_fail(const char *n) { write_str("[FAIL] "); write_str(n); write_str("\n"); }

void _start(void) {
    write_str("=== mremap/madvise Tests ===\n");

    /* mmap 4096 bytes anonymously */
    long addr = syscall6(SYS_mmap, 0, 4096, PROT_READ|PROT_WRITE,
                         MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    if (addr > 0) write_ok("mmap 4096 bytes");
    else { write_fail("mmap"); syscall1(SYS_exit_group, 1); }

    /* Write and read back to confirm mapping is usable */
    *((volatile int *)addr) = 0x1234;
    if (*((volatile int *)addr) == 0x1234) write_ok("write/read mmap'd memory");
    else write_fail("mmap memory access");

    /* madvise DONTNEED — hint that pages can be freed */
    long ret = syscall3(SYS_madvise, addr, 4096, MADV_DONTNEED);
    if (ret == 0) write_ok("madvise DONTNEED");
    else write_fail("madvise DONTNEED");

    /* mremap: shrink from 4096 → 2048 bytes with MREMAP_MAYMOVE */
    long new_addr = syscall5_r10_r8(SYS_mremap, addr, 4096, 2048, MREMAP_MAYMOVE, 0);
    if (new_addr > 0) write_ok("mremap shrink (4096->2048)");
    else write_fail("mremap shrink");

    /* munmap the remapped region */
    ret = syscall3(SYS_munmap, new_addr, 2048, 0);
    if (ret == 0) write_ok("munmap after mremap");
    else write_fail("munmap after mremap");

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
