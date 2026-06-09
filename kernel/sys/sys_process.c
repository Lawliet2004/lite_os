/*
 * sys_process.c — process identity and working-directory syscalls.
 *
 * Implements: getpid, getppid, getuid, getgid, geteuid, getegid,
 *             chdir, getcwd.
 */
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <sched/task.h>
#include <sched/scheduler.h>
#include <mm/uaccess.h>
#include <lib/string.h>
#include <fs/vfs.h>
#include <stdint.h>
#include <arch/x86_64/cpu.h>
#include <drivers/pit.h>


/* ------------------------------------------------------------------ */
/* getpid / getppid / uid / gid                                        */
/* ------------------------------------------------------------------ */

int64_t sys_getpid(struct syscall_frame *frame)
{
    (void)frame;
    if (current_task == 0 || current_task->process == 0) return 1;
    return (int64_t)current_task->process->pid;
}

int64_t sys_gettid(struct syscall_frame *frame)
{
    (void)frame;
    if (current_task == 0) return 1;
    return (int64_t)current_task->tid;
}

int64_t sys_getppid(struct syscall_frame *frame)
{
    (void)frame;
    if (current_task == 0 || current_task->process == 0) return 1;
    struct process *parent = current_task->process->parent;
    if (parent == 0) return 1; /* init is its own parent */
    return (int64_t)parent->pid;
}

/* We run as uid/gid 0 (root) for now — no multi-user support yet */
int64_t sys_getuid(struct syscall_frame *frame)  { (void)frame; return 0; }
int64_t sys_getgid(struct syscall_frame *frame)  { (void)frame; return 0; }
int64_t sys_geteuid(struct syscall_frame *frame) { (void)frame; return 0; }
int64_t sys_getegid(struct syscall_frame *frame) { (void)frame; return 0; }

/* ------------------------------------------------------------------ */
/* chdir / getcwd                                                       */
/* ------------------------------------------------------------------ */

int64_t sys_chdir(struct syscall_frame *frame)
{
    const char *path = (const char *)frame->rdi;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;

    char kpath[TASK_CWD_MAX];
    int err = copy_string_from_user(kpath, path, TASK_CWD_MAX);
    if (err != 0) return -(int64_t)EFAULT;

    char abspath[512];
    if (kpath[0] == '/') {
        size_t len = strlen(kpath);
        if (len >= sizeof(abspath)) return -(int64_t)ENAMETOOLONG;
        memcpy(abspath, kpath, len + 1);
    } else {
        char *cwd = current_task->process->cwd;
        size_t cwd_len = strlen(cwd);
        size_t path_len = strlen(kpath);

        if (cwd_len + 1 + path_len >= sizeof(abspath))
            return -(int64_t)ENAMETOOLONG;

        memcpy(abspath, cwd, cwd_len);
        if (abspath[cwd_len - 1] != '/') {
            abspath[cwd_len] = '/';
            cwd_len++;
        }
        memcpy(abspath + cwd_len, kpath, path_len + 1);
    }

    char cleanpath[TASK_CWD_MAX];
    vfs_canonicalize_path(abspath, cleanpath);

    /* Verify the path exists and is a directory */
    struct vfs_node *node = vfs_lookup(cleanpath);
    if (node == 0) return -(int64_t)ENOENT;
    if (node->type != VFS_TYPE_DIR) return -(int64_t)ENOTDIR;

    size_t clean_len = strlen(cleanpath);
    if (clean_len >= TASK_CWD_MAX) return -(int64_t)ENAMETOOLONG;
    memcpy(current_task->process->cwd, cleanpath, clean_len + 1);

    return 0;
}

int64_t sys_getcwd(struct syscall_frame *frame)
{
    char    *buf  = (char *)frame->rdi;
    size_t   size = (size_t)frame->rsi;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;
    if (buf == 0 || size == 0) return -(int64_t)EINVAL;

    const char *cwd = current_task->process->cwd;
    size_t cwd_len = strlen(cwd) + 1; /* include NUL */

    if (cwd_len > size) return -(int64_t)ERANGE;

    if (copy_to_user(buf, cwd, cwd_len) != 0) return -(int64_t)EFAULT;

    return (int64_t)(uintptr_t)buf; /* Linux returns buf pointer on success */
}

/* Phase 16: Signal and Kill Syscall Handlers */
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

