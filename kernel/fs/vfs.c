#include <fs/vfs.h>
#include <mm/heap.h>
#include <lib/string.h>
#include <drivers/serial.h>
#include <kernel/panic.h>
#include <kernel/printk.h>
#include <sys/syscall.h>
#include <mm/pmm.h>
#include <sched/task.h>
#include <drivers/pit.h>

static struct vfs_node *root_node = 0;
static uint64_t next_inode = 1;

static int default_file_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    if (offset >= node->size) {
        return 0;
    }
    size_t available = node->size - offset;
    size_t copy_len = count < available ? count : available;
    if (copy_len > 0 && node->data != 0) {
        memcpy(buf, (const uint8_t *)node->data + offset, copy_len);
    }
    return (int)copy_len;
}

static int default_file_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    if (offset + count > node->size) {
        void *new_data = krealloc(node->data, offset + count);
        if (new_data == 0) {
            return -1;
        }
        node->data = new_data;
        node->size = offset + count;
    }
    memcpy((uint8_t *)node->data + offset, buf, count);
    return (int)count;
}

static int default_dir_readdir(struct vfs_node *node, size_t index, struct vfs_dirent *dirent)
{
    struct vfs_node *child = node->children;
    size_t i = 0;
    while (child != 0 && i < index) {
        child = child->next;
        i++;
    }
    if (child == 0) {
        return 0; // End of directory
    }

    memset(dirent, 0, sizeof(struct vfs_dirent));
    size_t name_len = strlen(child->name);
    if (name_len >= sizeof(dirent->name)) {
        name_len = sizeof(dirent->name) - 1;
    }
    memcpy(dirent->name, child->name, name_len);
    dirent->name[name_len] = '\0';
    dirent->inode_num = child->inode_num;
    dirent->type = child->type;
    return 1;
}

static int console_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    if (count == 0) return 0;

    char *cbuf = buf;
    size_t read_bytes = 0;

    while (read_bytes < count) {
        char ch = serial_read_char();
        // Echo characters back to terminal
        if (ch == '\r') {
            ch = '\n';
        }
        // Echo back to user
        serial_write_char(ch);

        cbuf[read_bytes++] = ch;
        if (ch == '\n') {
            break;
        }
    }
    return (int)read_bytes;
}

static int console_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset;
    const char *cbuf = buf;
    for (size_t i = 0; i < count; i++) {
        serial_write_char(cbuf[i]);
    }
    return (int)count;
}

// PRNG state for /dev/random and /dev/urandom
static uint64_t rand_state = 88172645463325252ULL;
static uint64_t xorshift64(void)
{
    uint64_t x = rand_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    rand_state = x;
    return x;
}

// /dev/null callbacks
static int null_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf; (void)count;
    return 0;
}

static int null_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf;
    return (int)count;
}

// /dev/zero callbacks
static int zero_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    if (count == 0) return 0;
    memset(buf, 0, count);
    return (int)count;
}

static int zero_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf;
    return (int)count;
}

// /dev/full callbacks
static int full_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset; (void)buf;
    if (count == 0) return 0;
    return -ENOSPC;
}

// /dev/random and /dev/urandom callbacks
static int random_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    uint8_t *ubuf = buf;
    for (size_t i = 0; i < count; i++) {
        ubuf[i] = (uint8_t)(xorshift64() & 0xFF);
    }
    return (int)count;
}

static int random_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset;
    const uint8_t *ubuf = buf;
    uint64_t seed_mix = 0;
    size_t mix_len = count < 8 ? count : 8;
    for (size_t i = 0; i < mix_len; i++) {
        seed_mix |= ((uint64_t)ubuf[i]) << (i * 8);
    }
    if (seed_mix != 0) {
        rand_state ^= seed_mix;
    }
    return (int)count;
}

// /dev/tty callbacks
static int tty_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node;
    struct vfs_node *console = vfs_lookup("/dev/console");
    if (console == 0 || console->read == 0) return -EIO;
    return console->read(console, offset, buf, count);
}

