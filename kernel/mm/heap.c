#include <mm/heap.h>
#include <arch/x86_64/vmm.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <lib/string.h>
#include <lib/setjmp.h>
#include <mm/pmm.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP_START 0xffffffffa0000000ULL
#define HEAP_SIZE  0x10000000ULL
#define HEAP_MAX_SLAB_CHUNK 4096U

#define BIN_COUNT 8
static const uint16_t bin_sizes[BIN_COUNT] = { 16, 32, 64, 128, 256, 512, 1024, 2048 };

struct slab_header {
    struct slab_header *next;
    uint16_t bin_index;
    uint16_t total_chunks;
    uint16_t free_chunks;
    void *free_list;
};

struct alloc_header {
    uint16_t bin_index;
    uint16_t magic;
    uint32_t reserved;
    size_t requested_size;
    void *base;
};

#define MAGIC_ALLOC  0xA110
#define MAGIC_DIRECT 0xD1EC
#define MAGIC_FREE   0xDDDD
#define MAGIC_ALIGNED 0xA116
#define POISON_ALLOC 0xAA
#define POISON_FREE  0xDD

static struct slab_header *bins[BIN_COUNT];
static struct heap_stats stats;
static uintptr_t heap_next;
static uintptr_t heap_end;
static bool initialized;

static inline size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static inline bool is_power_of_two(size_t value)
{
    return value != 0 && (value & (value - 1)) == 0;
}

static void *heap_map_page(void)
{
    if (heap_next >= heap_end) {
        printk("Heap: out of virtual address space\n");
        return 0;
    }

    phys_addr_t phys = pmm_alloc_page();
    if (phys == 0) return 0;

    virt_addr_t virt = heap_next;
    heap_next += VMM_PAGE_SIZE;

    if (vmm_map(vmm_kernel_address_space(), virt, phys,
        VMM_PRESENT | VMM_WRITABLE | VMM_NO_EXECUTE) != 0) {
        pmm_free_page(phys);
        heap_next -= VMM_PAGE_SIZE;
        return 0;
    }

    return (void *)(uintptr_t)virt;
}

static int bin_for_size(size_t size)
{
    size_t total = size + sizeof(struct alloc_header);
    for (int i = 0; i < BIN_COUNT; i++) {
        if (total <= bin_sizes[i]) return i;
    }
    return -1;
}

static struct slab_header *slab_create(int bin_index)
{
    void *page = heap_map_page();
    if (page == 0) return 0;

    struct slab_header *slab = (struct slab_header *)page;
    slab->next = 0;
    slab->bin_index = (uint16_t)bin_index;
    slab->free_list = 0;
    slab->free_chunks = 0;

    size_t chunk_size = bin_sizes[bin_index];
    size_t header_size = sizeof(struct slab_header);
    uint8_t *chunk_base = (uint8_t *)page + header_size;
    size_t available = VMM_PAGE_SIZE - header_size;

    uint16_t count = 0;
    while (available >= chunk_size) {
        void *chunk = chunk_base;
        *(void **)chunk = slab->free_list;
        slab->free_list = chunk;
        chunk_base += chunk_size;
        available -= chunk_size;
        count++;
    }

    slab->total_chunks = count;
    slab->free_chunks = count;
    stats.slab_pages++;

    return slab;
}

static void *slab_alloc(int bin_index)
{
    if (bins[bin_index] == 0) {
        struct slab_header *slab = slab_create(bin_index);
        if (slab == 0) return 0;
        slab->next = bins[bin_index];
        bins[bin_index] = slab;
    }

    struct slab_header *slab = bins[bin_index];
    if (slab->free_chunks == 0) {
        struct slab_header *new_slab = slab_create(bin_index);
        if (new_slab == 0) return 0;
        new_slab->next = slab;
        bins[bin_index] = new_slab;
        slab = new_slab;
    }

    void *chunk = slab->free_list;
    slab->free_list = *(void **)chunk;
    slab->free_chunks--;
    return chunk;
}

