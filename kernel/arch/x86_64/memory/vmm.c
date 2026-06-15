#include <arch/x86_64/vmm.h>
#include <sched/task.h>
#include <arch/x86_64/cpu.h>
#include <arch/x86_64/smp.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <stddef.h>
#include <stdint.h>

#define PAGE_ENTRIES 512ULL
#define PAGE_MASK 0x000ffffffffff000ULL
#define PAGE_OFFSET_MASK 0xfffULL
#define PAGE_FLAGS_MASK 0xfff0000000000fffULL

static uint64_t hhdm_base;
static struct address_space kernel_space;

volatile uint64_t expect_page_fault_rip = 0;
volatile uint64_t resume_page_fault_rip = 0;
volatile uint64_t observed_page_fault_cr2 = 0;
volatile int page_fault_observed_count = 0;

static void vmm_run_fault_test(uintptr_t trigger_addr, bool expect_fault);

static inline uint64_t read_cr3(void)
{
    uint64_t value;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(value));
    return value;
}

static inline void invlpg(virt_addr_t virt)
{
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
}

static uint64_t pml4_index(virt_addr_t virt)
{
    return (virt >> 39) & 0x1ff;
}

static uint64_t pdpt_index(virt_addr_t virt)
{
    return (virt >> 30) & 0x1ff;
}

static uint64_t pd_index(virt_addr_t virt)
{
    return (virt >> 21) & 0x1ff;
}

static uint64_t pt_index(virt_addr_t virt)
{
    return (virt >> 12) & 0x1ff;
}

static uint64_t *phys_to_virt(phys_addr_t phys)
{
    return (uint64_t *)(uintptr_t)(hhdm_base + phys);
}

static bool page_aligned(uint64_t value)
{
    return (value & PAGE_OFFSET_MASK) == 0;
}

static phys_addr_t entry_phys(uint64_t entry)
{
    return entry & PAGE_MASK;
}

static uint64_t make_entry(phys_addr_t phys, uint64_t flags)
{
    return (phys & PAGE_MASK) | (flags & PAGE_FLAGS_MASK);
}

static uint64_t table_flags_from_leaf(uint64_t flags)
{
    uint64_t table_flags = VMM_PRESENT | VMM_WRITABLE;
    if ((flags & VMM_USER) != 0) {
        table_flags |= VMM_USER;
    }
    return table_flags;
}

static uint64_t *ensure_next_table(uint64_t *table, uint64_t index, uint64_t leaf_flags)
{
    if ((table[index] & VMM_PRESENT) != 0) {
        return phys_to_virt(entry_phys(table[index]));
    }

    phys_addr_t new_table = pmm_alloc_page();
    if (new_table == 0) {
        return 0;
    }

    memset(phys_to_virt(new_table), 0, VMM_PAGE_SIZE);
    table[index] = make_entry(new_table, table_flags_from_leaf(leaf_flags));
    return phys_to_virt(new_table);
}

static uint64_t *walk_to_pte(struct address_space *space, virt_addr_t virt,
    uint64_t flags, bool create)
{
    uint64_t *pml4 = phys_to_virt(space->pml4_phys);
    uint64_t *pdpt = create
        ? ensure_next_table(pml4, pml4_index(virt), flags)
        : (((pml4[pml4_index(virt)] & VMM_PRESENT) != 0)
            ? phys_to_virt(entry_phys(pml4[pml4_index(virt)])) : 0);
    if (pdpt == 0) {
        return 0;
    }

    uint64_t *pd = create
        ? ensure_next_table(pdpt, pdpt_index(virt), flags)
        : (((pdpt[pdpt_index(virt)] & VMM_PRESENT) != 0)
            ? phys_to_virt(entry_phys(pdpt[pdpt_index(virt)])) : 0);
    if (pd == 0) {
        return 0;
    }

    uint64_t *pt = create
        ? ensure_next_table(pd, pd_index(virt), flags)
        : (((pd[pd_index(virt)] & VMM_PRESENT) != 0)
            ? phys_to_virt(entry_phys(pd[pd_index(virt)])) : 0);
    if (pt == 0) {
        return 0;
    }

    return &pt[pt_index(virt)];
}

