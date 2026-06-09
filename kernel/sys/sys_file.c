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

static bool path_is_absolute(const char *path)
{
    return path != 0 && path[0] == '/';
}

static bool valid_access_mode(int mode)
{
    return (mode & ~(R_OK | W_OK | X_OK)) == 0;
}

static int64_t open_copied_path(const char *kpath, int flags)
{
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;
    struct process *proc = current_task->process;

    int fd = alloc_fd(proc);
    if (fd < 0) return -(int64_t)EMFILE;

    struct vfs_node *node = vfs_lookup_at(proc->cwd, kpath);
    if (node == 0) {
        if (flags & O_CREAT) {
            char abspath[PATH_MAX_LEN];
            if (kpath[0] == '/') {
                memcpy(abspath, kpath, strlen(kpath) + 1);
            } else {
                size_t clen = strlen(proc->cwd);
                memcpy(abspath, proc->cwd, clen);
                if (abspath[clen-1] != '/') abspath[clen++] = '/';
                memcpy(abspath + clen, kpath, strlen(kpath) + 1);
            }
            node = vfs_create_file(abspath, VFS_TYPE_FILE, 0, 0);
            if (node == 0) return -(int64_t)ENOMEM;
            node->mode = S_IFREG | 0644;
        } else {
            return -(int64_t)ENOENT;
        }
    }

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

/* ------------------------------------------------------------------ */
/* open                                                                 */
/* ------------------------------------------------------------------ */

int64_t sys_open(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;
    int flags = (int)frame->rsi;
    /* mode (frame->rdx) used for O_CREAT — ignored for now (always 0644) */

    char kpath[PATH_MAX_LEN];
    int err = copy_from_user(kpath, pathname, PATH_MAX_LEN - 1);
    if (err != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    return open_copied_path(kpath, flags);
}

int64_t sys_openat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int flags = (int)frame->rdx;

    char kpath[PATH_MAX_LEN];
    int err = copy_from_user(kpath, pathname, PATH_MAX_LEN - 1);
    if (err != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if (dirfd != AT_FDCWD && !path_is_absolute(kpath)) {
        return -(int64_t)ENOSYS;
    }

    return open_copied_path(kpath, flags);
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
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    struct vfs_node *node = vfs_lookup_at(current_task->process->cwd, kpath);
    if (node == 0) return -(int64_t)ENOENT;

    struct stat ks;
    fill_stat(&ks, node);

    if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
    return 0;
}

/* lstat: same as stat (no symlink support yet) */
int64_t sys_lstat(struct syscall_frame *frame) { return sys_stat(frame); }

int64_t sys_faccessat(struct syscall_frame *frame)
{
    int dirfd = (int)frame->rdi;
    const char *pathname = (const char *)frame->rsi;
    int mode = (int)frame->rdx;

    if (!valid_access_mode(mode)) return -(int64_t)EINVAL;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if (dirfd != AT_FDCWD && !path_is_absolute(kpath)) {
        return -(int64_t)ENOSYS;
    }

    struct vfs_node *node = vfs_lookup_at(current_task->process->cwd, kpath);
    if (node == 0) return -(int64_t)ENOENT;

    if (mode == F_OK) return 0;
    if ((mode & X_OK) && node->type != VFS_TYPE_DIR && (node->mode & 0111) == 0) {
        return -(int64_t)EACCES;
    }
    return 0;
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

    (void)buf;
    if (bufsiz == 0) return -(int64_t)EINVAL;
    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if (dirfd != AT_FDCWD && !path_is_absolute(kpath)) {
        return -(int64_t)ENOSYS;
    }

    struct vfs_node *node = vfs_lookup_at(current_task->process->cwd, kpath);
    if (node == 0) return -(int64_t)ENOENT;
    if (node->type != VFS_TYPE_LINK) return -(int64_t)EINVAL;

    return -(int64_t)ENOSYS;
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

    if ((flags & AT_EMPTY_PATH) && pathname == 0) {
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS || current_task->process->files[dirfd] == 0) {
            return -(int64_t)EBADF;
        }
        struct statx_linux sx;
        fill_statx(&sx, current_task->process->files[dirfd]->node);
        if (copy_to_user(statxbuf, &sx, sizeof(sx)) != 0) return -(int64_t)EFAULT;
        return 0;
    }

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if ((flags & AT_EMPTY_PATH) && kpath[0] == '\0') {
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS || current_task->process->files[dirfd] == 0) {
            return -(int64_t)EBADF;
        }
        struct statx_linux sx;
        fill_statx(&sx, current_task->process->files[dirfd]->node);
        if (copy_to_user(statxbuf, &sx, sizeof(sx)) != 0) return -(int64_t)EFAULT;
        return 0;
    }

    if (dirfd != AT_FDCWD && !path_is_absolute(kpath)) {
        return -(int64_t)ENOSYS;
    }

    struct vfs_node *node = vfs_lookup_at(current_task->process->cwd, kpath);
    if (node == 0) return -(int64_t)ENOENT;

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
    struct process *proc = current_task->process;

    if ((flags & AT_EMPTY_PATH) && pathname == 0) {
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS || proc->files[dirfd] == 0) {
            return -(int64_t)EBADF;
        }
        struct stat ks;
        fill_stat(&ks, proc->files[dirfd]->node);
        if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
        return 0;
    }

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    if ((flags & AT_EMPTY_PATH) && kpath[0] == '\0') {
        if (dirfd < 0 || dirfd >= MAX_FILES_PER_PROCESS || proc->files[dirfd] == 0) {
            return -(int64_t)EBADF;
        }
        struct stat ks;
        fill_stat(&ks, proc->files[dirfd]->node);
        if (copy_to_user(statbuf, &ks, sizeof(ks)) != 0) return -(int64_t)EFAULT;
        return 0;
    }

    if (dirfd != AT_FDCWD && !path_is_absolute(kpath)) {
        return -(int64_t)ENOSYS;
    }

    struct vfs_node *node = vfs_lookup_at(proc->cwd, kpath);
    if (node == 0) return -(int64_t)ENOENT;

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

    switch (req) {
    case TIOCGWINSZ: {
        struct winsize ws = { .ws_row = 24, .ws_col = 80, .ws_xpixel = 0, .ws_ypixel = 0 };
        if (copy_to_user(argp, &ws, sizeof(ws)) != 0) return -(int64_t)EFAULT;
        return 0;
    }
    case TIOCSWINSZ:
        return 0;  /* accept silently */
    case TCGETS: {
        /* Return sane cooked-mode defaults */
        struct termios t;
        memset(&t, 0, sizeof(t));
        t.c_iflag = 0x500;   /* ICRNL | IXON */
        t.c_oflag = 0x5;     /* OPOST | ONLCR */
        t.c_cflag = 0xBF;    /* CS8 | CREAD | HUPCL */
        t.c_lflag = 0x8A3B;  /* ISIG | ICANON | ECHO | ... */
        t.c_cc[4] = 1;       /* VMIN */
        if (copy_to_user(argp, &t, sizeof(t)) != 0) return -(int64_t)EFAULT;
        return 0;
    }
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        return 0;  /* accept silently */
    case TIOCGPGRP: {
        uint64_t pid = current_task->process->pid;
        if (copy_to_user(argp, &pid, sizeof(int)) != 0) return -(int64_t)EFAULT;
        return 0;
    }
    case TIOCSPGRP:
        return 0;
    default:
        return -(int64_t)ENOSYS;
    }
}

/* ------------------------------------------------------------------ */
/* mkdir / rmdir / unlink / rename                                     */
/* ------------------------------------------------------------------ */

int64_t sys_mkdir(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;
    /* mode (frame->rsi) ignored */

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    /* Make absolute */
    char abspath[PATH_MAX_LEN];
    if (kpath[0] == '/') {
        memcpy(abspath, kpath, strlen(kpath) + 1);
    } else {
        const char *cwd = current_task->process->cwd;
        size_t clen = strlen(cwd);
        memcpy(abspath, cwd, clen);
        if (abspath[clen-1] != '/') abspath[clen++] = '/';
        memcpy(abspath + clen, kpath, strlen(kpath) + 1);
    }

    int ret = vfs_mkdir(abspath);
    if (ret == -EEXIST) return -(int64_t)EEXIST;
    if (ret != 0) return -(int64_t)ENOMEM;
    return 0;
}

int64_t sys_rmdir(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    char abspath[PATH_MAX_LEN];
    if (kpath[0] == '/') {
        memcpy(abspath, kpath, strlen(kpath) + 1);
    } else {
        const char *cwd = current_task->process->cwd;
        size_t clen = strlen(cwd);
        memcpy(abspath, cwd, clen);
        if (abspath[clen-1] != '/') abspath[clen++] = '/';
        memcpy(abspath + clen, kpath, strlen(kpath) + 1);
    }

    /* vfs_unlink handles the "is dir" and "is empty" checks */
    int ret = vfs_unlink(abspath);
    if (ret == -ENOENT)    return -(int64_t)ENOENT;
    if (ret == -ENOTEMPTY) return -(int64_t)ENOTEMPTY;
    if (ret != 0)          return -(int64_t)EINVAL;
    return 0;
}

