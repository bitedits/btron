#include <btron/t_editor.h>
/*
 * B-System (BTRON 3.20) BTRON Accessory: TRON CUA Text Editor Window (t_editor)
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/wnd.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/file.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
extern void* Imalloc(size_t sz);
extern void Ifree(void *ptr);
extern void* Icalloc(size_t nmemb, size_t sz);
extern int snprintf(char *str, size_t size, const char *format, ...);
extern void* tkl_memmove(void *dest, const void *src, size_t n);
#define malloc  Imalloc
#define free    Ifree
#define calloc  Icalloc
#define strncpy tkl_strncpy
#define strncat tkl_strncat
#define memset  tkl_memset
#define memcpy  tkl_memcpy
#define memmove tkl_memmove
#define strlen  tkl_strlen
#define strstr  tkl_strstr
#define strcmp  tkl_strcmp

static inline char* local_strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == 0) return (char*)s;
    return (char*)last;
}
#define strrchr local_strrchr
#endif

/* TEditor struct is defined in <btron/t_editor.h> */

static TEditor g_teditor;
static char g_clipboard[2048] = "";

static void teditor_init_default(TEditor *ed) {
    memset(ed, 0, sizeof(TEditor));
    strncpy(ed->filename, "BTRON3_Report.txt", sizeof(ed->filename) - 1);

    const char *initial_doc[] = {
        "件名：【BTRON3仕様の新実装】におけるBTRON環境開発のご報告と情報共有のお願い",
        "宛先：ノルティアオーダー／TADワーキンググループ 小島秀樹様",
        "",
        "突然のご連絡失礼いたします。私は「bitedits」というオープンソース・プロジェクトで開発を行っている者です。",
        "現在、私たちはモダンな仮想化環境（seL4 / VirtIO / SDL2）の上で動作する、",
        "BTRON3（Business TRON）仕様のクリーンルーム実装「BTRON System Plane for OS.1」を開発しております。",
        "",
        "貴団体が長年にわたり超機能分散環境（HFDS）の基盤整備やTAD仕様の策定・維持にご尽力されていることを知り、",
        "私たちの成果をご報告するとともに、BTRONの技術的エコシステムの方々にぜひ共有させていただきたくご連絡いたしました。",
        "プロジェクトのリポジトリ・ドキュメント https://bitedits.github.io/btron/",
        "",
        "私たちの実装では、以下のようなBTRONの特徴的なアーキテクチャをモダンなC99ベースで再現することを目指しています：",
        "・μITRONリアルタイムタスク生成およびカーネルプリミティブのシミュレーション",
        "・Sakamuraグラフィックエンジン（DP.H）に基づく2Dベクトル/ラスタ描画",
        "・実オブジェクト／仮想オブジェクト（Real/Virt Body）のハイパーデータエンジンの再現",
        "・TRONコードによる多言語文字システムとTADセグメントのサポート",
        "",
        "私たちは、BTRONが持つ「直感的で先進的なハイパーデータ構造」という思想を、現代のセキュアなマイクロカーネル環境で復活させたいと考えております。",
        "もしよろしければ、仕様の解釈やTADデータの互換性などについて、貴団体の知見を拝見させていただけますと幸いです。",
        "また、日本のBTRONコミュニティや開発者の方々にご紹介いただけますと大変光栄に思います。",
        "お忙しいところ恐縮ですが、ご一読いただけますと幸いです。何卒よろしくお願い申し上げます。",
        "",
        "プロジェクト開発チーム（Namdak Tonpa Norbu）https://bitedits.github.io/btron/"
    };

    int n = sizeof(initial_doc) / sizeof(initial_doc[0]);
    ed->total_lines = n;
    for (int i = 0; i < n; i++) {
        strncpy(ed->lines[i], initial_doc[i], TEDITOR_MAX_COLS - 1);
    }

    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->wrap_text = FALSE;
    ed->sel_active = FALSE;
    ed->sel_anchor_r = 0;
    ed->sel_anchor_c = 0;
    for (int i = 0; i < 4; i++) ed->tree_hover[i] = -1;
    ed->has_vobj = TRUE;
    strncpy(ed->vobj_name, "Diagram.draw", sizeof(ed->vobj_name) - 1);
    ed->show_line_nums = TRUE;
}

void teditor_get_selection_range(const TEditor *ed, int *r1, int *c1, int *r2, int *c2) {
    if (!ed || !ed->sel_active) {
        if (r1) *r1 = 0; if (c1) *c1 = 0;
        if (r2) *r2 = 0; if (c2) *c2 = 0;
        return;
    }
    int ar = ed->sel_anchor_r, ac = ed->sel_anchor_c;
    int cr = ed->cursor_row, cc = ed->cursor_col;
    if (ar < cr || (ar == cr && ac <= cc)) {
        if (r1) *r1 = ar; if (c1) *c1 = ac;
        if (r2) *r2 = cr; if (c2) *c2 = cc;
    } else {
        if (r1) *r1 = cr; if (c1) *c1 = cc;
        if (r2) *r2 = ar; if (c2) *c2 = ac;
    }
}

static void teditor_ensure_cursor_visible(TEditor *ed) {
    if (ed->cursor_row < ed->scroll_row) {
        ed->scroll_row = ed->cursor_row;
    }
    if (ed->cursor_row >= ed->scroll_row + TEDITOR_VIEW_ROWS) {
        ed->scroll_row = ed->cursor_row - TEDITOR_VIEW_ROWS + 1;
    }
    if (ed->scroll_row < 0) ed->scroll_row = 0;

    if (ed->cursor_col < ed->scroll_col) {
        ed->scroll_col = ed->cursor_col;
    }
    if (ed->cursor_col >= ed->scroll_col + TEDITOR_VIEW_COLS) {
        ed->scroll_col = ed->cursor_col - TEDITOR_VIEW_COLS + 1;
    }
    if (ed->scroll_col < 0) ed->scroll_col = 0;
}

static void teditor_delete_selection(TEditor *ed) {
    if (!ed || !ed->sel_active) return;

    int r1, c1, r2, c2;
    teditor_get_selection_range(ed, &r1, &c1, &r2, &c2);
    if (r1 == r2 && c1 == c2) {
        ed->sel_active = FALSE;
        return;
    }

    if (r1 == r2) {
        int len = (int)strlen(ed->lines[r1]);
        if (c1 < len) {
            int remove_len = (c2 > len ? len : c2) - c1;
            memmove(&ed->lines[r1][c1], &ed->lines[r1][c1 + remove_len], len - (c1 + remove_len) + 1);
        }
    } else {
        /* Multi-line deletion */
        int tail_len = (int)strlen(ed->lines[r2]);
        int keep_c2 = (c2 < tail_len) ? c2 : tail_len;

        /* Append remainder of r2 to r1 */
        strncpy(&ed->lines[r1][c1], &ed->lines[r2][keep_c2], TEDITOR_MAX_COLS - c1 - 1);

        /* Remove lines from r1+1 to r2 */
        int remove_lines = r2 - r1;
        for (int i = r1 + 1; i + remove_lines < ed->total_lines; i++) {
            strncpy(ed->lines[i], ed->lines[i + remove_lines], TEDITOR_MAX_COLS - 1);
        }
        ed->total_lines -= remove_lines;
        if (ed->total_lines < 1) ed->total_lines = 1;
    }

    ed->cursor_row = r1;
    ed->cursor_col = c1;
    ed->sel_active = FALSE;
    ed->sel_anchor_r = r1;
    ed->sel_anchor_c = c1;
    ed->sel_start_r = r1;
    ed->sel_start_c = c1;
    ed->sel_end_r = r1;
    ed->sel_end_c = c1;
    ed->is_modified = TRUE;
}

static void teditor_copy_selection(TEditor *ed) {
    if (!ed) return;
    if (!ed->sel_active) {
        /* Copy current line if no selection */
        strncpy(g_clipboard, ed->lines[ed->cursor_row], sizeof(g_clipboard) - 1);
        return;
    }

    int r1, c1, r2, c2;
    teditor_get_selection_range(ed, &r1, &c1, &r2, &c2);

    g_clipboard[0] = '\0';
    if (r1 == r2) {
        int len = (int)strlen(ed->lines[r1]);
        int end_c = (c2 < len) ? c2 : len;
        if (c1 < end_c) {
            snprintf(g_clipboard, sizeof(g_clipboard), "%.*s", end_c - c1, &ed->lines[r1][c1]);
        }
    } else {
        int pos = 0;
        for (int r = r1; r <= r2 && r < ed->total_lines; r++) {
            const char *str = ed->lines[r];
            int len = (int)strlen(str);
            int start = (r == r1) ? c1 : 0;
            int end = (r == r2) ? ((c2 < len) ? c2 : len) : len;
            if (start < end) {
                int count = end - start;
                if (pos + count < (int)sizeof(g_clipboard) - 2) {
                    strncpy(&g_clipboard[pos], &str[start], count);
                    pos += count;
                }
            }
            if (r < r2 && pos < (int)sizeof(g_clipboard) - 2) {
                g_clipboard[pos++] = '\n';
                g_clipboard[pos] = '\0';
            }
        }
    }
}


static void teditor_paste_clipboard(TEditor *ed) {
    if (strlen(g_clipboard) == 0) return;
    if (ed->sel_active) teditor_delete_selection(ed);

    const char *p = g_clipboard;
    BOOL first = TRUE;
    while (*p) {
        /* Find next line boundary */
        char line[TEDITOR_MAX_COLS];
        int line_len = 0;
        while (*p && *p != '\n' && line_len < TEDITOR_MAX_COLS - 1) {
            line[line_len++] = *p++;
        }
        line[line_len] = '\0';
        if (*p == '\n') p++;

        if (!first) {
            /* Split current line and insert new line */
            if (ed->total_lines < TEDITOR_MAX_LINES - 1) {
                for (int i = ed->total_lines; i > ed->cursor_row + 1; i--) {
                    strncpy(ed->lines[i], ed->lines[i - 1], TEDITOR_MAX_COLS - 1);
                }
                ed->total_lines++;
                ed->cursor_row++;
                strncpy(ed->lines[ed->cursor_row], &ed->lines[ed->cursor_row - 1][ed->cursor_col], TEDITOR_MAX_COLS - 1);
                ed->lines[ed->cursor_row - 1][ed->cursor_col] = '\0';
                ed->cursor_col = 0;
            }
        }

        int insert_len = line_len;
        int cur_len = (int)strlen(ed->lines[ed->cursor_row]);
        if (cur_len + insert_len < TEDITOR_MAX_COLS - 1) {
            memmove(&ed->lines[ed->cursor_row][ed->cursor_col + insert_len],
                    &ed->lines[ed->cursor_row][ed->cursor_col],
                    cur_len - ed->cursor_col + 1);
            memcpy(&ed->lines[ed->cursor_row][ed->cursor_col], line, insert_len);
            ed->cursor_col += insert_len;
        }

        first = FALSE;
    }

    ed->is_modified = TRUE;
    teditor_ensure_cursor_visible(ed);
}

static void teditor_insert_char(TEditor *ed, char ch) {
    if (ed->sel_active) teditor_delete_selection(ed);

    int r = ed->cursor_row;
    int c = ed->cursor_col;
    int len = (int)strlen(ed->lines[r]);

    if (len < TEDITOR_MAX_COLS - 1) {
        memmove(&ed->lines[r][c + 1], &ed->lines[r][c], len - c + 1);
        ed->lines[r][c] = ch;
        ed->cursor_col++;
        ed->is_modified = TRUE;
    }
    teditor_ensure_cursor_visible(ed);
}

