/*
 * socket_la.h — minimal AF_INET loopback socket layer for LoongArch.
 *
 * Provides TCP (SOCK_STREAM) and UDP (SOCK_DGRAM) over 127.0.0.1.
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
#define LA_NSOCK              32
#define LA_SOCK_RECV_BUF      65536    /* 64 KB per socket */
#define LA_TCP_ACCEPT_BACKLOG  8       /* max pending connections per listener */
#define LA_UDP_DGRAM_MAX       8192    /* max payload per UDP datagram */
#define LA_UDP_DGRAM_QUEUE     8       /* max queued datagrams per socket */
#define LA_AUTO_PORT_BASE      1024    /* first auto-assigned port */

/* ---- Socket types ---- */
#define LA_SOCK_STREAM  1             /* SOCK_STREAM */
#define LA_SOCK_DGRAM   2             /* SOCK_DGRAM  */

/* ---- Socket states ---- */
#define LA_SOCK_CLOSED       0
#define LA_SOCK_LISTEN       1
#define LA_SOCK_ESTABLISHED  2

/* ---- Address family ---- */
#define LA_AF_INET           2

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
    int  type;                       /* LA_SOCK_STREAM / LA_SOCK_DGRAM */
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
    char     recv_buf[LA_SOCK_RECV_BUF];
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

    /* ---- Blocking wait flags ---- */
    int      waiting_recv;
    int      waiting_send;
    int      waiting_accept;
};

/* ---- Public API ---- */
void la_socket_init(void);
int  la_sock_socket(int domain, int type, int protocol);
int  la_sock_bind(int idx, uint32_t addr, uint16_t port);
int  la_sock_listen(int idx, int backlog);
int  la_sock_connect(int idx, uint32_t addr, uint16_t port);
int  la_sock_accept(int idx, uint32_t *uaddr, uint16_t *uport);
int  la_sock_send(int idx, const void *buf, uint32_t len);
int  la_sock_recv(int idx, void *buf, uint32_t len);
void la_sock_close(int idx);
int  la_sock_sendto(int idx, const void *buf, uint32_t len,
                    uint32_t addr, uint16_t port);
int  la_sock_recvfrom(int idx, void *buf, uint32_t len,
                      uint32_t *uaddr, uint16_t *uport);
int  la_sock_getname(int idx, uint32_t *uaddr, uint16_t *uport, int peer);

#endif
