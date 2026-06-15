/*
 * sys_mem.c — userspace memory management syscalls.
 *
 * Implements: brk, mmap (anonymous-only stub), munmap stub, mprotect stub.
 *
 * brk: grows/shrinks the process heap by mapping/unmapping 4 KiB pages.
 *      The heap_start is set by the ELF loader (page-aligned end of BSS).
 *      heap_end tracks the current program break.
 *
 * mmap: anonymous private and file-backed private mappings.
 *       Shared mappings are validated, then return -ENOSYS until shared
 *       page-cache/writeback semantics exist.
 */
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <sched/task.h>
#include <arch/x86_64/vmm.h>
#include <mm/pmm.h>
#include <mm/uaccess.h>
#include <fs/vfs.h>
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

static int vma_find_exact_slot(struct process *proc, uint64_t start, uint64_t end)
{
    for (int i = 0; i < VMA_MAX; i++) {
        if (proc->vmas[i].valid &&
            proc->vmas[i].start <= start &&
            proc->vmas[i].end >= end) {
            return i;
        }
    }
    return -1;
}

static void vma_drop_present_pages(struct process *proc, uint64_t start, uint64_t end)
{
    for (uint64_t vaddr = page_align_down(start); vaddr < page_align_up(end); vaddr += VMM_PAGE_SIZE) {
        phys_addr_t phys;
        if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
            vmm_unmap(proc->address_space, vaddr);
            pmm_free_page(phys);
        }
    }
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

static uint64_t mmap_hint = MMAP_BASE;  /* mmap hint for finding free virtual regions */

/* ------------------------------------------------------------------ */
/* VMA Helper functions                                               */
/* ------------------------------------------------------------------ */

struct vma *vma_find(struct process *proc, uint64_t addr)
{
    for (int i = 0; i < VMA_MAX; i++) {
        if (proc->vmas[i].valid && addr >= proc->vmas[i].start && addr < proc->vmas[i].end) {
            return &proc->vmas[i];
        }
    }
    return 0;
}

bool vma_overlaps(struct process *proc, uint64_t start, uint64_t end)
{
    for (int i = 0; i < VMA_MAX; i++) {
        if (proc->vmas[i].valid) {
            if (start < proc->vmas[i].end && end > proc->vmas[i].start) {
                return true;
            }
        }
    }
    return false;
}

void vma_merge_adjacent(struct process *proc)
{
    bool merged;
    do {
        merged = false;
        for (int i = 0; i < VMA_MAX; i++) {
            if (!proc->vmas[i].valid) continue;
            for (int j = 0; j < VMA_MAX; j++) {
                if (i == j || !proc->vmas[j].valid) continue;

                if (proc->vmas[i].end == proc->vmas[j].start) {
                    if (proc->vmas[i].prot_flags == proc->vmas[j].prot_flags &&
                        proc->vmas[i].mmap_flags == proc->vmas[j].mmap_flags &&
                        proc->vmas[i].is_anonymous == proc->vmas[j].is_anonymous &&
                        proc->vmas[i].is_private == proc->vmas[j].is_private &&
                        proc->vmas[i].file == proc->vmas[j].file &&
                        proc->vmas[i].flags == proc->vmas[j].flags) {
                        
                        if (proc->vmas[i].is_anonymous ||
                            (proc->vmas[i].file_offset + (proc->vmas[i].end - proc->vmas[i].start) == proc->vmas[j].file_offset)) {
                            
                            proc->vmas[i].end = proc->vmas[j].end;
                            if (!proc->vmas[j].is_anonymous && proc->vmas[j].file != 0) {
                                file_close(proc->vmas[j].file);
                            }
                            proc->vmas[j].valid = false;
                            merged = true;
                            break;
                        }
                    }
                }
            }
            if (merged) break;
        }
    } while (merged);
}

