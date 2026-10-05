/*
 * B-System BTRON3 — tv.c
 * Terminal Vision Editor & Viewer (TV)
 * NASA JPL Power of Ten compliant:
 *   - Strictly bounded loops and memory (no heap allocation during edit)
 *   - Multilingual CJK and Asian language support via lang.c
 *   - Word wrap with Kinsoku rules and wrap mode cycling (None/Word/Char)
 *   - Termios traffic optimization via diff-flushed cell screen (term.c)
 *   - Cleanroom VFS file I/O within B-System (no host OS portaling)
 */
#include "tv.h"

enum {
    STYLE_DEF = 0,
    STYLE_HEADER,
    STYLE_TEXT,
    STYLE_HIGHLIGHT,
    STYLE_MENU,
    STYLE_MENU_SEL,
    STYLE_FOOTER,
    STYLE_ACCENT
};

typedef struct {
    char   data[TV_MAX_LINE_BYTES];
    size_t len;
} TvLine;

static TvLine g_lines[TV_MAX_LINES];
static size_t g_line_count = 0;
static char   g_filename[VFS_MAX_PATH];
static int    g_view_mode = 0;
static int    g_insert_mode = 1;
static int    g_modified = 0;
static int    g_wrap_mode = LANG_WRAP_WORD;
static size_t g_cur_line = 0;
static size_t g_cur_byte = 0;
static int    g_scroll_y = 0;
static int    g_scroll_x = 0;

static void tv_setup_styles(void)
{
    term_style(STYLE_DEF,       "0");
    term_style(STYLE_HEADER,    "1;37;44");
    term_style(STYLE_TEXT,      "1;96;104");
    term_style(STYLE_HIGHLIGHT, "1;30;47");
    term_style(STYLE_MENU,      "90;106");
    term_style(STYLE_MENU_SEL,  "90;47");
    term_style(STYLE_FOOTER,    "37;44");
    term_style(STYLE_ACCENT,    "1;33;44");
}

static void tv_reset_buffer(void)
{
    g_line_count = 1;
    g_lines[0].len = 0;
    g_lines[0].data[0] = '\0';
    g_cur_line = 0;
    g_cur_byte = 0;
    g_scroll_y = 0;
    g_scroll_x = 0;
    g_modified = 0;
}

static void tv_load_file(const char *path)
{
    tv_reset_buffer();
    if (path == NULL || path[0] == '\0') {
        g_filename[0] = '\0';
        return;
    }

    size_t plen = strlen(path);
    if (plen >= sizeof(g_filename)) plen = sizeof(g_filename) - 1;
    memcpy(g_filename, path, plen);
    g_filename[plen] = '\0';

    static char chunk[TV_MAX_LINES * 64];
    size_t file_bytes = 0;
    if (vfs_read_file(path, chunk, sizeof(chunk) - 1, &file_bytes) != 0 || file_bytes == 0) {
        return;
    }
    chunk[file_bytes] = '\0';

    g_line_count = 0;
    size_t pos = 0;
    while (pos < file_bytes && g_line_count < TV_MAX_LINES) {
        size_t next_nl = pos;
        while (next_nl < file_bytes && chunk[next_nl] != '\n' && chunk[next_nl] != '\r') {
            next_nl++;
        }

        size_t lbytes = next_nl - pos;
        if (lbytes >= TV_MAX_LINE_BYTES) lbytes = TV_MAX_LINE_BYTES - 1;

        TvLine *tl = &g_lines[g_line_count++];
        memcpy(tl->data, chunk + pos, lbytes);
        tl->data[lbytes] = '\0';
        tl->len = lbytes;

        pos = next_nl;
        if (pos < file_bytes && chunk[pos] == '\r') pos++;
        if (pos < file_bytes && chunk[pos] == '\n') pos++;
    }

    if (g_line_count == 0) {
        g_line_count = 1;
        g_lines[0].len = 0;
        g_lines[0].data[0] = '\0';
    }
}