int64_t sys_rt_sigaction(struct syscall_frame *frame)
{
    int sig = (int)frame->rdi;
    const struct sigaction_linux *act = (const struct sigaction_linux *)frame->rsi;
    struct sigaction_linux *oact = (struct sigaction_linux *)frame->rdx;
    size_t sigsetsize = (size_t)frame->r10;

    if (sig <= 0 || sig >= 64 || sigsetsize != sizeof(uint64_t)) {
        return -(int64_t)EINVAL;
    }

    // SIGKILL (9) and SIGSTOP (19) cannot be caught or ignored
    if (sig == 9 || sig == 19) {
        return -(int64_t)EINVAL;
    }

    struct task *task = current_task;
    if (task == 0 || task->process == 0) return -(int64_t)EPERM;
    struct process *proc = task->process;

    if (oact != 0) {
        if (!uaccess_ok(oact, sizeof(struct sigaction_linux))) {
            return -(int64_t)EFAULT;
        }
        if (copy_to_user(oact, &proc->sigactions[sig], sizeof(struct sigaction_linux)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    if (act != 0) {
        if (!uaccess_ok(act, sizeof(struct sigaction_linux))) {
            return -(int64_t)EFAULT;
        }
        struct sigaction_linux new_act;
        if (copy_from_user(&new_act, act, sizeof(struct sigaction_linux)) != 0) {
            return -(int64_t)EFAULT;
        }
        proc->sigactions[sig] = new_act;
    }

    return 0;
}

int64_t sys_rt_sigprocmask(struct syscall_frame *frame)
{
    int how = (int)frame->rdi;
    const uint64_t *set = (const uint64_t *)frame->rsi;
    uint64_t *oset = (uint64_t *)frame->rdx;
    size_t sigsetsize = (size_t)frame->r10;

    if (sigsetsize != sizeof(uint64_t)) {
        return -(int64_t)EINVAL;
    }

    struct task *task = current_task;
    if (task == 0) return -(int64_t)EPERM;

    if (oset != 0) {
        if (!uaccess_ok(oset, sizeof(uint64_t))) {
            return -(int64_t)EFAULT;
        }
        if (copy_to_user(oset, &task->signal_blocked, sizeof(uint64_t)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    if (set != 0) {
        if (!uaccess_ok(set, sizeof(uint64_t))) {
            return -(int64_t)EFAULT;
        }
        uint64_t new_mask;
        if (copy_from_user(&new_mask, set, sizeof(uint64_t)) != 0) {
            return -(int64_t)EFAULT;
        }

        // SIGKILL and SIGSTOP cannot be blocked
        new_mask &= ~((1ULL << 9) | (1ULL << 19));

        if (how == SIG_BLOCK) {
            task->signal_blocked |= new_mask;
        } else if (how == SIG_UNBLOCK) {
            task->signal_blocked &= ~new_mask;
        } else if (how == SIG_SETMASK) {
            task->signal_blocked = new_mask;
        } else {
            return -(int64_t)EINVAL;
        }
    }

    return 0;
}

int64_t sys_rt_sigreturn(struct syscall_frame *frame)
{
    struct task *task = current_task;
    if (task == 0 || task->process == 0) return -(int64_t)EPERM;

    /* The user stack pointer points to &user_frame->info when sigreturn is called.
       So the start of the sigframe is exactly at frame->user_rsp - 8. */
    struct rt_sigframe *user_frame = (struct rt_sigframe *)(frame->user_rsp - 8);
    struct ucontext_linux uc;

    if (copy_from_user(&uc, &user_frame->uc, sizeof(struct ucontext_linux)) != 0) {
        return -(int64_t)EFAULT;
    }

    struct sigcontext_linux *sc = &uc.uc_mcontext;
    frame->r8 = sc->r8;
    frame->r9 = sc->r9;
    frame->r10 = sc->r10;
    frame->r11 = sc->r11;
    frame->r12 = sc->r12;
    frame->r13 = sc->r13;
    frame->r14 = sc->r14;
    frame->r15 = sc->r15;
    frame->rdi = sc->rdi;
    frame->rsi = sc->rsi;
    frame->rbp = sc->rbp;
    frame->rbx = sc->rbx;
    frame->rdx = sc->rdx;
    frame->rax = sc->rax;
    frame->rcx = sc->rip;      // restore RIP into RCX for SYSRETQ
    frame->r11 = sc->eflags;   // restore RFLAGS into R11 for SYSRETQ
    frame->user_rsp = sc->rsp;  // restore user stack pointer

    task->signal_blocked = uc.uc_sigmask;

    return (int64_t)frame->rax;
}

int64_t sys_kill(struct syscall_frame *frame)
{
    int64_t pid = (int64_t)frame->rdi;
    int sig = (int)frame->rsi;

    if (sig < 0 || sig >= 64) return -(int64_t)EINVAL;
    if (pid <= 0) return -(int64_t)ENOSYS; // Only support killing specific PID > 0 for now

    struct process *proc = find_process((uint64_t)pid);
    if (proc == 0 || proc->main_thread == 0) {
        return -(int64_t)ESRCH;
    }

    task_send_signal(proc->main_thread, sig);
    return 0;
}

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128

#define ARCH_SET_GS 0x1001
#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003
#define ARCH_GET_GS 0x1004

int64_t sys_clone(struct syscall_frame *frame)
{
    uint64_t flags = frame->rdi;
    void *child_stack = (void *)frame->rsi;
    int *parent_tid = (int *)frame->rdx;
    int *child_tid = (int *)frame->r10;
    uint64_t newtls = frame->r8;

    /* If it is a process fork clone (no CLONE_VM, no CLONE_THREAD) */
    if (!(flags & CLONE_VM) && !(flags & CLONE_THREAD)) {
        struct task *child = task_fork(frame);
        if (child == 0) return -ENOMEM;
        return (int64_t)child->pid;
    }

    /* Otherwise, it is a thread clone */
    struct task *child = task_clone_thread(frame, flags, child_stack, parent_tid, child_tid, newtls);
    if (child == 0) return -ENOMEM;

    /* Write parent_tid in parent if CLONE_PARENT_SETTID */
    if (flags & CLONE_PARENT_SETTID) {
        if (copy_to_user(parent_tid, &child->tid, sizeof(int)) != 0) {
            return -EFAULT;
        }
    }

    /* Write child_tid in child's address space if CLONE_CHILD_SETTID */
    if (flags & CLONE_CHILD_SETTID) {
        if (copy_to_user(child_tid, &child->tid, sizeof(int)) != 0) {
            return -EFAULT;
        }
    }

    return (int64_t)child->tid;
}

int64_t sys_futex(struct syscall_frame *frame)
{
    uint32_t *uaddr = (uint32_t *)frame->rdi;
    int op = (int)frame->rsi;
    uint32_t val = (uint32_t)frame->rdx;

    int cmd = op & ~FUTEX_PRIVATE_FLAG;

    if (!uaccess_ok(uaddr, sizeof(uint32_t))) {
        return -EFAULT;
    }

    if (cmd == FUTEX_WAIT) {
        uint32_t uval;
        if (copy_from_user(&uval, uaddr, sizeof(uint32_t)) != 0) {
            return -EFAULT;
        }

        if (uval != val) {
            return -EAGAIN;
        }

        // Block the current task on uaddr
        bool was_enabled = save_interrupts_and_disable();
        current_task->state = TASK_SLEEPING;
        current_task->futex_uaddr = uaddr;
        schedule();
        restore_interrupts(was_enabled);

        return 0;
    } else if (cmd == FUTEX_WAKE) {
        return futex_wake_address(uaddr, (int)val);
    }

    return -ENOSYS;
}

int64_t sys_arch_prctl(struct syscall_frame *frame)
{
    int code = (int)frame->rdi;
    unsigned long addr = (unsigned long)frame->rsi;

    if (code == ARCH_SET_FS) {
        current_task->fs_base = addr;
        write_fs_base(addr);
        return 0;
    } else if (code == ARCH_GET_FS) {
        if (!uaccess_ok((void *)addr, sizeof(unsigned long))) {
            return -EFAULT;
        }
        if (copy_to_user((void *)addr, &current_task->fs_base, sizeof(unsigned long)) != 0) {
            return -EFAULT;
        }
        return 0;
    }

    return -EINVAL;
}

int64_t sys_set_tid_address(struct syscall_frame *frame)
{
    int *tidptr = (int *)frame->rdi;
    current_task->clear_child_tid = (uint32_t *)tidptr;
    return (int64_t)current_task->tid;
}

struct utsname_linux {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static void copy_uts_field(char dst[65], const char *src)
{
    size_t len = strlen(src);
    if (len > 64) len = 64;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

int64_t sys_uname(struct syscall_frame *frame)
{
    struct utsname_linux *ubuf = (struct utsname_linux *)frame->rdi;
    if (ubuf == 0) return -(int64_t)EFAULT;

    struct utsname_linux kbuf;
    memset(&kbuf, 0, sizeof(kbuf));
    copy_uts_field(kbuf.sysname, "LiteNix");
    copy_uts_field(kbuf.nodename, "litenix");
    copy_uts_field(kbuf.release, "0.1.0");
    copy_uts_field(kbuf.version, "LiteNix freestanding kernel");
    copy_uts_field(kbuf.machine, "x86_64");
    copy_uts_field(kbuf.domainname, "localdomain");

    if (copy_to_user(ubuf, &kbuf, sizeof(kbuf)) != 0) {
        return -(int64_t)EFAULT;
    }
    return 0;
}

int64_t sys_set_robust_list(struct syscall_frame *frame)
{
    void *head = (void *)frame->rdi;
    size_t len = (size_t)frame->rsi;

    if (current_task == 0) return -(int64_t)EPERM;
    current_task->robust_list_head = head;
    current_task->robust_list_len = len;
    return 0;
}

int64_t sys_get_robust_list(struct syscall_frame *frame)
{
    int pid = (int)frame->rdi;
    void **head_ptr = (void **)frame->rsi;
    size_t *len_ptr = (size_t *)frame->rdx;

    if (current_task == 0) return -(int64_t)EPERM;
    if (pid != 0 && pid != (int)current_task->pid && pid != (int)current_task->tid) {
        return -(int64_t)ENOSYS;
    }
    if (copy_to_user(head_ptr, &current_task->robust_list_head, sizeof(void *)) != 0) {
        return -(int64_t)EFAULT;
    }
    if (copy_to_user(len_ptr, &current_task->robust_list_len, sizeof(size_t)) != 0) {
        return -(int64_t)EFAULT;
    }
    return 0;
}

static uint64_t random_state = 0x6a09e667f3bcc909ULL;

static uint64_t random_next(void)
{
    random_state ^= pit_ticks() + 0x9e3779b97f4a7c15ULL;
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state;
}

int64_t sys_getrandom(struct syscall_frame *frame)
{
    void *ubuf = (void *)frame->rdi;
    size_t buflen = (size_t)frame->rsi;
    unsigned int flags = (unsigned int)frame->rdx;

    if (flags & ~0x0003U) return -(int64_t)EINVAL;
    if (buflen == 0) return 0;
    if (!uaccess_ok(ubuf, buflen)) return -(int64_t)EFAULT;

    uint8_t kbuf[256];
    size_t done = 0;
    while (done < buflen) {
        size_t chunk = buflen - done;
        if (chunk > sizeof(kbuf)) chunk = sizeof(kbuf);

        for (size_t i = 0; i < chunk; i++) {
            if ((i & 7) == 0) {
                uint64_t r = random_next();
                memcpy(kbuf + i, &r, (chunk - i) < 8 ? (chunk - i) : 8);
            }
        }

        if (copy_to_user((uint8_t *)ubuf + done, kbuf, chunk) != 0) {
            return -(int64_t)EFAULT;
        }
        done += chunk;
    }

    return (int64_t)buflen;
}

struct rlimit64_linux {
    uint64_t rlim_cur;
    uint64_t rlim_max;
};

#define RLIM64_INFINITY UINT64_MAX
#define RLIMIT_STACK 3
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9

static void fill_rlimit(int resource, struct rlimit64_linux *rlim)
{
    rlim->rlim_cur = RLIM64_INFINITY;
    rlim->rlim_max = RLIM64_INFINITY;
    if (resource == RLIMIT_NOFILE) {
        rlim->rlim_cur = MAX_FILES_PER_PROCESS;
        rlim->rlim_max = MAX_FILES_PER_PROCESS;
    } else if (resource == RLIMIT_STACK) {
        rlim->rlim_cur = 8ULL * 1024 * 1024;
        rlim->rlim_max = 8ULL * 1024 * 1024;
    } else if (resource == RLIMIT_AS) {
        rlim->rlim_cur = PROCESS_HEAP_MAX;
        rlim->rlim_max = PROCESS_HEAP_MAX;
    }
}

int64_t sys_prlimit64(struct syscall_frame *frame)
{
    int pid = (int)frame->rdi;
    int resource = (int)frame->rsi;
    const struct rlimit64_linux *new_limit = (const struct rlimit64_linux *)frame->rdx;
    struct rlimit64_linux *old_limit = (struct rlimit64_linux *)frame->r10;

    if (current_task == 0) return -(int64_t)EPERM;
    if (pid != 0 && pid != (int)current_task->pid) return -(int64_t)ENOSYS;
    if (resource < 0 || resource > 15) return -(int64_t)EINVAL;
    if (new_limit != 0) return -(int64_t)ENOSYS;

    if (old_limit != 0) {
        struct rlimit64_linux rlim;
        fill_rlimit(resource, &rlim);
        if (copy_to_user(old_limit, &rlim, sizeof(rlim)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    return 0;
}

int64_t sys_getrlimit(struct syscall_frame *frame)
{
    struct syscall_frame f;
    memset(&f, 0, sizeof(f));
    f.rdi = 0;
    f.rsi = frame->rdi;
    f.rdx = 0;
    f.r10 = frame->rsi;
    return sys_prlimit64(&f);
}

struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};

struct timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timezone {
    int tz_minuteswest;
    int tz_dsttime;
};

int64_t sys_clock_gettime(struct syscall_frame *frame)
{
    int clockid = (int)frame->rdi;
    struct timespec *tp = (struct timespec *)frame->rsi;

    if (tp == 0) return -(int64_t)EINVAL;

    struct timespec kts;
    uint64_t ticks = pit_ticks();

    if (clockid == 1) { // CLOCK_MONOTONIC
        kts.tv_sec = ticks / 100;
        kts.tv_nsec = (ticks % 100) * 10000000;
    } else { // CLOCK_REALTIME (0)
        kts.tv_sec = 1700000000 + (ticks / 100);
        kts.tv_nsec = (ticks % 100) * 10000000;
    }

    if (copy_to_user(tp, &kts, sizeof(struct timespec)) != 0) {
        return -(int64_t)EFAULT;
    }
    return 0;
}

int64_t sys_gettimeofday(struct syscall_frame *frame)
{
    struct timeval *tv = (struct timeval *)frame->rdi;
    struct timezone *tz = (struct timezone *)frame->rsi;

    uint64_t ticks = pit_ticks();

    if (tv != 0) {
        struct timeval ktv;
        ktv.tv_sec = 1700000000 + (ticks / 100);
        ktv.tv_usec = (ticks % 100) * 10000;
        if (copy_to_user(tv, &ktv, sizeof(struct timeval)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    if (tz != 0) {
        struct timezone ktz;
        ktz.tz_minuteswest = 0;
        ktz.tz_dsttime = 0;
        if (copy_to_user(tz, &ktz, sizeof(struct timezone)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    return 0;
}

int64_t sys_nanosleep(struct syscall_frame *frame)
{
    const struct timespec *req = (const struct timespec *)frame->rdi;
    struct timespec *rem = (struct timespec *)frame->rsi;

    if (req == 0) return -(int64_t)EINVAL;

    struct timespec kreq;
    if (copy_from_user(&kreq, req, sizeof(struct timespec)) != 0) {
        return -(int64_t)EFAULT;
    }

    if (kreq.tv_sec < 0 || kreq.tv_nsec < 0 || kreq.tv_nsec >= 1000000000) {
        return -(int64_t)EINVAL;
    }

    uint64_t ticks = kreq.tv_sec * 100 + kreq.tv_nsec / 10000000;
    if (ticks > 0) {
        task_sleep_ticks(ticks);
    }

    if (rem != 0) {
        struct timespec krem;
        krem.tv_sec = 0;
        krem.tv_nsec = 0;
        if (copy_to_user(rem, &krem, sizeof(struct timespec)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    return 0;
}

int64_t sys_setpgid(struct syscall_frame *frame)
{
    uint64_t pid  = frame->rdi;
    uint64_t pgid = frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *caller = current_task->process;

    struct process *target = 0;
    if (pid == 0 || pid == caller->pid) {
        target = caller;
    } else {
        target = find_process(pid);
        if (target == 0) return -(int64_t)ESRCH;
    }

    if (pgid == 0) {
        pgid = target->pid;
    }

    target->pgid = (uint32_t)pgid;
    return 0;
}

int64_t sys_getpgid(struct syscall_frame *frame)
{
    uint64_t pid = frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *caller = current_task->process;

    struct process *target = 0;
    if (pid == 0 || pid == caller->pid) {
        target = caller;
    } else {
        target = find_process(pid);
        if (target == 0) return -(int64_t)ESRCH;
    }

    return (int64_t)target->pgid;
}

int64_t sys_setsid(struct syscall_frame *frame)
{
    (void)frame;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *caller = current_task->process;

    /* If process is already a group leader, setsid fails with EPERM */
    if (caller->pgid == caller->pid) {
        /* Accept it or return EPERM. Let's return EPERM to be POSIX compliant,
           but if some apps fail, we can check. Returning EPERM is standard. */
        return -(int64_t)EPERM;
    }

    caller->pgid = (uint32_t)caller->pid;
    caller->sid  = (uint32_t)caller->pid;
    return (int64_t)caller->sid;
}

int64_t sys_getsid(struct syscall_frame *frame)
{
    uint64_t pid = frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *caller = current_task->process;

    struct process *target = 0;
    if (pid == 0 || pid == caller->pid) {
        target = caller;
    } else {
        target = find_process(pid);
        if (target == 0) return -(int64_t)ESRCH;
    }

    return (int64_t)target->sid;
}
