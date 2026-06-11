/*
 * sys_epoll.c — epoll, select, pselect6 syscall implementations.
 *
 * Implements a minimal epoll that polls file descriptors for readiness.
 * Also implements select() and pselect6() via poll-based logic.
 */
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <sched/task.h>
#include <drivers/pit.h>
#include <mm/heap.h>
#include <mm/uaccess.h>
#include <fs/vfs.h>
#include <sched/wait_queue.h>
#include <lib/string.h>
#include <kernel/printk.h>

/* ------------------------------------------------------------------ */
/* select / pselect6                                                    */
/* ------------------------------------------------------------------ */

/* fd_set: 1024 bits = 16 uint64_t words */
#define FD_SET_LONGS 16
typedef struct { uint64_t fds_bits[FD_SET_LONGS]; } kernel_fd_set_t;

static int is_fd_set(const kernel_fd_set_t *set, int fd)
{
    if (!set || fd < 0 || fd >= 1024) return 0;
    return (set->fds_bits[fd / 64] >> (fd % 64)) & 1;
}

static void set_fd(kernel_fd_set_t *set, int fd)
{
    if (!set || fd < 0 || fd >= 1024) return;
    set->fds_bits[fd / 64] |= (1ULL << (fd % 64));
}

static void clear_fd_set(kernel_fd_set_t *set)
{
    if (!set) return;
    memset(set, 0, sizeof(*set));
}

struct timeval_user {
    int64_t tv_sec;
    int64_t tv_usec;
};

struct timespec_user {
    int64_t tv_sec;
    int64_t tv_nsec;
};

static bool task_has_unblocked_signal(void)
{
    return current_task != 0
        && (current_task->signal_pending & ~current_task->signal_blocked) != 0;
}

static uint64_t ms_to_ticks(int64_t timeout_ms)
{
    if (timeout_ms < 0) return UINT64_MAX;
    return (uint64_t)((timeout_ms + 9) / 10);
}

static uint64_t ts_to_ticks(const struct timespec_user *ts, bool *valid)
{
    if (valid) *valid = false;
    if (ts == 0) return UINT64_MAX;
    if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000LL) {
        return 0;
    }
    if (valid) *valid = true;
    uint64_t ticks = (uint64_t)ts->tv_sec * 100ULL;
    ticks += (uint64_t)(ts->tv_nsec / 10000000LL);
    return ticks;
}

static int poll_node(struct vfs_node *node, short events, short *revents)
{
    if (node == 0) return -EBADF;

    if (node->poll != 0) {
        return node->poll(node, events, revents);
    }

    short ready = 0;
    if ((events & 0x0001) && node->read != 0) {
        ready |= 0x0001;
    }
    if ((events & 0x0004) && node->write != 0) {
        ready |= 0x0004;
    }
    if ((events & 0x0008) && node->read == 0 && node->write == 0) {
        ready |= 0x0008;
    }
    if ((events & 0x0010) && node->write == 0) {
        ready |= 0x0010;
    }
    if (revents != 0) {
        *revents = ready;
    }
    return ready != 0 ? 1 : 0;
}

static int scan_fd_sets(struct process *proc, int nfds,
    const kernel_fd_set_t *rfds, const kernel_fd_set_t *wfds, const kernel_fd_set_t *efds,
    kernel_fd_set_t *out_rfds, kernel_fd_set_t *out_wfds, kernel_fd_set_t *out_efds)
{
    clear_fd_set(out_rfds);
    clear_fd_set(out_wfds);
    clear_fd_set(out_efds);

    int count = 0;
    for (int fd = 0; fd < nfds && fd < MAX_FILES_PER_PROCESS; fd++) {
        struct file *f = proc->files[fd];
        if (f == 0 || f->node == 0) {
            continue;
        }

        bool fd_ready = false;
        short revents = 0;
        if (rfds != 0 && is_fd_set(rfds, fd)) {
            if (poll_node(f->node, 0x0001, &revents) < 0) {
                set_fd(out_efds, fd);
                fd_ready = true;
            } else if (revents & 0x0001) {
                set_fd(out_rfds, fd);
                fd_ready = true;
            }
        }
        if (wfds != 0 && is_fd_set(wfds, fd)) {
            revents = 0;
            if (poll_node(f->node, 0x0004, &revents) < 0) {
                set_fd(out_efds, fd);
                fd_ready = true;
            } else if (revents & 0x0004) {
                set_fd(out_wfds, fd);
                fd_ready = true;
            }
        }
        if (efds != 0 && is_fd_set(efds, fd)) {
            revents = 0;
            if (poll_node(f->node, 0x0018, &revents) < 0) {
                set_fd(out_efds, fd);
                fd_ready = true;
            } else if (revents & 0x0018) {
                set_fd(out_efds, fd);
                fd_ready = true;
            }
        }
        if (fd_ready) {
            count++;
        }
    }
    return count;
}

