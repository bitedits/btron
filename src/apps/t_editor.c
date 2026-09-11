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
#define strncmp tkl_strncmp

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
    for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
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

/* ── Live Filesystem Walker — Open Menu Subsystem ──────────────── */

static int tmenu_strcasecmp(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'a' && *a <= 'z') ? (char)(*a - 32) : *a;
        char cb = (*b >= 'a' && *b <= 'z') ? (char)(*b - 32) : *b;
        if (ca != cb) return (int)(unsigned char)ca - (int)(unsigned char)cb;
        a++; b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int teditor_scan_fs_dir(const char *dir_path, TMenuTreeItem *out_items, int max_items) {
    if (!dir_path || !out_items || max_items <= 0) return 0;
    int count = 0;

    /* 1. If dir_path is "/SYS", enumerate system documents from volume or host */
    if (strcmp(dir_path, "/SYS") == 0) {
        if (g_sys_vol) {
            ID dir = opn_dir("/SYS");
            if (dir >= 0) {
                DIR_ENTRY entry;
                while (rd_dir(dir, &entry) == 0 && count < max_items) {
                    if (entry.name[0] == '\0' || strcmp(entry.name, "SYS") == 0 || strcmp(entry.name, "TRASH") == 0)
                        continue;
                    size_t nlen = strlen(entry.name);
                    BOOL is_text = FALSE;
                    if (nlen > 3 && strcmp(entry.name + nlen - 3, ".md") == 0) is_text = TRUE;
                    else if (nlen > 4 && strcmp(entry.name + nlen - 4, ".txt") == 0) is_text = TRUE;
                    if (is_text) {
                        strncpy(out_items[count].name, entry.name, sizeof(out_items[count].name) - 1);
                        out_items[count].name[sizeof(out_items[count].name) - 1] = '\0';
                        strncpy(out_items[count].path, entry.name, sizeof(out_items[count].path) - 1);
                        out_items[count].path[sizeof(out_items[count].path) - 1] = '\0';
                        out_items[count].is_dir = FALSE;
                        out_items[count].is_sep = FALSE;
                        count++;
                    }
                }
                cls_dir(dir);
            }
        }
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        if (count == 0) {
            DIR *d = opendir("doc/md");
            if (d) {
                struct dirent *de;
                while ((de = readdir(d)) != NULL && count < max_items) {
                    if (de->d_name[0] == '.') continue;
                    size_t nlen = strlen(de->d_name);
                    if (nlen > 3 && strcmp(de->d_name + nlen - 3, ".md") == 0) {
                        strncpy(out_items[count].name, de->d_name, sizeof(out_items[count].name) - 1);
                        out_items[count].name[sizeof(out_items[count].name) - 1] = '\0';
                        strncpy(out_items[count].path, de->d_name, sizeof(out_items[count].path) - 1);
                        out_items[count].path[sizeof(out_items[count].path) - 1] = '\0';
                        out_items[count].is_dir = FALSE;
                        out_items[count].is_sep = FALSE;
                        count++;
                    }
                }
                closedir(d);
            }
        }
#endif
        if (count == 0) {
            const char *sys_docs[] = {
                "BOOK.md", "CLU.md", "FS.md", "README.md", "SYS.md"
            };
            for (size_t i = 0; i < sizeof(sys_docs)/sizeof(sys_docs[0]) && count < max_items; i++) {
                strncpy(out_items[count].name, sys_docs[i], sizeof(out_items[count].name) - 1);
                out_items[count].name[sizeof(out_items[count].name) - 1] = '\0';
                strncpy(out_items[count].path, sys_docs[i], sizeof(out_items[count].path) - 1);
                out_items[count].path[sizeof(out_items[count].path) - 1] = '\0';
                out_items[count].is_dir = FALSE;
                out_items[count].is_sep = FALSE;
                count++;
            }
        }
        /* Sort files alphabetically */
        for (int i = 1; i < count; i++) {
            TMenuTreeItem tmp = out_items[i];
            int j = i - 1;
            while (j >= 0 && tmenu_strcasecmp(out_items[j].name, tmp.name) > 0) {
                out_items[j + 1] = out_items[j];
                j--;
            }
            out_items[j + 1] = tmp;
        }
        return count;
    }

    /* 2. Check if this is a volume path (/ANDERS) */
    Volume *v = (strncmp(dir_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol : NULL;
    if (v) {
        ID dir = opn_dir(dir_path);
        if (dir >= 0) {
            DIR_ENTRY entry;
            while (rd_dir(dir, &entry) == 0 && count < max_items) {
                if (entry.name[0] == '\0') continue;
                char probe[TEDITOR_MENU_PATH_LEN];
                snprintf(probe, sizeof(probe), "%s/%s", dir_path, entry.name);
                ID p = opn_dir(probe);
                BOOL is_dir = (p >= 0);
                if (is_dir) cls_dir(p);
                if (!is_dir) {
                    size_t nlen = strlen(entry.name);
                    BOOL ok = FALSE;
                    if (nlen > 4 && strcmp(entry.name + nlen - 4, ".txt") == 0) ok = TRUE;
                    else if (nlen > 3 && strcmp(entry.name + nlen - 3, ".md") == 0) ok = TRUE;
                    if (!ok) continue;
                }
                TMenuTreeItem *it = &out_items[count++];
                strncpy(it->name, entry.name, sizeof(it->name) - 1);
                it->name[sizeof(it->name) - 1] = '\0';
                strncpy(it->path, probe, sizeof(it->path) - 1);
                it->path[sizeof(it->path) - 1] = '\0';
                it->is_dir = is_dir;
                it->is_sep = FALSE;
            }
            cls_dir(dir);
        }
    }

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    if (count == 0) {
        /* Map /ANDERS to assets/anders on host */
        const char *host_path = dir_path;
        char mapped_path[TEDITOR_MENU_PATH_LEN];
        if (strncmp(dir_path, "/ANDERS", 7) == 0) {
            snprintf(mapped_path, sizeof(mapped_path), "assets/anders%s", dir_path + 7);
            host_path = mapped_path;
        }

        DIR *d = opendir(host_path);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL && count < max_items) {
                if (de->d_name[0] == '.') continue;
                char full[TEDITOR_MENU_PATH_LEN];
                snprintf(full, sizeof(full), "%s/%s", host_path, de->d_name);

                BOOL is_dir = FALSE;
#if defined(_DIRENT_HAVE_D_TYPE) || defined(DT_DIR)
                if (de->d_type == DT_DIR) is_dir = TRUE;
                else if (de->d_type == DT_REG) is_dir = FALSE;
                else
#endif
                {
                    struct stat st;
                    if (stat(full, &st) == 0 && S_ISDIR(st.st_mode)) is_dir = TRUE;
                }

                if (!is_dir) {
                    size_t nlen = strlen(de->d_name);
                    BOOL ok = FALSE;
                    if (nlen > 4 && strcmp(de->d_name + nlen - 4, ".txt") == 0) ok = TRUE;
                    else if (nlen > 3 && strcmp(de->d_name + nlen - 3, ".md") == 0) ok = TRUE;
                    else if (nlen > 11 && strcmp(de->d_name + nlen - 11, ".anders.txt") == 0) ok = TRUE;
                    if (!ok) continue;
                }

                TMenuTreeItem *it = &out_items[count++];
                strncpy(it->name, de->d_name, sizeof(it->name) - 1);
                it->name[sizeof(it->name) - 1] = '\0';
                strncpy(it->path, full, sizeof(it->path) - 1);
                it->path[sizeof(it->path) - 1] = '\0';
                it->is_dir = is_dir;
                it->is_sep = FALSE;
            }
            closedir(d);
        }
    }
#endif

    /* Sort: directories first (alphabetically), then files (alphabetically) */
    for (int i = 1; i < count; i++) {
        TMenuTreeItem tmp = out_items[i];
        int j = i - 1;
        while (j >= 0) {
            int cmp = 0;
            if (out_items[j].is_dir != tmp.is_dir) {
                cmp = out_items[j].is_dir ? -1 : 1;
            } else {
                cmp = tmenu_strcasecmp(out_items[j].name, tmp.name);
            }
            if (cmp <= 0) break;
            out_items[j + 1] = out_items[j];
            j--;
        }
        out_items[j + 1] = tmp;
    }
    return count;
}

static int teditor_get_tree_items(const TEditor *ed, int lvl, TMenuTreeItem *out_items, int max_items) {
    if (!ed || !out_items || max_items <= 0) return 0;

    if (lvl == 0) {
        char raw_files[32][64];
        int nfiles = teditor_get_asset_files(raw_files, 32);
        int count = 0;
        for (int i = 0; i < nfiles && count < max_items; i++) {
            strncpy(out_items[count].name, raw_files[i], sizeof(out_items[count].name) - 1);
            out_items[count].name[sizeof(out_items[count].name) - 1] = '\0';
            if (i == 0) {
                strncpy(out_items[count].path, "/SYS", sizeof(out_items[count].path) - 1);
                out_items[count].is_dir = TRUE;
            } else if (i == 1) {
                strncpy(out_items[count].path, "assets/anders", sizeof(out_items[count].path) - 1);
                out_items[count].is_dir = TRUE;
            } else {
                strncpy(out_items[count].path, raw_files[i], sizeof(out_items[count].path) - 1);
                out_items[count].is_dir = FALSE;
            }
            out_items[count].path[sizeof(out_items[count].path) - 1] = '\0';
            out_items[count].is_sep = FALSE;
            count++;
        }
        return count;
    }

    if (lvl < 0 || lvl >= TEDITOR_TREE_MAX_LEVELS) return 0;

    /* For lvl >= 1, query parent level to find the hovered directory path */
    TMenuTreeItem parent_items[64];
    int parent_count = teditor_get_tree_items(ed, lvl - 1, parent_items, 64);
    int hov = ed->tree_hover[lvl - 1];
    if (hov < 0 || hov >= parent_count) return 0;
    if (!parent_items[hov].is_dir) return 0;

    return teditor_scan_fs_dir(parent_items[hov].path, out_items, max_items);
}

