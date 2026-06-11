/*
 * sys_file.c — file-descriptor syscalls.
 *
 * Implements: read, write, open (with O_TRUNC/O_APPEND), close, lseek,
 *             fstat, stat, getdents64, execve, dup, dup2, fcntl,
 *             ioctl (basic TTY), mkdir, rmdir, unlink, rename.
 */
#include <sys/syscall.h>
#include <sys/syscall_table.h>
#include <mm/uaccess.h>
#include <fs/vfs.h>
#include <sched/task.h>
#include <sched/wait_queue.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <kernel/elf_loader.h>
#include <kernel/printk.h>
#include <drivers/serial.h>
#include <drivers/pit.h>

typedef uint32_t mode_t;

#define PATH_MAX_LEN 256
#define READ_MAX_BUF 4096
#define AT_FDCWD -100
#define AT_EMPTY_PATH 0x1000
#define R_OK 4
#define W_OK 2
#define X_OK 1
#define F_OK 0

#define STATX_TYPE        0x00000001U
#define STATX_MODE        0x00000002U
#define STATX_NLINK       0x00000004U
#define STATX_UID         0x00000008U
#define STATX_GID         0x00000010U
#define STATX_ATIME       0x00000020U
#define STATX_MTIME       0x00000040U
#define STATX_CTIME       0x00000080U
#define STATX_INO         0x00000100U
#define STATX_SIZE        0x00000200U
#define STATX_BLOCKS      0x00000400U
#define STATX_BASIC_STATS 0x000007ffU

struct iovec {
    void *iov_base;
    size_t iov_len;
};

int64_t sys_readv(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    const struct iovec *iov = (const struct iovec *)frame->rsi;
    int iovcnt = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];

    if (f->node == 0 || f->node->read == 0) return -(int64_t)EINVAL;
    if (iovcnt < 0) return -(int64_t)EINVAL;
    if (iovcnt == 0) return 0;
    if (iovcnt > 1024) return -(int64_t)EINVAL;

    if (!uaccess_ok(iov, iovcnt * sizeof(struct iovec))) {
        return -(int64_t)EFAULT;
    }

    struct iovec kiov[32];
    struct iovec *kiov_ptr = kiov;
    if (iovcnt > 32) {
        kiov_ptr = kmalloc(iovcnt * sizeof(struct iovec));
        if (!kiov_ptr) return -(int64_t)ENOMEM;
    }

    if (copy_from_user(kiov_ptr, iov, iovcnt * sizeof(struct iovec)) != 0) {
        if (kiov_ptr != kiov) kfree(kiov_ptr);
        return -(int64_t)EFAULT;
    }

    int64_t total_read = 0;
    for (int i = 0; i < iovcnt; i++) {
        void *buf = kiov_ptr[i].iov_base;
        size_t len = kiov_ptr[i].iov_len;
        if (len == 0) continue;

        if (!uaccess_ok(buf, len)) {
            if (kiov_ptr != kiov) kfree(kiov_ptr);
            return -(int64_t)EFAULT;
        }

        size_t offset = 0;
        while (offset < len) {
            size_t chunk = len - offset;
            if (chunk > READ_MAX_BUF) chunk = READ_MAX_BUF;

            uint8_t kbuf[READ_MAX_BUF];
            if (current_task) current_task->current_file_flags = f->flags;
            int read_bytes = f->node->read(f->node, f->offset, kbuf, chunk);
            if (read_bytes < 0) {
                if (total_read > 0) {
                    if (kiov_ptr != kiov) kfree(kiov_ptr);
                    return total_read;
                }
                if (kiov_ptr != kiov) kfree(kiov_ptr);
                return read_bytes;
            }
            if (read_bytes == 0) {
                break;
            }

            int err = copy_to_user((uint8_t *)buf + offset, kbuf, read_bytes);
            if (err != 0) {
                if (kiov_ptr != kiov) kfree(kiov_ptr);
                return -(int64_t)EFAULT;
            }
            f->offset += read_bytes;
            total_read += read_bytes;
            offset += read_bytes;

            if ((size_t)read_bytes < chunk) {
                break;
            }
        }
    }

    if (kiov_ptr != kiov) kfree(kiov_ptr);
    return total_read;
}

int64_t sys_writev(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    const struct iovec *iov = (const struct iovec *)frame->rsi;
    int iovcnt = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (iovcnt < 0) return -(int64_t)EINVAL;
    if (iovcnt == 0) return 0;
    if (iovcnt > 1024) return -(int64_t)EINVAL;

    if (!uaccess_ok(iov, iovcnt * sizeof(struct iovec))) {
        return -(int64_t)EFAULT;
    }

    struct iovec kiov[32];
    struct iovec *kiov_ptr = kiov;
    if (iovcnt > 32) {
        kiov_ptr = kmalloc(iovcnt * sizeof(struct iovec));
        if (!kiov_ptr) return -(int64_t)ENOMEM;
    }

    if (copy_from_user(kiov_ptr, iov, iovcnt * sizeof(struct iovec)) != 0) {
        if (kiov_ptr != kiov) kfree(kiov_ptr);
        return -(int64_t)EFAULT;
    }

    struct file *f = 0;
    if (fd >= 0 && fd < MAX_FILES_PER_PROCESS) {
        f = proc->files[fd];
    }

    if (f) {
        if (f->node == 0 || f->node->write == 0) {
            if (kiov_ptr != kiov) kfree(kiov_ptr);
            return -(int64_t)EBADF;
        }
    } else {
        if (fd != 1 && fd != 2) {
            if (kiov_ptr != kiov) kfree(kiov_ptr);
            return -(int64_t)EBADF;
        }
    }

    int64_t total_written = 0;
    for (int i = 0; i < iovcnt; i++) {
        const void *buf = kiov_ptr[i].iov_base;
        size_t len = kiov_ptr[i].iov_len;
        if (len == 0) continue;

        if (!uaccess_ok(buf, len)) {
            if (kiov_ptr != kiov) kfree(kiov_ptr);
            return -(int64_t)EFAULT;
        }

        size_t offset = 0;
        while (offset < len) {
            size_t chunk = len - offset;
            if (chunk > READ_MAX_BUF) chunk = READ_MAX_BUF;

            uint8_t kbuf[READ_MAX_BUF];
            if (copy_from_user(kbuf, (const uint8_t *)buf + offset, chunk) != 0) {
                if (kiov_ptr != kiov) kfree(kiov_ptr);
                return -(int64_t)EFAULT;
            }

            int written = 0;
            if (f) {
                written = f->node->write(f->node, f->offset, kbuf, chunk);
                if (written > 0) {
                    f->offset += written;
                }
            } else {
                for (size_t c = 0; c < chunk; c++) {
                    serial_write_char((char)kbuf[c]);
                }
                written = (int)chunk;
            }

            if (written < 0) {
                if (total_written > 0) {
                    if (kiov_ptr != kiov) kfree(kiov_ptr);
                    return total_written;
                }
                if (kiov_ptr != kiov) kfree(kiov_ptr);
                return written;
            }
            if (written == 0) {
                break;
            }
            total_written += written;
            offset += written;
        }
    }

    if (kiov_ptr != kiov) kfree(kiov_ptr);
    return total_written;
}

static int alloc_fd(struct process *proc)
{
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        if (proc->files[i] == 0) return i;
    }
    return -1;
}

static int alloc_fd_above(struct process *proc, int min_fd)
{
    for (int i = min_fd; i < MAX_FILES_PER_PROCESS; i++) {
        if (proc->files[i] == 0) return i;
    }
    return -1;
}