static int tty_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node;
    struct vfs_node *console = vfs_lookup("/dev/console");
    if (console == 0 || console->write == 0) return -EIO;
    return console->write(console, offset, buf, count);
}

#define KMSG_BUF_SIZE 16384
static char kmsg_buf[KMSG_BUF_SIZE];
static uint64_t kmsg_total_written = 0;

void kmsg_put_char(char ch)
{
    kmsg_buf[kmsg_total_written % KMSG_BUF_SIZE] = ch;
    kmsg_total_written++;
}

// /dev/kmsg callbacks
static int kmsg_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node;
    if (offset >= kmsg_total_written) return 0;

    if (kmsg_total_written > KMSG_BUF_SIZE && offset < kmsg_total_written - KMSG_BUF_SIZE) {
        offset = kmsg_total_written - KMSG_BUF_SIZE;
    }

    size_t avail = kmsg_total_written - offset;
    size_t copy_len = count < avail ? count : avail;

    for (size_t i = 0; i < copy_len; i++) {
        ((char *)buf)[i] = kmsg_buf[(offset + i) % KMSG_BUF_SIZE];
    }

    return (int)copy_len;
}

static int kmsg_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)node; (void)offset;
    char temp[256];
    size_t len = count < 255 ? count : 255;
    memcpy(temp, buf, len);
    temp[len] = '\0';
    printk("[kmsg] %s", temp);
    return (int)count;
}

static void utoa(uint64_t val, char *buf, int base)
{
    char temp[32];
    int i = 0;
    if (val == 0) {
        temp[i++] = '0';
    } else {
        while (val > 0) {
            int rem = val % base;
            temp[i++] = rem < 10 ? '0' + rem : 'a' + (rem - 10);
            val /= base;
        }
    }
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

static int proc_version_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    const char *ver = "LiteNix version 1.0.0 (x86_64) Freestanding C\n";
    size_t len = strlen(ver);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, ver + offset, copy);
    return (int)copy;
}

static int proc_cpuinfo_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    const char *info = "processor\t: 0\n"
                       "vendor_id\t: GenuineIntel\n"
                       "cpu family\t: 6\n"
                       "model\t\t: 62\n"
                       "model name\t: Intel(R) Xeon(R) CPU E5-2680 v2 @ 2.80GHz\n"
                       "cpu cores\t: 1\n"
                       "flags\t\t: fpu vme de pse tsc msr pae mce cx8 apic sep mtrr pge mca cmov pat pse36 clflush dts acpi mmx fxsr sse sse2 ss ht tm pbe syscall nx pdpe1gb rdtscp lm\n\n";
    size_t len = strlen(info);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, info + offset, copy);
    return (int)copy;
}

static int proc_meminfo_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node;
    struct pmm_stats pstats = pmm_get_stats();
    uint64_t total_kib = (pstats.total_pages * PMM_PAGE_SIZE) / 1024;
    uint64_t free_kib = (pstats.free_pages * PMM_PAGE_SIZE) / 1024;

    char msg[512];
    strcpy(msg, "MemTotal:       ");
    char num[32];
    utoa(total_kib, num, 10);
    strcat(msg, num);
    strcat(msg, " kB\nMemFree:        ");
    utoa(free_kib, num, 10);
    strcat(msg, num);
    strcat(msg, " kB\nMemAvailable:   ");
    utoa(free_kib, num, 10);
    strcat(msg, num);
    strcat(msg, " kB\n");

    size_t len = strlen(msg);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, msg + offset, copy);
    return (int)copy;
}

static int proc_uptime_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node;
    uint64_t ticks = pit_ticks();
    uint64_t secs = ticks / 100;
    uint64_t frac = ticks % 100;

    char msg[64];
    strcpy(msg, "");
    char num[32];
    utoa(secs, num, 10);
    strcat(msg, num);
    strcat(msg, ".");
    if (frac < 10) {
        strcat(msg, "0");
    }
    utoa(frac, num, 10);
    strcat(msg, num);
    strcat(msg, " 0.00\n");

    size_t len = strlen(msg);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, msg + offset, copy);
    return (int)copy;
}

