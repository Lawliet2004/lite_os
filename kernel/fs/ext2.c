#include <fs/block.h>
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

struct ext2_dir_entry_2 {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} __attribute__((packed));

struct ext2_vfs_data {
    uint32_t blocks[15];
    uint32_t inode_num;
};

static struct vfs_node *hda = 0;
static struct ext2_superblock current_sb;

static bool ext2_is_valid_block(uint32_t blk)
{
    if (blk == 0) return false;
    if (blk >= current_sb.s_blocks_count) return false;
    if (hda && ((size_t)blk * 1024 >= hda->size)) return false;
    return true;
}

static bool ext2_is_valid_inode(uint32_t ino)
{
    if (ino == 0) return false;
    if (ino > current_sb.s_inodes_count) return false;
    return true;
}

static void ext2_save_inode(uint32_t inode_num, struct ext2_inode *in)
{
    if (hda == 0 || hda->write == 0) return;
    if (!ext2_is_valid_inode(inode_num)) return;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_inode_table)) return;
    uint32_t block = gd->bg_inode_table + ((inode_num - 1) * 128) / 1024;
    uint32_t offset = ((inode_num - 1) * 128) % 1024;
    if (!ext2_is_valid_block(block)) return;
    uint8_t temp[1024];
    hda->read(hda, (size_t)block * 1024, temp, 1024);
    memcpy(temp + offset, in, 128);
    hda->write(hda, (size_t)block * 1024, temp, 1024);
}

static void ext2_free_block(uint32_t blk)
{
    if (hda == 0 || hda->write == 0) return;
    if (!ext2_is_valid_block(blk)) return;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_block_bitmap)) return;
    uint8_t bitmap[1024];
    hda->read(hda, (size_t)gd->bg_block_bitmap * 1024, bitmap, 1024);
    uint32_t i = blk - 1;
    if (i < 8192) {
        if (bitmap[i / 8] & (1 << (i % 8))) {
            bitmap[i / 8] &= ~(1 << (i % 8));
            hda->write(hda, (size_t)gd->bg_block_bitmap * 1024, bitmap, 1024);
            gd->bg_free_blocks_count++;
            hda->write(hda, 2048, gd_buf, 1024);
        }
    }
}

static void ext2_free_inode(uint32_t ino)
{
    if (hda == 0 || hda->write == 0) return;
    if (!ext2_is_valid_inode(ino)) return;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_inode_bitmap)) return;
    uint8_t bitmap[1024];
    hda->read(hda, (size_t)gd->bg_inode_bitmap * 1024, bitmap, 1024);
    uint32_t i = ino - 1;
    if (i < 8192) {
        if (bitmap[i / 8] & (1 << (i % 8))) {
            bitmap[i / 8] &= ~(1 << (i % 8));
            hda->write(hda, (size_t)gd->bg_inode_bitmap * 1024, bitmap, 1024);
            gd->bg_free_inodes_count++;
            hda->write(hda, 2048, gd_buf, 1024);
        }
    }
}

static uint32_t ext2_alloc_block(void)
{
    if (hda == 0 || hda->read == 0 || hda->write == 0) return 0;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_block_bitmap)) return 0;
    uint8_t bitmap[1024];
    hda->read(hda, (size_t)gd->bg_block_bitmap * 1024, bitmap, 1024);
    for (uint32_t i = 0; i < 8192; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            if (!ext2_is_valid_block(i + 1)) continue;
            bitmap[i / 8] |= (1 << (i % 8));
            hda->write(hda, (size_t)gd->bg_block_bitmap * 1024, bitmap, 1024);
            gd->bg_free_blocks_count--;
            hda->write(hda, 2048, gd_buf, 1024);
            uint8_t zero[1024];
            memset(zero, 0, 1024);
            hda->write(hda, (size_t)(i + 1) * 1024, zero, 1024);
            return i + 1;
        }
    }
    return 0;
}