static int tv_save_file(void)
{
    if (g_filename[0] == '\0' || g_view_mode) return 0;

    static char chunk[TV_MAX_LINES * 64];
    size_t offset = 0;

    for (size_t i = 0; i < g_line_count && offset < sizeof(chunk) - 2; i++) {
        size_t llen = g_lines[i].len;
        if (offset + llen + 1 >= sizeof(chunk)) {
            llen = sizeof(chunk) - offset - 2;
        }
        memcpy(chunk + offset, g_lines[i].data, llen);
        offset += llen;
        chunk[offset++] = '\n';
    }
    chunk[offset] = '\0';

    int r = vfs_write_file(g_filename, chunk, offset);
    if (r == 0) g_modified = 0;
    return r;
}

static void tv_draw_header(void)
{
    scr_fill(0, 0, term_cols, ' ', STYLE_HEADER);
    scr_str(0, 0, " TV ", STYLE_ACCENT);

    char title[128];
    const char *fname = (g_filename[0] != '\0') ? g_filename : "[New File]";
    const char *vstr = g_view_mode ? "[VIEW]" : (g_insert_mode ? "[INS]" : "[REP]");
    const char *wstr = (g_wrap_mode == LANG_WRAP_WORD) ? "WORD" :
                       ((g_wrap_mode == LANG_WRAP_CHAR) ? "CHAR" : "OFF");

    (void)snprintf(title, sizeof(title), " %s  %s%s [WRAP:%s]",
                   fname, vstr, g_modified ? " [+]" : "", wstr);
    scr_str(0, 5, title, STYLE_HEADER);
}

static void tv_draw_footer(void)
{
    int r = term_rows - 1;
    scr_fill(r, 0, term_cols, ' ', STYLE_FOOTER);
    scr_str(r, 0, " F1 Help  F3 View  F4 Edit  F5 Wrap  F10 Exit", STYLE_FOOTER);
}

static void tv_draw_text(void)
{
    int text_rows = term_rows - 2;
    if (text_rows < 1) text_rows = 1;

    for (int row = 0; row < text_rows; row++) {
        int screen_r = row + 1;
        size_t line_idx = (size_t)(g_scroll_y + row);

        scr_fill(screen_r, 0, term_cols, ' ', STYLE_TEXT);

        if (line_idx < g_line_count) {
            TvLine *l = &g_lines[line_idx];
            if (g_wrap_mode == LANG_WRAP_NONE) {
                (void)scr_text(screen_r, 0, term_cols, l->data, l->len, STYLE_TEXT, g_scroll_x);
            } else {
                size_t wlen = lang_wrap(l->data, l->len, term_cols, g_wrap_mode);
                (void)scr_text(screen_r, 0, term_cols, l->data, wlen, STYLE_TEXT, 0);
            }
        }
    }
}

static void tv_update_cursor(void)
{
    if (g_view_mode) {
        scr_cursor(1, 0, 0);
        return;
    }

    if (g_cur_line >= g_line_count) g_cur_line = g_line_count > 0 ? g_line_count - 1 : 0;
    TvLine *l = &g_lines[g_cur_line];
    if (g_cur_byte > l->len) g_cur_byte = l->len;

    int text_rows = term_rows - 2;
    if (text_rows < 1) text_rows = 1;

    if ((int)g_cur_line < g_scroll_y) {
        g_scroll_y = (int)g_cur_line;
    } else if ((int)g_cur_line >= g_scroll_y + text_rows) {
        g_scroll_y = (int)g_cur_line - text_rows + 1;
    }

    int disp_col = lang_cols(l->data, g_cur_byte);

    if (g_wrap_mode == LANG_WRAP_NONE) {
        if (disp_col < g_scroll_x) {
            g_scroll_x = disp_col;
        } else if (disp_col >= g_scroll_x + term_cols) {
            g_scroll_x = disp_col - term_cols + 1;
        }
    } else {
        g_scroll_x = 0;
    }

    int screen_r = (int)(g_cur_line - g_scroll_y) + 1;
    int screen_c = disp_col - g_scroll_x;
    if (screen_c < 0) screen_c = 0;
    if (screen_c >= term_cols) screen_c = term_cols - 1;

    scr_cursor(screen_r, screen_c, 1);
}