static int64_t wait_for_io_or_signal(uint64_t timeout_ticks)
{
    if (task_has_unblocked_signal()) {
        return -(int64_t)EINTR;
    }

    if (timeout_ticks == 0) {
        return 0;
    }

    if (timeout_ticks == UINT64_MAX) {
        wait_queue_sleep(wait_queue_io_event());
    } else {
        bool timed_out = wait_queue_sleep_timeout(wait_queue_io_event(), timeout_ticks);
        if (timed_out) {
            return 0;
        }
    }

    if (task_has_unblocked_signal()) {
        return -(int64_t)EINTR;
    }

    return 1;
}

static int64_t do_select(int nfds, const kernel_fd_set_t *rfds, const kernel_fd_set_t *wfds,
                          const kernel_fd_set_t *efds, kernel_fd_set_t *out_rfds,
                          kernel_fd_set_t *out_wfds, kernel_fd_set_t *out_efds,
                          uint64_t timeout_ticks)
{
    if (!current_task || !current_task->process) return -(int64_t)EBADF;
    struct process *proc = current_task->process;
    uint64_t deadline = UINT64_MAX;
    if (timeout_ticks != UINT64_MAX) {
        deadline = pit_ticks() + timeout_ticks;
    }

    for (;;) {
        int count = scan_fd_sets(proc, nfds, rfds, wfds, efds, out_rfds, out_wfds, out_efds);
        if (count > 0) {
            return count;
        }

        if (timeout_ticks == 0) {
            return 0;
        }

        uint64_t remaining = UINT64_MAX;
        if (deadline != UINT64_MAX) {
            uint64_t now = pit_ticks();
            if (now >= deadline) {
                return 0;
            }
            remaining = deadline - now;
        }

        int64_t wait_rc = wait_for_io_or_signal(remaining);
        if (wait_rc <= 0) {
            return wait_rc;
        }
    }
}