uint64_t vma_find_free_region(struct process *proc, uint64_t hint, uint64_t length)
{
    uint64_t aligned_len = page_align_up(length);
    uint64_t addr = (hint != 0) ? page_align_down(hint) : mmap_hint;

    if (addr < MMAP_BASE || addr + aligned_len > MMAP_TOP) {
        addr = MMAP_BASE;
    }

    uint64_t start_addr = addr;
    bool wrapped = false;
    while (1) {
        if (addr + aligned_len > MMAP_TOP) {
            if (wrapped) {
                return 0;
            }
            addr = MMAP_BASE;
            wrapped = true;
            continue;
        }

        bool overlap = false;
        for (int i = 0; i < VMA_MAX; i++) {
            if (proc->vmas[i].valid) {
                if (addr < proc->vmas[i].end && addr + aligned_len > proc->vmas[i].start) {
                    addr = page_align_up(proc->vmas[i].end);
                    overlap = true;
                    break;
                }
            }
        }

        if (!overlap) {
            mmap_hint = page_align_up(addr + aligned_len + VMM_PAGE_SIZE);
            if (mmap_hint >= MMAP_TOP) {
                mmap_hint = MMAP_BASE;
            }
            return addr;
        }

        if (wrapped && addr >= start_addr) {
            return 0;
        }
    }
}

int64_t sys_munmap(struct syscall_frame *frame);