void heap_init(void)
{
    if (vmm_kernel_address_space() == 0) {
        panic("Heap: VMM not initialized");
    }

    heap_next = HEAP_START;
    heap_end = HEAP_START + HEAP_SIZE;

    for (int i = 0; i < BIN_COUNT; i++) {
        bins[i] = 0;
    }

    memset(&stats, 0, sizeof(stats));
    initialized = true;
    printk("Heap: alloc_header=%u slab_header=%u\n", (uint32_t)sizeof(struct alloc_header), (uint32_t)sizeof(struct slab_header));
}

void *kmalloc(size_t size)
{
    if (size == 0) return 0;

    int bin = bin_for_size(size);
    if (bin >= 0) {
        void *chunk = slab_alloc(bin);
        if (chunk == 0) return 0;

        struct alloc_header *hdr = (struct alloc_header *)chunk;
        hdr->bin_index = (uint16_t)bin;
        hdr->magic = MAGIC_ALLOC;
        hdr->reserved = 0;
        hdr->requested_size = size;
        hdr->base = 0;

        uint8_t *user_ptr = (uint8_t *)chunk + sizeof(struct alloc_header);

#ifdef HEAP_DEBUG
        memset(user_ptr, POISON_ALLOC, bin_sizes[bin] - sizeof(struct alloc_header));
#endif

        stats.allocations++;
        stats.total_allocated_bytes += size;
        return user_ptr;
    }

    size_t pages = align_up(size + sizeof(struct alloc_header), VMM_PAGE_SIZE) / VMM_PAGE_SIZE;
    phys_addr_t phys = pmm_alloc_pages_contiguous(pages);
    if (phys == 0) return 0;

    uintptr_t virt_base = heap_next;
    for (size_t i = 0; i < pages; i++) {
        virt_addr_t virt = heap_next;
        phys_addr_t p = phys + i * VMM_PAGE_SIZE;
        heap_next += VMM_PAGE_SIZE;

        if (vmm_map(vmm_kernel_address_space(), virt, p,
            VMM_PRESENT | VMM_WRITABLE | VMM_NO_EXECUTE) != 0) {
            for (size_t j = 0; j < i; j++) {
                vmm_unmap(vmm_kernel_address_space(), virt_base + j * VMM_PAGE_SIZE);
            }
            pmm_free_pages_contiguous(phys, pages);
            heap_next = virt_base;
            return 0;
        }
    }

    struct alloc_header *hdr = (struct alloc_header *)(uintptr_t)virt_base;
    hdr->bin_index = (uint16_t)pages;
    hdr->magic = MAGIC_DIRECT;
    hdr->reserved = 0;
    hdr->requested_size = size;
    hdr->base = 0;

    uint8_t *user_ptr = (uint8_t *)hdr + sizeof(struct alloc_header);

#ifdef HEAP_DEBUG
    memset(user_ptr, POISON_ALLOC, size);
#endif

    stats.allocations++;
    stats.total_allocated_bytes += size;
    stats.direct_pages += pages;
    return user_ptr;
}

void *kzalloc(size_t size)
{
    void *ptr = kmalloc(size);
    if (ptr != 0) memset(ptr, 0, size);
    return ptr;
}