void vmm_init(uint64_t hhdm_offset)
{
    if (hhdm_offset == 0) {
        panic("VMM: missing HHDM offset");
    }

    hhdm_base = hhdm_offset;
    kernel_space.pml4_phys = read_cr3() & PAGE_MASK;
}

struct address_space *vmm_kernel_address_space(void)
{
    return &kernel_space;
}

int vmm_map(struct address_space *space, virt_addr_t virt, phys_addr_t phys, uint64_t flags)
{
    if (space == 0 || !page_aligned(virt) || !page_aligned(phys)) {
        return -1;
    }
    if ((flags & VMM_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pte = walk_to_pte(space, virt, flags, true);
    if (pte == 0) {
        return -1;
    }
    if ((*pte & VMM_PRESENT) != 0) {
        return -1;
    }

    *pte = make_entry(phys, flags);
    invlpg(virt);
    return 0;
}

int vmm_unmap(struct address_space *space, virt_addr_t virt)
{
    if (space == 0 || !page_aligned(virt)) {
        return -1;
    }

    uint64_t *pte = walk_to_pte(space, virt, 0, false);
    if (pte == 0 || (*pte & VMM_PRESENT) == 0) {
        return -1;
    }

    *pte = 0;
    invlpg(virt);

    /* Phase 5: TLB shootdown across the rest of the CPUs. Publish
     * the vaddr to every other CPU's g_tlb_queue[] and then
     * broadcast IPI_VECTOR_TLB_SHOOTDOWN. Each receiver drains its
     * own queue and does invlpg for each pending address. The
     * publish-before-IPI ordering is guaranteed on x86: the LAPIC
     * IPI write serializes, so the receiver sees the queue entry
     * writes that happened before the IPI.
     *
     * Gated on g_cpu_count > 1 to keep the single-CPU path free of
     * LAPIC MMIO and ring writes. We only shoot down if the unmapped
     * page lived in the kernel PML4 (i.e., this is the kernel's
     * own address space, not a per-process space). Per-process
     * unmaps don't need cross-CPU shootdown because process
     * spaces are not shared. */
    if (g_cpu_count > 1 && space == vmm_kernel_address_space()) {
        tlb_shootdown_publish_all_others(virt);
        smp_broadcast_ipi_excluding_self(IPI_VECTOR_TLB_SHOOTDOWN);
    }
    return 0;
}

int vmm_protect(struct address_space *space, virt_addr_t virt, uint64_t flags)
{
    if (space == 0 || !page_aligned(virt) || (flags & VMM_PRESENT) == 0) {
        return -1;
    }

    uint64_t *pte = walk_to_pte(space, virt, 0, false);
    if (pte == 0 || (*pte & VMM_PRESENT) == 0) {
        return -1;
    }

    phys_addr_t phys = entry_phys(*pte);
    *pte = make_entry(phys, flags);
    invlpg(virt);
    return 0;
}

bool vmm_virt_to_phys(struct address_space *space, virt_addr_t virt, phys_addr_t *phys_out)
{
    if (space == 0 || phys_out == 0) {
        return false;
    }

    uint64_t *pte = walk_to_pte(space, virt & ~PAGE_OFFSET_MASK, 0, false);
    if (pte == 0 || (*pte & VMM_PRESENT) == 0) {
        return false;
    }

    *phys_out = entry_phys(*pte) + (virt & PAGE_OFFSET_MASK);
    return true;
}

struct address_space *vmm_create_address_space(void)
{
    phys_addr_t pml4_phys = pmm_alloc_page();
    if (pml4_phys == 0) return 0;

    uint64_t *new_pml4 = phys_to_virt(pml4_phys);
    uint64_t *kernel_pml4 = phys_to_virt(kernel_space.pml4_phys);
    memset(new_pml4, 0, VMM_PAGE_SIZE);

    for (uint64_t i = 256; i < PAGE_ENTRIES; i++) {
        new_pml4[i] = kernel_pml4[i];
    }

    phys_addr_t struct_phys = pmm_alloc_page();
    if (struct_phys == 0) {
        pmm_free_page(pml4_phys);
        return 0;
    }

    struct address_space *space = (struct address_space *)(uintptr_t)(hhdm_base + struct_phys);
    space->pml4_phys = pml4_phys;
    space->self_phys = struct_phys;
    space->ref_count = 1;
    return space;
}

struct address_space *vmm_clone_address_space(struct address_space *parent)
{
    if (parent == 0) return 0;
    struct address_space *child = vmm_create_address_space();
    if (child == 0) return 0;

    uint64_t *parent_pml4 = phys_to_virt(parent->pml4_phys);

    for (uint64_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        if ((parent_pml4[pml4_i] & VMM_PRESENT) == 0) continue;

        uint64_t *pdpt = phys_to_virt(entry_phys(parent_pml4[pml4_i]));

        for (uint64_t pdpt_i = 0; pdpt_i < PAGE_ENTRIES; pdpt_i++) {
            if ((pdpt[pdpt_i] & VMM_PRESENT) == 0) continue;

            uint64_t *pd = phys_to_virt(entry_phys(pdpt[pdpt_i]));

            for (uint64_t pd_i = 0; pd_i < PAGE_ENTRIES; pd_i++) {
                if ((pd[pd_i] & VMM_PRESENT) == 0) continue;

                uint64_t *pt = phys_to_virt(entry_phys(pd[pd_i]));

                for (uint64_t pt_i = 0; pt_i < PAGE_ENTRIES; pt_i++) {
                    if ((pt[pt_i] & VMM_PRESENT) == 0) continue;

                    virt_addr_t virt = (pml4_i << 39) | (pdpt_i << 30) | (pd_i << 21) | (pt_i << 12);

                    phys_addr_t parent_phys = entry_phys(pt[pt_i]);
                    uint64_t flags = pt[pt_i] & PAGE_FLAGS_MASK;

                    if (flags & VMM_WRITABLE) {
                        flags &= ~VMM_WRITABLE;
                        flags |= VMM_COW;
                        pt[pt_i] = (pt[pt_i] & ~PAGE_FLAGS_MASK) | flags;
                        invlpg(virt);
                    }

                    if (vmm_map(child, virt, parent_phys, flags) != 0) {
                        vmm_destroy_address_space(child);
                        return 0;
                    }

                    pmm_ref_page(parent_phys);
                }
            }
        }
    }

    return child;
}

int vmm_handle_cow(struct address_space *space, virt_addr_t virt)
{
    if (space == 0) return -1;
    virt_addr_t fault_page = virt & ~PAGE_OFFSET_MASK;

    uint64_t *pte = walk_to_pte(space, fault_page, 0, false);
    if (pte == 0 || (*pte & VMM_PRESENT) == 0) {
        return -1;
    }

    if (!(*pte & VMM_COW)) {
        return -1;
    }

    phys_addr_t old_phys = entry_phys(*pte);
    uint16_t ref = pmm_get_page_ref(old_phys);

    if (ref == 1) {
        uint64_t flags = *pte & PAGE_FLAGS_MASK;
        flags &= ~VMM_COW;
        flags |= VMM_WRITABLE;
        *pte = (*pte & ~PAGE_FLAGS_MASK) | flags;
        invlpg(fault_page);
        return 0;
    } else if (ref > 1) {
        phys_addr_t new_phys = pmm_alloc_page();
        if (new_phys == 0) {
            return -2;
        }

        memcpy(phys_to_virt(new_phys), phys_to_virt(old_phys), VMM_PAGE_SIZE);

        uint64_t flags = *pte & PAGE_FLAGS_MASK;
        flags &= ~VMM_COW;
        flags |= VMM_WRITABLE;

        *pte = new_phys | flags;
        invlpg(fault_page);

        pmm_free_page(old_phys);
        return 0;
    }

    return -1;
}

void vmm_destroy_address_space(struct address_space *space)
{
    if (space == 0 || space == &kernel_space) return;

    uint64_t *pml4 = phys_to_virt(space->pml4_phys);

    for (uint64_t pml4_i = 0; pml4_i < 256; pml4_i++) {
        if ((pml4[pml4_i] & VMM_PRESENT) == 0) continue;

        uint64_t *pdpt = phys_to_virt(entry_phys(pml4[pml4_i]));

        for (uint64_t pdpt_i = 0; pdpt_i < PAGE_ENTRIES; pdpt_i++) {
            if ((pdpt[pdpt_i] & VMM_PRESENT) == 0) continue;

            uint64_t *pd = phys_to_virt(entry_phys(pdpt[pdpt_i]));

            for (uint64_t pd_i = 0; pd_i < PAGE_ENTRIES; pd_i++) {
                if ((pd[pd_i] & VMM_PRESENT) == 0) continue;

                uint64_t *pt = phys_to_virt(entry_phys(pd[pd_i]));

                for (uint64_t pt_i = 0; pt_i < PAGE_ENTRIES; pt_i++) {
                    if ((pt[pt_i] & VMM_PRESENT) == 0) continue;
                    pmm_free_page(entry_phys(pt[pt_i]));
                }

                pmm_free_page(entry_phys(pd[pd_i]));
            }

            pmm_free_page(entry_phys(pdpt[pdpt_i]));
        }

        pmm_free_page(entry_phys(pml4[pml4_i]));
    }

    pmm_free_page(space->pml4_phys);
    pmm_free_page(space->self_phys);
}

void vmm_switch_address_space(struct address_space *space)
{
    if (space == 0) return;
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->pml4_phys) : "memory");
}

