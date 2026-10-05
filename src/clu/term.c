/*
 * B-System BTRON3 — term.c
 * Key decoder, session control and the diffing cell screen (see term.h).
 */
#include "cluc.h"
#include "term.h"
#include "lang.h"
#include <btron/tty.h>

int term_rows = 24, term_cols = 80;

/* ── Output buffer: one write() per frame in the common case ──────── */

#define OB_MAX 8192u
static uint8_t       g_ob[OB_MAX];
static size_t        g_on;
static unsigned long g_bytes;

static void ob_flush(void)
{
    if (g_on > 0) {
        g_bytes += g_on;
        (void)bt_tty_write(g_ob, g_on);      /* on failure the frame is dropped */
        g_on = 0;
    }
}

static void ob_put(const char *s, size_t n)
{
    if (n > OB_MAX) n = OB_MAX;
    if (g_on + n > OB_MAX) ob_flush();
    memcpy(g_ob + g_on, s, n);
    g_on += n;
}

static void ob_str(const char *s) { ob_put(s, strlen(s)); }

static void ob_num(int v)
{
    char t[12];
    int n = 0;
    unsigned u = (v < 0) ? 0u : (unsigned)v;
    do { t[n++] = (char)('0' + (u % 10u)); u /= 10u; } while (u > 0u && n < 11);
    while (n > 0) { char c = t[--n]; ob_put(&c, 1); }
}

/* ── Key decoding ──────────────────────────────────────────────────── */

static int parse_csi(const uint8_t *b, size_t n, size_t *used)
{
    int p[4] = {0, 0, 0, 0}, np = 0, have = 0;
    size_t i;
    for (i = 2; i < n && i < 14; i++) {
        uint8_t ch = b[i];
        if (ch >= '0' && ch <= '9') {
            if (np < 4 && p[np] < 10000) p[np] = p[np] * 10 + (ch - '0');
            have = 1;
        } else if (ch == ';') {
            if (np < 3) np++;
        } else if (ch >= 0x20 && ch <= 0x2F) {
            continue;
        } else if (ch >= 0x40 && ch <= 0x7E) {
            int mod = (have && np >= 1) ? p[1] : 1;
            *used = i + 1;
            switch (ch) {
            case 'A': return mod == 5 ? K_CTRL_UP : K_UP;
            case 'B': return mod == 5 ? K_CTRL_DOWN : K_DOWN;
            case 'C': return (mod == 5 || mod == 3) ? K_CTRL_RIGHT : mod == 2 ? K_SHIFT_RIGHT : K_RIGHT;
            case 'D': return (mod == 5 || mod == 3) ? K_CTRL_LEFT : mod == 2 ? K_SHIFT_LEFT : K_LEFT;
            case 'H': return K_HOME;
            case 'F': return K_END;
            case 'Z': return K_BTAB;
            case 'P': return K_F1;
            case 'Q': return K_F2;
            case 'R': return K_F3;
            case 'S': return K_F4;
            case '~':
                switch (p[0]) {
                case 1: case 7: return K_HOME;
                case 2: return K_INSERT;
                case 3: return K_DELETE;
                case 4: case 8: return K_END;
                case 5: return K_PGUP;
                case 6: return K_PGDOWN;
                case 11: case 12: case 13: case 14: case 15: return K_F1 + (p[0] - 11);
                case 17: case 18: case 19: case 20: case 21: return K_F6 + (p[0] - 17);
                default: return K_NONE;
                }
            default: return K_NONE;
            }
        } else {
            *used = i;                       /* malformed: drop what we saw */
            return K_NONE;
        }
    }
    if (i >= 14) { *used = n; return K_NONE; }   /* garbage, bounded */
    return -1;
}

int term_parse(const uint8_t *b, size_t n, size_t *used)
{
    uint8_t c;
    if (n == 0) return -1;
    c = b[0];
    *used = 1;
    if (c == 0x1B) {
        if (n == 1) return -1;
        if (b[1] == '[') return parse_csi(b, n, used);
        if (b[1] == 'O') {
            if (n < 3) return -1;
            *used = 3;
            switch (b[2]) {
            case 'A': return K_UP;
            case 'B': return K_DOWN;
            case 'C': return K_RIGHT;
            case 'D': return K_LEFT;
            case 'H': return K_HOME;
            case 'F': return K_END;
            case 'P': return K_F1;
            case 'Q': return K_F2;
            case 'R': return K_F3;
            case 'S': return K_F4;
            default:  return K_NONE;
            }
        }
        if (b[1] == 0x1B) *used = 2;
        return K_ESC;
    }
    if (c >= 0x80u) {
        uint32_t cp;
        size_t need = (c >= 0xF0u) ? 4u : (c >= 0xE0u) ? 3u : (c >= 0xC2u) ? 2u : 1u;
        if (c >= 0xC2u && n < need) return -1;
        *used = lang_decode((const char *)b, n < need ? n : need, &cp);
        return (int)cp;
    }
    if (c == 0x7Fu || c == 0x08u) return K_BACKSPACE;
    if (c == '\t') return K_TAB;
    if (c == '\n' || c == '\r') return K_ENTER;
    if (c >= 1u && c <= 26u) return K_CTRL((int)c);
    if (c < 0x20u) return K_NONE;
    return (int)c;
}

