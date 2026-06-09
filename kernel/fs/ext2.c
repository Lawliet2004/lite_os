#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <kernel/printk.h>
#include <sys/syscall.h>

#define EXT2_SUPER_MAGIC 0xEF53

struct ext2_superblock {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count;
    uint32_t s_r_blocks_count;
    uint32_t s_free_blocks_count;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_frag_size;
    uint32_t s_blocks_per_group;
    uint32_t s_frags_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
};

struct ext2_group_desc {
    uint32_t bg_block_bitmap;
    uint32_t bg_inode_bitmap;
    uint32_t bg_inode_table;
    uint16_t bg_free_blocks_count;
    uint16_t bg_free_inodes_count;
    uint16_t bg_used_dirs_count;
    uint16_t bg_pad;
    uint32_t bg_reserved[3];
};

struct ext2_inode {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl;
    uint32_t i_dir_acl;
    uint32_t i_faddr;
    uint8_t  i_osd2[12];
};

struct ext2_dir_entry {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
};

struct ext2_vfs_data {
    uint32_t blocks[15];
    uint32_t inode_num;
};

static uint8_t ext2_disk_data[64 * 1024];

static void init_ext2_image(uint8_t *disk)
{
    memset(disk, 0, 64 * 1024);

    struct ext2_superblock *sb = (struct ext2_superblock *)(disk + 1024);
    sb->s_inodes_count = 16;
    sb->s_blocks_count = 64;
    sb->s_r_blocks_count = 0;
    sb->s_free_blocks_count = 64 - 9;
    sb->s_free_inodes_count = 16 - 11;
    sb->s_first_data_block = 1;
    sb->s_log_block_size = 0;
    sb->s_log_frag_size = 0;
    sb->s_blocks_per_group = 8192;
    sb->s_frags_per_group = 8192;
    sb->s_inodes_per_group = 16;
    sb->s_magic = EXT2_SUPER_MAGIC;
    sb->s_state = 1;
    sb->s_errors = 1;
    sb->s_rev_level = 0;

    struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
    gd->bg_block_bitmap = 3;
    gd->bg_inode_bitmap = 4;
    gd->bg_inode_table = 5;
    gd->bg_free_blocks_count = 64 - 9;
    gd->bg_free_inodes_count = 16 - 11;
    gd->bg_used_dirs_count = 1;

    disk[3 * 1024] = 0xFF;
    disk[3 * 1024 + 1] = 0x01;

    disk[4 * 1024] = 0xFF;
    disk[4 * 1024 + 1] = 0x07;

    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 5 * 1024);

    struct ext2_inode *root_inode = &inodes[1];
    root_inode->i_mode = 0x41ED;
    root_inode->i_size = 1024;
    root_inode->i_links_count = 2;
    root_inode->i_blocks = 2;
    root_inode->i_block[0] = 7;

    struct ext2_inode *file_inode = &inodes[10];
    file_inode->i_mode = 0x81A4;
    file_inode->i_size = 22;
    file_inode->i_links_count = 1;
    file_inode->i_blocks = 2;
    file_inode->i_block[0] = 8;

    uint8_t *dir_block = disk + 7 * 1024;

    struct ext2_dir_entry *de1 = (struct ext2_dir_entry *)dir_block;
    de1->inode = 2;
    de1->rec_len = 12;
    de1->name_len = 1;
    de1->file_type = 2;
    de1->name[0] = '.';

    struct ext2_dir_entry *de2 = (struct ext2_dir_entry *)(dir_block + 12);
    de2->inode = 2;
    de2->rec_len = 12;
    de2->name_len = 2;
    de2->file_type = 2;
    de2->name[0] = '.';
    de2->name[1] = '.';

    struct ext2_dir_entry *de3 = (struct ext2_dir_entry *)(dir_block + 24);
    de3->inode = 11;
    de3->rec_len = 1024 - 24;
    de3->name_len = 9;
    de3->file_type = 1;
    memcpy(de3->name, "hello.txt", 9);

    memcpy(disk + 8 * 1024, "Hello from EXT2 disk!\n", 22);
}

static int ram0_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    uint8_t *disk = (uint8_t *)node->data;
    if (disk == 0) return -EIO;
    if (offset >= 64 * 1024) return 0;
    size_t avail = 64 * 1024 - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, disk + offset, copy);
    return (int)copy;
}

static int ram0_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    uint8_t *disk = (uint8_t *)node->data;
    if (disk == 0) return -EIO;
    if (offset >= 64 * 1024) return -ENOSPC;
    size_t avail = 64 * 1024 - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(disk + offset, buf, copy);
    return (int)copy;
}

static int ext2_file_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    struct vfs_node *ram0 = vfs_lookup("/dev/ram0");
    if (ram0 == 0) return -EIO;
    uint8_t *disk = (uint8_t *)ram0->data;
    if (disk == 0) return -EIO;

    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (evd == 0) return -EIO;

    if (offset >= node->size) return 0;
    size_t avail = node->size - offset;
    size_t copy = count < avail ? count : avail;

    size_t bytes_copied = 0;
    while (copy > 0) {
        size_t block_idx = (offset + bytes_copied) / 1024;
        size_t block_offset = (offset + bytes_copied) % 1024;
        if (block_idx >= 12) break;

        uint32_t blk = evd->blocks[block_idx];
        if (blk == 0) break;

        size_t chunk = 1024 - block_offset;
        if (chunk > copy) chunk = copy;

        memcpy((uint8_t *)buf + bytes_copied, disk + blk * 1024 + block_offset, chunk);
        bytes_copied += chunk;
        copy -= chunk;
    }

    return (int)bytes_copied;
}