static uint32_t ext2_alloc_inode(uint8_t type)
{
    if (hda == 0 || hda->read == 0 || hda->write == 0) {
        printk("ext2_alloc_inode: hda invalid\n");
        return 0;
    }
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_inode_bitmap)) {
        printk("ext2_alloc_inode: invalid inode bitmap block %u\n", gd->bg_inode_bitmap);
        return 0;
    }
    uint8_t bitmap[1024];
    hda->read(hda, (size_t)gd->bg_inode_bitmap * 1024, bitmap, 1024);
    uint32_t max_ino = current_sb.s_inodes_per_group;
    if (max_ino > 8192) max_ino = 8192;
    for (uint32_t i = 10; i < max_ino; i++) {
        if (!(bitmap[i / 8] & (1 << (i % 8)))) {
            if (!ext2_is_valid_inode(i + 1)) {
                printk("ext2_alloc_inode: invalid inode %u\n", i + 1);
                continue;
            }
            bitmap[i / 8] |= (1 << (i % 8));
            hda->write(hda, (size_t)gd->bg_inode_bitmap * 1024, bitmap, 1024);
            gd->bg_free_inodes_count--;
            if (type == 2) gd->bg_used_dirs_count++;
            hda->write(hda, 2048, gd_buf, 1024);
            uint32_t inode_num = i + 1;
            struct ext2_inode in;
            memset(&in, 0, 128);
            if (type == 1) in.i_mode = 0x81A4;
            else if (type == 2) in.i_mode = 0x41ED;
            in.i_links_count = 1;
            ext2_save_inode(inode_num, &in);
            return inode_num;
        }
    }
    printk("ext2_alloc_inode: out of inodes (scanned up to %u)\n", max_ino);
    return 0;
}

static int ext2_file_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    if (hda == 0 || hda->read == 0) return -EIO;
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
        if (!ext2_is_valid_block(blk)) break;
        size_t chunk = 1024 - block_offset;
        if (chunk > copy) chunk = copy;
        uint8_t temp[1024];
        hda->read(hda, (size_t)blk * 1024, temp, 1024);
        memcpy((uint8_t *)buf + bytes_copied, temp + block_offset, chunk);
        bytes_copied += chunk;
        copy -= chunk;
    }
    return (int)bytes_copied;
}

int ext2_file_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    if (hda == 0 || hda->write == 0) return -EIO;
    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (evd == 0) return -EIO;
    if (offset + count > 12 * 1024) return -ENOSPC;
    size_t bytes_written = 0;
    size_t write_limit = count;
    while (write_limit > 0) {
        size_t block_idx = (offset + bytes_written) / 1024;
        size_t block_offset = (offset + bytes_written) % 1024;
        if (block_idx >= 12) break;
        uint32_t blk = evd->blocks[block_idx];
        if (blk == 0) {
            blk = ext2_alloc_block();
            if (blk == 0) return -ENOSPC;
            evd->blocks[block_idx] = blk;
            struct ext2_inode in;
            uint8_t gd_buf[1024];
            hda->read(hda, 2048, gd_buf, 1024);
            struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
            uint32_t it_block = gd->bg_inode_table + ((evd->inode_num - 1) * 128) / 1024;
            uint32_t it_offset = ((evd->inode_num - 1) * 128) % 1024;
            uint8_t temp_it[1024];
            hda->read(hda, (size_t)it_block * 1024, temp_it, 1024);
            memcpy(&in, temp_it + it_offset, 128);
            in.i_block[block_idx] = blk;
            in.i_blocks = ((node->size + 1023) / 1024) * 2;
            ext2_save_inode(evd->inode_num, &in);
        }
        size_t chunk = 1024 - block_offset;
        if (chunk > write_limit) chunk = write_limit;
        uint8_t temp[1024];
        hda->read(hda, (size_t)blk * 1024, temp, 1024);
        memcpy(temp + block_offset, (const uint8_t *)buf + bytes_written, chunk);
        hda->write(hda, (size_t)blk * 1024, temp, 1024);
        bytes_written += chunk;
        write_limit -= chunk;
    }
    if (offset + bytes_written > node->size) {
        node->size = offset + bytes_written;
        struct ext2_inode in;
        uint8_t gd_buf[1024];
        hda->read(hda, 2048, gd_buf, 1024);
        struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
        uint32_t it_block = gd->bg_inode_table + ((evd->inode_num - 1) * 128) / 1024;
        uint32_t it_offset = ((evd->inode_num - 1) * 128) % 1024;
        uint8_t temp_it[1024];
        hda->read(hda, (size_t)it_block * 1024, temp_it, 1024);
        memcpy(&in, temp_it + it_offset, 128);
        in.i_size = (uint32_t)node->size;
        ext2_save_inode(evd->inode_num, &in);
    }
    
    // Flush to disk
    if (hda && hda->data) {
        struct block_device *dev = (struct block_device *)hda->data;
        if (dev->flush) dev->flush(dev);
    }

    return (int)bytes_written;
}

