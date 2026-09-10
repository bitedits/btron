#include <btron/wnd.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/app_menu.h>
#include <btron/terminal_settings.h>
#include <btron/settings.h>
#include <btron/fs/vol_api.h>
#include "clu.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
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
#define strcat  tkl_strcat
#define strcmp  tkl_strcmp
#define strncmp tkl_strncmp
#define memset  tkl_memset
#define memcpy  tkl_memcpy
#define memmove tkl_memmove
#define strlen  tkl_strlen
#define isspace(c) ((c) == ' ' || (c) == '\t' || (c) == '\n' || (c) == '\r' || (c) == '\f' || (c) == '\v')

static inline char* gterm_strstr(const char *haystack, const char *needle) {
    if (!haystack || !needle) return NULL;
    if (!*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}
#define strstr  gterm_strstr
#endif

#define GTERM_MAX_COLS     256
#define GTERM_MAX_ROWS     32
#define GTERM_HIST_MAX     300
#define GTERM_CMD_HIST_MAX 32

/* ── Terminal Menu Command IDs ──────────────────────────────────────────── */
typedef enum {
    /* ファイル(F) */
    TCMD_FILE_NEW_TERM  = 1,
    TCMD_FILE_SAVE_LOG  = 2,
    TCMD_FILE_CLOSE     = 3,
    /* 編集(E) */
    TCMD_EDIT_COPY      = 10,
    TCMD_EDIT_PASTE     = 11,
    TCMD_EDIT_CLEAR     = 12,
    TCMD_EDIT_CANCEL    = 13,
    TCMD_EDIT_SELECT_ALL= 14,
    /* 表示(V) - Font Size */
    TCMD_VIEW_FONT_12   = 20,
    TCMD_VIEW_FONT_16   = 21,
    TCMD_VIEW_FONT_20   = 22,
    /* 表示(V) - Color Themes */
    TCMD_VIEW_COL_GREEN = 25,
    TCMD_VIEW_COL_AMBER = 26,
    TCMD_VIEW_COL_WHITE = 27,
    TCMD_VIEW_COL_CYAN  = 28,
    TCMD_VIEW_COL_LIGHT = 29,
    /* 表示(V) - Transparency */
    TCMD_VIEW_TRANS_100 = 31,
    TCMD_VIEW_TRANS_80  = 32,
    TCMD_VIEW_TRANS_60  = 33,
    /* 端末(T) */
    TCMD_TERM_RESET     = 40,
    TCMD_TERM_HIST_100  = 41,
    TCMD_TERM_HIST_300  = 42,
    TCMD_TERM_HIST_1000 = 43,
    TCMD_TERM_CUR_LINE  = 44,
    TCMD_TERM_CUR_BLOCK = 45,
    TCMD_TERM_CUR_BAR   = 46,
    TCMD_TERM_SETTINGS  = 47,
    /* ヘルプ(H) */
    TCMD_HELP_CMDS      = 50,
    TCMD_HELP_ABOUT     = 51
} GTERM_CMD;

typedef struct {
    char lines[GTERM_HIST_MAX][GTERM_MAX_COLS + 1];
    COLOR line_cols[GTERM_HIST_MAX];
    int total_lines;

    char input_buf[GTERM_MAX_COLS + 1];
    int input_len;

    char prompt[32];

    char cmd_history[GTERM_CMD_HIST_MAX][256];
    int cmd_hist_count;
    int cmd_hist_idx;

    /* In-Window Application Menu */
    APP_MENU_BAR menu_bar;

    /* Live display settings (synced from global TERMINAL_SETTINGS) */
    COLOR fg_color;        /* Foreground text colour */
    COLOR bg_color;        /* Background colour (includes alpha) */
    int   font_size;       /* Row height: 12, 16, or 20 */
    int   cursor_style;    /* 0=underline, 1=block, 2=bar */
    int   transparency;    /* 0=opaque, 1=80%, 2=60% */
    int   scrollback_max;  /* Max scroll-back lines */
} GTermState;

void gterm_append_line(GTermState *st, const char *text, COLOR col);
static void gterm_init_menu_bar(GTermState *st);
static void gterm_apply_settings(GTermState *st);

static int gterm_calc_text_pixel_width(const char *str) {
    if (!str) return 0;
    int w = 0;
    int i = 0;
    while (str[i] != '\0') {
        unsigned char c = (unsigned char)str[i];
        if (c < 0x80) {
            w += 8;
            i += 1;
        } else if ((c & 0xE0) == 0xC0) {
            w += 8;
            i += 2;
        } else if ((c & 0xF0) == 0xE0) {
            w += 16;
            i += 3;
        } else {
            w += 8;
            i += 1;
        }
    }
    return w;
}

/* ── Apply global TERMINAL_SETTINGS into GTermState live fields ─────────── */
static void gterm_apply_settings(GTermState *st) {
    TERMINAL_SETTINGS cfg;
    terminal_get_settings(&cfg);

    st->font_size      = (int)cfg.font_size;
    st->cursor_style   = (int)cfg.cursor_style;
    st->transparency   = (int)cfg.transparency;
    st->scrollback_max = cfg.scrollback_lines;

    /* Derive colours from theme */
    switch (cfg.theme) {
        case TERM_THEME_GREEN:
            st->fg_color = 0xFF22C55E;
            st->bg_color = terminal_get_effective_bg(&cfg);
            break;
        case TERM_THEME_AMBER:
            st->fg_color = 0xFFF59E0B;
            st->bg_color = terminal_get_effective_bg(&cfg);
            break;
        case TERM_THEME_CYAN:
            st->fg_color = 0xFF38BDF8;
            st->bg_color = terminal_get_effective_bg(&cfg);
            break;
        case TERM_THEME_LIGHT:
            st->fg_color = 0xFF0F172A;
            st->bg_color = terminal_get_effective_bg(&cfg);
            break;
        default: /* TERM_THEME_WHITE */
            st->fg_color = 0xFFFFFFFF;
            st->bg_color = terminal_get_effective_bg(&cfg);
            break;
    }
}

/* ── Initialise the in-window 5-header menu bar ─────────────────────────── */
static void gterm_init_menu_bar(GTermState *st) {
    app_menu_init(&st->menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    /* ファイル(F) */
    int h0 = app_menu_add_header(&st->menu_bar, "ファイル(F)", 104);
    app_menu_add_item(&st->menu_bar, h0, "新規端末 (New Terminal)",   "Ctrl+N", TCMD_FILE_NEW_TERM, TRUE);
    app_menu_add_item(&st->menu_bar, h0, "ログ保存 (Save Log)",       "Ctrl+S", TCMD_FILE_SAVE_LOG, TRUE);
    app_menu_add_separator(&st->menu_bar, h0);
    app_menu_add_item(&st->menu_bar, h0, "閉じる (Close)",             "Ctrl+W", TCMD_FILE_CLOSE,    TRUE);

    /* 編集(E) */
    int h1 = app_menu_add_header(&st->menu_bar, "編集(E)", 72);
    app_menu_add_item(&st->menu_bar, h1, "コピー (Copy)",              "Ctrl+C", TCMD_EDIT_COPY,      TRUE);
    app_menu_add_item(&st->menu_bar, h1, "貼り付け (Paste)",           "Ctrl+V", TCMD_EDIT_PASTE,     TRUE);
    app_menu_add_separator(&st->menu_bar, h1);
    app_menu_add_item(&st->menu_bar, h1, "画面消去 (Clear Screen)",    "Ctrl+L", TCMD_EDIT_CLEAR,     TRUE);
    app_menu_add_item(&st->menu_bar, h1, "入力取消 (Cancel Line)",     "Ctrl+U", TCMD_EDIT_CANCEL,    TRUE);
    app_menu_add_item(&st->menu_bar, h1, "すべて選択 (Select All)",    "Ctrl+A", TCMD_EDIT_SELECT_ALL,TRUE);

    /* 表示(V) */
    int h2 = app_menu_add_header(&st->menu_bar, "表示(V)", 72);
    app_menu_add_item(&st->menu_bar, h2, "フォント小 12px",            "",       TCMD_VIEW_FONT_12,   TRUE);
    app_menu_add_item(&st->menu_bar, h2, "フォント標準 16px",          "",       TCMD_VIEW_FONT_16,   TRUE);
    app_menu_add_item(&st->menu_bar, h2, "フォント大 20px",            "",       TCMD_VIEW_FONT_20,   TRUE);
    app_menu_add_separator(&st->menu_bar, h2);
    app_menu_add_item(&st->menu_bar, h2, "配色：グリーン (Green)",     "",       TCMD_VIEW_COL_GREEN, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "配色：アンバー (Amber)",     "",       TCMD_VIEW_COL_AMBER, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "配色：白黒 (White)",         "",       TCMD_VIEW_COL_WHITE, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "配色：BTRON青 (Cyan)",       "",       TCMD_VIEW_COL_CYAN,  TRUE);
    app_menu_add_item(&st->menu_bar, h2, "配色：ライト (Light)",       "",       TCMD_VIEW_COL_LIGHT, TRUE);
    app_menu_add_separator(&st->menu_bar, h2);
    app_menu_add_item(&st->menu_bar, h2, "不透明 (100% Opaque)",       "",       TCMD_VIEW_TRANS_100, TRUE);
    app_menu_add_item(&st->menu_bar, h2, "20%減光 (80% Dimmed)",       "",       TCMD_VIEW_TRANS_80,  TRUE);
    app_menu_add_item(&st->menu_bar, h2, "40%減光 (60% Dimmed)",       "",       TCMD_VIEW_TRANS_60,  TRUE);

    /* 端末(T) */
    int h3 = app_menu_add_header(&st->menu_bar, "端末(T)", 72);
    app_menu_add_item(&st->menu_bar, h3, "端末リセット (Reset)",       "",       TCMD_TERM_RESET,     TRUE);
    app_menu_add_separator(&st->menu_bar, h3);
    app_menu_add_item(&st->menu_bar, h3, "履歴 100行",                 "",       TCMD_TERM_HIST_100,  TRUE);
    app_menu_add_item(&st->menu_bar, h3, "履歴 300行 (標準)",          "",       TCMD_TERM_HIST_300,  TRUE);
    app_menu_add_item(&st->menu_bar, h3, "履歴 1000行 (大)",           "",       TCMD_TERM_HIST_1000, TRUE);
    app_menu_add_separator(&st->menu_bar, h3);
    app_menu_add_item(&st->menu_bar, h3, "カーソル下線 (_)",           "",       TCMD_TERM_CUR_LINE,  TRUE);
    app_menu_add_item(&st->menu_bar, h3, "ブロックカーソル (█)",       "",       TCMD_TERM_CUR_BLOCK, TRUE);
    app_menu_add_item(&st->menu_bar, h3, "バーカーソル (|)",           "",       TCMD_TERM_CUR_BAR,   TRUE);
    app_menu_add_separator(&st->menu_bar, h3);
    app_menu_add_item(&st->menu_bar, h3, "端末環境設定 (Settings)...", "",       TCMD_TERM_SETTINGS,  TRUE);

    /* ヘルプ(H) */
    int h4 = app_menu_add_header(&st->menu_bar, "ヘルプ(H)", 88);
    app_menu_add_item(&st->menu_bar, h4, "コマンド一覧 (Commands)",   "?",      TCMD_HELP_CMDS,      TRUE);
    app_menu_add_item(&st->menu_bar, h4, "端末について (About)",       "",       TCMD_HELP_ABOUT,     TRUE);
}

static void gterm_init_banner(GTermState *st) {
    memset(st, 0, sizeof(GTermState));
    /* Cho-Kanji style prompt: [/SYS]%  (updated by clu_cd) */
    snprintf(st->prompt, sizeof(st->prompt) - 1, "[%s]%% ", g_cwd_path);

    /* Load initial display settings from global TERMINAL_SETTINGS store */
    gterm_apply_settings(st);

    gterm_append_line(st, "B-System 3.0 Cho-Kanji Shell (gterm)", COLOR_WHITE);
    gterm_append_line(st, "Type 'help' or '?' for available system commands.", COLOR_GREEN);
}

void gterm_append_line(GTermState *st, const char *text, COLOR col) {
    if (!st || !text) return;

    /* Handle multi-line strings by splitting on '\n' */
    const char *p = text;
    while (*p) {
        char seg[GTERM_MAX_COLS + 1];
        int seg_i = 0;
        while (*p && *p != '\n' && seg_i < GTERM_MAX_COLS) {
            seg[seg_i++] = *p++;
        }
        seg[seg_i] = '\0';
        if (*p == '\n') p++;

        if (st->total_lines >= GTERM_HIST_MAX) {
            memmove(&st->lines[0], &st->lines[1], sizeof(st->lines[0]) * (GTERM_HIST_MAX - 1));
            memmove(&st->line_cols[0], &st->line_cols[1], sizeof(st->line_cols[0]) * (GTERM_HIST_MAX - 1));
            st->total_lines = GTERM_HIST_MAX - 1;
        }

        int idx = st->total_lines++;
        strncpy(st->lines[idx], seg, GTERM_MAX_COLS);
        st->lines[idx][GTERM_MAX_COLS] = '\0';
        st->line_cols[idx] = col;
    }
}

static void gterm_shell_out(const char *line, COLOR col, void *user_data) {
    GTermState *st = (GTermState*)user_data;
    if (st) {
        gterm_append_line(st, line, col);
    }
}

void shell_execute_cmd(const char *cmd_line, ShellOutputFn out_fn, void *user_data, WND *wnd) {
    if (!out_fn || !cmd_line) return;

    const char *p = cmd_line;
    while (*p && isspace((unsigned char)*p)) p++;

    if (*p == '\0') {
        return;
    }

    char cmd[64] = {0};
    char arg[256] = {0};
    int cmd_i = 0;
    while (*p && !isspace((unsigned char)*p) && cmd_i < 63) {
        cmd[cmd_i++] = *p++;
    }
    cmd[cmd_i] = '\0';
    while (*p && isspace((unsigned char)*p)) p++;
    int arg_i = 0;
    while (*p && arg_i < 255) {
        arg[arg_i++] = *p++;
    }
    arg[arg_i] = '\0';
    int n = (cmd_i > 0) ? (arg_i > 0 ? 2 : 1) : 0;

    /* ── Cho-Kanji CLU Filesystem Builtins ─────────────────────────────────── */
    if (strcmp(cmd, "ls")     == 0) { clu_ls     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "fs")     == 0) { clu_fs_cmd (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "tp")     == 0) { clu_tp     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "mkf")    == 0) { clu_mkf    (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "cp")     == 0) { clu_cp     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "ln")     == 0) { clu_ln     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "rm")     == 0) { clu_rm     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "ren")    == 0) { clu_ren    (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "apd")    == 0) { clu_apd    (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "df")     == 0) { clu_df     (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "sync")   == 0) { clu_sync_cmd(arg, out_fn, user_data); return; }
    if (strcmp(cmd, "empf")   == 0) { clu_empf   (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "chmod")  == 0) { clu_chmod  (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "touch")  == 0) { clu_touch  (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "chtime") == 0) { clu_chtime (arg, out_fn, user_data); return; }
    if (strcmp(cmd, "cd")     == 0) {
        clu_cd(arg, out_fn, user_data);
        /* Update gterm prompt after CWD change */
        GTermState *st_cd = (GTermState *)user_data;
        if (st_cd) snprintf(st_cd->prompt, sizeof(st_cd->prompt) - 1, "[%s]%% ", g_cwd_path);
        return;
    }
    /* ── END CLU Builtins ────────────────────────────────────────────────── */

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        out_fn("B-System Cho-Kanji Shell Commands:", COLOR_GREEN, user_data);
        out_fn("── Volume / Filesystem (CLU) ─────────────────────", COLOR_CYAN, user_data);
        out_fn("  ls [-l|-t]          - List /SYS volume files", COLOR_LTGRAY, user_data);
        out_fn("  fs [-l] <file>      - Show record index of file", COLOR_LTGRAY, user_data);
        out_fn("  tp [-x|-a] <file>   - Type/dump file TAD content", COLOR_LTGRAY, user_data);
        out_fn("  mkf <name>          - Create new Real Body", COLOR_LTGRAY, user_data);
        out_fn("  cp <src> <dst>      - Copy Real Body", COLOR_LTGRAY, user_data);
        out_fn("  ln <src> <link>     - Create RT_LINK Virtual Body", COLOR_LTGRAY, user_data);
        out_fn("  rm <name>           - Delete Real Body", COLOR_LTGRAY, user_data);
        out_fn("  ren <old> <new>     - Rename Real Body", COLOR_LTGRAY, user_data);
        out_fn("  empf <name>         - Empty all records from file", COLOR_LTGRAY, user_data);
        out_fn("  apd -d#.# <name>    - Delete records from file", COLOR_LTGRAY, user_data);
        out_fn("  chmod -a# <name>    - Change file attributes", COLOR_LTGRAY, user_data);
        out_fn("  touch <name>        - Update access/modify time", COLOR_LTGRAY, user_data);
        out_fn("  chtime <opts> <n>   - Change file timestamps", COLOR_LTGRAY, user_data);
        out_fn("  df                  - Volume free-space statistics", COLOR_LTGRAY, user_data);
        out_fn("  sync                - Flush all volume caches to disk", COLOR_LTGRAY, user_data);
        out_fn("  cd [/SYS|name]      - Change working volume path", COLOR_LTGRAY, user_data);
        out_fn("── System ────────────────────────────────────────", COLOR_CYAN, user_data);
        out_fn("  help, ?             - Show command help list", COLOR_LTGRAY, user_data);
        out_fn("  ver, uname          - System version & kernel info", COLOR_LTGRAY, user_data);
        out_fn("  devconf             - Registered hardware / device drivers", COLOR_LTGRAY, user_data);
        out_fn("  ps                  - Process and window task table", COLOR_LTGRAY, user_data);
        out_fn("  echo <text>         - Print string", COLOR_LTGRAY, user_data);
        out_fn("  mem                 - Memory pool allocation statistics", COLOR_LTGRAY, user_data);
        out_fn("  mouse status|move|click - Cursor control", COLOR_LTGRAY, user_data);
        out_fn("── Applications ──────────────────────────────────", COLOR_CYAN, user_data);
        out_fn("  edit, editor        - Launch Editor instance", COLOR_LTGRAY, user_data);
        out_fn("  tad, browser        - Launch TAD Browser instance", COLOR_LTGRAY, user_data);
        out_fn("  chat                - Launch BeOS Chat instance", COLOR_LTGRAY, user_data);
        out_fn("  audio               - Launch Audio Player instance", COLOR_LTGRAY, user_data);
        out_fn("  cabinet             - Launch Cabinet Explorer instance", COLOR_LTGRAY, user_data);
        out_fn("  term, gterm         - Launch new Terminal instance", COLOR_LTGRAY, user_data);
        out_fn("  date                - Current system time & date", COLOR_LTGRAY, user_data);
        out_fn("  clear, cls          - Clear terminal screen", COLOR_LTGRAY, user_data);
        out_fn("  history             - Show command history", COLOR_LTGRAY, user_data);
        out_fn("  ski, bootman        - Ski Bootloader interactive manager", COLOR_LTGRAY, user_data);
        out_fn("  exit, quit          - Close terminal window", COLOR_LTGRAY, user_data);
    } else if (strcmp(cmd, "ski") == 0 || strcmp(cmd, "bootman") == 0 || strcmp(cmd, "boot") == 0) {
        out_fn("🎿 Ski Bootloader (Bootman v1.0 · B-System OS.1):", COLOR_CYAN, user_data);
        out_fn("  [1] B-System BTRON3 Workstation (x86_64 UEFI SMP)", COLOR_WHITE, user_data);
        out_fn("  [2] B-System BTRON3 (Raspberry Pi 5 ARM64 BCM2712)", COLOR_WHITE, user_data);
        out_fn("  [3] B-System BTRON3 (Raspberry Pi 4/3 ARM64 BCM283x)", COLOR_WHITE, user_data);
        out_fn("  [4] TRON T-Kernel 2.0 (Sakamura Core)", COLOR_WHITE, user_data);
        out_fn("  [5] NEC PC-98 BTRON3 (i386/Pentium — Awe Morris)", COLOR_WHITE, user_data);
        out_fn("  [6] NEC PC-98 Linux 7.1 i386 (PC-9801/21)", COLOR_WHITE, user_data);
        out_fn("  [7] seL4 Microkernel (VirtIO VM)", COLOR_WHITE, user_data);
        out_fn("  [8] NetBSD 11.0 (smolBSD MICROVM)", COLOR_WHITE, user_data);
        if (arg[0] != '\0') {
            char bmsg[128];
            snprintf(bmsg, sizeof(bmsg), "Ski target switch set to '%s' (active)", arg);
            out_fn(bmsg, COLOR_GREEN, user_data);
        } else {
            out_fn("Type 'ski <target_id>' to switch or launch boot target directly.", COLOR_YELLOW, user_data);
        }
    } else if (strcmp(cmd, "ver") == 0 || strcmp(cmd, "uname") == 0) {
        btron_core_print_ver(out_fn, user_data, arg);
    } else if (strcmp(cmd, "devconf") == 0) {
        char dbuf[512];
        sys_get_devconf(dbuf, sizeof(dbuf));
        out_fn(dbuf, COLOR_CYAN, user_data);
    } else if (strcmp(cmd, "mem") == 0) {
        uint32_t base = 0, limit = 0, used = 0;
        sys_get_mem_stats(&base, &limit, &used);
        out_fn("T-Kernel 2.0 Memory Pool Statistics:", COLOR_CYAN, user_data);
        char mbuf[128];
        snprintf(mbuf, sizeof(mbuf), "  Heap Base  : 0x%08X", base);
        out_fn(mbuf, COLOR_LTGRAY, user_data);
        snprintf(mbuf, sizeof(mbuf), "  Heap Limit : 0x%08X", limit);
        out_fn(mbuf, COLOR_LTGRAY, user_data);
        snprintf(mbuf, sizeof(mbuf), "  Heap Used  : 0x%08X (%u bytes)", used, used);
        out_fn(mbuf, COLOR_LTGRAY, user_data);
    } else if (strcmp(cmd, "mouse") == 0) {
        if (strcmp(arg, "status") == 0 || arg[0] == '\0') {
            H mx = 0, my = 0;
            sys_mouse_get_pos(&mx, &my);
            char pos_buf[128];
            snprintf(pos_buf, sizeof(pos_buf), "Mouse Cursor Position: X=%d, Y=%d", mx, my);
            out_fn(pos_buf, COLOR_CYAN, user_data);
        } else if (strncmp(arg, "move", 4) == 0) {
            const char *sp = arg + 4;
            while (*sp == ' ' || *sp == '\t') sp++;
            int mx = 0, my = 0;
            while (*sp >= '0' && *sp <= '9') { mx = mx * 10 + (*sp - '0'); sp++; }
            while (*sp == ' ' || *sp == '\t') sp++;
            while (*sp >= '0' && *sp <= '9') { my = my * 10 + (*sp - '0'); sp++; }
            sys_mouse_set_pos((H)mx, (H)my);
            char mbuf[128];
            snprintf(mbuf, sizeof(mbuf), "Moved mouse cursor to (%d, %d)", mx, my);
            out_fn(mbuf, COLOR_GREEN, user_data);
        } else if (strncmp(arg, "click", 5) == 0) {
            const char *sp = arg + 5;
            while (*sp == ' ' || *sp == '\t') sp++;
            int mx = 0, my = 0;
            while (*sp >= '0' && *sp <= '9') { mx = mx * 10 + (*sp - '0'); sp++; }
            while (*sp == ' ' || *sp == '\t') sp++;
            while (*sp >= '0' && *sp <= '9') { my = my * 10 + (*sp - '0'); sp++; }
            if (mx > 0 || my > 0) {
                sys_mouse_click((H)mx, (H)my);
                char cbuf[128];
                snprintf(cbuf, sizeof(cbuf), "Simulated mouse click at (%d, %d)", mx, my);
                out_fn(cbuf, COLOR_GREEN, user_data);
            } else {
                H cur_x = 0, cur_y = 0;
                sys_mouse_get_pos(&cur_x, &cur_y);
                sys_mouse_click(cur_x, cur_y);
                out_fn("Simulated mouse click at current cursor position", COLOR_GREEN, user_data);
            }
        }
    } else if (strcmp(cmd, "pwd") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        char cwd[256];
        if (getcwd(cwd, sizeof(cwd))) {
            out_fn(cwd, COLOR_LTGRAY, user_data);
        } else {
            out_fn("Error: cannot get working directory", COLOR_RED, user_data);
        }
#else
        out_fn("/sys/btron_root", COLOR_LTGRAY, user_data);
#endif
    } else if (strcmp(cmd, "dir") == 0) {
        /* 'dir' → same as clu_ls but allow legacy use */
        clu_ls(arg, out_fn, user_data);
        return;
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "dir") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        const char *dir_path = (n > 1 && arg[0]) ? arg : ".";
        DIR *dir = opendir(dir_path);
        if (!dir) {
            char err[280];
            snprintf(err, sizeof(err), "ls: cannot access '%s'", dir_path);
            out_fn(err, COLOR_RED, user_data);
        } else {
            struct dirent *entry;
            char line_buf[256] = "";
            int count = 0;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.' && strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
                    continue;
                }
                char name_fmt[64];
                snprintf(name_fmt, sizeof(name_fmt), "%-18s ", entry->d_name);
                strncat(line_buf, name_fmt, sizeof(line_buf) - strlen(line_buf) - 1);
                count++;
                if (count % 3 == 0) {
                    out_fn(line_buf, COLOR_LTGRAY, user_data);
                    line_buf[0] = '\0';
                }
            }
            if (line_buf[0] != '\0') {
                out_fn(line_buf, COLOR_LTGRAY, user_data);
            }
            closedir(dir);
        }
