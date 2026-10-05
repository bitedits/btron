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

static TvContext g_default_tv = {
    .insert_mode = 1,
    .wrap_mode = LANG_WRAP_WORD
};
static TvContext *g_tv = &g_default_tv;

#define g_lines       (g_tv->lines)
#define g_line_count  (g_tv->line_count)
#define g_filename    (g_tv->filename)
#define g_view_mode   (g_tv->view_mode)
#define g_insert_mode (g_tv->insert_mode)
#define g_modified    (g_tv->modified)
#define g_wrap_mode   (g_tv->wrap_mode)
#define g_cur_line    (g_tv->cur_line)
#define g_cur_byte    (g_tv->cur_byte)
#define g_scroll_y    (g_tv->scroll_y)
#define g_scroll_sub  (g_tv->scroll_sub)
#define g_scroll_x    (g_tv->scroll_x)
#define g_modal_mode  (g_tv->modal_mode)
#define g_modal_sel   (g_tv->modal_sel)

static char g_tv_clipboard[TV_MAX_LINE_BYTES * 2] = {0};

TvContext *tv_context_create(void)
{
    TvContext *tv = (TvContext *)calloc(1, sizeof(TvContext));
    if (!tv) return NULL;
    tv->insert_mode = 1;
    tv->wrap_mode = LANG_WRAP_WORD;
    return tv;
}

void tv_context_destroy(TvContext *tv)
{
    if (tv && tv != &g_default_tv) {
        if (g_tv == tv) g_tv = &g_default_tv;
        free(tv);
    }
}

void tv_set_context(TvContext *tv)
{
    g_tv = tv ? tv : &g_default_tv;
}

TvContext *tv_get_context(void)
{
    return g_tv;
}

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
    g_scroll_sub = 0;
    g_scroll_x = 0;
    g_modified = 0;
    g_modal_mode = TV_MODAL_NONE;
    g_modal_sel = 0;
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

typedef struct {
    size_t byte_off;
    size_t byte_len;
} TvSubline;

static int tv_get_line_sublines(size_t line_idx, int width, int wrap_mode, TvSubline *subs, int max_subs)
{
    if (line_idx >= g_line_count || max_subs <= 0) return 0;
    TvLine *l = &g_lines[line_idx];
    if (wrap_mode == LANG_WRAP_NONE || l->len == 0 || width <= 4) {
        subs[0].byte_off = 0;
        subs[0].byte_len = l->len;
        return 1;
    }
    int count = 0;
    size_t off = 0;
    while (off < l->len && count < max_subs) {
        size_t wlen = lang_wrap(l->data + off, l->len - off, width, wrap_mode);
        if (wlen == 0) {
            size_t nxt = lang_cl_next(l->data + off, l->len - off, 0);
            wlen = (nxt > 0) ? nxt : 1;
        }
        subs[count].byte_off = off;
        subs[count].byte_len = wlen;
        count++;
        off += wlen;
    }
    if (count == 0) {
        subs[0].byte_off = 0;
        subs[0].byte_len = 0;
        count = 1;
    }
    return count;
}

static int tv_count_remaining_visual_rows(int from_line, int from_sub)
{
    if (from_line >= (int)g_line_count) return 0;
    int count = 0;
    int limit = term_rows;
    for (int l = from_line; l < (int)g_line_count && count <= limit; l++) {
        TvSubline subs[64];
        int nsubs = tv_get_line_sublines((size_t)l, term_cols, g_wrap_mode, subs, 64);
        int start = (l == from_line) ? from_sub : 0;
        if (start < nsubs) {
            count += (nsubs - start);
        }
    }
    return count;
}

static void tv_scroll_down(int count)
{
    int text_rows = term_rows - 2;
    if (text_rows < 1) text_rows = 1;

    while (count-- > 0) {
        if (tv_count_remaining_visual_rows(g_scroll_y, g_scroll_sub) <= text_rows) {
            break;
        }
        TvSubline subs[64];
        int nsubs = tv_get_line_sublines((size_t)g_scroll_y, term_cols, g_wrap_mode, subs, 64);
        if (g_scroll_sub + 1 < nsubs) {
            g_scroll_sub++;
        } else if (g_scroll_y + 1 < (int)g_line_count) {
            g_scroll_y++;
            g_scroll_sub = 0;
        } else {
            break;
        }
    }
    if (g_view_mode) {
        g_cur_line = (size_t)g_scroll_y;
    }
}

