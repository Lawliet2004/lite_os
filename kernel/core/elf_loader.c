#include <kernel/elf_loader.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/limine.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sched/scheduler.h>

#define EI_MAG0        0
#define EI_MAG1        1
#define EI_MAG2        2
#define EI_MAG3        3
#define EI_CLASS       4
#define EI_DATA        5
#define EI_VERSION     6
#define EI_OSABI       7
#define EI_ABIVERSION  8
#define EI_PAD         9
#define EI_NIDENT      16

#define ELFMAG0        0x7f
#define ELFMAG1        'E'
#define ELFMAG2        'L'
#define ELFMAG3        'F'

#define ELFCLASS64     2
#define ELFDATA2LSB    1

#define ET_EXEC        2
#define ET_DYN         3
#define EM_X86_64      62

#define PT_LOAD        1

#define PF_X           1
#define PF_W           2
#define PF_R           4

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint64_t      e_entry;
    uint64_t      e_phoff;
    uint64_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t   p_type;
    uint32_t   p_flags;
    uint64_t   p_offset;
    uint64_t   p_vaddr;
    uint64_t   p_paddr;
    uint64_t   p_filesz;
    uint64_t   p_memsz;
    uint64_t   p_align;
} Elf64_Phdr;

static inline void *phys_to_virt(phys_addr_t phys) {
    if (hhdm_request.response == 0) return 0;
    return (void *)(uintptr_t)(hhdm_request.response->offset + phys);
}

extern void user_task_entry(void *arg);
extern struct process *process_create_with_parent(struct process *parent);
extern struct task *task_create_user_process(const char *name, struct process *proc, void (*entry)(void *));

#define USER_STACK_PAGES 8          /* 32 KiB initial stack */
#define USER_STACK_TOP   0x00007ffffffff000ULL
#define MAX_STACK_ARGV   64

/* Auxiliary vector types (Linux ABI subset) */
#define AT_NULL    0
#define AT_PAGESZ  6
#define AT_ENTRY   9
#define AT_RANDOM  25

