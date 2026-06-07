#include <arch/x86_64/vmm.h>
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
    return 0;
}

void vmm_destroy_address_space(struct address_space *space)
{
    (void)space;
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
    printk("VMM: self-test passed\n");
}