static void tv_scroll_up(int count)
{
    while (count-- > 0) {
        if (g_scroll_sub > 0) {
            g_scroll_sub--;
        } else if (g_scroll_y > 0) {
            g_scroll_y--;
            TvSubline subs[64];
            int nsubs = tv_get_line_sublines((size_t)g_scroll_y, term_cols, g_wrap_mode, subs, 64);
            g_scroll_sub = (nsubs > 0) ? (nsubs - 1) : 0;
        } else {
            g_scroll_sub = 0;
            break;
        }
    }
    if (g_view_mode) {
        g_cur_line = (size_t)g_scroll_y;
    }
}

static void tv_draw_footer(void)
{
    int r = term_rows - 1;
    scr_fill(r, 0, term_cols, ' ', STYLE_FOOTER);

    char left_str[80];
    if (g_view_mode) {
        (void)snprintf(left_str, sizeof(left_str), " [VIEW] F1 Help  F4 Edit  F5 Wrap  ^Q/q Exit");
    } else {
        (void)snprintf(left_str, sizeof(left_str), " [EDIT] F1 Help  F3 View  F5 Wrap  ^S Save  ^Q Exit");
    }
    scr_str(r, 0, left_str, STYLE_FOOTER);

    char right_str[64];
    size_t cur_l = g_view_mode ? (size_t)g_scroll_y + 1 : g_cur_line + 1;
    if (cur_l > g_line_count) cur_l = g_line_count;
    if (g_line_count == 0) cur_l = 0;

    int pct = (g_line_count > 0) ? (int)((cur_l * 100) / g_line_count) : 100;
    const char *pos_tag = "";
    if (g_scroll_y == 0 && g_scroll_sub == 0) {
        pos_tag = "Top";
    } else if (tv_count_remaining_visual_rows(g_scroll_y, g_scroll_sub) <= (term_rows - 2)) {
        pos_tag = "End";
    }

    if (pos_tag[0] != '\0') {
        (void)snprintf(right_str, sizeof(right_str), "L:%zu/%zu  C:%zu  [%s] ",
                       cur_l, g_line_count, g_cur_byte, pos_tag);
    } else {
        (void)snprintf(right_str, sizeof(right_str), "L:%zu/%zu  C:%zu  [%d%%] ",
                       cur_l, g_line_count, g_cur_byte, pct);
    }

    int rlen = (int)strlen(right_str);
    int col = term_cols - rlen;
    if (col > (int)strlen(left_str) + 1) {
        scr_str(r, col, right_str, STYLE_FOOTER);
    }
}

static void tv_draw_text(void)
{
    int text_rows = term_rows - 2;
    if (text_rows < 1) text_rows = 1;

    size_t line_idx = (size_t)g_scroll_y;
    int sub_idx = g_scroll_sub;

    for (int row = 0; row < text_rows; row++) {
        int screen_r = row + 1;
        scr_fill(screen_r, 0, term_cols, ' ', STYLE_TEXT);

        if (line_idx < g_line_count) {
            TvSubline subs[64];
            int nsubs = tv_get_line_sublines(line_idx, term_cols, g_wrap_mode, subs, 64);
            if (sub_idx < nsubs) {
                TvLine *l = &g_lines[line_idx];
                size_t off = subs[sub_idx].byte_off;
                size_t len = subs[sub_idx].byte_len;
                if (g_wrap_mode == LANG_WRAP_NONE) {
                    (void)scr_text(screen_r, 0, term_cols, l->data + off, len, STYLE_TEXT, g_scroll_x);
                } else {
                    (void)scr_text(screen_r, 0, term_cols, l->data + off, len, STYLE_TEXT, 0);
                }
                sub_idx++;
                if (sub_idx >= nsubs) {
                    line_idx++;
                    sub_idx = 0;
                }
            } else {
                line_idx++;
                sub_idx = 0;
            }
        }
    }
}