static int proc_stat_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node; (void)offset;
    const char *msg = "cpu  23 0 45 1000 0 0 0 0 0 0\n"
                      "intr 123 0 0\n"
                      "ctxt 456\n"
                      "btime 1700000000\n"
                      "processes 15\n";
    size_t len = strlen(msg);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, msg + offset, copy);
    return (int)copy;
}

static int proc_self_status_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)node;
    struct task *t = current_task;
    if (t == 0) return 0;
    struct process *p = t->process;
    if (p == 0) return 0;

    char msg[256];
    strcpy(msg, "Name:\t");
    strcat(msg, t->name);
    strcat(msg, "\nState:\t");

    const char *state_str = "R (running)";
    if (t->state == TASK_READY) state_str = "R (ready)";
    else if (t->state == TASK_SLEEPING) state_str = "S (sleeping)";
    else if (t->state == TASK_ZOMBIE) state_str = "Z (zombie)";

    strcat(msg, state_str);
    strcat(msg, "\nTgid:\t");
    char num[32];
    utoa(p->pid, num, 10);
    strcat(msg, num);
    strcat(msg, "\nPid:\t");
    utoa(p->pid, num, 10);
    strcat(msg, num);
    strcat(msg, "\nPPid:\t");
    utoa(p->parent ? p->parent->pid : 0, num, 10);
    strcat(msg, num);
    strcat(msg, "\nUmask:\t0022\n");

    size_t len = strlen(msg);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, msg + offset, copy);
    return (int)copy;
}

static int proc_pid_status_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    uint64_t pid = (uintptr_t)node->data;
    struct process *p = find_process(pid);
    if (p == 0 || p->main_thread == 0) return 0;
    struct task *t = p->main_thread;

    char msg[256];
    strcpy(msg, "Name:\t");
    strcat(msg, t->name);
    strcat(msg, "\nState:\t");

    const char *state_str = "R (running)";
    if (t->state == TASK_READY) state_str = "R (ready)";
    else if (t->state == TASK_SLEEPING) state_str = "S (sleeping)";
    else if (t->state == TASK_ZOMBIE) state_str = "Z (zombie)";

    strcat(msg, state_str);
    strcat(msg, "\nTgid:\t");
    char num[32];
    utoa(p->pid, num, 10);
    strcat(msg, num);
    strcat(msg, "\nPid:\t");
    utoa(p->pid, num, 10);
    strcat(msg, num);
    strcat(msg, "\nPPid:\t");
    utoa(p->parent ? p->parent->pid : 0, num, 10);
    strcat(msg, num);
    strcat(msg, "\nUmask:\t0022\n");

    size_t len = strlen(msg);
    if (offset >= len) return 0;
    size_t avail = len - offset;
    size_t copy = count < avail ? count : avail;
    memcpy(buf, msg + offset, copy);
    return (int)copy;
}

static struct process *get_process_at_index(struct process *root, size_t *current_index, size_t target_index)
{
    if (root == 0) return 0;

    if (*current_index == target_index) {
        return root;
    }
    (*current_index)++;

    struct process *child = root->children;
    while (child != 0) {
        struct process *found = get_process_at_index(child, current_index, target_index);
        if (found != 0) return found;
        child = child->sibling;
    }
    return 0;
}

static int proc_dir_readdir(struct vfs_node *node, size_t index, struct vfs_dirent *dirent)
{
    struct vfs_node *child = node->children;
    size_t static_count = 0;
    while (child != 0) {
        bool is_num = true;
        if (child->name[0] == '\0') is_num = false;
        for (int k = 0; child->name[k] != '\0'; k++) {
            if (child->name[k] < '0' || child->name[k] > '9') {
                is_num = false;
                break;
            }
        }
        if (!is_num) {
            if (static_count == index) {
                strcpy(dirent->name, child->name);
                dirent->inode_num = child->inode_num;
                dirent->type = child->type;
                return 1;
            }
            static_count++;
        }
        child = child->next;
    }

    size_t process_target = index - static_count;
    size_t current_idx = 0;
    struct process *p = get_process_at_index(init_process, &current_idx, process_target);
    if (p != 0) {
        utoa(p->pid, dirent->name, 10);
        dirent->inode_num = 10000 + p->pid;
        dirent->type = VFS_TYPE_DIR;
        return 1;
    }

    return 0;
}

