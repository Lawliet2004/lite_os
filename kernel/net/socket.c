#include <sys/syscall.h>
#include <fs/vfs.h>
#include <sched/task.h>
#include <sched/wait_queue.h>
#include <net/net.h>
#include <lib/string.h>
#include <mm/heap.h>
#include <mm/uaccess.h>
#include <kernel/printk.h>

#define AF_UNIX      1
#define AF_INET      2
#define SOCK_STREAM  1
#define SOCK_DGRAM   2
#define SOL_SOCKET   1
#define SO_REUSEADDR 2
#define SO_TYPE      3
#define SO_ERROR     4
#define SO_ACCEPTCONN 30
#define SHUT_RD      0
#define SHUT_WR      1
#define SHUT_RDWR    2
#define MSG_NOSIGNAL 0x4000

struct iovec {
    void *iov_base;
    size_t iov_len;
};

struct msghdr_linux {
    void *msg_name;
    uint32_t msg_namelen;
    uint32_t _pad0;
    struct iovec *msg_iov;
    size_t msg_iovlen;
    void *msg_control;
    size_t msg_controllen;
    unsigned int msg_flags;
};

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

static struct socket *file_socket(struct file *f)
{
    if (!f || !f->node) return 0;
    return (struct socket *)f->node->data;
}

static struct socket *fd_socket(struct process *proc, int fd, int *err)
{
    if (err) *err = -EBADF;
    if (!proc || fd < 0 || fd >= MAX_FILES_PER_PROCESS || proc->files[fd] == 0)
        return 0;
    struct file *f = proc->files[fd];
    if (f->node == 0 || f->node->data == 0) {
        if (err) *err = -ENOTSOCK;
        return 0;
    }
    struct socket *sock = (struct socket *)f->node->data;
    if (!sock->valid) {
        if (err) *err = -EBADF;
        return 0;
    }
    return sock;
}

static int socket_poll(struct vfs_node *node, short events, short *revents)
{
    struct socket *sock = (struct socket *)node->data;
    if (!sock || !sock->valid) return -EBADF;

    short ready = 0;
    bool peer_closed = (sock->domain == AF_UNIX && sock->peer == 0);
    if ((events & 0x0001) && (sock->rx_read_ptr != sock->rx_write_ptr
        || sock->shutdown_write || sock->closed || peer_closed
        || (sock->type == SOCKET_TYPE_TCP && sock->tcp_state == TCP_STATE_LISTEN && sock->accept_count > 0))) {
        ready |= 0x0001;
    }
    if ((events & 0x0004) && !sock->shutdown_write && !sock->closed) {
        if (sock->domain == AF_UNIX) {
            ready |= 0x0004;
        } else if (sock->type == SOCKET_TYPE_TCP) {
            ready |= 0x0004;
        } else if (sock->connected) {
            ready |= 0x0004;
        }
    }
    if ((events & 0x0008) && (sock->closed || sock->so_error != 0)) {
        ready |= 0x0008;
    }
    if ((events & 0x0010) && (sock->closed || sock->shutdown_read || sock->shutdown_write || peer_closed)) {
        ready |= 0x0010;
    }
    if (revents != 0) {
        *revents = ready;
    }
    return ready != 0 ? 1 : 0;
}

static int socket_alloc_common(int domain, int sock_type, int fd_flags, int *out_fd)
{
    if (!current_task || !current_task->process)
        return -EPERM;
    struct process *proc = current_task->process;

    int fd = alloc_fd(proc);
    if (fd < 0) return -EMFILE;

    struct socket *sock = 0;
    for (int i = 0; i < MAX_SOCKETS; i++) {
        if (!socket_table[i].valid) {
            sock = &socket_table[i];
            memset(sock, 0, sizeof(*sock));
            sock->valid = true;
            sock->domain = domain;
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
    wait_queue_init(&sock->wait_q);
    if (sock_type == SOCKET_TYPE_TCP) {
        sock->tcp_state = TCP_STATE_CLOSED;
    }

    struct vfs_node *node = kzalloc(sizeof(struct vfs_node));
    if (!node) {
        kfree(sock->rx_buf);
        sock->rx_buf = 0;
        sock->valid = false;
        return -ENOMEM;
    }
    strcpy(node->name, "socket");
    node->type = VFS_TYPE_CHAR;
    node->data = sock;
    node->read = socket_read;
    node->write = socket_write;
    node->close = socket_close;
    node->poll = socket_poll;

    struct file *f = kzalloc(sizeof(struct file));
    if (!f) {
        kfree(node);
        kfree(sock->rx_buf);
        sock->rx_buf = 0;
        sock->valid = false;
        return -ENOMEM;
    }
    f->node = node;
    f->offset = 0;
    f->flags = O_RDWR;
    f->fd_flags = fd_flags;
    f->ref_count = 1;

    proc->files[fd] = f;
    *out_fd = fd;
    return 0;
}

static int socket_pair_send(struct socket *sock, const void *buf, size_t count, bool nonblock)
{
    if (!sock->peer || sock->shutdown_write || sock->closed)
        return -EPIPE;
    struct socket *peer = sock->peer;
    if (peer->shutdown_read || peer->closed)
        return -EPIPE;

    size_t written = 0;
    const uint8_t *src = (const uint8_t *)buf;
    while (written < count) {
        size_t next = (peer->rx_write_ptr + 1) % peer->rx_buf_size;
        if (next == peer->rx_read_ptr) {
            if (nonblock) {
                return written > 0 ? (int)written : -EAGAIN;
            }
            wait_queue_sleep(&peer->wait_q);
            if (peer->shutdown_read || peer->closed) {
                return written > 0 ? (int)written : -EPIPE;
            }
            continue;
        }
        peer->rx_buf[peer->rx_write_ptr] = src[written++];
        peer->rx_write_ptr = next;
    }
    wait_queue_wake_all(&peer->wait_q);
    io_event_notify();
    return (int)written;
}

static int socket_pair_recv(struct socket *sock, void *buf, size_t count, bool nonblock)
{
    while (sock->rx_read_ptr == sock->rx_write_ptr) {
        if (sock->shutdown_read)
            return 0;
        if (!sock->peer || (sock->peer->shutdown_write || sock->peer->closed))
            return 0;
        if (nonblock)
            return -EAGAIN;
        wait_queue_sleep(&sock->wait_q);
    }

    size_t copied = 0;
    uint8_t *dst = (uint8_t *)buf;
    while (copied < count && sock->rx_read_ptr != sock->rx_write_ptr) {
        dst[copied++] = sock->rx_buf[sock->rx_read_ptr];
        sock->rx_read_ptr = (sock->rx_read_ptr + 1) % sock->rx_buf_size;
    }
    if (sock->peer) {
        wait_queue_wake_all(&sock->peer->wait_q);
    }
    io_event_notify();
    return (int)copied;
}

static int socket_read(struct vfs_node *node, size_t offset, void *buf, size_t count)
{
    (void)offset;
    struct socket *sock = (struct socket *)node->data;
    if (!sock || !sock->valid) return -EBADF;

    if (sock->domain == AF_UNIX) {
        bool nonblock = current_task && (current_task->current_file_flags & O_NONBLOCK);
        return socket_pair_recv(sock, buf, count, nonblock);
    }

    while (sock->rx_read_ptr == sock->rx_write_ptr) {
        if (sock->shutdown_read) {
            return 0;
        }
        if (sock->type == SOCKET_TYPE_TCP && (sock->tcp_state == TCP_STATE_CLOSED || sock->tcp_state == TCP_STATE_CLOSE_WAIT || sock->closed || sock->shutdown_write)) {
            // Check if there is still data in the buffer to read first
            if (sock->rx_read_ptr != sock->rx_write_ptr) {
                break;
            }
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

    if (sock->domain == AF_UNIX) {
        bool nonblock = current_task && (current_task->current_file_flags & O_NONBLOCK);
        return socket_pair_send(sock, buf, count, nonblock);
    }

    if (sock->type == SOCKET_TYPE_TCP) {
        if (sock->shutdown_write) {
            return -EPIPE;
        }
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

    sock->closed = true;
    sock->shutdown_read = true;
    sock->shutdown_write = true;
    wait_queue_wake_all(&sock->wait_q);
    io_event_notify();
    if (sock->peer) {
        sock->peer->peer = 0;
        wait_queue_wake_all(&sock->peer->wait_q);
        io_event_notify();
    }

    if (sock->type == SOCKET_TYPE_TCP) {
        if (sock->tcp_state == TCP_STATE_ESTABLISHED || sock->tcp_state == TCP_STATE_SYN_RECEIVED) {
            sock->tcp_state = TCP_STATE_FIN_WAIT_1;
            net_tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);

            int timeout = 100;
            while (sock->tcp_state != TCP_STATE_CLOSED && timeout-- > 0) {
                task_sleep_ticks(1);
            }
        } else if (sock->tcp_state == TCP_STATE_CLOSE_WAIT) {
            sock->tcp_state = TCP_STATE_LAST_ACK;
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

    if (domain != AF_INET) return -EAFNOSUPPORT;

    int sock_type = 0;
    if ((type & 0xf) == SOCK_STREAM) {
        sock_type = SOCKET_TYPE_TCP;
    } else if ((type & 0xf) == SOCK_DGRAM) {
        sock_type = SOCKET_TYPE_UDP;
    } else {
        return -EINVAL;
    }

    int fd = -1;
    int rc = socket_alloc_common(domain, sock_type, 0, &fd);
    if (rc < 0) return rc;
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

    if (sock->domain != AF_INET) return -EAFNOSUPPORT;
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

    if (sock->domain != AF_INET) return -EAFNOSUPPORT;
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

    if (sock->domain != AF_INET) return -EAFNOSUPPORT;
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
    child_node->poll = socket_poll;

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

    if (sock->domain != AF_INET) return -EAFNOSUPPORT;
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
    io_event_notify();
    } else {
        sock->connected = true;
        io_event_notify();
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

    if (sock->shutdown_write) return -EPIPE;
    if (!uaccess_ok(buf, len)) return -EFAULT;

    if (sock->domain == AF_UNIX) {
        if (dest_addr != 0) return -EISCONN;
        uint8_t *kbuf = kmalloc(len);
        if (!kbuf) return -ENOMEM;
        if (copy_from_user(kbuf, buf, len) != 0) {
            kfree(kbuf);
            return -EFAULT;
        }
        bool nonblock = f->flags & O_NONBLOCK;
        int rc = socket_pair_send(sock, kbuf, len, nonblock);
        kfree(kbuf);
        return rc;
    }

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

    if (sock->domain == AF_UNIX) {
        uint8_t *kbuf = kmalloc(len);
        if (!kbuf) return -ENOMEM;
        bool nonblock = f->flags & O_NONBLOCK;
        int rc = socket_pair_recv(sock, kbuf, len, nonblock);
        if (rc > 0 && copy_to_user(buf, kbuf, (size_t)rc) != 0) {
            kfree(kbuf);
            return -EFAULT;
        }
        if (src_addr && addrlen) {
            uint32_t slen = 0;
            if (copy_from_user(&slen, addrlen, sizeof(slen)) == 0) {
                struct sockaddr sa;
                memset(&sa, 0, sizeof(sa));
                sa.sa_family = AF_UNIX;
                copy_to_user(src_addr, &sa, slen < sizeof(sa) ? slen : sizeof(sa));
                slen = sizeof(sa);
                copy_to_user(addrlen, &slen, sizeof(slen));
            }
        }
        io_event_notify();
        kfree(kbuf);
        return rc;
    }

    while (sock->rx_read_ptr == sock->rx_write_ptr) {
        if (sock->type == SOCKET_TYPE_TCP && (sock->tcp_state == TCP_STATE_CLOSED || sock->tcp_state == TCP_STATE_CLOSE_WAIT || sock->closed)) {
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
    io_event_notify();
    return copied;
}

int64_t sys_setsockopt(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    int level = (int)frame->rsi;
    int optname = (int)frame->rdx;
    const void *optval = (const void *)frame->r10;
    uint32_t optlen = (uint32_t)frame->r8;

    struct process *proc = current_task ? current_task->process : 0;
    int err = -EBADF;
    struct socket *sock = fd_socket(proc, fd, &err);
    if (!sock) return err;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (!optval || optlen < sizeof(int) || !uaccess_ok(optval, sizeof(int))) return -EINVAL;

    int value = 0;
    if (copy_from_user(&value, optval, sizeof(int)) != 0) return -EFAULT;

    switch (optname) {
    case SO_REUSEADDR:
        sock->so_reuseaddr = value != 0;
        return 0;
    default:
        return -ENOPROTOOPT;
    }
}

int64_t sys_getsockopt(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    int level = (int)frame->rsi;
    int optname = (int)frame->rdx;
    void *optval = (void *)frame->r10;
    uint32_t *optlen = (uint32_t *)frame->r8;

    struct process *proc = current_task ? current_task->process : 0;
    int err = -EBADF;
    struct socket *sock = fd_socket(proc, fd, &err);
    if (!sock) return err;
    if (level != SOL_SOCKET) return -ENOPROTOOPT;
    if (!optval || !optlen) return -EFAULT;

    uint32_t koptlen = 0;
    if (copy_from_user(&koptlen, optlen, sizeof(koptlen)) != 0) return -EFAULT;
    if (koptlen < sizeof(int)) return -EINVAL;

    int value = 0;
    switch (optname) {
    case SO_REUSEADDR:
        value = sock->so_reuseaddr;
        break;
    case SO_TYPE:
        value = (sock->type == SOCKET_TYPE_TCP) ? SOCK_STREAM : SOCK_DGRAM;
        break;
    case SO_ERROR:
        value = sock->so_error;
        sock->so_error = 0;
        break;
    case SO_ACCEPTCONN:
        value = sock->tcp_state == TCP_STATE_LISTEN;
        break;
    default:
        return -ENOPROTOOPT;
    }

    if (copy_to_user(optval, &value, sizeof(value)) != 0) return -EFAULT;
    koptlen = sizeof(value);
    if (copy_to_user(optlen, &koptlen, sizeof(koptlen)) != 0) return -EFAULT;
    return 0;
}

int64_t sys_shutdown(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    int how = (int)frame->rsi;

    struct process *proc = current_task ? current_task->process : 0;
    int err = -EBADF;
    struct socket *sock = fd_socket(proc, fd, &err);
    if (!sock) return err;

    if (how != SHUT_RD && how != SHUT_WR && how != SHUT_RDWR)
        return -EINVAL;
    if (!sock->connected && sock->domain != AF_UNIX && sock->tcp_state != TCP_STATE_LISTEN)
        return -ENOTCONN;

    if (how == SHUT_RD || how == SHUT_RDWR)
        sock->shutdown_read = true;
    if (how == SHUT_WR || how == SHUT_RDWR) {
        sock->shutdown_write = true;
        if (sock->peer) {
            wait_queue_wake_all(&sock->peer->wait_q);
        }
        if (sock->type == SOCKET_TYPE_TCP) {
            if (sock->tcp_state == TCP_STATE_ESTABLISHED || sock->tcp_state == TCP_STATE_SYN_RECEIVED) {
                sock->tcp_state = TCP_STATE_FIN_WAIT_1;
                net_tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
            } else if (sock->tcp_state == TCP_STATE_CLOSE_WAIT) {
                sock->tcp_state = TCP_STATE_LAST_ACK;
                net_tcp_send_segment(sock, TCP_FLAG_FIN | TCP_FLAG_ACK, 0, 0);
            }
        }
    }
    wait_queue_wake_all(&sock->wait_q);
    return 0;
}

int64_t sys_socketpair(struct syscall_frame *frame)
{
    int domain = (int)frame->rdi;
    int type = (int)frame->rsi;
    int protocol = (int)frame->rdx;
    int *sv = (int *)frame->r10;
    (void)protocol;

    if (domain != AF_UNIX) return -EAFNOSUPPORT;
    if ((type & 0xf) != SOCK_STREAM) return -EOPNOTSUPP;
    if (!sv) return -EFAULT;

    int fd0 = -1, fd1 = -1;
    int rc = socket_alloc_common(AF_UNIX, SOCKET_TYPE_TCP, 0, &fd0);
    if (rc < 0) return rc;
    rc = socket_alloc_common(AF_UNIX, SOCKET_TYPE_TCP, 0, &fd1);
    if (rc < 0) {
        struct file *f0 = current_task->process->files[fd0];
        current_task->process->files[fd0] = 0;
        file_close(f0);
        return rc;
    }

    struct file *f0 = current_task->process->files[fd0];
    struct file *f1 = current_task->process->files[fd1];
    struct socket *s0 = file_socket(f0);
    struct socket *s1 = file_socket(f1);

    s0->connected = true;
    s1->connected = true;
    s0->peer = s1;
    s1->peer = s0;

    int out[2] = { fd0, fd1 };
    if (copy_to_user(sv, out, sizeof(out)) != 0) {
        current_task->process->files[fd0] = 0;
        current_task->process->files[fd1] = 0;
        file_close(f0);
        file_close(f1);
        return -EFAULT;
    }
    return 0;
}

int64_t sys_sendmsg(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    const struct msghdr_linux *msg = (const struct msghdr_linux *)frame->rsi;
    if (!current_task || !current_task->process) return -EPERM;
    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS) return -EBADF;
    if (!msg || !uaccess_ok(msg, sizeof(*msg))) return -EFAULT;

    struct msghdr_linux kmsg;
    if (copy_from_user(&kmsg, msg, sizeof(kmsg)) != 0) return -EFAULT;
    if (kmsg.msg_iovlen == 0 || kmsg.msg_iovlen > 64) return -EINVAL;
    if (kmsg.msg_control || kmsg.msg_controllen) return -ENOTSUP;

    struct iovec kiov[16];
    struct iovec *iov = kiov;
    if (kmsg.msg_iovlen > 16) {
        iov = kmalloc(kmsg.msg_iovlen * sizeof(*iov));
        if (!iov) return -ENOMEM;
    }
    if (!uaccess_ok(kmsg.msg_iov, kmsg.msg_iovlen * sizeof(*iov)) ||
        copy_from_user(iov, kmsg.msg_iov, kmsg.msg_iovlen * sizeof(*iov)) != 0) {
        if (iov != kiov) kfree(iov);
        return -EFAULT;
    }

    size_t total = 0;
    for (size_t i = 0; i < kmsg.msg_iovlen; i++) total += iov[i].iov_len;
    uint8_t *buf = kmalloc(total ? total : 1);
    if (!buf) {
        if (iov != kiov) kfree(iov);
        return -ENOMEM;
    }

    size_t off = 0;
    for (size_t i = 0; i < kmsg.msg_iovlen; i++) {
        if (iov[i].iov_len == 0) continue;
        if (!uaccess_ok(iov[i].iov_base, iov[i].iov_len) ||
            copy_from_user(buf + off, iov[i].iov_base, iov[i].iov_len) != 0) {
            if (iov != kiov) kfree(iov);
            kfree(buf);
            return -EFAULT;
        }
        off += iov[i].iov_len;
    }

    struct file *f = current_task->process->files[fd];
    if (!f || !f->node) {
        if (iov != kiov) kfree(iov);
        kfree(buf);
        return -EBADF;
    }
    if (kmsg.msg_name != 0) {
        if (iov != kiov) kfree(iov);
        kfree(buf);
        return -EOPNOTSUPP;
    }

    int64_t ret = f->node->write(f->node, f->offset, buf, total);
    if (ret > 0) {
        f->offset += (size_t)ret;
    }
    if (iov != kiov) kfree(iov);
    kfree(buf);
    return ret;
}

int64_t sys_recvmsg(struct syscall_frame *frame)
{
    int fd = (int)frame->rdi;
    struct msghdr_linux *msg = (struct msghdr_linux *)frame->rsi;
    if (!current_task || !current_task->process) return -EPERM;
    if (fd < 0 || fd >= MAX_FILES_PER_PROCESS) return -EBADF;
    if (!msg || !uaccess_ok(msg, sizeof(*msg))) return -EFAULT;

    struct msghdr_linux kmsg;
    if (copy_from_user(&kmsg, msg, sizeof(kmsg)) != 0) return -EFAULT;
    if (kmsg.msg_iovlen == 0 || kmsg.msg_iovlen > 64) return -EINVAL;
    if (kmsg.msg_control || kmsg.msg_controllen) return -ENOTSUP;

    struct iovec kiov[16];
    struct iovec *iov = kiov;
    if (kmsg.msg_iovlen > 16) {
        iov = kmalloc(kmsg.msg_iovlen * sizeof(*iov));
        if (!iov) return -ENOMEM;
    }
    if (!uaccess_ok(kmsg.msg_iov, kmsg.msg_iovlen * sizeof(*iov)) ||
        copy_from_user(iov, kmsg.msg_iov, kmsg.msg_iovlen * sizeof(*iov)) != 0) {
        if (iov != kiov) kfree(iov);
        return -EFAULT;
    }

    size_t total = 0;
    for (size_t i = 0; i < kmsg.msg_iovlen; i++) total += iov[i].iov_len;
    uint8_t *buf = kmalloc(total ? total : 1);
    if (!buf) {
        if (iov != kiov) kfree(iov);
        return -ENOMEM;
    }

    struct file *f = current_task->process->files[fd];
    if (!f || !f->node) {
        if (iov != kiov) kfree(iov);
        kfree(buf);
        return -EBADF;
    }
    int64_t ret = f->node->read(f->node, f->offset, buf, total);
    if (ret > 0) {
        f->offset += (size_t)ret;
    }
    if (ret > 0) {
        size_t remaining = (size_t)ret;
        size_t off = 0;
        for (size_t i = 0; i < kmsg.msg_iovlen && remaining > 0; i++) {
            size_t chunk = iov[i].iov_len < remaining ? iov[i].iov_len : remaining;
            if (chunk > 0 && copy_to_user(iov[i].iov_base, buf + off, chunk) != 0) {
                ret = -EFAULT;
                break;
            }
            off += chunk;
            remaining -= chunk;
        }
    }
    if (ret >= 0 && kmsg.msg_name != 0 && kmsg.msg_namelen >= sizeof(struct sockaddr)) {
        struct sockaddr sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_family = file_socket(f)->domain;
        if (copy_to_user(kmsg.msg_name, &sa, sizeof(sa)) != 0) {
            ret = -EFAULT;
        }
    }
    kmsg.msg_flags = 0;
    if (copy_to_user(msg, &kmsg, sizeof(kmsg)) != 0 && ret >= 0) {
        ret = -EFAULT;
    }

    if (iov != kiov) kfree(iov);
    kfree(buf);
    return ret;
}