#define KB_MAX 16u
static uint8_t g_kb[KB_MAX];
static size_t  g_kn;

int term_key(void)
{
    unsigned guard;
    if (bt_tty_take_resize()) { (void)term_resize(); return K_RESIZE; }
    if (g_kn == 0) {
        long r = bt_tty_read(g_kb, 1);
        if (r < 0) return K_EOF;
        if (r == 0) return K_NONE;
        g_kn = 1;
    }
    for (guard = 0; guard < KB_MAX; guard++) {
        size_t used = 1, i;
        int k = term_parse(g_kb, g_kn, &used);
        if (k != -1) {
            if (used > g_kn) used = g_kn;
            for (i = used; i < g_kn; i++) g_kb[i - used] = g_kb[i];
            g_kn -= used;
            return k;
        }
        if (g_kn >= KB_MAX || bt_tty_poll(30) <= 0 || bt_tty_read(g_kb + g_kn, 1) <= 0) {
            int lone = (g_kn == 1 && g_kb[0] == 0x1Bu);
            g_kn = 0;
            return lone ? K_ESC : K_NONE;
        }
        g_kn++;
    }
    g_kn = 0;
    return K_NONE;
}

/* ── Screen buffers & Context Management ───────────────────────────── */

typedef struct { uint32_t cp, m1, m2; uint8_t at; } Cell;

#define CELLS ((size_t)TERM_MAXR * TERM_MAXC)

struct TermContext {
    int rows;
    int cols;
    Cell front[CELLS];
    Cell back[CELLS];
    char sgr[TERM_STYLES][32];
    int cur_r;
    int cur_c;
    int cur_at;
    int want_r;
    int want_c;
    int want_vis;
    int cur_vis;
    int need_clear;
    int custom_size;
};

static TermContext g_default_term = {
    .rows = 24,
    .cols = 80,
    .cur_r = -1,
    .cur_c = -1,
    .cur_at = -1,
    .need_clear = 1,
    .custom_size = 0
};
static TermContext *g_term = &g_default_term;

#define g_front (g_term->front)
#define g_back (g_term->back)
#define g_sgr (g_term->sgr)
#define g_cur_r (g_term->cur_r)
#define g_cur_c (g_term->cur_c)
#define g_cur_at (g_term->cur_at)
#define g_want_r (g_term->want_r)
#define g_want_c (g_term->want_c)
#define g_want_vis (g_term->want_vis)
#define g_cur_vis (g_term->cur_vis)
#define g_need_clear (g_term->need_clear)
#define g_custom_size (g_term->custom_size)

TermContext *term_context_create(void)
{
    TermContext *tc = (TermContext *)calloc(1, sizeof(TermContext));
    if (!tc) return NULL;
    tc->rows = 24;
    tc->cols = 80;
    tc->cur_r = -1;
    tc->cur_c = -1;
    tc->cur_at = -1;
    tc->need_clear = 1;
    tc->custom_size = 1;
    for (size_t i = 0; i < CELLS; i++) {
        tc->front[i].cp = ' ';
        tc->back[i].cp = ' ';
    }
    return tc;
}

void term_context_destroy(TermContext *tc)
{
    if (tc && tc != &g_default_term) {
        if (g_term == tc) {
            g_term = &g_default_term;
            term_rows = g_term->rows ? g_term->rows : 24;
            term_cols = g_term->cols ? g_term->cols : 80;
        }
        free(tc);
    }
}

void term_set_context(TermContext *tc)
{
    if (g_term) {
        g_term->rows = term_rows;
        g_term->cols = term_cols;
    }
    g_term = tc ? tc : &g_default_term;
    term_rows = g_term->rows ? g_term->rows : 24;
    term_cols = g_term->cols ? g_term->cols : 80;
}

