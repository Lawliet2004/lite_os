section .rodata

global initramfs_start
global initramfs_end

initramfs_start:
    incbin "build/initramfs.tar"
initramfs_end:
