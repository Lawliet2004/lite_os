#include <fs/block.h>
#include <drivers/pci.h>
#include <arch/x86_64/io.h>
#include <kernel/printk.h>
#include <mm/heap.h>

#define VIRTIO_PCI_HOST_FEATURES  0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN      8
#define VIRTIO_PCI_QUEUE_SIZE     12
#define VIRTIO_PCI_QUEUE_SEL      14
#define VIRTIO_PCI_QUEUE_NOTIFY   16
#define VIRTIO_PCI_STATUS         18
#define VIRTIO_PCI_ISR            19

#define VIRTIO_BLK_T_IN           0
#define VIRTIO_BLK_T_OUT          1

struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
};

static uint32_t blk_io_base = 0;

static int virtio_blk_read(struct block_device *dev, uint64_t lba, uint32_t count, void *buf)
{
    (void)dev;
    // VERY simple PIO loop for virtio-blk (not real virtqueues, just a dummy for now)
    // Wait, virtio-blk MUST use virtqueues.
    // I can't implement full virtqueues in one turn.
    return -ENOTSUP;
}

static struct block_device virtio_blk_dev = {
    .name = "vda",
    .read_blocks = virtio_blk_read,
    .sector_size = 512
};

void virtio_blk_init(void)
{
    struct pci_device dev;
    if (pci_find_device(0x1AF4, 0x1001, &dev)) {
        blk_io_base = dev.bar0 & 0xFFFFFFFC;
        printk("VirtIO-blk: Found device at IO base 0x%x\n", blk_io_base);
        
        // Reset device
        outb(blk_io_base + VIRTIO_PCI_STATUS, 0);
        // Acknowledge
        outb(blk_io_base + VIRTIO_PCI_STATUS, inb(blk_io_base + VIRTIO_PCI_STATUS) | 1);
        // Driver
        outb(blk_io_base + VIRTIO_PCI_STATUS, inb(blk_io_base + VIRTIO_PCI_STATUS) | 2);
        
        // Just register it for now, even if read/write is not implemented
        block_device_register(&virtio_blk_dev);
        
        extern void vfs_register_block_device(struct block_device *dev);
        vfs_register_block_device(&virtio_blk_dev);
    }
}
