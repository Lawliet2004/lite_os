#define SYSCALL_TRACE
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sched/task.h>

/* Phase 33: kernel canary as a stack-corruption tripwire. The
 * syscall dispatcher touches the kernel stack and many kernel
 * data structures on every user→kernel transition. A simple
 * canary in BSS, checked on every syscall, catches:
 *   - stack overflows in the syscall path that scribble into
 *     the canary's address (a compiler/linker bug or a missed
 *     bounds check)
 *   - ROP gadgets that pivot the stack into BSS
 *   - any code that overwrites a "do not touch" sentinel
 *
 * Not a substitute for -fstack-protector (per-function canaries)
 * which is disabled by the build flags; it's a coarse catch-all
 * for the syscall path. The value is volatile so the compiler
 * doesn't elide the read. */
static volatile uint64_t kernel_canary = 0xDEADBEEFCAFEBABEULL;

#define KERNEL_CANARY_EXPECTED 0xDEADBEEFCAFEBABEULL

static inline void kernel_canary_check(void)
{
    if (kernel_canary != KERNEL_CANARY_EXPECTED) {
        panic("kernel canary corrupted (possible stack overflow or BSS scribble)");
    }
}

/* ------------------------------------------------------------------ */
/* Human-readable syscall name lookup                                  */
/* ------------------------------------------------------------------ */

static __attribute__((unused)) const char *syscall_name(unsigned nr)
{
    switch (nr) {
    /* ---- File I/O ---- */
    case SYS_read:       return "read";
    case SYS_write:      return "write";
    case SYS_readv:      return "readv";
    case SYS_writev:     return "writev";
    case SYS_open:       return "open";
    case SYS_openat:     return "openat";
    case SYS_close:      return "close";
    case SYS_stat:       return "stat";
    case SYS_lstat:      return "lstat";
    case SYS_fstat:      return "fstat";
    case SYS_newfstatat: return "newfstatat";
    case SYS_lseek:      return "lseek";
    case SYS_ioctl:      return "ioctl";
    case SYS_dup:        return "dup";
    case SYS_dup2:       return "dup2";
    case SYS_fcntl:      return "fcntl";
    case SYS_getdents64: return "getdents64";
    case SYS_access:     return "access";
    case SYS_faccessat:  return "faccessat";
    case SYS_readlink:   return "readlink";
    case SYS_readlinkat: return "readlinkat";
    case SYS_statx:      return "statx";
    case SYS_rename:     return "rename";
    case SYS_mkdir:      return "mkdir";
    case SYS_rmdir:      return "rmdir";
    case SYS_unlink:     return "unlink";
    case SYS_chdir:      return "chdir";
    case SYS_getcwd:     return "getcwd";
    case SYS_symlink:    return "symlink";
    case SYS_chmod:      return "chmod";
    case SYS_fchmod:     return "fchmod";
    case SYS_chown:      return "chown";
    case SYS_fchown:     return "fchown";
    case SYS_lchown:     return "lchown";
    case SYS_umask:      return "umask";
    case SYS_mkdirat:    return "mkdirat";
    case SYS_chownat:    return "fchownat";
    case SYS_unlinkat:   return "unlinkat";
    case SYS_renameat:   return "renameat";
    case SYS_symlinkat:  return "symlinkat";
    case SYS_fchmodat:   return "fchmodat";
    case SYS_dup3:       return "dup3";
    case SYS_renameat2:  return "renameat2";
    case SYS_mount:      return "mount";
    case SYS_umount2:    return "umount2";

    /* ---- Memory ---- */
    case SYS_mmap:       return "mmap";
    case SYS_mprotect:   return "mprotect";
    case SYS_munmap:     return "munmap";
    case SYS_brk:        return "brk";

    /* ---- Process ---- */
    case SYS_getpid:     return "getpid";
    case SYS_gettid:     return "gettid";
    case SYS_getppid:    return "getppid";
    case SYS_getuid:     return "getuid";
    case SYS_getgid:     return "getgid";
    case SYS_geteuid:    return "geteuid";
    case SYS_getegid:    return "getegid";
    case SYS_fork:       return "fork";
    case SYS_execve:     return "execve";
    case SYS_exit:       return "exit";
    case SYS_exit_group: return "exit_group";
    case SYS_wait4:      return "wait4";
    case SYS_clone:      return "clone";
    case SYS_arch_prctl: return "arch_prctl";
    case SYS_set_tid_address: return "set_tid_address";
    case SYS_setpgid:    return "setpgid";
    case SYS_getpgid:    return "getpgid";
    case SYS_setsid:     return "setsid";
    case SYS_getsid:     return "getsid";

    /* ---- Signals ---- */
    case SYS_rt_sigaction:  return "rt_sigaction";
    case SYS_rt_sigprocmask: return "rt_sigprocmask";
    case SYS_rt_sigreturn:  return "rt_sigreturn";
    case SYS_kill:          return "kill";

    /* ---- Threads & Sync ---- */
    case SYS_futex:         return "futex";
    case SYS_set_robust_list:  return "set_robust_list";
    case SYS_get_robust_list:  return "get_robust_list";
    case SYS_getrandom:     return "getrandom";

    /* ---- Pipes & Poll ---- */
    case SYS_pipe:          return "pipe";
    case SYS_pipe2:         return "pipe2";
    case SYS_poll:          return "poll";

    /* ---- Time ---- */
    case SYS_clock_gettime:  return "clock_gettime";
    case SYS_gettimeofday:   return "gettimeofday";
    case SYS_nanosleep:      return "nanosleep";
    case SYS_uname:          return "uname";
    case SYS_getrlimit:      return "getrlimit";
    case SYS_prlimit64:      return "prlimit64";
    case SYS_reboot:         return "reboot";

    /* ---- Sockets ---- */
    case SYS_socket:       return "socket";
    case SYS_connect:      return "connect";
    case SYS_accept:       return "accept";
    case SYS_sendto:       return "sendto";
    case SYS_recvfrom:     return "recvfrom";
    case SYS_bind:         return "bind";
    case SYS_listen:       return "listen";
    case SYS_select:       return "select";
    case SYS_pselect6:     return "pselect6";
    case SYS_epoll_create1: return "epoll_create1";
    case SYS_epoll_ctl:    return "epoll_ctl";
    case SYS_epoll_wait:   return "epoll_wait";
    case SYS_sendmsg:      return "sendmsg";
    case SYS_recvmsg:      return "recvmsg";
    case SYS_socketpair:   return "socketpair";
    case SYS_setsockopt:   return "setsockopt";
    case SYS_getsockopt:   return "getsockopt";
    case SYS_shutdown:     return "shutdown";
    case SYS_mremap:       return "mremap";
    case SYS_madvise:      return "madvise";
    case SYS_prctl:        return "prctl";
    case SYS_getresuid:    return "getresuid";
    case SYS_setresuid:    return "setresuid";
    case SYS_getresgid:    return "getresgid";
    case SYS_setresgid:    return "setresgid";

    default:               return "unknown";
    }
}

