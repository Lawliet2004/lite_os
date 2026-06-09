#include <sched/task.h>
#include <sched/scheduler.h>
#include <sched/wait_queue.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/vmm.h>
#include <mm/uaccess.h>
#include <drivers/pit.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <fs/vfs.h>
#include <sys/syscall.h>


struct task *current_task;
struct process *init_process;

static uint64_t next_pid = 1;
static uint64_t next_tid = 1;

static struct wait_queue reaper_queue;

struct process *process_create_with_parent(struct process *parent)
{
    struct process *process = kzalloc(sizeof(struct process));
    if (process == 0) return 0;

    process->pid = next_pid++;
    process->parent = parent;
    process->children = 0;
    process->sibling = 0;
    process->main_thread = 0;
    process->address_space = vmm_kernel_address_space();
    process->owns_address_space = false;
    process->exited = false;
    process->exit_code = 0;
    process->heap_start = 0;
    process->heap_end = 0;

    /* Default cwd to root */
    process->cwd[0] = '/';
    process->cwd[1] = '\0';

    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        process->files[i] = 0;
    }

    if (parent != 0) {
        process->sibling = parent->children;
        parent->children = process;

        /* Inherit cwd from parent */
        int ci = 0;
        while (ci < TASK_CWD_MAX - 1 && parent->cwd[ci] != '\0') {
            process->cwd[ci] = parent->cwd[ci];
            ci++;
        }
        process->cwd[ci] = '\0';

        /* Inherit heap layout */
        process->heap_start = parent->heap_start;
        process->heap_end   = parent->heap_end;

        /* Inherit file descriptors */
        for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
            process->files[i] = parent->files[i];
            if (process->files[i] != 0) {
                process->files[i]->ref_count++;
            }
        }
    }

    return process;
}

void process_unlink_child(struct process *parent, struct process *child)
{
    if (parent == 0 || child == 0) return;

    struct process *prev = 0;
    struct process *cursor = parent->children;
    while (cursor != 0) {
        if (cursor == child) {
            if (prev != 0) {
                prev->sibling = cursor->sibling;
            } else {
                parent->children = cursor->sibling;
            }
            cursor->sibling = 0;
            return;
        }
        prev = cursor;
        cursor = cursor->sibling;
    }
}

static void process_reparent_children(struct process *process)
{
    if (process == 0) return;
    struct process *init_p = init_process;
    if (init_p == 0 || init_p == process) return;

    while (process->children != 0) {
        struct process *child = process->children;
        process->children = child->sibling;
        child->sibling = init_p->children;
        init_p->children = child;
        child->parent = init_p;
    }
}

static void task_trampoline(void)
{
    __asm__ volatile ("sti");
    if (current_task == 0) {
        panic("Task: trampoline with no current task");
    }
    void (*entry)(void *) = current_task->entry;
    void *arg = current_task->entry_arg;
    if (entry == 0) {
        task_exit(0);
        return;
    }
    entry(arg);
    task_exit(0);
}