int64_t sys_unlink(struct syscall_frame *frame)
{
    const char *pathname = (const char *)frame->rdi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kpath[PATH_MAX_LEN];
    if (copy_from_user(kpath, pathname, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kpath[PATH_MAX_LEN - 1] = '\0';

    char abspath[PATH_MAX_LEN];
    if (kpath[0] == '/') {
        memcpy(abspath, kpath, strlen(kpath) + 1);
    } else {
        const char *cwd = current_task->process->cwd;
        size_t clen = strlen(cwd);
        memcpy(abspath, cwd, clen);
        if (abspath[clen-1] != '/') abspath[clen++] = '/';
        memcpy(abspath + clen, kpath, strlen(kpath) + 1);
    }

    int ret = vfs_unlink(abspath);
    if (ret == -ENOENT) return -(int64_t)ENOENT;
    if (ret != 0)       return -(int64_t)EINVAL;
    return 0;
}

int64_t sys_rename(struct syscall_frame *frame)
{
    const char *oldpath = (const char *)frame->rdi;
    const char *newpath = (const char *)frame->rsi;

    if (current_task == 0 || current_task->process == 0) return -(int64_t)EPERM;

    char kold[PATH_MAX_LEN], knew[PATH_MAX_LEN];
    if (copy_from_user(kold, oldpath, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    if (copy_from_user(knew, newpath, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
    kold[PATH_MAX_LEN - 1] = '\0';
    knew[PATH_MAX_LEN - 1] = '\0';

    /* Note: only absolute paths for now */
    int ret = vfs_rename(kold, knew);
    if (ret == -ENOENT) return -(int64_t)ENOENT;
    if (ret != 0)       return -(int64_t)EINVAL;
    return 0;
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
    if (copy_from_user(kpath, filename, PATH_MAX_LEN - 1) != 0) return -(int64_t)EFAULT;
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
            if (copy_from_user(kstr, (const void *)uptr, MAX_ARG_LEN - 1) != 0) {
                kfree(kstr);
                goto fail_fault_argv;
            }
            kstr[MAX_ARG_LEN - 1] = '\0';
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
            if (copy_from_user(kstr, (const void *)uptr, MAX_ARG_LEN - 1) != 0) {
                kfree(kstr);
                goto fail_fault_envp;
            }
            kstr[MAX_ARG_LEN - 1] = '\0';
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

    /* --- Close FD_CLOEXEC descriptors --- */
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        if (proc->files[i] != 0 && (proc->files[i]->fd_flags & FD_CLOEXEC)) {
            struct file *f = proc->files[i];
            proc->files[i] = 0;
            file_close(f);
        }
    }

    /* --- Load ELF --- */
    uint64_t entry = 0, rsp = 0;
    int load_err = elf_load_into_process(proc, node->data, node->size,
                                          argc, kargv, kenvp, filename, &entry, &rsp);
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
        }
    } else {
        p->readers--;
        if (p->readers == 0) {
            wait_queue_wake_all(&p->write_wq);
        }
    }

    if (p->readers == 0 && p->writers == 0) {
        kfree(p);
        kfree(node);
    }
    return 0;
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

            if (node->read == pipe_read) {
                struct pipe *p = (struct pipe *)node->data;
                if (p != 0) {
                    if (p->count > 0 && (kfds[i].events & POLLIN)) {
                        kfds[i].revents |= POLLIN;
                    }
                    if (p->writers == 0) {
                        kfds[i].revents |= POLLHUP;
                    }
                }
            }
            else if (node->write == pipe_write) {
                struct pipe *p = (struct pipe *)node->data;
                if (p != 0) {
                    if (p->count < PIPE_BUF_SIZE && (kfds[i].events & POLLOUT)) {
                        kfds[i].revents |= POLLOUT;
                      }
                      if (p->readers == 0) {
                          kfds[i].revents |= POLLERR;
                      }
                  }
              }
              else {
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
