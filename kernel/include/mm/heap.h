#ifndef LITENIX_MM_HEAP_H
#define LITENIX_MM_HEAP_H

#include <stddef.h>
#include <stdint.h>

struct heap_stats {
    uint64_t allocations;
    uint64_t frees;
    uint64_t total_allocated_bytes;
    uint64_t total_freed_bytes;
    uint64_t slab_pages;
    uint64_t direct_pages;
};

void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void kfree(void *ptr);
void *krealloc(void *ptr, size_t new_size);
void *kmalloc_aligned(size_t size, size_t alignment);
struct heap_stats heap_get_stats(void);
void heap_print_stats(void);
void heap_self_test(void);

#endif
