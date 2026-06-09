#include <fs/block.h>
#include <arch/x86_64/io.h>
#include <kernel/printk.h>

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
    while (inb(ATA_PRIMARY_STATUS) & 0x80) { }
}

static void ata_wait_drq(void)
{
    while (!(inb(ATA_PRIMARY_STATUS) & 0x08)) {
        if (inb(ATA_PRIMARY_STATUS) & 0x01) {
            break; // Error
        }
    }
}

static int ata_read_blocks(struct block_device *dev, uint64_t lba, uint32_t count, void *buf)
{
    (void)dev;
    uint16_t *ptr = (uint16_t *)buf;

    for (uint32_t i = 0; i < count; i++) {
        uint32_t current_lba = lba + i;
        
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
        uint32_t current_lba = lba + i;
        
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
        
        // Wait for write to finish
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

static struct block_device ata_dev = {
    .name = "hda",
    .sector_count = 0,
    .sector_size = 512,
    .read_blocks = ata_read_blocks,
    .write_blocks = ata_write_blocks,
    .flush = ata_flush,
    .private_data = 0
};

void ata_init(void)
{
    outb(ATA_PRIMARY_DRV_HEAD, 0xA0);
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0) {
        // No drive
        return;
    }

    ata_wait_bsy();
    if (inb(ATA_PRIMARY_LBA_MID) != 0 || inb(ATA_PRIMARY_LBA_HI) != 0) {
        // Not ATA
        return;
    }

    ata_wait_drq();

    uint16_t identify[256];
    for (int i = 0; i < 256; i++) {
        identify[i] = inw(ATA_PRIMARY_DATA);
    }

    uint32_t sectors = *((uint32_t *)&identify[60]);
    ata_dev.sector_count = sectors;

    printk("ATA: found drive %s, %u sectors (%u MB)\n", ata_dev.name, sectors, (sectors * 512) / (1024 * 1024));

    block_device_register(&ata_dev);
}
