/*
 * BTRON 3.20 TCP/IP Network Manager API
 * include/btron/tcpip.h
 *
 * Normative source: b-spec/os_spec/shell/tcpip.html
 */

#ifndef __BTRON_TCPIP_H__
#define __BTRON_TCPIP_H__

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Protocol and Address Families ────────────────────────────── */
#define PF_UNSPEC       0
#define PF_INET         2

#define AF_UNSPEC       0
#define AF_INET         2

/* ── Socket Types ─────────────────────────────────────────────── */
#define SOCK_STREAM     1   /* Stream socket (TCP) */
#define SOCK_DGRAM      2   /* Datagram socket (UDP) */
#define SOCK_RAW        3   /* Raw IP socket */

/* ── Socket Options (setsockopt / getsockopt) ─────────────────── */
#define SOL_SOCKET      0xffff
#define IPPROTO_TCP     0x0001
#define IPPROTO_IP      0x0002
#define IPPROTO_UDP     0x0011

#define SO_DEBUG        0x0001
#define SO_REUSEADDR    0x0004
#define SO_KEEPALIVE    0x0008
#define SO_DONTROUTE    0x0010
#define SO_BROADCAST    0x0020
#define SO_LINGER       0x0080
#define SO_OOBINLINE    0x0100
#define SO_SNDBUF       0x1001
#define SO_RCVBUF       0x1002
#define SO_ERROR        0x1007
#define SO_TYPE         0x1008

#define TCP_MAXSEG      0x2000
#define TCP_NODELAY     0x2001

#define IP_OPTIONS      0x0001

/* ── Message Flags (send / recv) ──────────────────────────────── */
#define MSG_OOB         0x0001
#define MSG_PEEK        0x0002
#define MSG_DONTROUTE   0x0004

/* ── TCP/IP Error Codes ───────────────────────────────────────── */
#define EX_HOSTUNREACH  ((-401) << 16)
#define EX_TIMEDOUT     ((-402) << 16)
#define EX_CONNABORTED  ((-403) << 16)
#define EX_NOBUFS       ((-404) << 16)
#define EX_BADF         ((-405) << 16)
#define EX_WOULDBLOCK   ((-407) << 16)
#define EX_MSGSIZE      ((-408) << 16)
#define EX_DESTADDRREQ  ((-409) << 16)
#define EX_PROTOTYPE    ((-410) << 16)
#define EX_NOPROTOOPT   ((-411) << 16)
#define EX_PROTONOSUPPORT ((-412) << 16)
#define EX_SOCKTNOSUPPORT ((-413) << 16)
#define EX_OPNOTSUPP    ((-414) << 16)
#define EX_PFNOSUPPORT  ((-415) << 16)
#define EX_AFNOSUPPORT  ((-416) << 16)
#define EX_ADDRINUSE    ((-417) << 16)
#define EX_ADDRNOTAVAIL ((-418) << 16)
#define EX_NETDOWN      ((-419) << 16)
#define EX_NETUNREACH   ((-420) << 16)
#define EX_NETRESET     ((-421) << 16)
#define EX_CONNRESET    ((-422) << 16)
#define EX_ISCONN       ((-423) << 16)
#define EX_NOTCONN      ((-424) << 16)
#define EX_SHUTDOWN     ((-425) << 16)
#define EX_CONNREFUSED  ((-426) << 16)
#define EX_HOSTDOWN     ((-427) << 16)
#define EX_ALREADY      ((-428) << 16)
#define EX_INPROGRESS   ((-429) << 16)

/* ── Structures ───────────────────────────────────────────────── */

#if !defined(_SYS_SOCKET_H_) && !defined(_SYS_SOCKET_H) && !defined(_STRUCT_SOCKADDR)
#define _STRUCT_SOCKADDR
struct sockaddr {
    unsigned short  sa_family;
    char            sa_data[14];
};
#endif

#if !defined(_NETINET_IN_H) && !defined(_NETINET_IN_H_) && !defined(_STRUCT_IN_ADDR)
#define _STRUCT_IN_ADDR
struct in_addr {
    unsigned long   s_addr;
};
#endif