int64_t sys_mmap(struct syscall_frame *frame)
{
    uint64_t addr   = (uint64_t)frame->rdi;
    uint64_t length = (uint64_t)frame->rsi;
    int      prot   = (int)frame->rdx;
    int      flags  = (int)frame->r10;
    int      fd     = (int)frame->r8;
    uint64_t offset = (uint64_t)frame->r9;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;

    struct process *proc = current_task->process;

    bool map_shared = (flags & MAP_SHARED) != 0;
    bool map_private = (flags & MAP_PRIVATE) != 0;
    if (map_shared == map_private) {
        return -(int64_t)EINVAL;
    }
    if (length == 0) return -(int64_t)EINVAL;
    if (length > UINT64_MAX - 0xfff) return -(int64_t)EINVAL;
    uint64_t aligned_len = page_align_up(length);
    if (addr + aligned_len < addr) return -(int64_t)EINVAL;
    if (flags & MAP_FIXED) {
        if (addr & 0xfff) return -(int64_t)EINVAL;
        if (addr < VMM_USER_BASE || addr + aligned_len > VMM_USER_TOP) {
            return -(int64_t)EINVAL;
        }
    }

    /* Handle file-backed mappings */
    if (!(flags & MAP_ANONYMOUS)) {
        /* Validate fd */
        if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0) {
            return -(int64_t)EBADF;
        }

        struct file *f = proc->files[fd];
        if (f->node == 0) {
            return -(int64_t)EBADF;
        }

        /* Must be a regular file */
        if (f->node->type != VFS_TYPE_FILE) {
            return -(int64_t)ENODEV;
        }

        /* Offset must be page-aligned */
        if (offset & 0xfff) {
            return -(int64_t)EINVAL;
        }

        if (map_shared) {
            return -(int64_t)ENOSYS;
        }

        uint64_t map_addr;

        if (flags & MAP_FIXED) {
            map_addr = addr;

            /* munmap any existing mappings in that range first */
            struct syscall_frame dummy_frame;
            dummy_frame.rdi = map_addr;
            dummy_frame.rsi = aligned_len;
            sys_munmap(&dummy_frame);
        } else {
            map_addr = vma_find_free_region(proc, addr, aligned_len);
            if (map_addr == 0) {
                return -(int64_t)ENOMEM;
            }
        }

        /* Compute page flags from prot */
        uint64_t vflags = VMM_PRESENT | VMM_USER;
        if (prot & PROT_WRITE) vflags |= VMM_WRITABLE;
        if (!(prot & PROT_EXEC)) vflags |= VMM_NO_EXECUTE;

        /* For MAP_PRIVATE with PROT_WRITE, we map read-only initially (COW) */
        if (map_private && (prot & PROT_WRITE)) {
            vflags &= ~VMM_WRITABLE;
        }

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
        proc->vmas[free_slot].end = map_addr + aligned_len;
        proc->vmas[free_slot].prot_flags = prot;
        proc->vmas[free_slot].mmap_flags = flags;
        proc->vmas[free_slot].is_anonymous = false;
        proc->vmas[free_slot].is_private = true;
        proc->vmas[free_slot].file = f;
        f->ref_count++;  /* VMA holds a reference */
        proc->vmas[free_slot].file_offset = offset;
        proc->vmas[free_slot].flags = vflags;
        proc->vmas[free_slot].valid = true;

        vma_merge_adjacent(proc);

        return (int64_t)map_addr;
    }

    if (map_shared) {
        return -(int64_t)ENOSYS;
    }

    /* Anonymous-only path continues */

    /* proc already declared above */
    uint64_t map_addr;

    if (flags & MAP_FIXED) {
        map_addr = addr;

        /* munmap any existing mappings in that range first */
        struct syscall_frame dummy_frame;
        dummy_frame.rdi = map_addr;
        dummy_frame.rsi = aligned_len;
        sys_munmap(&dummy_frame);
    } else {
        map_addr = vma_find_free_region(proc, addr, aligned_len);
        if (map_addr == 0) {
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
    proc->vmas[free_slot].end = map_addr + aligned_len;
    proc->vmas[free_slot].prot_flags = prot;
    proc->vmas[free_slot].mmap_flags = flags;
    proc->vmas[free_slot].is_anonymous = true;
    proc->vmas[free_slot].is_private = (flags & MAP_PRIVATE) ? true : false;
    proc->vmas[free_slot].file = 0;
    proc->vmas[free_slot].file_offset = 0;
    proc->vmas[free_slot].flags = vflags;
    proc->vmas[free_slot].valid = true;

    vma_merge_adjacent(proc);

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
    uint64_t aligned_len = page_align_up(length);
    if (addr + aligned_len < addr) return -(int64_t)EINVAL;

    uint64_t start = addr;
    uint64_t end = addr + aligned_len;

    /* Pre-check if split slot is needed and available */
    bool need_split_slot = false;
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (vma->valid && vma->start < start && vma->end > end) {
            need_split_slot = true;
            break;
        }
    }
    if (need_split_slot) {
        int free_slot = -1;
        for (int j = 0; j < VMA_MAX; j++) {
            if (!proc->vmas[j].valid) {
                free_slot = j;
                break;
            }
        }
        if (free_slot == -1) {
            return -(int64_t)ENOMEM;
        }
    }

    /* Unmap physical pages first (only if they are mapped) */
    uint64_t pages = aligned_len / VMM_PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t vaddr = addr + i * VMM_PAGE_SIZE;
        phys_addr_t phys;
        if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
            vmm_unmap(proc->address_space, vaddr);
            pmm_free_page(phys);
        }
    }

    /* Update VMAs */
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (!vma->valid) continue;

        if (vma->start >= start && vma->end <= end) {
            if (!vma->is_anonymous && vma->file != 0) {
                file_close(vma->file);
            }
            vma->valid = false;
        }
        else if (vma->start < start && vma->end > end) {
            uint64_t old_end = vma->end;
            vma->end = start;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    struct vma new_vma = *vma;
                    new_vma.start = end;
                    new_vma.end = old_end;
                    if (!vma->is_anonymous) {
                        new_vma.file_offset = vma->file_offset + (end - vma->start);
                    }
                    proc->vmas[j] = new_vma;
                    break;
                }
            }
        }
        else if (vma->start < start && vma->end > start && vma->end <= end) {
            vma->end = start;
        }
        else if (vma->start >= start && vma->start < end && vma->end > end) {
            uint64_t old_start = vma->start;
            vma->start = end;
            if (!vma->is_anonymous) {
                vma->file_offset += (end - old_start);
            }
        }
    }

    vma_merge_adjacent(proc);
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
    uint64_t aligned_len = page_align_up(length);
    if (addr + aligned_len < addr) return -(int64_t)EINVAL;

    uint64_t start = addr;
    uint64_t end = addr + aligned_len;

    /* Pre-check slots needed and availability */
    int slots_needed = 0;
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (!vma->valid) continue;

        if (vma->start < start && vma->end > end) {
            slots_needed += 2;
        } else if (vma->start < start && vma->end > start && vma->end <= end) {
            slots_needed += 1;
        } else if (vma->start >= start && vma->start < end && vma->end > end) {
            slots_needed += 1;
        }
    }

    int slots_available = 0;
    for (int j = 0; j < VMA_MAX; j++) {
        if (!proc->vmas[j].valid) {
            slots_available++;
        }
    }
    if (slots_available < slots_needed) {
        return -(int64_t)ENOMEM;
    }

    uint64_t vflags = VMM_PRESENT | VMM_USER;
    if (prot & PROT_WRITE) vflags |= VMM_WRITABLE;
    if (!(prot & PROT_EXEC)) vflags |= VMM_NO_EXECUTE;

    /* Walk page table and update protection flags for mapped pages */
    uint64_t pages = aligned_len / VMM_PAGE_SIZE;
    for (uint64_t i = 0; i < pages; i++) {
        uint64_t vaddr = addr + i * VMM_PAGE_SIZE;
        phys_addr_t phys;
        if (vmm_virt_to_phys(proc->address_space, vaddr, &phys)) {
            vmm_protect(proc->address_space, vaddr, vflags);
        }
    }

    /* Update VMAs */
    for (int i = 0; i < VMA_MAX; i++) {
        struct vma *vma = &proc->vmas[i];
        if (!vma->valid) continue;

        if (vma->start >= start && vma->end <= end) {
            vma->prot_flags = prot;
            vma->flags = vflags;
        }
        else if (vma->start < start && vma->end > end) {
            uint64_t old_end = vma->end;
            int slot1 = -1, slot2 = -1;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    if (slot1 == -1) slot1 = j;
                    else { slot2 = j; break; }
                }
            }
            if (slot1 != -1 && slot2 != -1) {
                vma->end = start;

                struct vma mid_vma = *vma;
                mid_vma.start = start;
                mid_vma.end = end;
                mid_vma.prot_flags = prot;
                mid_vma.flags = vflags;
                if (!vma->is_anonymous) {
                    mid_vma.file_offset = vma->file_offset + (start - vma->start);
                }
                proc->vmas[slot1] = mid_vma;

                struct vma right_vma = *vma;
                right_vma.start = end;
                right_vma.end = old_end;
                if (!vma->is_anonymous) {
                    right_vma.file_offset = vma->file_offset + (end - vma->start);
                }
                proc->vmas[slot2] = right_vma;
            }
        }
        else if (vma->start < start && vma->end > start && vma->end <= end) {
            int slot = -1;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    slot = j;
                    break;
                }
            }
            if (slot != -1) {
                uint64_t old_end = vma->end;
                vma->end = start;

                struct vma new_vma = *vma;
                new_vma.start = start;
                new_vma.end = old_end;
                new_vma.prot_flags = prot;
                new_vma.flags = vflags;
                if (!vma->is_anonymous) {
                    new_vma.file_offset = vma->file_offset + (start - vma->start);
                }
                proc->vmas[slot] = new_vma;
            }
        }
        else if (vma->start >= start && vma->start < end && vma->end > end) {
            int slot = -1;
            for (int j = 0; j < VMA_MAX; j++) {
                if (!proc->vmas[j].valid) {
                    slot = j;
                    break;
                }
            }
            if (slot != -1) {
                uint64_t old_start = vma->start;
                vma->start = end;
                if (!vma->is_anonymous) {
                    vma->file_offset += (end - old_start);
                }

                struct vma new_vma = *vma;
                new_vma.start = old_start;
                new_vma.end = end;
                new_vma.prot_flags = prot;
                new_vma.flags = vflags;
                if (!vma->is_anonymous) {
                    new_vma.file_offset = vma->file_offset - (end - old_start);
                }
                proc->vmas[slot] = new_vma;
            }
        }
    }

    vma_merge_adjacent(proc);
    return 0;
}