/* ------------------------------------------------------------------ */
/* Human-readable errno name lookup                                    */
/* ------------------------------------------------------------------ */

static __attribute__((unused)) const char *errno_name(int64_t err)
{
    /* err is the negative errno value returned by the syscall */
    switch (-err) {
    case EPERM:       return "EPERM";
    case ENOENT:      return "ENOENT";
    case ESRCH:       return "ESRCH";
    case EINTR:       return "EINTR";
    case EIO:         return "EIO";
    case ENXIO:       return "ENXIO";
    case E2BIG:       return "E2BIG";
    case EBADF:       return "EBADF";
    case ECHILD:      return "ECHILD";
    case ENOMEM:      return "ENOMEM";
    case EACCES:      return "EACCES";
    case EFAULT:      return "EFAULT";
    case ENOTBLK:     return "ENOTBLK";
    case EBUSY:       return "EBUSY";
    case EEXIST:      return "EEXIST";
    case EXDEV:       return "EXDEV";
    case ENODEV:      return "ENODEV";
    case ENOTDIR:     return "ENOTDIR";
    case EISDIR:      return "EISDIR";
    case EINVAL:      return "EINVAL";
    case ENFILE:      return "ENFILE";
    case EMFILE:      return "EMFILE";
    case ENOSPC:      return "ENOSPC";
    case EPIPE:       return "EPIPE";
    case ERANGE:      return "ERANGE";
    case ENOSYS:      return "ENOSYS";
    case ENOTEMPTY:   return "ENOTEMPTY";
    case ELOOP:       return "ELOOP";
    case ENAMETOOLONG: return "ENAMETOOLONG";
    case EAGAIN:      return "EAGAIN";
    case ENOEXEC:     return "ENOEXEC";
    case EOVERFLOW:   return "EOVERFLOW";
    case ENOTSOCK:    return "ENOTSOCK";
    case EDESTADDRREQ: return "EDESTADDRREQ";
    case 95:          return "EOPNOTSUPP/ENOTSUP";  /* EOPNOTSUPP == ENOTSUP == 95 */
    case EAFNOSUPPORT: return "EAFNOSUPPORT";
    case EISCONN:     return "EISCONN";
    case ENOTCONN:    return "ENOTCONN";
    case 111:         return "ECONNREFUSED";
    default:          return "EUNKNOWN";
    }
}

