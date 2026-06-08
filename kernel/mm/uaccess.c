#include <mm/uaccess.h>
#include <arch/x86_64/vmm.h>
#include <sched/task.h>
#include <lib/string.h>

/*
 * uaccess.c — safe user-space memory access helpers.
 *
 * Phase 8 validates pointers using vmm_is_user_address() which checks
 * VMM_USER_BASE..VMM_USER_TOP. Actual page-table validation is deferred
 * to Phase 9 when real user address spaces exist.
 *
 * Overflow safety: we check that ptr + len does not wrap around.
 */

int uaccess_ok(const void *ptr, size_t len)
{
    if (ptr == 0) return 0;
    if (len == 0) return 1;

    uintptr_t start = (uintptr_t)ptr;
    uintptr_t end;

    /* Check for pointer arithmetic overflow */
    if (__builtin_add_overflow(start, len - 1, &end)) {
        return 0;
    }

    /* Both start and end must be in user address range */
    if (!vmm_is_user_address((virt_addr_t)start)) return 0;
    if (!vmm_is_user_address((virt_addr_t)end))   return 0;

    return 1;
}

int copy_from_user(void *kdst, const void *usrc, size_t len)
{
    if (kdst == 0) return -1;
    if (len == 0)  return 0;

    if (!uaccess_ok(usrc, len)) return -1;

    struct address_space *space = 0;
    struct task *curr = current_task;
    if (curr != 0 && curr->process != 0) {
        space = curr->process->address_space;
    }
    if (space == 0) {
        space = vmm_kernel_address_space();
    }

    return vmm_copy_from_user(kdst, space, usrc, len);
}

int copy_to_user(void *udst, const void *ksrc, size_t len)
{
    if (ksrc == 0) return -1;
    if (len == 0)  return 0;

    if (!uaccess_ok(udst, len)) return -1;

    struct address_space *space = 0;
    struct task *curr = current_task;
    if (curr != 0 && curr->process != 0) {
        space = curr->process->address_space;
    }
    if (space == 0) {
        space = vmm_kernel_address_space();
    }

    return vmm_copy_to_user(space, udst, ksrc, len);
}
