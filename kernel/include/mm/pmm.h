#ifndef LITENIX_MM_PMM_H
#define LITENIX_MM_PMM_H

#include <arch/x86_64/limine.h>
#include <kernel/types.h>
#include <stdint.h>

#define PMM_PAGE_SIZE 4096ULL
#define PMM_SELF_TEST_PAGE_COUNT 2000U

struct pmm_stats {
    uint64_t total_pages;
    uint64_t usable_pages;
    uint64_t reserved_pages;
    uint64_t free_pages;
    uint64_t allocated_pages;
    uint64_t bitmap_bytes;
    phys_addr_t bitmap_phys;
};

void pmm_init(volatile struct limine_memmap_response *memmap, uint64_t hhdm_offset);
phys_addr_t pmm_alloc_page(void);
void pmm_free_page(phys_addr_t page);
phys_addr_t pmm_alloc_pages_contiguous(uint64_t count);
void pmm_free_pages_contiguous(phys_addr_t base, uint64_t count);
struct pmm_stats pmm_get_stats(void);
void pmm_print_stats(void);
void pmm_self_test(void);

#endif