/* ------------------------------------------------------------------ */
/* Forward declarations of all syscall handlers                        */
/* ------------------------------------------------------------------ */

/* fs/file syscalls (sys_file.c) */
int64_t sys_read(struct syscall_frame *frame);
int64_t sys_write(struct syscall_frame *frame);
int64_t sys_open(struct syscall_frame *frame);
int64_t sys_openat(struct syscall_frame *frame);
int64_t sys_close(struct syscall_frame *frame);
int64_t sys_stat(struct syscall_frame *frame);
int64_t sys_lstat(struct syscall_frame *frame);
int64_t sys_fstat(struct syscall_frame *frame);
int64_t sys_newfstatat(struct syscall_frame *frame);
int64_t sys_lseek(struct syscall_frame *frame);
int64_t sys_ioctl(struct syscall_frame *frame);
int64_t sys_dup(struct syscall_frame *frame);
int64_t sys_dup2(struct syscall_frame *frame);
int64_t sys_fcntl(struct syscall_frame *frame);
int64_t sys_getdents64(struct syscall_frame *frame);
int64_t sys_mkdir(struct syscall_frame *frame);
int64_t sys_rmdir(struct syscall_frame *frame);
int64_t sys_unlink(struct syscall_frame *frame);
int64_t sys_rename(struct syscall_frame *frame);
int64_t sys_execve(struct syscall_frame *frame);
int64_t sys_access(struct syscall_frame *frame);
int64_t sys_faccessat(struct syscall_frame *frame);
int64_t sys_readlink(struct syscall_frame *frame);
int64_t sys_readlinkat(struct syscall_frame *frame);
int64_t sys_statx(struct syscall_frame *frame);
int64_t sys_readv(struct syscall_frame *frame);
int64_t sys_writev(struct syscall_frame *frame);
int64_t sys_symlink(struct syscall_frame *frame);
int64_t sys_chmod(struct syscall_frame *frame);
int64_t sys_fchmod(struct syscall_frame *frame);
int64_t sys_chown(struct syscall_frame *frame);
int64_t sys_fchown(struct syscall_frame *frame);
int64_t sys_lchown(struct syscall_frame *frame);
int64_t sys_umask(struct syscall_frame *frame);
int64_t sys_mkdirat(struct syscall_frame *frame);
int64_t sys_chownat(struct syscall_frame *frame);
int64_t sys_unlinkat(struct syscall_frame *frame);
int64_t sys_renameat(struct syscall_frame *frame);
int64_t sys_symlinkat(struct syscall_frame *frame);
int64_t sys_fchmodat(struct syscall_frame *frame);
int64_t sys_dup3(struct syscall_frame *frame);
int64_t sys_renameat2(struct syscall_frame *frame);
int64_t sys_mount(struct syscall_frame *frame);
int64_t sys_umount2(struct syscall_frame *frame);

/* process lifecycle (sys_exit.c) */
int64_t sys_exit(struct syscall_frame *frame);
int64_t sys_exit_group(struct syscall_frame *frame);
int64_t sys_fork(struct syscall_frame *frame);
int64_t sys_wait4(struct syscall_frame *frame);