bool vmm_is_user_address(virt_addr_t virt)
{
    return virt < VMM_KERNEL_BASE;
}

bool vmm_is_kernel_address(virt_addr_t virt)
{
    return virt >= VMM_KERNEL_BASE;
}

#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

bool vmm_validate_user_ptr_ex(struct address_space *space, const void *ptr, size_t length, bool require_write)
{
    if (space == 0) return false;
    if (length == 0) return true;
    if (ptr == 0) return false;

    virt_addr_t start = (virt_addr_t)(uintptr_t)ptr;
    virt_addr_t last = start + length - 1;

    if (last < start) return false;
    if (start < VMM_USER_BASE || last > VMM_USER_TOP) return false;
    if (!vmm_is_user_address(start) || !vmm_is_user_address(last)) return false;

    /* We need sched/task.h definitions to lookup VMAs of the current process */
    extern struct task *current_task;

    virt_addr_t page = start & ~PAGE_OFFSET_MASK;
    for (;;) {
        uint64_t *pte = walk_to_pte(space, page, 0, false);
        bool present = (pte != 0 && (*pte & VMM_PRESENT) != 0);

        if (!present) {
            /* If the page is not present, check if a VMA of the current task covers it */
            if (current_task != 0 && current_task->process != 0 && current_task->process->address_space == space) {
                struct process *proc = current_task->process;
                struct vma *vma = 0;
                for (int i = 0; i < VMA_MAX; i++) {
                    if (proc->vmas[i].valid && page >= proc->vmas[i].start && page < proc->vmas[i].end) {
                        vma = &proc->vmas[i];
                        break;
                    }
                }

                if (vma != 0) {
                    /* Check permissions */
                    if (require_write && !(vma->prot_flags & PROT_WRITE)) {
                        return false;
                    }
                    if (!require_write && !(vma->prot_flags & PROT_READ)) {
                        return false;
                    }

                    /* Allocate and map lazy page */
                    phys_addr_t phys = pmm_alloc_page();
                    if (phys == 0) return false;
                    memset(phys_to_virt(phys), 0, VMM_PAGE_SIZE);

                    if (vmm_map(space, page, phys, vma->flags) != 0) {
                        pmm_free_page(phys);
                        return false;
                    }
                    pte = walk_to_pte(space, page, 0, false);
                    present = (pte != 0 && (*pte & VMM_PRESENT) != 0);
                }
            }
        }

        if (!present) return false;
        if ((*pte & VMM_USER) == 0) return false;

        if (require_write && (*pte & VMM_WRITABLE) == 0) {
            /*
             * If the PTE is COW-marked, resolve COW first (this is what would
             * happen on a userspace write fault). Then re-walk the PTE.
             */
            if ((*pte & VMM_COW) != 0) {
                int cow_rc = vmm_handle_cow(space, page);
                if (cow_rc == 0) {
                    pte = walk_to_pte(space, page, 0, false);
                }
            }

            /* If write is required but not set in PTE, check if VMA allows writing and update PTE flags */
            if (pte == 0 || (*pte & VMM_WRITABLE) == 0) {
                if (current_task != 0 && current_task->process != 0 && current_task->process->address_space == space) {
                    struct process *proc = current_task->process;
                    struct vma *vma = 0;
                    for (int i = 0; i < VMA_MAX; i++) {
                        if (proc->vmas[i].valid && page >= proc->vmas[i].start && page < proc->vmas[i].end) {
                            vma = &proc->vmas[i];
                            break;
                        }
                    }
                    if (vma != 0 && (vma->prot_flags & PROT_WRITE)) {
                        if (vmm_protect(space, page, vma->flags) == 0) {
                            pte = walk_to_pte(space, page, 0, false);
                        }
                    }
                }
            }
            if (pte == 0 || (*pte & VMM_WRITABLE) == 0) return false;
        }

        if (page >= (last & ~PAGE_OFFSET_MASK)) break;
        page += VMM_PAGE_SIZE;
    }

    return true;
}