#else
        out_fn("BTRON3_Report.txt  README.txt          btron_store/       ", COLOR_LTGRAY, user_data);
        out_fn("dev/screen0        dev/uart0           kernel.sys         ", COLOR_LTGRAY, user_data);
#endif
    } else if (strcmp(cmd, "cat") == 0) {
        if (n <= 1 || !arg[0]) {
            out_fn("cat: missing file argument", COLOR_RED, user_data);
        } else {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
            FILE *f = fopen(arg, "r");
            if (!f) {
                char err[280];
                snprintf(err, sizeof(err), "cat: %s: No such file", arg);
                out_fn(err, COLOR_RED, user_data);
            } else {
                char buf[256];
                int max_read = 30;
                while (fgets(buf, sizeof(buf), f) && max_read-- > 0) {
                    out_fn(buf, COLOR_LTGRAY, user_data);
                }
                fclose(f);
            }
#else
            out_fn("【BTRON3仕様の新実装】報告文書", COLOR_CYAN, user_data);
            out_fn("宛先：ノルティアオーダー／TADワーキンググループ 小島秀樹様", COLOR_LTGRAY, user_data);
#endif
        }
    } else if (strcmp(cmd, "echo") == 0) {
        out_fn(arg, COLOR_LTGRAY, user_data);
    } else if (strcmp(cmd, "desktop") == 0 || strcmp(cmd, "startx") == 0 || strcmp(cmd, "gui") == 0) {
        out_fn("B-System BTRON3 Graphical Window Compositor & Desktop: Active", COLOR_GREEN, user_data);
        out_fn("  Resolution : 1024x768 (32-bpp Linear Framebuffer)", COLOR_CYAN, user_data);
        out_fn("  Subsystems : HFDS Cabinet, GTerm Terminal, Editor, Mozc IME", COLOR_LTGRAY, user_data);
        out_fn("  SMP Engine : 4 Cores Live & Scheduled", COLOR_YELLOW, user_data);
    } else if (strcmp(cmd, "ps") == 0) {
        int num_cores = 1;
#if defined(BTRON_SMP)
        extern volatile uint32_t g_num_cpus;
        if (g_num_cpus > 0) num_cores = (int)g_num_cpus;
#endif
        out_fn("PID   CORE  TASK           STAT   ADDR         BOUNDS    TITLE", COLOR_CYAN, user_data);
        out_fn("-------------------------------------------------------------------------", COLOR_DKGRAY, user_data);
        char sline[256];
        snprintf(sline, sizeof(sline), "  1   #%-3d  tk_desktop     RUN    0x01020000   1024x768  [B-System Desktop]", 0);
        out_fn(sline, COLOR_GREEN, user_data);
        snprintf(sline, sizeof(sline), "  2   #%-3d  tk_wnd_mgr     READY  0x01040000   1024x768  [Window Compositor]", (1 % num_cores));
        out_fn(sline, COLOR_GREEN, user_data);
        snprintf(sline, sizeof(sline), "  3   #%-3d  tk_tip_ime     READY  0x010A0000   Candidate [Mozc Japanese IME]", (2 % num_cores));
        out_fn(sline, COLOR_GREEN, user_data);

        WND *w = get_wnd_list();
        if (!w) {
            out_fn("  (No active user application instances)", COLOR_LTGRAY, user_data);
        } else {
            WND *w_list[64];
            int count = 0;
            for (WND *curr = w; curr && count < 64; curr = curr->next) {
                w_list[count++] = curr;
            }

            for (int i = 0; i < count; i++) {
                WND *cw = w_list[i];
                const char *tname = "btron_app";
                if (strstr(cw->title, "gterm") || strstr(cw->title, "Terminal")) tname = "gterm";
                else if (strstr(cw->title, "Editor") || strstr(cw->title, "Editor")) tname = "t_editor";
                else if (strstr(cw->title, "TAD") || strstr(cw->title, "仕様書")) tname = "tad_browser";
                else if (strstr(cw->title, "Cabinet") || strstr(cw->title, "キャビネット")) tname = "vobj_mgr";
                else if (strstr(cw->title, "TC-K777ES") || strstr(cw->title, "SONY")) tname = "audio_player";
                else if (strstr(cw->title, "Chat") || strstr(cw->title, "対話") || strstr(cw->title, "会話") || strstr(cw->title, "Blabber")) tname = "beos_chat";

                /* Calculate instance index for this task type */
                int inst_num = 1;
                for (int j = 0; j < i; j++) {
                    WND *prev_w = w_list[j];
                    const char *pname = "btron_app";
                    if (strstr(prev_w->title, "gterm") || strstr(prev_w->title, "Terminal")) pname = "gterm";
                    else if (strstr(prev_w->title, "Editor") || strstr(prev_w->title, "Editor")) pname = "t_editor";
                    else if (strstr(prev_w->title, "TAD") || strstr(prev_w->title, "仕様書")) pname = "tad_browser";
                    else if (strstr(prev_w->title, "Cabinet") || strstr(prev_w->title, "キャビネット")) pname = "vobj_mgr";
                    else if (strstr(prev_w->title, "TC-K777ES") || strstr(prev_w->title, "SONY")) pname = "audio_player";
                    else if (strstr(prev_w->title, "Chat") || strstr(prev_w->title, "対話") || strstr(prev_w->title, "会話") || strstr(prev_w->title, "Blabber")) pname = "beos_chat";

                    if (strcmp(pname, tname) == 0) inst_num++;
                }

                char task_with_inst[32];
                snprintf(task_with_inst, sizeof(task_with_inst), "%s#%d", tname, inst_num);

                H ww = cw->bounds.right - cw->bounds.left;
                H wh = cw->bounds.bottom - cw->bounds.top;
                char bounds_str[32];
                snprintf(bounds_str, sizeof(bounds_str), "%dx%d", ww, wh);

                int task_core = (cw->id + i) % num_cores;
                char line[256];
                snprintf(line, sizeof(line), "%3d   #%-3d  %-14s %-6s 0x%08lx %-9s %s",
                         cw->id,
                         task_core,
                         task_with_inst,
                         cw->focused ? "RUN" : "SLEEP",
                         (unsigned long)((uintptr_t)cw & 0xFFFFFFFF),
                         bounds_str,
                         cw->title);

                COLOR col = cw->focused ? COLOR_YELLOW : COLOR_LTGRAY;
                out_fn(line, col, user_data);
            }
        }
    } else if (strcmp(cmd, "editor") == 0 || strcmp(cmd, "edit") == 0) {
        open_t_editor_window();
        out_fn("Started new Editor instance", COLOR_GREEN, user_data);
    } else if (strcmp(cmd, "tad") == 0 || strcmp(cmd, "browser") == 0) {
        open_tad_browser_window("tad_bin/01_btron3_spec.tad", "【仕様書】BTRON3 3.20");
        out_fn("Started new TAD Browser instance", COLOR_GREEN, user_data);
    } else if (strcmp(cmd, "chat") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        launch_beos_chat();
        out_fn("Started new BeOS Chat instance", COLOR_GREEN, user_data);
#else
        out_fn("BeOS Chat is supported in POSIX / Hosted mode", COLOR_YELLOW, user_data);
#endif
    } else if (strcmp(cmd, "audio") == 0 || strcmp(cmd, "player") == 0 || strcmp(cmd, "cassette") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        open_audio_player_window();
        out_fn("Started SONY Cassette Deck instance", COLOR_GREEN, user_data);
#else
        out_fn("SONY Cassette Deck is supported in POSIX / Hosted mode", COLOR_YELLOW, user_data);
#endif
    } else if (strcmp(cmd, "cabinet") == 0 || strcmp(cmd, "vobjmgr") == 0) {
        open_vobj_manager_window();
        out_fn("Started Cabinet Explorer instance", COLOR_GREEN, user_data);
    } else if (strcmp(cmd, "gterm") == 0 || strcmp(cmd, "term") == 0) {
        open_gterm_window();
        out_fn("Started new Terminal instance", COLOR_GREEN, user_data);
    } else if (strcmp(cmd, "vobj") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        DIR *dir = opendir("./btron_store");
        if (!dir) {
            out_fn("vobj: btron_store directory not found", COLOR_RED, user_data);
        } else {
            out_fn("B-System Real/Virtual Body Store:", COLOR_CYAN, user_data);
            struct dirent *entry;
            while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] != '.') {
                    char item[128];
                    snprintf(item, sizeof(item), "  [VOBJ] %s", entry->d_name);
                    out_fn(item, COLOR_GREEN, user_data);
                }
            }
            closedir(dir);
        }