/* process identity (sys_process.c) */
int64_t sys_getpid(struct syscall_frame *frame);
int64_t sys_gettid(struct syscall_frame *frame);
int64_t sys_getppid(struct syscall_frame *frame);
int64_t sys_getuid(struct syscall_frame *frame);
int64_t sys_getgid(struct syscall_frame *frame);
int64_t sys_geteuid(struct syscall_frame *frame);
int64_t sys_getegid(struct syscall_frame *frame);
int64_t sys_setpgid(struct syscall_frame *frame);
int64_t sys_getpgid(struct syscall_frame *frame);
int64_t sys_setsid(struct syscall_frame *frame);
int64_t sys_getsid(struct syscall_frame *frame);
int64_t sys_chdir(struct syscall_frame *frame);
int64_t sys_getcwd(struct syscall_frame *frame);
int64_t sys_clone(struct syscall_frame *frame);
int64_t sys_arch_prctl(struct syscall_frame *frame);
int64_t sys_futex(struct syscall_frame *frame);
int64_t sys_set_tid_address(struct syscall_frame *frame);
int64_t sys_pipe(struct syscall_frame *frame);
int64_t sys_pipe2(struct syscall_frame *frame);
int64_t sys_poll(struct syscall_frame *frame);
int64_t sys_clock_gettime(struct syscall_frame *frame);
int64_t sys_gettimeofday(struct syscall_frame *frame);
int64_t sys_nanosleep(struct syscall_frame *frame);
int64_t sys_uname(struct syscall_frame *frame);
int64_t sys_set_robust_list(struct syscall_frame *frame);
int64_t sys_get_robust_list(struct syscall_frame *frame);
int64_t sys_getrandom(struct syscall_frame *frame);
int64_t sys_getrlimit(struct syscall_frame *frame);
int64_t sys_prlimit64(struct syscall_frame *frame);
int64_t sys_reboot(struct syscall_frame *frame);

/* Sockets (Phase 24) */
int64_t sys_socket(struct syscall_frame *frame);
int64_t sys_connect(struct syscall_frame *frame);
int64_t sys_accept(struct syscall_frame *frame);
int64_t sys_sendto(struct syscall_frame *frame);
int64_t sys_recvfrom(struct syscall_frame *frame);
int64_t sys_bind(struct syscall_frame *frame);
int64_t sys_listen(struct syscall_frame *frame);

/* Epoll and select (sys_epoll.c) */
int64_t sys_select(struct syscall_frame *frame);
int64_t sys_pselect6(struct syscall_frame *frame);
int64_t sys_epoll_create1(struct syscall_frame *frame);
int64_t sys_epoll_ctl(struct syscall_frame *frame);
int64_t sys_epoll_wait(struct syscall_frame *frame);

/* Process credentials (sys_process.c) */
int64_t sys_getresuid(struct syscall_frame *frame);
int64_t sys_setresuid(struct syscall_frame *frame);
int64_t sys_getresgid(struct syscall_frame *frame);
int64_t sys_setresgid(struct syscall_frame *frame);
int64_t sys_prctl(struct syscall_frame *frame);

/* Extended memory (sys_mem.c) */
int64_t sys_mremap(struct syscall_frame *frame);
int64_t sys_madvise(struct syscall_frame *frame);

/* Extended sockets (socket.c) */
int64_t sys_sendmsg(struct syscall_frame *frame);
int64_t sys_recvmsg(struct syscall_frame *frame);
int64_t sys_socketpair(struct syscall_frame *frame);
int64_t sys_setsockopt(struct syscall_frame *frame);
int64_t sys_getsockopt(struct syscall_frame *frame);
int64_t sys_shutdown(struct syscall_frame *frame);

/* Signals */
int64_t sys_rt_sigaction(struct syscall_frame *frame);
int64_t sys_rt_sigprocmask(struct syscall_frame *frame);
int64_t sys_rt_sigreturn(struct syscall_frame *frame);
int64_t sys_kill(struct syscall_frame *frame);

/* memory (sys_mem.c) */
int64_t sys_brk(struct syscall_frame *frame);
int64_t sys_mmap(struct syscall_frame *frame);
int64_t sys_munmap(struct syscall_frame *frame);
int64_t sys_mprotect(struct syscall_frame *frame);

/* ------------------------------------------------------------------ */
/* Dispatch table                                                       */
/* ------------------------------------------------------------------ */

static syscall_fn_t syscall_table[SYSCALL_TABLE_SIZE];

static int64_t sys_enosys(struct syscall_frame *frame)
{
    (void)frame;
    return -(int64_t)ENOSYS;
}

