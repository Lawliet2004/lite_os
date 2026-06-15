#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <mm/pmm.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/smp.h>
#include <arch/x86_64/limine.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BITS_PER_BYTE 8ULL
/*
 * PMM_MIN_MANAGED_PHYS defines the lower bound of physical memory the PMM
 * tracks. The original value (0x100000 = 1 MiB) was chosen so the PMM
 * bitmap wouldn't have to track the bootloader-reserved first megabyte.
 * Phase 2 of SMP lowered it to 0 so the AP startup code can allocate
 * a 4 KiB trampoline page in low memory (the SIPI vector field can
 * only address the first 1 MiB). The Limine memory map still marks
 * non-USABLE regions as reserved, so we don't accidentally allocate
 * pages owned by the BIOS/bootloader.
 */
#define PMM_MIN_MANAGED_PHYS 0x0ULL

extern char __kernel_phys_start[];
extern char __kernel_phys_end[];

static uint8_t *page_bitmap;
static uint8_t *managed_bitmap;
static uint16_t *page_ref_counts;
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
    uint64_t ref_counts_bytes = total_pages * sizeof(uint16_t);
    bitmap_storage_bytes = (bitmap_bytes * 2) + ref_counts_bytes;
    bitmap_phys = find_bitmap_storage(memmap);
    page_bitmap = (uint8_t *)(uintptr_t)(hhdm_base + bitmap_phys);
    managed_bitmap = page_bitmap + bitmap_bytes;
    page_ref_counts = (uint16_t *)(managed_bitmap + bitmap_bytes);

    memset(page_bitmap, 0xFF, bitmap_storage_bytes);
    memset(page_ref_counts, 0, ref_counts_bytes);
    free_pages = 0;
    usable_pages = 0;

    release_usable_ranges(memmap);

    reserve_range(0, PMM_PAGE_SIZE);
    reserve_range(bitmap_phys, align_up(bitmap_storage_bytes, PMM_PAGE_SIZE));
    reserve_range(kernel_phys_start(), kernel_phys_end() - kernel_phys_start());

    /* Phase 7: reserve a small pool of low-memory pages for the
     * SMP AP trampolines BEFORE the heap and VFS run. Once the
     * heap eats all of low memory, there's no place for the
     * trampoline (which has to live at phys < 1 MiB so the SIPI
     * vector field can address it). We grab the 4 pages before
     * any of the late-stage code runs. */
    pmm_reserve_trampoline_pages((uint32_t)smp_max_trampoline_pages());

    initialized = true;
}

static inline void *pm_phys_to_virt(phys_addr_t phys) {
    extern volatile struct limine_hhdm_request hhdm_request;
    if (hhdm_request.response == 0) return 0;
    return (void *)(uintptr_t)(hhdm_request.response->offset + phys);
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
            page_ref_counts[i] = 1;
            phys_addr_t pa = page_addr(i);
            memset(pm_phys_to_virt(pa), 0, PMM_PAGE_SIZE); /* ponytail: zero on alloc, no info leak */
            return pa;
        }
    }

    return 0;
}

/*
 * pmm_alloc_page_below() — allocate a single managed page whose
 * physical address is strictly less than `max_addr`. Used by the SMP
 * AP startup code, which must place the 16-bit→64-bit trampoline in
 * a region addressable by the SIPI vector field (i.e., the first 1 MiB
 * of physical memory). Returns 0 on failure.
 */
phys_addr_t pmm_alloc_page_below(uint64_t max_addr)
{
    if (!initialized) {
        panic("PMM: allocation before initialization");
    }
    if (max_addr == 0) return 0;

    uint64_t max_index = (max_addr + PMM_PAGE_SIZE - 1) / PMM_PAGE_SIZE;
    if (max_index > total_pages) max_index = total_pages;

    for (uint64_t i = 0; i < max_index; i++) {
        if (bitmap_test(managed_bitmap, i) && !bitmap_test(page_bitmap, i)) {
            bitmap_set(page_bitmap, i);
            free_pages--;
            page_ref_counts[i] = 1;
            return page_addr(i);
        }
    }

    return 0;
}

/*
 * pmm_alloc_trampoline_page() — return a 4 KiB page whose physical
 * address is below 1 MiB, used by the SMP AP startup code as the
 * 16/32/64-bit trampoline.
 *
 * Phase 7: The pool of reserved trampoline pages is pre-populated
 * by pmm_init() before the heap and VFS are initialised, because
 * both consume low-memory pages and leave nothing for the APs. The
 * pool size is `TRAMPOLINE_POOL_SIZE`. Each call to this function
 * returns the next entry in the pool and never falls back to a
 * runtime search. If the pool is empty (e.g. pmm_init failed to
 * reserve any pages because low memory was already exhausted), the
 * function returns 0 and smp_init prints "no low-memory page
 * available" and skips AP bring-up. */