#else
        out_fn("B-System Real/Virtual Body Store:", COLOR_CYAN, user_data);
        out_fn("  [VOBJ] BTRON3_Report.txt (RealObject #101)", COLOR_GREEN, user_data);
        out_fn("  [VOBJ] Kojima_Hideki_Link.vlk (VirtualLink #102)", COLOR_GREEN, user_data);
        out_fn("  [VOBJ] TKernel_Subsystem.sys (RealObject #103)", COLOR_GREEN, user_data);
#endif
    } else if (strcmp(cmd, "date") == 0) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        time_t t = time(NULL);
        struct tm *tm_info = localtime(&t);
        char tbuf[128];
        strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M:%S %Z", tm_info);
        out_fn(tbuf, COLOR_LTGRAY, user_data);
#else
        out_fn("2026-08-28 12:00:00 JST", COLOR_LTGRAY, user_data);
#endif
    } else if (strcmp(cmd, "clear") == 0 || strcmp(cmd, "cls") == 0) {
        /* Screen cleared via shell caller */
    } else if (strcmp(cmd, "exit") == 0 || strcmp(cmd, "quit") == 0) {
        if (wnd) {
            cls_wnd(wnd);
        }
    } else {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        /* Fallback: Execute external shell command via popen */
        FILE *pipe = popen(cmd_line, "r");
        if (!pipe) {
            char err[280];
            snprintf(err, sizeof(err), "gterm: command not found: %s", cmd);
            out_fn(err, COLOR_RED, user_data);
        } else {
            char linebuf[256];
            int max_lines = 40;
            while (fgets(linebuf, sizeof(linebuf), pipe) && max_lines-- > 0) {
                out_fn(linebuf, COLOR_LTGRAY, user_data);
            }
            pclose(pipe);
        }
#else
        char err[280];
        snprintf(err, sizeof(err), "gterm: command not found: %s", cmd);
        out_fn(err, COLOR_RED, user_data);
#endif
    }
}

