#include <sys/syscall.h>
#include <fs/vfs.h>
#include <sched/task.h>
#include <sched/wait_queue.h>
#include <net/net.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <mm/uaccess.h>
#include <kernel/printk.h>

struct socket socket_table[MAX_SOCKETS];

// Forward declaration of file ops
static int socket_read(struct vfs_node *node, size_t offset, void *buf, size_t count);
static int socket_write(struct vfs_node *node, size_t offset, const void *buf, size_t count);
static int socket_close(struct vfs_node *node, struct file *f);

static int alloc_fd(struct process *proc)
{
    for (int i = 0; i < MAX_FILES_PER_PROCESS; i++) {
        if (proc->files[i] == 0) return i;
    }
    return -1;
}

static int socket_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)offset;
    struct socket *sock = (struct socket *)node->data;
    if (!sock || !sock->valid) return -EBADF;

    while (sock->rx_read_ptr == sock->rx_write_ptr) {
        if (sock->type == SOCKET_TYPE_TCP && (sock->tcp_state == TCP_STATE_CLOSED || sock->closed)) {
            return 0; // EOF
        }
        wait_queue_sleep(&sock->wait_q);
    }

    size_t copied = 0;
    if (sock->type == SOCKET_TYPE_TCP) {
        while (sock->rx_read_ptr != sock->rx_write_ptr && copied < count) {
            ((uint8_t *)buf)[copied++] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }
    } else {
        uint8_t src_ip[4];
        uint16_t src_port;
        uint16_t payload_len;

        for (int i = 0; i < 4; i++) {
            src_ip[i] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }
        src_port = sock->rx_buf[sock->rx_read_ptr];
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        src_port |= ((uint16_t)sock->rx_buf[sock->rx_read_ptr]) << 8;
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;

        (void)src_ip;
        (void)src_port;

        payload_len = sock->rx_buf[sock->rx_read_ptr];
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        payload_len |= ((uint16_t)sock->rx_buf[sock->rx_read_ptr]) << 8;
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;

        size_t to_copy = (payload_len < count) ? payload_len : count;
        for (size_t i = 0; i < to_copy; i++) {
            ((uint8_t *)buf)[i] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }

        if (payload_len > to_copy) {
            sock->rx_read_ptr = (sock->rx_read_ptr + (payload_len - to_copy)) % sock->rx_buf_size;
        }
        copied = to_copy;
    }

    return copied;
}

extern void net_tcp_send_segment(struct socket *sock, uint8_t flags, const void *payload, uint16_t len);
extern void net_udp_send(struct socket *sock, const uint8_t *dest_ip, uint16_t dest_port, const void *payload, uint16_t len);

static int socket_write(struct vfs_node *node, size_t offset, const void *buf, size_t count)
{
    (void)offset;
    struct socket *sock = (struct socket *)node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->type == SOCKET_TYPE_TCP) {
        if (sock->tcp_state != TCP_STATE_ESTABLISHED) {
            return -ENOTCONN;
        }
        net_tcp_send_segment(sock, TCP_FLAG_ACK | TCP_FLAG_PSH, buf, count);
        return count;
    } else {
        if (!sock->connected) {
            return -EDESTADDRREQ;
        }
        net_udp_send(sock, sock->remote_ip, sock->remote_port, buf, count);
        return count;
    }
}

static int socket_close(struct vfs_node *node, struct file *f)
{
    (void)f;
    struct socket *sock = (struct socket *)node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->type == SOCKET_TYPE_TCP) {
        if (sock->tcp_state == TCP_STATE_ESTABLISHED || sock->tcp_state == TCP_STATE_SYN_RECEIVED) {
            sock->tcp_state = TCP_STATE_FIN_WAIT_1;
            net_tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);

            int timeout = 100;
            while (sock->tcp_state != TCP_STATE_CLOSED && timeout-- > 0) {
                task_sleep_ticks(1);
            }
        }
    }

    if (sock->rx_buf) {
        kfree(sock->rx_buf);
        sock->rx_buf = NULL;
    }
    sock->valid = false;
    kfree(node);
    return 0;
}