void ext2_init(void);

void vfs_init(void)
{
    root_node = kzalloc(sizeof(struct vfs_node));
    if (root_node == 0) {
        panic("VFS: Failed to allocate root node");
    }
    strcpy(root_node->name, "/");
    root_node->type = VFS_TYPE_DIR;
    root_node->inode_num = next_inode++;
    root_node->readdir = default_dir_readdir;

    // Create /dev directory
    vfs_create_file("/dev", VFS_TYPE_DIR, 0, 0);

    // Create /dev/console
    vfs_create_device("/dev/console", console_read, console_write);

    // Create /dev/null
    vfs_create_device("/dev/null", null_read, null_write);

    // Create /dev/zero
    vfs_create_device("/dev/zero", zero_read, zero_write);

    // Create /dev/full
    vfs_create_device("/dev/full", zero_read, full_write);

    // Create /dev/random and /dev/urandom
    vfs_create_device("/dev/random", random_read, random_write);
    vfs_create_device("/dev/urandom", random_read, random_write);

    // Create /dev/tty
    vfs_create_device("/dev/tty", tty_read, tty_write);

    // Create /dev/kmsg
    vfs_create_device("/dev/kmsg", kmsg_read, kmsg_write);

    // Create /sys directory
    vfs_create_file("/sys", VFS_TYPE_DIR, 0, 0);

    // Create /proc directory
    struct vfs_node *proc_node = vfs_create_file("/proc", VFS_TYPE_DIR, 0, 0);
    if (proc_node != 0) {
        proc_node->readdir = proc_dir_readdir;
    }

    // Create /proc/version, cpuinfo, meminfo, uptime, stat
    vfs_create_device("/proc/version", proc_version_read, 0);
    vfs_create_device("/proc/cpuinfo", proc_cpuinfo_read, 0);
    vfs_create_device("/proc/meminfo", proc_meminfo_read, 0);
    vfs_create_device("/proc/uptime", proc_uptime_read, 0);
    vfs_create_device("/proc/stat", proc_stat_read, 0);

    // Create /proc/self directory and status
    vfs_create_file("/proc/self", VFS_TYPE_DIR, 0, 0);
    vfs_create_device("/proc/self/status", proc_self_status_read, 0);

    // Phase 23: Create /tmp directory (acting as tmpfs)
    vfs_create_file("/tmp", VFS_TYPE_DIR, 0, 0);

    // Initialize ext2 filesystem
    ext2_init();
}

struct vfs_node *vfs_get_root(void)
{
    return root_node;
}

void vfs_canonicalize_path(const char *src, char *dst)
{
    int dst_idx = 0;
    dst[dst_idx++] = '/';
    dst[dst_idx] = '\0';

    const char *p = src;
    if (*p == '/') p++;

    while (*p != '\0') {
        const char *comp_start = p;
        while (*p != '\0' && *p != '/') {
            p++;
        }
        size_t len = p - comp_start;
        if (*p == '/') p++;

        if (len == 0 || (len == 1 && comp_start[0] == '.')) {
            continue;
        }

        if (len == 2 && comp_start[0] == '.' && comp_start[1] == '.') {
            if (dst_idx > 1) {
                dst_idx--;
                while (dst_idx > 0 && dst[dst_idx] != '/') {
                    dst_idx--;
                }
                if (dst_idx == 0) {
                    dst_idx = 1;
                }
                dst[dst_idx] = '\0';
            }
            continue;
        }

        if (dst_idx > 1) {
            dst[dst_idx++] = '/';
        }
        for (size_t i = 0; i < len; i++) {
            dst[dst_idx++] = comp_start[i];
        }
        dst[dst_idx] = '\0';
    }
}

struct vfs_node *vfs_lookup(const char *path)
{
    if (path == 0) return 0;

    char cleanpath[512];
    if (path[0] != '/') {
        char temp[512];
        temp[0] = '/';
        size_t len = strlen(path);
        if (len >= 510) return 0;
        memcpy(temp + 1, path, len + 1);
        vfs_canonicalize_path(temp, cleanpath);
    } else {
        vfs_canonicalize_path(path, cleanpath);
    }

    struct vfs_node *current = root_node;
    const char *p = cleanpath;
    if (p[0] == '/') {
        p++;
    }

    char component[128];
    while (*p != '\0') {
        int len = 0;
        while (*p != '\0' && *p != '/' && len < 127) {
            component[len++] = *p++;
        }
        component[len] = '\0';

        if (*p == '/') {
            p++;
        }

        if (len == 0) {
            continue;
        }

        struct vfs_node *child = current->children;
        bool found = false;
        while (child != 0) {
            if (strcmp(child->name, component) == 0) {
                current = child;
                found = true;
                break;
            }
            child = child->next;
        }

        if (!found) {
            if (strcmp(current->name, "proc") == 0 && current->parent == root_node) {
                bool numeric = true;
                if (component[0] == '\0') numeric = false;
                for (int i = 0; component[i] != '\0'; i++) {
                    if (component[i] < '0' || component[i] > '9') {
                        numeric = false;
                        break;
                    }
                }
                if (numeric) {
                    uint64_t pid = 0;
                    for (int i = 0; component[i] != '\0'; i++) {
                        pid = pid * 10 + (component[i] - '0');
                    }
                    struct process *p = find_process(pid);
                    if (p != 0) {
                        char pid_dir_path[256];
                        strcpy(pid_dir_path, "/proc/");
                        strcat(pid_dir_path, component);

                        struct vfs_node *pid_dir = vfs_create_file(pid_dir_path, VFS_TYPE_DIR, 0, 0);
                        if (pid_dir != 0) {
                            char pid_status_path[256];
                            strcpy(pid_status_path, pid_dir_path);
                            strcat(pid_status_path, "/status");

                            struct vfs_node *status_node = vfs_create_device(pid_status_path, proc_pid_status_read, 0);
                            if (status_node != 0) {
                                status_node->data = (void *)(uintptr_t)pid;
                            }

                            child = current->children;
                            while (child != 0) {
                                if (strcmp(child->name, component) == 0) {
                                    current = child;
                                    found = true;
                                    break;
                                }
                                child = child->next;
                            }
                        }
                    }
                }
            }
        }

        if (!found) {
            return 0;
        }
    }
    return current;
}

struct vfs_node *vfs_create_file(const char *path, uint32_t type, size_t size, const void *data)
{
    if (path == 0 || path[0] != '/') return 0;

    struct vfs_node *current = root_node;
    path++;

    char component[128];
    while (*path != '\0') {
        int len = 0;
        while (*path != '\0' && *path != '/' && len < 127) {
            component[len++] = *path++;
        }
        component[len] = '\0';

        bool is_last = (*path == '\0');
        if (*path == '/') {
            path++;
        }

        if (len == 0) continue;

        struct vfs_node *child = current->children;
        struct vfs_node *prev = 0;
        struct vfs_node *found_node = 0;
        while (child != 0) {
            if (strcmp(child->name, component) == 0) {
                found_node = child;
                break;
            }
            prev = child;
            child = child->next;
        }

        if (found_node != 0) {
            if (is_last) {
                return found_node;
            }
            current = found_node;
        } else {
            struct vfs_node *new_node = kzalloc(sizeof(struct vfs_node));
            if (new_node == 0) return 0;

            strcpy(new_node->name, component);
            new_node->inode_num = next_inode++;
            new_node->parent = current;

            if (is_last) {
                new_node->type = type;
                new_node->size = size;
                if (type == VFS_TYPE_FILE) {
                    new_node->read = default_file_read;
                    new_node->write = default_file_write;
                    if (size > 0 && data != 0) {
                        new_node->data = kmalloc(size);
                        if (new_node->data == 0) {
                            kfree(new_node);
                            return 0;
                        }
                        memcpy(new_node->data, data, size);
                    }
                } else if (type == VFS_TYPE_DIR) {
                    new_node->readdir = default_dir_readdir;
                }
            } else {
                new_node->type = VFS_TYPE_DIR;
                new_node->readdir = default_dir_readdir;
            }

            if (prev != 0) {
                prev->next = new_node;
            } else {
                current->children = new_node;
            }

            current = new_node;
        }
    }
    return current;
}