static void ext2_flush_to_disk(void)
{
    struct vfs_node *hda = vfs_lookup("/dev/hda");
    if (hda != 0 && hda->write != 0) {
        hda->write(hda, 0, ext2_disk_data, 64 * 1024);
    }
}

int ext2_file_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    struct vfs_node *ram0 = vfs_lookup("/dev/ram0");
    if (ram0 == 0) return -EIO;
    uint8_t *disk = (uint8_t *)ram0->data;
    if (disk == 0) return -EIO;

    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (evd == 0) return -EIO;

    size_t bytes_written = 0;
    size_t write_limit = count;

    while (write_limit > 0) {
        size_t block_idx = (offset + bytes_written) / 1024;
        size_t block_offset = (offset + bytes_written) % 1024;
        if (block_idx >= 12) break;

        uint32_t blk = evd->blocks[block_idx];
        if (blk == 0) {
            uint8_t *bitmap = disk + 3 * 1024;
            int free_blk = -1;
            for (int b = 9; b < 64; b++) {
                int byte = b / 8;
                int bit = b % 8;
                if (!(bitmap[byte] & (1 << bit))) {
                    bitmap[byte] |= (1 << bit);
                    free_blk = b;
                    break;
                }
            }
            if (free_blk == -1) return -ENOSPC;
            evd->blocks[block_idx] = free_blk;
            blk = free_blk;

            struct ext2_inode *inodes = (struct ext2_inode *)(disk + 5 * 1024);
            struct ext2_inode *in = &inodes[evd->inode_num - 1];
            in->i_block[block_idx] = blk;
            in->i_blocks += 2;

            struct ext2_superblock *sb = (struct ext2_superblock *)(disk + 1024);
            sb->s_free_blocks_count--;
            struct ext2_group_desc *gd = (struct ext2_group_desc *)(disk + 2048);
            gd->bg_free_blocks_count--;
        }

        size_t chunk = 1024 - block_offset;
        if (chunk > write_limit) chunk = write_limit;

        memcpy(disk + blk * 1024 + block_offset, (const uint8_t *)buf + bytes_written, chunk);
        bytes_written += chunk;
        write_limit -= chunk;
    }

    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
        struct ext2_inode *inodes = (struct ext2_inode *)(disk + 5 * 1024);
        struct ext2_inode *in = &inodes[evd->inode_num - 1];
        in->i_size = (uint32_t)node->size;
    }

    ext2_flush_to_disk();
    return (int)bytes_written;
}

int ext2_truncate(struct vfs_node *node, size_t new_size)
{
    if (new_size != 0) return -ENOTSUP; // We only support truncating to 0 for now

    struct vfs_node *ram0 = vfs_lookup("/dev/ram0");
    if (ram0 == 0) return -EIO;
    uint8_t *disk = (uint8_t *)ram0->data;
    if (disk == 0) return -EIO;

    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (evd == 0) return -EIO;

    struct ext2_inode *inodes = (struct ext2_inode *)(disk + 5 * 1024);
    struct ext2_inode *in = &inodes[evd->inode_num - 1];
    in->i_size = 0;
    node->size = 0;

    ext2_flush_to_disk();
    return 0;
}

void ext2_init(void)
{
    vfs_create_device("/dev/ram0", ram0_read, ram0_write);
    struct vfs_node *ram0 = vfs_lookup("/dev/ram0");
    if (ram0) {
        ram0->data = ext2_disk_data;
    }

    struct vfs_node *hda = vfs_lookup("/dev/hda");
    if (hda != 0 && hda->read != 0) {
        printk("ext2: /dev/hda detected, loading filesystem from disk...\n");
        int r = hda->read(hda, 0, ext2_disk_data, 64 * 1024);
        if (r < 0) {
            printk("ext2: ERROR: failed to read from /dev/hda (got %d)\n", r);
            init_ext2_image(ext2_disk_data);
        } else {
            struct ext2_superblock *sb = (struct ext2_superblock *)(ext2_disk_data + 1024);
            if (sb->s_magic == EXT2_SUPER_MAGIC) {
                printk("ext2: valid superblock magic found on /dev/hda!\n");
            } else {
                printk("ext2: invalid superblock magic on /dev/hda, formatting...\n");
                init_ext2_image(ext2_disk_data);
                if (hda->write != 0) {
                    hda->write(hda, 0, ext2_disk_data, 64 * 1024);
                }
            }
        }
    } else {
        printk("ext2: no /dev/hda detected, using pure memory /dev/ram0\n");
        init_ext2_image(ext2_disk_data);
    }

    vfs_create_file("/ext2", VFS_TYPE_DIR, 0, 0);

    struct ext2_inode *inodes = (struct ext2_inode *)(ext2_disk_data + 5 * 1024);
    struct ext2_inode *hello_in = &inodes[10];

    vfs_create_file("/ext2/hello.txt", VFS_TYPE_FILE, hello_in->i_size, 0);
    struct vfs_node *hello_node = vfs_lookup("/ext2/hello.txt");
    if (hello_node != 0) {
        struct ext2_vfs_data *evd = kmalloc(sizeof(struct ext2_vfs_data));
        if (evd != 0) {
            evd->inode_num = 11;
            for (int i = 0; i < 15; i++) {
                evd->blocks[i] = hello_in->i_block[i];
            }
            hello_node->data = evd;
            hello_node->read = ext2_file_read;
            hello_node->write = ext2_file_write;
        }
    }

    printk("ext2: block device initialized and mounted on /ext2\n");
}