static struct task *task_alloc(const char *name, struct process *process,
    void (*entry)(void *), void *arg)
{
    struct task *task = kzalloc(sizeof(struct task));
    if (task == 0) return 0;

    task->kstack_base = kmalloc(KERNEL_STACK_SIZE);
    if (task->kstack_base == 0) {
        kfree(task);
        return 0;
    }

    task->kstack_top = (void *)((uintptr_t)task->kstack_base + KERNEL_STACK_SIZE);

    uint64_t *stack = (uint64_t *)task->kstack_top;
    *--stack = (uint64_t)task_trampoline;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    *--stack = 0;
    task->saved_rsp = (uint64_t)stack;

    task->pid = process->pid;
    task->tid = next_tid++;
    size_t name_len = name != 0 ? strlen(name) : 0;
    if (name_len >= TASK_NAME_MAX) name_len = TASK_NAME_MAX - 1;
    if (name_len > 0) {
        memcpy(task->name, name, name_len);
    }
    task->name[name_len] = '\0';
    task->state = TASK_READY;
    task->mode = TASK_MODE_KERNEL;
    task->cr3 = process->address_space->pml4_phys;
    task->entry = entry;
    task->entry_arg = arg;
    task->process = process;
    task->parent = current_task;
    task->next = 0;
    task->wait_next = 0;
    task->wait_queue = 0;
    task->created_ticks = pit_ticks();
    task->last_run_tick = pit_ticks();
    task->exit_code = 0;
    task->cancel_requested = false;

    /* Phase 7: initialize priority and time-slice fields */
    task->priority = SCHED_PRIO_DEFAULT;
    task->static_priority = SCHED_PRIO_DEFAULT;
    task->time_slice_max = sched_prio_to_slice(SCHED_PRIO_DEFAULT);
    task->time_slice = task->time_slice_max;
    task->total_ticks = 0;
    task->sleep_until = 0;
    task->flags = 0;
    task->sleep_next = 0;

    return task;
}

void task_init(void)
{
    wait_queue_init(&reaper_queue);

    struct process *idle_process = process_create_with_parent(0);
    if (idle_process == 0) panic("Task: failed to allocate idle process");
    init_process = idle_process;

    struct task *idle = task_alloc("idle", idle_process, 0, 0);
    if (idle == 0) panic("Task: failed to allocate idle task");

    idle->state = TASK_RUNNING;
    idle->flags = TASK_FLAG_IDLE;
    idle->priority = SCHED_PRIO_MAX;
    idle->static_priority = SCHED_PRIO_MAX;
    idle->time_slice_max = sched_prio_to_slice(SCHED_PRIO_MAX);
    idle->time_slice = idle->time_slice_max;
    idle_process->main_thread = idle;
    current_task = idle;
    sched_add_task(idle);
    sched_set_idle_task(idle);
}

struct task *task_create_arg(const char *name, void (*entry)(void *), void *arg)
{
    if (entry == 0) return 0;

    struct process *parent_process = current_task != 0 ? current_task->process : 0;
    struct process *process = process_create_with_parent(parent_process);
    if (process == 0) return 0;

    struct task *task = task_alloc(name, process, entry, arg);
    if (task == 0) {
        process_unlink_child(parent_process, process);
        kfree(process);
        return 0;
    }

    process->main_thread = task;
    sched_add_task(task);
    return task;
}

struct task *task_create(const char *name, void (*entry)(void))
{
    return task_create_arg(name, (void (*)(void *))entry, 0);
}

void user_task_entry(void *arg)
{
    (void)arg;
    struct task *self = current_task;
    if (self == 0) {
        panic("user_task_entry: current_task is NULL");
    }

    // Switch address space
    vmm_switch_address_space(self->process->address_space);

    printk("user_task_entry: Jumping to user RIP=0x%llx RSP=0x%llx\n",
           (unsigned long long)self->user_rip, (unsigned long long)self->user_rsp);

    // Jump to user mode!
    jump_to_usermode(self->user_rip, self->user_rsp);
}

struct task *task_create_user_process(const char *name, struct process *proc, void (*entry)(void *))
{
    struct task *task = task_alloc(name, proc, entry, 0);
    if (task != 0) {
        sched_add_task(task);
    }
    return task;
}

struct task *task_thread_create(const char *name, struct process *process,
    void (*entry)(void *), void *arg)
{
    if (process == 0 || entry == 0) return 0;

    struct task *task = task_alloc(name, process, entry, arg);
    if (task == 0) return 0;

    sched_add_task(task);
    return task;
}