void syscall_table_init(void)
{
    /* Fill all entries with -ENOSYS stub */
    for (int i = 0; i < SYSCALL_TABLE_SIZE; i++) {
        syscall_table[i] = sys_enosys;
    }

    /* ---- Memory ---- */
    syscall_table[SYS_mmap]      = sys_mmap;
    syscall_table[SYS_mprotect]  = sys_mprotect;
    syscall_table[SYS_munmap]    = sys_munmap;
    syscall_table[SYS_brk]       = sys_brk;

    /* ---- File I/O ---- */
    syscall_table[SYS_read]      = sys_read;
    syscall_table[SYS_write]     = sys_write;
    syscall_table[SYS_readv]     = sys_readv;
    syscall_table[SYS_writev]    = sys_writev;
    syscall_table[SYS_open]      = sys_open;
    syscall_table[SYS_openat]    = sys_openat;
    syscall_table[SYS_close]     = sys_close;
    syscall_table[SYS_stat]      = sys_stat;
    syscall_table[SYS_lstat]     = sys_lstat;
    syscall_table[SYS_fstat]     = sys_fstat;
    syscall_table[SYS_newfstatat]= sys_newfstatat;
    syscall_table[SYS_lseek]     = sys_lseek;
    syscall_table[SYS_ioctl]     = sys_ioctl;
    syscall_table[SYS_dup]       = sys_dup;
    syscall_table[SYS_dup2]      = sys_dup2;
    syscall_table[SYS_fcntl]     = sys_fcntl;
    syscall_table[SYS_getdents64]= sys_getdents64;
    syscall_table[SYS_access]    = sys_access;
    syscall_table[SYS_faccessat] = sys_faccessat;
    syscall_table[SYS_readlink]  = sys_readlink;
    syscall_table[SYS_readlinkat]= sys_readlinkat;
    syscall_table[SYS_statx]     = sys_statx;

    /* ---- Directory / filesystem ---- */
    syscall_table[SYS_rename]    = sys_rename;
    syscall_table[SYS_mkdir]     = sys_mkdir;
    syscall_table[SYS_rmdir]     = sys_rmdir;
    syscall_table[SYS_unlink]    = sys_unlink;
    syscall_table[SYS_chdir]     = sys_chdir;
    syscall_table[SYS_getcwd]    = sys_getcwd;
    syscall_table[SYS_symlink]   = sys_symlink;
    syscall_table[SYS_chmod]     = sys_chmod;
    syscall_table[SYS_fchmod]    = sys_fchmod;
    syscall_table[SYS_chown]     = sys_chown;
    syscall_table[SYS_fchown]    = sys_fchown;
    syscall_table[SYS_lchown]    = sys_lchown;
    syscall_table[SYS_umask]     = sys_umask;
    syscall_table[SYS_mkdirat]   = sys_mkdirat;
    syscall_table[SYS_chownat]   = sys_chownat;
    syscall_table[SYS_unlinkat]  = sys_unlinkat;
    syscall_table[SYS_renameat]  = sys_renameat;
    syscall_table[SYS_symlinkat] = sys_symlinkat;
    syscall_table[SYS_fchmodat]  = sys_fchmodat;
    syscall_table[SYS_dup3]      = sys_dup3;
    syscall_table[SYS_renameat2] = sys_renameat2;
    syscall_table[SYS_mount]     = sys_mount;
    syscall_table[SYS_umount2]   = sys_umount2;

    /* ---- Process ---- */
    syscall_table[SYS_getpid]    = sys_getpid;
    syscall_table[SYS_gettid]    = sys_gettid;
    syscall_table[SYS_getppid]   = sys_getppid;
    syscall_table[SYS_getuid]    = sys_getuid;
    syscall_table[SYS_getgid]    = sys_getgid;
    syscall_table[SYS_geteuid]   = sys_geteuid;
    syscall_table[SYS_getegid]   = sys_getegid;
    syscall_table[SYS_fork]      = sys_fork;
    syscall_table[SYS_execve]    = sys_execve;
    syscall_table[SYS_exit]      = sys_exit;
    syscall_table[SYS_exit_group]= sys_exit_group;
    syscall_table[SYS_wait4]     = sys_wait4;
    syscall_table[SYS_setpgid]   = sys_setpgid;
    syscall_table[SYS_getpgid]   = sys_getpgid;
    syscall_table[SYS_setsid]    = sys_setsid;
    syscall_table[SYS_getsid]    = sys_getsid;

    /* ---- Signals ---- */
    syscall_table[SYS_rt_sigaction]   = sys_rt_sigaction;
    syscall_table[SYS_rt_sigprocmask]  = sys_rt_sigprocmask;
    syscall_table[SYS_rt_sigreturn]    = sys_rt_sigreturn;
    syscall_table[SYS_kill]            = sys_kill;

    /* ---- Threads & TLS ---- */
    syscall_table[SYS_clone]            = sys_clone;
    syscall_table[SYS_arch_prctl]        = sys_arch_prctl;
    syscall_table[SYS_futex]             = sys_futex;
    syscall_table[SYS_set_tid_address]   = sys_set_tid_address;
    syscall_table[SYS_set_robust_list]   = sys_set_robust_list;
    syscall_table[SYS_get_robust_list]   = sys_get_robust_list;
    syscall_table[SYS_getrandom]         = sys_getrandom;

    /* ---- Pipes & Poll ---- */
    syscall_table[SYS_pipe]             = sys_pipe;
    syscall_table[SYS_pipe2]            = sys_pipe2;
    syscall_table[SYS_poll]             = sys_poll;

    /* ---- Timekeeping ---- */
    syscall_table[SYS_clock_gettime]    = sys_clock_gettime;
    syscall_table[SYS_gettimeofday]     = sys_gettimeofday;
    syscall_table[SYS_nanosleep]        = sys_nanosleep;
    syscall_table[SYS_uname]            = sys_uname;
    syscall_table[SYS_getrlimit]         = sys_getrlimit;
    syscall_table[SYS_prlimit64]         = sys_prlimit64;
    syscall_table[SYS_reboot]            = sys_reboot;

    /* ---- Sockets ---- */
    syscall_table[SYS_socket]            = sys_socket;
    syscall_table[SYS_connect]           = sys_connect;
    syscall_table[SYS_accept]            = sys_accept;
    syscall_table[SYS_sendto]            = sys_sendto;
    syscall_table[SYS_recvfrom]          = sys_recvfrom;
    syscall_table[SYS_bind]              = sys_bind;
    syscall_table[SYS_listen]            = sys_listen;

    /* ---- Epoll and Select ---- */
    syscall_table[SYS_select]        = sys_select;
    syscall_table[SYS_pselect6]      = sys_pselect6;
    syscall_table[SYS_epoll_wait]    = sys_epoll_wait;
    syscall_table[SYS_epoll_ctl]     = sys_epoll_ctl;
    syscall_table[SYS_epoll_create1] = sys_epoll_create1;

    /* ---- Process Credentials ---- */
    syscall_table[SYS_getresuid]     = sys_getresuid;
    syscall_table[SYS_setresuid]     = sys_setresuid;
    syscall_table[SYS_getresgid]     = sys_getresgid;
    syscall_table[SYS_setresgid]     = sys_setresgid;
    syscall_table[SYS_prctl]         = sys_prctl;

    /* ---- Extended Memory ---- */
    syscall_table[SYS_mremap]        = sys_mremap;
    syscall_table[SYS_madvise]       = sys_madvise;

    /* ---- Extended Sockets ---- */
    syscall_table[SYS_sendmsg]       = sys_sendmsg;
    syscall_table[SYS_recvmsg]       = sys_recvmsg;
    syscall_table[SYS_socketpair]    = sys_socketpair;
    syscall_table[SYS_setsockopt]    = sys_setsockopt;
    syscall_table[SYS_getsockopt]    = sys_getsockopt;
    syscall_table[SYS_shutdown]      = sys_shutdown;
}