bool vmm_validate_user_ptr(struct address_space *space, const void *ptr, size_t length)
{
    return vmm_validate_user_ptr_ex(space, ptr, length, false);
}

bool vmm_validate_user_ptr_writable(struct address_space *space, const void *ptr, size_t length)
{
    return vmm_validate_user_ptr_ex(space, ptr, length, true);
}

int vmm_copy_from_user(void *kernel_dst, struct address_space *space, const void *user_src, size_t length)
{
    if (length == 0) return 0;
    if (kernel_dst == 0) return -1;
    if (!vmm_validate_user_ptr(space, user_src, length)) return -1;

    uint8_t *dst = kernel_dst;
    virt_addr_t src = (virt_addr_t)(uintptr_t)user_src;
    size_t remaining = length;

    /* Enable user-space memory access (SMAP override) */
    stac();

    while (remaining > 0) {
        phys_addr_t phys = 0;
        if (!vmm_virt_to_phys(space, src, &phys)) {
            clac();
            return -1;
        }

        size_t page_left = VMM_PAGE_SIZE - (src & PAGE_OFFSET_MASK);
        size_t chunk = remaining < page_left ? remaining : page_left;
        memcpy(dst, (uint8_t *)phys_to_virt(phys), chunk);

        dst += chunk;
        src += chunk;
        remaining -= chunk;
    }

    /* Disable user-space memory access */
    clac();

    return 0;
}

