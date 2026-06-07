#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_BYTE 8ULL
#define PMM_MIN_MANAGED_PHYS 0x100000ULL

extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

static uint8_t *page_bitmap;
static uint8_t *managed_bitmap;
static uint64_t bitmap_bytes;
static uint64_t bitmap_storage_bytes;
static uint64_t total_pages;
static uint64_t usable_pages;
static uint64_t free_pages;
static uint64_t hhdm_base;
static phys_addr_t bitmap_phys;
static bool initialized;

static phys_addr_t self_test_pages[PMM_SELF_TEST_PAGE_COUNT];

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static uint64_t align_down(uint64_t value, uint64_t alignment)
{
    return value & ~(alignment - 1);
}

static uint64_t page_index(phys_addr_t page)
{
    return page / PMM_PAGE_SIZE;
}

static phys_addr_t page_addr(uint64_t index)
{
    return index * PMM_PAGE_SIZE;
}

static void bitmap_set(uint8_t *bitmap, uint64_t index)
{
    bitmap[index / BITS_PER_BYTE] |= (uint8_t)(1U << (index % BITS_PER_BYTE));
}

static void bitmap_clear(uint8_t *bitmap, uint64_t index)
{
    bitmap[index / BITS_PER_BYTE] &= (uint8_t)~(1U << (index % BITS_PER_BYTE));
}

static bool bitmap_test(const uint8_t *bitmap, uint64_t index)
{
    return (bitmap[index / BITS_PER_BYTE] & (uint8_t)(1U << (index % BITS_PER_BYTE))) != 0;
}

static bool range_is_valid(phys_addr_t base, uint64_t count)
{
    if ((base % PMM_PAGE_SIZE) != 0 || count == 0) {
        return false;
    }

    uint64_t start = page_index(base);
    return start < total_pages && count <= (total_pages - start);
}

static void mark_page_free(uint64_t index)
{
    if (index >= total_pages || !bitmap_test(page_bitmap, index)) {
        return;
    }

    bitmap_set(managed_bitmap, index);
    bitmap_clear(page_bitmap, index);
    free_pages++;
}

static void mark_page_reserved(uint64_t index)
{
    if (index >= total_pages) {
        return;
    }

    if (!bitmap_test(page_bitmap, index)) {
        free_pages--;
    }
    bitmap_clear(managed_bitmap, index);
    bitmap_set(page_bitmap, index);
}

static void reserve_range(phys_addr_t base, uint64_t length)
{
    uint64_t start = align_down(base, PMM_PAGE_SIZE);
    uint64_t end = align_up(base + length, PMM_PAGE_SIZE);

    for (uint64_t addr = start; addr < end; addr += PMM_PAGE_SIZE) {
        mark_page_reserved(page_index(addr));
    }
}

static phys_addr_t kernel_phys_start(void)
{
    return (phys_addr_t)(uintptr_t)__kernel_phys_start;
}

static phys_addr_t kernel_phys_end(void)
{
    return (phys_addr_t)(uintptr_t)__kernel_phys_end;
}

static uint64_t find_max_usable_physical_address(volatile struct limine_memmap_response *memmap)
{
    uint64_t max_addr = 0;

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t end = entry->base + entry->length;
        if (end > max_addr) {
            max_addr = end;
        }
    }

    return max_addr;
}

static phys_addr_t find_bitmap_storage(volatile struct limine_memmap_response *memmap)
{
    uint64_t needed = align_up(bitmap_storage_bytes, PMM_PAGE_SIZE);

    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start = align_up(entry->base, PMM_PAGE_SIZE);
        uint64_t end = align_down(entry->base + entry->length, PMM_PAGE_SIZE);

        if (start < PMM_MIN_MANAGED_PHYS) {
            start = PMM_MIN_MANAGED_PHYS;
        }
        if (start < kernel_phys_end() && end > kernel_phys_start()) {
            start = align_up(kernel_phys_end(), PMM_PAGE_SIZE);
        }

        if (end > start && (end - start) >= needed) {
            return start;
        }
    }

    panic("PMM: no usable range can hold bitmap");
}

static void release_usable_ranges(volatile struct limine_memmap_response *memmap)
{
    for (uint64_t i = 0; i < memmap->entry_count; i++) {
        const struct limine_memmap_entry *entry = memmap->entries[i];
        if (entry->type != LIMINE_MEMMAP_USABLE) {
            continue;
        }

        uint64_t start = align_up(entry->base, PMM_PAGE_SIZE);
        uint64_t end = align_down(entry->base + entry->length, PMM_PAGE_SIZE);
        if (start < PMM_MIN_MANAGED_PHYS) {
            start = PMM_MIN_MANAGED_PHYS;
        }

        for (uint64_t addr = start; addr < end; addr += PMM_PAGE_SIZE) {
            mark_page_free(page_index(addr));
            usable_pages++;
        }
    }
}