int ext2_truncate(struct vfs_node *node, size_t new_size)
{
    if (new_size != 0) return -ENOTSUP;
    node->size = 0;
    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (evd && hda && hda->write) {
        for (int i = 0; i < 12; i++) {
            if (evd->blocks[i] != 0) {
                ext2_free_block(evd->blocks[i]);
                evd->blocks[i] = 0;
            }
        }
        struct ext2_inode in;
        uint8_t gd_buf[1024];
        hda->read(hda, 2048, gd_buf, 1024);
        struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
        uint32_t it_block = gd->bg_inode_table + ((evd->inode_num - 1) * 128) / 1024;
        uint32_t it_offset = ((evd->inode_num - 1) * 128) % 1024;
        uint8_t temp_it[1024];
        hda->read(hda, (size_t)it_block * 1024, temp_it, 1024);
        memcpy(&in, temp_it + it_offset, 128);
        in.i_size = 0;
        ext2_save_inode(evd->inode_num, &in);
    }
    return 0;
}

static int ext2_readdir(struct vfs_node *node, size_t index, struct vfs_dirent *dirent)
{
    struct ext2_vfs_data *evd = (struct ext2_vfs_data *)node->data;
    if (!evd || !hda) return -EIO;
    uint32_t block = evd->blocks[0];
    if (block == 0) return -EIO;
    if (!ext2_is_valid_block(block)) return -EIO;
    uint8_t dir_buf[1024];
    hda->read(hda, (size_t)block * 1024, dir_buf, 1024);
    uint32_t offset = 0;
    size_t current_index = 0;
    while (offset < 1024) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(dir_buf + offset);
        if (de->rec_len < 8) break;
        if (de->inode != 0) {
            if (current_index == index) {
                size_t nlen = de->name_len < 255 ? de->name_len : 255;
                memcpy(dirent->name, de->name, nlen);
                dirent->name[nlen] = '\0';
                dirent->inode_num = de->inode;
                dirent->type = (de->file_type == 2) ? VFS_TYPE_DIR : VFS_TYPE_FILE;
                return 0;
            }
            current_index++;
        }
        offset += de->rec_len;
    }
    return -ENOENT;
}