static void tv_insert_char(uint32_t cp)
{
    if (g_view_mode) return;
    if (g_cur_line >= g_line_count) g_cur_line = g_line_count > 0 ? g_line_count - 1 : 0;
    TvLine *l = &g_lines[g_cur_line];

    if (cp == '\n') {
        if (g_line_count >= TV_MAX_LINES) return;

        for (size_t i = g_line_count; i > g_cur_line + 1; i--) {
            g_lines[i] = g_lines[i - 1];
        }
        g_line_count++;

        TvLine *nl = &g_lines[g_cur_line + 1];
        size_t tail = l->len - g_cur_byte;
        if (tail > 0) {
            memcpy(nl->data, l->data + g_cur_byte, tail);
        }
        nl->data[tail] = '\0';
        nl->len = tail;

        l->data[g_cur_byte] = '\0';
        l->len = g_cur_byte;

        g_cur_line++;
        g_cur_byte = 0;
        g_modified = 1;
        return;
    }

    char enc[4];
    size_t elen = lang_encode(cp, enc);
    if (l->len + elen >= TV_MAX_LINE_BYTES) return;

    if (g_insert_mode) {
        memmove(l->data + g_cur_byte + elen, l->data + g_cur_byte, l->len - g_cur_byte + 1);
        memcpy(l->data + g_cur_byte, enc, elen);
        l->len += elen;
        g_cur_byte += elen;
    } else {
        size_t nxt = lang_next(l->data, l->len, g_cur_byte);
        size_t old_len = nxt - g_cur_byte;
        if (old_len == 0) old_len = 1;
        memmove(l->data + g_cur_byte + elen, l->data + nxt, l->len - nxt + 1);
        memcpy(l->data + g_cur_byte, enc, elen);
        l->len = l->len - old_len + elen;
        g_cur_byte += elen;
    }
    g_modified = 1;
}

static void tv_delete_char(void)
{
    if (g_view_mode) return;
    if (g_cur_line >= g_line_count) return;
    TvLine *l = &g_lines[g_cur_line];

    if (g_cur_byte < l->len) {
        size_t nxt = lang_cl_next(l->data, l->len, g_cur_byte);
        size_t bytes = nxt - g_cur_byte;
        memmove(l->data + g_cur_byte, l->data + nxt, l->len - nxt + 1);
        l->len -= bytes;
        g_modified = 1;
    } else if (g_cur_line + 1 < g_line_count) {
        TvLine *nl = &g_lines[g_cur_line + 1];
        if (l->len + nl->len < TV_MAX_LINE_BYTES) {
            memcpy(l->data + l->len, nl->data, nl->len + 1);
            l->len += nl->len;
            for (size_t i = g_cur_line + 1; i + 1 < g_line_count; i++) {
                g_lines[i] = g_lines[i + 1];
            }
            g_line_count--;
            g_modified = 1;
        }
    }
}

static void tv_backspace(void)
{
    if (g_view_mode) return;
    if (g_cur_byte > 0) {
        TvLine *l = &g_lines[g_cur_line];
        size_t prv = lang_cl_prev(l->data, l->len, g_cur_byte);
        size_t bytes = g_cur_byte - prv;
        memmove(l->data + prv, l->data + g_cur_byte, l->len - g_cur_byte + 1);
        l->len -= bytes;
        g_cur_byte = prv;
        g_modified = 1;
    } else if (g_cur_line > 0) {
        TvLine *prev = &g_lines[g_cur_line - 1];
        TvLine *curr = &g_lines[g_cur_line];
        if (prev->len + curr->len < TV_MAX_LINE_BYTES) {
            size_t old_len = prev->len;
            memcpy(prev->data + prev->len, curr->data, curr->len + 1);
            prev->len += curr->len;
            for (size_t i = g_cur_line; i + 1 < g_line_count; i++) {
                g_lines[i] = g_lines[i + 1];
            }
            g_line_count--;
            g_cur_line--;
            g_cur_byte = old_len;
            g_modified = 1;
        }
    }
}