void pmm_init(volatile struct limine_memmap_response *memmap, uint64_t hhdm_offset)
{
    if (memmap == 0) {
        panic("PMM: missing memory map");
    }
    if (hhdm_offset == 0) {
        panic("PMM: missing HHDM offset");
    }

    hhdm_base = hhdm_offset;
    total_pages = align_up(find_max_usable_physical_address(memmap), PMM_PAGE_SIZE) / PMM_PAGE_SIZE;
    bitmap_bytes = align_up(total_pages, BITS_PER_BYTE) / BITS_PER_BYTE;
    bitmap_storage_bytes = bitmap_bytes * 2;
    bitmap_phys = find_bitmap_storage(memmap);
    page_bitmap = (uint8_t *)(uintptr_t)(hhdm_base + bitmap_phys);
    managed_bitmap = page_bitmap + bitmap_bytes;

    memset(page_bitmap, 0xFF, bitmap_storage_bytes);
    free_pages = 0;
    usable_pages = 0;

    release_usable_ranges(memmap);

    reserve_range(0, PMM_PAGE_SIZE);
    reserve_range(bitmap_phys, align_up(bitmap_storage_bytes, PMM_PAGE_SIZE));
    reserve_range(kernel_phys_start(), kernel_phys_end() - kernel_phys_start());

    initialized = true;
}

phys_addr_t pmm_alloc_page(void)
{
    if (!initialized) {
        panic("PMM: allocation before initialization");
    }

    for (uint64_t i = 0; i < total_pages; i++) {
        if (bitmap_test(managed_bitmap, i) && !bitmap_test(page_bitmap, i)) {
            bitmap_set(page_bitmap, i);
            free_pages--;
            return page_addr(i);
        }
    }

    return 0;
}

void pmm_free_page(phys_addr_t page)
{
    if (!initialized) {
        panic("PMM: free before initialization");
    }
    if (!range_is_valid(page, 1)) {
        panic("PMM: invalid free 0x%llx", page);
    }

    uint64_t index = page_index(page);
    if (!bitmap_test(managed_bitmap, index)) {
        panic("PMM: free of unmanaged page 0x%llx", page);
    }
    if (!bitmap_test(page_bitmap, index)) {
        panic("PMM: double free 0x%llx", page);
    }

    bitmap_clear(page_bitmap, index);
    free_pages++;
}

phys_addr_t pmm_alloc_pages_contiguous(uint64_t count)
{
    if (!initialized) {
        panic("PMM: allocation before initialization");
    }
    if (count == 0 || count > total_pages) {
        return 0;
    }

    uint64_t run_start = 0;
    uint64_t run_length = 0;

    for (uint64_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(managed_bitmap, i) || bitmap_test(page_bitmap, i)) {
            run_length = 0;
            continue;
        }

        if (run_length == 0) {
            run_start = i;
        }
        run_length++;

        if (run_length == count) {
            for (uint64_t j = 0; j < count; j++) {
                bitmap_set(page_bitmap, run_start + j);
            }
            free_pages -= count;
            return page_addr(run_start);
        }
    }

    return 0;
}

void pmm_free_pages_contiguous(phys_addr_t base, uint64_t count)
{
    if (!range_is_valid(base, count)) {
        panic("PMM: invalid contiguous free base=0x%llx count=%llu", base, count);
    }

    uint64_t start = page_index(base);
    for (uint64_t i = 0; i < count; i++) {
        if (!bitmap_test(managed_bitmap, start + i)) {
            panic("PMM: free of unmanaged page in range base=0x%llx index=%llu", base, i);
        }
        if (!bitmap_test(page_bitmap, start + i)) {
            panic("PMM: double free in range base=0x%llx index=%llu", base, i);
        }
    }
    for (uint64_t i = 0; i < count; i++) {
        bitmap_clear(page_bitmap, start + i);
    }
    free_pages += count;
}

struct pmm_stats pmm_get_stats(void)
{
    struct pmm_stats stats = {
        .total_pages = total_pages,
        .usable_pages = usable_pages,
        .reserved_pages = total_pages - free_pages,
        .free_pages = free_pages,
        .allocated_pages = usable_pages - free_pages,
        .bitmap_bytes = bitmap_storage_bytes,
        .bitmap_phys = bitmap_phys,
    };

    return stats;
}

void pmm_print_stats(void)
{
    struct pmm_stats stats = pmm_get_stats();

    printk("PMM: total=%llu KiB usable=%llu KiB free=%llu KiB reserved=%llu KiB\n",
        stats.total_pages * (PMM_PAGE_SIZE / 1024),
        stats.usable_pages * (PMM_PAGE_SIZE / 1024),
        stats.free_pages * (PMM_PAGE_SIZE / 1024),
        stats.reserved_pages * (PMM_PAGE_SIZE / 1024));
    printk("PMM: bitmap=%llu bytes at phys=0x%llx\n",
        stats.bitmap_bytes, stats.bitmap_phys);
}

void pmm_self_test(void)
{
    uint64_t before = free_pages;

    for (uint64_t i = 0; i < PMM_SELF_TEST_PAGE_COUNT; i++) {
        self_test_pages[i] = pmm_alloc_page();
        if (self_test_pages[i] == 0) {
            panic("PMM: self-test allocation failed at %llu", i);
        }
    }

    phys_addr_t contiguous = pmm_alloc_pages_contiguous(16);
    if (contiguous == 0) {
        panic("PMM: contiguous self-test allocation failed");
    }
    pmm_free_pages_contiguous(contiguous, 16);

    for (uint64_t i = 0; i < PMM_SELF_TEST_PAGE_COUNT; i++) {
        pmm_free_page(self_test_pages[i]);
    }

    if (free_pages != before) {
        panic("PMM: self-test leaked pages before=%llu after=%llu", before, free_pages);
    }

    printk("PMM: self-test passed (%u pages)\n", PMM_SELF_TEST_PAGE_COUNT);
}