int vmm_copy_to_user(struct address_space *space, void *user_dst, const void *kernel_src, size_t length)
{
    if (length == 0) return 0;
    if (kernel_src == 0) return -1;
    if (!vmm_validate_user_ptr_writable(space, user_dst, length)) return -1;

    virt_addr_t dst = (virt_addr_t)(uintptr_t)user_dst;
    const uint8_t *src = kernel_src;
    size_t remaining = length;

    /* Enable user-space memory access (SMAP override) */
    stac();

    while (remaining > 0) {
        phys_addr_t phys = 0;
        if (!vmm_virt_to_phys(space, dst, &phys)) {
            clac();
            return -1;
        }

        size_t page_left = VMM_PAGE_SIZE - (dst & PAGE_OFFSET_MASK);
        size_t chunk = remaining < page_left ? remaining : page_left;
        memcpy((uint8_t *)phys_to_virt(phys), src, chunk);

        dst += chunk;
        src += chunk;
        remaining -= chunk;
    }

    /* Disable user-space memory access */
    clac();

    return 0;
}

void vmm_negative_self_test(void)
{
    struct address_space *ks = vmm_kernel_address_space();

    if (vmm_map(0, 0xdead0000ULL, 0x1000ULL, VMM_PRESENT | VMM_WRITABLE) == 0) {
        panic("VMM: negative test: map with null space succeeded");
    }
    if (vmm_map(ks, 0xdead1000ULL, 0x1234ULL, VMM_PRESENT | VMM_WRITABLE) == 0) {
        panic("VMM: negative test: map with misaligned phys succeeded");
    }
    if (vmm_map(ks, 0x2345ULL, 0x1000ULL, VMM_PRESENT | VMM_WRITABLE) == 0) {
        panic("VMM: negative test: map with misaligned virt succeeded");
    }
    if (vmm_map(ks, 0xdead2000ULL, 0x1000ULL, VMM_WRITABLE) == 0) {
        panic("VMM: negative test: map without PRESENT succeeded");
    }
    if (vmm_unmap(ks, 0xdead3000ULL) == 0) {
        panic("VMM: negative test: unmap of unmapped succeeded");
    }
    if (vmm_protect(ks, 0xdead4000ULL, VMM_PRESENT) == 0) {
        panic("VMM: negative test: protect of unmapped succeeded");
    }

    struct address_space *user_space = vmm_create_address_space();
    if (user_space == 0) panic("VMM: negative test: address space create failed");

    if (!vmm_validate_user_ptr(user_space, (const void *)0x1000ULL, 0)) {
        panic("VMM: negative test: zero length did not return true");
    }
    if (vmm_validate_user_ptr(0, (const void *)0x1000ULL, 16)) {
        panic("VMM: negative test: null space accepted");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)0xFFFF800000000000ULL, 16)) {
        panic("VMM: negative test: kernel address accepted as user");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)0x1000ULL, (size_t)-1)) {
        panic("VMM: negative test: overflow range accepted");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)0x00007ffffffff000ULL, 0x2000)) {
        panic("VMM: negative test: range crossing user top accepted");
    }

    virt_addr_t page0 = 0x10000ULL;
    virt_addr_t page1 = 0x11000ULL;
    if (vmm_map(user_space, page0, 0x1000ULL, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        panic("VMM: negative test: map page0 failed");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)page0, 0x2001)) {
        panic("VMM: negative test: range crossing unmapped page accepted");
    }
    (void)page1;

    if (vmm_validate_user_ptr(user_space, (const void *)0xFFFF800000000000ULL, 1)) {
        panic("VMM: negative test: range starting in kernel accepted");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)0x1000ULL, 16)) {
        panic("VMM: negative test: range crossing into kernel accepted (gap)");
    }

    vmm_destroy_address_space(user_space);
    printk("VMM: negative self-test passed\n");
}