void teditor_get_level_box(const TEditor *ed, GDEV *dev, int lvl, RECT *out_box, int *out_count) {
    if (!ed || !out_box || !out_count) return;
    *out_count = 0;
    memset(out_box, 0, sizeof(RECT));

    TMenuTreeItem items[64];
    int count = teditor_get_tree_items(ed, lvl, items, 64);
    if (count <= 0) return;
    *out_count = count;

    if (lvl == 0) {
        H x = ed->menu_bar.headers[0].rect.left + APP_MENU_DROPDOWN_WIDTH - 2;
        H y = APP_MENU_BAR_HEIGHT + 3 + 1 * APP_MENU_ROW_HEIGHT;
        H w = 240;
        H h = count * APP_MENU_ROW_HEIGHT + 6;
        if (dev && x + w > dev->width) x = ed->menu_bar.headers[0].rect.left - w + 2;
        out_box->left = x; out_box->top = y;
        out_box->right = x + w; out_box->bottom = y + h;
        return;
    }

    RECT parent_box;
    int parent_count = 0;
    teditor_get_level_box(ed, dev, lvl - 1, &parent_box, &parent_count);
    int hov = ed->tree_hover[lvl - 1];
    if (hov < 0 || hov >= parent_count) return;

    H w = (lvl == 1) ? 220 : ((lvl == 2) ? 190 : 250);
    H h = count * APP_MENU_ROW_HEIGHT + 6;
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

    for (int lvl = 0; lvl < TEDITOR_TREE_MAX_LEVELS; lvl++) {
        RECT box;
        int count = 0;
        teditor_get_level_box(ed, dev, lvl, &box, &count);
        if (count <= 0) break;

        app_menu_draw_3d_bevel_box(dev, &box);

        TMenuTreeItem items[64];
        int n = teditor_get_tree_items(ed, lvl, items, 64);
        int hov_idx = ed->tree_hover[lvl];

        for (int i = 0; i < n && i < count; i++) {
            RECT row = { box.left + 3, box.top + 3 + i * APP_MENU_ROW_HEIGHT,
                         box.right - 3, box.top + 3 + (i + 1) * APP_MENU_ROW_HEIGHT };

            if (items[i].is_sep) {
                H mid_y = (row.top + row.bottom) / 2;
                drw_lin(dev, row.left + 2, mid_y, row.right - 2, mid_y);
                continue;
            }

            BOOL is_hov = (hov_idx == i);
            if (is_hov) fill_rec(dev, &row, COLOR_NAVY);
            COLOR fg = is_hov ? COLOR_WHITE : COLOR_BLACK;

            drw_tc_string(dev, row.left + 6, row.top + 3, items[i].name, fg, 0x00000000);
            if (items[i].is_dir && lvl > 0) {
                drw_tc_string(dev, row.right - 16, row.top + 3, "▶", fg, 0x00000000);
            }
        }
    }
}

static BOOL teditor_handle_tree_mouse(TEditor *ed, WND *wnd, H rel_x, H rel_y, BOOL is_click) {
    if (!ed || ed->menu_bar.active_menu != 0 || ed->menu_bar.active_submenu != 1) return FALSE;

    GDEV *dev = wnd ? wnd->dev : NULL;

    /* Hit-test from deepest visible level outward */
    for (int lvl = TEDITOR_TREE_MAX_LEVELS - 1; lvl >= 0; lvl--) {
        RECT box;
        int count = 0;
        teditor_get_level_box(ed, dev, lvl, &box, &count);
        if (count <= 0) continue;

        if (rel_x >= box.left && rel_x <= box.right &&
            rel_y >= box.top  && rel_y <= box.bottom) {

            int idx = (rel_y - (box.top + 3)) / APP_MENU_ROW_HEIGHT;
            if (idx < 0 || idx >= count) return TRUE;

            TMenuTreeItem items[64];
            int n = teditor_get_tree_items(ed, lvl, items, 64);
            if (idx >= n) return TRUE;
            if (items[idx].is_sep) return TRUE;

            if (is_click) {
                if (!items[idx].is_dir && items[idx].path[0] != '\0') {
                    teditor_load_file(ed, items[idx].path);
                    if (wnd) snprintf(wnd->title, sizeof(wnd->title), "Editor - %s", ed->filename);
                    teditor_close_menu(ed);
                    return TRUE;
                } else if (items[idx].is_dir) {
                    ed->tree_hover[lvl] = idx;
                    for (int k = lvl + 1; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
                    return TRUE;
                }
            } else {
                /* Hover: set this level hover, and clear all deeper levels */
                if (ed->tree_hover[lvl] != idx) {
                    ed->tree_hover[lvl] = idx;
                    for (int k = lvl + 1; k < TEDITOR_TREE_MAX_LEVELS; k++) {
                        ed->tree_hover[k] = -1;
                    }
                }
                return TRUE;
            }
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

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* Discover .txt files from host directory assets/texts ONLY.
       MD files are mounted inside SYS System Docs, NOT duplicated in root! */
    DIR *d = opendir("assets/texts");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && count < max_files) {
            if (de->d_name[0] == '.') continue;
            size_t nlen = strlen(de->d_name);
            if (nlen > 4 && strcmp(de->d_name + nlen - 4, ".txt") == 0) {
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
    for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
    app_menu_open(&ed->menu_bar, menu_idx);
    teditor_sync_menu_state(ed);
}

void teditor_close_menu(TEditor *ed) {
    if (!ed) return;
    app_menu_close(&ed->menu_bar);
    for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
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
            if (ed->menu_bar.active_submenu != 1) {
                for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
            }
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
            if (ed->menu_bar.active_submenu != 1) {
                for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
            }
            if (cmd != 0) {
                teditor_execute_menu_cmd(ed, wnd, cmd, sub_idx);
            }
            return;
        }
        teditor_sync_menu_state(ed);
        if (ed->menu_bar.active_submenu != 1) {
            for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
        }

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
                ed->menu_bar.active_submenu = 1;
                teditor_sync_menu_state(ed);
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
    for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;
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
    for (int k = 0; k < TEDITOR_TREE_MAX_LEVELS; k++) ed->tree_hover[k] = -1;

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