TermContext *term_get_context(void)
{
    return g_term;
}

#define CELL(buf, r, c) ((buf)[(size_t)(r) * TERM_MAXC + (size_t)(c)])

void term_style(int id, const char *sgr)
{
    size_t i = 0;
    if (id <= 0 || id >= TERM_STYLES || sgr == NULL) return;
    while (i < 31u && sgr[i] != '\0') { g_sgr[id][i] = sgr[i]; i++; }
    g_sgr[id][i] = '\0';
}

static Cell blank(int style)
{
    Cell c;
    c.cp = ' '; c.m1 = 0; c.m2 = 0; c.at = (uint8_t)style;
    return c;
}

void scr_invalidate(void)
{
    size_t i;
    Cell b = blank(0);
    for (i = 0; i < CELLS; i++) g_front[i] = b;
    g_need_clear = 1;
    g_cur_r = g_cur_c = g_cur_at = -1;
}

void scr_clear(int style)
{
    size_t r, c;
    Cell b = blank(style);
    for (r = 0; r < (size_t)term_rows; r++)
        for (c = 0; c < (size_t)term_cols; c++) CELL(g_back, r, c) = b;
}

/* Overwriting [c, c+w) must not leave half of a wide char behind. */
static void break_wide(int r, int c, int w)
{
    if (c > 0 && CELL(g_back, r, c).cp == 0u) CELL(g_back, r, c - 1) = blank(CELL(g_back, r, c - 1).at);
    if (c + w < term_cols && CELL(g_back, r, c + w).cp == 0u)
        CELL(g_back, r, c + w) = blank(CELL(g_back, r, c + w).at);
}

static void put_cell(int r, int c, uint32_t cp, int w, int style)
{
    Cell x;
    break_wide(r, c, w);
    x.cp = cp; x.m1 = 0; x.m2 = 0; x.at = (uint8_t)style;
    CELL(g_back, r, c) = x;
    if (w == 2) { x.cp = 0; CELL(g_back, r, c + 1) = x; }
}

void scr_fill(int row, int col, int w, uint32_t cp, int style)
{
    int i;
    if (row < 0 || row >= term_rows) return;
    for (i = 0; i < w; i++) {
        int c = col + i;
        if (c >= 0 && c < term_cols) put_cell(row, c, cp, 1, style);
    }
}

int scr_text(int row, int col, int maxw, const char *s, size_t len, int style, int skip)
{
    size_t p = 0;
    int dc = 0, out = 0, last = -1;
    if (row < 0 || row >= term_rows || s == NULL) return 0;
    if (col < 0) { maxw += col; col = 0; }
    if (maxw > term_cols - col) maxw = term_cols - col;
    while (p < len && out < maxw) {                  /* bounded by len */
        uint32_t cp;
        size_t n = lang_decode(s + p, len - p, &cp);
        int w, k;
        p += n;
        if (cp >= 0x300u && lang_width(cp) == 0) {   /* combining mark */
            if (last >= 0) {
                Cell *h = &CELL(g_back, row, last);
                if (h->m1 == 0u) h->m1 = cp; else if (h->m2 == 0u) h->m2 = cp;
            }
            continue;
        }
        w = lang_adv(cp, dc);
        if (dc + w <= skip) { dc += w; continue; }   /* left of the window */
        if (dc < skip) {                             /* straddles the edge */
            for (k = 0; k < dc + w - skip && out < maxw; k++, out++)
                put_cell(row, col + out, ' ', 1, style);
            dc += w;
            continue;
        }
        if (cp == '\t') {
            for (k = 0; k < w && out < maxw; k++, out++) put_cell(row, col + out, ' ', 1, style);
            last = -1;
        } else {
            if (cp < 0x20u || cp == 0x7Fu) cp = '?';
            if (out + w > maxw) {                    /* wide char does not fit */
                for (; out < maxw; out++) put_cell(row, col + out, ' ', 1, style);
                break;
            }
            put_cell(row, col + out, cp, w, style);
            last = col + out;
            out += w;
        }
        dc += w;
    }
    return out;
}

int scr_str(int row, int col, const char *s, int style)
{
    return scr_text(row, col, term_cols, s, s ? strlen(s) : 0, style, 0);
}

void scr_cursor(int row, int col, int visible)
{
    g_want_r = row; g_want_c = col; g_want_vis = visible;
}

/* ── Diff flush ────────────────────────────────────────────────────── */

#define GAP 4