static int ext2_add_entry(struct vfs_node *parent, const char *name, uint32_t inode_num, uint8_t type)
{
    struct ext2_vfs_data *pevd = (struct ext2_vfs_data *)parent->data;
    if (!pevd || !hda) return -EIO;
    uint32_t block = pevd->blocks[0];
    if (block == 0) return -ENOSPC;
    if (!ext2_is_valid_block(block)) return -EIO;
    uint8_t dir_buf[1024];
    hda->read(hda, (size_t)block * 1024, dir_buf, 1024);
    uint32_t offset = 0;
    struct ext2_dir_entry_2 *last_de = 0;
    while (offset < 1024) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(dir_buf + offset);
        if (de->rec_len == 0) break;
        last_de = de;
        if (offset + de->rec_len >= 1024) break;
        offset += de->rec_len;
    }
    if (!last_de) return -EIO;
    uint32_t min_last_rec_len = (8 + last_de->name_len + 3) & ~3;
    uint32_t remaining = last_de->rec_len - min_last_rec_len;
    size_t name_len = strlen(name);
    uint32_t needed = (8 + name_len + 3) & ~3;
    if (remaining < needed) return -ENOSPC;
    last_de->rec_len = (uint16_t)min_last_rec_len;
    offset += min_last_rec_len;
    struct ext2_dir_entry_2 *new_de = (struct ext2_dir_entry_2 *)(dir_buf + offset);
    new_de->inode = inode_num;
    new_de->rec_len = (uint16_t)remaining;
    new_de->name_len = (uint8_t)name_len;
    new_de->file_type = type;
    memcpy(new_de->name, name, name_len);
    hda->write(hda, (size_t)block * 1024, dir_buf, 1024);
    return 0;
}

static int ext2_create(struct vfs_node *parent, const char *name, uint32_t mode, struct vfs_node **out_node);
static int ext2_mkdir(struct vfs_node *parent, const char *name, uint32_t mode);
static int ext2_unlink(struct vfs_node *parent, const char *name);
static int ext2_rename(struct vfs_node *old_parent, const char *old_name, struct vfs_node *new_parent, const char *new_name);

static int ext2_create(struct vfs_node *parent, const char *name, uint32_t mode, struct vfs_node **out_node)
{
    uint32_t inode_num = ext2_alloc_inode(1);
    if (inode_num == 0) {
        printk("ext2_create: ext2_alloc_inode failed\n");
        return -ENOSPC;
    }
    int r = ext2_add_entry(parent, name, inode_num, 1);
    if (r != 0) {
        printk("ext2_create: ext2_add_entry failed with %d\n", r);
        return r;
    }
    struct vfs_node *new_node = kzalloc(sizeof(struct vfs_node));
    if (!new_node) {
        printk("ext2_create: kzalloc failed\n");
        return -ENOMEM;
    }
    strcpy(new_node->name, name);
    new_node->type = VFS_TYPE_FILE;
    new_node->mode = mode | S_IFREG;
    new_node->inode_num = inode_num;
    new_node->parent = parent;
    struct ext2_vfs_data *evd = kzalloc(sizeof(struct ext2_vfs_data));
    if (evd) {
        evd->inode_num = inode_num;
        new_node->data = evd;
        new_node->read = ext2_file_read;
        new_node->write = ext2_file_write;
        new_node->truncate = ext2_truncate;
    } else {
        printk("ext2_create: evd kzalloc failed\n");
    }
    new_node->next = parent->children;
    parent->children = new_node;
    if (out_node) *out_node = new_node;
    return 0;
}

