section .rodata

global user_init_elf_start
global user_init_elf_end
user_init_elf_start:
    incbin "build/user/init.elf"
user_init_elf_end:

global user_test_read_kernel_elf_start
global user_test_read_kernel_elf_end
user_test_read_kernel_elf_start:
    incbin "build/user/test_read_kernel.elf"
user_test_read_kernel_elf_end:

global user_test_privileged_elf_start
global user_test_privileged_elf_end
user_test_privileged_elf_start:
    incbin "build/user/test_privileged.elf"
user_test_privileged_elf_end:
