#include <fs/initramfs.h>
#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <kernel/printk.h>
#include <kernel/panic.h>

extern uint8_t initramfs_start[];
extern uint8_t initramfs_end[];

struct tar_header {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag[1];
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} __attribute__((packed));

static size_t parse_octal(const char *str, int size)
{
    size_t val = 0;
    int i = 0;
    while (i < size && str[i] == ' ') {
        i++;
    }
    for (; i < size; i++) {
        if (str[i] == '\0' || str[i] == ' ') {
            break;
        }
        if (str[i] >= '0' && str[i] <= '7') {
            val = val * 8 + (str[i] - '0');
        }
    }
    return val;
}

static inline size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

void initramfs_init(void)
{
    printk("VFS: Unpacking initramfs...\n");
    uint8_t *ptr = initramfs_start;
    uint8_t *end = initramfs_end;

    if (ptr == end) {
        printk("VFS: initramfs archive is empty\n");
        return;
    }

    while (ptr + 512 <= end) {
        struct tar_header *header = (struct tar_header *)ptr;

        // A block of all zeros indicates the end of the archive
        bool all_zeros = true;
        for (int i = 0; i < 512; i++) {
            if (ptr[i] != 0) {
                all_zeros = false;
                break;
            }
        }
        if (all_zeros) {
            break;
        }

        // Validate magic (ustar)
        if (memcmp(header->magic, "ustar", 5) != 0) {
            printk("VFS: Invalid tar header magic. Skipping remainder.\n");
            break;
        }

        size_t file_size = parse_octal(header->size, 12);
        char typeflag = header->typeflag[0];

        // Construct full path
        char full_path[512];
        memset(full_path, 0, sizeof(full_path));
        strcpy(full_path, "/");

        // Handle prefix if present
        if (header->prefix[0] != '\0') {
            size_t pfx_len = strlen(header->prefix);
            if (pfx_len > 154) pfx_len = 154;
            char pfx_tmp[156];
            memcpy(pfx_tmp, header->prefix, pfx_len);
            pfx_tmp[pfx_len] = '\0';
            strcpy(full_path + 1, pfx_tmp);
            // Append '/' between prefix and name if prefix doesn't end with it
            if (pfx_tmp[pfx_len - 1] != '/') {
                strcat(full_path, "/");
            }
        }

        // Append file name
        size_t name_len = strlen(header->name);
        if (name_len > 99) name_len = 99;
        char name_tmp[101];
        memcpy(name_tmp, header->name, name_len);
        name_tmp[name_len] = '\0';

        // Remove trailing '/' for path resolution if it's a directory
        if (name_len > 0 && name_tmp[name_len - 1] == '/') {
            name_tmp[name_len - 1] = '\0';
            typeflag = '5'; // Force directory type
        }

        if (header->prefix[0] != '\0') {
            // Strncat/strcat helper is not implemented, but we can do manual copy
            size_t base_len = strlen(full_path);
            strcpy(full_path + base_len, name_tmp);
        } else {
            strcpy(full_path + 1, name_tmp);
        }

        uint32_t type = VFS_TYPE_FILE;
        if (typeflag == '5') {
            type = VFS_TYPE_DIR;
        } else if (typeflag == '2') {
            type = VFS_TYPE_LINK;
        }

        uint8_t *data = ptr + 512;
        size_t size = file_size;
        if (type == VFS_TYPE_LINK) {
            data = (uint8_t *)header->linkname;
            size = strlen(header->linkname) + 1;
        }

        struct vfs_node *node = vfs_create_file(full_path, type, size, data);
        if (node != 0) {
            size_t mode_val = parse_octal(header->mode, 8);
            if (mode_val == 0) {
                mode_val = (type == VFS_TYPE_DIR) ? 0755 : (type == VFS_TYPE_LINK ? 0777 : 0644);
            }
            if (type == VFS_TYPE_FILE && (memcmp(full_path, "/bin/", 5) == 0 || memcmp(full_path, "/tests/", 7) == 0)) {
                mode_val |= 0555; // Ensure readable and executable
            }
            if (type == VFS_TYPE_DIR) {
                node->mode = S_IFDIR | (mode_val & 07777);
            } else if (type == VFS_TYPE_LINK) {
                node->mode = S_IFLNK | (mode_val & 07777);
            } else {
                node->mode = S_IFREG | (mode_val & 07777);
            }
            printk("VFS: mounted %s (type=%s, size=%llu bytes, mode=%x)\n",
                   full_path, type == VFS_TYPE_DIR ? "DIR" : type == VFS_TYPE_LINK ? "LINK" : "FILE", (unsigned long long)size, (unsigned int)node->mode);
        } else {
            printk("VFS: failed to mount %s\n", full_path);
        }

        // Advance pointer: 512 bytes for header + aligned file data blocks
        ptr += 512 + align_up(file_size, 512);
    }
    printk("VFS: initramfs unpacked successfully\n");
}
