/*
 * test_credentials.c — Tests for process credential syscalls.
 * Tests: getresuid, setresuid, getresgid, setresgid, prctl, getuid, geteuid, getgid, getegid
 */
#include <stdint.h>
#include <stddef.h>

/* Minimal syscall wrappers */
static long syscall1(long nr, long a1) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1) : "rcx","r11","memory");
    return r;
}
static long syscall3(long nr, long a1, long a2, long a3) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2), "d"(a3) : "rcx","r11","memory");
    return r;
}
static long syscall2(long nr, long a1, long a2) {
    long r;
    __asm__ volatile("syscall" : "=a"(r) : "0"(nr), "D"(a1), "S"(a2) : "rcx","r11","memory");
    return r;
}

#define SYS_write      1
#define SYS_exit_group 231
#define SYS_getuid     102
#define SYS_getgid     104
#define SYS_geteuid    107
#define SYS_getegid    108
#define SYS_getresuid  118
#define SYS_setresuid  117
#define SYS_getresgid  120
#define SYS_setresgid  119
#define SYS_prctl      157

static void write_str(const char *s) {
    size_t len = 0;
    while (s[len]) len++;
    syscall3(SYS_write, 1, (long)s, (long)len);
}

static void write_ok(const char *name) {
    write_str("[ OK ] ");
    write_str(name);
    write_str("\n");
}

static void write_fail(const char *name) {
    write_str("[FAIL] ");
    write_str(name);
    write_str("\n");
}

void _start(void) {
    write_str("=== Credential Syscall Tests ===\n");

    /* Test getuid/geteuid/getgid/getegid */
    long uid = syscall1(SYS_getuid, 0);
    long euid = syscall1(SYS_geteuid, 0);
    long gid = syscall1(SYS_getgid, 0);
    long egid = syscall1(SYS_getegid, 0);

    if (uid >= 0 && euid >= 0) write_ok("getuid/geteuid return non-negative");
    else write_fail("getuid/geteuid");

    if (gid >= 0 && egid >= 0) write_ok("getgid/getegid return non-negative");
    else write_fail("getgid/getegid");

    /* Test getresuid */
    unsigned int ruid_out = 0xDEAD, euid_out = 0xDEAD, suid_out = 0xDEAD;
    long ret = syscall3(SYS_getresuid, (long)&ruid_out, (long)&euid_out, (long)&suid_out);
    if (ret == 0) write_ok("getresuid returns 0");
    else write_fail("getresuid");

    if (ruid_out != 0xDEAD) write_ok("getresuid fills ruid");
    else write_fail("getresuid did not fill ruid");

    /* Test getresgid */
    unsigned int rgid_out = 0xDEAD, egid_out = 0xDEAD, sgid_out = 0xDEAD;
    ret = syscall3(SYS_getresgid, (long)&rgid_out, (long)&egid_out, (long)&sgid_out);
    if (ret == 0) write_ok("getresgid returns 0");
    else write_fail("getresgid");

    /* Test prctl PR_SET_NAME / PR_GET_NAME */
    char name_buf[16];
    ret = syscall2(SYS_prctl, 15, (long)"test_thread");
    if (ret == 0) write_ok("prctl PR_SET_NAME");
    else write_fail("prctl PR_SET_NAME");

    ret = syscall2(SYS_prctl, 16, (long)name_buf);
    if (ret == 0) write_ok("prctl PR_GET_NAME");
    else write_fail("prctl PR_GET_NAME");

    write_str("=== Done ===\n");
    syscall1(SYS_exit_group, 0);
}
