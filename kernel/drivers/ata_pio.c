#include <fs/block.h>
#include <arch/x86_64/io.h>
#include <kernel/printk.h>
#include <fs/vfs.h>
#include <lib/string.h>
#include <sys/syscall.h>

#define ATA_PRIMARY_DATA         0x1F0
#define ATA_PRIMARY_ERR          0x1F1
#define ATA_PRIMARY_SECCOUNT     0x1F2
#define ATA_PRIMARY_LBA_LO       0x1F3
#define ATA_PRIMARY_LBA_MID      0x1F4
#define ATA_PRIMARY_LBA_HI       0x1F5
#define ATA_PRIMARY_DRV_HEAD     0x1F6
#define ATA_PRIMARY_STATUS       0x1F7
#define ATA_PRIMARY_CMD          0x1F7

#define ATA_CMD_READ_PIO         0x20
#define ATA_CMD_WRITE_PIO        0x30
#define ATA_CMD_CACHE_FLUSH      0xE7
#define ATA_CMD_IDENTIFY         0xEC

static void ata_wait_bsy(void)
{
    uint32_t timeout = 1000000;
    while ((inb(ATA_PRIMARY_STATUS) & 0x80) && timeout > 0) {
        timeout--;
    }
}

static void ata_wait_drq(void)
{
    uint32_t timeout = 1000000;
    while (!(inb(ATA_PRIMARY_STATUS) & 0x08) && timeout > 0) {
        if (inb(ATA_PRIMARY_STATUS) & 0x01) {
            break; // Error
        }
        timeout--;
    }
}

static int ata_read_blocks(struct block_device *dev, uint64_t lba, uint32_t count, void *buf)
{
    (void)dev;
    uint16_t *ptr = (uint16_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t current_lba = (uint32_t)lba + i;

        ata_wait_bsy();
        outb(ATA_PRIMARY_DRV_HEAD, 0xE0 | ((current_lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_SECCOUNT, 1);
        outb(ATA_PRIMARY_LBA_LO, (uint8_t)current_lba);
        outb(ATA_PRIMARY_LBA_MID, (uint8_t)(current_lba >> 8));
        outb(ATA_PRIMARY_LBA_HI, (uint8_t)(current_lba >> 16));
        outb(ATA_PRIMARY_CMD, ATA_CMD_READ_PIO);

        ata_wait_bsy();
        ata_wait_drq();

        for (int j = 0; j < 256; j++) {
            ptr[j] = inw(ATA_PRIMARY_DATA);
        }
        ptr += 256;
    }
    return 0;
}

static int ata_write_blocks(struct block_device *dev, uint64_t lba, uint32_t count, const void *buf)
{
    (void)dev;
    const uint16_t *ptr = (const uint16_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t current_lba = (uint32_t)lba + i;

        ata_wait_bsy();
        outb(ATA_PRIMARY_DRV_HEAD, 0xE0 | ((current_lba >> 24) & 0x0F));
        outb(ATA_PRIMARY_SECCOUNT, 1);
        outb(ATA_PRIMARY_LBA_LO, (uint8_t)current_lba);
        outb(ATA_PRIMARY_LBA_MID, (uint8_t)(current_lba >> 8));
        outb(ATA_PRIMARY_LBA_HI, (uint8_t)(current_lba >> 16));
        outb(ATA_PRIMARY_CMD, ATA_CMD_WRITE_PIO);

        ata_wait_bsy();
        ata_wait_drq();

        for (int j = 0; j < 256; j++) {
            outw(ATA_PRIMARY_DATA, ptr[j]);
        }
        ptr += 256;

        outb(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
        ata_wait_bsy();
    }
    return 0;
}

static int ata_flush(struct block_device *dev)
{
    (void)dev;
    outb(ATA_PRIMARY_CMD, ATA_CMD_CACHE_FLUSH);
    ata_wait_bsy();
    return 0;
}

static struct block_device ata_devices[2];
static int ata_device_count = 0;

void ata_init_device(uint8_t drive_bit)
{
    outb(ATA_PRIMARY_DRV_HEAD, drive_bit);
    for (int i = 0; i < 15; i++) inb(ATA_PRIMARY_STATUS); // delay

    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0 || status == 0xFF) return;

    ata_wait_bsy();
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) return;

    ata_wait_drq();

    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ATA_PRIMARY_DATA);
    }

    uint32_t sectors;
    memcpy(&sectors, &identify[60], 4);
    if (sectors == 0) return;

    struct block_device *dev = &ata_devices[ata_device_count];
    dev->name[0] = 'h';
    dev->name[1] = 'd';
    dev->name[2] = 'a' + ata_device_count;
    dev->name[3] = '\0';
    dev->sector_count = sectors;
    dev->sector_size = 512;
    dev->read_blocks = ata_read_blocks;
    dev->write_blocks = ata_write_blocks;
    dev->flush = ata_flush;
    dev->private_data = (void *)(uintptr_t)drive_bit;

    printk("ATA: found drive %s, %u sectors (%u MB)\n", dev->name, sectors, (sectors * 512) / (1024 * 1024));

    block_device_register(dev);

    extern void vfs_register_block_device(struct block_device *dev);
    vfs_register_block_device(dev);

    ata_device_count++;
}

void ata_init(void)
{
    ata_init_device(0xA0); // Primary Master
    ata_init_device(0xB0); // Primary Slave
}