static int same(int r, int c)
{
    const Cell *a = &CELL(g_front, r, c), *b = &CELL(g_back, r, c);
    return a->cp == b->cp && a->m1 == b->m1 && a->m2 == b->m2 && a->at == b->at;
}

static void move_to(int r, int c)
{
    if (g_cur_r == r && g_cur_c == c) return;
    if (g_cur_r == r && g_cur_c >= 0 && c > g_cur_c) {
        ob_str("\x1b["); if (c - g_cur_c > 1) ob_num(c - g_cur_c); ob_str("C");
    } else if (g_cur_r == r && g_cur_c >= 0 && c < g_cur_c) {
        ob_str("\x1b["); if (g_cur_c - c > 1) ob_num(g_cur_c - c); ob_str("D");
    } else {
        ob_str("\x1b["); ob_num(r + 1); ob_str(";"); ob_num(c + 1); ob_str("H");
    }
    g_cur_r = r; g_cur_c = c;
}

static void emit_cell(int r, int c)
{
    const Cell *x = &CELL(g_back, r, c);
    char u[4];
    int w;
    if (x->cp == 0u) return;                         /* right half of a wide char */
    if ((int)x->at != g_cur_at) {
        ob_str("\x1b[0;"); ob_str(g_sgr[x->at < TERM_STYLES ? x->at : 0]); ob_str("m");
        g_cur_at = x->at;
    }
    ob_put(u, lang_encode(x->cp, u));
    if (x->m1) ob_put(u, lang_encode(x->m1, u));
    if (x->m2) ob_put(u, lang_encode(x->m2, u));
    w = lang_width(x->cp) == 2 ? 2 : 1;
    g_cur_c += w;
    if (g_cur_c >= term_cols) g_cur_c = -1;          /* pending wrap: position unknown */
    (void)r;
}

static int cell_w(int r, int c)
{
    return (c + 1 < term_cols && CELL(g_back, r, c + 1).cp == 0u &&
            CELL(g_back, r, c).cp != 0u) ? 2 : 1;
}

void scr_flush(void)
{
    int r, c;
    if (g_need_clear) {
        ob_str("\x1b[0m\x1b[2J");
        g_need_clear = 0;
        g_cur_at = 0;
        g_cur_r = g_cur_c = -1;
    }
    for (r = 0; r < term_rows; r++) {
        int dirty = 0;
        c = 0;
        while (c < term_cols) {                      /* bounded by cols */
            int j, stop = 0;
            if (same(r, c)) { c++; continue; }
            dirty = 1;
            if (CELL(g_back, r, c).cp == 0u && c > 0) c--;   /* start at the head */
            move_to(r, c);
            while (!stop) {                          /* bounded: c strictly grows */
                emit_cell(r, c);
                c += cell_w(r, c);
                for (j = c; j < term_cols && j - c <= GAP && same(r, j); j++) { }
                if (j >= term_cols || j - c > GAP) {
                    stop = 1;
                } else {
                    while (c < j) { emit_cell(r, c); c += cell_w(r, c); }
                    if (c >= term_cols) stop = 1;
                }
            }
        }
        if (dirty) {
            for (c = 0; c < term_cols; c++) CELL(g_front, r, c) = CELL(g_back, r, c);
        }
    }
    if (g_want_vis && g_want_r >= 0 && g_want_r < term_rows &&
        g_want_c >= 0 && g_want_c < term_cols) {
        move_to(g_want_r, g_want_c);
        if (!g_cur_vis) { ob_str("\x1b[?25h"); g_cur_vis = 1; }
    } else if (g_cur_vis) {
        ob_str("\x1b[?25l");
        g_cur_vis = 0;
    }
    ob_flush();
}

unsigned long scr_bytes(void) { return g_bytes; }

/* ── Session ───────────────────────────────────────────────────────── */

#define ENTER_SEQ "\x1b[?1049h\x1b[?25l"
#define LEAVE_SEQ "\x1b[0m\x1b[?25h\x1b[?1049l"

static int       g_depth;
static BtTermios g_orig;

int term_resize(void)
{
    if (g_custom_size) {
        scr_invalidate();
        g_cur_vis = 0;
        return 1;
    }
    int r = 24, c = 80, changed;
    if (bt_tcgetwinsize(&r, &c) != 0) { r = term_rows; c = term_cols; }
    r = CLU_CLAMP(r, 4, TERM_MAXR);
    c = CLU_CLAMP(c, 20, TERM_MAXC);
    changed = (r != term_rows || c != term_cols);
    term_rows = r;
    term_cols = c;
    scr_invalidate();
    g_cur_vis = 0;
    return changed;
}