static void teditor_insert_text(TEditor *ed, const char *text) {
    if (!text || !*text) return;
    if (ed->sel_active) teditor_delete_selection(ed);

    int r = ed->cursor_row;
    int c = ed->cursor_col;
    int cur_len = (int)strlen(ed->lines[r]);
    int ins_len = (int)strlen(text);

    if (cur_len + ins_len < TEDITOR_MAX_COLS - 1) {
        memmove(&ed->lines[r][c + ins_len], &ed->lines[r][c], cur_len - c + 1);
        memcpy(&ed->lines[r][c], text, ins_len);
        ed->cursor_col += ins_len;
        ed->is_modified = TRUE;
    }
    teditor_ensure_cursor_visible(ed);
}

/* UTF-8 Character Boundary Helpers */
static int utf8_next_offset(const char *s, int cur_offset) {
    if (!s || cur_offset >= (int)strlen(s)) return cur_offset;
    const unsigned char *p = (const unsigned char *)s + cur_offset;
    int len = 1;
    if ((*p & 0x80) == 0) len = 1;
    else if ((*p & 0xE0) == 0xC0) len = 2;
    else if ((*p & 0xF0) == 0xE0) len = 3;
    else if ((*p & 0xF8) == 0xF0) len = 4;
    return cur_offset + len;
}

