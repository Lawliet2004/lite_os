#include <fs/block.h>
#include <lib/string.h>

#define MAX_BLOCK_DEVICES 8
static struct block_device *block_devices[MAX_BLOCK_DEVICES];
static int block_device_count = 0;

void block_device_register(struct block_device *dev)
{
    if (block_device_count >= MAX_BLOCK_DEVICES) return;
    block_devices[block_device_count++] = dev;
}

struct block_device *block_device_get(const char *name)
{
    for (int i = 0; i < block_device_count; i++) {
        if (strcmp(block_devices[i]->name, name) == 0) {
            return block_devices[i];
        }
    }
    return 0;
}
