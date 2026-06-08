ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text*)
    }

    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata*)
    }

    . = ALIGN(0x1000);

    .data : {
        *(.data*)
    }

    . = ALIGN(0x1000);

    .bss : {
        *(.bss*)
    }
}