static void gterm_execute_cmd(WND *wnd, GTermState *st, const char *cmd_line) {
    if (!st || !cmd_line) return;

    /* Echo command line into terminal */
    char echo_buf[300];
    snprintf(echo_buf, sizeof(echo_buf), "%s%s", st->prompt, cmd_line);
    gterm_append_line(st, echo_buf, COLOR_WHITE);

    /* Record in history */
    if (cmd_line[0] != '\0') {
        if (st->cmd_hist_count < GTERM_CMD_HIST_MAX) {
            strncpy(st->cmd_history[st->cmd_hist_count++], cmd_line, 255);
        } else {
            memmove(&st->cmd_history[0], &st->cmd_history[1], sizeof(st->cmd_history[0]) * (GTERM_CMD_HIST_MAX - 1));
            strncpy(st->cmd_history[GTERM_CMD_HIST_MAX - 1], cmd_line, 255);
        }
    }
    st->cmd_hist_idx = st->cmd_hist_count;

    if (strcmp(cmd_line, "history") == 0) {
        gterm_append_line(st, "Command History:", COLOR_CYAN);
        for (int i = 0; i < st->cmd_hist_count; i++) {
            char hline[280];
            snprintf(hline, sizeof(hline), " %2d  %s", i + 1, st->cmd_history[i]);
            gterm_append_line(st, hline, COLOR_LTGRAY);
        }
    } else if (strcmp(cmd_line, "clear") == 0 || strcmp(cmd_line, "cls") == 0) {
        st->total_lines = 0;
    } else {
        shell_execute_cmd(cmd_line, gterm_shell_out, st, wnd);
    }
}

