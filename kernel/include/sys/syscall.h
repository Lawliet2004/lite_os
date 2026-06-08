#ifndef LITENIX_SYS_SYSCALL_H
#define LITENIX_SYS_SYSCALL_H

#include <stdint.h>

/*
 * Linux x86_64 errno values (negative convention).
 * Handlers return negative errno on error, non-negative on success.
 */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define E2BIG    7
#define EBADF    9
#define ECHILD   10
#define ENOMEM  12
#define EACCES  13
#define EFAULT  14
#define ENOTBLK 15
#define EBUSY   16
#define EEXIST  17
#define EXDEV   18
#define ENODEV  19
#define ENOTDIR 20
#define EISDIR  21
#define EINVAL  22
#define ENFILE  23
#define EMFILE  24
#define ENOSPC  28
#define EPIPE   32
#define ERANGE  34
#define ENOSYS  38
#define ENOTEMPTY 39
#define ELOOP   40
#define ENAMETOOLONG 36
#define EWOULDBLOCK 11
#define EAGAIN  11
#define ENOEXEC 8
#define EOVERFLOW 75

#define ENOTSOCK      88
#define EDESTADDRREQ  89
#define EOPNOTSUPP    95
#define EAFNOSUPPORT  97
#define EISCONN       106
#define ENOTCONN      107
#define ENOTSUP 95

/*
 * wait4 options and status macros — Linux ABI.
 */
#define WNOHANG    1
#define WUNTRACED  2
/* Encode exit status for wait4 wstatus (normal exit: exit_code shifted left 8) */
#define W_EXITCODE(code)   (((code) & 0xff) << 8)
/* Macros for decoding wstatus in userspace (see libc_lite.h) */
#define WIFEXITED(s)       (((s) & 0x7f) == 0)
#define WEXITSTATUS(s)     (((s) >> 8) & 0xff)
#define WIFSIGNALED(s)     (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)        ((s) & 0x7f)

/*
 * Linux x86_64 syscall numbers.
 */
#define SYS_read          0
#define SYS_write         1
#define SYS_open          2
#define SYS_close         3
#define SYS_stat          4
#define SYS_fstat         5
#define SYS_lstat         6
#define SYS_poll          7
#define SYS_lseek         8
#define SYS_mmap          9
#define SYS_mprotect     10
#define SYS_munmap       11
#define SYS_brk          12
#define SYS_ioctl        16
#define SYS_access       21
#define SYS_dup          32
#define SYS_dup2         33
#define SYS_uname        63
#define SYS_getpid       39
#define SYS_gettid      186
#define SYS_fork         57
#define SYS_execve       59
#define SYS_exit         60
#define SYS_wait4        61
#define SYS_fcntl        72
#define SYS_getcwd       79
#define SYS_chdir        80
#define SYS_rename       82
#define SYS_mkdir        83
#define SYS_rmdir        84
#define SYS_unlink       87
#define SYS_readlink     89
#define SYS_getrlimit    97
#define SYS_getppid     110
#define SYS_getuid       102
#define SYS_getgid       104
#define SYS_geteuid      107
#define SYS_getegid      108
#define SYS_rt_sigaction  13
#define SYS_rt_sigprocmask 14
#define SYS_kill          62
#define SYS_exit_group  231
#define SYS_getdents64  217
#define SYS_openat      257
#define SYS_readlinkat  267
#define SYS_faccessat   269
#define SYS_newfstatat  262
#define SYS_clone         56
#define SYS_arch_prctl   158
#define SYS_futex        202
#define SYS_set_tid_address 218
#define SYS_set_robust_list 273
#define SYS_get_robust_list 274
#define SYS_prlimit64   302
#define SYS_getrandom   318
#define SYS_statx       332
#define SYS_pipe         22
#define SYS_pipe2       293
#define SYS_socket        41
#define SYS_connect       42
#define SYS_accept        43
#define SYS_sendto        44
#define SYS_recvfrom      45
#define SYS_bind          49
#define SYS_listen        50
#define SYS_nanosleep     35
#define SYS_gettimeofday  96
#define SYS_clock_gettime 228

/*
 * fcntl commands
 */
#define F_DUPFD     0
#define F_GETFD     1
#define F_SETFD     2
#define F_GETFL     3
#define F_SETFL     4
#define FD_CLOEXEC  1

/*
 * struct syscall_frame — register state saved by syscall_stub.S.
 *
 * Layout must exactly match the push order in syscall_stub.S:
 *   push r15, r14, r13, r12, rbp, rbx,
 *   push r11, r10, r9, r8, rdx, rsi, rdi, rcx, rax  (top of stack)
 *
 * rax  = syscall number on entry; overwritten with return value before SYSRETQ
 * rcx  = user RIP (saved by SYSCALL instruction)
 * r11  = user RFLAGS (saved by SYSCALL instruction)
 * r10  = arg4 (Linux ABI uses r10 instead of rcx for arg4)
 */
struct syscall_frame {
    uint64_t rax;   /* syscall number / return value */
    uint64_t rcx;   /* user RIP */
    uint64_t rdi;   /* arg1 */
    uint64_t rsi;   /* arg2 */
    uint64_t rdx;   /* arg3 */
    uint64_t r8;    /* arg5 */
    uint64_t r9;    /* arg6 */
    uint64_t r10;   /* arg4 */
    uint64_t r11;   /* user RFLAGS */
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t user_rsp; /* user RSP */
};

#endif
