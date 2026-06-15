#ifndef LITENIX_NET_NET_H
#define LITENIX_NET_NET_H

#include <stdint.h>
#include <stdbool.h>

#define ETH_ADDR_LEN 6
#define IP_ADDR_LEN  4

/* Ethernet Header */
struct eth_hdr {
    uint8_t  dest[ETH_ADDR_LEN];
    uint8_t  src[ETH_ADDR_LEN];
    uint16_t type;
} __attribute__((packed));

#define ETH_TYPE_ARP  0x0806
#define ETH_TYPE_IPV4 0x0800

/* ARP Header */
struct arp_hdr {
    uint16_t hw_type;
    uint16_t proto_type;
    uint8_t  hw_len;
    uint8_t  proto_len;
    uint16_t opcode;
    uint8_t  sender_mac[ETH_ADDR_LEN];
    uint8_t  sender_ip[IP_ADDR_LEN];
    uint8_t  target_mac[ETH_ADDR_LEN];
    uint8_t  target_ip[IP_ADDR_LEN];
} __attribute__((packed));

#define ARP_OP_REQUEST 1
#define ARP_OP_REPLY   2

/* IPv4 Header */
struct ipv4_hdr {
    uint8_t  ver_ihl;
    uint8_t  tos;
    uint16_t len;
    uint16_t id;
    uint16_t flags_frag;
    uint8_t  ttl;
    uint8_t  proto;
    uint16_t csum;
    uint8_t  src[IP_ADDR_LEN];
    uint8_t  dest[IP_ADDR_LEN];
} __attribute__((packed));

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP  6
#define IP_PROTO_UDP  17

/* ICMP Header */
struct icmp_hdr {
    uint8_t  type;
    uint8_t  code;
    uint16_t csum;
    uint16_t id;
    uint16_t seq;
} __attribute__((packed));

#define ICMP_TYPE_ECHO_REPLY   0
#define ICMP_TYPE_ECHO_REQUEST 8

/* UDP Header */
struct udp_hdr {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t csum;
} __attribute__((packed));

/* TCP Header */
struct tcp_hdr {
    uint16_t src_port;
    uint16_t dest_port;
    uint32_t seq;
    uint32_t ack;
    uint8_t  data_offset; // upper 4 bits
    uint8_t  flags;
    uint16_t window;
    uint16_t csum;
    uint16_t urgent;
} __attribute__((packed));

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#include <sched/wait_queue.h>
#include <kernel/spinlock.h>

#define MAX_SOCKETS 64

/* ponytail: single coarse-grained lock protecting the socket_table array
 * and per-socket state. IRQ-safe. */
extern spinlock_t socket_table_lock;

#define SOCKET_TYPE_UDP 1
#define SOCKET_TYPE_TCP 2

#define TCP_STATE_CLOSED      0
#define TCP_STATE_LISTEN      1
#define TCP_STATE_SYN_SENT    2
#define TCP_STATE_SYN_RECEIVED 3
#define TCP_STATE_ESTABLISHED 4
#define TCP_STATE_FIN_WAIT_1  5
#define TCP_STATE_FIN_WAIT_2  6
#define TCP_STATE_CLOSE_WAIT  7
#define TCP_STATE_LAST_ACK    8
#define TCP_STATE_TIME_WAIT   9

struct socket {
    bool valid;
    int domain;
    int type;
    uint16_t local_port;
    uint16_t remote_port;
    uint8_t remote_ip[4];

    /* RX ring buffer */
    uint8_t *rx_buf;
    size_t rx_buf_size;
    volatile size_t rx_read_ptr;
    volatile size_t rx_write_ptr;

    /* TCP sequence tracking */
    int tcp_state;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint32_t snd_una;
    uint64_t rtx_time;
    uint32_t rtx_count;

    struct socket *parent_listener;
    struct socket *accept_queue[8];
    int accept_count;

    bool bound;
    bool connected;
    bool closed;
    bool shutdown_read;
    bool shutdown_write;
    int so_error;
    int so_reuseaddr;
    struct socket *peer;

    struct wait_queue wait_q;
};

/* Network API declarations */
void net_init(void);
void net_process_packet(const void *data, uint16_t len);

/* Helper function: checksum */
uint16_t ip_checksum(const void *data, size_t len);

struct sockaddr {
    uint16_t sa_family;
    char     sa_data[14];
};

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr_in {
    uint16_t       sin_family;
    uint16_t       sin_port;
    struct in_addr sin_addr;
    char           sin_zero[8];
};

#endif