/* ASCII Key with Shift / CapsLock Translation */
static char get_ascii_char_with_shift(UW key, uint16_t mod) {
    BOOL shift = (mod & BTRON_KMOD_SHIFT) != 0;
    BOOL caps  = (mod & BTRON_KMOD_CAPS) != 0;

    if (key >= 'A' && key <= 'Z') {
        if (shift ^ caps) return (char)key;
        return (char)(key - 'A' + 'a');
    }

    if (key >= 'a' && key <= 'z') {
        if (shift ^ caps) return (char)(key - 'a' + 'A');
        return (char)key;
    }

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

/* ── Dispatch a GTERM_CMD from the menu to update live terminal state ────── */
static void gterm_dispatch_cmd(WND *wnd, GTermState *st, int cmd) {
    TERMINAL_SETTINGS cfg;
    terminal_get_settings(&cfg);

    switch ((GTERM_CMD)cmd) {
        /* ── ファイル ─────────────────────────────── */
        case TCMD_FILE_NEW_TERM:
            open_gterm_window();
            return;
        case TCMD_FILE_SAVE_LOG:
            gterm_append_line(st, "[gterm] Log save: not yet implemented", COLOR_YELLOW);
            return;
        case TCMD_FILE_CLOSE:
            cls_wnd(wnd);
            return;

        /* ── 編集 ─────────────────────────────────── */
        case TCMD_EDIT_CLEAR:
            st->total_lines = 0;
            return;
        case TCMD_EDIT_CANCEL:
            st->input_buf[0] = '\0';
            st->input_len = 0;
            tip_cancel();
            return;
        case TCMD_EDIT_SELECT_ALL:
        case TCMD_EDIT_COPY:
        case TCMD_EDIT_PASTE:
            /* clipboard stubs — host integration TBD */
            return;

        /* ── 表示: Font Size ──────────────────────── */
        case TCMD_VIEW_FONT_12: st->font_size = 12; cfg.font_size = TERM_FONT_12; terminal_set_settings(&cfg); return;
        case TCMD_VIEW_FONT_16: st->font_size = 16; cfg.font_size = TERM_FONT_16; terminal_set_settings(&cfg); return;
        case TCMD_VIEW_FONT_20: st->font_size = 20; cfg.font_size = TERM_FONT_20; terminal_set_settings(&cfg); return;

        /* ── 表示: Colour Themes ──────────────────── */
        case TCMD_VIEW_COL_GREEN:
            cfg.theme = TERM_THEME_GREEN; cfg.fg_color = 0xFF22C55E;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_COL_AMBER:
            cfg.theme = TERM_THEME_AMBER; cfg.fg_color = 0xFFF59E0B;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_COL_WHITE:
            cfg.theme = TERM_THEME_WHITE; cfg.fg_color = 0xFFFFFFFF;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_COL_CYAN:
            cfg.theme = TERM_THEME_CYAN; cfg.fg_color = 0xFF38BDF8;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_COL_LIGHT:
            cfg.theme = TERM_THEME_LIGHT; cfg.fg_color = 0xFF0F172A;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;

        /* ── 表示: Transparency ───────────────────── */
        case TCMD_VIEW_TRANS_100:
            cfg.transparency = TERM_TRANSPARENCY_OPAQUE;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_TRANS_80:
            cfg.transparency = TERM_TRANSPARENCY_80;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;
        case TCMD_VIEW_TRANS_60:
            cfg.transparency = TERM_TRANSPARENCY_60;
            terminal_set_settings(&cfg); gterm_apply_settings(st); return;

        /* ── 端末 ─────────────────────────────────── */
        case TCMD_TERM_RESET:
            st->total_lines = 0;
            gterm_init_banner(st);
            gterm_execute_cmd(wnd, st, "ver");
            return;
        case TCMD_TERM_HIST_100:  st->scrollback_max = 100;  cfg.scrollback_lines = 100;  terminal_set_settings(&cfg); return;
        case TCMD_TERM_HIST_300:  st->scrollback_max = 300;  cfg.scrollback_lines = 300;  terminal_set_settings(&cfg); return;
        case TCMD_TERM_HIST_1000: st->scrollback_max = 1000; cfg.scrollback_lines = 1000; terminal_set_settings(&cfg); return;
        case TCMD_TERM_CUR_LINE:  st->cursor_style = 0; cfg.cursor_style = TERM_CURSOR_UNDERLINE; terminal_set_settings(&cfg); return;
        case TCMD_TERM_CUR_BLOCK: st->cursor_style = 1; cfg.cursor_style = TERM_CURSOR_BLOCK;     terminal_set_settings(&cfg); return;
        case TCMD_TERM_CUR_BAR:   st->cursor_style = 2; cfg.cursor_style = TERM_CURSOR_BAR;       terminal_set_settings(&cfg); return;
        case TCMD_TERM_SETTINGS:
            open_terminal_settings_window();
            return;

        /* ── ヘルプ ───────────────────────────────── */
        case TCMD_HELP_CMDS:
            shell_execute_cmd("help", gterm_shell_out, st, wnd);
            return;
        case TCMD_HELP_ABOUT:
            open_gterm_about_window();
            return;
        default:
            return;
    }
}

WND* open_gterm_about_window(void) {
    return app_menu_create_about_dialog("gterm", "ターミナル",
        "B-System Terminal Emulator (VT100/TRON Console)",
        "Brought to B-System by 5HT",
        300, 200);
}

static void handle_gterm_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    if (!st) return;

    /* Compute relative coordinates (inside client, accounting for border+title) */
    H rel_x = evt->pos.x - wnd->client.left;
    H rel_y = evt->pos.y - wnd->client.top;

    /* ── Mouse Move: forward to menu bar ─────────────────────────────────── */
    if (evt->type == EV_MOUSE_MOVE) {
        if (st->menu_bar.header_count > 0) {
            app_menu_handle_mouse_move(&st->menu_bar, rel_x, rel_y);
        }
        return;
    }

    /* ── Mouse Down: menu first, then terminal area ───────────────────────── */
    if (evt->type == EV_BUT_DOWN) {
        if (st->menu_bar.header_count > 0) {
            int cmd = 0, sub_idx = -1;
            if (app_menu_handle_mouse_down(&st->menu_bar, rel_x, rel_y, &cmd, &sub_idx)) {
                if (cmd != 0) {
                    gterm_dispatch_cmd(wnd, st, cmd);
                }
                return;
            }
        }
        /* Click in terminal canvas — close any open menu */
        if (st->menu_bar.active_menu >= 0) {
            app_menu_close(&st->menu_bar);
        }
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        UW key_code = evt->key;
        uint16_t mod = (uint16_t)(uintptr_t)evt->data;

        /* Forward to in-window menu bar shortcuts & keyboard navigation */
        int menu_cmd = 0;
        if (app_menu_handle_key(&st->menu_bar, key_code, mod, &menu_cmd)) {
            if (menu_cmd != 0) {
                gterm_dispatch_cmd(wnd, st, menu_cmd);
            }
            return;
        }

        char commit_buf[128] = "";
        BOOL ctrl = (mod & BTRON_KMOD_CTRL) != 0;

        /* Control key combinations */
        if (ctrl) {
            if (key_code == 'c' || key_code == 'C') {
                char cancel_msg[280];
                snprintf(cancel_msg, sizeof(cancel_msg), "%s%s^C", st->prompt, st->input_buf);
                gterm_append_line(st, cancel_msg, COLOR_RED);
                st->input_buf[0] = '\0';
                st->input_len = 0;
                tip_cancel();
                return;
            } else if (key_code == 'l' || key_code == 'L') {
                st->total_lines = 0;
                return;
            } else if (key_code == 'u' || key_code == 'U') {
                st->input_buf[0] = '\0';
                st->input_len = 0;
                tip_cancel();
                return;
            }
        }

        /* Check if TIP handles key (Japanese IME mode or F10 / Ctrl+Space toggle) */
        if (tip_process_key(key_code, mod, commit_buf, sizeof(commit_buf))) {
            if (commit_buf[0] != '\0') {
                int clen = (int)strlen(commit_buf);
                if (st->input_len + clen < GTERM_MAX_COLS - (int)strlen(st->prompt) - 2) {
                    strcat(st->input_buf, commit_buf);
                    st->input_len += clen;
                }
            }
            return;
        }

        UW sym = key_code;

        /* Non-printable navigation & action keys */
        if (sym == BTRON_KEY_RETURN || sym == BTRON_KEY_KP_ENTER || sym == '\r' || sym == '\n') {
            gterm_execute_cmd(wnd, st, st->input_buf);
            st->input_buf[0] = '\0';
            st->input_len = 0;
            return;
        } else if (sym == BTRON_KEY_BACKSPACE || sym == 0x08) {
            if (st->input_len > 0) {
                int prev_c = st->input_len - 1;
                while (prev_c > 0 && ((unsigned char)st->input_buf[prev_c] & 0xC0) == 0x80) {
                    prev_c--;
                }
                st->input_buf[prev_c] = '\0';
                st->input_len = prev_c;
            }
            return;
        } else if (sym == BTRON_KEY_UP) {
            if (st->cmd_hist_count > 0 && st->cmd_hist_idx > 0) {
                st->cmd_hist_idx--;
                strncpy(st->input_buf, st->cmd_history[st->cmd_hist_idx], sizeof(st->input_buf) - 1);
                st->input_len = (int)strlen(st->input_buf);
            }
            return;
        } else if (sym == BTRON_KEY_DOWN) {
            if (st->cmd_hist_idx < st->cmd_hist_count - 1) {
                st->cmd_hist_idx++;
                strncpy(st->input_buf, st->cmd_history[st->cmd_hist_idx], sizeof(st->input_buf) - 1);
                st->input_len = (int)strlen(st->input_buf);
            } else {
                st->cmd_hist_idx = st->cmd_hist_count;
                st->input_buf[0] = '\0';
                st->input_len = 0;
            }
            return;
        } else if (sym == BTRON_KEY_TAB || sym == '\t') {
            if (st->input_len + 4 < GTERM_MAX_COLS - (int)strlen(st->prompt) - 2) {
                strcat(st->input_buf, "    ");
                st->input_len += 4;
            }
            return;
        }

        /* Direct English / Printable ASCII Text Input */
        if (!ctrl && sym >= 32 && sym <= 126) {
            char ch = get_ascii_char_with_shift((UW)sym, mod);
            if (st->input_len < GTERM_MAX_COLS - (int)strlen(st->prompt) - 2) {
                st->input_buf[st->input_len++] = ch;
                st->input_buf[st->input_len] = '\0';
            }
            return;
        }
    }
}