static inline size_t align_up(size_t value, size_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static bool valid_access_mode(int mode)
{
    return (mode & ~(R_OK | W_OK | X_OK)) == 0;
}

static bool current_is_root(void)
{
    return current_task && current_task->process && current_task->process->euid == 0;
}

static bool current_owns_node(struct vfs_node *node)
{
    return current_task && current_task->process && node &&
           current_task->process->euid == node->uid;
}

static int node_required_access(int flags, struct vfs_node *node)
{
    int access_mode = 0;
    switch (flags & 3) {
    case O_WRONLY: access_mode |= VFS_ACCESS_WRITE; break;
    case O_RDWR:   access_mode |= VFS_ACCESS_READ | VFS_ACCESS_WRITE; break;
    default:       access_mode |= VFS_ACCESS_READ; break;
    }
    if ((flags & O_TRUNC) && node && node->type == VFS_TYPE_FILE) {
        access_mode |= VFS_ACCESS_WRITE;
    }
    if ((flags & O_DIRECTORY) && node && node->type == VFS_TYPE_DIR) {
        access_mode |= VFS_ACCESS_EXEC;
    }
    return access_mode;
}

static int parent_path_from_absolute(const char *path, char *parent_out, size_t parent_sz)
{
    size_t len = strlen(path);
    if (len == 0 || path[0] != '/' || len >= parent_sz) return -EINVAL;
    if (len == 1) {
        strcpy(parent_out, "/");
        return 0;
    }
    size_t last = len - 1;
    while (last > 0 && path[last] == '/') last--;
    while (last > 0 && path[last] != '/') last--;
    if (last == 0) {
        strcpy(parent_out, "/");
        return 0;
    }
    memcpy(parent_out, path, last);
    parent_out[last] = '\0';
    return 0;
}

static struct vfs_node *resolve_dirfd_ex(int dirfd, const char *kpath, bool follow_last_symlink, int flags, int *err_out)
{
    if (kpath[0] == '\0' && (flags & AT_EMPTY_PATH)) {
        if (dirfd == AT_FDCWD) {
            return vfs_lookup(current_task->process->cwd);
        }
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS) {
            *err_out = -EBADF;
            return 0;
        }
        struct file *f = current_task->process->files[dirfd];
        if (f == 0) {
            *err_out = -EBADF;
            return 0;
        }
        return f->node;
    }

    if (kpath[0] == '/') {
        return vfs_resolve_path_at(vfs_get_root(), kpath, follow_last_symlink, err_out);
    }
    if (dirfd == AT_FDCWD) {
        struct vfs_node *cwd_node = vfs_lookup(current_task->process->cwd);
        return vfs_resolve_path_at(cwd_node, kpath, follow_last_symlink, err_out);
    }
    if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS) {
        *err_out = -EBADF;
        return 0;
    }
    struct file *f = current_task->process->files[dirfd];
    if (f == 0) {
        *err_out = -EBADF;
        return 0;
    }
    if (f->node->type != VFS_TYPE_DIR) {
        *err_out = -ENOTDIR;
        return 0;
    }
    return vfs_resolve_path_at(f->node, kpath, follow_last_symlink, err_out);
}

static int vfs_get_node_path(struct vfs_node *node, char *buf, size_t bufsz)
{
    if (node == 0) return -1;
    if (node->parent == 0) {
        if (bufsz < 2) return -1;
        buf[0] = '/';
        buf[1] = '\0';
        return 0;
    }

    char temp[512];
    temp[0] = '\0';

    struct vfs_node *curr = node;
    while (curr != 0 && curr->parent != 0) {
        char part[256];
        strcpy(part, "/");
        strcat(part, curr->name);

        char next_temp[512];
        strcpy(next_temp, part);
        strcat(next_temp, temp);
        strcpy(temp, next_temp);

        curr = curr->parent;
    }

    if (strlen(temp) >= bufsz) return -1;
    strcpy(buf, temp);
    return 0;
}

static int get_absolute_path_at(int dirfd, const char *kpath, char *out_path, size_t out_sz)
{
    if (kpath[0] == '/') {
        if (strlen(kpath) >= out_sz) return -ENAMETOOLONG;
        strcpy(out_path, kpath);
        return 0;
    }

    struct vfs_node *start_node = 0;
    if (dirfd == AT_FDCWD) {
        start_node = vfs_lookup(current_task->process->cwd);
    } else {
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS) return -EBADF;
        struct file *f = current_task->process->files[dirfd];
        if (f == 0) return -EBADF;
        if (f->node->type != VFS_TYPE_DIR) return -ENOTDIR;
        start_node = f->node;
    }

    char start_path[512];
    if (vfs_get_node_path(start_node, start_path, sizeof(start_path)) != 0) {
        return -ENAMETOOLONG;
    }

    size_t slen = strlen(start_path);
    size_t plen = strlen(kpath);
    if (slen + 1 + plen >= out_sz) return -ENAMETOOLONG;

    strcpy(out_path, start_path);
    if (out_path[slen - 1] != '/') {
        strcat(out_path, "/");
    }
    strcat(out_path, kpath);
    return 0;
}

static int64_t open_copied_path_at(int dirfd, const char *kpath, int flags)
{
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    int fd = alloc_fd(proc);
    if (fd < 0) return -(int64_t)EMFILE;

    int err = 0;
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, !(flags & O_NOFOLLOW), flags, &err);
    if (node == 0) {
        if (flags & O_CREAT) {
            char abspath[512];
            int get_err = get_absolute_path_at(dirfd, kpath, abspath, sizeof(abspath));
            if (get_err < 0) return get_err;

            char cleanpath[512];
            vfs_canonicalize_path(abspath, cleanpath);

            char parent_path[512];
            if (parent_path_from_absolute(cleanpath, parent_path, sizeof(parent_path)) != 0)
                return -(int64_t)EINVAL;
            struct vfs_node *parent = vfs_lookup(parent_path);
            if (parent == 0) return -(int64_t)ENOENT;
            int perm = vfs_check_permission(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
            if (perm != 0) return perm;

            node = vfs_create_file(cleanpath, VFS_TYPE_FILE, 0, 0);
            if (node == 0) return -(int64_t)ENOMEM;
            node->mode = S_IFREG | ((0666 & ~proc->umask) & 07777);
            node->uid = proc->euid;
            node->gid = proc->egid;
        } else {
            return err;
        }
    }

    int access = node_required_access(flags, node);
    int perm = vfs_check_permission(node, access);
    if (perm != 0) return perm;

    if ((flags & O_DIRECTORY) && node->type != VFS_TYPE_DIR) {
        return -(int64_t)ENOTDIR;
    }

    if ((flags & O_TRUNC) && node->type == VFS_TYPE_FILE) {
        vfs_truncate(node, 0);
    }

    struct file *f = kzalloc(sizeof(struct file));
    if (f == 0) return -(int64_t)ENOMEM;
    f->node      = node;
    f->offset    = 0;
    f->flags     = flags;
    f->fd_flags  = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
    f->ref_count = 1;

    proc->files[fd] = f;
    return fd;
}

