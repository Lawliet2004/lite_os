#include <kernel/elf_loader.h>
#include <arch/x86_64/vmm.h>
#include <arch/x86_64/limine.h>
#include <mm/pmm.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sched/scheduler.h>
#include <fs/vfs.h>
#include <sys/syscall.h>

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
#define PT_INTERP      3

#define PF_X           1
#define PF_W           2
#define PF_R           4

/* Auxiliary vector types (Linux ABI) */
#define AT_NULL    0
#define AT_PHDR    3
#define AT_PHENT   4
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   8
#define AT_RANDOM  25
#define AT_EXECFN  31
#define AT_UID     11
#define AT_EUID    12
#define AT_GID     13
#define AT_EGID    14
#define AT_SECURE  23

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

static uint64_t setup_user_stack(struct address_space *space, int argc __attribute__((unused)),
                                  char **argv, char **envp, uint64_t entry_point __attribute__((unused)),
                                  uint64_t phdr_addr, int phnum, uint64_t interp_base,
                                  uint64_t exec_entry, const char *exec_path)
{
    const size_t stack_bytes = USER_STACK_PAGES * 4096;
    uint8_t *kbuf = kzalloc(stack_bytes);
    if (kbuf == 0) return 0;

    size_t top = stack_bytes;

    /* AT_RANDOM: 16 pseudo-random bytes */
    top -= 16;
    uint64_t rand_user_addr = USER_STACK_TOP - (stack_bytes - top);
    for (int i = 0; i < 16; i++) kbuf[top + i] = (uint8_t)(i ^ 0xA5 ^ (i * 7));

    /* AT_EXECFN string */
    size_t execfn_len = exec_path ? strlen(exec_path) + 1 : 1;
    top -= execfn_len;
    uint64_t execfn_addr = USER_STACK_TOP - (stack_bytes - top);
    if (exec_path) memcpy(kbuf + top, exec_path, execfn_len);
    else kbuf[top] = '\0';

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

    /* auxv entries count: AT_PHDR, AT_PHENT, AT_PHNUM, AT_PAGESZ, AT_BASE, AT_ENTRY, AT_RANDOM, AT_EXECFN, AT_UID, AT_EUID, AT_GID, AT_EGID, AT_SECURE, AT_NULL = 14 entries = 28 uint64_t */
    size_t ptr_count = 1 + (argc_count + 1) + (envc + 1) + 28; /* argc + argv + envp + auxv */
    size_t ptr_bytes = ptr_count * 8;

    size_t rsp_offset = (top - ptr_bytes);
    rsp_offset &= ~(size_t)15;

    if (rsp_offset < 64) {
        kfree(kbuf);
        return 0;
    }

    uint64_t *p = (uint64_t *)(kbuf + rsp_offset);
    size_t idx = 0;

    p[idx++] = (uint64_t)argc_count;
    for (int i = 0; i < argc_count; i++) p[idx++] = user_argv_addrs[i];
    p[idx++] = 0;
    for (int i = 0; i < envc; i++)  p[idx++] = user_envp_addrs[i];
    p[idx++] = 0;

    /* Full auxv vector */
    p[idx++] = AT_PHDR;    p[idx++] = phdr_addr;
    p[idx++] = AT_PHENT;   p[idx++] = sizeof(Elf64_Phdr);
    p[idx++] = AT_PHNUM;   p[idx++] = (uint64_t)phnum;
    p[idx++] = AT_PAGESZ;  p[idx++] = 4096;
    p[idx++] = AT_BASE;    p[idx++] = interp_base;
    p[idx++] = AT_ENTRY;   p[idx++] = exec_entry;
    p[idx++] = AT_RANDOM;  p[idx++] = rand_user_addr;
    p[idx++] = AT_EXECFN;  p[idx++] = execfn_addr;
    p[idx++] = AT_UID;     p[idx++] = 0;
    p[idx++] = AT_EUID;    p[idx++] = 0;
    p[idx++] = AT_GID;     p[idx++] = 0;
    p[idx++] = AT_EGID;    p[idx++] = 0;
    p[idx++] = AT_SECURE;  p[idx++] = 0;
    p[idx++] = AT_NULL;    p[idx++] = 0;

    /* Map stack pages */
    for (int i = 0; i < USER_STACK_PAGES; i++) {
        phys_addr_t phys = pmm_alloc_page();
        if (phys == 0) { kfree(kbuf); return 0; }
        uint64_t vaddr = USER_STACK_TOP - (uint64_t)(i + 1) * 4096;
        vmm_map(space, vaddr, phys,
                VMM_PRESENT | VMM_WRITABLE | VMM_USER | VMM_NO_EXECUTE);
        size_t kbuf_page_start = stack_bytes - (size_t)(i + 1) * 4096;
        memcpy(phys_to_virt(phys), kbuf + kbuf_page_start, 4096);
    }

    uint64_t user_rsp = USER_STACK_TOP - (stack_bytes - rsp_offset);
    kfree(kbuf);
    return user_rsp;
}

