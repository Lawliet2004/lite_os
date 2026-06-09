#ifndef LITENIX_SCHED_TASK_H
#define LITENIX_SCHED_TASK_H

#include <arch/x86_64/vmm.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define TASK_NAME_MAX 32
#define KERNEL_STACK_SIZE 16384

/* Phase 7: priority and time-slice constants */
#define SCHED_PRIO_MIN   (-20)
#define SCHED_PRIO_MAX   19
#define SCHED_PRIO_DEFAULT 0
#define SCHED_BASE_SLICE 1
#define SCHED_BOOST_INTERVAL 500  /* ticks between starvation boost scans */
/* Minimum ticks without running before a task is considered starved */
#define SCHED_STARVE_THRESHOLD 200

/* Task flags */
#define TASK_FLAG_IDLE   (1U << 0)

/* Process heap limits */
#define PROCESS_HEAP_MAX  (256ULL * 1024 * 1024)  /* 256 MiB cap */

enum task_state {
    TASK_READY,
    TASK_RUNNING,
    TASK_SLEEPING,
    TASK_ZOMBIE,
};

enum task_mode {
    TASK_MODE_KERNEL,
    TASK_MODE_USER,
};

struct wait_queue;
struct file;

#define MAX_FILES_PER_PROCESS 64

#define TASK_CWD_MAX 256

struct vma {
    uint64_t start;
    uint64_t end;
    uint32_t prot_flags;   /* PROT_READ | PROT_WRITE | PROT_EXEC */
    uint32_t mmap_flags;   /* MAP_PRIVATE | MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED */
    bool is_anonymous;
    bool is_private;
    struct file *file;     /* NULL if anonymous */
    uint64_t file_offset;
    uint32_t flags;        /* Page table flags (e.g. VMM_PRESENT | VMM_USER) */
    bool valid;
};

#define VMA_MAX 32

struct process {
    uint64_t pid;
    struct process *parent;
    struct process *children;
    struct process *sibling;
    struct task *main_thread;
    struct address_space *address_space;
    bool owns_address_space;
    bool exited;
    int exit_code;
    struct file *files[MAX_FILES_PER_PROCESS];

    /* Current working directory (absolute path, always starts with /) */
    char cwd[TASK_CWD_MAX];

    /* Heap (brk) tracking */
    uint64_t heap_start;  /* set by ELF loader: page-aligned end of BSS */
    uint64_t heap_end;    /* current brk pointer */

    struct vma vmas[VMA_MAX];
};

struct task {
    uint64_t pid;
    uint64_t tid;
    char name[TASK_NAME_MAX];
    enum task_state state;
    enum task_mode mode;
    uint64_t saved_rsp;
    uint64_t saved_irq_rsp;
    uint64_t cr3;
    uint64_t user_rip;
    uint64_t user_rsp;
    void *kstack_base;
    void *kstack_top;
    void (*entry)(void *);
    void *entry_arg;
    struct process *process;
    struct task *parent;
    struct task *next;
    struct task *wait_next;
    struct wait_queue *wait_queue;
    uint64_t created_ticks;
    int exit_code;
    bool cancel_requested;

    /* Phase 7: priority-based scheduler fields */
    int8_t priority;        /* nice value: -20 (highest) .. +19 (lowest) */
    int8_t static_priority; /* original priority before boost */
    uint32_t time_slice;    /* ticks remaining in current quantum */
    uint32_t time_slice_max;/* full quantum for this priority */
    uint64_t total_ticks;   /* lifetime CPU ticks consumed */
    uint64_t last_run_tick; /* PIT tick when this task last ran (for starvation) */
    uint64_t sleep_until;   /* PIT tick deadline for timed sleep (0=wait queue) */
    uint32_t flags;         /* TASK_FLAG_* */

    /* Phase 16: Signal fields */
    uint64_t signal_pending;
    uint64_t signal_blocked;

    /* Phase 17: Threading fields */
    uint64_t fs_base;
    uint32_t *clear_child_tid;
    uint32_t *futex_uaddr;
    void *robust_list_head;
    size_t robust_list_len;

    /* Phase 7: sleep list linkage */
    struct task *sleep_next;
    int current_file_flags;
};

/* Compute time-slice for a given nice value */
static inline uint32_t sched_prio_to_slice(int8_t priority)
{
    return (uint32_t)(SCHED_BASE_SLICE + (19 - priority));
}

void task_init(void);
void jump_to_usermode(uintptr_t entry, uintptr_t stack_top);
struct process *process_create_with_parent(struct process *parent);
void process_unlink_child(struct process *parent, struct process *child);
void process_init_standard_fds(struct process *proc);
struct task *task_create(const char *name, void (*entry)(void));
struct task *task_create_arg(const char *name, void (*entry)(void *), void *arg);
struct task *task_thread_create(const char *name, struct process *process,
    void (*entry)(void *), void *arg);
void task_exit(int exit_code);
int task_wait(uint64_t pid, int *exit_code);
int task_wait_nohang(uint64_t pid, int *exit_code, uint64_t *out_pid);
int task_join(uint64_t tid, int *exit_code);
int task_wait_any(int *exit_code, uint64_t *out_pid);
#define CLONE_VM             0x00000100
#define CLONE_FS             0x00000200
#define CLONE_FILES          0x00000400
#define CLONE_SIGHAND        0x00000800
#define CLONE_THREAD         0x00010000
#define CLONE_SYSVSEM        0x00040000
#define CLONE_SETTLS         0x00080000
#define CLONE_PARENT_SETTID  0x00100000
#define CLONE_CHILD_CLEARTID 0x00200000
#define CLONE_CHILD_SETTID   0x01000000

struct syscall_frame;
struct task *task_fork(struct syscall_frame *parent_frame);
struct task *task_clone_thread(struct syscall_frame *parent_frame, uint64_t flags, void *child_stack, int *parent_tid, int *child_tid, uint64_t newtls);
void syscall_return_to_user(struct syscall_frame *frame) __attribute__((noreturn));
void fork_child_entry(void *arg);


void task_yield(void);
void task_kill(struct task *task);
void task_request_cancel(struct task *task);

/* Phase 7: priority and sleep APIs */
void task_set_priority(struct task *task, int8_t priority);
int8_t task_get_priority(const struct task *task);
void task_sleep_ticks(uint64_t ticks);

/* Phase 16: Signal APIs */
struct process *find_process(uint64_t pid);
void task_send_signal(struct task *task, int sig);
void task_deliver_signals(void);
int futex_wake_address(uint32_t *uaddr, int val);

extern struct task *current_task;
extern struct process *init_process;

#endif