void task_exit(int exit_code)
{
    if (current_task == 0) {
        panic("Task: task_exit with no current task");
    }

    /* Phase 17: clear-child-tid behavior */
    if (current_task->clear_child_tid != 0) {
        uint32_t zero = 0;
        if (copy_to_user(current_task->clear_child_tid, &zero, sizeof(uint32_t)) == 0) {
            futex_wake_address(current_task->clear_child_tid, 1);
        }
    }

    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = exit_code;
    if (current_task->process != 0) {
        bool any_alive = false;
        if (current_task->process->main_thread != 0
            && current_task->process->main_thread->state != TASK_ZOMBIE) {
            any_alive = true;
        }

        if (!any_alive) {
            current_task->process->exited = true;
            current_task->process->exit_code = exit_code;
            process_reparent_children(current_task->process);
        }
    }

    wait_queue_wake_all(&reaper_queue);

    schedule();
    panic("Task: schedule returned in task_exit");
}

void task_kill(struct task *task)
{
    if (task == 0 || task == current_task) return;
    if (task->state == TASK_ZOMBIE) return;

    /* If the task is on the sleep list, remove it */
    if (task->sleep_until != 0) {
        sched_remove_sleeper(task);
        task->sleep_until = 0;
    }

    task->state = TASK_ZOMBIE;
    task->exit_code = -1;
    if (task->process != 0 && task->process->main_thread == task) {
        task->process->exited = true;
        task->process->exit_code = -1;
        process_reparent_children(task->process);
    }
    if (task->wait_queue != 0) {
        struct wait_queue *wq = task->wait_queue;
        struct task *prev = 0;
        struct task *cursor = wq->head;
        while (cursor != 0 && cursor != task) {
            prev = cursor;
            cursor = cursor->wait_next;
        }
        if (cursor == task) {
            if (prev != 0) {
                prev->wait_next = cursor->wait_next;
            } else {
                wq->head = cursor->wait_next;
            }
            if (wq->tail == cursor) {
                wq->tail = prev;
            }
        }
        task->wait_next = 0;
        task->wait_queue = 0;
    }

    wait_queue_wake_all(&reaper_queue);
}

void task_request_cancel(struct task *task)
{
    if (task == 0) return;
    task->cancel_requested = true;
}

/* Phase 7: set task priority and recalculate time-slice */
void task_set_priority(struct task *task, int8_t priority)
{
    if (task == 0) return;

    /* Clamp to valid range */
    if (priority < SCHED_PRIO_MIN) priority = SCHED_PRIO_MIN;
    if (priority > SCHED_PRIO_MAX) priority = SCHED_PRIO_MAX;

    task->priority = priority;
    task->static_priority = priority;
    task->time_slice_max = sched_prio_to_slice(priority);

    /* Don't let current slice exceed new max */
    if (task->time_slice > task->time_slice_max) {
        task->time_slice = task->time_slice_max;
    }
}

int8_t task_get_priority(const struct task *task)
{
    if (task == 0) return 0;
    return task->priority;
}

/*
 * Phase 7: put current task to sleep for a given number of PIT ticks.
 * The task is removed from the runqueue and placed on the scheduler's
 * sorted sleep list. sched_check_sleepers() will wake it when the
 * deadline passes.
 */
void task_sleep_ticks(uint64_t ticks)
{
    if (current_task == 0 || ticks == 0) return;

    uint64_t rflags;
    __asm__ volatile (
        "pushfq\n"
        "pop %0\n"
        : "=r"(rflags)
        :
        : "memory");
    bool was_enabled = (rflags & (1ULL << 9)) != 0;

    interrupts_disable();

    current_task->sleep_until = pit_ticks() + ticks;
    current_task->state = TASK_SLEEPING;

    /*
     * Task stays in the runqueue — pick_next_task skips non-READY tasks.
     * We add it to the sorted sleep list so sched_check_sleepers can
     * set it back to TASK_READY when the deadline passes.
     */
    sched_add_sleeper(current_task);

    schedule();

    /* After wakeup */
    current_task->sleep_until = 0;
    current_task->sleep_next = 0;

    if (was_enabled) {
        interrupts_enable();
    }
}

