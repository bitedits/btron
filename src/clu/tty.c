/*
 * B-System BTRON3 — tty.c
 * Termios-scope terminal layer (see btron/tty.h).
 *
 * Software line discipline over a raw byte device:
 *   ICANON (line editing), ECHO, ICRNL, IXON, VMIN/VTIME.
 * The only target-specific part is BtTtyOps; a POSIX default is built in.
 */
#include <btron/tty.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(_WIN32)
#  define TTY_POSIX 1
#  include <termios.h>
#  include <unistd.h>
#  include <signal.h>
#  include <errno.h>
#  include <stdlib.h>
#  include <sys/ioctl.h>
#  include <sys/select.h>
#else
#  define TTY_POSIX 0
#endif

#define TTY_LINE_MAX 1024u
#define TTY_PEND_MAX 1280u
#define TTY_EXIT_MAX 32u
#define TTY_READ_MAX 4096u   /* hard bound for multi-byte reads */

static BtTtyOps  g_ops;
static int       g_have;
static int       g_raw_on;
static BtTermios g_t = { BT_IXON | BT_ICRNL,
                         BT_ICANON | BT_ECHO | BT_IEXTEN | BT_ISIG, { 1u, 0u } };
static uint8_t   g_pend[TTY_PEND_MAX];
static size_t    g_pend_r, g_pend_w;
static uint8_t   g_line[TTY_LINE_MAX];
static size_t    g_line_n;
static volatile int g_resize;
static char      g_exit_seq[TTY_EXIT_MAX];

/* ── POSIX default device ──────────────────────────────────────────── */
#if TTY_POSIX
static struct termios h_orig;
static int h_saved, h_hooked;

static void h_restore(void)
{
    if (h_saved) (void)tcsetattr(STDIN_FILENO, TCSAFLUSH, &h_orig);
}

static void h_fatal(int sig)            /* async-signal-safe only */
{
    size_t n = 0;
    h_restore();
    while (n < TTY_EXIT_MAX && g_exit_seq[n] != '\0') n++;
    if (n > 0) { ssize_t w = write(STDOUT_FILENO, g_exit_seq, n); (void)w; }
    (void)signal(sig, SIG_DFL);
    (void)raise(sig);
}

static void h_winch(int sig) { (void)sig; g_resize = 1; }

static int h_raw(int on, int isig)
{
    struct termios r;
    if (!h_saved) {
        if (tcgetattr(STDIN_FILENO, &h_orig) != 0) return -1;
        h_saved = 1;
    }
    if (!h_hooked) {
        h_hooked = 1;
        (void)atexit(h_restore);
        (void)signal(SIGWINCH, h_winch);
        (void)signal(SIGTERM, h_fatal);
        (void)signal(SIGHUP, h_fatal);
        (void)signal(SIGINT, h_fatal);
    }
    if (!on) return tcsetattr(STDIN_FILENO, TCSAFLUSH, &h_orig);
    r = h_orig;
    r.c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN | ISIG);
    if (isig) r.c_lflag |= ISIG;
    r.c_iflag &= ~(tcflag_t)(IXON | ICRNL | INLCR | IGNCR);
    r.c_cc[VMIN] = 1;
    r.c_cc[VTIME] = 0;
    return tcsetattr(STDIN_FILENO, TCSAFLUSH, &r);
}

static long h_read(void *buf, size_t n, int timeout_ms)
{
    fd_set fds;
    struct timeval tv, *tp = NULL;
    ssize_t r;
    int s;
    FD_ZERO(&fds);
    FD_SET(STDIN_FILENO, &fds);
    if (timeout_ms >= 0) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        tp = &tv;
    }
    s = select(STDIN_FILENO + 1, &fds, NULL, NULL, tp);
    if (s < 0) return (errno == EINTR) ? 0 : -1;   /* EINTR: let caller see resize */
    if (s == 0) return 0;
    r = read(STDIN_FILENO, buf, n);
    if (r < 0) return (errno == EINTR || errno == EAGAIN) ? 0 : -1;
    return (r == 0) ? -1 : (long)r;                /* 0 bytes = end of input */
}

static long h_write(const void *buf, size_t n)
{
    int tries;
    for (tries = 0; tries < 64; tries++) {
        ssize_t w = write(STDOUT_FILENO, buf, n);
        if (w > 0) return (long)w;
        if (w < 0 && errno != EINTR && errno != EAGAIN) return -1;
    }
    return -1;
}