int64_t sys_select(struct syscall_frame *frame)
{
    int nfds = (int)frame->rdi;
    kernel_fd_set_t *u_rfds = (kernel_fd_set_t *)frame->rsi;
    kernel_fd_set_t *u_wfds = (kernel_fd_set_t *)frame->rdx;
    kernel_fd_set_t *u_efds = (kernel_fd_set_t *)frame->r10;
    struct timeval_user *u_tv = (struct timeval_user *)frame->r8;

    kernel_fd_set_t krfds, kwfds, kefds;
    clear_fd_set(&krfds); clear_fd_set(&kwfds); clear_fd_set(&kefds);

    if (u_rfds && copy_from_user(&krfds, u_rfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_wfds && copy_from_user(&kwfds, u_wfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_efds && copy_from_user(&kefds, u_efds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;

    int64_t timeout_ms = -1;
    if (u_tv) {
        struct timeval_user tv;
        if (copy_from_user(&tv, u_tv, sizeof(tv)) != 0) return -(int64_t)EFAULT;
        if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000) return -(int64_t)EINVAL;
        timeout_ms = tv.tv_sec * 1000 + tv.tv_usec / 1000;
    }

    int64_t ret = do_select(nfds,
        u_rfds ? &krfds : 0,
        u_wfds ? &kwfds : 0,
        u_efds ? &kefds : 0,
        u_rfds ? &krfds : 0,
        u_wfds ? &kwfds : 0,
        u_efds ? &kefds : 0,
        ms_to_ticks(timeout_ms));

    if (ret < 0) return ret;

    if (u_rfds && copy_to_user(u_rfds, &krfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_wfds && copy_to_user(u_wfds, &kwfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_efds && copy_to_user(u_efds, &kefds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;

    return ret;
}

int64_t sys_pselect6(struct syscall_frame *frame)
{
    int nfds = (int)frame->rdi;
    kernel_fd_set_t *u_rfds = (kernel_fd_set_t *)frame->rsi;
    kernel_fd_set_t *u_wfds = (kernel_fd_set_t *)frame->rdx;
    kernel_fd_set_t *u_efds = (kernel_fd_set_t *)frame->r10;
    struct timespec_user *u_ts = (struct timespec_user *)frame->r8;
    const uint64_t *u_sigmask = (const uint64_t *)frame->r9;

    kernel_fd_set_t krfds, kwfds, kefds;
    clear_fd_set(&krfds); clear_fd_set(&kwfds); clear_fd_set(&kefds);

    if (u_rfds && copy_from_user(&krfds, u_rfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_wfds && copy_from_user(&kwfds, u_wfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_efds && copy_from_user(&kefds, u_efds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;

    struct task *task = current_task;
    if (task == 0) return -(int64_t)EPERM;

    uint64_t user_mask = 0;
    uint64_t saved_mask = task->signal_blocked;
    bool have_sigmask = false;
    if (u_sigmask != 0) {
        if (copy_from_user(&user_mask, u_sigmask, sizeof(uint64_t)) != 0) return -(int64_t)EFAULT;
        have_sigmask = true;
        task->signal_blocked = user_mask & ~((1ULL << 9) | (1ULL << 19));
    }

    bool valid = false;
    uint64_t timeout_ticks = ts_to_ticks(u_ts, &valid);
    if (!valid && u_ts != 0) {
        if (have_sigmask) task->signal_blocked = saved_mask;
        return -(int64_t)EINVAL;
    }

    int64_t ret = do_select(nfds,
        u_rfds ? &krfds : 0,
        u_wfds ? &kwfds : 0,
        u_efds ? &kefds : 0,
        u_rfds ? &krfds : 0,
        u_wfds ? &kwfds : 0,
        u_efds ? &kefds : 0,
        timeout_ticks);

    if (have_sigmask) {
        task->signal_blocked = saved_mask;
    }

    if (ret < 0) return ret;

    if (u_rfds && copy_to_user(u_rfds, &krfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_wfds && copy_to_user(u_wfds, &kwfds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;
    if (u_efds && copy_to_user(u_efds, &kefds, sizeof(kernel_fd_set_t)) != 0)
        return -(int64_t)EFAULT;

    return ret;
}

/* ------------------------------------------------------------------ */
/* epoll                                                               */
/* ------------------------------------------------------------------ */

#define EPOLL_MAX_EVENTS  1024
#define EPOLL_MAX_WATCHES  256

#define EPOLLIN   0x001
#define EPOLLOUT  0x004
#define EPOLLERR  0x008
#define EPOLLHUP  0x010
#define EPOLLRDHUP 0x2000
#define EPOLLET   (1U << 31)

#define EPOLL_CTL_ADD 1
#define EPOLL_CTL_DEL 2
#define EPOLL_CTL_MOD 3

struct epoll_event {
    uint32_t events;
    uint64_t data; /* union -- store as u64 */
} __attribute__((packed));

struct epoll_watch {
    int fd;
    uint32_t events;
    uint64_t data;
    bool valid;
};

struct epoll_instance {
    struct epoll_watch watches[EPOLL_MAX_WATCHES];
    int watch_count;
    bool valid;
};

#define EPOLL_INSTANCES_MAX 64
static struct epoll_instance epoll_instances[EPOLL_INSTANCES_MAX];

static struct epoll_instance *epoll_alloc(void)
{
    for (int i = 0; i < EPOLL_INSTANCES_MAX; i++) {
        if (!epoll_instances[i].valid) {
            memset(&epoll_instances[i], 0, sizeof(struct epoll_instance));
            epoll_instances[i].valid = true;
            return &epoll_instances[i];
        }
    }
    return 0;
}

static int epoll_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf; (void)count;
    return -EIO;
}

static int epoll_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf; (void)count;
    return -EIO;
}

static int epoll_poll(struct vfs_node *node, short events, short *revents)
{
    struct epoll_instance *ep = (struct epoll_instance *)node->data;
    if (!ep || !ep->valid) return -EBADF;

    short ready = 0;
    for (int i = 0; i < EPOLL_MAX_WATCHES; i++) {
        if (!ep->watches[i].valid) continue;
        int fd = ep->watches[i].fd;
        if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || !current_task || !current_task->process) continue;
        struct file *f = current_task->process->files[fd];
        if (!f || !f->node) continue;
        short re = 0;
        if (poll_node(f->node, (short)ep->watches[i].events, &re) >= 0 && re != 0) {
            ready |= 0x0001;
            break;
        }
    }
    if (revents != 0) {
        *revents = ready;
    }
    (void)events;
    return ready != 0 ? 1 : 0;
}

static int epoll_close(struct vfs_node *node, struct file *f)
{
    (void)f;
    if (node && node->data) {
        struct epoll_instance *ep = (struct epoll_instance *)node->data;
        ep->valid = false;
        memset(ep, 0, sizeof(*ep));
    }
    return 0;
}

int64_t sys_epoll_create1(struct syscall_frame *frame)
{
    int flags = (int)frame->rdi;
    (void)flags; /* EPOLL_CLOEXEC supported */

    if (!current_task || !current_task->process) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    /* Find a free fd */
    int fd = -1;
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        if (!proc->files[i]) { fd = i; break; }
    }
    if (fd < 0) return -(int64_t)EMFILE;

    struct epoll_instance *ep = epoll_alloc();
    if (!ep) return -(int64_t)ENOMEM;

    /* Create a VFS node to wrap the epoll instance */
    struct vfs_node *node = kzalloc(sizeof(struct vfs_node));
    if (!node) { ep->valid = false; return -(int64_t)ENOMEM; }
    memcpy(node->name, "[epoll]", 7);
    node->type = VFS_TYPE_CHAR;
    node->mode = 0;
    node->data = ep;
    node->read = epoll_read;
    node->write = epoll_write;
    node->close = epoll_close;
    node->poll = epoll_poll;

    struct file *f = kzalloc(sizeof(struct file));
    if (!f) { kfree(node); ep->valid = false; return -(int64_t)ENOMEM; }
    f->node = node;
    f->offset = 0;
    f->flags = O_RDWR;
    f->fd_flags = (flags & 0x80000) ? FD_CLOEXEC : 0; /* EPOLL_CLOEXEC = O_CLOEXEC */
    f->ref_count = 1;

    proc->files[fd] = f;
    return (int64_t)fd;
}

int64_t sys_epoll_ctl(struct syscall_frame *frame)
{
    int epfd = (int)frame->rdi;
    int op   = (int)frame->rsi;
    int fd   = (int)frame->rdx;
    struct epoll_event *u_event = (struct epoll_event *)frame->r10;

    if (!current_task || !current_task->process) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (epfd < 0 || epfd >= MAX_FILES_PER_PROCESS || !proc->files[epfd])
        return -(int64_t)EBADF;
    struct vfs_node *enode = proc->files[epfd]->node;
    if (!enode || !enode->data) return -(int64_t)EINVAL;
    struct epoll_instance *ep = (struct epoll_instance *)enode->data;
    if (!ep->valid) return -(int64_t)EINVAL;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS) return -(int64_t)EBADF;

    struct epoll_event kev;
    memset(&kev, 0, sizeof(kev));
    if (op != EPOLL_CTL_DEL && u_event) {
        if (copy_from_user(&kev, u_event, sizeof(struct epoll_event)) != 0)
            return -(int64_t)EFAULT;
    }

    if (op == EPOLL_CTL_ADD) {
        /* Check not already there */
        for (int i = 0; i < EPOLL_MAX_WATCHES; i++) {
            if (ep->watches[i].valid && ep->watches[i].fd == fd)
                return -(int64_t)EEXIST;
        }
        /* Find free slot */
        for (int i = 0; i < EPOLL_MAX_WATCHES; i++) {
            if (!ep->watches[i].valid) {
                ep->watches[i].fd = fd;
                ep->watches[i].events = kev.events;
                ep->watches[i].data = kev.data;
                ep->watches[i].valid = true;
                ep->watch_count++;
                return 0;
            }
        }
        return -(int64_t)ENOMEM;
    } else if (op == EPOLL_CTL_MOD) {
        for (int i = 0; i < EPOLL_MAX_WATCHES; i++) {
            if (ep->watches[i].valid && ep->watches[i].fd == fd) {
                ep->watches[i].events = kev.events;
                ep->watches[i].data = kev.data;
                return 0;
            }
        }
        return -(int64_t)ENOENT;
    } else if (op == EPOLL_CTL_DEL) {
        for (int i = 0; i < EPOLL_MAX_WATCHES; i++) {
            if (ep->watches[i].valid && ep->watches[i].fd == fd) {
                ep->watches[i].valid = false;
                ep->watch_count--;
                return 0;
            }
        }
        return -(int64_t)ENOENT;
    }

    return -(int64_t)EINVAL;
}

int64_t sys_epoll_wait(struct syscall_frame *frame)
{
    int epfd = (int)frame->rdi;
    struct epoll_event *u_events = (struct epoll_event *)frame->rsi;
    int maxevents = (int)frame->rdx;
    int timeout_ms = (int)frame->r10;

    if (!current_task || !current_task->process) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (epfd < 0 || epfd >= MAX_FILES_PER_PROCESS || !proc->files[epfd])
        return -(int64_t)EBADF;
    struct vfs_node *enode = proc->files[epfd]->node;
    if (!enode || !enode->data) return -(int64_t)EINVAL;
    struct epoll_instance *ep = (struct epoll_instance *)enode->data;
    if (!ep->valid) return -(int64_t)EINVAL;

    if (!u_events || maxevents <= 0) return -(int64_t)EINVAL;

    uint64_t deadline = UINT64_MAX;
    if (timeout_ms >= 0) {
        deadline = pit_ticks() + ms_to_ticks(timeout_ms);
    }

    for (;;) {
        int ready_count = 0;

        for (int i = 0; i < EPOLL_MAX_WATCHES && ready_count < maxevents; i++) {
            if (!ep->watches[i].valid) continue;
            int fd = ep->watches[i].fd;
            if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || !proc->files[fd]) continue;

            struct file *f = proc->files[fd];
            short revents = 0;
            if (poll_node(f->node, (short)ep->watches[i].events, &revents) < 0) {
                revents |= EPOLLERR;
            }
            if (revents != 0) {
                struct epoll_event out_ev;
                out_ev.events = (uint32_t)revents;
                out_ev.data = ep->watches[i].data;
                if (copy_to_user(&u_events[ready_count], &out_ev, sizeof(out_ev)) != 0)
                    return -(int64_t)EFAULT;
                ready_count++;
            }
        }

        if (ready_count > 0) {
            return ready_count;
        }

        if (timeout_ms == 0) {
            return 0;
        }

        uint64_t remaining = UINT64_MAX;
        if (deadline != UINT64_MAX) {
            uint64_t now = pit_ticks();
            if (now >= deadline) {
                return 0;
            }
            remaining = deadline - now;
        }

        int64_t wait_rc = wait_for_io_or_signal(remaining);
        if (wait_rc <= 0) {
            return wait_rc;
        }
    }
}