int64_t sys_open(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;
    int flags = (int)frame->rsi;

    char kpath[PATH_MAX_LEN];
    int err = copy_string_from_user(kpath, pathname, PATH_MAX_LEN);
    if (err != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    return open_copied_path_at(AT_FDCWD, kpath, flags);
}

int64_t sys_openat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int flags = (int)frame->rdx;

    char kpath[PATH_MAX_LEN];
    int err = copy_string_from_user(kpath, pathname, PATH_MAX_LEN);
    if (err != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    return open_copied_path_at(dirfd, kpath, flags);
}


/* ------------------------------------------------------------------ */
/* close                                                                */
/* ------------------------------------------------------------------ */

int64_t sys_close(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;

    struct file *f = proc->files[fd];
    proc->files[fd] = 0;
    file_close(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* read                                                                 */
/* ------------------------------------------------------------------ */

int64_t sys_read(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    void *buf = (void *)frame->rsi;
    size_t count = (size_t)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];

    if (f->node == 0 || f->node->read == 0) return -(int64_t)EINVAL;
    if (count == 0) return 0;
    if (count > READ_MAX_BUF) count = READ_MAX_BUF;

    uint8_t kbuf[READ_MAX_BUF];
    if (current_task) current_task->current_file_flags = f->flags;
    int read_bytes = f->node->read(f->node, f->offset, kbuf, count);
    if (read_bytes < 0) return read_bytes;

    if (read_bytes > 0) {
        int err = copy_to_user(buf, kbuf, read_bytes);
        if (err != 0) return -(int64_t)EFAULT;
        f->offset += read_bytes;
    }
    return read_bytes;
}

/* ------------------------------------------------------------------ */
/* write (moved here from sys_write.c, with O_APPEND support)         */
/* ------------------------------------------------------------------ */

int64_t sys_write(struct syscall_frame *frame)
{
    int fd = (int)(int64_t)frame->rdi;
    const void *buf = (const void *)frame->rsi;
    size_t count = (size_t)frame->rdx;

    if (count == 0) return 0;
    if (count > READ_MAX_BUF) count = READ_MAX_BUF;

    if (current_task && current_task->process &&
        fd >= 0 && fd < MAX_FILES_PER_PROCESS &&
        current_task->process->files[fd] != 0) {

        struct file *f = current_task->process->files[fd];
        if (f->node == 0 || f->node->write == 0) return -(int64_t)EBADF;

        uint8_t kbuf[READ_MAX_BUF];
        int err = copy_from_user(kbuf, buf, count);
        if (err != 0) return -(int64_t)EFAULT;

        /* O_APPEND: seek to end before writing */
        if (f->flags & O_APPEND) {
            f->offset = f->node->size;
        }

        if (current_task) current_task->current_file_flags = f->flags;
        int written = f->node->write(f->node, f->offset, kbuf, count);
        if (written > 0) f->offset += written;
        return (int64_t)written;
    }

    /* Fallback stdout (1) and stderr (2) direct output */
    if (fd == 1 || fd == 2) {
        uint8_t kbuf[READ_MAX_BUF];
        int err = copy_from_user(kbuf, buf, count);
        if (err != 0) return -(int64_t)EFAULT;

        for (size_t i = 0; i < count; i++) {
            serial_write_char((char)kbuf[i]);
        }
        return (int64_t)count;
    }

    /* No open fd — return EBADF (not a silent serial fallback) */
    return -(int64_t)EBADF;
}

/* ------------------------------------------------------------------ */
/* lseek                                                                */
/* ------------------------------------------------------------------ */

int64_t sys_lseek(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    off_t offset = (off_t)frame->rsi;
    int whence = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];
    if (f->node == 0) return -(int64_t)EBADF;

    off_t new_offset;
    if      (whence == SEEK_SET) new_offset = offset;
    else if (whence == SEEK_CUR) new_offset = (off_t)f->offset + offset;
    else if (whence == SEEK_END) new_offset = (off_t)f->node->size + offset;
    else return -(int64_t)EINVAL;

    if (new_offset < 0) return -(int64_t)EINVAL;
    f->offset = (size_t)new_offset;
    return new_offset;
}

/* ------------------------------------------------------------------ */
/* fstat                                                                */
/* ------------------------------------------------------------------ */

static void fill_stat(struct stat *ks, struct vfs_node *node)
{
    memset(ks, 0, sizeof(*ks));
    ks->st_ino    = node->inode_num;
    ks->st_size   = node->size;
    ks->st_nlink  = 1;
    ks->st_uid    = node->uid;
    ks->st_gid    = node->gid;
    ks->st_blksize = 4096;
    ks->st_blocks  = (node->size + 511) / 512;
    ks->st_atime   = node->atime;
    ks->st_mtime   = node->mtime;
    ks->st_ctime   = node->ctime;

    if (node->mode != 0) {
        ks->st_mode = node->mode;
    } else if (node->type == VFS_TYPE_DIR) {
        ks->st_mode = S_IFDIR | 0755;
    } else if (node->type == VFS_TYPE_CHAR) {
        ks->st_mode = S_IFCHR | 0666;
    } else {
        ks->st_mode = S_IFREG | 0644;
    }
}

int64_t sys_fstat(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    struct stat *statbuf = (struct stat *)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];
    if (f->node == 0) return -(int64_t)EBADF;

    struct stat ks;
    fill_stat(&ks, f->node);

    if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
    return 0;
}

/* ------------------------------------------------------------------ */
/* stat (path-based)                                                    */
/* ------------------------------------------------------------------ */

int64_t sys_stat(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;
    struct stat *statbuf = (struct stat *)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    struct vfs_node *node = vfs_resolve_path_at(0, kpath, true, &err);
    if (node == 0) return err;

    struct stat ks;
    fill_stat(&ks, node);

    if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
    return 0;
}

int64_t sys_lstat(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;
    struct stat *statbuf = (struct stat *)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    struct vfs_node *node = vfs_resolve_path_at(0, kpath, false, &err);
    if (node == 0) return err;

    struct stat ks;
    fill_stat(&ks, node);

    if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
    return 0;
}

int64_t sys_faccessat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int mode = (int)frame->rdx;
    int flags = (int)frame->r10;

    if (!valid_access_mode(mode)) return -(int64_t)EINVAL;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    bool follow = !(flags & 0x100); /* AT_SYMLINK_NOFOLLOW */
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, follow, flags, &err);
    if (node == 0) return err;

    if (mode == F_OK) return 0;
    int access = 0;
    if (mode & R_OK) access |= VFS_ACCESS_READ;
    if (mode & W_OK) access |= VFS_ACCESS_WRITE;
    if (mode & X_OK) access |= VFS_ACCESS_EXEC;
    return vfs_check_permission(node, access);
}

int64_t sys_access(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    f.rsi = frame->rdi;
    f.rdx = frame->rsi;
    f.r10 = 0;
    return sys_faccessat(&f);
}

int64_t sys_readlinkat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    char *buf = (char *)frame->rdx;
    size_t bufsiz = (size_t)frame->r10;

    if (bufsiz == 0) return -(int64_t)EINVAL;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, false, 0, &err);
    if (node == 0) return err;
    if (node->type != VFS_TYPE_LINK) return -(int64_t)EINVAL;

    const char *target = (const char *)node->data;
    if (target == 0) return -(int64_t)EINVAL;

    size_t len = strlen(target);
    size_t copy_len = len < bufsiz ? len : bufsiz;

    if (copy_to_user(buf, target, copy_len) != 0) return -(int64_t)EFAULT;
    return (int64_t)copy_len;
}

int64_t sys_readlink(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r10 = frame->rdx;
    f.rdx = frame->rsi;
    f.rsi = frame->rdi;
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_readlinkat(&f);
}