static void tv_move_word(int forward)
{
    TvLine *l = &g_lines[g_cur_line];
    if (forward) {
        while (g_cur_byte < l->len) {
            uint32_t cp;
            (void)lang_decode(l->data + g_cur_byte, l->len - g_cur_byte, &cp);
            if (lang_wclass(cp) != LANG_W_SPACE) break;
            g_cur_byte = lang_cl_next(l->data, l->len, g_cur_byte);
        }
        if (g_cur_byte < l->len) {
            uint32_t first_cp;
            (void)lang_decode(l->data + g_cur_byte, l->len - g_cur_byte, &first_cp);
            int cls = lang_wclass(first_cp);
            while (g_cur_byte < l->len) {
                uint32_t cp;
                (void)lang_decode(l->data + g_cur_byte, l->len - g_cur_byte, &cp);
                if (lang_wclass(cp) != cls) break;
                g_cur_byte = lang_cl_next(l->data, l->len, g_cur_byte);
            }
        }
    } else {
        while (g_cur_byte > 0) {
            size_t prv = lang_cl_prev(l->data, l->len, g_cur_byte);
            uint32_t cp;
            (void)lang_decode(l->data + prv, l->len - prv, &cp);
            if (lang_wclass(cp) != LANG_W_SPACE) break;
            g_cur_byte = prv;
        }
        if (g_cur_byte > 0) {
            size_t prv = lang_cl_prev(l->data, l->len, g_cur_byte);
            uint32_t last_cp;
            (void)lang_decode(l->data + prv, l->len - prv, &last_cp);
            int cls = lang_wclass(last_cp);
            while (g_cur_byte > 0) {
                size_t p = lang_cl_prev(l->data, l->len, g_cur_byte);
                uint32_t cp;
                (void)lang_decode(l->data + p, l->len - p, &cp);
                if (lang_wclass(cp) != cls) break;
                g_cur_byte = p;
            }
        }
    }
}

static int tv_exit_dialog(void)
{
    if (!g_modified) return 1;

    int dw = 46, dh = 5;
    int dr = (term_rows - dh) / 2;
    int dc = (term_cols - dw) / 2;
    int sel = 0; /* 0: Save & Exit, 1: Discard, 2: Cancel */

    while (1) {
        for (int i = 0; i < dh; i++) {
            scr_fill(dr + i, dc, dw, ' ', STYLE_MENU);
        }
        scr_str(dr + 1, dc + 4, "File modified. Save before exiting?", STYLE_MENU);

        const char *b0 = "[ Save & Exit ]";
        const char *b1 = "[ Discard ]";
        const char *b2 = "[ Cancel ]";

        scr_str(dr + 3, dc + 3,  b0, sel == 0 ? STYLE_MENU_SEL : STYLE_MENU);
        scr_str(dr + 3, dc + 20, b1, sel == 1 ? STYLE_MENU_SEL : STYLE_MENU);
        scr_str(dr + 3, dc + 33, b2, sel == 2 ? STYLE_MENU_SEL : STYLE_MENU);

        scr_cursor(dr + 3, dc + (sel == 0 ? 4 : (sel == 1 ? 21 : 34)), 1);
        scr_flush();

        int k = term_key();
        if (k == K_LEFT || k == K_BTAB) {
            sel = (sel + 2) % 3;
        } else if (k == K_RIGHT || k == K_TAB) {
            sel = (sel + 1) % 3;
        } else if (k == K_ENTER) {
            if (sel == 0) { (void)tv_save_file(); return 1; }
            if (sel == 1) return 1;
            return 0;
        } else if (k == K_ESC) {
            return 0;
        }
    }
}

int tv_session_init(const char *filepath, int view_only, int rows, int cols)
{
    if (vfs_init() < 0) return -1;
    if (rows > 0 && cols > 0) {
        term_set_size(rows, cols);
    }
    tv_setup_styles();

    g_view_mode = view_only;
    tv_load_file(filepath);

    tv_draw_header();
    tv_draw_text();
    tv_draw_footer();
    tv_update_cursor();
    scr_flush();
    return 0;
}