static int ext2_mkdir(struct vfs_node *parent, const char *name, uint32_t mode)
{
    uint32_t inode_num = ext2_alloc_inode(2);
    if (inode_num == 0) return -ENOSPC;
    int r = ext2_add_entry(parent, name, inode_num, 2);
    if (r != 0) return r;
    uint32_t block = ext2_alloc_block();
    if (block == 0) return -ENOSPC;
    uint8_t dir_buf[1024];
    memset(dir_buf, 0, 1024);
    struct ext2_dir_entry_2 *de_dot = (struct ext2_dir_entry_2 *)dir_buf;
    de_dot->inode = inode_num;
    de_dot->rec_len = 12;
    de_dot->name_len = 1;
    de_dot->file_type = 2;
    de_dot->name[0] = '.';
    struct ext2_dir_entry_2 *de_dotdot = (struct ext2_dir_entry_2 *)(dir_buf + 12);
    de_dotdot->inode = parent->inode_num;
    de_dotdot->rec_len = 1024 - 12;
    de_dotdot->name_len = 2;
    de_dotdot->file_type = 2;
    de_dotdot->name[0] = '.';
    de_dotdot->name[1] = '.';
    hda->write(hda, (size_t)block * 1024, dir_buf, 1024);
    struct ext2_inode in;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    uint32_t it_block = gd->bg_inode_table + ((inode_num - 1) * 128) / 1024;
    uint32_t it_offset = ((inode_num - 1) * 128) % 1024;
    uint8_t temp_it[1024];
    hda->read(hda, (size_t)it_block * 1024, temp_it, 1024);
    memcpy(&in, temp_it + it_offset, 128);
    in.i_block[0] = block;
    in.i_size = 1024;
    in.i_links_count = 2;
    in.i_blocks = 2;
    ext2_save_inode(inode_num, &in);
    struct vfs_node *new_node = kzalloc(sizeof(struct vfs_node));
    if (new_node) {
        strcpy(new_node->name, name);
        new_node->type = VFS_TYPE_DIR;
        new_node->mode = mode | S_IFDIR;
        new_node->inode_num = inode_num;
        new_node->parent = parent;
        new_node->readdir = ext2_readdir;
        new_node->create = ext2_create;
        new_node->mkdir = ext2_mkdir;
        new_node->unlink = ext2_unlink;
        new_node->rename = ext2_rename;
        struct ext2_vfs_data *evd = kzalloc(sizeof(struct ext2_vfs_data));
        if (evd) {
            evd->inode_num = inode_num;
            evd->blocks[0] = block;
            new_node->data = evd;
        }
        new_node->next = parent->children;
        parent->children = new_node;
    }
    return 0;
}

static int ext2_unlink(struct vfs_node *parent, const char *name)
{
    struct ext2_vfs_data *pevd = (struct ext2_vfs_data *)parent->data;
    if (!pevd || !hda) return -EIO;
    uint32_t block = pevd->blocks[0];
    if (block == 0) return -EIO;
    if (!ext2_is_valid_block(block)) return -EIO;

    uint8_t dir_buf[1024];
    hda->read(hda, (size_t)block * 1024, dir_buf, 1024);

    uint32_t offset = 0;
    uint32_t target_inode = 0;
    while (offset < 1024) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(dir_buf + offset);
        if (de->rec_len < 8) break;
        if (de->inode != 0) {
            size_t nlen = de->name_len < 255 ? de->name_len : 255;
            if (nlen == strlen(name) && memcmp(de->name, name, nlen) == 0) {
                target_inode = de->inode;
                de->inode = 0;
                hda->write(hda, (size_t)block * 1024, dir_buf, 1024);
                break;
            }
        }
        if (offset + de->rec_len >= 1024) break;
        offset += de->rec_len;
    }

    if (target_inode == 0) return -ENOENT;

    struct ext2_inode in;
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    if (!ext2_is_valid_block(gd->bg_inode_table)) return 0;
    uint32_t it_block = gd->bg_inode_table + ((target_inode - 1) * 128) / 1024;
    uint32_t it_offset = ((target_inode - 1) * 128) % 1024;
    if (!ext2_is_valid_block(it_block)) return 0;

    uint8_t temp_it[1024];
    hda->read(hda, (size_t)it_block * 1024, temp_it, 1024);
    memcpy(&in, temp_it + it_offset, 128);

    if (in.i_links_count > 0) {
        if ((in.i_mode & S_IFMT) == S_IFDIR) {
            in.i_links_count = 0; // Removing a directory drops both links (parent dirent and inner '.')
        } else {
            in.i_links_count--;
        }
        if (in.i_links_count == 0) {
            for (int i = 0; i < 12; i++) {
                if (in.i_block[i] != 0) {
                    ext2_free_block(in.i_block[i]);
                    in.i_block[i] = 0;
                }
            }
            in.i_size = 0;
            in.i_blocks = 0;
            in.i_dtime = 1;
            ext2_save_inode(target_inode, &in);
            ext2_free_inode(target_inode);
        } else {
            ext2_save_inode(target_inode, &in);
        }
    }
    
    // Flush to disk
    if (hda && hda->data) {
        struct block_device *dev = (struct block_device *)hda->data;
        if (dev->flush) dev->flush(dev);
    }

    return 0;
}