static struct task *find_zombie_child(uint64_t pid)
{
    if (current_task == 0 || current_task->process == 0) return 0;
    struct process *child = current_task->process->children;
    while (child != 0) {
        struct process *next = child->sibling;
        if ((pid == 0 || child->pid == pid) && child->main_thread != 0
            && child->main_thread->state == TASK_ZOMBIE) {
            return child->main_thread;
        }
        child = next;
    }
    return 0;
}

static bool has_unfinished_child(uint64_t pid)
{
    if (current_task == 0 || current_task->process == 0) return false;
    struct process *child = current_task->process->children;
    while (child != 0) {
        struct process *next = child->sibling;
        if ((pid == 0 || child->pid == pid) && child->main_thread != 0) {
            return true;
        }
        child = next;
    }
    return false;
}

static int reap_zombie(struct task *zombie, int *exit_code)
{
    if (zombie == 0) return -1;
    if (exit_code != 0) *exit_code = zombie->exit_code;

    struct process *child = zombie->process;
    process_unlink_child(current_task->process, child);

    sched_remove_task(zombie);
    kfree(zombie->kstack_base);
    kfree(zombie);
    if (child != 0) {
        // Close and free child process file descriptors
        for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
            if (child->files[i] != 0) {
                file_close(child->files[i]);
                child->files[i] = 0;
            }
        }
        // Release file references held by VMAs
        for (int i = 0; i < VMA_MAX; i++) {
            if (child->vmas[i].valid && !child->vmas[i].is_anonymous && child->vmas[i].file != 0) {
                file_close(child->vmas[i].file);
                child->vmas[i].file = 0;
            }
        }
        if (child->owns_address_space && child->address_space != 0) {
            vmm_destroy_address_space(child->address_space);
            child->address_space = 0;
        }
        kfree(child);
    }
    return 0;
}

int task_wait(uint64_t pid, int *exit_code)
{
    if (current_task == 0 || current_task->process == 0) return -1;

    for (;;) {
        bool was_enabled = save_interrupts_and_disable();

        struct task *zombie = find_zombie_child(pid);
        if (zombie != 0) {
            int ret = reap_zombie(zombie, exit_code);
            restore_interrupts(was_enabled);
            return ret;
        }

        if (!has_unfinished_child(pid)) {
            restore_interrupts(was_enabled);
            return -1;
        }

        wait_queue_sleep_locked(&reaper_queue);
        restore_interrupts(was_enabled);
    }
}

int task_wait_any(int *exit_code, uint64_t *out_pid)
{
    if (current_task == 0 || current_task->process == 0) return -1;

    for (;;) {
        bool was_enabled = save_interrupts_and_disable();

        struct task *zombie = find_zombie_child(0);
        if (zombie != 0) {
            if (out_pid != 0) *out_pid = zombie->pid;
            int ret = reap_zombie(zombie, exit_code);
            restore_interrupts(was_enabled);
            return ret;
        }
        if (current_task->process->children == 0) {
            restore_interrupts(was_enabled);
            return -1;
        }
        wait_queue_sleep_locked(&reaper_queue);
        restore_interrupts(was_enabled);
    }
}

/*
 * task_wait_nohang — non-blocking variant used by WNOHANG.
 * Returns reaped pid on success, 0 if no zombie yet, -1 if no matching child.
 */
int task_wait_nohang(uint64_t pid, int *exit_code, uint64_t *out_pid)
{
    if (current_task == 0 || current_task->process == 0) return -1;

    bool was_enabled = save_interrupts_and_disable();

    struct task *zombie = find_zombie_child(pid);
    if (zombie != 0) {
        uint64_t reaped_pid = zombie->pid;
        int ret = reap_zombie(zombie, exit_code);
        if (out_pid != 0) *out_pid = reaped_pid;
        restore_interrupts(was_enabled);
        return (ret == 0) ? (int)reaped_pid : ret;
    }

    bool has_child = has_unfinished_child(pid);
    restore_interrupts(was_enabled);

    if (!has_child) return -1; /* ECHILD */
    return 0; /* no child exited yet */
}