static void tv_update_cursor(void)
{
    if (g_line_count == 0) {
        g_cur_line = 0;
        g_cur_byte = 0;
        g_scroll_y = 0;
        g_scroll_sub = 0;
        scr_cursor(1, 0, g_view_mode ? 0 : 1);
        return;
    }

    if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
    TvLine *l = &g_lines[g_cur_line];
    if (g_cur_byte > l->len) g_cur_byte = l->len;

    int text_rows = term_rows - 2;
    if (text_rows < 1) text_rows = 1;

    if (g_view_mode) {
        if (g_scroll_y < 0) g_scroll_y = 0;
        if (g_scroll_y >= (int)g_line_count) g_scroll_y = (int)g_line_count - 1;
        g_cur_line = (size_t)g_scroll_y;
        scr_cursor(1, 0, 0);
        return;
    }

    TvSubline subs[64];
    int nsubs = tv_get_line_sublines(g_cur_line, term_cols, g_wrap_mode, subs, 64);
    int cur_sub = 0;
    for (int i = 0; i < nsubs; i++) {
        if (g_cur_byte >= subs[i].byte_off && (g_cur_byte <= subs[i].byte_off + subs[i].byte_len || i == nsubs - 1)) {
            cur_sub = i;
            break;
        }
    }

    if ((int)g_cur_line < g_scroll_y || ((int)g_cur_line == g_scroll_y && cur_sub < g_scroll_sub)) {
        g_scroll_y = (int)g_cur_line;
        g_scroll_sub = cur_sub;
    }

    int vdist = 0;
    for (int line = g_scroll_y; line < (int)g_cur_line; line++) {
        TvSubline ls[64];
        int ns = tv_get_line_sublines((size_t)line, term_cols, g_wrap_mode, ls, 64);
        int start = (line == g_scroll_y) ? g_scroll_sub : 0;
        if (start < ns) vdist += (ns - start);
    }
    vdist += (cur_sub - ((int)g_cur_line == g_scroll_y ? g_scroll_sub : 0));

    if (vdist >= text_rows) {
        tv_scroll_down(vdist - text_rows + 1);
        vdist = 0;
        for (int line = g_scroll_y; line < (int)g_cur_line; line++) {
            TvSubline ls[64];
            int ns = tv_get_line_sublines((size_t)line, term_cols, g_wrap_mode, ls, 64);
            int start = (line == g_scroll_y) ? g_scroll_sub : 0;
            if (start < ns) vdist += (ns - start);
        }
        vdist += (cur_sub - ((int)g_cur_line == g_scroll_y ? g_scroll_sub : 0));
    }

    int screen_r = vdist + 1;
    size_t sub_byte_off = subs[cur_sub].byte_off;
    size_t byte_in_sub = (g_cur_byte >= sub_byte_off) ? (g_cur_byte - sub_byte_off) : 0;
    int disp_col = lang_cols(l->data + sub_byte_off, byte_in_sub);

    if (g_wrap_mode == LANG_WRAP_NONE) {
        if (disp_col < g_scroll_x) {
            g_scroll_x = disp_col;
        } else if (disp_col >= g_scroll_x + term_cols) {
            g_scroll_x = disp_col - term_cols + 1;
        }
    } else {
        g_scroll_x = 0;
    }

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
    if (g_line_count == 0) return;
    if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
    TvLine *l = &g_lines[g_cur_line];
    if (forward) {
        if (g_cur_byte >= l->len) {
            if (g_cur_line + 1 < g_line_count) {
                g_cur_line++;
                g_cur_byte = 0;
            }
            return;
        }
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
        if (g_cur_byte == 0) {
            if (g_cur_line > 0) {
                g_cur_line--;
                g_cur_byte = g_lines[g_cur_line].len;
            }
            return;
        }
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

static void tv_copy_current_line(void)
{
    if (g_line_count == 0 || g_cur_line >= g_line_count) return;
    TvLine *l = &g_lines[g_cur_line];
    size_t copy_len = l->len;
    if (copy_len >= sizeof(g_tv_clipboard)) copy_len = sizeof(g_tv_clipboard) - 1;
    memcpy(g_tv_clipboard, l->data, copy_len);
    g_tv_clipboard[copy_len] = '\0';
}

static void tv_cut_current_line(void)
{
    if (g_view_mode || g_line_count == 0 || g_cur_line >= g_line_count) return;
    tv_copy_current_line();
    if (g_line_count == 1) {
        g_lines[0].data[0] = '\0';
        g_lines[0].len = 0;
        g_cur_byte = 0;
        g_modified = 1;
        return;
    }
    for (size_t i = g_cur_line; i + 1 < g_line_count; i++) {
        g_lines[i] = g_lines[i + 1];
    }
    g_line_count--;
    if (g_cur_line >= g_line_count) g_cur_line = g_line_count - 1;
    TvLine *l = &g_lines[g_cur_line];
    if (g_cur_byte > l->len) g_cur_byte = l->len;
    g_modified = 1;
}

static void tv_paste_clipboard(void)
{
    if (g_view_mode || g_tv_clipboard[0] == '\0') return;
    const char *p = g_tv_clipboard;
    while (*p) {
        uint32_t cp;
        size_t n = lang_decode(p, strlen(p), &cp);
        if (n == 0) break;
        tv_insert_char(cp);
        p += n;
    }
}

static void tv_kill_to_eol(void)
{
    if (g_view_mode || g_cur_line >= g_line_count) return;
    TvLine *l = &g_lines[g_cur_line];
    if (g_cur_byte < l->len) {
        size_t kill_len = l->len - g_cur_byte;
        if (kill_len >= sizeof(g_tv_clipboard)) kill_len = sizeof(g_tv_clipboard) - 1;
        memcpy(g_tv_clipboard, l->data + g_cur_byte, kill_len);
        g_tv_clipboard[kill_len] = '\0';
        l->data[g_cur_byte] = '\0';
        l->len = g_cur_byte;
        g_modified = 1;
    } else if (g_cur_line + 1 < g_line_count) {
        tv_delete_char();
    }
}

static void tv_kill_to_sol(void)
{
    if (g_view_mode || g_cur_line >= g_line_count || g_cur_byte == 0) return;
    TvLine *l = &g_lines[g_cur_line];
    size_t kill_len = g_cur_byte;
    if (kill_len >= sizeof(g_tv_clipboard)) kill_len = sizeof(g_tv_clipboard) - 1;
    memcpy(g_tv_clipboard, l->data, kill_len);
    g_tv_clipboard[kill_len] = '\0';
    memmove(l->data, l->data + g_cur_byte, l->len - g_cur_byte + 1);
    l->len -= g_cur_byte;
    g_cur_byte = 0;
    g_modified = 1;
}

static void tv_draw_help_dialog(void)
{
    int dw = 52, dh = 11;
    int dr = (term_rows - dh) / 2;
    int dc = (term_cols - dw) / 2;
    if (dr < 0) dr = 0;
    if (dc < 0) dc = 0;

    for (int i = 0; i < dh; i++) {
        scr_fill(dr + i, dc, dw, ' ', STYLE_MENU);
    }
    scr_str(dr + 1, dc + 14, "TV - Terminal Vision Help", STYLE_MENU_SEL);
    scr_str(dr + 3, dc + 3, "^S / Cmd+S : Save       ^Q / Cmd+Q : Quit", STYLE_MENU);
    scr_str(dr + 4, dc + 3, "^W / Cmd+W : Wrap Mode  ^N / Cmd+N : New File", STYLE_MENU);
    scr_str(dr + 5, dc + 3, "^O / Cmd+O : Reload     ^C/^X/^V   : Copy/Cut/Paste", STYLE_MENU);
    scr_str(dr + 6, dc + 3, "Opt+Left/Right: Word    Cmd+Up/Dn  : File Top/End", STYLE_MENU);
    scr_str(dr + 7, dc + 3, "F1: Help   F3: View     F4: Edit   F5: Wrap Mode", STYLE_MENU);
    scr_str(dr + 9, dc + 12, "[ Press ESC or ENTER to Close ]", STYLE_MENU_SEL);

    scr_cursor(dr + 9, dc + 13, 1);
    scr_flush();
}

static void tv_draw_exit_dialog(int sel)
{
    int dw = 46, dh = 5;
    int dr = (term_rows - dh) / 2;
    int dc = (term_cols - dw) / 2;
    if (dr < 0) dr = 0;
    if (dc < 0) dc = 0;

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

    if (g_modal_mode == TV_MODAL_EXIT) {
        if (k == K_RESIZE) {
            scr_invalidate();
            tv_draw_header();
            tv_draw_text();
            tv_draw_footer();
            tv_draw_exit_dialog(g_modal_sel);
            return 1;
        }
        if (k == K_LEFT || k == K_BTAB || k == K_UP) {
            g_modal_sel = (g_modal_sel + 2) % 3;
            tv_draw_exit_dialog(g_modal_sel);
            return 1;
        }
        if (k == K_RIGHT || k == K_TAB || k == K_DOWN) {
            g_modal_sel = (g_modal_sel + 1) % 3;
            tv_draw_exit_dialog(g_modal_sel);
            return 1;
        }
        if (k == K_ESC) {
            g_modal_mode = TV_MODAL_NONE;
            tv_draw_header();
            tv_draw_text();
            tv_draw_footer();
            tv_update_cursor();
            scr_flush();
            return 1;
        }
        if (k == K_ENTER) {
            if (g_modal_sel == 0) {
                (void)tv_save_file();
                return 0; /* Save & Exit */
            } else if (g_modal_sel == 1) {
                return 0; /* Discard & Exit */
            } else {
                g_modal_mode = TV_MODAL_NONE;
                tv_draw_header();
                tv_draw_text();
                tv_draw_footer();
                tv_update_cursor();
                scr_flush();
                return 1;
            }
        }
        return 1;
    }

    if (g_modal_mode == TV_MODAL_HELP) {
        if (k == K_RESIZE) {
            scr_invalidate();
            tv_draw_header();
            tv_draw_text();
            tv_draw_footer();
            tv_draw_help_dialog();
            return 1;
        }
        if (k == K_ESC || k == K_ENTER || k == K_F1 || k == 'q' || k == ' ') {
            g_modal_mode = TV_MODAL_NONE;
            tv_draw_header();
            tv_draw_text();
            tv_draw_footer();
            tv_update_cursor();
            scr_flush();
            return 1;
        }
        return 1;
    }

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
        g_modal_mode = TV_MODAL_HELP;
        tv_draw_help_dialog();
        return 1;

    case K_F3:
        g_view_mode = 1;
        break;

    case K_F4:
        g_view_mode = 0;
        break;

    case K_F5:
    case K_CTRL('W'):
        g_wrap_mode = (g_wrap_mode + 1) % LANG_WRAP_MODES;
        g_scroll_sub = 0;
        break;

    case K_F10:
    case K_CTRL('Q'):
        if (!g_modified || g_view_mode) {
            return 0;
        }
        g_modal_mode = TV_MODAL_EXIT;
        g_modal_sel = 0;
        tv_draw_exit_dialog(g_modal_sel);
        return 1;

    case K_ESC:
        if (g_view_mode) {
            return 0;
        }
        if (!g_modified) {
            return 0;
        }
        g_modal_mode = TV_MODAL_EXIT;
        g_modal_sel = 0;
        tv_draw_exit_dialog(g_modal_sel);
        return 1;

    case K_CTRL('S'):
        (void)tv_save_file();
        break;

    case K_CTRL('N'):
        if (!g_view_mode) {
            tv_reset_buffer();
        }
        break;

    case K_CTRL('O'):
        if (g_filename[0] != '\0') {
            tv_load_file(g_filename);
        }
        break;

    case K_CTRL('C'):
        tv_copy_current_line();
        break;

    case K_CTRL('X'):
        tv_cut_current_line();
        break;

    case K_CTRL('V'):
        tv_paste_clipboard();
        break;

    case K_CTRL('K'):
        tv_kill_to_eol();
        break;

    case K_CTRL('U'):
        if (g_view_mode) {
            int text_rows = term_rows - 2;
            tv_scroll_up(text_rows > 2 ? text_rows / 2 : 1);
        } else {
            tv_kill_to_sol();
        }
        break;

    case K_CTRL('D'):
        if (g_view_mode) {
            int text_rows = term_rows - 2;
            tv_scroll_down(text_rows > 2 ? text_rows / 2 : 1);
        } else {
            tv_delete_char();
        }
        break;

    case K_CTRL('A'):
        if (g_view_mode) {
            g_scroll_y = 0;
            g_scroll_sub = 0;
            g_cur_line = 0;
            g_cur_byte = 0;
        } else {
            g_cur_byte = 0;
        }
        break;

    case K_CTRL('E'):
        if (g_view_mode) {
            tv_scroll_down(TV_MAX_LINES * 2);
        } else {
            g_cur_byte = g_lines[g_cur_line].len;
        }
        break;

    case K_CTRL_LEFT:
        tv_move_word(0);
        break;

    case K_CTRL_RIGHT:
        tv_move_word(1);
        break;

    case K_CTRL_UP:
        g_scroll_y = 0;
        g_scroll_sub = 0;
        g_cur_line = 0;
        g_cur_byte = 0;
        break;

    case K_CTRL_DOWN:
        if (g_view_mode) {
            tv_scroll_down(TV_MAX_LINES * 2);
        } else {
            g_cur_line = (g_line_count > 0) ? (g_line_count - 1) : 0;
            g_cur_byte = g_lines[g_cur_line].len;
        }
        break;

    case K_HOME:
        if (g_view_mode) {
            g_scroll_y = 0;
            g_scroll_sub = 0;
            g_cur_line = 0;
            g_cur_byte = 0;
        } else {
            g_cur_byte = 0;
        }
        break;

    case K_END:
        if (g_view_mode) {
            tv_scroll_down(TV_MAX_LINES * 2);
        } else {
            g_cur_byte = g_lines[g_cur_line].len;
        }
        break;

    case K_UP:
        if (g_view_mode) {
            tv_scroll_up(1);
        } else {
            if (g_cur_line > 0) {
                g_cur_line--;
                TvLine *l = &g_lines[g_cur_line];
                if (g_cur_byte > l->len) g_cur_byte = l->len;
            }
        }
        break;

    case K_DOWN:
        if (g_view_mode) {
            tv_scroll_down(1);
        } else {
            if (g_cur_line + 1 < g_line_count) {
                g_cur_line++;
                TvLine *l = &g_lines[g_cur_line];
                if (g_cur_byte > l->len) g_cur_byte = l->len;
            }
        }
        break;

    case K_LEFT:
        if (g_view_mode) {
            if (g_wrap_mode == LANG_WRAP_NONE) {
                g_scroll_x = (g_scroll_x > 4) ? g_scroll_x - 4 : 0;
            }
        } else {
            if (g_cur_byte > 0) {
                TvLine *l = &g_lines[g_cur_line];
                g_cur_byte = lang_cl_prev(l->data, l->len, g_cur_byte);
            } else if (g_cur_line > 0) {
                g_cur_line--;
                g_cur_byte = g_lines[g_cur_line].len;
            }
        }
        break;

    case K_RIGHT:
        if (g_view_mode) {
            if (g_wrap_mode == LANG_WRAP_NONE) {
                g_scroll_x += 4;
            }
        } else {
            TvLine *l = &g_lines[g_cur_line];
            if (g_cur_byte < l->len) {
                g_cur_byte = lang_cl_next(l->data, l->len, g_cur_byte);
            } else if (g_cur_line + 1 < g_line_count) {
                g_cur_line++;
                g_cur_byte = 0;
            }
        }
        break;

    case K_PGUP: {
        int text_rows = term_rows - 2;
        int step = (text_rows > 3) ? (text_rows - 3) : 1;
        tv_scroll_up(step);
        if (!g_view_mode) {
            if (g_cur_line > (size_t)step) g_cur_line -= step;
            else g_cur_line = 0;
        }
        break;
    }

    case K_PGDOWN: {
        int text_rows = term_rows - 2;
        int step = (text_rows > 3) ? (text_rows - 3) : 1;
        tv_scroll_down(step);
        if (!g_view_mode) {
            g_cur_line += step;
            if (g_cur_line >= g_line_count) g_cur_line = (g_line_count > 0) ? (g_line_count - 1) : 0;
        }
        break;
    }

    case K_INSERT:
        if (!g_view_mode) g_insert_mode = !g_insert_mode;
        break;

    case K_DELETE:
        if (!g_view_mode) tv_delete_char();
        break;

    case K_BACKSPACE:
        if (!g_view_mode) tv_backspace();
        break;

    case K_ENTER:
        if (g_view_mode) {
            tv_scroll_down(1);
        } else {
            tv_insert_char('\n');
        }
        break;

    case K_TAB:
        if (!g_view_mode) tv_insert_char('\t');
        break;

    default:
        if (g_view_mode) {
            if (k == 'j') tv_scroll_down(1);
            else if (k == 'k') tv_scroll_up(1);
            else if (k == ' ' || k == 'f') {
                int text_rows = term_rows - 2;
                tv_scroll_down(text_rows > 3 ? text_rows - 3 : 1);
            } else if (k == 'b') {
                int text_rows = term_rows - 2;
                tv_scroll_up(text_rows > 3 ? text_rows - 3 : 1);
            } else if (k == 'd') {
                int text_rows = term_rows - 2;
                tv_scroll_down(text_rows > 2 ? text_rows / 2 : 1);
            } else if (k == 'u') {
                int text_rows = term_rows - 2;
                tv_scroll_up(text_rows > 2 ? text_rows / 2 : 1);
            } else if (k == 'g') {
                g_scroll_y = 0;
                g_scroll_sub = 0;
                g_cur_line = 0;
                g_cur_byte = 0;
            } else if (k == 'G') {
                tv_scroll_down(TV_MAX_LINES * 2);
            } else if (k == 'q') {
                return 0;
            } else if (k == 'i' || k == 'e') {
                g_view_mode = 0;
            }
        } else if (k >= 32 && k < K_BASE) {
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