int64_t sys_socket(struct syscall_frame *frame)
{
    int domain = (int)frame->rdi;
    int type = (int)frame->rsi;
    int protocol = (int)frame->rdx;
    (void)protocol;

    if (domain != 2) return -EAFNOSUPPORT; // AF_INET = 2

    int sock_type = 0;
    if (type == 1) { // SOCK_STREAM
        sock_type = SOCKET_TYPE_TCP;
    } else if (type == 2) { // SOCK_DGRAM
        sock_type = SOCKET_TYPE_UDP;
    } else {
        return -EINVAL;
    }

    struct process *proc = current_task->process;
    int fd = alloc_fd(proc);
    if (fd < 0) return -EMFILE;

    struct socket *sock = NULL;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].valid) {
            sock = &socket_table[i];
            memset(sock, 0, sizeof(struct socket));
            sock->valid = true;
            sock->type = sock_type;
            break;
        }
    }
    if (!sock) return -ENFILE;

    sock->rx_buf_size = 8192;
    sock->rx_buf = kmalloc(sock->rx_buf_size);
    if (!sock->rx_buf) {
        sock->valid = false;
        return -ENOMEM;
    }
    sock->rx_read_ptr = 0;
    sock->rx_write_ptr = 0;
    wait_queue_init(&sock->wait_q);

    if (sock_type == SOCKET_TYPE_TCP) {
        sock->tcp_state = TCP_STATE_CLOSED;
    }

    struct vfs_node *node = kzalloc(sizeof(struct vfs_node));
    if (!node) {
        kfree(sock->rx_buf);
        sock->valid = false;
        return -ENOMEM;
    }
    strcpy(node->name, "socket");
    node->type = VFS_TYPE_CHAR;
    node->data = sock;
    node->read = socket_read;
    node->write = socket_write;
    node->close = socket_close;

    struct file *f = kzalloc(sizeof(struct file));
    if (!f) {
        kfree(node);
        kfree(sock->rx_buf);
        sock->valid = false;
        return -ENOMEM;
    }
    f->node = node;
    f->offset = 0;
    f->flags = O_RDWR;
    f->fd_flags = 0;
    f->ref_count = 1;

    proc->files[fd] = f;
    return fd;
}

int64_t sys_bind(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)frame->rsi;
    uint32_t addrlen = (uint32_t)frame->rdx;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (!uaccess_ok(addr, addrlen)) return -EFAULT;

    struct sockaddr_in sin;
    if (addrlen < sizeof(struct sockaddr_in)) return -EINVAL;
    if (copy_from_user(&sin, addr, sizeof(struct sockaddr_in)) != 0) {
        return -EFAULT;
    }

    uint16_t port = (sin.sin_port << 8) | (sin.sin_port >> 8);
    sock->local_port = port;
    sock->bound = true;
    return 0;
}

int64_t sys_listen(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    int backlog = (int)frame->rsi;
    (void)backlog;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->type != SOCKET_TYPE_TCP) return -EOPNOTSUPP;

    sock->tcp_state = TCP_STATE_LISTEN;
    return 0;
}

int64_t sys_accept(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    struct sockaddr *addr = (struct sockaddr *)frame->rsi;
    uint32_t *addrlen = (uint32_t *)frame->rdx;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->type != SOCKET_TYPE_TCP) return -EOPNOTSUPP;

    if (addr && !uaccess_ok(addr, sizeof(struct sockaddr))) return -EFAULT;
    if (addrlen && !uaccess_ok(addrlen, sizeof(uint32_t))) return -EFAULT;

    while (sock->accept_count == 0) {
        wait_queue_sleep(&sock->wait_q);
    }

    struct socket *child = sock->accept_queue[0];
    for (int i = 1; i < sock->accept_count; i++) {
        sock->accept_queue[i-1] = sock->accept_queue[i];
    }
    sock->accept_count--;

    int child_fd = alloc_fd(proc);
    if (child_fd < 0) return -EMFILE;

    struct vfs_node *child_node = kzalloc(sizeof(struct vfs_node));
    if (!child_node) return -ENOMEM;
    strcpy(child_node->name, "socket_child");
    child_node->type = VFS_TYPE_CHAR;
    child_node->data = child;
    child_node->read = socket_read;
    child_node->write = socket_write;
    child_node->close = socket_close;

    struct file *child_file = kzalloc(sizeof(struct file));
    if (!child_file) {
        kfree(child_node);
        return -ENOMEM;
    }
    child_file->node = child_node;
    child_file->offset = 0;
    child_file->flags = O_RDWR;
    child_file->fd_flags = 0;
    child_file->ref_count = 1;

    proc->files[child_fd] = child_file;

    if (addr && addrlen) {
        uint32_t len;
        if (copy_from_user(&len, addrlen, sizeof(uint32_t)) != 0) {
            file_close(child_file);
            return -EFAULT;
        }

        struct sockaddr_in sin;
        memset(&sin, 0, sizeof(sin));
        sin.sin_family = 2; // AF_INET
        sin.sin_port = (child->remote_port << 8) | (child->remote_port >> 8);
        memcpy(&sin.sin_addr.s_addr, child->remote_ip, 4);

        uint32_t copy_len = (len < sizeof(struct sockaddr_in)) ? len : sizeof(struct sockaddr_in);
        if (copy_to_user(addr, &sin, copy_len) != 0) {
            file_close(child_file);
            return -EFAULT;
        }

        len = sizeof(struct sockaddr_in);
        if (copy_to_user(addrlen, &len, sizeof(uint32_t)) != 0) {
            file_close(child_file);
            return -EFAULT;
        }
    }

    return child_fd;
}

int64_t sys_connect(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    const struct sockaddr *addr = (const struct sockaddr *)frame->rsi;
    uint32_t addrlen = (uint32_t)frame->rdx;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (!uaccess_ok(addr, addrlen)) return -EFAULT;

    struct sockaddr_in sin;
    if (addrlen < sizeof(struct sockaddr_in)) return -EINVAL;
    if (copy_from_user(&sin, addr, sizeof(struct sockaddr_in)) != 0) {
        return -EFAULT;
    }

    sock->remote_port = (sin.sin_port << 8) | (sin.sin_port >> 8);
    memcpy(sock->remote_ip, &sin.sin_addr.s_addr, 4);

    if (sock->type == SOCKET_TYPE_TCP) {
        if (!sock->bound) {
            static uint16_t next_ephemeral_port = 49152;
            sock->local_port = next_ephemeral_port++;
            if (next_ephemeral_port == 0) next_ephemeral_port = 49152;
            sock->bound = true;
        }

        sock->tcp_state = TCP_STATE_SYN_SENT;
        sock->snd_nxt = 1000;
        sock->rcv_nxt = 0;

        net_tcp_send_segment(sock, TCP_FLAG_SYN, 0, 0);

        while (sock->tcp_state != TCP_STATE_ESTABLISHED) {
            wait_queue_sleep(&sock->wait_q);
        }
        sock->connected = true;
    } else {
        sock->connected = true;
    }

    return 0;
}

int64_t sys_sendto(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    const void *buf = (const void *)frame->rsi;
    size_t len = (size_t)frame->rdx;
    int flags = (int)frame->r10;
    const struct sockaddr *dest_addr = (const struct sockaddr *)frame->r8;
    uint32_t addrlen = (uint32_t)frame->r9;

    (void)flags;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (!uaccess_ok(buf, len)) return -EFAULT;

    if (len > 1400) len = 1400;
    uint8_t *kbuf = kmalloc(len);
    if (!kbuf) return -ENOMEM;
    if (copy_from_user(kbuf, buf, len) != 0) {
        kfree(kbuf);
        return -EFAULT;
    }

    if (dest_addr && addrlen >= sizeof(struct sockaddr_in)) {
        if (!uaccess_ok(dest_addr, addrlen)) {
            kfree(kbuf);
            return -EFAULT;
        }
        struct sockaddr_in sin;
        if (copy_from_user(&sin, dest_addr, sizeof(struct sockaddr_in)) != 0) {
            kfree(kbuf);
            return -EFAULT;
        }

        uint16_t port = (sin.sin_port << 8) | (sin.sin_port >> 8);
        uint8_t ip[4];
        memcpy(ip, &sin.sin_addr.s_addr, 4);

        if (sock->type == SOCKET_TYPE_TCP) {
            kfree(kbuf);
            return -EISCONN;
        }

        net_udp_send(sock, ip, port, kbuf, len);
    } else {
        if (!sock->connected) {
            kfree(kbuf);
            return -EDESTADDRREQ;
        }
        if (sock->type == SOCKET_TYPE_TCP) {
            net_tcp_send_segment(sock, TCP_FLAG_ACK | TCP_FLAG_PSH, kbuf, len);
        } else {
            net_udp_send(sock, sock->remote_ip, sock->remote_port, kbuf, len);
        }
    }

    kfree(kbuf);
    return len;
}

int64_t sys_recvfrom(struct syscall_frame *frame)
{
    int sockfd = (int)frame->rdi;
    void *buf = (void *)frame->rsi;
    size_t len = (size_t)frame->rdx;
    int flags = (int)frame->r10;
    struct sockaddr *src_addr = (struct sockaddr *)frame->r8;
    uint32_t *addrlen = (uint32_t *)frame->r9;

    (void)flags;

    struct process *proc = current_task->process;
    if (sockfd < 0 || sockfd >= MAX_FILES_PER_PROCESS || proc->files[sockfd] == 0) {
        return -EBADF;
    }
    struct file *f = proc->files[sockfd];
    if (f->node == 0 || f->node->read != socket_read) {
        return -ENOTSOCK;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (!uaccess_ok(buf, len)) return -EFAULT;
    if (src_addr && !uaccess_ok(src_addr, sizeof(struct sockaddr))) return -EFAULT;
    if (addrlen && !uaccess_ok(addrlen, sizeof(uint32_t))) return -EFAULT;

    while (sock->rx_read_ptr == sock->rx_write_ptr) {
        if (sock->type == SOCKET_TYPE_TCP && (sock->tcp_state == TCP_STATE_CLOSED || sock->closed)) {
            return 0; // EOF
        }
        wait_queue_sleep(&sock->wait_q);
    }

    uint8_t *kbuf = kmalloc(len);
    if (!kbuf) return -ENOMEM;

    size_t copied = 0;
    if (sock->type == SOCKET_TYPE_TCP) {
        while (sock->rx_read_ptr != sock->rx_write_ptr && copied < len) {
            kbuf[copied++] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }
    } else {
        uint8_t ip[4];
        uint16_t port;
        uint16_t payload_len;

        for (int i = 0; i < 4; i++) {
            ip[i] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }
        port = sock->rx_buf[sock->rx_read_ptr];
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        port |= ((uint16_t)sock->rx_buf[sock->rx_read_ptr]) << 8;
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;

        payload_len = sock->rx_buf[sock->rx_read_ptr];
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        payload_len |= ((uint16_t)sock->rx_buf[sock->rx_read_ptr]) << 8;
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;

        copied = (payload_len < len) ? payload_len : len;
        for (size_t i = 0; i < copied; i++) {
            kbuf[i] = sock->rx_buf[sock->rx_read_ptr];
            sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
        }

        if (payload_len > copied) {
            sock->rx_read_ptr = (sock->rx_read_ptr + (payload_len - copied)) % sock->rx_buf_size;
        }

        if (src_addr && addrlen) {
            uint32_t slen;
            if (copy_from_user(&slen, addrlen, sizeof(uint32_t)) == 0) {
                struct sockaddr_in sin;
                memset(&sin, 0, sizeof(sin));
                sin.sin_family = 2; // AF_INET
                sin.sin_port = (port << 8) | (port >> 8);
                memcpy(&sin.sin_addr.s_addr, ip, 4);

                uint32_t copy_len = (slen < sizeof(struct sockaddr_in)) ? slen : sizeof(struct sockaddr_in);
                copy_to_user(src_addr, &sin, copy_len);

                slen = sizeof(struct sockaddr_in);
                copy_to_user(addrlen, &slen, sizeof(uint32_t));
            }
        }
    }

    if (copy_to_user(buf, kbuf, copied) != 0) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return copied;
}