int task_join(uint64_t tid, int *exit_code)
{
    if (current_task == 0 || current_task->process == 0) return -1;
    if (tid == current_task->tid) return -1;

    for (;;) {
        bool was_enabled = save_interrupts_and_disable();

        struct task *target = 0;
        struct task *cursor = sched_get_runqueue_head();
        while (cursor != 0) {
            if (cursor->tid == tid && cursor->process == current_task->process) {
                target = cursor;
                break;
            }
            cursor = cursor->next;
        }

        if (target == 0) {
            restore_interrupts(was_enabled);
            return -1;
        }

        if (target->state == TASK_ZOMBIE) {
            if (exit_code != 0) *exit_code = target->exit_code;

            sched_remove_task(target);
            kfree(target->kstack_base);
            kfree(target);

            restore_interrupts(was_enabled);
            return 0;
        }

        wait_queue_sleep_locked(&reaper_queue);
        restore_interrupts(was_enabled);
    }
}

void task_yield(void)
{
    schedule();
}

void process_init_standard_fds(struct process *proc)
{
    if (proc == 0) return;
    struct vfs_node *console = vfs_lookup("/dev/console");
    if (console == 0) return;

    struct file *f = kzalloc(sizeof(struct file));
    if (f == 0) return;

    f->node = console;
    f->offset = 0;
    f->flags = 3; // O_RDWR
    f->ref_count = 3;

    proc->files[0] = f;
    proc->files[1] = f;
    proc->files[2] = f;
}

void fork_child_entry(void *arg)
{
    struct syscall_frame *frame = (struct syscall_frame *)arg;
    struct task *self = current_task;
    if (self == 0) {
        panic("fork_child_entry: current_task is NULL");
    }

    // Switch address space
    vmm_switch_address_space(self->process->address_space);

    // Jump to user mode!
    syscall_return_to_user(frame);
}

struct task *task_fork(struct syscall_frame *parent_frame)
{
    struct task *parent_task = current_task;
    if (parent_task == 0) return 0;
    struct process *parent_process = parent_task->process;
    if (parent_process == 0) return 0;

    // 1. Create child process
    struct process *child_process = process_create_with_parent(parent_process);
    if (child_process == 0) return 0;

    // 2. Clone parent's address space
    struct address_space *cloned_space = vmm_clone_address_space(parent_process->address_space);
    if (cloned_space == 0) {
        process_unlink_child(parent_process, child_process);
        kfree(child_process);
        return 0;
    }
    child_process->address_space = cloned_space;
    child_process->owns_address_space = true;

    // Copy parent VMAs to child
    memcpy(child_process->vmas, parent_process->vmas, sizeof(parent_process->vmas));


    // 3. Allocate child task structure
    struct task *child_task = kzalloc(sizeof(struct task));
    if (child_task == 0) {
        vmm_destroy_address_space(cloned_space);
        process_unlink_child(parent_process, child_process);
        kfree(child_process);
        return 0;
    }

    child_task->kstack_base = kmalloc(KERNEL_STACK_SIZE);
    if (child_task->kstack_base == 0) {
        kfree(child_task);
        vmm_destroy_address_space(cloned_space);
        process_unlink_child(parent_process, child_process);
        kfree(child_process);
        return 0;
    }
    child_task->kstack_top = (void *)((uintptr_t)child_task->kstack_base + KERNEL_STACK_SIZE);

    // 4. Place child's syscall_frame at the top of its kernel stack
    struct syscall_frame *child_frame = (struct syscall_frame *)((uintptr_t)child_task->kstack_top - sizeof(struct syscall_frame));
    memcpy(child_frame, parent_frame, sizeof(struct syscall_frame));
    child_frame->rax = 0; // Return value for child is 0

