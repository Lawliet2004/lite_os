#ifndef LITENIX_FS_VFS_H
#define LITENIX_FS_VFS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef int64_t off_t;

/* open() flags — Linux ABI values */
#define O_RDONLY    0
#define O_WRONLY    1
#define O_RDWR      2
#define O_CREAT     64      /* 0100 */
#define O_TRUNC     512     /* 0200 */
#define O_APPEND    1024    /* 0400 */
#define O_NONBLOCK  2048    /* 01000 */
#define O_CLOEXEC   524288  /* 02000000 */
#define O_DIRECTORY 65536   /* 0200000 */

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* VFS node types */
#define VFS_TYPE_FILE 1
#define VFS_TYPE_DIR  2
#define VFS_TYPE_CHAR 3
#define VFS_TYPE_LINK 4

/* stat mode bits */
#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFLNK  0120000

#define S_IRUSR 00400
#define S_IWUSR 00200
#define S_IXUSR 00100
#define S_IRGRP 00040
#define S_IWGRP 00020
#define S_IXGRP 00010
#define S_IROTH 00004
#define S_IWOTH 00002
#define S_IXOTH 00001

#define S_IRWXU (S_IRUSR|S_IWUSR|S_IXUSR)
#define S_IRWXG (S_IRGRP|S_IWGRP|S_IXGRP)
#define S_IRWXO (S_IROTH|S_IWOTH|S_IXOTH)

/* getdents64 d_type values */
#define DT_UNKNOWN  0
#define DT_FIFO     1
#define DT_CHR      2
#define DT_DIR      4
#define DT_BLK      6
#define DT_REG      8
#define DT_LNK      10
#define DT_SOCK     12

struct vfs_node;

struct vfs_dirent {
    char name[256];
    uint64_t inode_num;
    uint32_t type;
};

struct stat {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int64_t  st_size;
    int64_t  st_blksize;
    int64_t  st_blocks;
    int64_t  st_atime;
    int64_t  st_mtime;
    int64_t  st_ctime;
};

struct linux_dirent64 {
    uint64_t d_ino;
    int64_t  d_off;
    uint16_t d_reclen;
    uint8_t  d_type;
    char     d_name[];
};

struct file;
typedef int (*vfs_read_t)(struct vfs_node *node, size_t offset, void *buf, size_t count);
typedef int (*vfs_write_t)(struct vfs_node *node, size_t offset, const void *buf, size_t count);
typedef int (*vfs_readdir_t)(struct vfs_node *node, size_t index, struct vfs_dirent *dirent);
typedef int (*vfs_close_t)(struct vfs_node *node, struct file *f);

struct vfs_node {
    char name[128];
    uint32_t type;
    uint32_t mode;          /* permission bits (S_IFREG | 0644 etc.) */
    uint32_t uid;
    uint32_t gid;
    size_t size;
    void *data;
    uint64_t inode_num;
    int64_t  atime;
    int64_t  mtime;
    int64_t  ctime;

    struct vfs_node *parent;
    struct vfs_node *children;
    struct vfs_node *next;

    vfs_read_t read;
    vfs_write_t write;
    vfs_readdir_t readdir;
    vfs_close_t close;
};

struct file {
    struct vfs_node *node;
    size_t offset;
    int flags;          /* open() flags — includes O_APPEND */
    int fd_flags;       /* fcntl FD flags — FD_CLOEXEC */
    int ref_count;
};

/* Core VFS API */
void vfs_init(void);
struct vfs_node *vfs_get_root(void);
void vfs_canonicalize_path(const char *src, char *dst);
struct vfs_node *vfs_lookup(const char *path);
struct vfs_node *vfs_lookup_at(const char *cwd, const char *path);
struct vfs_node *vfs_create_file(const char *path, uint32_t type, size_t size, const void *data);
struct vfs_node *vfs_create_device(const char *path, vfs_read_t read, vfs_write_t write);

/* Directory / file mutation */
int vfs_mkdir(const char *path);
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);
int vfs_truncate(struct vfs_node *node, size_t new_size);
void file_close(struct file *f);

#endif