#if !defined(_NETINET_IN_H) && !defined(_NETINET_IN_H_) && !defined(_STRUCT_SOCKADDR_IN)
#define _STRUCT_SOCKADDR_IN
struct sockaddr_in {
    short           sin_family;
    unsigned short  sin_port;
    struct in_addr  sin_addr;
    char            sin_zero[8];
};
#endif

#if !defined(_SYS_UIO_H_) && !defined(_SYS_UIO_H) && !defined(_STRUCT_IOVEC)
#define _STRUCT_IOVEC
struct iovec {
    char            *iov_base;
    int             iov_len;
};
#endif

#if !defined(_SYS_SOCKET_H_) && !defined(_SYS_SOCKET_H) && !defined(_STRUCT_MSGHDR)
#define _STRUCT_MSGHDR
struct msghdr {
    char            *msg_name;
    int             msg_namelen;
    struct iovec    *msg_iov;
    int             msg_iovlen;
    char            *msg_accrights;
    int             msg_accrightslen;
};
#endif

#if !defined(_NETDB_H_) && !defined(_NETDB_H) && !defined(_STRUCT_HOSTENT)
#define _STRUCT_HOSTENT
struct hostent {
    char            *h_name;
    char            **h_aliases;
    int             h_addrtype;
    int             h_length;
    char            **h_addr_list;
#ifndef h_addr
#define h_addr      h_addr_list[0]
#endif
};
#endif

#if !defined(_NETDB_H_) && !defined(_NETDB_H) && !defined(_STRUCT_SERVENT)
#define _STRUCT_SERVENT
struct servent {
    char            *s_name;
    char            **s_aliases;
    int             s_port;
    char            *s_proto;
};
#endif

#if !defined(_SYS_SOCKET_H_) && !defined(_SYS_SOCKET_H) && !defined(_STRUCT_LINGER)
#define _STRUCT_LINGER
struct linger {
    int             l_onoff;
    int             l_linger;
};
#endif

#if defined(BTRON_TARGET) && BTRON_TARGET == 0
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#else
#if !defined(_SYS_SELECT_H_) && !defined(_SYS_SELECT_H) && !defined(_SYS__TYPES__FD_DEF_H) && !defined(_FD_SET) && !defined(_SYS_FD_SET_H_) && !defined(_SYS_FD_SET_H)
#define _SYS_SELECT_H_
#define _SYS_SELECT_H
#define _SYS__TYPES__FD_DEF_H
#define _SYS_FD_SET_H_
#define _SYS_FD_SET_H
#define _FD_SET
#define _FD_SET_DEFINED
#define FD_SETSIZE      256
typedef struct fd_set {
    int             fds_bits[FD_SETSIZE / (sizeof(int) * 8)];
} fd_set;
#endif

#if !defined(_SYS_TIME_H_) && !defined(_SYS_TIME_H) && !defined(_STRUCT_TIMEVAL) && !defined(_SYS__TIMEVAL_H_) && !defined(_SYS_TIMEVAL_H_)
#define _SYS__TIMEVAL_H_
#define _SYS_TIMEVAL_H_
#define _STRUCT_TIMEVAL struct timeval
struct timeval {
    long            tv_sec;
    long            tv_usec;
};
#endif
#endif

/* ── System Calls ─────────────────────────────────────────────── */

ERR  so_start(W arg);
ERR  so_finish(W arg);
WERR so_socket(W domain, W type, W protocol);
ERR  so_bind(W s, struct sockaddr *nam, W namlen);
ERR  so_listen(W s, W backlog);
WERR so_accept(W s, struct sockaddr *nam, W *namlen);
ERR  so_connect(W s, struct sockaddr *nam, W namlen);
WERR so_send(W s, B *buf, W len, W flags);
WERR so_recv(W s, B *buf, W len, W flags);
ERR  so_close(W s);
WERR so_select(W nfds, fd_set *rfds, fd_set *wfds, fd_set *efds, struct timeval *tmout);
ERR  so_gethostbyname(B *nam, struct hostent *hp, B *buf);
ERR  so_gethostname(B *name, W nlen);

#ifdef __cplusplus
}
#endif

#endif /* __BTRON_TCPIP_H__ */