int64_t syscall_dispatch(struct syscall_frame *frame)
{
    /* Phase 33: tripwire for stack corruption in the syscall path. */
    kernel_canary_check();

    uint64_t nr = frame->rax;
    int64_t ret;

    if (nr >= SYSCALL_TABLE_SIZE) {
        ret = -(int64_t)ENOSYS;
    } else {
        ret = syscall_table[nr](frame);
    }

#ifdef SYSCALL_TRACE
    if (nr != SYS_write) {
        printk("syscall: nr=%llu name=%s args=(rdi=%llx rsi=%llx rdx=%llx r10=%llx r8=%llx r9=%llx) ret=%lld\n",
               (unsigned long long)nr,
               syscall_name(nr),
               (unsigned long long)frame->rdi,
               (unsigned long long)frame->rsi,
               (unsigned long long)frame->rdx,
               (unsigned long long)frame->r10,
               (unsigned long long)frame->r8,
               (unsigned long long)frame->r9,
               (long long)ret);
    }
#endif

    frame->rax = ret;

    if (current_task && current_task->mode == TASK_MODE_USER) {
        task_deliver_signals(frame);
    }

    return ret;
}

__attribute__((weak)) int64_t sys_mremap(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_madvise(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_sendmsg(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_recvmsg(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_socketpair(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_setsockopt(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_getsockopt(struct syscall_frame *frame) { (void)frame; return -38; }
__attribute__((weak)) int64_t sys_shutdown(struct syscall_frame *frame) { (void)frame; return -38; }
