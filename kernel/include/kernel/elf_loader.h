#ifndef LITENIX_KERNEL_ELF_LOADER_H
#define LITENIX_KERNEL_ELF_LOADER_H

#include <sched/task.h>
#include <stddef.h>

struct task *elf_load(const char *name, const void *elf_data, size_t elf_size, int argc, char **argv, char **envp);
int elf_load_into_process(struct process *proc, const void *elf_data, size_t elf_size, int argc, char **argv, char **envp, const char *exec_path, uint64_t *out_entry, uint64_t *out_rsp);

#endif /* LITENIX_KERNEL_ELF_LOADER_H */
