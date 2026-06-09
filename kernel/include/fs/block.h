#ifndef LITENIX_FS_BLOCK_H
#define LITENIX_FS_BLOCK_H

#include <stdint.h>
#include <stddef.h>

struct block_device {
    char name[32];
    uint64_t sector_count;
    uint32_t sector_size;

    int (*read_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, void *buf);
    int (*write_blocks)(struct block_device *dev, uint64_t lba, uint32_t count, const void *buf);
    int (*flush)(struct block_device *dev);

    void *private_data;
};

void block_device_register(struct block_device *dev);
struct block_device *block_device_get(const char *name);

#endif