static int h_winsize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) != 0) return -1;
    if (ws.ws_row == 0 || ws.ws_col == 0) return -1;
    *rows = ws.ws_row;
    *cols = ws.ws_col;
    return 0;
}

static const BtTtyOps k_posix_ops = { h_read, h_write, h_winsize, h_raw };
#endif /* TTY_POSIX */

/* ── Device selection ──────────────────────────────────────────────── */

static const BtTtyOps *dev(void)
{
    if (g_have) return &g_ops;
#if TTY_POSIX
    g_ops = k_posix_ops;
    g_have = 1;
    return &g_ops;
#else
    return NULL;
#endif
}

void bt_tty_set_ops(const BtTtyOps *ops)
{
    g_pend_r = g_pend_w = 0;
    g_line_n = 0;
    g_raw_on = 0;
    if (ops == NULL) { g_have = 0; return; }
    g_ops = *ops;
    g_have = (ops->read != NULL && ops->write != NULL);
}

int bt_tty_available(void) { return dev() != NULL; }

void bt_tty_set_exit_seq(const char *seq)
{
    size_t i = 0;
    while (seq != NULL && i + 1 < TTY_EXIT_MAX && seq[i] != '\0') {
        g_exit_seq[i] = seq[i];
        i++;
    }
    g_exit_seq[i] = '\0';
}

void bt_tty_notify_resize(void) { g_resize = 1; }

int bt_tty_take_resize(void)
{
    int r = g_resize;
    g_resize = 0;
    return r;
}

/* ── termios surface ───────────────────────────────────────────────── */

int bt_tcgetattr(BtTermios *t)
{
    if (t == NULL || dev() == NULL) return -1;
    *t = g_t;
    return 0;
}

int bt_tcsetattr(int action, const BtTermios *t)
{
    const BtTtyOps *d = dev();
    int want_raw;
    if (t == NULL || d == NULL) return -1;
    if (action != BT_TCSANOW && action != BT_TCSAFLUSH) return -1;
    if (action == BT_TCSAFLUSH) { g_pend_r = g_pend_w = 0; g_line_n = 0; }
    g_t = *t;
    if (d->raw == NULL) return 0;
    want_raw = (t->c_lflag & BT_ICANON) ? 0 : 1;
    if (want_raw == g_raw_on) return 0;
    if (d->raw(want_raw, (t->c_lflag & BT_ISIG) ? 1 : 0) != 0) return -1;
    g_raw_on = want_raw;
    return 0;
}

int bt_tcgetwinsize(int *rows, int *cols)
{
    const BtTtyOps *d = dev();
    if (rows == NULL || cols == NULL || d == NULL || d->winsize == NULL) return -1;
    return d->winsize(rows, cols);
}

/* ── Line discipline ───────────────────────────────────────────────── */

static int soft_canon(const BtTtyOps *d)
{
    return (g_t.c_lflag & BT_ICANON) != 0 && d->raw == NULL;
}

/* Apply ICRNL / IXON in place; returns the new length. */
static size_t filter_in(uint8_t *b, size_t n)
{
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == '\r' && (g_t.c_iflag & BT_ICRNL)) c = '\n';
        if ((c == 0x11u || c == 0x13u) && (g_t.c_iflag & BT_IXON)) continue;
        b[o++] = c;
    }
    return o;
}

static void echo(const BtTtyOps *d, const char *s, size_t n)
{
    if (g_t.c_lflag & BT_ECHO) (void)d->write(s, n);
}

/* Remove the last UTF-8 character of the edit line (with echo). */
static void line_erase(const BtTtyOps *d)
{
    if (g_line_n == 0) return;
    do { g_line_n--; } while (g_line_n > 0 && (g_line[g_line_n] & 0xC0u) == 0x80u);
    echo(d, "\b \b", 3);
}

static void pend_put(const uint8_t *b, size_t n)
{
    size_t i;
    for (i = 0; i < n && g_pend_w < TTY_PEND_MAX; i++) g_pend[g_pend_w++] = b[i];
}

static size_t pend_take(void *buf, size_t n)
{
    size_t avail = g_pend_w - g_pend_r, k = (n < avail) ? n : avail, i;
    for (i = 0; i < k; i++) ((uint8_t *)buf)[i] = g_pend[g_pend_r + i];
    g_pend_r += k;
    if (g_pend_r == g_pend_w) g_pend_r = g_pend_w = 0;
    return k;
}