void tv_session_close(void)
{
    term_close();
}

int tv_session_step(int k)
{
    if (k == K_NONE) return 1;
    if (k == K_RESIZE) {
        scr_invalidate();
        tv_draw_header();
        tv_draw_text();
        tv_draw_footer();
        tv_update_cursor();
        scr_flush();
        return 1;
    }
    if (k == K_EOF) return 0;

    switch (k) {
    case K_F1:
        /* Help */
        break;
    case K_F3:
        g_view_mode = 1;
        break;
    case K_F4:
        g_view_mode = 0;
        break;
    case K_F5:
        g_wrap_mode = (g_wrap_mode + 1) % LANG_WRAP_MODES;
        break;
    case K_F10:
    case K_ESC:
        if (tv_exit_dialog()) return 0;
        break;
    case K_UP:
        if (g_cur_line > 0) {
            g_cur_line--;
            TvLine *l = &g_lines[g_cur_line];
            if (g_cur_byte > l->len) g_cur_byte = l->len;
        }
        break;
    case K_DOWN:
        if (g_cur_line + 1 < g_line_count) {
            g_cur_line++;
            TvLine *l = &g_lines[g_cur_line];
            if (g_cur_byte > l->len) g_cur_byte = l->len;
        }
        break;
    case K_LEFT:
        if (g_cur_byte > 0) {
            TvLine *l = &g_lines[g_cur_line];
            g_cur_byte = lang_cl_prev(l->data, l->len, g_cur_byte);
        } else if (g_cur_line > 0) {
            g_cur_line--;
            g_cur_byte = g_lines[g_cur_line].len;
        }
        break;
    case K_RIGHT: {
        TvLine *l = &g_lines[g_cur_line];
        if (g_cur_byte < l->len) {
            g_cur_byte = lang_cl_next(l->data, l->len, g_cur_byte);
        } else if (g_cur_line + 1 < g_line_count) {
            g_cur_line++;
            g_cur_byte = 0;
        }
        break;
    }
    case K_CTRL_LEFT:
        tv_move_word(0);
        break;
    case K_CTRL_RIGHT:
        tv_move_word(1);
        break;
    case K_HOME:
        g_cur_byte = 0;
        break;
    case K_END:
        g_cur_byte = g_lines[g_cur_line].len;
        break;
    case K_PGUP:
        if (g_cur_line > (size_t)(term_rows - 2)) {
            g_cur_line -= (term_rows - 2);
        } else {
            g_cur_line = 0;
        }
        break;
    case K_PGDOWN:
        g_cur_line += (term_rows - 2);
        if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
        break;
    case K_INSERT:
        g_insert_mode = !g_insert_mode;
        break;
    case K_DELETE:
        tv_delete_char();
        break;
    case K_BACKSPACE:
        tv_backspace();
        break;
    case K_ENTER:
        tv_insert_char('\n');
        break;
    case K_TAB:
        tv_insert_char('\t');
        break;
    default:
        if (k >= 32 && k < K_BASE) {
            tv_insert_char((uint32_t)k);
        }
        break;
    }

    tv_draw_header();
    tv_draw_text();
    tv_draw_footer();
    tv_update_cursor();
    scr_flush();
    return 1;
}

int tv_run(const char *filepath, int view_only)
{
    if (term_open() != 0) return -1;
    if (tv_session_init(filepath, view_only, 0, 0) != 0) {
        term_close();
        return -1;
    }

    while (1) {
        int k = term_key();
        if (k == K_NONE) continue;
        if (!tv_session_step(k)) break;
    }

    tv_session_close();
    return 0;
}


int tv_main(int argc, char *argv[])
{
    const char *f = (argc > 1) ? argv[1] : "/SYS/readme.txt";
    int view = 0;
    if (argc > 2 && strcmp(argv[1], "-v") == 0) {
        view = 1;
        f = argv[2];
    }
    return tv_run(f, view);
}

#ifdef TV_STANDALONE
int main(int argc, char *argv[])
{
    return tv_main(argc, argv);
}
#endif