static int utf8_prev_offset(const char *s, int cur_offset) {
    if (!s || cur_offset <= 0) return 0;
    int pos = cur_offset - 1;
    const unsigned char *p = (const unsigned char *)s;
    while (pos > 0 && (p[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

static int teditor_find_byte_offset_from_x(const char *line, int target_x) {
    if (!line || target_x <= 36) return 0;
    int cur_x = 36;
    int i = 0;
    TC prev_code = 0;
    while (line[i]) {
        int consumed = 0;
        TC code = utf8_to_tc(&line[i], &consumed);
        int step = (consumed > 0 ? consumed : 1);
        int gw = tc_get_char_advance(code, prev_code);
        if (gw > 0 && target_x < cur_x + gw / 2) {
            return i;
        }
        cur_x += gw;
        if (gw > 0 || (code >> 8) != 0x6F) {
            prev_code = code;
        }
        i += step;
    }
    return i;
}

static void teditor_insert_newline(TEditor *ed) {
    if (ed->sel_active) teditor_delete_selection(ed);
    if (ed->total_lines >= TEDITOR_MAX_LINES - 1) return;

    int r = ed->cursor_row;
    int c = ed->cursor_col;

    for (int i = ed->total_lines; i > r + 1; i--) {
        strncpy(ed->lines[i], ed->lines[i - 1], TEDITOR_MAX_COLS - 1);
    }
    ed->total_lines++;

    strncpy(ed->lines[r + 1], &ed->lines[r][c], TEDITOR_MAX_COLS - 1);
    ed->lines[r][c] = '\0';

    ed->cursor_row++;
    ed->cursor_col = 0;
    ed->is_modified = TRUE;
    teditor_ensure_cursor_visible(ed);
}

static void teditor_backspace(TEditor *ed) {
    if (ed->sel_active) {
        teditor_delete_selection(ed);
        return;
    }

    int r = ed->cursor_row;
    int c = ed->cursor_col;

    if (c > 0) {
        int prev_c = utf8_prev_offset(ed->lines[r], c);
        int len = (int)strlen(ed->lines[r]);
        memmove(&ed->lines[r][prev_c], &ed->lines[r][c], len - c + 1);
        ed->cursor_col = prev_c;
        ed->is_modified = TRUE;
    } else if (r > 0) {
        int prev_len = (int)strlen(ed->lines[r - 1]);
        int cur_len = (int)strlen(ed->lines[r]);

        if (prev_len + cur_len < TEDITOR_MAX_COLS - 1) {
            strncat(ed->lines[r - 1], ed->lines[r], TEDITOR_MAX_COLS - prev_len - 1);
            for (int i = r; i < ed->total_lines - 1; i++) {
                strncpy(ed->lines[i], ed->lines[i + 1], TEDITOR_MAX_COLS - 1);
            }
            ed->total_lines--;
            ed->cursor_row--;
            ed->cursor_col = prev_len;
            ed->is_modified = TRUE;
        }
    }
    teditor_ensure_cursor_visible(ed);
}

static void teditor_delete_char_forward(TEditor *ed) {
    if (ed->sel_active) {
        teditor_delete_selection(ed);
        return;
    }

    int r = ed->cursor_row;
    int c = ed->cursor_col;
    int len = (int)strlen(ed->lines[r]);

    if (c < len) {
        int next_c = utf8_next_offset(ed->lines[r], c);
        memmove(&ed->lines[r][c], &ed->lines[r][next_c], len - next_c + 1);
        ed->is_modified = TRUE;
    } else if (r < ed->total_lines - 1) {
        int next_len = (int)strlen(ed->lines[r + 1]);
        if (len + next_len < TEDITOR_MAX_COLS - 1) {
            strncat(ed->lines[r], ed->lines[r + 1], TEDITOR_MAX_COLS - len - 1);
            for (int i = r + 1; i < ed->total_lines - 1; i++) {
                strncpy(ed->lines[i], ed->lines[i + 1], TEDITOR_MAX_COLS - 1);
            }
            ed->total_lines--;
            ed->is_modified = TRUE;
        }
    }
    teditor_ensure_cursor_visible(ed);
}

/* ASCII Key with Shift / CapsLock Translation */
static char get_ascii_char_with_shift(UW key, uint16_t mod) {
    BOOL shift = (mod & BTRON_KMOD_SHIFT) != 0;
    BOOL caps  = (mod & BTRON_KMOD_CAPS) != 0;

    /* If key is already uppercase 'A'..'Z' */
    if (key >= 'A' && key <= 'Z') {
        if (shift ^ caps) return (char)key;
        return (char)(key - 'A' + 'a');
    }

    /* If key is lowercase 'a'..'z' */
    if (key >= 'a' && key <= 'z') {
        if (shift ^ caps) return (char)(key - 'a' + 'A');
        return (char)key;
    }

    /* Shifted punctuation and numbers */
    if (shift) {
        switch (key) {
            case '1': return '!';
            case '2': return '@';
            case '3': return '#';
            case '4': return '$';
            case '5': return '%';
            case '6': return '^';
            case '7': return '&';
            case '8': return '*';
            case '9': return '(';
            case '0': return ')';
            case '-': return '_';
            case '=': return '+';
            case '[': return '{';
            case ']': return '}';
            case '\\': return '|';
            case ';': return ':';
            case '\'': return '"';
            case ',': return '<';
            case '.': return '>';
            case '/': return '?';
            case '`': return '~';
            default: break;
        }
    }
    return (char)key;
}

static void destroy_t_editor(WND *wnd) {
    if (wnd && wnd->user_data) {
        TEditor *ed = (TEditor*)(uintptr_t)wnd->user_data;
        free(ed);
        wnd->user_data = 0;
    }
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_vobj_manager_window(void) {
    return NULL;
}
#else
extern WND* open_vobj_manager_window(void);
#endif

/* Forward declarations */
int teditor_load_file(TEditor *ed, const char *filepath);
void teditor_close_menu(TEditor *ed);

/* ── CUA Movement & Word Jump Helpers ───────────────────────────────── */
static void teditor_move_cursor(TEditor *ed, int new_r, int new_c, BOOL shift) {
    if (!ed) return;
    if (new_r < 0) new_r = 0;
    if (new_r >= ed->total_lines) new_r = ed->total_lines - 1;
    int line_len = (int)strlen(ed->lines[new_r]);
    if (new_c < 0) new_c = 0;
    if (new_c > line_len) new_c = line_len;

    if (shift) {
        if (!ed->sel_active) {
            ed->sel_anchor_r = ed->cursor_row;
            ed->sel_anchor_c = ed->cursor_col;
            ed->sel_active = TRUE;
        }
        ed->cursor_row = new_r;
        ed->cursor_col = new_c;
        if (ed->cursor_row == ed->sel_anchor_r && ed->cursor_col == ed->sel_anchor_c) {
            ed->sel_active = FALSE;
        }
    } else {
        ed->sel_active = FALSE;
        ed->cursor_row = new_r;
        ed->cursor_col = new_c;
        ed->sel_anchor_r = new_r;
        ed->sel_anchor_c = new_c;
    }

    int r1, c1, r2, c2;
    teditor_get_selection_range(ed, &r1, &c1, &r2, &c2);
    ed->sel_start_r = r1; ed->sel_start_c = c1;
    ed->sel_end_r = r2; ed->sel_end_c = c2;
    teditor_ensure_cursor_visible(ed);
}

static int find_word_prev(const char *line, int col) {
    if (!line || col <= 0) return 0;
    int c = col;
    while (c > 0 && (line[c - 1] == ' ' || line[c - 1] == '\t')) c--;
    while (c > 0 && line[c - 1] != ' ' && line[c - 1] != '\t') {
        c = utf8_prev_offset(line, c);
    }
    return c;
}

static int find_word_next(const char *line, int col) {
    if (!line) return 0;
    int len = (int)strlen(line);
    if (col >= len) return len;
    int c = col;
    while (c < len && line[c] != ' ' && line[c] != '\t') {
        c = utf8_next_offset(line, c);
    }
    while (c < len && (line[c] == ' ' || line[c] == '\t')) c++;
    return c;
}

/* ── Word Wrap & Japanese Kinsoku Shori ───────────────────────────────── */
static BOOL is_cjk_no_break_before(TC code) {
    if (code == '.' || code == ',' || code == '!' || code == '?' ||
        code == ':' || code == ';' || code == ')' || code == ']' || code == '}') {
        return TRUE;
    }
    if ((code >> 8) == 0x21) {
        UB low = code & 0xFF;
        if (low >= 0x22 && low <= 0x2A) return TRUE; /* 、 。 ， ． ・ ： ； ？ ！ */
        if (low == 0x3C) return TRUE; /* ー */
        if (low == 0x4B || low == 0x4D || low == 0x4F || low == 0x51 ||
            low == 0x53 || low == 0x55 || low == 0x57 || low == 0x59 || low == 0x5B) {
            return TRUE; /* ） 〕 〉 》 ］ ｝ 」 』 】 */
        }
    }
    return FALSE;
}

static int teditor_wrap_line(const char *utf8_line, int max_w, char wrapped_lines[][TEDITOR_MAX_COLS], int max_wrapped) {
    if (!utf8_line || max_wrapped <= 0) return 0;
    if (utf8_line[0] == '\0') {
        wrapped_lines[0][0] = '\0';
        return 1;
    }
    if (max_w < 40) max_w = 40;

    const char *p = utf8_line;
    const char *line_start = p;
    const char *last_break = NULL;
    H cur_line_w = 0;
    TC prev_code = 0;
    int line_idx = 0;

    while (*p && line_idx < max_wrapped) {
        int consumed = 0;
        TC code = utf8_to_tc(p, &consumed);
        int step = (consumed > 0) ? consumed : 1;
        H adv = tc_get_char_advance(code, prev_code);

        if (*p == ' ') {
            last_break = p;
        } else if (*p == '-' || *p == '/') {
            last_break = p + 1;
        } else if ((code >> 8) == 0x6F && (code & 0xFF) == 0x0B) {
            last_break = p + step;
        } else if (adv == 16) {
            if (prev_code != 0 && !is_cjk_no_break_before(code)) {
                last_break = p;
            }
        }

        if (cur_line_w + adv > max_w && cur_line_w > 0) {
            const char *break_pt = last_break;
            if (!break_pt || break_pt <= line_start) {
                break_pt = p;
            }
            int len = (int)(break_pt - line_start);
            if (len >= TEDITOR_MAX_COLS) len = TEDITOR_MAX_COLS - 1;
            memcpy(wrapped_lines[line_idx], line_start, len);
            wrapped_lines[line_idx][len] = '\0';
            line_idx++;

            if (*break_pt == ' ') break_pt++;
            p = break_pt;
            line_start = p;
            last_break = NULL;
            cur_line_w = 0;
            prev_code = 0;
            continue;
        }

        cur_line_w += adv;
        if (adv > 0 || (code >> 8) != 0x6F) {
            prev_code = code;
        }
        p += step;
    }

    if (line_start && line_idx < max_wrapped) {
        int len = (int)strlen(line_start);
        if (len >= TEDITOR_MAX_COLS) len = TEDITOR_MAX_COLS - 1;
        memcpy(wrapped_lines[line_idx], line_start, len);
        wrapped_lines[line_idx][len] = '\0';
        line_idx++;
    }
    return line_idx;
}

void teditor_toggle_wrap(TEditor *ed) {
    if (!ed) return;
    ed->wrap_text = !ed->wrap_text;
}

/* ── Anders & Volumes Hierarchical Tree Menu Subsystem ────────────────── */
typedef struct {
    const char *label;
    const char *path;
    int child_start;
    int child_count;
    BOOL is_separator;
} TEditorTreeNode;

enum {
    /* Level 1: Open Menu Root Items (indices 0..5) */
    TN_ROOT_SYS = 0,
    TN_ROOT_ANDERS,
    TN_ROOT_SEP,
    TN_ROOT_SUTRA,
    TN_ROOT_REPORT,
    TN_ROOT_FSMD,

    /* Level 2: Under /SYS (indices 6..9) */
    TN_SYS_REPORT,
    TN_SYS_FSMD,
    TN_SYS_SUTRA,
    TN_SYS_HELLO,

    /* Level 2: Under /ANDERS (indices 10..12) */
    TN_ANDERS_BOOK,
    TN_ANDERS_FOUNDATIONS,
    TN_ANDERS_MATHEMATICS,

    /* Level 3: Under foundations (indices 13..16) */
    TN_FND_LOGIC,
    TN_FND_MLTT,
    TN_FND_MODAL,
    TN_FND_UNIVALENT,

    /* Level 3: Under mathematics (indices 17..21) */
    TN_MATH_ALGEBRA,
    TN_MATH_ANALYSIS,
    TN_MATH_CATEGORIES,
    TN_MATH_GEOMETRY,
    TN_MATH_HOMOTOPY,

    /* Level 4: Under foundations/logic (indices 22..28) */
    TN_LOGIC_AWODEY,
    TN_LOGIC_FAVONIA,
    TN_LOGIC_HILBERT,
    TN_LOGIC_HLYVENKO,
    TN_LOGIC_KRAUS,
    TN_LOGIC_MINIMALIST,
    TN_LOGIC_MIPHAM,

    /* Level 4: Under foundations/mltt (indices 29..42) */
    TN_MLTT_BOOL,
    TN_MLTT_EITHER,
    TN_MLTT_FIN,
    TN_MLTT_INDUCTIVE,
    TN_MLTT_LAMBDA,
    TN_MLTT_LIST,
    TN_MLTT_MAYBE,
    TN_MLTT_MLTT,
    TN_MLTT_NAT,
    TN_MLTT_NATW,
    TN_MLTT_PI,
    TN_MLTT_PROTO,
    TN_MLTT_SIGMA,
    TN_MLTT_VEC,

    /* Level 4: Under foundations/modal (indices 43..48) */
    TN_MODAL_FLAT,
    TN_MODAL_INFINITESIMAL,
    TN_MODAL_MODALITY,
    TN_MODAL_SHARP,
    TN_MODAL_STT,
    TN_MODAL_TWISTED,

    /* Level 4: Under foundations/univalent (indices 49..55) */
    TN_UNIV_CARTESIAN,
    TN_UNIV_EQUIV,
    TN_UNIV_EXTENSIONALITY,
    TN_UNIV_HEDBERG,
    TN_UNIV_ISO,
    TN_UNIV_PATH,
    TN_UNIV_PROP,

    /* Level 4: Under mathematics/algebra (indices 56..59) */
    TN_ALG_ALGEBRA,
    TN_ALG_HOMOLOGY,
    TN_ALG_INT,
    TN_ALG_PYTHAGOR,

    /* Level 4: Under mathematics/analysis (indices 60..62) */
    TN_ANA_BOREL,
    TN_ANA_REAL,
    TN_ANA_TOPOLOGY,

    /* Level 4: Under mathematics/categories (indices 63..75) */
    TN_CAT_ABELIAN,
    TN_CAT_ADJUNCTION,
    TN_CAT_CARTESIAN,
    TN_CAT_CAT,
    TN_CAT_CATEGORY,
    TN_CAT_EQUIVALENCE,
    TN_CAT_FUNCTOR,
    TN_CAT_GROUPOID,
    TN_CAT_NATURAL,
    TN_CAT_SYMMETRIC,
    TN_CAT_TOPOS,
    TN_CAT_UNIVERSAL,
    TN_CAT_YONEDA,

    /* Level 4: Under mathematics/geometry (indices 76..79) */
    TN_GEO_BUNDLE,
    TN_GEO_ETALE,
    TN_GEO_FORMALDISC,
    TN_GEO_KREIN,

    /* Level 4: Under mathematics/homotopy (indices 80..99) */
    TN_HOM_KG1,
    TN_HOM_KGN,
    TN_HOM_S1,
    TN_HOM_SN,
    TN_HOM_SNW,
    TN_HOM_COEQUALIZER,
    TN_HOM_COLIM,
    TN_HOM_CONSTCUBES,
    TN_HOM_HOMOTOPY,
    TN_HOM_HOPF,
    TN_HOM_HS,
    TN_HOM_HSW,
    TN_HOM_LOOP,
    TN_HOM_PULLBACK,
    TN_HOM_PUSHOUT,
    TN_HOM_QUOTIENT,
    TN_HOM_QUOTIENT2,
    TN_HOM_SETQUOT,
    TN_HOM_SUSPENSION,
    TN_HOM_TRUNCATION,

    TN_TOTAL_COUNT
};

static const TEditorTreeNode s_tree_nodes[] = {
    /* Level 1: Root */
    [TN_ROOT_SYS]       = { "[/SYS] System Docs ▶", NULL, TN_SYS_REPORT, 4, FALSE },
    [TN_ROOT_ANDERS]    = { "[/ANDERS] Anders Proofs ▶", NULL, TN_ANDERS_BOOK, 3, FALSE },
    [TN_ROOT_SEP]       = { "", NULL, -1, 0, TRUE },
    [TN_ROOT_SUTRA]     = { "Heart_Sutra_Tibetan.txt", "Heart_Sutra_Tibetan.txt", -1, 0, FALSE },
    [TN_ROOT_REPORT]    = { "BTRON3_Report.txt", "BTRON3_Report.txt", -1, 0, FALSE },
    [TN_ROOT_FSMD]      = { "FS.md", "FS.md", -1, 0, FALSE },

    /* Level 2: /SYS */
    [TN_SYS_REPORT]     = { "BTRON3_Report.txt", "BTRON3_Report.txt", -1, 0, FALSE },
    [TN_SYS_FSMD]       = { "FS.md", "FS.md", -1, 0, FALSE },
    [TN_SYS_SUTRA]      = { "Heart_Sutra_Tibetan.txt", "Heart_Sutra_Tibetan.txt", -1, 0, FALSE },
    [TN_SYS_HELLO]      = { "hello.txt", "hello.txt", -1, 0, FALSE },

    /* Level 2: /ANDERS */
    [TN_ANDERS_BOOK]        = { "book.anders.txt", "assets/anders/book.anders.txt", -1, 0, FALSE },
    [TN_ANDERS_FOUNDATIONS] = { "foundations ▶", NULL, TN_FND_LOGIC, 4, FALSE },
    [TN_ANDERS_MATHEMATICS] = { "mathematics ▶", NULL, TN_MATH_ALGEBRA, 5, FALSE },

    /* Level 3: foundations */
    [TN_FND_LOGIC]      = { "logic ▶", NULL, TN_LOGIC_AWODEY, 7, FALSE },
    [TN_FND_MLTT]       = { "mltt ▶", NULL, TN_MLTT_BOOL, 14, FALSE },
    [TN_FND_MODAL]      = { "modal ▶", NULL, TN_MODAL_FLAT, 6, FALSE },
    [TN_FND_UNIVALENT]  = { "univalent ▶", NULL, TN_UNIV_CARTESIAN, 7, FALSE },

    /* Level 3: mathematics */
    [TN_MATH_ALGEBRA]    = { "algebra ▶", NULL, TN_ALG_ALGEBRA, 4, FALSE },
    [TN_MATH_ANALYSIS]   = { "analysis ▶", NULL, TN_ANA_BOREL, 3, FALSE },
    [TN_MATH_CATEGORIES] = { "categories ▶", NULL, TN_CAT_ABELIAN, 13, FALSE },
    [TN_MATH_GEOMETRY]   = { "geometry ▶", NULL, TN_GEO_BUNDLE, 4, FALSE },
    [TN_MATH_HOMOTOPY]   = { "homotopy ▶", NULL, TN_HOM_KG1, 20, FALSE },

    /* Level 4: logic */
    [TN_LOGIC_AWODEY]     = { "awodey.anders.txt", "assets/anders/foundations/logic/awodey.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_FAVONIA]    = { "favonia.anders.txt", "assets/anders/foundations/logic/favonia.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_HILBERT]    = { "hilbert.anders.txt", "assets/anders/foundations/logic/hilbert.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_HLYVENKO]   = { "hlyvenko.anders.txt", "assets/anders/foundations/logic/hlyvenko.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_KRAUS]      = { "kraus.anders.txt", "assets/anders/foundations/logic/kraus.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_MINIMALIST] = { "minimalist.anders.txt", "assets/anders/foundations/logic/minimalist.anders.txt", -1, 0, FALSE },
    [TN_LOGIC_MIPHAM]     = { "mipham.anders.txt", "assets/anders/foundations/logic/mipham.anders.txt", -1, 0, FALSE },

    /* Level 4: mltt */
    [TN_MLTT_BOOL]        = { "bool.anders.txt", "assets/anders/foundations/mltt/bool.anders.txt", -1, 0, FALSE },
    [TN_MLTT_EITHER]      = { "either.anders.txt", "assets/anders/foundations/mltt/either.anders.txt", -1, 0, FALSE },
    [TN_MLTT_FIN]         = { "fin.anders.txt", "assets/anders/foundations/mltt/fin.anders.txt", -1, 0, FALSE },
    [TN_MLTT_INDUCTIVE]   = { "inductive.anders.txt", "assets/anders/foundations/mltt/inductive.anders.txt", -1, 0, FALSE },
    [TN_MLTT_LAMBDA]      = { "lambda.anders.txt", "assets/anders/foundations/mltt/lambda.anders.txt", -1, 0, FALSE },
    [TN_MLTT_LIST]        = { "list.anders.txt", "assets/anders/foundations/mltt/list.anders.txt", -1, 0, FALSE },
    [TN_MLTT_MAYBE]       = { "maybe.anders.txt", "assets/anders/foundations/mltt/maybe.anders.txt", -1, 0, FALSE },
    [TN_MLTT_MLTT]        = { "mltt.anders.txt", "assets/anders/foundations/mltt/mltt.anders.txt", -1, 0, FALSE },
    [TN_MLTT_NAT]         = { "nat.anders.txt", "assets/anders/foundations/mltt/nat.anders.txt", -1, 0, FALSE },
    [TN_MLTT_NATW]        = { "natw.anders.txt", "assets/anders/foundations/mltt/natw.anders.txt", -1, 0, FALSE },
    [TN_MLTT_PI]          = { "pi.anders.txt", "assets/anders/foundations/mltt/pi.anders.txt", -1, 0, FALSE },
    [TN_MLTT_PROTO]       = { "proto.anders.txt", "assets/anders/foundations/mltt/proto.anders.txt", -1, 0, FALSE },
    [TN_MLTT_SIGMA]       = { "sigma.anders.txt", "assets/anders/foundations/mltt/sigma.anders.txt", -1, 0, FALSE },
    [TN_MLTT_VEC]         = { "vec.anders.txt", "assets/anders/foundations/mltt/vec.anders.txt", -1, 0, FALSE },

    /* Level 4: modal */
    [TN_MODAL_FLAT]          = { "flat.anders.txt", "assets/anders/foundations/modal/flat.anders.txt", -1, 0, FALSE },
    [TN_MODAL_INFINITESIMAL] = { "infinitesimal.anders.txt", "assets/anders/foundations/modal/infinitesimal.anders.txt", -1, 0, FALSE },
    [TN_MODAL_MODALITY]      = { "modality.anders.txt", "assets/anders/foundations/modal/modality.anders.txt", -1, 0, FALSE },
    [TN_MODAL_SHARP]         = { "sharp.anders.txt", "assets/anders/foundations/modal/sharp.anders.txt", -1, 0, FALSE },
    [TN_MODAL_STT]           = { "stt.anders.txt", "assets/anders/foundations/modal/stt.anders.txt", -1, 0, FALSE },
    [TN_MODAL_TWISTED]       = { "twisted.anders.txt", "assets/anders/foundations/modal/twisted.anders.txt", -1, 0, FALSE },

    /* Level 4: univalent */
    [TN_UNIV_CARTESIAN]      = { "cartesian.anders.txt", "assets/anders/foundations/univalent/cartesian.anders.txt", -1, 0, FALSE },
    [TN_UNIV_EQUIV]          = { "equiv.anders.txt", "assets/anders/foundations/univalent/equiv.anders.txt", -1, 0, FALSE },
    [TN_UNIV_EXTENSIONALITY] = { "extensionality.anders.txt", "assets/anders/foundations/univalent/extensionality.anders.txt", -1, 0, FALSE },
    [TN_UNIV_HEDBERG]        = { "hedberg.anders.txt", "assets/anders/foundations/univalent/hedberg.anders.txt", -1, 0, FALSE },
    [TN_UNIV_ISO]            = { "iso.anders.txt", "assets/anders/foundations/univalent/iso.anders.txt", -1, 0, FALSE },
    [TN_UNIV_PATH]           = { "path.anders.txt", "assets/anders/foundations/univalent/path.anders.txt", -1, 0, FALSE },
    [TN_UNIV_PROP]           = { "prop.anders.txt", "assets/anders/foundations/univalent/prop.anders.txt", -1, 0, FALSE },

    /* Level 4: algebra */
    [TN_ALG_ALGEBRA]   = { "algebra.anders.txt", "assets/anders/mathematics/algebra/algebra.anders.txt", -1, 0, FALSE },
    [TN_ALG_HOMOLOGY]  = { "homology.anders.txt", "assets/anders/mathematics/algebra/homology.anders.txt", -1, 0, FALSE },
    [TN_ALG_INT]       = { "int.anders.txt", "assets/anders/mathematics/algebra/int.anders.txt", -1, 0, FALSE },
    [TN_ALG_PYTHAGOR]  = { "pythagor.anders.txt", "assets/anders/mathematics/algebra/pythagor.anders.txt", -1, 0, FALSE },

    /* Level 4: analysis */
    [TN_ANA_BOREL]     = { "borel.anders.txt", "assets/anders/mathematics/analysis/borel.anders.txt", -1, 0, FALSE },
    [TN_ANA_REAL]      = { "real.anders.txt", "assets/anders/mathematics/analysis/real.anders.txt", -1, 0, FALSE },
    [TN_ANA_TOPOLOGY]  = { "topology.anders.txt", "assets/anders/mathematics/analysis/topology.anders.txt", -1, 0, FALSE },

    /* Level 4: categories */
    [TN_CAT_ABELIAN]     = { "abelian.anders.txt", "assets/anders/mathematics/categories/abelian.anders.txt", -1, 0, FALSE },
    [TN_CAT_ADJUNCTION]  = { "adjunction.anders.txt", "assets/anders/mathematics/categories/adjunction.anders.txt", -1, 0, FALSE },
    [TN_CAT_CARTESIAN]   = { "cartesian.anders.txt", "assets/anders/mathematics/categories/cartesian.anders.txt", -1, 0, FALSE },
    [TN_CAT_CAT]         = { "cat.anders.txt", "assets/anders/mathematics/categories/cat.anders.txt", -1, 0, FALSE },
    [TN_CAT_CATEGORY]    = { "category.anders.txt", "assets/anders/mathematics/categories/category.anders.txt", -1, 0, FALSE },
    [TN_CAT_EQUIVALENCE] = { "equivalence.anders.txt", "assets/anders/mathematics/categories/equivalence.anders.txt", -1, 0, FALSE },
    [TN_CAT_FUNCTOR]     = { "functor.anders.txt", "assets/anders/mathematics/categories/functor.anders.txt", -1, 0, FALSE },
    [TN_CAT_GROUPOID]    = { "groupoid.anders.txt", "assets/anders/mathematics/categories/groupoid.anders.txt", -1, 0, FALSE },
    [TN_CAT_NATURAL]     = { "natural.anders.txt", "assets/anders/mathematics/categories/natural.anders.txt", -1, 0, FALSE },
    [TN_CAT_SYMMETRIC]   = { "symmetric.anders.txt", "assets/anders/mathematics/categories/symmetric.anders.txt", -1, 0, FALSE },
    [TN_CAT_TOPOS]       = { "topos.anders.txt", "assets/anders/mathematics/categories/topos.anders.txt", -1, 0, FALSE },
    [TN_CAT_UNIVERSAL]   = { "universal.anders.txt", "assets/anders/mathematics/categories/universal.anders.txt", -1, 0, FALSE },
    [TN_CAT_YONEDA]      = { "yoneda.anders.txt", "assets/anders/mathematics/categories/yoneda.anders.txt", -1, 0, FALSE },

    /* Level 4: geometry */
    [TN_GEO_BUNDLE]      = { "bundle.anders.txt", "assets/anders/mathematics/geometry/bundle.anders.txt", -1, 0, FALSE },
    [TN_GEO_ETALE]       = { "etale.anders.txt", "assets/anders/mathematics/geometry/etale.anders.txt", -1, 0, FALSE },
    [TN_GEO_FORMALDISC]  = { "formalDisc.anders.txt", "assets/anders/mathematics/geometry/formalDisc.anders.txt", -1, 0, FALSE },
    [TN_GEO_KREIN]       = { "krein.anders.txt", "assets/anders/mathematics/geometry/krein.anders.txt", -1, 0, FALSE },

    /* Level 4: homotopy */
    [TN_HOM_KG1]         = { "KG1.anders.txt", "assets/anders/mathematics/homotopy/KG1.anders.txt", -1, 0, FALSE },
    [TN_HOM_KGN]         = { "KGn.anders.txt", "assets/anders/mathematics/homotopy/KGn.anders.txt", -1, 0, FALSE },
    [TN_HOM_S1]          = { "S1.anders.txt", "assets/anders/mathematics/homotopy/S1.anders.txt", -1, 0, FALSE },
    [TN_HOM_SN]          = { "Sn.anders.txt", "assets/anders/mathematics/homotopy/Sn.anders.txt", -1, 0, FALSE },
    [TN_HOM_SNW]         = { "Snw.anders.txt", "assets/anders/mathematics/homotopy/Snw.anders.txt", -1, 0, FALSE },
    [TN_HOM_COEQUALIZER] = { "coequalizer.anders.txt", "assets/anders/mathematics/homotopy/coequalizer.anders.txt", -1, 0, FALSE },
    [TN_HOM_COLIM]       = { "colim.anders.txt", "assets/anders/mathematics/homotopy/colim.anders.txt", -1, 0, FALSE },
    [TN_HOM_CONSTCUBES]  = { "constcubes.anders.txt", "assets/anders/mathematics/homotopy/constcubes.anders.txt", -1, 0, FALSE },
    [TN_HOM_HOMOTOPY]    = { "homotopy.anders.txt", "assets/anders/mathematics/homotopy/homotopy.anders.txt", -1, 0, FALSE },
    [TN_HOM_HOPF]        = { "hopf.anders.txt", "assets/anders/mathematics/homotopy/hopf.anders.txt", -1, 0, FALSE },
    [TN_HOM_HS]          = { "hs.anders.txt", "assets/anders/mathematics/homotopy/hs.anders.txt", -1, 0, FALSE },
    [TN_HOM_HSW]         = { "hsw.anders.txt", "assets/anders/mathematics/homotopy/hsw.anders.txt", -1, 0, FALSE },
    [TN_HOM_LOOP]        = { "loop.anders.txt", "assets/anders/mathematics/homotopy/loop.anders.txt", -1, 0, FALSE },
    [TN_HOM_PULLBACK]    = { "pullback.anders.txt", "assets/anders/mathematics/homotopy/pullback.anders.txt", -1, 0, FALSE },
    [TN_HOM_PUSHOUT]     = { "pushout.anders.txt", "assets/anders/mathematics/homotopy/pushout.anders.txt", -1, 0, FALSE },
    [TN_HOM_QUOTIENT]    = { "quotient.anders.txt", "assets/anders/mathematics/homotopy/quotient.anders.txt", -1, 0, FALSE },
    [TN_HOM_QUOTIENT2]   = { "quotient2.anders.txt", "assets/anders/mathematics/homotopy/quotient2.anders.txt", -1, 0, FALSE },
    [TN_HOM_SETQUOT]     = { "setquot.anders.txt", "assets/anders/mathematics/homotopy/setquot.anders.txt", -1, 0, FALSE },
    [TN_HOM_SUSPENSION]  = { "suspension.anders.txt", "assets/anders/mathematics/homotopy/suspension.anders.txt", -1, 0, FALSE },
    [TN_HOM_TRUNCATION]  = { "truncation.anders.txt", "assets/anders/mathematics/homotopy/truncation.anders.txt", -1, 0, FALSE },
};

static void teditor_get_level_box(const TEditor *ed, GDEV *dev, int level, RECT *out_box, int *out_start, int *out_count) {
    if (!ed || !out_box || !out_start || !out_count) return;
    *out_start = -1;
    *out_count = 0;
    memset(out_box, 0, sizeof(RECT));

    if (level == 0) {
        char files[32][64];
        int count = teditor_get_asset_files(files, 32);
        *out_start = 0;
        *out_count = count;
        H x = ed->menu_bar.headers[0].rect.left + APP_MENU_DROPDOWN_WIDTH - 2;
        H y = APP_MENU_BAR_HEIGHT + 3 + 1 * APP_MENU_ROW_HEIGHT;
        H w = 240;
        H h = (*out_count) * APP_MENU_ROW_HEIGHT + 6;
        if (dev && x + w > dev->width) x = ed->menu_bar.headers[0].rect.left - w + 2;
        out_box->left = x; out_box->top = y;
        out_box->right = x + w; out_box->bottom = y + h;
        return;
    }

    RECT parent_box;
    int parent_start = -1, parent_count = 0;
    teditor_get_level_box(ed, dev, level - 1, &parent_box, &parent_start, &parent_count);
    int hov = ed->tree_hover[level - 1];
    if (hov < 0 || hov >= parent_count) return;

    if (level == 1) {
        if (hov == 0) {
            /* /SYS */
            *out_start = TN_SYS_REPORT;
            *out_count = 4;
        } else if (hov == 1) {
            /* /ANDERS */
            *out_start = TN_ANDERS_BOOK;
            *out_count = 3;
        } else {
            return;
        }
    } else {
        int parent_node = parent_start + hov;
        if (parent_node >= TN_TOTAL_COUNT) return;
        if (s_tree_nodes[parent_node].child_start < 0 || s_tree_nodes[parent_node].child_count <= 0) return;

        *out_start = s_tree_nodes[parent_node].child_start;
        *out_count = s_tree_nodes[parent_node].child_count;
    }

    H w = (level == 1) ? 220 : ((level == 2) ? 190 : 250);
    H h = (*out_count) * APP_MENU_ROW_HEIGHT + 6;
    H x = parent_box.right - 2;
    if (dev && x + w > dev->width) {
        x = parent_box.left - w + 2;
        if (x < 0) x = 0;
    }
    H y = parent_box.top + 3 + hov * APP_MENU_ROW_HEIGHT;
    if (dev && y + h > dev->height - 20) y = dev->height - 20 - h;
    if (y < APP_MENU_BAR_HEIGHT) y = APP_MENU_BAR_HEIGHT;

    out_box->left = x; out_box->top = y;
    out_box->right = x + w; out_box->bottom = y + h;
}

static void teditor_paint_tree_menu(const TEditor *ed, GDEV *dev) {
    if (!ed || !dev) return;
    char root_files[32][64];
    int root_count = teditor_get_asset_files(root_files, 32);

    for (int lvl = 0; lvl < 4; lvl++) {
        RECT box;
        int start = -1, count = 0;
        teditor_get_level_box(ed, dev, lvl, &box, &start, &count);
        if (count <= 0 || start < 0) break;

        app_menu_draw_3d_bevel_box(dev, &box);

        for (int i = 0; i < count; i++) {
            const char *label = "";
            BOOL has_sub = FALSE;
            BOOL is_sep = FALSE;

            if (lvl == 0) {
                if (i < root_count) {
                    label = root_files[i];
                    if (i == 0 || i == 1) has_sub = TRUE;
                }
            } else {
                int node_idx = start + i;
                if (node_idx < TN_TOTAL_COUNT) {
                    const TEditorTreeNode *node = &s_tree_nodes[node_idx];
                    label = node->label;
                    has_sub = (node->child_count > 0);
                    is_sep = node->is_separator;
                }
            }

            RECT row = { box.left + 3, box.top + 3 + i * APP_MENU_ROW_HEIGHT,
                         box.right - 3, box.top + 3 + (i + 1) * APP_MENU_ROW_HEIGHT };

            if (is_sep) {
                H mid_y = (row.top + row.bottom) / 2;
                drw_lin(dev, row.left + 2, mid_y, row.right - 2, mid_y);
                continue;
            }

            BOOL is_hov = (ed->tree_hover[lvl] == i);
            if (is_hov) {
                fill_rec(dev, &row, COLOR_NAVY);
            }
            COLOR fg = is_hov ? COLOR_WHITE : COLOR_BLACK;
            drw_tc_string(dev, row.left + 6, row.top + 3, label, fg, 0x00000000);
            if (has_sub) {
                drw_tc_string(dev, row.right - 16, row.top + 3, "▶", fg, 0x00000000);
            }
        }
    }
}

static BOOL teditor_handle_tree_mouse(TEditor *ed, WND *wnd, H rel_x, H rel_y, BOOL is_click) {
    if (!ed || ed->menu_bar.active_menu != 0 || ed->menu_bar.active_submenu != 1) return FALSE;

    GDEV *dev = wnd ? wnd->dev : NULL;
    char root_files[32][64];
    int root_count = teditor_get_asset_files(root_files, 32);

    for (int lvl = 3; lvl >= 0; lvl--) {
        RECT box;
        int start = -1, count = 0;
        teditor_get_level_box(ed, dev, lvl, &box, &start, &count);
        if (count <= 0 || start < 0) continue;

        if (rel_x >= box.left && rel_x <= box.right && rel_y >= box.top && rel_y <= box.bottom) {
            int idx = (rel_y - (box.top + 3)) / APP_MENU_ROW_HEIGHT;
            if (idx >= 0 && idx < count) {
                if (is_click) {
                    const char *target_path = NULL;
                    if (lvl == 0) {
                        if (idx >= 2 && idx < root_count) {
                            target_path = root_files[idx];
                        }
                    } else {
                        int node_idx = start + idx;
                        if (node_idx < TN_TOTAL_COUNT) {
                            const TEditorTreeNode *node = &s_tree_nodes[node_idx];
                            if (node->path && node->path[0] != '\0') {
                                target_path = node->path;
                            }
                        }
                    }

                    if (target_path) {
                        teditor_load_file(ed, target_path);
                        if (wnd) snprintf(wnd->title, sizeof(wnd->title), "Editor - %s", ed->filename);
                        teditor_close_menu(ed);
                        return TRUE;
                    }
                } else {
                    if (ed->tree_hover[lvl] != idx) {
                        ed->tree_hover[lvl] = idx;
                        for (int k = lvl + 1; k < 4; k++) ed->tree_hover[k] = -1;
                    }
                    return TRUE;
                }
            }
            return TRUE;
        }
    }
    return FALSE;
}

/* ── BTRON 3.20 & BeOS-Style Menu System ─────────────────────────────── */
typedef enum {
    TMENU_FILE = 0,
    TMENU_EDIT,
    TMENU_VIEW,
    TMENU_VOBJ,
    TMENU_HELP,
    TMENU_COUNT
} TMenuId;

enum {
    TCMD_NONE = 0,
    TCMD_FILE_NEW,
    TCMD_FILE_OPEN_SUBMENU,
    TCMD_FILE_OPEN_ASSET,
    TCMD_FILE_SAVE,
    TCMD_FILE_CLOSE,
    TCMD_EDIT_UNDO,
    TCMD_EDIT_CUT,
    TCMD_EDIT_COPY,
    TCMD_EDIT_PASTE,
    TCMD_EDIT_SELECT_ALL,
    TCMD_VIEW_ZOOM_IN,
    TCMD_VIEW_ZOOM_OUT,
    TCMD_VIEW_TOGGLE_LINES,
    TCMD_VIEW_WRAP_TOGGLE,
    TCMD_VOBJ_INSERT,
    TCMD_VOBJ_CABINET,
    TCMD_HELP_ABOUT
};

static void teditor_sync_menu_state(TEditor *ed) {
    if (!ed) return;
    ed->active_menu = ed->menu_bar.active_menu;
    ed->hover_menu = ed->menu_bar.hover_menu;
    ed->hover_item = ed->menu_bar.hover_item;
    ed->active_submenu = ed->menu_bar.active_submenu;
    ed->hover_subitem = ed->menu_bar.hover_subitem;
}

static void teditor_init_menu_bar(TEditor *ed) {
    if (!ed) return;
    app_menu_init(&ed->menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    int h0 = app_menu_add_header(&ed->menu_bar, "ファイル(F)", 104);
    app_menu_add_item(&ed->menu_bar, h0, "新規作成 (New)", "Ctrl+N", TCMD_FILE_NEW, TRUE);
    app_menu_add_submenu_item(&ed->menu_bar, h0, "開く (Open) ▶", TCMD_FILE_OPEN_ASSET, 1);
    app_menu_add_separator(&ed->menu_bar, h0);
    app_menu_add_item(&ed->menu_bar, h0, "上書き保存 (Save)", "Ctrl+S", TCMD_FILE_SAVE, TRUE);
    app_menu_add_item(&ed->menu_bar, h0, "閉じる (Close)", "Ctrl+Q", TCMD_FILE_CLOSE, TRUE);

    int h1 = app_menu_add_header(&ed->menu_bar, "編集(E)", 72);
    app_menu_add_item(&ed->menu_bar, h1, "元に戻す (Undo)", "Ctrl+Z", TCMD_EDIT_UNDO, FALSE);
    app_menu_add_separator(&ed->menu_bar, h1);
    app_menu_add_item(&ed->menu_bar, h1, "切り取り (Cut)", "Ctrl+X", TCMD_EDIT_CUT, TRUE);
    app_menu_add_item(&ed->menu_bar, h1, "コピー (Copy)", "Ctrl+C", TCMD_EDIT_COPY, TRUE);
    app_menu_add_item(&ed->menu_bar, h1, "貼り付け (Paste)", "Ctrl+V", TCMD_EDIT_PASTE, TRUE);
    app_menu_add_item(&ed->menu_bar, h1, "すべて選択 (Select All)", "Ctrl+A", TCMD_EDIT_SELECT_ALL, TRUE);

    int h2 = app_menu_add_header(&ed->menu_bar, "表示(V)", 72);
    app_menu_add_item(&ed->menu_bar, h2, "拡大 (Zoom In)", "+", TCMD_VIEW_ZOOM_IN, TRUE);
    app_menu_add_item(&ed->menu_bar, h2, "縮小 (Zoom Out)", "-", TCMD_VIEW_ZOOM_OUT, TRUE);
    app_menu_add_separator(&ed->menu_bar, h2);
    app_menu_add_item(&ed->menu_bar, h2, "行番号表示 (Line Nums)", "", TCMD_VIEW_TOGGLE_LINES, TRUE);
    app_menu_add_item(&ed->menu_bar, h2, "行折り返し (Wrap Text)", "Ctrl+W", TCMD_VIEW_WRAP_TOGGLE, TRUE);

    int h3 = app_menu_add_header(&ed->menu_bar, "仮身(O)", 72);
    app_menu_add_item(&ed->menu_bar, h3, "仮身を挿入 (Insert Fusen)", "", TCMD_VOBJ_INSERT, TRUE);
    app_menu_add_item(&ed->menu_bar, h3, "実身キャビネット (Cabinet)", "", TCMD_VOBJ_CABINET, TRUE);

    int h4 = app_menu_add_header(&ed->menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&ed->menu_bar, h4, "Editor について (About)", "", TCMD_HELP_ABOUT, TRUE);

    teditor_sync_menu_state(ed);
}

int teditor_get_asset_files(char files[][64], int max_files) {
    if (!files || max_files <= 0) return 0;
    int count = 0;

    /* Row 0 & 1: Top-level Volume roots */
    if (count < max_files) {
        strncpy(files[count], "[/SYS] System Docs ▶", 63);
        files[count][63] = '\0';
        count++;
    }
    if (count < max_files) {
        strncpy(files[count], "[/ANDERS] Anders Proofs ▶", 63);
        files[count][63] = '\0';
        count++;
    }

    /* 1. Discover files from BTRON volume if mounted */
    if (g_sys_vol) {
        ID dir = opn_dir("/SYS");
        if (dir >= 0) {
            DIR_ENTRY entry;
            while (rd_dir(dir, &entry) == 0 && count < max_files) {
                if (entry.name[0] == '\0' || strcmp(entry.name, "SYS") == 0 || strcmp(entry.name, "TRASH") == 0)
                    continue;
                size_t nlen = strlen(entry.name);
                BOOL is_text = FALSE;
                if (nlen > 3 && strcmp(entry.name + nlen - 3, ".md") == 0) is_text = TRUE;
                else if (nlen > 4 && strcmp(entry.name + nlen - 4, ".txt") == 0) is_text = TRUE;
                if (is_text) {
                    BOOL dup = FALSE;
                    for (int i = 0; i < count; i++) {
                        if (strcmp(files[i], entry.name) == 0) { dup = TRUE; break; }
                    }
                    if (!dup) {
                        strncpy(files[count], entry.name, 63);
                        files[count][63] = '\0';
                        count++;
                    }
                }
            }
            cls_dir(dir);
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* 2. Discover files from host directories (assets/texts, doc/md, assets) */
    const char *dirs[] = { "assets/texts", "doc/md", "assets", NULL };
    for (int d_idx = 0; dirs[d_idx] && count < max_files; d_idx++) {
        DIR *d = opendir(dirs[d_idx]);
        if (!d) continue;
        struct dirent *de;
        while ((de = readdir(d)) != NULL && count < max_files) {
            if (de->d_name[0] == '.') continue;
            size_t nlen = strlen(de->d_name);
            BOOL is_text = FALSE;
            if (nlen > 3 && strcmp(de->d_name + nlen - 3, ".md") == 0) is_text = TRUE;
            else if (nlen > 4 && strcmp(de->d_name + nlen - 4, ".txt") == 0) is_text = TRUE;
            if (is_text) {
                BOOL dup = FALSE;
                for (int i = 0; i < count; i++) {
                    if (strcmp(files[i], de->d_name) == 0) { dup = TRUE; break; }
                }
                if (!dup) {
                    strncpy(files[count], de->d_name, 63);
                    files[count][63] = '\0';
                    count++;
                }
            }
        }
        closedir(d);
    }
#endif

    /* Fallback if no files discovered */
    if (count <= 2) {
        const char *defaults[] = {
            "BTRON3_Report.txt",
            "Heart_Sutra_Tibetan.txt",
            "FS.md",
            "README.md",
            "CLU.md",
            "hello.txt"
        };
        for (size_t i = 0; i < sizeof(defaults)/sizeof(defaults[0]) && count < max_files; i++) {
            strncpy(files[count], defaults[i], 63);
            files[count][63] = '\0';
            count++;
        }
    }

    return count;
}

void teditor_open_menu(TEditor *ed, int menu_idx) {
    if (!ed || menu_idx < 0 || menu_idx >= TMENU_COUNT) return;
    if (ed->menu_bar.header_count == 0) teditor_init_menu_bar(ed);
    app_menu_open(&ed->menu_bar, menu_idx);
    teditor_sync_menu_state(ed);
}

void teditor_close_menu(TEditor *ed) {
    if (!ed) return;
    app_menu_close(&ed->menu_bar);
    for (int i = 0; i < 4; i++) ed->tree_hover[i] = -1;
    teditor_sync_menu_state(ed);
}

static void teditor_execute_menu_cmd(TEditor *ed, WND *wnd, int cmd, int sub_idx) {
    switch (cmd) {
        case TCMD_FILE_NEW:
            teditor_init_default(ed);
            ed->total_lines = 1;
            ed->lines[0][0] = '\0';
            snprintf(wnd->title, sizeof(wnd->title), "Editor - Untitled.txt");
            break;
        case TCMD_FILE_OPEN_ASSET: {
            char files[32][64];
            int cnt = teditor_get_asset_files(files, 32);
            if (sub_idx >= 2 && sub_idx < cnt) {
                teditor_load_file(ed, files[sub_idx]);
                snprintf(wnd->title, sizeof(wnd->title), "Editor - %s", ed->filename);
            }
            break;
        }
        case TCMD_FILE_SAVE:
            teditor_save_file(ed, ed->filename);
            ed->is_modified = FALSE;
            break;
        case TCMD_FILE_CLOSE:
            cls_wnd(wnd);
            break;
        case TCMD_VIEW_WRAP_TOGGLE:
            teditor_toggle_wrap(ed);
            break;
        case TCMD_EDIT_CUT:
            teditor_copy_selection(ed);
            teditor_delete_selection(ed);
            break;
        case TCMD_EDIT_COPY:
            teditor_copy_selection(ed);
            break;
        case TCMD_EDIT_PASTE:
            teditor_paste_clipboard(ed);
            break;
        case TCMD_EDIT_SELECT_ALL:
            ed->sel_active = TRUE;
            ed->sel_start_r = 0;
            ed->sel_start_c = 0;
            ed->sel_end_r = ed->total_lines - 1;
            ed->sel_end_c = (int)strlen(ed->lines[ed->total_lines - 1]);
            ed->cursor_row = ed->sel_end_r;
            ed->cursor_col = ed->sel_end_c;
            break;
        case TCMD_VIEW_ZOOM_IN:
            if (wnd->bounds.right - wnd->bounds.left < 980) wnd->bounds.right += 60;
            if (wnd->bounds.bottom - wnd->bounds.top < 650) wnd->bounds.bottom += 40;
            break;
        case TCMD_VIEW_ZOOM_OUT:
            if (wnd->bounds.right - wnd->bounds.left > 480) wnd->bounds.right -= 60;
            if (wnd->bounds.bottom - wnd->bounds.top > 300) wnd->bounds.bottom -= 40;
            break;
        case TCMD_VIEW_TOGGLE_LINES:
            ed->show_line_nums = !ed->show_line_nums;
            break;
        case TCMD_VOBJ_INSERT:
            teditor_insert_text(ed, "[仮身: #101 図形 (Diagram.draw)]\n");
            break;
        case TCMD_VOBJ_CABINET:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;
        case TCMD_HELP_ABOUT:
            open_teditor_about_window();
            break;
        default:
            break;
    }
}

WND* open_teditor_about_window(void) {
    return app_menu_create_about_dialog("Document Editor", "文書編集",
                                        "Cleanroom BTRON Word Processor",
                                        "Brought to B-System by 5HT",
                                        240, 160);
}

static void handle_t_editor_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    TEditor *ed = (wnd->user_data) ? (TEditor*)(uintptr_t)wnd->user_data : &g_teditor;

    if (evt->type == EV_MOUSE_MOVE) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        if (teditor_handle_tree_mouse(ed, wnd, rel_x, rel_y, FALSE)) {
            return;
        }
        if (app_menu_handle_mouse_move(&ed->menu_bar, rel_x, rel_y)) {
            teditor_sync_menu_state(ed);
            return;
        }
        teditor_sync_menu_state(ed);
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;

        if (teditor_handle_tree_mouse(ed, wnd, rel_x, rel_y, TRUE)) {
            return;
        }

        int cmd = 0, sub_idx = -1;
        if (app_menu_handle_mouse_down(&ed->menu_bar, rel_x, rel_y, &cmd, &sub_idx)) {
            teditor_sync_menu_state(ed);
            if (cmd != 0) {
                teditor_execute_menu_cmd(ed, wnd, cmd, sub_idx);
            }
            return;
        }
        teditor_sync_menu_state(ed);

        /* Status Bar Footer click -> Toggle JP / EN mode */
        H client_h = wnd->dev ? wnd->dev->height : (wnd->bounds.bottom - wnd->bounds.top - 26);
        if (rel_y >= client_h - 22) {
            tip_toggle_mode();
            return;
        }

        /* Editor client canvas click -> Move cursor (y >= 24) */
        if (rel_y >= 24) {
            int click_r = (rel_y - 24) / 18 + ed->scroll_row;
            if (click_r >= 0 && click_r < ed->total_lines) {
                ed->cursor_row = click_r;
                int text_x_offset = ed->show_line_nums ? 36 : 10;
                ed->cursor_col = teditor_find_byte_offset_from_x(ed->lines[click_r], rel_x - (text_x_offset - 36));
                ed->sel_active = FALSE;
                ed->sel_anchor_r = ed->cursor_row;
                ed->sel_anchor_c = ed->cursor_col;
                teditor_ensure_cursor_visible(ed);
            }
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW key_code = evt->key;
        uint16_t mod = (uint16_t)(uintptr_t)evt->data;

        /* Forward to in-window menu bar shortcuts & keyboard navigation */
        int menu_cmd = 0;
        if (app_menu_handle_key(&ed->menu_bar, key_code, mod, &menu_cmd)) {
            teditor_sync_menu_state(ed);
            if (menu_cmd != 0) {
                teditor_execute_menu_cmd(ed, wnd, menu_cmd, -1);
            }
            return;
        }

        char commit_buf[128] = "";
        BOOL shift = (mod & BTRON_KMOD_SHIFT) != 0;
        BOOL ctrl = (mod & BTRON_KMOD_CTRL) != 0;

        /* Check if TIP handles the key event (Japanese IME mode) */
        if (tip_process_key(key_code, mod, commit_buf, sizeof(commit_buf))) {
            if (commit_buf[0] != '\0') {
                teditor_insert_text(ed, commit_buf);
            }
            return;
        }

        UW sym = key_code;

        if (sym == BTRON_KEY_ESCAPE || sym == 27) {
            if (ed->active_menu >= 0) {
                teditor_close_menu(ed);
                return;
            }
        }

        if (ctrl) {
            if (sym == 'c' || sym == 'C') {
                teditor_copy_selection(ed);
                return;
            } else if (sym == 'x' || sym == 'X') {
                teditor_copy_selection(ed);
                teditor_delete_selection(ed);
                return;
            } else if (sym == 'v' || sym == 'V') {
                teditor_paste_clipboard(ed);
                return;
            } else if (sym == 'a' || sym == 'A') {
                teditor_move_cursor(ed, 0, 0, FALSE);
                teditor_move_cursor(ed, ed->total_lines - 1, (int)strlen(ed->lines[ed->total_lines - 1]), TRUE);
                return;
            } else if (sym == 's' || sym == 'S') {
                ed->is_modified = FALSE;
                return;
            } else if (sym == 'n' || sym == 'N') {
                teditor_init_default(ed);
                ed->total_lines = 1;
                ed->lines[0][0] = '\0';
                snprintf(wnd->title, sizeof(wnd->title), "Editor - Untitled.txt");
                return;
            } else if (sym == 'o' || sym == 'O') {
                /* Ctrl+O: Open File Menu with cascading document list */
                teditor_open_menu(ed, TMENU_FILE);
                ed->active_submenu = 1;
                return;
            } else if (sym == 'w' || sym == 'W') {
                teditor_toggle_wrap(ed);
                return;
            } else if (sym == 'q' || sym == 'Q') {
                cls_wnd(wnd);
                return;
            }
        }

        if (sym == BTRON_KEY_RETURN || sym == BTRON_KEY_KP_ENTER || sym == '\r' || sym == '\n') {
            teditor_insert_newline(ed);
            return;
        } else if (sym == BTRON_KEY_BACKSPACE || sym == 0x08) {
            teditor_backspace(ed);
            return;
        } else if (sym == BTRON_KEY_DELETE || sym == 0x7F) {
            teditor_delete_char_forward(ed);
            return;
        } else if (sym == BTRON_KEY_LEFT) {
            int nr = ed->cursor_row;
            int nc = ed->cursor_col;
            if (ctrl) {
                if (nc > 0) {
                    nc = find_word_prev(ed->lines[nr], nc);
                } else if (nr > 0) {
                    nr--;
                    nc = (int)strlen(ed->lines[nr]);
                }
            } else {
                if (nc > 0) {
                    nc = utf8_prev_offset(ed->lines[nr], nc);
                } else if (nr > 0) {
                    nr--;
                    nc = (int)strlen(ed->lines[nr]);
                }
            }
            teditor_move_cursor(ed, nr, nc, shift);
            return;
        } else if (sym == BTRON_KEY_RIGHT) {
            int nr = ed->cursor_row;
            int nc = ed->cursor_col;
            int len = (int)strlen(ed->lines[nr]);
            if (ctrl) {
                if (nc < len) {
                    nc = find_word_next(ed->lines[nr], nc);
                } else if (nr < ed->total_lines - 1) {
                    nr++;
                    nc = 0;
                }
            } else {
                if (nc < len) {
                    nc = utf8_next_offset(ed->lines[nr], nc);
                } else if (nr < ed->total_lines - 1) {
                    nr++;
                    nc = 0;
                }
            }
            teditor_move_cursor(ed, nr, nc, shift);
            return;
        } else if (sym == BTRON_KEY_UP) {
            int nr = (ed->cursor_row > 0) ? ed->cursor_row - 1 : 0;
            teditor_move_cursor(ed, nr, ed->cursor_col, shift);
            return;
        } else if (sym == BTRON_KEY_DOWN) {
            int nr = (ed->cursor_row < ed->total_lines - 1) ? ed->cursor_row + 1 : ed->total_lines - 1;
            teditor_move_cursor(ed, nr, ed->cursor_col, shift);
            return;
        } else if (sym == BTRON_KEY_HOME) {
            if (ctrl) {
                teditor_move_cursor(ed, 0, 0, shift);
            } else {
                teditor_move_cursor(ed, ed->cursor_row, 0, shift);
            }
            return;
        } else if (sym == BTRON_KEY_END) {
            if (ctrl) {
                int last_r = ed->total_lines - 1;
                teditor_move_cursor(ed, last_r, (int)strlen(ed->lines[last_r]), shift);
            } else {
                teditor_move_cursor(ed, ed->cursor_row, (int)strlen(ed->lines[ed->cursor_row]), shift);
            }
            return;
        } else if (sym == BTRON_KEY_PAGE_UP || sym == 0x8052) {
            int vrows = (wnd && wnd->dev) ? (wnd->dev->height - 60) / 18 : 14;
            int nr = ed->cursor_row - vrows;
            if (nr < 0) nr = 0;
            teditor_move_cursor(ed, nr, ed->cursor_col, shift);
            return;
        } else if (sym == BTRON_KEY_PAGE_DOWN || sym == 0x8053) {
            int vrows = (wnd && wnd->dev) ? (wnd->dev->height - 60) / 18 : 14;
            int nr = ed->cursor_row + vrows;
            if (nr >= ed->total_lines) nr = ed->total_lines - 1;
            teditor_move_cursor(ed, nr, ed->cursor_col, shift);
            return;
        } else if (sym == BTRON_KEY_TAB || sym == '\t') {
            teditor_insert_text(ed, "    ");
            return;
        }

        /* Direct English / Printable ASCII Text Input */
        if (!ctrl && sym >= 32 && sym <= 126) {
            char ch = get_ascii_char_with_shift((UW)sym, mod);
            teditor_insert_char(ed, ch);
            return;
        }
    }
}

static void paint_t_editor(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    TEditor *ed = (wnd->user_data) ? (TEditor*)(uintptr_t)wnd->user_data : &g_teditor;

    /* Background page */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* ── 1. BTRON 3.20 Standard Menu Bar with Integrated IME & Status ─── */
    if (ed->menu_bar.header_count == 0) teditor_init_menu_bar(ed);
    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s%s", ed->filename, ed->is_modified ? " *" : "");
    app_menu_set_right_text(&ed->menu_bar, title_buf);
    app_menu_paint_bar(&ed->menu_bar, dev);

    /* ── 2. Render Gutter & Text Lines (Canvas starts directly at y=24) ── */
    int view_rows = (dev->height - 60) / 18;
    if (view_rows < 1) view_rows = 1;
    int start_r = ed->scroll_row;
    int end_r = start_r + view_rows;
    if (end_r > ed->total_lines) end_r = ed->total_lines;

    int sel_r1 = 0, sel_c1 = 0, sel_r2 = 0, sel_c2 = 0;
    if (ed->sel_active) {
        teditor_get_selection_range(ed, &sel_r1, &sel_c1, &sel_r2, &sel_c2);
    }

    int y = 24;
    for (int r_idx = start_r; r_idx < end_r && y < dev->height - 40; r_idx++) {
        /* Gutter line number */
        if (ed->show_line_nums) {
            char num_str[10];
            snprintf(num_str, sizeof(num_str), "%2d|", r_idx + 1);
            drw_tc_string(dev, 6, y, num_str, COLOR_GRAY, COLOR_WHITE);
        }

        int text_x_start = ed->show_line_nums ? 36 : 12;
        const char *line = ed->lines[r_idx];

        if (ed->wrap_text) {
            char wrapped[16][TEDITOR_MAX_COLS];
            int text_w = dev->width - 16 - text_x_start;
            int nwrap = teditor_wrap_line(line, text_w, wrapped, 16);
            int byte_base = 0;

            for (int v = 0; v < nwrap && y < dev->height - 40; v++) {
                const char *wp = wrapped[v];
                int x = text_x_start;
                int sub_byte_idx = 0;

                while (*wp && x < dev->width - 16) {
                    int consumed = 0;
                    (void)utf8_to_tc(wp, &consumed);
                    int step = (consumed > 0 ? consumed : 1);
                    int cur_byte = byte_base + sub_byte_idx;

                    BOOL in_sel = FALSE;
                    if (ed->sel_active) {
                        if (r_idx > sel_r1 && r_idx < sel_r2) in_sel = TRUE;
                        else if (r_idx == sel_r1 && r_idx == sel_r2) {
                            if (cur_byte >= sel_c1 && cur_byte < sel_c2) in_sel = TRUE;
                        } else if (r_idx == sel_r1) {
                            if (cur_byte >= sel_c1) in_sel = TRUE;
                        } else if (r_idx == sel_r2) {
                            if (cur_byte < sel_c2) in_sel = TRUE;
                        }
                    }

                    COLOR fg = in_sel ? COLOR_WHITE : COLOR_BLACK;
                    COLOR bg = in_sel ? COLOR_NAVY : COLOR_WHITE;
                    char glyph[8] = "";
                    memcpy(glyph, wp, step);
                    glyph[step] = '\0';

                    int gw = tc_calc_string_width(glyph, step);
                    RECT gr = { x, y, x + gw, y + 16 };
                    if (in_sel) fill_rec(dev, &gr, bg);
                    drw_tc_string(dev, x, y, glyph, fg, bg);

                    x += gw;
                    wp += step;
                    sub_byte_idx += step;
                }

                /* Draw cursor on matching wrapped sub-line */
                if (r_idx == ed->cursor_row) {
                    int wlen = (int)strlen(wrapped[v]);
                    BOOL cursor_on_sub = FALSE;
                    int sub_cursor_col = 0;

                    if (v == nwrap - 1) {
                        if (ed->cursor_col >= byte_base) {
                            cursor_on_sub = TRUE;
                            sub_cursor_col = ed->cursor_col - byte_base;
                            if (sub_cursor_col > wlen) sub_cursor_col = wlen;
                        }
                    } else {
                        if (ed->cursor_col >= byte_base && ed->cursor_col < byte_base + wlen) {
                            cursor_on_sub = TRUE;
                            sub_cursor_col = ed->cursor_col - byte_base;
                        }
                    }

                    if (cursor_on_sub) {
                        int cur_x = text_x_start + tc_calc_string_width(wrapped[v], sub_cursor_col);
                        if (cur_x >= text_x_start && cur_x < dev->width - 10) {
                            if (wnd->focused && tip_get_state() != TIP_STATE_IDLE) {
                                char comp_buf[128];
                                tip_get_converted_text(comp_buf, sizeof(comp_buf));
                                BOOL is_dotted = (tip_get_state() == TIP_STATE_PRECOMP);
                                drw_tc_string_underlined(dev, cur_x, y, comp_buf, COLOR_NAVY, COLOR_WHITE, is_dotted);
                                tip_set_caret_pos(wnd->bounds.left + cur_x, wnd->bounds.top + y);
                            } else if (wnd->focused) {
                                RECT cursor_rect = { cur_x, y, cur_x + 2, y + 16 };
                                fill_rec(dev, &cursor_rect, COLOR_NAVY);
                            }
                        }
                    }
                }

                byte_base += (int)strlen(wrapped[v]);
                y += 18;
            }
            continue;
        }

        /* Line content (unwrapped mode) */
        const char *p = line;
        int byte_idx = 0;
        int x = text_x_start;
        while (*p && x < dev->width - 16) {
            int consumed = 0;
            (void)utf8_to_tc(p, &consumed);
            int step = (consumed > 0 ? consumed : 1);

            /* Selection check */
            BOOL in_sel = FALSE;
            if (ed->sel_active) {
                if (r_idx > sel_r1 && r_idx < sel_r2) in_sel = TRUE;
                else if (r_idx == sel_r1 && r_idx == sel_r2) {
                    if (byte_idx >= sel_c1 && byte_idx < sel_c2) in_sel = TRUE;
                } else if (r_idx == sel_r1) {
                    if (byte_idx >= sel_c1) in_sel = TRUE;
                } else if (r_idx == sel_r2) {
                    if (byte_idx < sel_c2) in_sel = TRUE;
                }
            }

            COLOR fg = in_sel ? COLOR_WHITE : COLOR_BLACK;
            COLOR bg = in_sel ? COLOR_NAVY : COLOR_WHITE;
            char glyph[8] = "";
            memcpy(glyph, p, step);
            glyph[step] = '\0';

            int gw = tc_calc_string_width(glyph, step);
            RECT gr = { x, y, x + gw, y + 16 };
            if (in_sel) fill_rec(dev, &gr, bg);
            drw_tc_string(dev, x, y, glyph, fg, bg);

            x += gw;
            p += step;
            byte_idx += step;
        }

        /* Draw Blinking/Solid Cursor and Inline TIP Composition */
        if (r_idx == ed->cursor_row) {
            int cur_x = text_x_start + tc_calc_string_width(line, ed->cursor_col);
            if (cur_x >= text_x_start && cur_x < dev->width - 10) {
                if (wnd->focused && tip_get_state() != TIP_STATE_IDLE) {
                    char comp_buf[128];
                    tip_get_converted_text(comp_buf, sizeof(comp_buf));
                    BOOL is_dotted = (tip_get_state() == TIP_STATE_PRECOMP);
                    drw_tc_string_underlined(dev, cur_x, y, comp_buf, COLOR_NAVY, COLOR_WHITE, is_dotted);
                    tip_set_caret_pos(wnd->bounds.left + cur_x, wnd->bounds.top + y);
                } else if (wnd->focused) {
                    RECT cursor_rect = { cur_x, y, cur_x + 2, y + 16 };
                    fill_rec(dev, &cursor_rect, COLOR_NAVY);
                }
            }
        }
        y += 18;
    }

    /* Embedded Virtual Body Icon inside Document */
    if (ed->has_vobj) {
        RECT embed_vobj = { 36, dev->height - 45, 230, dev->height - 20 };
        fill_rec(dev, &embed_vobj, COLOR_LTGRAY);
        drw_rec(dev, &embed_vobj);
        char vobj_str[64];
        snprintf(vobj_str, sizeof(vobj_str), "[VOBJ: %s]", ed->vobj_name);
        drw_tc_string(dev, 42, dev->height - 38, vobj_str, COLOR_NAVY, COLOR_LTGRAY);
    }

    /* Status Bar Footer with Mozc indicator */
    RECT sb = { 0, dev->height - 20, dev->width, dev->height };
    fill_rec(dev, &sb, COLOR_LTGRAY);
    drw_lin(dev, 0, dev->height - 20, dev->width, dev->height - 20);

    const char *mode_str = (tip_get_mode() == TIP_MODE_HIRAGANA) ? "あ" :
                           ((tip_get_mode() == TIP_MODE_KATAKANA) ? "ア" :
                            ((tip_get_mode() == TIP_MODE_TIBETAN) ? "བོད" : "A"));
    char status_buf[128];
    snprintf(status_buf, sizeof(status_buf), " Line %d, Col %d  |  TRON-Code (Tibetan/JIS)  |  %s  |  [CUA INS]",
             ed->cursor_row + 1, ed->cursor_col + 1, ed->wrap_text ? "[WRAP]" : "[NOWRAP]");
    drw_tc_string(dev, 8, dev->height - 16, status_buf, COLOR_BLACK, COLOR_LTGRAY);

    /* Interactive Status Bar Mozc Mode Badge Button */
    RECT mode_badge = { dev->width - 160, dev->height - 19, dev->width - 4, dev->height - 2 };
    fill_rec(dev, &mode_badge, (tip_get_mode() == TIP_MODE_ASCII) ? COLOR_LTGRAY : COLOR_CYAN);
    drw_rec(dev, &mode_badge);
    char badge_str[32];
    snprintf(badge_str, sizeof(badge_str), "[TIP: %s (F10)]", mode_str);
    drw_tc_string(dev, mode_badge.left + 6, mode_badge.top + 2, badge_str, COLOR_BLACK, 0x00000000);

    /* ── 4. Floating Menu & Cascading Submenu Overlay (Topmost Layer) ─── */
    if (ed->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&ed->menu_bar, dev);
        if (ed->menu_bar.active_menu == 0 && ed->menu_bar.active_submenu == 1) {
            teditor_paint_tree_menu(ed, dev);
        }
    }
}

WND* open_t_editor_window_rect(const char *filepath, H x, H y, H w, H h, UW attr) {
    TEditor *ed = (TEditor*)calloc(1, sizeof(TEditor));
    if (!ed) return NULL;

    ed->active_menu = -1;
    ed->hover_menu = -1;
    ed->hover_item = -1;
    ed->active_submenu = -1;
    ed->hover_subitem = -1;
    for (int i = 0; i < 4; i++) ed->tree_hover[i] = -1;
    ed->show_line_nums = TRUE;

    if (!filepath || teditor_load_file(ed, filepath) != 0) {
        teditor_init_default(ed);
    }
    teditor_init_menu_bar(ed);

    char title[128];
    snprintf(title, sizeof(title), "編集者 (Editor) \"%s\" ", ed->filename);

    WND *wnd = opn_wnd(title, x, y, w, h, attr);
    if (wnd) {
        wnd->user_data = (VW)(uintptr_t)ed;
        wnd->paint = paint_t_editor;
        wnd->event_handler = handle_t_editor_event;
        wnd->destroy = destroy_t_editor;
    } else {
        free(ed);
    }
    return wnd;
}

WND* open_t_editor_window_with_file(const char *filepath) {
    return open_t_editor_window_rect(filepath, 220, 50, 760, 480,
                                     WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
}

WND* open_t_editor_window(void) {
    return open_t_editor_window_with_file("assets/texts/BTRON3_Report.txt");
}

TEditor* teditor_get_current(void) {
    return &g_teditor;
}


/* ── File Management & Real Object/Virtual Object Storage Subsystem ── */

int teditor_load_file(TEditor *ed, const char *filepath) {
    if (!ed || !filepath) return -1;

    ed->active_menu = -1;
    ed->hover_menu = -1;
    ed->hover_item = -1;
    ed->active_submenu = -1;
    ed->hover_subitem = -1;
    for (int i = 0; i < 4; i++) ed->tree_hover[i] = -1;

    /* Extract base filename */
    const char *slash = strrchr(filepath, '/');
#ifdef _WIN32
    const char *bslash = strrchr(filepath, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : filepath;

    /* 1. Try loading from BTRON volume if mounted */
    if (g_sys_vol || g_anders_vol) {
        ID fd = opn_fil(base, 0x0001 /* F_READ */);
        if (fd < 0 && filepath[0] != '/') {
            fd = opn_fil(filepath, 0x0001);
        }
        if (fd >= 0) {
            ID rec = opn_rec(fd, 0, 0x0001);
            if (rec >= 0) {
                OpenFile *of = &g_open_files[(int)fd];
                UW rsize = (of->nrec > 0) ? of->ridx[0].size : 0;
                char *buf = (char *)malloc(rsize + 1);
                if (buf) {
                    W read_sz = 0;
                    rd_rec(rec, buf, (W)rsize, &read_sz);
                    buf[read_sz] = '\0';
                    cls_rec(rec);
                    cls_fil(fd);

                    const char *p = buf;
                    if ((unsigned char)p[0] == 0xFF && (unsigned char)p[1] == 0xE1 && read_sz >= 4) {
                        p += 4;
                    }

                    ed->total_lines = 0;
                    ed->cursor_row = 0;
                    ed->cursor_col = 0;
                    ed->scroll_row = 0;
                    ed->scroll_col = 0;
                    ed->sel_active = FALSE;
                    ed->is_modified = FALSE;

                    while (*p && ed->total_lines < TEDITOR_MAX_ROWS) {
                        const char *eol = p;
                        while (*eol && *eol != '\n' && *eol != '\r') eol++;
                        size_t llen = (size_t)(eol - p);
                        if (llen >= TEDITOR_MAX_COLS) llen = TEDITOR_MAX_COLS - 1;
                        memcpy(ed->lines[ed->total_lines], p, llen);
                        ed->lines[ed->total_lines][llen] = '\0';
                        ed->total_lines++;
                        p = eol;
                        if (*p == '\r') p++;
                        if (*p == '\n') p++;
                    }
                    if (ed->total_lines == 0) {
                        ed->total_lines = 1;
                        ed->lines[0][0] = '\0';
                    }
                    free(buf);
                    strncpy(ed->filename, base, sizeof(ed->filename) - 1);
                    ed->filename[sizeof(ed->filename) - 1] = '\0';
                    return 0;
                }
                cls_rec(rec);
            }
            cls_fil(fd);
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* 2. Fall back to host filesystem */
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        char alt_path[128];
        snprintf(alt_path, sizeof(alt_path), "doc/md/%s", base);
        fp = fopen(alt_path, "r");
        if (!fp) {
            snprintf(alt_path, sizeof(alt_path), "assets/texts/%s", base);
            fp = fopen(alt_path, "r");
            if (!fp) {
                snprintf(alt_path, sizeof(alt_path), "assets/%s", base);
                fp = fopen(alt_path, "r");
                if (!fp) {
                    snprintf(alt_path, sizeof(alt_path), "assets/anders/%s", base);
                    fp = fopen(alt_path, "r");
                }
            }
        }
    }
    if (!fp) return -1;

    ed->total_lines = 0;
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->sel_active = FALSE;
    ed->is_modified = FALSE;

    char line_buf[TEDITOR_MAX_COLS * 2];
    while (fgets(line_buf, sizeof(line_buf), fp) && ed->total_lines < TEDITOR_MAX_ROWS) {
        size_t len = strlen(line_buf);
        while (len > 0 && (line_buf[len - 1] == '\n' || line_buf[len - 1] == '\r')) {
            line_buf[--len] = '\0';
        }
        strncpy(ed->lines[ed->total_lines], line_buf, TEDITOR_MAX_COLS - 1);
        ed->lines[ed->total_lines][TEDITOR_MAX_COLS - 1] = '\0';
        ed->total_lines++;
    }
    fclose(fp);

    if (ed->total_lines == 0) {
        ed->total_lines = 1;
        ed->lines[0][0] = '\0';
    }

    strncpy(ed->filename, base, sizeof(ed->filename) - 1);
    ed->filename[sizeof(ed->filename) - 1] = '\0';
    return 0;
#else
    return -1;
#endif
}

int teditor_save_file(TEditor *ed, const char *filepath) {
    if (!ed) return -1;
    const char *target = filepath ? filepath : ed->filename;

    const char *slash = strrchr(target, '/');
#ifdef _WIN32
    const char *bslash = strrchr(target, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
#endif
    const char *base = slash ? slash + 1 : target;

    /* 1. If BTRON volume is mounted, save Real Body record on volume */
    if (g_sys_vol) {
        ID fd = opn_fil(base, 0x0002 /* F_WRITE */);
        if (fd < 0) {
            fd = cre_fil(base, 0x0002 /* F_WRITE */);
        }
        if (fd >= 0) {
            size_t total_sz = 0;
            for (int i = 0; i < ed->total_lines; i++) {
                total_sz += strlen(ed->lines[i]) + 1;
            }
            char *buf = (char *)malloc(total_sz + 1);
            if (buf) {
                size_t pos = 0;
                for (int i = 0; i < ed->total_lines; i++) {
                    size_t llen = strlen(ed->lines[i]);
                    memcpy(buf + pos, ed->lines[i], llen);
                    pos += llen;
                    buf[pos++] = '\n';
                }
                OpenFile *of = &g_open_files[(int)fd];
                if (of->nrec > 0) {
                    del_rec(fd, 0);
                }
                ins_rec(fd, 0, buf, (W)total_sz);
                fil_set_rec_type(fd, 0, (unsigned short)RT_TADDATA);
                free(buf);
            }
            cls_fil(fd);
            vol_sync(g_sys_vol);
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* 2. Also save to host filesystem if hosted */
    FILE *fp = fopen(target, "w");
    if (!fp) {
        char alt_path[128];
        snprintf(alt_path, sizeof(alt_path), "doc/md/%s", base);
        fp = fopen(alt_path, "w");
        if (!fp) {
            snprintf(alt_path, sizeof(alt_path), "assets/texts/%s", base);
            fp = fopen(alt_path, "w");
        }
    }
    if (fp) {
        for (int i = 0; i < ed->total_lines; i++) {
            fprintf(fp, "%s\n", ed->lines[i]);
        }
        fclose(fp);
    }
#endif

    ed->is_modified = FALSE;
    return 0;
}

int teditor_close_file(TEditor *ed) {
    if (!ed) return -1;
    memset(ed->lines, 0, sizeof(ed->lines));
    ed->total_lines = 1;
    ed->lines[0][0] = '\0';
    ed->cursor_row = 0;
    ed->cursor_col = 0;
    ed->scroll_row = 0;
    ed->scroll_col = 0;
    ed->sel_active = FALSE;
    ed->is_modified = FALSE;
    strncpy(ed->filename, "Untitled.txt", sizeof(ed->filename) - 1);
    return 0;
}

BOOL t_editor_is_menu_open(WND *wnd) {
    if (!wnd) return FALSE;
    TEditor *ed = (TEditor*)(uintptr_t)wnd->user_data;
    return (ed && ed->menu_bar.active_menu >= 0);
}

void t_editor_open_menu(WND *wnd, int menu_idx) {
    if (!wnd) return;
    TEditor *ed = (TEditor*)(uintptr_t)wnd->user_data;
    if (ed) {
        app_menu_open(&ed->menu_bar, menu_idx);
        ed->menu_bar.hover_item = 0;
    }
}

void t_editor_close_menu(WND *wnd) {
    if (!wnd) return;
    TEditor *ed = (TEditor*)(uintptr_t)wnd->user_data;
    if (ed) {
        app_menu_close(&ed->menu_bar);
    }
}