/* Feed one filtered byte to the canonical editor.  Returns 1 when a line
 * (or EOF marker) became available in g_pend. */
static int canon_feed(const BtTtyOps *d, uint8_t c, int *eof)
{
    *eof = 0;
    if (c == '\n') {
        if (g_line_n < TTY_LINE_MAX) g_line[g_line_n++] = '\n';
        echo(d, "\r\n", 2);
        pend_put(g_line, g_line_n);
        g_line_n = 0;
        return 1;
    }
    if (c == 0x7Fu || c == 0x08u) { line_erase(d); return 0; }
    if (c == 0x15u) {                                   /* ^U kill line */
        size_t guard = g_line_n;
        while (g_line_n > 0 && guard-- > 0) line_erase(d);
        return 0;
    }
    if (c == 0x04u) {                                   /* ^D */
        if (g_line_n == 0) { *eof = 1; return 1; }
        pend_put(g_line, g_line_n);
        g_line_n = 0;
        return 1;
    }
    if (g_line_n + 1 < TTY_LINE_MAX) {
        g_line[g_line_n++] = c;
        if (c >= 0x20u || (c & 0x80u)) echo(d, (const char *)&c, 1);
    }
    return 0;
}

static long read_canon(const BtTtyOps *d, void *buf, size_t n)
{
    unsigned guard;
    for (guard = 0; guard < TTY_READ_MAX * 4u; guard++) {
        uint8_t c;
        size_t k;
        int eof, ready;
        long r;
        if (g_pend_r < g_pend_w) return (long)pend_take(buf, n);
        r = d->read(&c, 1, -1);
        if (r < 0) return -1;
        if (r == 0) continue;
        k = filter_in(&c, 1);
        if (k == 0) continue;
        ready = canon_feed(d, c, &eof);
        if (ready && eof) return 0;                      /* POSIX EOF */
    }
    return 0;
}

static long read_raw(const BtTtyOps *d, void *buf, size_t n)
{
    unsigned vmin = g_t.c_cc[BT_VMIN], vtime = g_t.c_cc[BT_VTIME];
    size_t got = 0, want;
    uint8_t *b = (uint8_t *)buf;
    unsigned guard;
    if (n > TTY_READ_MAX) n = TTY_READ_MAX;
    if (g_pend_r < g_pend_w) return (long)pend_take(buf, n);
    if (vmin == 0u) {
        long r = d->read(b, n, vtime == 0u ? 0 : (int)(vtime * 100u));
        if (r <= 0) return r;
        return (long)filter_in(b, (size_t)r);
    }
    want = (vmin < n) ? vmin : n;
    for (guard = 0; guard < TTY_READ_MAX && got < want; guard++) {
        int to = (vtime > 0u && got > 0u) ? (int)(vtime * 100u) : -1;
        long r = d->read(b + got, n - got, to);
        if (r < 0) return got > 0 ? (long)got : -1;
        if (r == 0) { if (vtime > 0u && got > 0u) break; continue; }
        got += filter_in(b + got, (size_t)r);
    }
    return (long)got;
}

long bt_tty_read(void *buf, size_t n)
{
    const BtTtyOps *d = dev();
    if (buf == NULL || d == NULL) return -1;
    if (n == 0) return 0;
    return soft_canon(d) ? read_canon(d, buf, n) : read_raw(d, buf, n);
}

int bt_tty_poll(int timeout_ms)
{
    const BtTtyOps *d = dev();
    uint8_t c;
    long r;
    size_t k;
    if (d == NULL) return -1;
    if (g_pend_r < g_pend_w) return 1;
    r = d->read(&c, 1, timeout_ms);
    if (r < 0) return -1;
    if (r == 0) return 0;
    k = filter_in(&c, 1);
    if (k == 0) return 0;
    pend_put(&c, 1);
    return 1;
}

long bt_tty_write(const void *buf, size_t n)
{
    const BtTtyOps *d = dev();
    const uint8_t *p = (const uint8_t *)buf;
    size_t off = 0;
    if (d == NULL || (buf == NULL && n > 0)) return -1;
    while (off < n) {                                   /* bounded: off grows >=1 */
        long w = d->write(p + off, n - off);
        if (w <= 0) return -1;
        off += (size_t)w;
    }
    return (long)n;
}
