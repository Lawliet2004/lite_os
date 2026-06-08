#ifndef LITENIX_SYS_SYSCALL_TABLE_H
#define LITENIX_SYS_SYSCALL_TABLE_H

#include <sys/syscall.h>
#include <stdint.h>

/* Syscall handler function type */
typedef int64_t (*syscall_fn_t)(struct syscall_frame *frame);

/* Maximum number of syscall table entries */
#define SYSCALL_TABLE_SIZE 512

/*
 * syscall_table_init() — populate the dispatch table.
 * Must be called before any syscall can be handled.
 */
void syscall_table_init(void);

/*
 * syscall_dispatch() — called from syscall_stub.S.
 * Looks up frame->rax in the table; returns -ENOSYS for unknowns.
 * With SYSCALL_TRACE defined, logs every call + return value.
 */
int64_t syscall_dispatch(struct syscall_frame *frame);

#endif
