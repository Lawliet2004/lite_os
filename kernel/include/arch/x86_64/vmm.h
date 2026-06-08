#ifndef LITENIX_ARCH_X86_64_VMM_H
#define LITENIX_ARCH_X86_64_VMM_H

#include <kernel/types.h>
#include <stdbool.h>
#include <stdint.h>

#define VMM_PAGE_SIZE 4096ULL
#define VMM_TEST_BASE 0xffffffff90000000ULL
#define VMM_USER_BASE 0x1000ULL
#define VMM_USER_TOP  0x00007fffffffffffULL
#define VMM_KERNEL_BASE 0xffff800000000000ULL
#define VMM_GUARD_PAGE_SIZE VMM_PAGE_SIZE

enum vmm_flags {
    VMM_PRESENT = 1ULL << 0,
    VMM_WRITABLE = 1ULL << 1,
    VMM_USER = 1ULL << 2,
    VMM_NO_EXECUTE = 1ULL << 63,
};

struct address_space {
    phys_addr_t pml4_phys;
    phys_addr_t self_phys;
    uint64_t ref_count;
};

void vmm_init(uint64_t hhdm_offset);
int vmm_map(struct address_space *space, virt_addr_t virt, phys_addr_t phys, uint64_t flags);
int vmm_unmap(struct address_space *space, virt_addr_t virt);
int vmm_protect(struct address_space *space, virt_addr_t virt, uint64_t flags);
bool vmm_virt_to_phys(struct address_space *space, virt_addr_t virt, phys_addr_t *phys_out);
struct address_space *vmm_kernel_address_space(void);
struct address_space *vmm_create_address_space(void);
struct address_space *vmm_clone_address_space(struct address_space *parent);
void vmm_destroy_address_space(struct address_space *space);
void vmm_switch_address_space(struct address_space *space);
bool vmm_is_user_address(virt_addr_t virt);
bool vmm_is_kernel_address(virt_addr_t virt);
bool vmm_validate_user_ptr(struct address_space *space, const void *ptr, size_t length);
bool vmm_validate_user_ptr_writable(struct address_space *space, const void *ptr, size_t length);
int vmm_copy_from_user(void *kernel_dst, struct address_space *space, const void *user_src, size_t length);
int vmm_copy_to_user(struct address_space *space, void *user_dst, const void *kernel_src, size_t length);
void vmm_self_test(void);
void vmm_negative_self_test(void);

extern volatile uint64_t expect_page_fault_rip;
extern volatile uint64_t resume_page_fault_rip;
extern volatile uint64_t observed_page_fault_cr2;
extern volatile int page_fault_observed_count;

#endif