int ram_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    if (node->data == 0) return 0;
    if (offset >= node->size) return 0;
    size_t avail = node->size - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, (uint8_t *)node->data + offset, copy);
    return (int)copy;
}

int ram_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    if (node->data == 0) return -ENOSPC;
    if (offset >= 1024) return -ENOSPC;
    size_t avail = 1024 - offset;
    size_t copy = count < avail ? count : avail;
    memcpy((uint8_t *)node->data + offset, buf, copy);
    if (offset + copy > node->size) node->size = offset + copy;
    return (int)copy;
}

int ram_truncate(struct vfs_node *node, size_t new_size)
{
    if (new_size != 0) return -ENOTSUP;
    if (node->data) {
        memset(node->data, 0, 1024);
    }
    node->size = 0;
    return 0;
}

static int ext2_rename(struct vfs_node *old_parent, const char *old_name, struct vfs_node *new_parent, const char *new_name)
{
    struct ext2_vfs_data *old_pevd = (struct ext2_vfs_data *)old_parent->data;
    struct ext2_vfs_data *new_pevd = (struct ext2_vfs_data *)new_parent->data;
    if (!old_pevd || !new_pevd || !hda) return -EIO;

    uint32_t old_block = old_pevd->blocks[0];
    if (old_block == 0 || !ext2_is_valid_block(old_block)) return -EIO;

    uint8_t old_dir_buf[1024];
    hda->read(hda, (size_t)old_block * 1024, old_dir_buf, 1024);

    uint32_t offset = 0;
    uint32_t target_inode = 0;
    uint8_t target_type = 0;
    struct ext2_dir_entry_2 *target_de = 0;

    while (offset < 1024) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(old_dir_buf + offset);
        if (de->rec_len < 8) break;
        if (de->inode != 0) {
            size_t nlen = de->name_len < 255 ? de->name_len : 255;
            if (nlen == strlen(old_name) && memcmp(de->name, old_name, nlen) == 0) {
                target_inode = de->inode;
                target_type = de->file_type;
                target_de = de;
                break;
            }
        }
        if (offset + de->rec_len >= 1024) break;
        offset += de->rec_len;
    }

    if (target_inode == 0) return -ENOENT;

    // Add entry to new directory
    int r = ext2_add_entry(new_parent, new_name, target_inode, target_type);
    if (r != 0) return r;

    // Remove from old directory (just zero the inode)
    target_de->inode = 0;
    hda->write(hda, (size_t)old_block * 1024, old_dir_buf, 1024);

    if (hda && hda->data) {
        struct block_device *dev = (struct block_device *)hda->data;
        if (dev->flush) dev->flush(dev);
    }
    return 0;
}

