OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

KERNEL_VMA = 0xffffffff80000000;
KERNEL_LMA = 0x100000;

SECTIONS
{
    . = KERNEL_VMA;
    __kernel_start = .;

    . = ALIGN(4K);
    .text : AT (ADDR(.text) - KERNEL_VMA + KERNEL_LMA)
    {
        *(.text .text.*)
    }

    . = ALIGN(4K);
    .rodata : AT (ADDR(.rodata) - KERNEL_VMA + KERNEL_LMA)
    {
        *(.rodata .rodata.*)
    }

    . = ALIGN(4K);
    .limine_requests : AT (ADDR(.limine_requests) - KERNEL_VMA + KERNEL_LMA)
    {
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    }

    . = ALIGN(4K);
    .data : AT (ADDR(.data) - KERNEL_VMA + KERNEL_LMA)
    {
        *(.data .data.*)
    }

    . = ALIGN(4K);
    .bss : AT (ADDR(.bss) - KERNEL_VMA + KERNEL_LMA)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    . = ALIGN(4K);
    __kernel_end = .;
    __kernel_phys_start = __kernel_start - KERNEL_VMA + KERNEL_LMA;
    __kernel_phys_end = __kernel_end - KERNEL_VMA + KERNEL_LMA;
}