void kfree(void *ptr)
{
    if (ptr == 0) return;

    uint8_t *raw = (uint8_t *)ptr - sizeof(struct alloc_header);
    struct alloc_header *hdr = (struct alloc_header *)raw;

    if (hdr->magic == MAGIC_FREE) {
        panic("Heap: double free at %p", ptr);
    }

    if (hdr->magic == MAGIC_ALIGNED) {
        void *base = hdr->base;
        hdr->magic = MAGIC_FREE;
        kfree(base);
        return;
    }

#ifdef HEAP_DEBUG
    if (hdr->magic == MAGIC_DIRECT) {
        size_t pages = hdr->bin_index;
        size_t total_size = pages * VMM_PAGE_SIZE - sizeof(struct alloc_header);
        uint8_t *check = (uint8_t *)ptr;
        for (size_t i = 0; i < total_size; i++) {
            if (check[i] != POISON_ALLOC && check[i] != POISON_FREE) {
                break;
            }
        }
    }
#endif

    if (hdr->magic == MAGIC_ALLOC) {
        int bin = hdr->bin_index;
        if (bin < 0 || bin >= BIN_COUNT) {
            panic("Heap: corrupted alloc header at %p", ptr);
        }

        struct slab_header *slab = bins[bin];
        while (slab != 0) {
            uintptr_t slab_base = (uintptr_t)slab;
            uintptr_t chunk_base = (uintptr_t)raw;
            if (chunk_base >= slab_base && chunk_base < slab_base + VMM_PAGE_SIZE) {
#ifdef HEAP_DEBUG
                size_t usable = bin_sizes[bin] - sizeof(struct alloc_header);
                memset(ptr, POISON_FREE, usable);
#endif
                hdr->magic = MAGIC_FREE;
                *(void **)raw = slab->free_list;
                slab->free_list = raw;
                slab->free_chunks++;
                stats.frees++;
                stats.total_freed_bytes += hdr->requested_size;
                return;
            }
            slab = slab->next;
        }

        panic("Heap: free of untracked slab pointer %p", ptr);
    } else if (hdr->magic == MAGIC_DIRECT) {
        size_t pages = hdr->bin_index;
        size_t requested_size = hdr->requested_size;
        uintptr_t page_base = (uintptr_t)raw & ~(VMM_PAGE_SIZE - 1);

#ifdef HEAP_DEBUG
        size_t total_size = pages * VMM_PAGE_SIZE - sizeof(struct alloc_header);
        memset(ptr, POISON_FREE, total_size);
#endif

        for (size_t i = 0; i < pages; i++) {
            phys_addr_t phys = 0;
            if (!vmm_virt_to_phys(vmm_kernel_address_space(),
                page_base + i * VMM_PAGE_SIZE, &phys)) {
                panic("Heap: direct free translation failed for %p", ptr);
            }
            vmm_unmap(vmm_kernel_address_space(), page_base + i * VMM_PAGE_SIZE);
            pmm_free_page(phys);
        }

        stats.frees++;
        stats.total_freed_bytes += requested_size;
        stats.direct_pages -= pages;
    } else {
        panic("Heap: invalid magic at %p (got magic=0x%x, bin=0x%x, res=0x%x, size=%llu, base=%p) (possible corruption)",
              ptr, hdr->magic, hdr->bin_index, hdr->reserved, (unsigned long long)hdr->requested_size, hdr->base);
    }
}

void *krealloc(void *ptr, size_t new_size)
{
    if (ptr == 0) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }

    uint8_t *raw = (uint8_t *)ptr - sizeof(struct alloc_header);
    struct alloc_header *hdr = (struct alloc_header *)raw;

    if (hdr->magic == MAGIC_DIRECT) {
        size_t old_pages = hdr->bin_index;
        size_t old_size = old_pages * VMM_PAGE_SIZE - sizeof(struct alloc_header);
        if (new_size <= old_size) {
            hdr->requested_size = new_size;
            return ptr;
        }
    } else if (hdr->magic == MAGIC_ALLOC) {
        int bin = hdr->bin_index;
        size_t old_usable = bin_sizes[bin] - sizeof(struct alloc_header);
        if (new_size <= old_usable) {
            hdr->requested_size = new_size;
            return ptr;
        }
    }

    void *new_ptr = kmalloc(new_size);
    if (new_ptr == 0) return 0;

    size_t copy_size = 0;
    if (hdr->magic == MAGIC_DIRECT) {
        copy_size = hdr->bin_index * VMM_PAGE_SIZE - sizeof(struct alloc_header);
    } else {
        copy_size = bin_sizes[hdr->bin_index] - sizeof(struct alloc_header);
    }
    if (copy_size > new_size) copy_size = new_size;

    memcpy(new_ptr, ptr, copy_size);
    kfree(ptr);
    return new_ptr;
}