int elf_load_into_process(struct process *proc, const void *elf_data, size_t elf_size,
                           int argc, char **argv, char **envp, const char *exec_path,
                           uint64_t *out_entry, uint64_t *out_rsp)
{
    int err_code = 0;

    if (elf_size < sizeof(Elf64_Ehdr)) return -ENOEXEC;
    Elf64_Ehdr *ehdr = (Elf64_Ehdr *)elf_data;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_X86_64) {
        return -ENOEXEC;
    }

    struct address_space *new_space = vmm_create_address_space();
    if (new_space == 0) return -ENOMEM;

    Elf64_Phdr *phdrs = (Elf64_Phdr *)((const uint8_t *)elf_data + ehdr->e_phoff);

    /* Look for PT_INTERP */
    char *interp_path = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_INTERP) {
            interp_path = (char *)((const uint8_t *)elf_data + phdrs[i].p_offset);
            break;
        }
    }

    /* Determine load base for ET_DYN (PIE/shared objects) vs ET_EXEC */
    uint64_t load_base = (ehdr->e_type == ET_DYN) ? 0x400000ULL : 0ULL;
    uint64_t interp_base = 0;
    uint64_t interp_load_base = 0;
    uint64_t final_entry = load_base + ehdr->e_entry;
    uint64_t max_vaddr = 0;

    /* Load main executable PT_LOAD segments */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;

        uint64_t flags = VMM_PRESENT | VMM_USER;
        if (phdr->p_flags & PF_W) flags |= VMM_WRITABLE;
        if (!(phdr->p_flags & PF_X)) flags |= VMM_NO_EXECUTE;

        uint64_t seg_vaddr = load_base + phdr->p_vaddr;
        uint64_t start_vaddr = seg_vaddr;
        uint64_t end_vaddr = seg_vaddr + phdr->p_memsz;
        uint64_t page_start = start_vaddr & ~4095ULL;
        uint64_t page_end = (end_vaddr + 4095ULL) & ~4095ULL;

        if (page_end > max_vaddr) max_vaddr = page_end;

        for (uint64_t vaddr = page_start; vaddr < page_end; vaddr += 4096) {
            phys_addr_t phys;
            if (vmm_virt_to_phys(new_space, vaddr, &phys)) {
                vmm_protect(new_space, vaddr, flags);
            } else {
                phys = pmm_alloc_page();
                if (phys == 0) { err_code = -ENOMEM; goto error_cleanup; }
                vmm_map(new_space, vaddr, phys, flags);
                memset(phys_to_virt(phys), 0, 4096);
            }

            uint64_t page_vaddr_start = vaddr;
            uint64_t page_vaddr_end = vaddr + 4096;
            uint64_t file_data_start = seg_vaddr;
            uint64_t file_data_end = seg_vaddr + phdr->p_filesz;

            uint64_t overlap_start = page_vaddr_start > file_data_start ? page_vaddr_start : file_data_start;
            uint64_t overlap_end = page_vaddr_end < file_data_end ? page_vaddr_end : file_data_end;

            if (overlap_start < overlap_end) {
                size_t copy_len = overlap_end - overlap_start;
                size_t file_offset = phdr->p_offset + (overlap_start - seg_vaddr);
                size_t page_offset = overlap_start - page_vaddr_start;
                void *dest = (uint8_t *)phys_to_virt(phys) + page_offset;
                const void *src = (const uint8_t *)elf_data + file_offset;
                memcpy(dest, src, copy_len);
            }
        }
    }

    /* Load interpreter if PT_INTERP found */
    if (interp_path != 0) {
        printk("ELF Loader: PT_INTERP=%s\n", interp_path);
        struct vfs_node *interp_node = vfs_lookup(interp_path);
        if (interp_node == 0 || interp_node->data == 0) {
            printk("ELF Loader: interpreter not found\n");
            err_code = -ENOENT;
            goto error_cleanup;
        }

        Elf64_Ehdr *interp_ehdr = (Elf64_Ehdr *)interp_node->data;
        if (interp_ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
            interp_ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
            printk("ELF Loader: invalid interpreter ELF\n");
            err_code = -ENOEXEC;
            goto error_cleanup;
        }

        /* Choose interpreter base well above the main executable */
        interp_load_base = (max_vaddr + 0x1fffffULL) & ~0x1fffffULL;
        if (interp_load_base == 0) interp_load_base = 0x1000000ULL;

        Elf64_Phdr *interp_phdrs = (Elf64_Phdr *)((uint8_t *)interp_node->data + interp_ehdr->e_phoff);
        for (int i = 0; i < interp_ehdr->e_phnum; i++) {
            Elf64_Phdr *phdr = &interp_phdrs[i];
            if (phdr->p_type != PT_LOAD) continue;

            uint64_t flags = VMM_PRESENT | VMM_USER;
            if (phdr->p_flags & PF_W) flags |= VMM_WRITABLE;
            if (!(phdr->p_flags & PF_X)) flags |= VMM_NO_EXECUTE;

            uint64_t seg_vaddr = interp_load_base + phdr->p_vaddr;
            uint64_t start_vaddr = seg_vaddr;
            uint64_t end_vaddr = seg_vaddr + phdr->p_memsz;
            uint64_t page_start = start_vaddr & ~4095ULL;
            uint64_t page_end = (end_vaddr + 4095ULL) & ~4095ULL;

            for (uint64_t vaddr = page_start; vaddr < page_end; vaddr += 4096) {
                phys_addr_t phys;
                if (vmm_virt_to_phys(new_space, vaddr, &phys)) {
                    vmm_protect(new_space, vaddr, flags);
                } else {
                    phys = pmm_alloc_page();
                    if (phys == 0) { err_code = -ENOMEM; goto error_cleanup; }
                    vmm_map(new_space, vaddr, phys, flags);
                    memset(phys_to_virt(phys), 0, 4096);
                }

                uint64_t page_vaddr_start = vaddr;
                uint64_t page_vaddr_end = vaddr + 4096;
                uint64_t file_data_start = seg_vaddr;
                uint64_t file_data_end = seg_vaddr + phdr->p_filesz;

                uint64_t overlap_start = page_vaddr_start > file_data_start ? page_vaddr_start : file_data_start;
                uint64_t overlap_end = page_vaddr_end < file_data_end ? page_vaddr_end : file_data_end;

                if (overlap_start < overlap_end) {
                    size_t copy_len = overlap_end - overlap_start;
                    size_t file_offset = phdr->p_offset + (overlap_start - seg_vaddr);
                    size_t page_offset = overlap_start - page_vaddr_start;
                    void *dest = (uint8_t *)phys_to_virt(phys) + page_offset;
                    const void *src = (const uint8_t *)interp_node->data + file_offset;
                    memcpy(dest, src, copy_len);
                }
            }
        }
        interp_base = interp_load_base;
        final_entry = interp_load_base + interp_ehdr->e_entry;
        printk("ELF Loader: interpreter base=%llx entry=%llx\n", interp_load_base, final_entry);
    }

    /* Calculate phdr address (where first PT_LOAD maps the phdrs) */
    uint64_t phdr_addr = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdrs[i].p_type == PT_LOAD) {
            phdr_addr = load_base + phdrs[i].p_vaddr + ehdr->e_phoff;
            break;
        }
    }

    /* Setup stack */
    uint64_t user_rsp = setup_user_stack(new_space, argc, argv, envp, final_entry,
                                          phdr_addr, ehdr->e_phnum, interp_base,
                                          load_base + ehdr->e_entry, exec_path);
    if (user_rsp == 0) { err_code = -ENOMEM; goto error_cleanup; }

    /* Compute heap_start */
    uint64_t heap_start = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        Elf64_Phdr *phdr = &phdrs[i];
        if (phdr->p_type != PT_LOAD) continue;
        uint64_t seg_end = (load_base + phdr->p_vaddr + phdr->p_memsz + 0xfffULL) & ~0xfffULL;
        if (seg_end > heap_start) heap_start = seg_end;
    }

    if (proc->address_space != 0 && proc->owns_address_space) {
        vmm_destroy_address_space(proc->address_space);
    }

    proc->address_space = new_space;
    proc->owns_address_space = true;
    proc->heap_start = heap_start;
    proc->heap_end   = heap_start;
    *out_entry = final_entry;
    *out_rsp = user_rsp;
    return 0;

error_cleanup:
    vmm_destroy_address_space(new_space);
    return err_code;
}

struct task *elf_load(const char *name, const void *elf_data, size_t elf_size, int argc, char **argv, char **envp)
{
    struct process *proc = process_create_with_parent(init_process);
    if (proc == 0) return 0;

    uint64_t entry, rsp;
    if (elf_load_into_process(proc, elf_data, elf_size, argc, argv, envp, name, &entry, &rsp) != 0) {
        process_unlink_child(init_process, proc);
        kfree(proc);
        return 0;
    }

    process_init_standard_fds(proc);

    struct task *task = task_create_user_process(name, proc, user_task_entry);
    if (task == 0) {
        if (proc->address_space != 0) vmm_destroy_address_space(proc->address_space);
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