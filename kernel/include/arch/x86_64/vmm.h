#ifndef LITENIX_ARCH_X86_64_VMM_H
#define LITENIX_ARCH_X86_64_VMM_H

#include <kernel/types.h>
#include <stdbool.h>
#include <stdint.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_TEST_BASE 0xffffffff90000000ULL

enum vmm_flags {
    VMM_PRESENT = 1ULL << 0,
    VMM_WRITABLE = 1ULL << 1,
    VMM_USER = 1ULL << 2,
    VMM_NO_EXECUTE = 1ULL << 63,
};

struct address_space {
    phys_addr_t pml4_phys;
};

void vmm_init(uint64_t hhdm_offset);
int vmm_map(struct address_space *space, virt_addr_t virt, phys_addr_t phys, uint64_t flags);
int vmm_unmap(struct address_space *space, virt_addr_t virt);
int vmm_protect(struct address_space *space, virt_addr_t virt, uint64_t flags);
bool vmm_virt_to_phys(struct address_space *space, virt_addr_t virt, phys_addr_t *phys_out);
struct address_space *vmm_kernel_address_space(void);
struct address_space *vmm_create_address_space(void);
void vmm_destroy_address_space(struct address_space *space);
void vmm_self_test(void);

#endif
