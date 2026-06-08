#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <sched/task.h>
#include <mm/uaccess.h>
#include <stdint.h>
#include <sched/scheduler.h>

/*
 * sys_exit — Linux ABI: void exit(int status)
 *
 * frame->rdi = exit status
 *
 * Calls task_exit() which puts the current task into zombie state and
 * triggers a reschedule. Does not return to userspace.
 */
int64_t sys_exit(struct syscall_frame *frame)
{
    int status = (int)(int64_t)frame->rdi;
    task_exit(status);
    /* task_exit() never returns — panic() is called if it does */
    return 0; /* unreachable, silences compiler warning */
}

/*
 * sys_exit_group — Linux ABI: void exit_group(int status)
 *
 * Terminates all threads in the process.
 */
int64_t sys_exit_group(struct syscall_frame *frame)
{
    int status = (int)(int64_t)frame->rdi;
    if (current_task != 0 && current_task->process != 0) {
        sched_exit_group(current_task->process, status);
    }
    return sys_exit(frame);
}

int64_t sys_fork(struct syscall_frame *frame)
{
    struct task *child = task_fork(frame);
    if (child == 0) {
        return -(int64_t)ENOMEM;
    }
    return (int64_t)child->pid;
}

/*
 * sys_wait4 — Linux ABI: pid_t wait4(pid_t pid, int *wstatus, int options, struct rusage *rusage)
 *
 * wstatus encoding (Linux):
 *   normal exit  -> (exit_code & 0xff) << 8
 *   killed by sig-> signal_number & 0x7f
 *
 * Supports WNOHANG (return 0 immediately if no child has exited yet).
 */
int64_t sys_wait4(struct syscall_frame *frame)
{
    int64_t  pid     = (int64_t)frame->rdi;
    int     *wstatus = (int *)frame->rsi;
    int      options = (int)frame->rdx;
    /* rusage (frame->r8) intentionally ignored — Phase 22 */

    if (wstatus != 0) {
        if (!uaccess_ok(wstatus, sizeof(int))) {
            return -(int64_t)EFAULT;
        }
    }

    int exit_code = 0;
    uint64_t out_pid = 0;
    int ret;

    if (options & WNOHANG) {
        /* Non-blocking: return 0 if no child has exited yet */
        uint64_t search_pid = (pid <= 0) ? 0 : (uint64_t)pid;
        ret = task_wait_nohang(search_pid, &exit_code, &out_pid);
        if (ret < 0) {
            /* No matching children at all */
            return -(int64_t)ECHILD;
        }
        if (ret == 0 && out_pid == 0) {
            /* No zombie yet */
            return 0;
        }
        /* Reaped a zombie — ret holds the reaped pid */
    } else if (pid <= 0) {
        /* Wait for any child */
        ret = task_wait_any(&exit_code, &out_pid);
        if (ret < 0) return -(int64_t)ECHILD;
    } else {
        /* Wait for specific child */
        ret = task_wait((uint64_t)pid, &exit_code);
        out_pid = (uint64_t)pid;
        if (ret < 0) return -(int64_t)ECHILD;
    }

    /* Encode wstatus in Linux format: normal exit = (code & 0xff) << 8, sig = sig & 0x7f */
    if (wstatus != 0) {
        int encoded;
        if (exit_code < 0) {
            encoded = (-exit_code) & 0x7f;
        } else {
            encoded = W_EXITCODE(exit_code);
        }
        if (copy_to_user(wstatus, &encoded, sizeof(int)) != 0) {
            return -(int64_t)EFAULT;
        }
    }

    return (int64_t)out_pid;
}

/* ECHILD is not in our errno list; define locally if needed */
#ifndef ECHILD
#define ECHILD 10
#endif