static void paint_gterm(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    if (!st) return;

    /* Initialise menu bar on first paint */
    if (st->menu_bar.header_count == 0) {
        gterm_init_menu_bar(st);
    }

    /* Determine effective colours and row height from live settings */
    COLOR eff_bg = st->bg_color ? st->bg_color : 0xCC000000;
    COLOR eff_fg = st->fg_color ? st->fg_color : 0xFFFFFFFF;
    int   row_h  = (st->font_size >= 12 && st->font_size <= 20) ? st->font_size : 16;

    /* ── 1. Full-window background ─────────────────────────────────────── */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, eff_bg);

    /* ── 2. In-Window Menu Bar (y = 0..APP_MENU_BAR_HEIGHT) ───────────── */
    app_menu_set_right_text(&st->menu_bar,
        (tip_get_mode() == TIP_MODE_HIRAGANA) ? "[JP あ | F10]"
      : (tip_get_mode() == TIP_MODE_KATAKANA) ? "[JP ア | F10]"
      :                                          "[EN A | F10]");
    app_menu_paint_bar(&st->menu_bar, dev);

    /* ── 3. Terminal Canvas (starts below menu bar) ────────────────────── */
    int canvas_top = APP_MENU_BAR_HEIGHT + 2;   /* 2px separator gap */
    int max_disp_rows = (dev->height - canvas_top - row_h - 4) / row_h;
    if (max_disp_rows < 3) max_disp_rows = 3;

    int start_line = 0;
    if (st->total_lines > max_disp_rows) {
        start_line = st->total_lines - max_disp_rows;
    }

    int y = canvas_top + 2;
    for (int i = start_line; i < st->total_lines; i++) {
        COLOR col = st->line_cols[i];
        if (col == 0) col = eff_fg;
        drw_tc_string(dev, 8, y, st->lines[i], col, eff_bg);
        y += row_h;
    }

    /* ── 4. Prompt line with TIP inline preview and cursor ─────────────── */
    char prompt_line[300];
    snprintf(prompt_line, sizeof(prompt_line), "%s%s", st->prompt, st->input_buf);
    drw_tc_string(dev, 8, y, prompt_line, eff_fg, eff_bg);

    int prompt_pixel_w = 8 + gterm_calc_text_pixel_width(prompt_line);
    if (wnd->focused && tip_get_state() != TIP_STATE_IDLE) {
        char comp_buf[128];
        tip_get_converted_text(comp_buf, sizeof(comp_buf));
        BOOL is_dotted = (tip_get_state() == TIP_STATE_PRECOMP);
        drw_tc_string_underlined(dev, prompt_pixel_w, y, comp_buf, COLOR_CYAN, eff_bg, is_dotted);
        tip_set_caret_pos(wnd->bounds.left + prompt_pixel_w, wnd->bounds.top + y);
    } else if (wnd->focused) {
        /* Cursor shape: underline '_', block '█', or bar '|' */
        const char *cursor_glyph = (st->cursor_style == 1) ? "\xe2\x96\x88" /* █ */
                                 : (st->cursor_style == 2) ? "|"
                                 :                            "_";
        drw_tc_string(dev, prompt_pixel_w, y, cursor_glyph, eff_fg, eff_bg);
    }

    /* ── 5. Dropdown overlay (drawn on top of everything) ──────────────── */
    if (st->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&st->menu_bar, dev);
    }
}