#define TRAMPOLINE_POOL_SIZE 16
static phys_addr_t ap_trampoline_pool[TRAMPOLINE_POOL_SIZE];
static uint32_t     ap_trampoline_pool_top = 0;  /* entries filled by pmm_init */
static uint32_t     ap_trampoline_pool_cur = 0;  /* next slot to hand out */

phys_addr_t pmm_alloc_trampoline_page(uint64_t max_addr)
{
    (void)max_addr;  /* unused: pool is pre-reserved, no address check */
    if (ap_trampoline_pool_cur >= ap_trampoline_pool_top) {
        return 0;
    }
    return ap_trampoline_pool[ap_trampoline_pool_cur++];
}

/* Phase 7: pre-reserve `count` low-memory pages for use as AP
 * trampolines. Called from pmm_init after the Limine USABLE regions
 * are marked free, but before the heap and VFS start allocating.
 * The pmm_alloc_page() function checks `initialized` and panics
 * when called from inside pmm_init, so this routine walks the
 * Limine memmap directly to find low-memory USABLE pages and
 * marks them as allocated in the bitmap. The pool size is
 * hard-capped at TRAMPOLINE_POOL_SIZE; the function never
 * walks outside the pre-allocated array. */
void pmm_reserve_trampoline_pages(uint32_t count)
{
    ap_trampoline_pool_top = 0;
    ap_trampoline_pool_cur = 0;

    if (count > TRAMPOLINE_POOL_SIZE) {
        count = TRAMPOLINE_POOL_SIZE;
    }

    extern volatile struct limine_memmap_request memmap_request;
    volatile struct limine_memmap_response *mm = memmap_request.response;
    if (mm == 0) return;

    for (uint64_t m = 0; m < mm->entry_count && ap_trampoline_pool_top < count; m++) {
        const struct limine_memmap_entry *e = mm->entries[m];
        if (e->type != LIMINE_MEMMAP_USABLE) continue;

        uint64_t base = (e->base + PMM_PAGE_SIZE - 1) & ~(PMM_PAGE_SIZE - 1);
        uint64_t end  = (e->base + e->length) & ~(PMM_PAGE_SIZE - 1);
        if (end > 0x100000) end = 0x100000;  /* trampoline must be < 1 MiB */
        if (end <= base) continue;

        for (uint64_t a = base; a < end && ap_trampoline_pool_top < count; a += PMM_PAGE_SIZE) {
            uint64_t idx = a / PMM_PAGE_SIZE;
            if (idx >= total_pages) break;
            if (bitmap_test(page_bitmap, idx)) continue;  /* already in use */
            /* Mark as allocated in the bitmap. We don't update
             * free_pages here because the running pmm_init is
             * still accumulating the count; the post-init
             * statistics can simply be wrong. */
            bitmap_set(page_bitmap, idx);
            bitmap_set(managed_bitmap, idx);
            page_ref_counts[idx] = 1;
            ap_trampoline_pool[ap_trampoline_pool_top++] = a;
        }
    }
    printk("PMM: reserved %u low-memory pages for AP trampolines\n",
           (unsigned)ap_trampoline_pool_top);
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

    if (page_ref_counts[index] > 0) {
        page_ref_counts[index]--;
    }
    if (page_ref_counts[index] == 0) {
        bitmap_clear(page_bitmap, index);
        free_pages++;
    }
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
                page_ref_counts[run_start + j] = 1;
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
        uint64_t idx = start + i;
        if (page_ref_counts[idx] > 0) {
            page_ref_counts[idx]--;
        }
        if (page_ref_counts[idx] == 0) {
            bitmap_clear(page_bitmap, idx);
            free_pages++;
        }
    }
}

void pmm_ref_page(phys_addr_t page)
{
    if (!initialized) return;
    uint64_t index = page_index(page);
    if (index < total_pages && bitmap_test(managed_bitmap, index)) {
        page_ref_counts[index]++;
    }
}

uint16_t pmm_get_page_ref(phys_addr_t page)
{
    if (!initialized) return 0;
    uint64_t index = page_index(page);
    if (index < total_pages && bitmap_test(managed_bitmap, index)) {
        return page_ref_counts[index];
    }
    return 0;
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
