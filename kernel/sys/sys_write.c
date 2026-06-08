#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <mm/uaccess.h>
#include <drivers/serial.h>
#include <stdint.h>
#include <fs/vfs.h>
#include <sched/task.h>

#define WRITE_MAX 4096  /* maximum bytes per write call */

int64_t sys_write(struct syscall_frame *frame)
{
    int fd         = (int)(int64_t)frame->rdi;
    const void *buf = (const void *)frame->rsi;
    size_t count   = (size_t)frame->rdx;

    if (count == 0) {
        return 0;
    }

    if (count > WRITE_MAX) {
        count = WRITE_MAX;
    }

    // Check if the process has this file descriptor open
    if (current_task && current_task->process && fd >= 0 && fd < MAX_FILES_PER_PROCESS && current_task->process->files[fd] != 0) {
        struct file *f = current_task->process->files[fd];
        if (f->node == 0 || f->node->write == 0) {
            return -(int64_t)EBADF;
        }

        uint8_t kbuf[WRITE_MAX];
        int err = copy_from_user(kbuf, buf, count);
        if (err != 0) {
            return -(int64_t)EFAULT;
        }

        int written = f->node->write(f->node, f->offset, kbuf, count);
        if (written > 0) {
            f->offset += written;
        }
        return (int64_t)written;
    }

    /* Fallback stdout (1) and stderr (2) direct output */
    if (fd != 1 && fd != 2) {
        return -(int64_t)EBADF;
    }

    uint8_t kbuf[WRITE_MAX];
    int err = copy_from_user(kbuf, buf, count);
    if (err != 0) {
        return -(int64_t)EFAULT;
    }

    for (size_t i = 0; i < count; i++) {
        serial_write_char((char)kbuf[i]);
    }

    return (int64_t)count;
}