void ext2_init(void)
{
    const char *devs[] = { "/dev/hda", "/dev/hdb", "/dev/hdc", "/dev/hdd" };
    for (int i = 0; i < 4; i++) {
        hda = vfs_lookup(devs[i]);
        if (hda == 0) continue;
        uint8_t sb_buf[1024];
        if (hda->read(hda, 1024, sb_buf, 1024) < 0) continue;
        struct ext2_superblock *sb = (struct ext2_superblock *)sb_buf;
        if (sb->s_magic == EXT2_SUPER_MAGIC) {
            current_sb = *sb;
            printk("ext2: valid magic found on %s, mounting /persist\n", devs[i]);
            goto found;
        }
    }
    printk("ext2: no valid ext2 filesystem found on hdX devices, using memory fallback\n");
    vfs_create_file("/persist", VFS_TYPE_DIR, 0, 0);
    vfs_create_file("/persist/hello.txt", VFS_TYPE_FILE, 22, 0);
    struct vfs_node *n = vfs_lookup("/persist/hello.txt");
    if (n) {
        n->data = kmalloc(1024);
        memset(n->data, 0, 1024);
        const char *msg = "Hello from EXT2 disk!\n";
        memcpy(n->data, msg, 22);
        n->read = ram_read;
        n->write = ram_write;
        n->truncate = ram_truncate;
    }
    return;
found:
    vfs_create_file("/persist", VFS_TYPE_DIR, 0, 0);
    struct vfs_node *ext2_root = vfs_lookup("/persist");
    uint8_t gd_buf[1024];
    hda->read(hda, 2048, gd_buf, 1024);
    struct ext2_group_desc *gd = (struct ext2_group_desc *)gd_buf;
    uint8_t it_buf[8192];
    hda->read(hda, (size_t)gd->bg_inode_table * 1024, it_buf, 8192);
    struct ext2_inode *inodes = (struct ext2_inode *)it_buf;
    struct ext2_inode *root_in = &inodes[2 - 1];
    uint32_t root_block = root_in->i_block[0];
    if (ext2_root) {
        struct ext2_vfs_data *revd = kzalloc(sizeof(struct ext2_vfs_data));
        if (revd) {
            revd->inode_num = 2;
            revd->blocks[0] = root_block;
            ext2_root->data = revd;
        }
        ext2_root->readdir = ext2_readdir;
    }
    if (root_block == 0) return;
    uint8_t dir_buf[1024];
    hda->read(hda, (size_t)root_block * 1024, dir_buf, 1024);
    uint32_t offset = 0;
    while (offset < 1024) {
        struct ext2_dir_entry_2 *de = (struct ext2_dir_entry_2 *)(dir_buf + offset);
        if (de->rec_len < 8) break;
        if (de->inode != 0 && de->name_len > 0) {
            char name[256];
            size_t nlen = de->name_len < 255 ? de->name_len : 255;
            memcpy(name, de->name, nlen);
            name[nlen] = '\0';
            if (strcmp(name, ".") != 0 && strcmp(name, "..") != 0) {
                if (de->inode <= 64) {
                    struct ext2_inode *in = &inodes[de->inode - 1];
                    char full_path[512];
                    strcpy(full_path, "/persist/");
                    strcat(full_path, name);
                    uint32_t vfs_type = VFS_TYPE_FILE;
                    if (de->file_type == 2) vfs_type = VFS_TYPE_DIR;
                    vfs_create_file(full_path, vfs_type, in->i_size, 0);
                    struct vfs_node *node = vfs_lookup(full_path);
                    if (node != 0) {
                        struct ext2_vfs_data *evd = kmalloc(sizeof(struct ext2_vfs_data));
                        if (evd != 0) {
                            evd->inode_num = de->inode;
                            for (int k = 0; k < 15; k++) evd->blocks[k] = in->i_block[k];
                            node->data = evd;
                            if (vfs_type == VFS_TYPE_FILE) {
                                node->read = ext2_file_read;
                                node->write = ext2_file_write;
                                node->truncate = ext2_truncate;
                            } else {
                                node->readdir = ext2_readdir;
                                node->create = ext2_create;
                                node->mkdir = ext2_mkdir;
                                node->unlink = ext2_unlink;
                                node->rename = ext2_rename;
                            }
                        }
                    }
                    printk("ext2: found /persist/%s (inode %u, size %u)\n", name, de->inode, (uint32_t)in->i_size);
                }
            }
        }
        offset += de->rec_len;
    }

    if (ext2_root) {
        ext2_root->create = ext2_create;
        ext2_root->mkdir = ext2_mkdir;
        ext2_root->unlink = ext2_unlink;
        ext2_root->rename = ext2_rename;
    }
    const char *subdirs[] = { "/persist/bin", "/persist/lib", "/persist/tests" };
    for (int k = 0; k < 3; k++) {
        struct vfs_node *sd = vfs_lookup(subdirs[k]);
        if (sd) {
            sd->create = ext2_create;
            sd->mkdir = ext2_mkdir;
            sd->unlink = ext2_unlink;
            sd->rename = ext2_rename;
        }
    }

    printk("ext2: mounted /persist from %s successfully\n", hda->name);
}
