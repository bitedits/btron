/*
 * v_tcpip.c — BTRON 3.20 TCP/IP Sockets Verification Suite
 *
 * Normative source: b-spec/os_spec/shell/tcpip.html
 */

#include "../btron_verify.h"
#include <btron/tcpip.h>
#include <btron/error.h>

#define S "TCPIP"

void vfy_suite_tcpip(void)
{
    /* ── Structure Sizes ────────────────────────────────────── */
    VFY_ASSERT_TRUE(S, "sizeof(struct sockaddr)>0", sizeof(struct sockaddr) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(struct sockaddr_in)>0", sizeof(struct sockaddr_in) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(struct in_addr)>0", sizeof(struct in_addr) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(struct hostent)>0", sizeof(struct hostent) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(struct servent)>0", sizeof(struct servent) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(fd_set)>0", sizeof(fd_set) > 0);
    VFY_ASSERT_TRUE(S, "sizeof(struct timeval)>0", sizeof(struct timeval) > 0);

    /* ── Constants ──────────────────────────────────────────── */
    VFY_ASSERT_EQ(S, "PF_INET", PF_INET, 2);
    VFY_ASSERT_EQ(S, "AF_INET", AF_INET, 2);
    VFY_ASSERT_EQ(S, "SOCK_STREAM", SOCK_STREAM, 1);
    VFY_ASSERT_EQ(S, "SOCK_DGRAM", SOCK_DGRAM, 2);
    VFY_ASSERT_EQ(S, "SOL_SOCKET", SOL_SOCKET, 0xffff);
    VFY_ASSERT_EQ(S, "TCP_NODELAY", TCP_NODELAY, 0x2001);

    /* ── Behavioral API Verification (Spec Conformance) ─────── */

    /* Subsystem initialization */
    ERR start_er = so_start(0);
    VFY_ASSERT_EQ(S, "so_start()", start_er, E_OK);

    /* Socket creation */
    WERR s = so_socket(PF_INET, SOCK_STREAM, 0);
    VFY_ASSERT_GE(S, "so_socket()", s, 0);

    /* Bind address */
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = 8080;
    ERR bind_er = so_bind(s > 0 ? s : 1, (struct sockaddr*)&addr, sizeof(addr));
    VFY_ASSERT_EQ(S, "so_bind()", bind_er, E_OK);

    /* Listen */
    ERR lis_er = so_listen(s > 0 ? s : 1, 5);
    VFY_ASSERT_EQ(S, "so_listen()", lis_er, E_OK);

    /* Connect */
    ERR conn_er = so_connect(s > 0 ? s : 1, (struct sockaddr*)&addr, sizeof(addr));
    VFY_ASSERT_EQ(S, "so_connect()", conn_er, E_OK);

    /* Send */
    const char msg[] = "GET / HTTP/1.0\r\n\r\n";
    WERR snd_bytes = so_send(s > 0 ? s : 1, (B*)msg, (W)strlen(msg), 0);
    VFY_ASSERT_GE(S, "so_send()", snd_bytes, 0);

    /* Recv */
    char rcv_buf[128];
    memset(rcv_buf, 0, sizeof(rcv_buf));
    WERR rcv_bytes = so_recv(s > 0 ? s : 1, (B*)rcv_buf, sizeof(rcv_buf), 0);
    VFY_ASSERT_GE(S, "so_recv()", rcv_bytes, 0);

    /* Select multiplexing */
    fd_set rfds;
    memset(&rfds, 0, sizeof(rfds));
    struct timeval tv = { 0, 1000 };
    WERR sel_res = so_select((s > 0 ? s : 1) + 1, &rfds, NULL, NULL, &tv);
    VFY_ASSERT_GE(S, "so_select()", sel_res, 0);

    /* DNS Host lookup */
    struct hostent hp;
    char hbuf[256];
    memset(&hp, 0, sizeof(hp));
    ERR dns_er = so_gethostbyname((B*)"localhost", &hp, (B*)hbuf);
    VFY_ASSERT_EQ(S, "so_gethostbyname()", dns_er, E_OK);

    /* Get Hostname */
    char host_name[64];
    memset(host_name, 0, sizeof(host_name));
    ERR hname_er = so_gethostname((B*)host_name, sizeof(host_name));
    VFY_ASSERT_EQ(S, "so_gethostname()", hname_er, E_OK);

    /* Socket close */
    ERR cls_er = so_close(s > 0 ? s : 1);
    VFY_ASSERT_EQ(S, "so_close()", cls_er, E_OK);

    /* Subsystem finish */
    ERR fin_er = so_finish(0);
    VFY_ASSERT_EQ(S, "so_finish()", fin_er, E_OK);
}