    // 5. Initialize context-switch stack below child_frame
    uint64_t *stack = (uint64_t *)child_frame;
    *--stack = (uint64_t)task_trampoline;
    *--stack = 0; // r15
    *--stack = 0; // r14
    *--stack = 0; // r13
    *--stack = 0; // r12
    *--stack = 0; // rbp
    *--stack = 0; // rbx
    child_task->saved_rsp = (uint64_t)stack;

    // 6. Set remaining task properties
    child_task->pid = child_process->pid;
    child_task->tid = next_tid++;

    strcpy(child_task->name, parent_task->name);
    child_task->state = TASK_READY;
    child_task->mode = parent_task->mode;
    child_task->cr3 = cloned_space->pml4_phys;
    child_task->user_rip = parent_task->user_rip;
    child_task->user_rsp = parent_task->user_rsp;
    child_task->entry = fork_child_entry;
    child_task->entry_arg = child_frame;
    child_task->process = child_process;
    child_task->parent = parent_task;
    child_task->next = 0;
    child_task->wait_next = 0;
    child_task->wait_queue = 0;
    child_task->created_ticks = pit_ticks();
    child_task->exit_code = 0;
    child_task->cancel_requested = false;

    child_task->priority = parent_task->priority;
    child_task->static_priority = parent_task->static_priority;
    child_task->time_slice_max = parent_task->time_slice_max;
    child_task->time_slice = child_task->time_slice_max;
    child_task->total_ticks = 0;
    child_task->sleep_until = 0;
    child_task->flags = parent_task->flags & ~TASK_FLAG_IDLE;
    child_task->sleep_next = 0;
    child_task->signal_blocked = parent_task->signal_blocked;
    child_task->signal_pending = 0;

    child_process->main_thread = child_task;

    sched_add_task(child_task);

    return child_task;
}

struct task *task_clone_thread(struct syscall_frame *parent_frame, uint64_t flags, void *child_stack, int *parent_tid, int *child_tid, uint64_t newtls)
{
    (void)parent_tid;
    struct task *parent_task = current_task;
    if (parent_task == 0) return 0;
    struct process *process = parent_task->process;
    if (process == 0) return 0;

    // Allocate child task structure
    struct task *child_task = kzalloc(sizeof(struct task));
    if (child_task == 0) return 0;

    child_task->kstack_base = kmalloc(KERNEL_STACK_SIZE);
    if (child_task->kstack_base == 0) {
        kfree(child_task);
        return 0;
    }
    child_task->kstack_top = (void *)((uintptr_t)child_task->kstack_base + KERNEL_STACK_SIZE);

    // Place child's syscall_frame at the top of its kernel stack
    struct syscall_frame *child_frame = (struct syscall_frame *)((uintptr_t)child_task->kstack_top - sizeof(struct syscall_frame));
    memcpy(child_frame, parent_frame, sizeof(struct syscall_frame));
    child_frame->rax = 0; // Return value for child is 0
    if (child_stack != 0) {
        child_frame->user_rsp = (uint64_t)child_stack;
    }

    // Initialize context-switch stack below child_frame
    uint64_t *stack = (uint64_t *)child_frame;
    *--stack = (uint64_t)task_trampoline;
    *--stack = 0; // r15
    *--stack = 0; // r14
    *--stack = 0; // r13
    *--stack = 0; // r12
    *--stack = 0; // rbp
    *--stack = 0; // rbx
    child_task->saved_rsp = (uint64_t)stack;

    // Set remaining task properties
    child_task->pid = process->pid;
    child_task->tid = next_tid++;

    strcpy(child_task->name, parent_task->name);
    child_task->state = TASK_READY;
    child_task->mode = parent_task->mode;
    child_task->cr3 = parent_task->cr3;
    child_task->user_rip = parent_task->user_rip;
    child_task->user_rsp = (child_stack != 0) ? (uint64_t)child_stack : parent_task->user_rsp;
    child_task->entry = fork_child_entry;
    child_task->entry_arg = child_frame;
    child_task->process = process;
    child_task->parent = parent_task;
    child_task->next = 0;
    child_task->wait_next = 0;
    child_task->wait_queue = 0;
    child_task->created_ticks = pit_ticks();
    child_task->exit_code = 0;
    child_task->cancel_requested = false;