struct statx_timestamp {
    int64_t tv_sec;
    uint32_t tv_nsec;
    int32_t __reserved;
};

struct statx_linux {
    uint32_t stx_mask;
    uint32_t stx_blksize;
    uint64_t stx_attributes;
    uint32_t stx_nlink;
    uint32_t stx_uid;
    uint32_t stx_gid;
    uint16_t stx_mode;
    uint16_t __spare0[1];
    uint64_t stx_ino;
    uint64_t stx_size;
    uint64_t stx_blocks;
    uint64_t stx_attributes_mask;
    struct statx_timestamp stx_atime;
    struct statx_timestamp stx_btime;
    struct statx_timestamp stx_ctime;
    struct statx_timestamp stx_mtime;
    uint32_t stx_rdev_major;
    uint32_t stx_rdev_minor;
    uint32_t stx_dev_major;
    uint32_t stx_dev_minor;
    uint64_t __spare2[14];
};

static void fill_statx(struct statx_linux *sx, struct vfs_node *node)
{
    struct stat st;
    fill_stat(&st, node);
    memset(sx, 0, sizeof(*sx));
    sx->stx_mask = STATX_BASIC_STATS;
    sx->stx_blksize = (uint32_t)st.st_blksize;
    sx->stx_nlink = st.st_nlink;
    sx->stx_uid = st.st_uid;
    sx->stx_gid = st.st_gid;
    sx->stx_mode = (uint16_t)st.st_mode;
    sx->stx_ino = st.st_ino;
    sx->stx_size = (uint64_t)st.st_size;
    sx->stx_blocks = (uint64_t)st.st_blocks;
    sx->stx_atime.tv_sec = st.st_atime;
    sx->stx_mtime.tv_sec = st.st_mtime;
    sx->stx_ctime.tv_sec = st.st_ctime;
}

int64_t sys_statx(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int flags = (int)frame->rdx;
    struct statx_linux *statxbuf = (struct statx_linux *)frame->r8;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    if (statxbuf == 0) return -(int64_t)EFAULT;

    char kpath[PATH_MAX_LEN];
    if (pathname != 0) {
        if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
        kpath[PATH_MAX_LEN - 1] = '\0';
    } else {
        kpath[0] = '\0';
    }

    int err = 0;
    bool follow = !(flags & 0x100); /* AT_SYMLINK_NOFOLLOW */
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, follow, flags, &err);
    if (node == 0) return err;

    struct statx_linux sx;
    fill_statx(&sx, node);
    if (copy_to_user(statxbuf, &sx, sizeof(sx)) != 0) return -(int64_t)EFAULT;
    return 0;
}

int64_t sys_newfstatat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    struct stat *statbuf = (struct stat *)frame->rdx;
    int flags = (int)frame->r10;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (pathname != 0) {
        if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
        kpath[PATH_MAX_LEN - 1] = '\0';
    } else {
        kpath[0] = '\0';
    }

    int err = 0;
    bool follow = !(flags & 0x100); /* AT_SYMLINK_NOFOLLOW */
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, follow, flags, &err);
    if (node == 0) return err;

    struct stat ks;
    fill_stat(&ks, node);

    if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
    return 0;
}

/* ------------------------------------------------------------------ */
/* getdents64                                                           */
/* ------------------------------------------------------------------ */

int64_t sys_getdents64(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    struct linux_dirent64 *dirp = (struct linux_dirent64 *)frame->rsi;
    unsigned int count = (unsigned int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];
    if (f->node == 0 || f->node->type != VFS_TYPE_DIR || f->node->readdir == 0)
        return -(int64_t)ENOTDIR;

    uint8_t *kbuf = kzalloc(count);
    if (kbuf == 0) return -(int64_t)ENOMEM;

    size_t bytes_written = 0;
    struct vfs_dirent vd;

    while (bytes_written < count) {
        int ret = f->node->readdir(f->node, f->offset, &vd);
        if (ret <= 0) break;

        size_t name_len = strlen(vd.name);
        size_t entry_size = align_up(sizeof(struct linux_dirent64) + name_len + 1, 8);
        if (bytes_written + entry_size > count) {
            if (bytes_written == 0) { kfree(kbuf); return -(int64_t)EINVAL; }
            break;
        }

        struct linux_dirent64 *entry = (struct linux_dirent64 *)(kbuf + bytes_written);
        entry->d_ino    = vd.inode_num;
        entry->d_off    = f->offset + 1;
        entry->d_reclen = (uint16_t)entry_size;
        entry->d_type   = (vd.type == VFS_TYPE_DIR) ? DT_DIR :
                          (vd.type == VFS_TYPE_CHAR) ? DT_CHR : DT_REG;
        memcpy(entry->d_name, vd.name, name_len + 1);
        bytes_written += entry_size;
        f->offset++;
    }

    if (bytes_written > 0) {
        if (copy_to_user(dirp, kbuf, bytes_written) != 0) {
            kfree(kbuf);
            return -(int64_t)EFAULT;
        }
    }

    kfree(kbuf);
    return bytes_written;
}

/* ------------------------------------------------------------------ */
/* dup / dup2                                                           */
/* ------------------------------------------------------------------ */

int64_t sys_dup(struct syscall_frame *frame)
{
    int oldfd = (int)frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS || proc->files[oldfd] == 0)
        return -(int64_t)EBADF;

    int newfd = alloc_fd(proc);
    if (newfd < 0) return -(int64_t)EMFILE;

    proc->files[newfd] = proc->files[oldfd];
    proc->files[newfd]->ref_count++;
    /* New fd does NOT inherit FD_CLOEXEC */
    /* We share the same file struct — flags adjusted below if needed */
    return newfd;
}

int64_t sys_dup2(struct syscall_frame *frame)
{
    int oldfd = (int)frame->rdi;
    int newfd = (int)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS || proc->files[oldfd] == 0)
        return -(int64_t)EBADF;
    if (newfd < 0 || newfd >= MAX_FILES_PER_PROCESS)
        return -(int64_t)EBADF;

    if (oldfd == newfd) return newfd;

    /* Close newfd if already open */
    if (proc->files[newfd] != 0) {
        struct file *old = proc->files[newfd];
        proc->files[newfd] = 0;
        file_close(old);
    }

    proc->files[newfd] = proc->files[oldfd];
    proc->files[newfd]->ref_count++;
    return newfd;
}

/* ------------------------------------------------------------------ */
/* fcntl                                                                */
/* ------------------------------------------------------------------ */

int64_t sys_fcntl(struct syscall_frame *frame)
{
    int fd  = (int)frame->rdi;
    int cmd = (int)frame->rsi;
    int arg = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];

    switch (cmd) {
    case F_GETFD: return f->fd_flags;
    case F_SETFD: f->fd_flags = arg; return 0;
    case F_GETFL: return f->flags;
    case F_SETFL:
        /* Only allow changing O_APPEND and O_NONBLOCK */
        f->flags = (f->flags & ~(O_APPEND | O_NONBLOCK)) | (arg & (O_APPEND | O_NONBLOCK));
        return 0;
    case F_DUPFD:
    case F_DUPFD + 0x400: { /* F_DUPFD_CLOEXEC */
        int min_fd = (arg >= 0) ? arg : 0;
        int newfd = alloc_fd_above(proc, min_fd);
        if (newfd < 0) return -(int64_t)EMFILE;
        proc->files[newfd] = f;
        f->ref_count++;
        if (cmd != F_DUPFD) proc->files[newfd]->fd_flags |= FD_CLOEXEC;
        return newfd;
    }
    default:
        return -(int64_t)EINVAL;
    }
}

