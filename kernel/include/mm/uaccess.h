#ifndef LITENIX_MM_UACCESS_H
#define LITENIX_MM_UACCESS_H

#include <stddef.h>
#include <stdint.h>

/*
 * User-space access helpers — Phase 8.
 *
 * These functions validate that [ptr, ptr+len) lies entirely within the
 * user address range before copying. On validation failure they return -1
 * (caller converts to -EFAULT).
 *
 * In Phase 8 there is no real user address space yet (Phase 9 adds it).
 * The implementation checks the pointer against VMM_USER_BASE / VMM_USER_TOP.
 * When called from the kernel self-test with a kernel pointer they will
 * correctly return -EFAULT, exercising the validation path.
 *
 * Phase 9 will pass the current task's address_space for proper PTE validation.
 */

/*
 * copy_from_user — copy `len` bytes from user address `usrc` to kernel `kdst`.
 * Returns 0 on success, -1 if the pointer is invalid.
 */
int copy_from_user(void *kdst, const void *usrc, size_t len);

/*
 * copy_to_user — copy `len` bytes from kernel `ksrc` to user address `udst`.
 * Returns 0 on success, -1 if the pointer is invalid.
 */
int copy_to_user(void *udst, const void *ksrc, size_t len);

/*
 * uaccess_ok — check whether [ptr, ptr+len) is a valid user range.
 * Returns 1 (true) if valid, 0 if not.
 */
int uaccess_ok(const void *ptr, size_t len);

#endif