void *kmalloc_aligned(size_t size, size_t alignment)
{
    if (alignment < sizeof(void *)) alignment = sizeof(void *);
    if (!is_power_of_two(alignment)) return 0;

    size_t total = size + alignment + sizeof(struct alloc_header);
    uint8_t *raw = (uint8_t *)kmalloc(total);
    if (raw == 0) return 0;

    uintptr_t aligned = align_up((uintptr_t)raw, alignment);
    while (aligned - (uintptr_t)raw < sizeof(struct alloc_header)) {
        aligned += alignment;
    }
    uint8_t *aligned_ptr = (uint8_t *)aligned;

    struct alloc_header *new_hdr = (struct alloc_header *)(aligned_ptr - sizeof(struct alloc_header));
    new_hdr->bin_index = 0;
    new_hdr->magic = MAGIC_ALIGNED;
    new_hdr->reserved = 0;
    new_hdr->requested_size = size;
    new_hdr->base = raw;

    return aligned_ptr;
}

struct heap_stats heap_get_stats(void)
{
    return stats;
}

void heap_print_stats(void)
{
    printk("Heap: allocs=%llu frees=%llu "
        "allocated=%llu freed=%llu "
        "slab_pages=%llu direct_pages=%llu\n",
        stats.allocations, stats.frees,
        stats.total_allocated_bytes, stats.total_freed_bytes,
        stats.slab_pages, stats.direct_pages);
}

// ltenix_setjmp and ltenix_longjmp are implemented in kernel/lib/setjmp.S

static jmp_buf_t heap_panic_env;
static volatile bool heap_panic_expected = false;
static volatile bool heap_panic_occurred = false;

static void heap_panic_hook_fn(void)
{
    if (heap_panic_expected) {
        heap_panic_occurred = true;
        heap_panic_expected = false;
        ltenix_longjmp(heap_panic_env, 1);
    }
}