struct vfs_node *vfs_create_device(const char *path, vfs_read_t read, vfs_write_t write)
{
    struct vfs_node *node = vfs_create_file(path, VFS_TYPE_CHAR, 0, 0);
    if (node != 0) {
        node->read = read;
        node->write = write;
        node->mode = S_IFCHR | 0666;
    }
    return node;
}

/* ------------------------------------------------------------------ */
/* vfs_lookup_at — resolve path relative to cwd                        */
/* ------------------------------------------------------------------ */

/*
 * If path starts with '/' it is treated as absolute.
 * Otherwise cwd is prepended.  cwd may be NULL (treated as "/").
 */
struct vfs_node *vfs_lookup_at(const char *cwd, const char *path)
{
    if (path == 0) return 0;
    if (path[0] == '/') {
        return vfs_lookup(path);
    }

    /* Build absolute path = cwd + "/" + path */
    char abspath[512];
    if (cwd == 0 || cwd[0] == '\0') {
        abspath[0] = '/';
        abspath[1] = '\0';
    } else {
        size_t clen = strlen(cwd);
        if (clen >= sizeof(abspath) - 2) return 0;
        memcpy(abspath, cwd, clen);
        if (abspath[clen - 1] != '/') {
            abspath[clen] = '/';
            clen++;
        }
        abspath[clen] = '\0';
    }

    size_t alen = strlen(abspath);
    size_t plen = strlen(path);
    if (alen + plen >= sizeof(abspath)) return 0;
    memcpy(abspath + alen, path, plen + 1);

    return vfs_lookup(abspath);
}

/* ------------------------------------------------------------------ */
/* vfs_mkdir                                                            */
/* ------------------------------------------------------------------ */

int vfs_mkdir(const char *path)
{
    if (path == 0 || path[0] != '/') return -EINVAL;

    /* Check it doesn't already exist */
    struct vfs_node *existing = vfs_lookup(path);
    if (existing != 0) return -EEXIST;

    struct vfs_node *node = vfs_create_file(path, VFS_TYPE_DIR, 0, 0);
    if (node == 0) return -ENOMEM;
    node->mode = S_IFDIR | 0755;
    return 0;
}

/* ------------------------------------------------------------------ */
/* vfs_unlink — remove a file node from the tree                       */
/* ------------------------------------------------------------------ */

static struct vfs_node *find_parent_of(const char *path, char *basename_out, size_t basename_sz)
{
    /* Split path into directory part and basename */
    if (path == 0 || path[0] != '/') return 0;

    /* Find last '/' */
    size_t len = strlen(path);
    size_t last_slash = 0;
    for (size_t i = 0; i < len; i++) {
        if (path[i] == '/') last_slash = i;
    }

    /* Basename */
    const char *base = path + last_slash + 1;
    size_t blen = strlen(base);
    if (blen == 0 || blen >= basename_sz) return 0;
    memcpy(basename_out, base, blen + 1);

    /* Parent path */
    if (last_slash == 0) {
        /* Parent is root */
        return root_node;
    }

    char parent_path[512];
    if (last_slash >= sizeof(parent_path)) return 0;
    memcpy(parent_path, path, last_slash);
    parent_path[last_slash] = '\0';

    return vfs_lookup(parent_path);
}

int vfs_unlink(const char *path)
{
    if (path == 0 || path[0] != '/') return -EINVAL;

    char basename[128];
    struct vfs_node *parent = find_parent_of(path, basename, sizeof(basename));
    if (parent == 0) return -ENOENT;

    /* Find node in parent's children list */
    struct vfs_node *prev = 0;
    struct vfs_node *node = parent->children;
    while (node != 0) {
        if (strcmp(node->name, basename) == 0) break;
        prev = node;
        node = node->next;
    }
    if (node == 0) return -ENOENT;
    if (node->type == VFS_TYPE_DIR && node->children != 0) return -ENOTEMPTY;

    /* Unlink from sibling list */
    if (prev != 0) {
        prev->next = node->next;
    } else {
        parent->children = node->next;
    }

    /* Free data and node */
    if (node->data != 0) kfree(node->data);
    kfree(node);
    return 0;
}

/* ------------------------------------------------------------------ */
/* vfs_rename                                                           */
/* ------------------------------------------------------------------ */

int vfs_rename(const char *oldpath, const char *newpath)
{
    if (oldpath == 0 || newpath == 0) return -EINVAL;

    /* Unlink target if it exists (overwrite) */
    struct vfs_node *target = vfs_lookup(newpath);
    if (target != 0) {
        int r = vfs_unlink(newpath);
        if (r != 0) return r;
    }

    char old_basename[128];
    struct vfs_node *old_parent = find_parent_of(oldpath, old_basename, sizeof(old_basename));
    if (old_parent == 0) return -ENOENT;

    char new_basename[128];
    struct vfs_node *new_parent = find_parent_of(newpath, new_basename, sizeof(new_basename));
    if (new_parent == 0) return -ENOENT;

    /* Find node */
    struct vfs_node *prev = 0;
    struct vfs_node *node = old_parent->children;
    while (node != 0) {
        if (strcmp(node->name, old_basename) == 0) break;
        prev = node;
        node = node->next;
    }
    if (node == 0) return -ENOENT;

    /* Detach from old parent */
    if (prev != 0) {
        prev->next = node->next;
    } else {
        old_parent->children = node->next;
    }
    node->next = 0;

    /* Rename */
    size_t nlen = strlen(new_basename);
    if (nlen >= sizeof(node->name)) nlen = sizeof(node->name) - 1;
    memcpy(node->name, new_basename, nlen);
    node->name[nlen] = '\0';
    node->parent = new_parent;

    /* Attach to new parent at head */
    node->next = new_parent->children;
    new_parent->children = node;

    return 0;
}

/* ------------------------------------------------------------------ */
/* vfs_truncate                                                         */
/* ------------------------------------------------------------------ */

int vfs_truncate(struct vfs_node *node, size_t new_size)
{
    if (node == 0) return -EINVAL;
    if (node->type != VFS_TYPE_FILE) return -EINVAL;

    extern int ext2_file_write(struct vfs_node *node, size_t offset, const void *buf, size_t count);
    if (node->write == ext2_file_write) {
        extern int ext2_truncate(struct vfs_node *node, size_t new_size);
        return ext2_truncate(node, new_size);
    }

    if (new_size == 0) {
        if (node->data != 0) {
            kfree(node->data);
            node->data = 0;
        }
        node->size = 0;
        return 0;
    }

    void *new_data = krealloc(node->data, new_size);
    if (new_data == 0) return -ENOMEM;

    /* Zero-fill extension */
    if (new_size > node->size) {
        memset((uint8_t *)new_data + node->size, 0, new_size - node->size);
    }

    node->data = new_data;
    node->size = new_size;
    return 0;
}

void file_close(struct file *f)
{
    if (f == 0) return;
    f->ref_count--;
    if (f->ref_count == 0) {
        if (f->node != 0 && f->node->close != 0) {
            f->node->close(f->node, f);
        }
        kfree(f);
    }
}