static uint64_t setup_user_stack(struct address_space *space, int argc,
                                  char **argv, char **envp, uint64_t entry_point)
{
    (void)argc; /* count comes from iterating argv */

    /*
     * Layout (from high to low in user virtual memory):
     *   [USER_STACK_TOP - 1]     <- top (last mapped page boundary)
     *   [strings: envp, argv null-terminated, AT_RANDOM bytes]
     *   [padding for 16-byte alignment]
     *   [auxv: AT_PAGESZ, AT_ENTRY, AT_RANDOM, AT_NULL]
     *   [envp[0]..envp[n], NULL]
     *   [argv[0]..argv[m], NULL]
     *   [argc]                   <- RSP points here on entry
     *
     * We build everything into a flat kernel buffer sized for
     * USER_STACK_PAGES pages, then copy to mapped physical pages.
     */
    const size_t stack_bytes = USER_STACK_PAGES * 4096;
    uint8_t *kbuf = kzalloc(stack_bytes);
    if (kbuf == 0) return 0;

    /*
     * We work backwards from the top of kbuf (which maps to USER_STACK_TOP).
     * kbuf[stack_bytes - 1] corresponds to the byte at USER_STACK_TOP - 1.
     * offset = stack_bytes - (USER_STACK_TOP - user_vaddr)
     */
    size_t top = stack_bytes; /* next free byte (working downward) */

    /* AT_RANDOM: 16 pseudo-random bytes */
    top -= 16;
    uint64_t rand_user_addr = USER_STACK_TOP - (stack_bytes - top);
    for (int i = 0; i < 16; i++) kbuf[top + i] = (uint8_t)(i ^ 0xA5 ^ (i * 7));

    /* Strings: envp */
    int argc_count = 0, envc = 0;
    uint64_t user_argv_addrs[MAX_STACK_ARGV];
    uint64_t user_envp_addrs[MAX_STACK_ARGV];

    if (envp) {
        while (envp[envc] != 0 && envc < MAX_STACK_ARGV) envc++;
        for (int i = envc - 1; i >= 0; i--) {
            size_t len = strlen(envp[i]) + 1;
            top -= len;
            memcpy(kbuf + top, envp[i], len);
            user_envp_addrs[i] = USER_STACK_TOP - (stack_bytes - top);
        }
    }

    /* Strings: argv */
    if (argv) {
        while (argv[argc_count] != 0 && argc_count < MAX_STACK_ARGV) argc_count++;
        for (int i = argc_count - 1; i >= 0; i--) {
            size_t len = strlen(argv[i]) + 1;
            top -= len;
            memcpy(kbuf + top, argv[i], len);
            user_argv_addrs[i] = USER_STACK_TOP - (stack_bytes - top);
        }
    }

    /* Align to 8 bytes */
    top &= ~(size_t)7;

    /* Calculate pointer table size:
     *   auxv: (3 entries + AT_NULL) * 2 uint64_t = 8 uint64_t
     *   envp: (envc + 1) uint64_t
     *   argv: (argc_count + 1) uint64_t
     *   argc: 1 uint64_t
     */
    size_t ptr_count = 8 + (envc + 1) + (argc_count + 1) + 1;
    size_t ptr_bytes = ptr_count * 8;

    /* Align RSP to 16 bytes:
     * Before _start call, the stack should have argc at RSP.
     * After the call instruction in _start prologue, RSP is misaligned by 8.
     * So we need (top - ptr_bytes) to be 8-mod-16 before we subtract.
     */
    size_t rsp_offset = (top - ptr_bytes);
    rsp_offset &= ~(size_t)15; /* 16-byte align */

    if (rsp_offset < 64) {
        /* Not enough space — buffer overflow would occur */
        kfree(kbuf);
        return 0;
    }

    /* Write pointer table at rsp_offset */
    uint64_t *p = (uint64_t *)(kbuf + rsp_offset);
    size_t idx = 0;

    p[idx++] = (uint64_t)argc_count;
    for (int i = 0; i < argc_count; i++) p[idx++] = user_argv_addrs[i];
    p[idx++] = 0; /* argv NULL */
    for (int i = 0; i < envc; i++)  p[idx++] = user_envp_addrs[i];
    p[idx++] = 0; /* envp NULL */
    /* auxv */
    p[idx++] = AT_PAGESZ;  p[idx++] = 4096;
    p[idx++] = AT_ENTRY;   p[idx++] = entry_point;
    p[idx++] = AT_RANDOM;  p[idx++] = rand_user_addr;
    p[idx++] = AT_NULL;    p[idx++] = 0;

    /* Map stack pages and copy kbuf into them */
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        phys_addr_t phys = pmm_alloc_page();
        if (phys == 0) { kfree(kbuf); return 0; }
        uint64_t vaddr = USER_STACK_TOP - (uint64_t)(i + 1) * 4096;
        vmm_map(space, vaddr, phys,
                VMM_PRESENT | VMM_WRITABLE | VMM_USER | VMM_NO_EXECUTE);
        /* kbuf page i starts at kbuf + (stack_bytes - (i+1)*4096) */
        size_t kbuf_page_start = stack_bytes - (size_t)(i + 1) * 4096;
        memcpy(phys_to_virt(phys), kbuf + kbuf_page_start, 4096);
    }

    uint64_t user_rsp = USER_STACK_TOP - (stack_bytes - rsp_offset);
    kfree(kbuf);
    return user_rsp;
}