void heap_self_test(void)
{
    void *p1 = kmalloc(24);
    if (p1 == 0) panic("Heap: self-test small alloc failed");
    memset(p1, 0x42, 24);
    for (size_t i = 0; i < 24; i++) {
        if (((uint8_t *)p1)[i] != 0x42) panic("Heap: self-test small write readback failed");
    }
    kfree(p1);

    void *p2 = kmalloc(512);
    if (p2 == 0) panic("Heap: self-test medium alloc failed");
    memset(p2, 0x5A, 512);
    for (size_t i = 0; i < 512; i++) {
        if (((uint8_t *)p2)[i] != 0x5A) panic("Heap: self-test medium write readback failed");
    }
    kfree(p2);

    void *p3 = kmalloc(5000);
    if (p3 == 0) panic("Heap: self-test large alloc failed");
    memset(p3, 0x7E, 5000);
    for (size_t i = 0; i < 5000; i++) {
        if (((uint8_t *)p3)[i] != 0x7E) panic("Heap: self-test large write readback failed");
    }
    kfree(p3);

    void *p4 = kzalloc(128);
    if (p4 == 0) panic("Heap: self-test kzalloc failed");
    for (size_t i = 0; i < 128; i++) {
        if (((uint8_t *)p4)[i] != 0) panic("Heap: self-test kzalloc nonzero at %llu", (unsigned long long)i);
    }
    kfree(p4);

    void *p5 = kmalloc(32);
    if (p5 == 0) panic("Heap: self-test krealloc source failed");
    memset(p5, 0xAB, 32);
    void *p6 = krealloc(p5, 200);
    if (p6 == 0) panic("Heap: self-test krealloc grow failed");
    for (size_t i = 0; i < 32; i++) {
        if (((uint8_t *)p6)[i] != 0xAB) panic("Heap: self-test krealloc data lost at %llu", (unsigned long long)i);
    }
    memset((uint8_t *)p6 + 32, 0xCD, 168);
    kfree(p6);

    void *p7 = kmalloc(512);
    if (p7 == 0) panic("Heap: self-test alloc-500 failed");
    kfree(p7);

    void *p8 = kmalloc_aligned(256, 64);
    if (p8 == 0) panic("Heap: self-test aligned alloc failed");
    if (((uintptr_t)p8 & 63) != 0) panic("Heap: self-test alignment failed");
    kfree(p8);

    struct heap_stats before = heap_get_stats();

    #define STRESS_COUNT 500
    void *blocks[STRESS_COUNT];
    for (int i = 0; i < STRESS_COUNT; i++) {
        blocks[i] = kmalloc((i % 1000) + 16);
        if (blocks[i] == 0) panic("Heap: self-test stress alloc %d failed", i);
        memset(blocks[i], (uint8_t)i, 16);
    }
    for (int i = 0; i < STRESS_COUNT; i++) {
        kfree(blocks[i]);
    }

    struct heap_stats after = heap_get_stats();

    if (after.frees - before.frees != STRESS_COUNT) {
        panic("Heap: self-test stress free count mismatch");
    }

    void *aligned_many[16];
    for (int i = 0; i < 16; i++) {
        aligned_many[i] = kmalloc_aligned(128, 1ULL << (i + 3));
        if (aligned_many[i] == 0) panic("Heap: self-test aligned stress alloc %d failed", i);
        if (((uintptr_t)aligned_many[i] & (((uintptr_t)1 << (i + 3)) - 1)) != 0) {
            panic("Heap: self-test aligned stress alignment %d failed", i);
        }
    }
    for (int i = 0; i < 16; i++) kfree(aligned_many[i]);

    void *large = kmalloc(128 * 1024);
    if (large == 0) panic("Heap: self-test large direct alloc failed");
    memset(large, 0xCC, 128 * 1024);
    for (size_t i = 0; i < 128 * 1024; i++) {
        if (((uint8_t *)large)[i] != 0xCC) panic("Heap: self-test large direct readback failed");
    }
    kfree(large);

    void *multi_page = kmalloc(64 * 1024 + 1234);
    if (multi_page == 0) panic("Heap: self-test multi-page direct alloc failed");
    memset(multi_page, 0x33, 64 * 1024 + 1234);
    kfree(multi_page);

    // Stronger fragmentation and slab reuse test
    {
        void *p[5];
        for (int i = 0; i < 5; i++) {
            p[i] = kmalloc(64);
            if (p[i] == 0) panic("Heap: fragmentation test alloc failed");
        }
        kfree(p[1]);
        kfree(p[3]);

        void *r1 = kmalloc(64);
        void *r2 = kmalloc(64);
        if (r1 == 0 || r2 == 0) panic("Heap: fragmentation test reuse alloc failed");

        bool match1 = (r1 == p[1] && r2 == p[3]);
        bool match2 = (r1 == p[3] && r2 == p[1]);
        if (!match1 && !match2) {
            panic("Heap: fragmentation test address reuse failed");
        }
        kfree(p[0]);
        kfree(r1);
        kfree(p[2]);
        kfree(r2);
        kfree(p[4]);
        printk("Heap: fragmentation reuse verified successfully\n");
    }

    // Stronger direct large-allocation test
    {
        struct heap_stats s1 = heap_get_stats();

        void *l1 = kmalloc(16 * 1024);
        void *l2 = kmalloc(32 * 1024);
        void *l3 = kmalloc(8 * 1024);
        if (l1 == 0 || l2 == 0 || l3 == 0) {
            panic("Heap: large alloc test failed");
        }

        if (((uintptr_t)l1 & 0xFFF) != sizeof(struct alloc_header)) {
            panic("Heap: large alloc l1 alignment check failed");
        }
        if (((uintptr_t)l2 & 0xFFF) != sizeof(struct alloc_header)) {
            panic("Heap: large alloc l2 alignment check failed");
        }
        if (((uintptr_t)l3 & 0xFFF) != sizeof(struct alloc_header)) {
            panic("Heap: large alloc l3 alignment check failed");
        }

        kfree(l2);
        kfree(l1);
        kfree(l3);

        struct heap_stats s2 = heap_get_stats();
        if (s2.allocations != s1.allocations + 3 || s2.frees != s1.frees + 3) {
            panic("Heap: large alloc stats mismatch");
        }
        if (s2.direct_pages != s1.direct_pages) {
            panic("Heap: large alloc leaked pages");
        }
        printk("Heap: direct large-allocations verified successfully\n");
    }

    // Negative tests: Double free, invalid free, corrupted header
    heap_panic_expected = false;
    heap_panic_occurred = false;

    // Double free test
    {
        void *p_double = kmalloc(32);
        heap_panic_occurred = false;
        heap_panic_expected = true;
        panic_hook = heap_panic_hook_fn;
        printk("[heap] panic_hook set to %p (fn=%p)\n", panic_hook, heap_panic_hook_fn);
        if (ltenix_setjmp(heap_panic_env) == 0) {
            kfree(p_double);
            kfree(p_double);
            panic_hook = 0;
            panic("Heap: negative test double-free did not panic");
        }
        panic_hook = 0;
        if (!heap_panic_occurred) {
            panic("Heap: negative test double-free panic not caught");
        }
        printk("Heap: negative test: double free panic detected and bypassed OK\n");
    }

    // Invalid free (wild pointer)
    {
        heap_panic_occurred = false;
        heap_panic_expected = true;
        panic_hook = heap_panic_hook_fn;
        if (ltenix_setjmp(heap_panic_env) == 0) {
            kfree((void *)0xdeadbeefULL);
            panic_hook = 0;
            panic("Heap: negative test wild pointer free did not panic");
        }
        panic_hook = 0;
        if (!heap_panic_occurred) {
            panic("Heap: negative test wild pointer free panic not caught");
        }
        printk("Heap: negative test: wild pointer free panic detected and bypassed OK\n");
    }

    // Invalid free (unaligned pointer)
    {
        void *p_unaligned = kmalloc(64);
        heap_panic_occurred = false;
        heap_panic_expected = true;
        panic_hook = heap_panic_hook_fn;
        if (ltenix_setjmp(heap_panic_env) == 0) {
            kfree((uint8_t *)p_unaligned + 8);
            panic_hook = 0;
            panic("Heap: negative test unaligned pointer free did not panic");
        }
        panic_hook = 0;
        kfree(p_unaligned);
        if (!heap_panic_occurred) {
            panic("Heap: negative test unaligned pointer free panic not caught");
        }
        printk("Heap: negative test: unaligned pointer free panic detected and bypassed OK\n");
    }

    // Corrupted header test
    {
        void *p_corrupt = kmalloc(32);
        struct alloc_header *hdr = (struct alloc_header *)((uint8_t *)p_corrupt - sizeof(struct alloc_header));
        uint16_t old_magic = hdr->magic;
        hdr->magic = 0x1234;

        heap_panic_occurred = false;
        heap_panic_expected = true;
        panic_hook = heap_panic_hook_fn;
        if (ltenix_setjmp(heap_panic_env) == 0) {
            kfree(p_corrupt);
            panic_hook = 0;
            panic("Heap: negative test corrupted magic did not panic");
        }
        panic_hook = 0;
        hdr->magic = old_magic;
        kfree(p_corrupt);
        if (!heap_panic_occurred) {
            panic("Heap: negative test corrupted magic panic not caught");
        }
        printk("Heap: negative test: corrupted magic panic detected and bypassed OK\n");
    }

    kfree(0);
    printk("Heap: kfree(NULL) accepted\n");

    struct heap_stats end_stats = heap_get_stats();
    if (end_stats.allocations != end_stats.frees) {
        panic("Heap: self-test leak allocs=%llu frees=%llu",
            end_stats.allocations, end_stats.frees);
    }
    if (end_stats.slab_pages > 0 && end_stats.slab_pages < 0) {
        panic("Heap: self-test slab_pages underflow");
    }
    if (end_stats.direct_pages != 0) {
        panic("Heap: self-test direct_pages not zero after free: %llu",
            end_stats.direct_pages);
    }

    heap_print_stats();
    printk("Heap: self-test passed\n");
}
