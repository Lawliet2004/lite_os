#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <kernel/printk.h>
#include <sched/task.h>

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

/* Sockets (Phase 24) */
int64_t sys_socket(struct syscall_frame *frame);
int64_t sys_connect(struct syscall_frame *frame);
int64_t sys_accept(struct syscall_frame *frame);
int64_t sys_sendto(struct syscall_frame *frame);
int64_t sys_recvfrom(struct syscall_frame *frame);
int64_t sys_bind(struct syscall_frame *frame);
int64_t sys_listen(struct syscall_frame *frame);

/* Signals */
int64_t sys_rt_sigaction(struct syscall_frame *frame);
int64_t sys_rt_sigprocmask(struct syscall_frame *frame);
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

    /* ---- Signals ---- */
    syscall_table[SYS_rt_sigaction]   = sys_rt_sigaction;
    syscall_table[SYS_rt_sigprocmask]  = sys_rt_sigprocmask;
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

    /* ---- Sockets ---- */
    syscall_table[SYS_socket]            = sys_socket;
    syscall_table[SYS_connect]           = sys_connect;
    syscall_table[SYS_accept]            = sys_accept;
    syscall_table[SYS_sendto]            = sys_sendto;
    syscall_table[SYS_recvfrom]          = sys_recvfrom;
    syscall_table[SYS_bind]              = sys_bind;
    syscall_table[SYS_listen]            = sys_listen;
}

int64_t syscall_dispatch(struct syscall_frame *frame)
{
    uint64_t nr = frame->rax;
    int64_t ret;

    if (nr >= SYSCALL_TABLE_SIZE) {
        ret = -(int64_t)ENOSYS;
    } else {
        ret = syscall_table[nr](frame);
    }

#ifdef SYSCALL_TRACE
    if (nr != SYS_write) { /* suppress write spam */
        printk("syscall: nr=%llu ret=%lld\n",
               (unsigned long long)nr, (long long)ret);
    }
#endif

    if (current_task && current_task->mode == TASK_MODE_USER) {
        task_deliver_signals();
    }

    return ret;
}