static int raw_on(void)
{
    BtTermios raw;
    if (bt_tcgetattr(&raw) != 0) return -1;
    raw.c_lflag &= ~(uint32_t)(BT_ICANON | BT_ECHO | BT_IEXTEN);
    raw.c_iflag &= ~(uint32_t)BT_IXON;
    raw.c_cc[BT_VMIN] = 0;                   /* 100 ms tick: lets us see SIGWINCH */
    raw.c_cc[BT_VTIME] = 1;
    return bt_tcsetattr(BT_TCSAFLUSH, &raw);
}

int term_open(void)
{
    if (g_depth > 0) { g_depth++; scr_invalidate(); return 0; }
    if (!bt_tty_available() || bt_tcgetattr(&g_orig) != 0) return -1;
    if (raw_on() != 0) return -1;
    bt_tty_set_exit_seq(LEAVE_SEQ);
    g_depth = 1;
    g_kn = 0;
    g_custom_size = 0;
    ob_str(ENTER_SEQ);
    g_cur_vis = 0;
    (void)term_resize();
    return 0;
}

void term_close(void)
{
    if (g_depth == 0) return;
    g_depth--;
    if (g_depth > 0) { scr_invalidate(); return; }
    ob_str(LEAVE_SEQ);
    ob_flush();
    (void)bt_tcsetattr(BT_TCSAFLUSH, &g_orig);
}

void term_suspend(void)
{
    if (g_depth == 0) return;
    ob_str(LEAVE_SEQ);
    ob_flush();
    (void)bt_tcsetattr(BT_TCSAFLUSH, &g_orig);
}

void term_resume(void)
{
    if (g_depth == 0) return;
    (void)raw_on();
    ob_str(ENTER_SEQ);
    g_cur_vis = 0;
    g_kn = 0;
    (void)term_resize();
}

int term_get_cell(int r, int c, uint32_t *cp, int *style)
{
    if (r < 0 || r >= term_rows || c < 0 || c >= term_cols) return 0;
    size_t idx = (size_t)r * TERM_MAXC + (size_t)c;
    if (idx >= CELLS) return 0;
    if (cp) *cp = g_back[idx].cp;
    if (style) *style = (int)g_back[idx].at;
    return 1;
}

void term_set_size(int rows, int cols)
{
    if (rows < 4) rows = 4;
    if (rows > TERM_MAXR) rows = TERM_MAXR;
    if (cols < 20) cols = 20;
    if (cols > TERM_MAXC) cols = TERM_MAXC;
    term_rows = rows;
    term_cols = cols;
    g_custom_size = 1;
    scr_invalidate();
}

void term_get_style_colors(int style, uint32_t *fg, uint32_t *bg)
{
    uint32_t f = 0xFFFFFFFF;
    uint32_t b = 0xFF001A4E; /* Classic SC midnight blue */

    switch (style) {
    case 0: /* DEF / RESET */
        f = 0xFFCCCCCC; b = 0xFF001A4E; break;
    case 1: /* HEADER */
        f = 0xFFFFFF00; b = 0xFF001A4E; break;
    case 2: /* TEXT */
        f = 0xFFFFFFFF; b = 0xFF001A4E; break;
    case 3: /* HIGHLIGHT */
        f = 0xFF000000; b = 0xFF00D2D2; break;
    case 4: /* DIR */
        f = 0xFFFFFF33; b = 0xFF001A4E; break;
    case 5: /* BORDER */
        f = 0xFF00E5E5; b = 0xFF001A4E; break;
    case 6: /* BOTTOM / FOOTER */
        f = 0xFF000000; b = 0xFFCCCCCC; break;
    case 7: /* MENU / ACCENT */
        f = 0xFF000000; b = 0xFF00D2D2; break;
    case 8: /* MENU_SEL */
        f = 0xFF000000; b = 0xFFFFFFFF; break;
    case 9:
        f = 0xFFFFFFFF; b = 0xFF990099; break;
    case 10:
        f = 0xFFFFFF00; b = 0xFF990099; break;
    case 11:
        f = 0xFF000000; b = 0xFFFFFFFF; break;
    default:
        f = 0xFFFFFFFF; b = 0xFF001A4E; break;
    }
    if (fg) *fg = f;
    if (bg) *bg = b;
}

void term_get_cursor(int *row, int *col, int *visible)
{
    if (row) *row = g_want_r;
    if (col) *col = g_want_c;
    if (visible) *visible = g_want_vis;
}