/* ------------------------------------------------------------------ */
/* ioctl — basic TTY ioctls                                            */
/* ------------------------------------------------------------------ */

/* Linux ioctl numbers for TTY */
#define TCGETS       0x5401
#define TCSETS       0x5402
#define TCSETSW      0x5403
#define TCSETSF      0x5404
#define TIOCGPGRP    0x540F
#define TIOCSPGRP    0x5410
#define TIOCGWINSZ   0x5413
#define TIOCSWINSZ   0x5414
#define TIOCGSERIAL  0x541E

struct winsize {
    uint16_t ws_row;
    uint16_t ws_col;
    uint16_t ws_xpixel;
    uint16_t ws_ypixel;
};

/* Linux termios — just enough to not crash */
struct termios {
    uint32_t c_iflag;
    uint32_t c_oflag;
    uint32_t c_cflag;
    uint32_t c_lflag;
    uint8_t  c_line;
    uint8_t  c_cc[19];
};

int64_t sys_ioctl(struct syscall_frame *frame)
{
    int fd      = (int)frame->rdi;
    uint64_t req = (uint64_t)frame->rsi;
    void *argp  = (void *)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;

    struct file *f = proc->files[fd];
    if (f->node != 0 && f->node->type == VFS_TYPE_CHAR &&
        (strcmp(f->node->name, "tty") == 0 || strcmp(f->node->name, "console") == 0)) {
        extern int tty_ioctl(uint64_t req, void *argp);
        return tty_ioctl(req, argp);
    }

    return -(int64_t)25; /* ENOTTY */
}

/* ------------------------------------------------------------------ */
/* mkdir / rmdir / unlink / rename                                     */
/* ------------------------------------------------------------------ */

int64_t sys_mkdirat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    mode_t mode = (mode_t)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    char abspath[PATH_MAX_LEN];
    int err = get_absolute_path_at(dirfd, kpath, abspath, sizeof(abspath));
    if (err < 0) return err;

    char cleanpath[PATH_MAX_LEN];
    vfs_canonicalize_path(abspath, cleanpath);

    char parent_path[PATH_MAX_LEN];
    if (parent_path_from_absolute(cleanpath, parent_path, sizeof(parent_path)) != 0)
        return -(int64_t)EINVAL;
    struct vfs_node *parent = vfs_lookup(parent_path);
    if (parent == 0) return -(int64_t)ENOENT;
    int perm = vfs_check_permission(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
    if (perm != 0) return perm;

    /* Check it doesn't already exist */
    int lookup_err = 0;
    struct vfs_node *existing = vfs_resolve_path_at(vfs_get_root(), cleanpath, false, &lookup_err);
    if (existing != 0) return -(int64_t)EEXIST;

    int r = vfs_mkdir(cleanpath, S_IFDIR | ((mode & ~proc->umask) & 07777));
    return r;
}

int64_t sys_mkdir(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rdx = frame->rsi; /* mode */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_mkdirat(&f);
}

int64_t sys_unlinkat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int flags = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    char abspath[PATH_MAX_LEN];
    int err = get_absolute_path_at(dirfd, kpath, abspath, sizeof(abspath));
    if (err < 0) return err;

    char cleanpath[PATH_MAX_LEN];
    vfs_canonicalize_path(abspath, cleanpath);

    int lookup_err = 0;
    struct vfs_node *node = vfs_resolve_path_at(vfs_get_root(), cleanpath, false, &lookup_err);
    if (node == 0) return -(int64_t)ENOENT;

    char parent_path[PATH_MAX_LEN];
    if (parent_path_from_absolute(cleanpath, parent_path, sizeof(parent_path)) != 0)
        return -(int64_t)EINVAL;
    struct vfs_node *parent = vfs_lookup(parent_path);
    if (parent == 0) return -(int64_t)ENOENT;
    int perm = vfs_check_permission(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
    if (perm != 0) return perm;

    if (flags & 0x200) { /* AT_REMOVEDIR */
        if (node->type != VFS_TYPE_DIR) return -(int64_t)ENOTDIR;
    } else {
        if (node->type == VFS_TYPE_DIR) return -(int64_t)EISDIR;
    }

    int ret = vfs_unlink(cleanpath);
    if (ret == -ENOENT)    return -(int64_t)ENOENT;
    if (ret == -ENOTEMPTY) return -(int64_t)ENOTEMPTY;
    if (ret != 0)          return -(int64_t)EINVAL;
    return 0;
}

int64_t sys_unlink(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rdx = 0; /* flags = 0 */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_unlinkat(&f);
}

int64_t sys_rmdir(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rdx = 0x200; /* AT_REMOVEDIR */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_unlinkat(&f);
}

int64_t sys_renameat2(struct syscall_frame *frame)
{
    int olddirfd = (int)frame->rdi;
    const char *oldpath = (const char *)frame->rsi;
    int newdirfd = (int)frame->rdx;
    const char *newpath = (const char *)frame->r10;
    unsigned int flags = (unsigned int)frame->r8;

    if (flags != 0) return -(int64_t)EINVAL;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kold[PATH_MAX_LEN], knew[PATH_MAX_LEN];
    if (copy_string_from_user(kold, oldpath, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    if (copy_string_from_user(knew, newpath, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kold[PATH_MAX_LEN - 1] = '\0';
    knew[PATH_MAX_LEN - 1] = '\0';

    char old_abspath[PATH_MAX_LEN];
    int err = get_absolute_path_at(olddirfd, kold, old_abspath, sizeof(old_abspath));
    if (err < 0) return err;

    char new_abspath[PATH_MAX_LEN];
    err = get_absolute_path_at(newdirfd, knew, new_abspath, sizeof(new_abspath));
    if (err < 0) return err;

    char old_clean[PATH_MAX_LEN], new_clean[PATH_MAX_LEN];
    vfs_canonicalize_path(old_abspath, old_clean);
    vfs_canonicalize_path(new_abspath, new_clean);

    char old_parent_path[PATH_MAX_LEN];
    char new_parent_path[PATH_MAX_LEN];
    if (parent_path_from_absolute(old_clean, old_parent_path, sizeof(old_parent_path)) != 0 ||
        parent_path_from_absolute(new_clean, new_parent_path, sizeof(new_parent_path)) != 0)
        return -(int64_t)EINVAL;
    struct vfs_node *old_parent = vfs_lookup(old_parent_path);
    struct vfs_node *new_parent = vfs_lookup(new_parent_path);
    if (old_parent == 0 || new_parent == 0) return -(int64_t)ENOENT;
    int old_perm = vfs_check_permission(old_parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
    if (old_perm != 0) return old_perm;
    int new_perm = vfs_check_permission(new_parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
    if (new_perm != 0) return new_perm;

    int ret = vfs_rename(old_clean, new_clean);
    if (ret == -ENOENT) return -(int64_t)ENOENT;
    if (ret != 0)       return -(int64_t)EINVAL;
    return 0;
}

int64_t sys_renameat(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r8 = 0; /* flags = 0 */
    return sys_renameat2(&f);
}

int64_t sys_rename(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r8 = 0; /* flags = 0 */
    f.r10 = frame->rsi; /* newpath */
    f.rdx = (uint64_t)(int64_t)AT_FDCWD; /* newdirfd */
    f.rsi = frame->rdi; /* oldpath */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD; /* olddirfd */
    return sys_renameat2(&f);
}

int64_t sys_symlinkat(struct syscall_frame *frame)
{
    const char *target = (const char *)frame->rdi;
    int newdirfd = (int)frame->rsi;
    const char *linkpath = (const char *)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char ktarget[PATH_MAX_LEN];
    if (copy_string_from_user(ktarget, target, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    ktarget[PATH_MAX_LEN - 1] = '\0';

    char klink[PATH_MAX_LEN];
    if (copy_string_from_user(klink, linkpath, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    klink[PATH_MAX_LEN - 1] = '\0';

    char link_abspath[PATH_MAX_LEN];
    int err = get_absolute_path_at(newdirfd, klink, link_abspath, sizeof(link_abspath));
    if (err < 0) return err;

    char link_clean[PATH_MAX_LEN];
    vfs_canonicalize_path(link_abspath, link_clean);

    char parent_path[PATH_MAX_LEN];
    if (parent_path_from_absolute(link_clean, parent_path, sizeof(parent_path)) != 0)
        return -(int64_t)EINVAL;
    struct vfs_node *parent = vfs_lookup(parent_path);
    if (parent == 0) return -(int64_t)ENOENT;
    int perm = vfs_check_permission(parent, VFS_ACCESS_WRITE | VFS_ACCESS_EXEC);
    if (perm != 0) return perm;

    /* Check if link already exists */
    int lookup_err = 0;
    struct vfs_node *existing = vfs_resolve_path_at(vfs_get_root(), link_clean, false, &lookup_err);
    if (existing != 0) return -(int64_t)EEXIST;

    struct vfs_node *node = vfs_create_symlink(ktarget, link_clean);
    if (node == 0) return -(int64_t)ENOMEM;
    node->mode = S_IFLNK | 0777;
    node->uid = current_task->process->euid;
    node->gid = current_task->process->egid;
    return 0;
}

int64_t sys_symlink(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rdx = frame->rsi; /* linkpath */
    f.rsi = (uint64_t)(int64_t)AT_FDCWD; /* newdirfd */
    f.rdi = frame->rdi; /* target */
    return sys_symlinkat(&f);
}

int64_t sys_fchmodat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    mode_t mode = (mode_t)frame->rdx;
    int flags = (int)frame->r10;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    bool follow = !(flags & 0x100); /* AT_SYMLINK_NOFOLLOW */
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, follow, flags, &err);
    if (node == 0) return err;
    if (!current_is_root() && !current_owns_node(node)) return -(int64_t)EPERM;

    node->mode = (node->mode & S_IFMT) | (mode & 07777);
    return 0;
}

int64_t sys_chmod(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r10 = 0; /* flags */
    f.rdx = frame->rsi; /* mode */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_fchmodat(&f);
}

int64_t sys_fchmod(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    mode_t mode = (mode_t)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];
    if (f->node == 0) return -(int64_t)EBADF;
    if (!current_is_root() && !current_owns_node(f->node)) return -(int64_t)EPERM;

    f->node->mode = (f->node->mode & S_IFMT) | (mode & 07777);
    return 0;
}

int64_t sys_chownat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int32_t owner = (int32_t)frame->rdx;
    int32_t group = (int32_t)frame->r10;
    int flags = (int)frame->r8;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, pathname, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    int err = 0;
    bool follow = !(flags & 0x100); /* AT_SYMLINK_NOFOLLOW */
    struct vfs_node *node = resolve_dirfd_ex(dirfd, kpath, follow, flags, &err);
    if (node == 0) return err;
    if (!current_is_root()) return -(int64_t)EPERM;

    if (owner != -1) node->uid = (uint32_t)owner;
    if (group != -1) node->gid = (uint32_t)group;
    return 0;
}

int64_t sys_chown(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r8 = 0; /* flags = 0 */
    f.r10 = frame->rdx; /* group */
    f.rdx = frame->rsi; /* owner */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_chownat(&f);
}

int64_t sys_lchown(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.r8 = 0x100; /* flags = AT_SYMLINK_NOFOLLOW */
    f.r10 = frame->rdx; /* group */
    f.rdx = frame->rsi; /* owner */
    f.rsi = frame->rdi; /* pathname */
    f.rdi = (uint64_t)(int64_t)AT_FDCWD;
    return sys_chownat(&f);
}

int64_t sys_fchown(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    int32_t owner = (int32_t)frame->rsi;
    int32_t group = (int32_t)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return -(int64_t)EBADF;
    struct file *f = proc->files[fd];
    if (f->node == 0) return -(int64_t)EBADF;
    if (!current_is_root()) return -(int64_t)EPERM;

    if (owner != -1) f->node->uid = (uint32_t)owner;
    if (group != -1) f->node->gid = (uint32_t)group;
    return 0;
}

int64_t sys_umask(struct syscall_frame *frame)
{
    mode_t mask = (mode_t)frame->rdi;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    uint32_t old = proc->umask;
    proc->umask = mask & 0777;
    return (int64_t)old;
}

int64_t sys_dup3(struct syscall_frame *frame)
{
    int oldfd = (int)frame->rdi;
    int newfd = (int)frame->rsi;
    int flags = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (oldfd == newfd) return -(int64_t)EINVAL;

    if (oldfd < 0 || oldfd >= MAX_FILES_PER_PROCESS || proc->files[oldfd] == 0)
        return -(int64_t)EBADF;
    if (newfd < 0 || newfd >= MAX_FILES_PER_PROCESS)
        return -(int64_t)EBADF;

    if (flags & ~O_CLOEXEC) return -(int64_t)EINVAL;

    /* Close newfd if already open */
    if (proc->files[newfd] != 0) {
        struct file *old = proc->files[newfd];
        proc->files[newfd] = 0;
        file_close(old);
    }

    struct file *f = proc->files[oldfd];
    proc->files[newfd] = f;
    f->ref_count++;

    if (flags & O_CLOEXEC) {
        proc->files[newfd]->fd_flags |= FD_CLOEXEC;
    } else {
        proc->files[newfd]->fd_flags &= ~FD_CLOEXEC;
    }

    return newfd;
}

/* ------------------------------------------------------------------ */
/* execve                                                              */
/* ------------------------------------------------------------------ */

#define MAX_ARGV 64
#define MAX_ARG_LEN 4096

int64_t sys_execve(struct syscall_frame *frame)
{
    const char *filename = (const char *)frame->rdi;
    char **argv = (char **)frame->rsi;
    char **envp = (char **)frame->rdx;

    char kpath[PATH_MAX_LEN];
    if (copy_string_from_user(kpath, filename, PATH_MAX_LEN) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    /* --- Copy argv --- */
    char *kargv[MAX_ARGV];
    int argc = 0;
    if (argv != 0) {
        for (int i = 0; i < MAX_ARGV; i++) {
            uint64_t uptr;
            if (copy_from_user(&uptr, argv + i, sizeof(uptr)) != 0) {
                goto fail_fault_argv;
            }
            if (uptr == 0) break;
            char *kstr = kmalloc(MAX_ARG_LEN);
            if (kstr == 0) goto fail_argv;
            if (copy_string_from_user(kstr, (const char *)uptr, MAX_ARG_LEN) != 0) {
                kfree(kstr);
                goto fail_fault_argv;
            }
            kargv[argc++] = kstr;
        }
    }
    kargv[argc] = 0;

    /* --- Copy envp --- */
    char *kenvp[MAX_ARGV];
    int envc = 0;
    if (envp != 0) {
        for (int i = 0; i < MAX_ARGV; i++) {
            uint64_t uptr;
            if (copy_from_user(&uptr, envp + i, sizeof(uptr)) != 0) {
                goto fail_fault_envp;
            }
            if (uptr == 0) break;
            char *kstr = kmalloc(MAX_ARG_LEN);
            if (kstr == 0) goto fail_envp;
            if (copy_string_from_user(kstr, (const char *)uptr, MAX_ARG_LEN) != 0) {
                kfree(kstr);
                goto fail_fault_envp;
            }
            kenvp[envc++] = kstr;
        }
    }
    kenvp[envc] = 0;

    /* --- Lookup file --- */
    struct vfs_node *node = vfs_lookup_at(proc->cwd, kpath);
    if (node == 0) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)ENOENT;
    }
    if (node->type != VFS_TYPE_FILE) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)EACCES;
    }
    if (node->data == 0 || node->size == 0) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)ENOEXEC;
    }

    if (vfs_check_permission(node, VFS_ACCESS_EXEC) != 0) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)EACCES;
    }

    /* --- SetUID / SetGID Handling --- */
    if (node->mode & S_ISUID) {
        proc->euid = node->uid;
        proc->suid = node->uid;
    }
    if (node->mode & S_ISGID) {
        proc->egid = node->gid;
        proc->sgid = node->gid;
    }

    /* --- Close FD_CLOEXEC descriptors --- */
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        if (proc->files[i] != 0 && (proc->files[i]->fd_flags & FD_CLOEXEC)) {
            struct file *f = proc->files[i];
            proc->files[i] = 0;
            file_close(f);
        }
    }

    /* --- Load ELF --- */
    void *elf_buf = kmalloc(node->size);
    if (elf_buf == 0) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)ENOMEM;
    }
    int read_bytes = 0;
    if (node->read) {
        read_bytes = node->read(node, 0, elf_buf, node->size);
    } else {
        read_bytes = -EIO;
    }
    if (read_bytes < 0 || (size_t)read_bytes != node->size) {
        kfree(elf_buf);
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return -(int64_t)EIO;
    }

    uint64_t entry = 0, rsp = 0;
    int load_err = elf_load_into_process(proc, elf_buf, node->size,
                                          argc, kargv, kenvp, filename, &entry, &rsp);
    kfree(elf_buf);
    if (load_err != 0) {
        for (int i = 0; i < envc; i++) kfree(kenvp[i]);
        for (int i = 0; i < argc; i++) kfree(kargv[i]);
        return (int64_t)load_err;
    }

    /* Update task for syscall return to new image */
    current_task->user_rip = entry;
    current_task->user_rsp = rsp;
    current_task->cr3 = proc->address_space->pml4_phys;

    frame->rcx     = entry;
    frame->user_rsp = rsp;

    __asm__ volatile("mov %0, %%cr3" :: "r"(proc->address_space->pml4_phys) : "memory");

    for (int i = 0; i < argc; i++) kfree(kargv[i]);
    for (int i = 0; i < envc; i++) kfree(kenvp[i]);
    return 0;

fail_envp:
    for (int i = 0; i < envc; i++) kfree(kenvp[i]);
fail_argv:
    for (int i = 0; i < argc; i++) kfree(kargv[i]);
    return -(int64_t)ENOMEM;

fail_fault_envp:
    for (int i = 0; i < envc; i++) kfree(kenvp[i]);
fail_fault_argv:
    for (int i = 0; i < argc; i++) kfree(kargv[i]);
    return -(int64_t)EFAULT;
}

/* ------------------------------------------------------------------ */
/* Pipes (Phase 21)                                                   */
/* ------------------------------------------------------------------ */
#define PIPE_BUF_SIZE 4096

struct pipe {
    uint8_t buffer[PIPE_BUF_SIZE];
    size_t head;
    size_t tail;
    size_t count;
    struct wait_queue read_wq;
    struct wait_queue write_wq;
    int readers;
    int writers;
};

static int pipe_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)offset;
    struct pipe *p = (struct pipe *)node->data;
    if (p == 0) return -EINVAL;

    bool nonblock = (current_task && (current_task->current_file_flags & O_NONBLOCK));

    while (p->count == 0) {
        if (p->writers == 0) {
            return 0; // EOF
        }
        if (nonblock) {
            return -EAGAIN;
        }
        wait_queue_sleep(&p->read_wq);
    }

    size_t read_bytes = 0;
    uint8_t *ubuf = buf;
    while (count > 0 && p->count > 0) {
        ubuf[read_bytes++] = p->buffer[p->tail];
        p->tail = (p->tail + 1) % PIPE_BUF_SIZE;
        p->count--;
        count--;
    }

    wait_queue_wake_all(&p->write_wq);
    io_event_notify();
    return (int)read_bytes;
}

static int pipe_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)offset;
    struct pipe *p = (struct pipe *)node->data;
    if (p == 0) return -EINVAL;

    if (p->readers == 0) {
        if (current_task) {
            task_send_signal(current_task, 13); // SIGPIPE
        }
        return -EPIPE;
    }

    bool nonblock = (current_task && (current_task->current_file_flags & O_NONBLOCK));

    size_t written_bytes = 0;
    const uint8_t *ubuf = buf;

    while (count > 0) {
        while (p->count == PIPE_BUF_SIZE) {
            if (p->readers == 0) {
                if (written_bytes > 0) return (int)written_bytes;
                if (current_task) {
                    task_send_signal(current_task, 13); // SIGPIPE
                }
                return -EPIPE;
            }
            if (nonblock) {
                if (written_bytes > 0) return (int)written_bytes;
                return -EAGAIN;
            }
            wait_queue_sleep(&p->write_wq);
        }

        size_t space = PIPE_BUF_SIZE - p->count;
        size_t chunk = count < space ? count : space;
        for (size_t i = 0; i < chunk; i++) {
            p->buffer[p->head] = ubuf[written_bytes++];
            p->head = (p->head + 1) % PIPE_BUF_SIZE;
            p->count++;
        }
        count -= chunk;
        wait_queue_wake_all(&p->read_wq);
        io_event_notify();
    }

    return (int)written_bytes;
}

static int pipe_close(struct vfs_node *node, struct file *f)
{
    struct pipe *p = (struct pipe *)node->data;
    if (p == 0) return -EINVAL;

    if ((f->flags & O_WRONLY)) {
        p->writers--;
        if (p->writers == 0) {
            wait_queue_wake_all(&p->read_wq);
            io_event_notify();
        }
    } else {
        p->readers--;
        if (p->readers == 0) {
            wait_queue_wake_all(&p->write_wq);
            io_event_notify();
        }
    }

    if (p->readers == 0 && p->writers == 0) {
        kfree(p);
        kfree(node);
    }
    return 0;
}

static int pipe_poll(struct vfs_node *node, short events, short *revents)
{
    struct pipe *p = (struct pipe *)node->data;
    if (p == 0) return -EINVAL;

    short ready = 0;
    if ((events & 0x0001) && (p->count > 0 || p->writers == 0)) {
        ready |= 0x0001;
    }
    if ((events & 0x0004) && p->readers > 0 && p->count < PIPE_BUF_SIZE) {
        ready |= 0x0004;
    }
    if ((events & 0x0008) && p->writers == 0 && p->count == 0) {
        ready |= 0x0008;
    }
    if ((events & 0x0010) && p->readers == 0) {
        ready |= 0x0010;
    }
    if (revents != 0) {
        *revents = ready;
    }
    return ready != 0 ? 1 : 0;
}

