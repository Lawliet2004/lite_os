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

static int hda_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    struct block_device *dev = (struct block_device *)node->data;
    if (!dev || !dev->read_blocks) return -EIO;

    uint64_t max_bytes = dev->sector_count * dev->sector_size;
    if (offset >= max_bytes) return 0;
    if (offset + count > max_bytes) {
        count = max_bytes - offset;
    }

    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t offset_in_sector = offset % sector_size;

    uint8_t temp[512];
    size_t bytes_read = 0;

    if (offset_in_sector != 0) {
        if (dev->read_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        size_t chunk = sector_size - offset_in_sector;
        if (chunk > count) chunk = count;
        memcpy(buf, temp + offset_in_sector, chunk);
        bytes_read += chunk;
        start_sector++;
    }

    size_t remaining = count - bytes_read;
    uint32_t aligned_sectors = remaining / sector_size;
    if (aligned_sectors > 0) {
        if (dev->read_blocks(dev, start_sector, aligned_sectors, (uint8_t *)buf + bytes_read) != 0) {
            return -EIO;
        }
        bytes_read += aligned_sectors * sector_size;
        start_sector += aligned_sectors;
    }

    remaining = count - bytes_read;
    if (remaining > 0) {
        if (dev->read_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        memcpy((uint8_t *)buf + bytes_read, temp, remaining);
        bytes_read += remaining;
    }

    return (int)bytes_read;
}

static int hda_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    struct block_device *dev = (struct block_device *)node->data;
    if (!dev || !dev->write_blocks) return -EIO;

    uint64_t max_bytes = dev->sector_count * dev->sector_size;
    if (offset >= max_bytes) return -ENOSPC;
    if (offset + count > max_bytes) {
        count = max_bytes - offset;
    }

    uint32_t sector_size = dev->sector_size;
    uint64_t start_sector = offset / sector_size;
    uint32_t offset_in_sector = offset % sector_size;

    uint8_t temp[512];
    size_t bytes_written = 0;

    if (offset_in_sector != 0) {
        if (dev->read_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        size_t chunk = sector_size - offset_in_sector;
        if (chunk > count) chunk = count;
        memcpy(temp + offset_in_sector, buf, chunk);
        if (dev->write_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        bytes_written += chunk;
        start_sector++;
    }

    size_t remaining = count - bytes_written;
    uint32_t aligned_sectors = remaining / sector_size;
    if (aligned_sectors > 0) {
        if (dev->write_blocks(dev, start_sector, aligned_sectors, (const uint8_t *)buf + bytes_written) != 0) {
            return -EIO;
        }
        bytes_written += aligned_sectors * sector_size;
        start_sector += aligned_sectors;
    }

    remaining = count - bytes_written;
    if (remaining > 0) {
        if (dev->read_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        memcpy(temp, (const uint8_t *)buf + bytes_written, remaining);
        if (dev->write_blocks(dev, start_sector, 1, temp) != 0) {
            return -EIO;
        }
        bytes_written += remaining;
    }

    return (int)bytes_written;
}

void ata_init(void)
{
    outb(ATA_PRIMARY_DRV_HEAD, 0xA0);
    outb(ATA_PRIMARY_SECCOUNT, 0);
    outb(ATA_PRIMARY_LBA_LO, 0);
    outb(ATA_PRIMARY_LBA_MID, 0);
    outb(ATA_PRIMARY_LBA_HI, 0);
    outb(ATA_PRIMARY_CMD, ATA_CMD_IDENTIFY);

    uint8_t status = inb(ATA_PRIMARY_STATUS);
    if (status == 0 || status == 0xFF) {
        // No drive or floating bus
        return;
    }

    ata_wait_bsy();
    uint8_t mid = inb(ATA_PRIMARY_LBA_MID);
    uint8_t hi = inb(ATA_PRIMARY_LBA_HI);
    if (mid != 0 || hi != 0) {
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

    // Register /dev/hda device node in VFS
    vfs_create_device("/dev/hda", hda_read, hda_write);
    struct vfs_node *hda_node = vfs_lookup("/dev/hda");
    if (hda_node) {
        hda_node->data = &ata_dev;
    }
}