bool elf_load_into_process(struct process *proc, const void *elf_data, size_t elf_size, int argc, char **argv, char **envp, uint64_t *out_entry, uint64_t *out_rsp)
{
    if (elf_size < sizeof(Elf64_Ehdr)) return false;
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;

    // Validate ELF header
    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        printk("ELF Loader: Invalid magic\n");
        return false;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_X86_64) {
        printk("ELF Loader: Unsupported architecture/format\n");
        return false;
    }

    struct address_space *new_space = vmm_create_address_space();
    if (new_space == 0) {
        printk("ELF Loader: Failed to create address space\n");
        return false;
    }

    // Load PT_LOAD segments
    Elf64_Phdr *phdrs = (Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;

        uint64_t flags = VMM_PRESENT | VMM_USER;
        if (phdr->p_flags & PF_W) flags |= VMM_WRITABLE;
        if (!(phdr->p_flags & PF_X)) flags |= VMM_NO_EXECUTE;

        uint64_t start_vaddr = phdr->p_vaddr;
        uint64_t end_vaddr = phdr->p_vaddr + phdr->p_memsz;

        uint64_t page_start = start_vaddr & ~4095ULL;
        uint64_t page_end = (end_vaddr + 4095ULL) & ~4095ULL;

        for (uint64_t vaddr = page_start; vaddr < page_end; vaddr += 4096) {
            phys_addr_t phys;
            if (vmm_virt_to_phys(new_space, vaddr, &phys)) {
                vmm_protect(new_space, vaddr, flags);
            } else {
                phys = pmm_alloc_page();
                if (phys == 0) {
                    printk("ELF Loader: Out of physical memory\n");
                    goto error_cleanup;
                }
                vmm_map(new_space, vaddr, phys, flags);
                memset(phys_to_virt(phys), 0, 4096);
            }

            uint64_t page_vaddr_start = vaddr;
            uint64_t page_vaddr_end = vaddr + 4096;

            uint64_t file_data_start = phdr->p_vaddr;
            uint64_t file_data_end = phdr->p_vaddr + phdr->p_filesz;

            uint64_t overlap_start = page_vaddr_start > file_data_start ? page_vaddr_start : file_data_start;
            uint64_t overlap_end = page_vaddr_end < file_data_end ? page_vaddr_end : file_data_end;

            if (overlap_start < overlap_end) {
                size_t copy_len = overlap_end - overlap_start;
                size_t file_offset = phdr->p_offset + (overlap_start - phdr->p_vaddr);
                size_t page_offset = overlap_start - page_vaddr_start;

                void *dest = (uint8_t *)phys_to_virt(phys) + page_offset;
                const void *src = (const uint8_t *)elf_data + file_offset;
                memcpy(dest, src, copy_len);
            }
        }
    }

    /* Setup stack — needs entry point for auxv AT_ENTRY */
    uint64_t user_rsp = setup_user_stack(new_space, argc, argv, envp, ehdr->e_entry);
    if (user_rsp == 0) {
        printk("ELF Loader: Failed to setup stack\n");
        goto error_cleanup;
    }

    /* Compute heap_start = page-aligned end of all loaded segments */
    uint64_t heap_start = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        uint64_t seg_end = (phdr->p_vaddr + phdr->p_memsz + 0xfffULL) & ~0xfffULL;
        if (seg_end > heap_start) heap_start = seg_end;
    }

    /* Success! Swap address space */
    if (proc->address_space != 0 && proc->owns_address_space) {
        vmm_destroy_address_space(proc->address_space);
    }

    proc->address_space = new_space;
    proc->owns_address_space = true;
    proc->heap_start = heap_start;
    proc->heap_end   = heap_start;  /* brk starts at segment end */
    *out_entry = ehdr->e_entry;
    *out_rsp = user_rsp;
    return true;

error_cleanup:
    vmm_destroy_address_space(new_space);
    return false;
}

struct task *elf_load(const char *name, const void *elf_data, size_t elf_size, int argc, char **argv, char **envp)
{
    struct process *proc = process_create_with_parent(init_process);
    if (proc == 0) return 0;

    uint64_t entry, rsp;
    if (!elf_load_into_process(proc, elf_data, elf_size, argc, argv, envp, &entry, &rsp)) {
        process_unlink_child(init_process, proc);
        kfree(proc);
        return 0;
    }

    // Pre-populate standard FDs
    process_init_standard_fds(proc);

    struct task *task = task_create_user_process(name, proc, user_task_entry);
    if (task == 0) {
        if (proc->address_space != 0) {
            vmm_destroy_address_space(proc->address_space);
        }
        process_unlink_child(init_process, proc);
        kfree(proc);
        return 0;
    }

    task->mode = TASK_MODE_USER;
    task->user_rip = entry;
    task->user_rsp = rsp;
    task->cr3 = proc->address_space->pml4_phys;

    proc->main_thread = task;
    return task;
}