int64_t sys_pipe2(struct syscall_frame *frame)
{
    int *pipefd = (int *)frame->rdi;
    int flags = (int)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    int kfd[2];

    struct pipe *p = kzalloc(sizeof(struct pipe));
    if (p == 0) return -(int64_t)ENOMEM;

    wait_queue_init(&p->read_wq);
    wait_queue_init(&p->write_wq);
    p->readers = 1;
    p->writers = 1;

    struct vfs_node *read_node = kzalloc(sizeof(struct vfs_node));
    if (read_node == 0) {
        kfree(p);
        return -(int64_t)ENOMEM;
    }
    strcpy(read_node->name, "pipe_read");
    read_node->type = VFS_TYPE_FILE;
    read_node->read = pipe_read;
    read_node->close = pipe_close;
    read_node->poll = pipe_poll;
    read_node->data = p;

    struct vfs_node *write_node = kzalloc(sizeof(struct vfs_node));
    if (write_node == 0) {
        kfree(read_node);
        kfree(p);
        return -(int64_t)ENOMEM;
    }
    strcpy(write_node->name, "pipe_write");
    write_node->type = VFS_TYPE_FILE;
    write_node->write = pipe_write;
    write_node->close = pipe_close;
    write_node->poll = pipe_poll;
    write_node->data = p;

    int fd_r = alloc_fd(proc);
    if (fd_r < 0) {
        kfree(write_node);
        kfree(read_node);
        kfree(p);
        return -(int64_t)EMFILE;
    }
    proc->files[fd_r] = kzalloc(sizeof(struct file));
    if (proc->files[fd_r] == 0) {
        kfree(write_node);
        kfree(read_node);
        kfree(p);
        return -(int64_t)ENOMEM;
    }
    proc->files[fd_r]->node = read_node;
    proc->files[fd_r]->ref_count = 1;
    proc->files[fd_r]->flags = O_RDONLY | (flags & O_NONBLOCK);
    if (flags & O_CLOEXEC) proc->files[fd_r]->fd_flags |= FD_CLOEXEC;

    int fd_w = alloc_fd(proc);
    if (fd_w < 0) {
        kfree(proc->files[fd_r]);
        proc->files[fd_r] = 0;
        kfree(write_node);
        kfree(read_node);
        kfree(p);
        return -(int64_t)EMFILE;
    }
    proc->files[fd_w] = kzalloc(sizeof(struct file));
    if (proc->files[fd_w] == 0) {
        kfree(proc->files[fd_r]);
        proc->files[fd_r] = 0;
        kfree(write_node);
        kfree(read_node);
        kfree(p);
        return -(int64_t)ENOMEM;
    }
    proc->files[fd_w]->node = write_node;
    proc->files[fd_w]->ref_count = 1;
    proc->files[fd_w]->flags = O_WRONLY | (flags & O_NONBLOCK);
    if (flags & O_CLOEXEC) proc->files[fd_w]->fd_flags |= FD_CLOEXEC;

    kfd[0] = fd_r;
    kfd[1] = fd_w;

    if (copy_to_user(pipefd, kfd, sizeof(kfd)) != 0) {
        file_close(proc->files[fd_r]);
        proc->files[fd_r] = 0;
        file_close(proc->files[fd_w]);
        proc->files[fd_w] = 0;
        return -(int64_t)EFAULT;
    }

    return 0;
}

int64_t sys_pipe(struct syscall_frame *frame)
{
    struct syscall_frame f = *frame;
    f.rsi = 0;
    return sys_pipe2(&f);
}

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN     0x0001
#define POLLOUT    0x0004
#define POLLERR    0x0008
#define POLLHUP    0x0010
#define POLLNVAL   0x0020

int64_t sys_poll(struct syscall_frame *frame)
{
    struct pollfd *fds = (struct pollfd *)frame->rdi;
    uint64_t nfds = (uint64_t)frame->rsi;
    int timeout = (int)frame->rdx;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    if (nfds == 0) {
        if (timeout > 0) {
            task_sleep_ticks(timeout / 10);
        }
        return 0;
    }

    struct pollfd kfds[32];
    if (nfds > 32) return -(int64_t)EINVAL;

    if (copy_from_user(kfds, fds, nfds * sizeof(struct pollfd)) != 0) {
        return -(int64_t)EFAULT;
    }

    uint64_t start_ticks = pit_ticks();
    uint64_t timeout_ticks = (timeout >= 0) ? (timeout / 10) : 0;

    for (;;) {
        int ready_count = 0;

        for (uint64_t i = 0; i < nfds; i++) {
            kfds[i].revents = 0;
            int fd = kfds[i].fd;
            if (fd < 0) continue;

            if (fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0) {
                kfds[i].revents = POLLNVAL;
                ready_count++;
                continue;
            }

            struct file *f = proc->files[fd];
            struct vfs_node *node = f->node;

            if (node != 0 && node->poll != 0) {
                short revents = 0;
                int poll_rc = node->poll(node, kfds[i].events, &revents);
                if (poll_rc < 0) {
                    kfds[i].revents |= POLLNVAL;
                } else {
                    kfds[i].revents |= revents;
                }
            } else if (node->read == pipe_read) {
                struct pipe *p = (struct pipe *)node->data;
                if (p != 0) {
                    if (p->count > 0 && (kfds[i].events & POLLIN)) {
                        kfds[i].revents |= POLLIN;
                    }
                    if (p->writers == 0) {
                        kfds[i].revents |= POLLHUP;
                    }
                }
            } else if (node->write == pipe_write) {
                struct pipe *p = (struct pipe *)node->data;
                if (p != 0) {
                    if (p->count < PIPE_BUF_SIZE && (kfds[i].events & POLLOUT)) {
                        kfds[i].revents |= POLLOUT;
                    }
                    if (p->readers == 0) {
                        kfds[i].revents |= POLLERR;
                    }
                }
            } else {
                if (kfds[i].events & POLLIN) kfds[i].revents |= POLLIN;
                if (kfds[i].events & POLLOUT) kfds[i].revents |= POLLOUT;
            }

              if (kfds[i].revents != 0) {
                  ready_count++;
              }
          }

          if (ready_count > 0 || timeout == 0) {
              if (copy_to_user(fds, kfds, nfds * sizeof(struct pollfd)) != 0) {
                  return -(int64_t)EFAULT;
              }
              return ready_count;
          }

          if (timeout > 0) {
              uint64_t elapsed = pit_ticks() - start_ticks;
              if (elapsed >= timeout_ticks) {
                  if (copy_to_user(fds, kfds, nfds * sizeof(struct pollfd)) != 0) {
                      return -(int64_t)EFAULT;
                  }
                  return 0;
              }
          }

          task_sleep_ticks(1);
      }
  }

/* ------------------------------------------------------------------ */
/* sys_mount — mount a filesystem                                      */
/* ------------------------------------------------------------------ */
int64_t sys_mount(struct syscall_frame *frame)
{
    /* mount(source, target, fstype, mountflags, data) */
    /* Stub: kernel-internal callers use vfs_mount() directly. */
    (void)frame;
    return 0;
}

/* ------------------------------------------------------------------ */
/* sys_umount2 — unmount a filesystem                                  */
/* ------------------------------------------------------------------ */
int64_t sys_umount2(struct syscall_frame *frame)
{
    (void)frame;
    return 0;
}