#define MREMAP_MAYMOVE 1
#define MREMAP_FIXED   2

int64_t sys_mremap(struct syscall_frame *frame)
{
    uint64_t old_addr = (uint64_t)frame->rdi;
    uint64_t old_size = (uint64_t)frame->rsi;
    uint64_t new_size = (uint64_t)frame->rdx;
    unsigned long flags = (unsigned long)frame->r10;
    uint64_t new_addr_hint = (uint64_t)frame->r8;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;
    if ((flags & ~(MREMAP_MAYMOVE | MREMAP_FIXED)) != 0)
        return -(int64_t)EINVAL;
    if ((flags & MREMAP_FIXED) && !(flags & MREMAP_MAYMOVE))
        return -(int64_t)EINVAL;
    if (old_size == 0 || new_size == 0 || (old_addr & 0xfff))
        return -(int64_t)EINVAL;

    struct process *proc = current_task->process;
    uint64_t old_len = page_align_up(old_size);
    uint64_t new_len = page_align_up(new_size);
    uint64_t old_end = old_addr + old_len;
    if (old_end < old_addr)
        return -(int64_t)EINVAL;

    int vma_index = vma_find_exact_slot(proc, old_addr, old_end);
    if (vma_index < 0)
        return -(int64_t)EFAULT;

    struct vma *vma = &proc->vmas[vma_index];
    if (vma->start != old_addr)
        return -(int64_t)EFAULT;

    if (new_len == old_len)
        return (int64_t)old_addr;

    if (new_len < old_len) {
        struct syscall_frame unmap_frame;
        unmap_frame.rdi = old_addr + new_len;
        unmap_frame.rsi = old_len - new_len;
        int64_t ret = sys_munmap(&unmap_frame);
        if (ret < 0)
            return ret;
        return (int64_t)old_addr;
    }

    uint64_t grow_end = old_addr + new_len;
    if (grow_end < old_addr)
        return -(int64_t)EINVAL;

    if (!vma_overlaps(proc, old_end, grow_end)) {
        vma->end = grow_end;
        return (int64_t)old_addr;
    }

    if (!(flags & MREMAP_MAYMOVE))
        return -(int64_t)ENOMEM;
    if (flags & MREMAP_FIXED) {
        if (new_addr_hint & 0xfff)
            return -(int64_t)EINVAL;
    }

    uint64_t dst_addr = (flags & MREMAP_FIXED)
        ? new_addr_hint
        : vma_find_free_region(proc, new_addr_hint, new_len);
    if (dst_addr == 0)
        return -(int64_t)ENOMEM;
    if ((flags & MREMAP_FIXED) && vma_overlaps(proc, dst_addr, dst_addr + new_len)) {
        struct syscall_frame unmap_frame;
        unmap_frame.rdi = dst_addr;
        unmap_frame.rsi = new_len;
        (void)sys_munmap(&unmap_frame);
    }

    for (uint64_t off = 0; off < old_len; off += VMM_PAGE_SIZE) {
        phys_addr_t phys;
        uint64_t src = old_addr + off;
        uint64_t dst = dst_addr + off;
        if (!vmm_virt_to_phys(proc->address_space, src, &phys))
            continue;
        vmm_unmap(proc->address_space, src);
        if (vmm_map(proc->address_space, dst, phys, vma->flags) != 0) {
            return -(int64_t)ENOMEM;
        }
    }

    vma->start = dst_addr;
    vma->end = dst_addr + new_len;
    return (int64_t)dst_addr;
}