    child_task->priority = parent_task->priority;
    child_task->static_priority = parent_task->static_priority;
    child_task->time_slice_max = parent_task->time_slice_max;
    child_task->time_slice = child_task->time_slice_max;
    child_task->total_ticks = 0;
    child_task->sleep_until = 0;
    child_task->flags = parent_task->flags & ~TASK_FLAG_IDLE;
    child_task->sleep_next = 0;
    child_task->signal_blocked = parent_task->signal_blocked;
    child_task->signal_pending = 0;

    // Thread TLS (newtls)
    if (flags & CLONE_SETTLS) {
        child_task->fs_base = newtls;
    } else {
        child_task->fs_base = parent_task->fs_base;
    }

    // Clear child TID on exit
    if (flags & CLONE_CHILD_CLEARTID) {
        child_task->clear_child_tid = (uint32_t *)child_tid;
    }

    sched_add_task(child_task);

    return child_task;
}


/* Helper functions for signal handling (Phase 16) */
static struct process *find_process_rec(struct process *root, uint64_t pid)
{
    if (root == 0) return 0;
    if (root->pid == pid) return root;

    struct process *child = root->children;
    while (child != 0) {
        struct process *found = find_process_rec(child, pid);
        if (found != 0) return found;
        child = child->sibling;
    }
    return 0;
}

struct process *find_process(uint64_t pid)
{
    return find_process_rec(init_process, pid);
}

void task_send_signal(struct task *task, int sig)
{
    if (task == 0 || sig <= 0 || sig >= 64) return;

    bool was_enabled = save_interrupts_and_disable();

    task->signal_pending |= (1ULL << sig);

    /* Wake up the task if it is sleeping on a wait queue */
    if (task->state == TASK_SLEEPING) {
        if (!(task->signal_blocked & (1ULL << sig))) {
            if (task->wait_queue != 0) {
                struct wait_queue *wq = task->wait_queue;
                struct task *prev = 0;
                struct task *curr = wq->head;
                while (curr != 0) {
                    if (curr == task) {
                        if (prev != 0) {
                            prev->wait_next = task->wait_next;
                        } else {
                            wq->head = task->wait_next;
                        }
                        if (wq->tail == task) {
                            wq->tail = prev;
                        }
                        break;
                    }
                    prev = curr;
                    curr = curr->wait_next;
                }
                task->wait_next = 0;
                task->wait_queue = 0;
            }
            task->state = TASK_READY;
            sched_add_task(task);
        }
    }

    restore_interrupts(was_enabled);
}

void task_deliver_signals(void)
{
    struct task *task = current_task;
    if (task == 0 || task->state != TASK_RUNNING) return;

    uint64_t active = task->signal_pending & ~task->signal_blocked;
    if (active == 0) return;

    for (int sig = 1; sig < 64; sig++) {
        if (active & (1ULL << sig)) {
            task->signal_pending &= ~(1ULL << sig);

            if (sig == 17) { // SIGCHLD
                continue;
            }

            printk("Task %s (PID %llu) killed by signal %d\n", task->name, task->pid, sig);
            task_exit(-sig);
        }
    }
}

int futex_wake_address(uint32_t *uaddr, int val)
{
    if (uaddr == 0) return 0;

    bool was_enabled = save_interrupts_and_disable();

    int woken = 0;
    struct task *curr = sched_get_runqueue_head();
    while (curr != 0) {
        if (curr->state == TASK_SLEEPING && curr->futex_uaddr == uaddr) {
            curr->state = TASK_READY;
            curr->futex_uaddr = 0;
            woken++;
            if (woken >= val) {
                break;
            }
        }
        curr = curr->next;
    }

    restore_interrupts(was_enabled);
    return woken;
}
