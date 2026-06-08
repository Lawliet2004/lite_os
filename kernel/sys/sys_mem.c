/*
 * sys_mem.c — userspace memory management syscalls.
 *
 * Implements: brk, mmap (anonymous-only stub), munmap stub, mprotect stub.
 *
 * brk: grows/shrinks the process heap by mapping/unmapping 4 KiB pages.
 *      The heap_start is set by the ELF loader (page-aligned end of BSS).
 *      heap_end tracks the current program break.
 *
 * mmap: anonymous private mappings only (MAP_ANONYMOUS|MAP_PRIVATE).
 *       File-backed and shared mappings return -ENOSYS (Phase 23).
 */
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <sched/task.h>
#include <arch/x86_64/vmm.h>
#include <mm/pmm.h>
#include <mm/uaccess.h>
#include <stdint.h>

/* mmap flags (Linux ABI) */
#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_ANON      MAP_ANONYMOUS
#define MAP_FAILED    ((void *)-1)

/* mmap prot flags */
#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

/* ------------------------------------------------------------------ */
/* Helper: align address up to page boundary                           */
/* ------------------------------------------------------------------ */
static inline uint64_t page_align_up(uint64_t addr)
{
    return (addr + 0xfff) & ~(uint64_t)0xfff;
}

static inline uint64_t page_align_down(uint64_t addr)
{
    return addr & ~(uint64_t)0xfff;
}

/* ------------------------------------------------------------------ */
/* sys_brk                                                              */
/* ------------------------------------------------------------------ */

/*
 * sys_brk(addr):
 *   - addr == 0 → return current brk (heap_end)
 *   - addr > heap_end → grow heap, map new pages
 *   - addr < heap_end → shrink heap, unmap pages
 *   - addr < heap_start → return -EINVAL
 *
 * On success returns the new (or unchanged) brk value.
 * On failure returns -errno.
 */
int64_t sys_brk(struct syscall_frame *frame)
{
    uint64_t new_brk = (uint64_t)frame->rdi;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;

    struct process *proc = current_task->process;

    /* Query: return current brk */
    if (new_brk == 0) {
        return (int64_t)proc->heap_end;
    }

    /* Validate bounds */
    if (new_brk < proc->heap_start) {
        return -(int64_t)EINVAL;
    }
    if (new_brk - proc->heap_start > PROCESS_HEAP_MAX) {
        return -(int64_t)ENOMEM;
    }

    uint64_t old_end = page_align_up(proc->heap_end);
    uint64_t new_end = page_align_up(new_brk);

    if (new_end > old_end) {
        /* Grow: map new pages */
        for (uint64_t vaddr = old_end; vaddr < new_end; vaddr += VMM_PAGE_SIZE) {
            phys_addr_t phys = pmm_alloc_page();
            if (phys == 0) {
                /* Partial growth — leave heap_end at last successful page */
                proc->heap_end = vaddr;
                return -(int64_t)ENOMEM;
            }
            /* Use vmm_map; PMM allocator guarantees zeroed pages */
            if (vmm_map(proc->address_space, vaddr, phys,
                        VMM_PRESENT | VMM_WRITABLE | VMM_USER | VMM_NO_EXECUTE) != 0) {
                pmm_free_page(phys);
                proc->heap_end = vaddr;
                return -(int64_t)ENOMEM;
            }
        }
    } else if (new_end < old_end) {
        /* Shrink: unmap pages */
        for (uint64_t vaddr = new_end; vaddr < old_end; vaddr += VMM_PAGE_SIZE) {
            phys_addr_t phys;
            if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
                vmm_unmap(proc->address_space, vaddr);
                pmm_free_page(phys);
            }
        }
    }

    proc->heap_end = new_brk;
    return (int64_t)new_brk;
}

/* ------------------------------------------------------------------ */
/* sys_mmap — anonymous private mappings only                          */
/* ------------------------------------------------------------------ */

/*
 * Anonymous mmap allocates pages and maps them into the process address space.
 * We find a free region above the heap and below the stack.
 * File-backed mappings are not yet supported.
 *
 * Linux prototype:
 *   void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
 *   args: rdi=addr, rsi=len, rdx=prot, r10=flags, r8=fd, r9=offset
 */

/* mmap allocation arena: between heap and stack */
#define MMAP_BASE 0x00007f0000000000ULL  /* 127 TB user space */
#define MMAP_TOP  0x00007fff00000000ULL

static uint64_t mmap_hint = MMAP_BASE;  /* simple bump allocator for addresses */

int64_t sys_munmap(struct syscall_frame *frame);