#define MADV_NORMAL     0
#define MADV_RANDOM     1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED   3
#define MADV_DONTNEED   4

int64_t sys_madvise(struct syscall_frame *frame)
{
    uint64_t addr = (uint64_t)frame->rdi;
    uint64_t length = (uint64_t)frame->rsi;
    int advice = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0)
        return -(int64_t)EPERM;
    if (length == 0)
        return 0;
    if (addr & 0xfff)
        return -(int64_t)EINVAL;

    struct process *proc = current_task->process;
    uint64_t end = addr + page_align_up(length);
    if (end < addr)
        return -(int64_t)EINVAL;

    switch (advice) {
    case MADV_NORMAL:
    case MADV_RANDOM:
    case MADV_SEQUENTIAL:
    case MADV_WILLNEED:
        return 0;
    case MADV_DONTNEED:
        for (int i = 0; i < VMA_MAX; i++) {
            struct vma *vma = &proc->vmas[i];
            if (!vma->valid)
                continue;
            if (addr < vma->end && end > vma->start) {
                uint64_t drop_start = addr > vma->start ? addr : vma->start;
                uint64_t drop_end = end < vma->end ? end : vma->end;
                vma_drop_present_pages(proc, drop_start, drop_end);
            }
        }
        return 0;
    default:
        return -(int64_t)EINVAL;
    }
}
