/*
 * socket_la.h — minimal AF_INET loopback socket layer for LoongArch.
 *
 * Provides TCP (SOCK_STREAM), UDP (SOCK_DGRAM), and a minimal raw-socket
 * compatibility surface over 127.0.0.1.
 * No real network stack — data is delivered directly between socket
 * buffers on the same host.  This is sufficient for iperf/netperf
 * loopback benchmarks and cyclictest networking.
 *
 * TCP: three-way handshake is implicit (connect creates a connected
 *      socket pair instantly; accept dequeues it).  Flow control via
 *      circular receive buffers.
 * UDP: datagram queue with source-address recording.
 */

#ifndef SEAOS_LOONGARCH_SOCKET_H
#define SEAOS_LOONGARCH_SOCKET_H

#include <stdint.h>

/* ---- Configuration ---- */
#define LA_NSOCK              512
#define LA_SOCK_RECV_BUF      4096     /* one demand-allocated page per stream */
#define LA_TCP_ACCEPT_BACKLOG  8       /* max pending connections per listener */
#define LA_UDP_DGRAM_MAX       8192    /* max payload per UDP datagram */
#define LA_UDP_DGRAM_QUEUE     4       /* max queued datagrams per socket */
#define LA_AUTO_PORT_BASE      1024    /* first auto-assigned port */

/* ---- Socket types ---- */
#define LA_SOCK_STREAM  1             /* SOCK_STREAM */
#define LA_SOCK_DGRAM   2             /* SOCK_DGRAM  */
#define LA_SOCK_RAW     3             /* SOCK_RAW    */
#define LA_SOCK_SEQPACKET 5           /* SOCK_SEQPACKET */

/* ---- Socket states ---- */
#define LA_SOCK_CLOSED       0
#define LA_SOCK_LISTEN       1
#define LA_SOCK_ESTABLISHED  2

/* ---- Address family ---- */
#define LA_AF_UNIX           1
#define LA_AF_INET           2
#define LA_AF_INET6          10
#define LA_AF_NETLINK        16
#define LA_AF_PACKET         17

/* Minimal IPv6 ancillary receive-option state. */
#define LA_IPV6_RECVOPT_PKTINFO       (1U << 0)
#define LA_IPV6_RECVOPT_HOPLIMIT      (1U << 1)
#define LA_IPV6_RECVOPT_RTHDR         (1U << 2)
#define LA_IPV6_RECVOPT_HOPOPTS       (1U << 3)
#define LA_IPV6_RECVOPT_DSTOPTS       (1U << 4)
#define LA_IPV6_RECVOPT_TCLASS        (1U << 5)
#define LA_IPV6_RECVOPT_2292PKTINFO   (1U << 6)
#define LA_IPV6_RECVOPT_2292HOPLIMIT  (1U << 7)
#define LA_IPV6_RECVOPT_2292RTHDR     (1U << 8)
#define LA_IPV6_RECVOPT_2292HOPOPTS   (1U << 9)
#define LA_IPV6_RECVOPT_2292DSTOPTS   (1U << 10)

/* ---- UDP datagram (queued per socket) ---- */
struct la_udp_dgram {
    char     data[LA_UDP_DGRAM_MAX];
    uint16_t len;
    uint16_t src_port;               /* network byte order */
    uint32_t src_addr;               /* network byte order */
};

/* ---- Socket object ---- */
struct la_socket {
    int  used;
    int  refs;                       /* open fd references across fork */
    int  domain;                     /* LA_AF_* */
    int  type;                       /* LA_SOCK_STREAM / LA_SOCK_DGRAM / LA_SOCK_RAW */
    int  protocol;                   /* original socket protocol */
    int  raw_checksum_offset;         /* IPV6_CHECKSUM offset, -1 = disabled */
    int  ipv6_recvpktinfo;            /* IPV6_RECVPKTINFO enabled flag */
    uint32_t ipv6_recvopts;           /* LA_IPV6_RECVOPT_* bitmask */
    uint32_t icmp6_filter[8];         /* ICMP6_FILTER bitmap: bit 1 blocks */
    int  state;                      /* LA_SOCK_* */

    /* Local address */
    uint32_t laddr;                  /* network byte order */
    uint16_t lport;                  /* network byte order */

    /* Remote address (connected sockets) */
    uint32_t raddr;
    uint16_t rport;

    /* ---- TCP connected-pair ---- */
    struct la_socket *peer;          /* the other end (NULL for listeners) */

    /* ---- TCP receive buffer (circular) ---- */
    char    *recv_buf;
    uint32_t recv_head;              /* next byte to read (mod RECV_BUF) */
    uint32_t recv_tail;              /* next byte to write (mod RECV_BUF) */
    uint32_t recv_total;             /* total bytes readable (recv_tail - recv_head) */
    int      recv_eof;               /* 1 = remote side closed write direction */

    /* ---- TCP listen queue ---- */
    int      backlog;
    int      accept_cnt;
    struct la_socket *accept_q[LA_TCP_ACCEPT_BACKLOG];

    /* ---- UDP datagram queue ---- */
    struct la_udp_dgram dgram_q[LA_UDP_DGRAM_QUEUE];
    int      dgram_head;             /* oldest datagram index */
    int      dgram_cnt;              /* number of queued datagrams */
    int      mcast_joined;           /* minimal SOL_IP multicast membership */
    int      unix_bound;             /* minimal AF_UNIX pathname bind state */
    char     unix_path[256];

    /* ---- Blocking wait flags ---- */
    int      waiting_recv;
    int      waiting_send;
    int      waiting_accept;
};

/* ---- Public API ---- */
void la_socket_init(void);
int  la_sock_socket(int domain, int type, int protocol);
int  la_sock_bind(int idx, uint32_t addr, uint16_t port);
int  la_sock_bind_unix(int idx, const char *path);
int  la_sock_unix_bound(int idx);
int  la_sock_listen(int idx, int backlog);
int  la_sock_connect(int idx, uint32_t addr, uint16_t port);
int  la_sock_accept(int idx, uint32_t *uaddr, uint16_t *uport);
int  la_sock_send(int idx, const void *buf, uint32_t len);
int  la_sock_recv(int idx, void *buf, uint32_t len);
void la_sock_dup(int idx);
void la_sock_close(int idx);
void la_sock_connect_pair(int a, int b);
int  la_sock_alloc(void);
int  la_sock_sendto(int idx, const void *buf, uint32_t len,
                    uint32_t addr, uint16_t port);
int  la_sock_recvfrom(int idx, void *buf, uint32_t len,
                      uint32_t *uaddr, uint16_t *uport);
int  la_sock_getname(int idx, uint32_t *uaddr, uint16_t *uport, int peer);
int  la_sock_readable(int idx);
int  la_sock_writable(int idx);
int  la_sock_type(int idx);
int  la_sock_domain(int idx);
int  la_sock_set_ipv6_checksum(int idx, int offset);
int  la_sock_set_ipv6_recvpktinfo(int idx, int enabled);
int  la_sock_get_ipv6_recvpktinfo(int idx, int *enabled);
int  la_sock_set_ipv6_recvopt(int idx, uint32_t optbit, int enabled);
int  la_sock_get_ipv6_recvopt(int idx, uint32_t optbit, int *enabled);
uint32_t la_sock_get_ipv6_recvopts(int idx);
int  la_sock_set_icmp6_filter(int idx, const uint32_t *filter_words);
int  la_sock_mcast_join(int idx);
int  la_sock_mcast_leave(int idx);

#endif