static void vmm_permission_self_test(void)
{
    struct address_space *user_space = vmm_create_address_space();
    if (user_space == 0) panic("VMM: permission test: address space create failed");

    phys_addr_t phys = pmm_alloc_page();
    if (phys == 0) panic("VMM: permission test: PMM allocation failed");

    virt_addr_t ro_virt = 0x20000ULL;
    if (vmm_map(user_space, ro_virt, phys, VMM_PRESENT | VMM_USER) != 0) {
        panic("VMM: permission test: RO map failed");
    }

    vmm_switch_address_space(user_space);
    volatile uint8_t *ro_ptr = (volatile uint8_t *)(uintptr_t)ro_virt;
    (void)*ro_ptr;

    if (!vmm_validate_user_ptr(user_space, (const void *)(uintptr_t)ro_virt, 16)) {
        panic("VMM: permission test: RO mapping not user-visible");
    }

    if (vmm_validate_user_ptr_writable(user_space, (const void *)(uintptr_t)ro_virt, 16)) {
        panic("VMM: permission test: RO mapping is writable");
    }

    if (vmm_protect(user_space, ro_virt, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        panic("VMM: permission test: protect to RW failed");
    }

    if (!vmm_validate_user_ptr_writable(user_space, (const void *)(uintptr_t)ro_virt, 16)) {
        panic("VMM: permission test: RW mapping not writable");
    }

    vmm_switch_address_space(vmm_kernel_address_space());
    vmm_destroy_address_space(user_space);
    printk("VMM: permission self-test passed\n");
}

void vmm_self_test(void)
{
    struct address_space *space = vmm_kernel_address_space();
    phys_addr_t page = pmm_alloc_page();
    if (page == 0) {
        panic("VMM: self-test PMM allocation failed");
    }

    virt_addr_t virt = VMM_TEST_BASE;
    if (vmm_map(space, virt, page, VMM_PRESENT | VMM_WRITABLE | VMM_NO_EXECUTE) != 0) {
        panic("VMM: self-test map failed");
    }

    volatile uint64_t *test_ptr = (volatile uint64_t *)(uintptr_t)virt;
    *test_ptr = 0x4c6974654e697858ULL;
    if (*test_ptr != 0x4c6974654e697858ULL) {
        panic("VMM: self-test readback failed");
    }

    phys_addr_t translated = 0;
    if (!vmm_virt_to_phys(space, virt, &translated) || translated != page) {
        panic("VMM: self-test translation failed");
    }

    if (vmm_protect(space, virt, VMM_PRESENT | VMM_NO_EXECUTE) != 0) {
        panic("VMM: self-test protect failed");
    }

    if (vmm_unmap(space, virt) != 0) {
        panic("VMM: self-test unmap failed");
    }

    if (vmm_virt_to_phys(space, virt, &translated)) {
        panic("VMM: self-test translation survived unmap");
    }

    pmm_free_page(page);

    struct pmm_stats before = pmm_get_stats();
    struct address_space *user_space = vmm_create_address_space();
    if (user_space == 0) {
        panic("VMM: self-test address-space creation failed");
    }

    phys_addr_t user_page = pmm_alloc_page();
    if (user_page == 0) {
        panic("VMM: self-test user page allocation failed");
    }

    virt_addr_t user_virt = 0x1000;
    if (vmm_map(user_space, user_virt, user_page, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        panic("VMM: self-test user map failed");
    }

    vmm_switch_address_space(user_space);

    volatile uint64_t *user_ptr = (volatile uint64_t *)(uintptr_t)user_virt;
    *user_ptr = 0xBEEF;
    if (*user_ptr != 0xBEEF) {
        panic("VMM: self-test user-space readback failed");
    }

    vmm_switch_address_space(&kernel_space);

    if (!vmm_is_user_address(user_virt) || vmm_is_user_address(VMM_TEST_BASE)) {
        panic("VMM: self-test is_user_address check failed");
    }
    if (!vmm_is_kernel_address(VMM_TEST_BASE) || vmm_is_kernel_address(user_virt)) {
        panic("VMM: self-test is_kernel_address check failed");
    }

    if (!vmm_validate_user_ptr(user_space, (const void *)(uintptr_t)user_virt, 8)) {
        panic("VMM: self-test validate_user_ptr on mapped page failed");
    }
    if (vmm_validate_user_ptr(user_space, (const void *)(uintptr_t)0xFFFF800000000000ULL, 8)) {
        panic("VMM: self-test validate_user_ptr accepted kernel address");
    }

    char test_data[] = "hello";
    if (vmm_copy_to_user(user_space, (void *)(uintptr_t)(user_virt + 8), test_data, 6) != 0) {
        panic("VMM: self-test copy_to_user failed");
    }
    char kernel_buf[16] = {0};
    if (vmm_copy_from_user(kernel_buf, user_space, (const void *)(uintptr_t)(user_virt + 8), 6) != 0) {
        panic("VMM: self-test copy_from_user failed");
    }
    if (memcmp(test_data, kernel_buf, 6) != 0) {
        panic("VMM: self-test copy round-trip mismatch");
    }

    vmm_destroy_address_space(user_space);

    struct pmm_stats after = pmm_get_stats();
    if (after.free_pages != before.free_pages) {
        printk("VMM: leak warning free_pages before=%llu after=%llu\n",
            before.free_pages, after.free_pages);
    }

    vmm_negative_self_test();
    vmm_permission_self_test();

    // NULL fault verification
    vmm_run_fault_test(0, true);
    printk("VMM: NULL fault verification passed\n");

    // Guard page trap verification
    struct address_space *cur_space = vmm_create_address_space();
    if (cur_space == 0) panic("VMM: self-test create space for guard test failed");

    phys_addr_t guard_phys = pmm_alloc_page();
    if (guard_phys == 0) panic("VMM: self-test alloc page for guard test failed");

    virt_addr_t buffer_virt = 0x30000ULL;
    virt_addr_t guard_virt = buffer_virt - VMM_PAGE_SIZE;

    if (vmm_map(cur_space, buffer_virt, guard_phys, VMM_PRESENT | VMM_WRITABLE | VMM_USER) != 0) {
        panic("VMM: self-test map buffer page failed");
    }

    vmm_switch_address_space(cur_space);
    vmm_run_fault_test(buffer_virt, false);
    vmm_run_fault_test(guard_virt, true);

    vmm_switch_address_space(vmm_kernel_address_space());
    vmm_destroy_address_space(cur_space);

    printk("VMM: Guard page fault verification passed\n");

    printk("VMM: self-test passed\n");
    printk("VMM: address-space self-test passed\n");
}

static void vmm_run_fault_test(uintptr_t trigger_addr, bool expect_fault)
{
    expect_page_fault_rip = 0;
    resume_page_fault_rip = 0;
    observed_page_fault_cr2 = 0;
    page_fault_observed_count = 0;

    uint64_t trig_addr;
    uint64_t res_addr;

    __asm__ volatile (
        "leaq 1f(%%rip), %0\n"
        "leaq 2f(%%rip), %1\n"
        "movq %0, expect_page_fault_rip(%%rip)\n"
        "movq %1, resume_page_fault_rip(%%rip)\n"
        "1:\n"
        "movb $0, (%2)\n"
        "2:\n"
        : "=&r"(trig_addr), "=&r"(res_addr)
        : "r"(trigger_addr)
        : "memory"
    );

    expect_page_fault_rip = 0;
    resume_page_fault_rip = 0;

    if (expect_fault) {
        if (page_fault_observed_count != 1) {
            panic("VMM fault test: expected page fault did not occur");
        }
        if (observed_page_fault_cr2 != trigger_addr) {
            panic("VMM fault test: CR2 mismatch (expected 0x%llx, got 0x%llx)",
                (unsigned long long)trigger_addr, (unsigned long long)observed_page_fault_cr2);
        }
    } else {
        if (page_fault_observed_count != 0) {
            panic("VMM fault test: unexpected page fault occurred");
        }
    }
}