int64_t sys_mmap(struct syscall_frame *frame)
{
    uint64_t addr   = (uint64_t)frame->rdi;
    uint64_t length = (uint64_t)frame->rsi;
    int      prot   = (int)frame->rdx;
    int      flags  = (int)frame->r10;
    int      fd     = (int)frame->r8;
    /* offset (frame->r9) ignored for anonymous mappings */

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;

    /* Only support anonymous private mappings */
    if (!(flags & MAP_ANONYMOUS)) {
        /* File-backed mmap — Phase 23 */
        (void)fd;
        return -(int64_t)ENOSYS;
    }

    if (length == 0) return -(int64_t)EINVAL;

    uint64_t map_addr;
    struct process *proc = current_task->process;

    if ((flags & MAP_FIXED) && addr != 0) {
        map_addr = page_align_down(addr);
        /* munmap any existing mappings in that range first */
        struct syscall_frame dummy_frame;
        dummy_frame.rdi = map_addr;
        dummy_frame.rsi = length;
        sys_munmap(&dummy_frame);
    } else {
        /* Bump-allocate from mmap_hint */
        map_addr = mmap_hint;
        mmap_hint += page_align_up(length) + VMM_PAGE_SIZE; /* gap page */
        if (mmap_hint >= MMAP_TOP) {
            mmap_hint = MMAP_BASE;
            return -(int64_t)ENOMEM;
        }
    }

    /* Compute page flags from prot */
    uint64_t vflags = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vflags |= VMM_WRITABLE;
    if (!(prot & PROT_EXEC)) vflags |= VMM_NO_EXECUTE;

    /* Allocate a VMA slot */
    int free_slot = -1;
    for (int i = 0; i < VMA_MAX; i++) {
        if (!proc->vmas[i].valid) {
            free_slot = i;
            break;
        }
    }

    if (free_slot == -1) {
        return -(int64_t)ENOMEM;
    }

    proc->vmas[free_slot].start = map_addr;
    proc->vmas[free_slot].end = map_addr + page_align_up(length);
    proc->vmas[free_slot].flags = vflags;
    proc->vmas[free_slot].valid = true;

    return (int64_t)map_addr;
}

/* ------------------------------------------------------------------ */
/* sys_munmap — unmap and free pages                                   */
/* ------------------------------------------------------------------ */

int64_t sys_munmap(struct syscall_frame *frame)
{
    uint64_t addr   = (uint64_t)frame->rdi;
    uint64_t length = (uint64_t)frame->rsi;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;
    if (length == 0) return -(int64_t)EINVAL;
    if (addr & 0xfff) return -(int64_t)EINVAL; /* must be page-aligned */

    struct process *proc = current_task->process;
    uint64_t pages = page_align_up(length) / VMM_PAGE_SIZE;

    /* Unmap physical pages first */
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t vaddr = addr + i * VMM_PAGE_SIZE;
        phys_addr_t phys;
        if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
            vmm_unmap(proc->address_space, vaddr);
            pmm_free_page(phys);
        }
    }

    /* Update VMAs */
    uint64_t start = addr;
    uint64_t end = addr + page_align_up(length);
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (!vma->valid) continue;

        if (vma->start >= start && vma->end <= end) {
            vma->valid = false;
        }
        else if (vma->start < start && vma->end > end) {
            uint64_t old_end = vma->end;
            vma->end = start;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    proc->vmas[j].start = end;
                    proc->vmas[j].end = old_end;
                    proc->vmas[j].flags = vma->flags;
                    proc->vmas[j].valid = true;
                    break;
                }
            }
        }
        else if (vma->start < start && vma->end > start && vma->end <= end) {
            vma->end = start;
        }
        else if (vma->start >= start && vma->start < end && vma->end > end) {
            vma->start = end;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_mprotect — change permissions on existing pages                 */
/* ------------------------------------------------------------------ */

int64_t sys_mprotect(struct syscall_frame *frame)
{
    uint64_t addr   = (uint64_t)frame->rdi;
    uint64_t length = (uint64_t)frame->rsi;
    int      prot   = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;
    if (addr & 0xfff) return -(int64_t)EINVAL;
    if (length == 0) return 0;

    struct process *proc = current_task->process;
    uint64_t vflags = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vflags |= VMM_WRITABLE;
    if (!(prot & PROT_EXEC)) vflags |= VMM_NO_EXECUTE;

    uint64_t pages = page_align_up(length) / VMM_PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t vaddr = addr + i * VMM_PAGE_SIZE;
        phys_addr_t phys;
        if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
            vmm_protect(proc->address_space, vaddr, vflags);
        }
    }

    /* Update VMAs */
    uint64_t start = addr;
    uint64_t end = addr + page_align_up(length);
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (!vma->valid) continue;

        if (vma->start >= start && vma->end <= end) {
            vma->flags = vflags;
        }
        else if (vma->start < start && vma->end > end) {
            uint64_t old_end = vma->end;
            uint32_t old_flags = vma->flags;
            vma->end = start;
            int mid_idx = -1;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    proc->vmas[mid_idx = j].start = start;
                    proc->vmas[j].end = end;
                    proc->vmas[j].flags = vflags;
                    proc->vmas[j].valid = true;
                    break;
                }
            }
            if (mid_idx != -1) {
                for (int j = 0; j < VMA_MAX; j++) {
                    if (!proc->vmas[j].valid) {
                        proc->vmas[j].start = end;
                        proc->vmas[j].end = old_end;
                        proc->vmas[j].flags = old_flags;
                        proc->vmas[j].valid = true;
                        break;
                    }
                }
            }
        }
        else if (vma->start < start && vma->end > start && vma->end <= end) {
            uint64_t old_end = vma->end;
            vma->end = start;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    proc->vmas[j].start = start;
                    proc->vmas[j].end = old_end;
                    proc->vmas[j].flags = vflags;
                    proc->vmas[j].valid = true;
                    break;
                }
            }
        }
        else if (vma->start >= start && vma->start < end && vma->end > end) {
            uint64_t old_start = vma->start;
            vma->start = end;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    proc->vmas[j].start = old_start;
                    proc->vmas[j].end = end;
                    proc->vmas[j].flags = vflags;
                    proc->vmas[j].valid = true;
                    break;
                }
            }
        }
    }

    return 0;
}