static void destroy_gterm(WND *wnd) {
    if (!wnd) return;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    if (st) {
        free(st);
        wnd->user_data = 0;
    }
}

WND* open_gterm_window_rect(H x, H y, H w, H h, UW attr) {
    WND *wnd = opn_wnd("端末 (Terminal)", x, y, w, h, attr);
    if (wnd) {
        GTermState *st = (GTermState*)calloc(1, sizeof(GTermState));
        if (st) {
            gterm_init_menu_bar(st);
            gterm_init_banner(st);
            gterm_execute_cmd(wnd, st, "ver");
            wnd->user_data = (VW)(uintptr_t)st;
        }
        wnd->paint = paint_gterm;
        wnd->event_handler = handle_gterm_event;
        wnd->destroy = destroy_gterm;
        tip_cancel();
        tip_set_mode(TIP_MODE_ASCII); /* Terminal strictly starts in EN mode */
        top_wnd(wnd);
    }
    return wnd;
}

WND* open_gterm_window(void) {
    static int s_gterm_spawn_count = 0;
    H x = 220 + (s_gterm_spawn_count % 6) * 24;
    H y = 160 + (s_gterm_spawn_count % 6) * 24;
    s_gterm_spawn_count++;

    return open_gterm_window_rect(x, y, 560, 360,
                                  WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER | WND_ATTR_RESIZE);
}

BOOL gterm_is_menu_open(WND *wnd) {
    if (!wnd) return FALSE;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    return (st && st->menu_bar.active_menu >= 0);
}

void gterm_open_menu(WND *wnd, int menu_idx) {
    if (!wnd) return;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    if (st) {
        app_menu_open(&st->menu_bar, menu_idx);
        st->menu_bar.hover_item = 0;
    }
}

void gterm_close_menu(WND *wnd) {
    if (!wnd) return;
    GTermState *st = (GTermState*)(uintptr_t)wnd->user_data;
    if (st) {
        app_menu_close(&st->menu_bar);
    }
}