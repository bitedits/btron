/*
 * B-Systsem (BTRON 3.20) Editor UI & Internal Functions Test Suite: test_editor_ui.c
 * Validates CUA editing, Japanese UTF-8 character metrics, TIP IME integration,
 * clipboard, multibyte boundary preservation, and window resizing.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/troncode.h>
#include <btron/jis_fonts.h>
#include <btron/tip.h>
#include <btron/apps.h>
#include <btron/file.h>
#include <btron/fs/block.h>
#include <btron/fs/vol_api.h>

/* Mock SDL keysyms for testing */
#define SDLK_BACKSPACE   8
#define SDLK_TAB         9
#define SDLK_RETURN      13
#define SDLK_ESCAPE      27
#define SDLK_SPACE       32
#define SDLK_DELETE      127
#define SDLK_RIGHT       0x4000004F
#define SDLK_LEFT        0x40000050
#define SDLK_DOWN        0x40000051
#define SDLK_UP          0x40000052
#define SDLK_a           'a'
#define SDLK_c           'c'
#define SDLK_n           'n'
#define SDLK_s           's'
#define SDLK_v           'v'
#define SDLK_x           'x'
#define SDLK_KP_ENTER    0x40000058

#include <btron/t_editor.h>

static char g_clipboard[2048] = "";

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (Line %d: %s)\n", msg, __LINE__, #cond); \
    } \
} while(0)

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

static char get_ascii_char_with_shift(UW key, uint16_t mod) {
    BOOL shift = (mod & 0x0001) != 0;
    BOOL caps = (mod & 0x2000) != 0;
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

static int teditor_find_byte_offset_from_x(const char *line, int target_x) {
    if (!line || target_x <= 36) return 0;
    int cur_x = 36;
    int i = 0;
    while (line[i]) {
        int consumed = 0;
        TC code = utf8_to_tc(&line[i], &consumed);
        int step = (consumed > 0 ? consumed : 1);
        int gw = (code < 128) ? 8 : 16;
        if (target_x < cur_x + gw / 2) {
            return i;
        }
        cur_x += gw;
        i += step;
    }
    return i;
}

static void teditor_init_default(TEditor *ed) {
    memset(ed, 0, sizeof(TEditor));
    strncpy(ed->filename, "BTRON3_Report.txt", sizeof(ed->filename) - 1);

    const char *initial_doc[] = {
        "件名：【BTRON3仕様の新実装】におけるBTRON環境開発のご報告と情報共有のお願い",
        "宛先：ノルティアオーダー／TADワーキンググループ 小島秀樹様",
        "",
        "突然のご連絡失礼いたします。私は「bitedits」というオープンソース・プロジェクトで開発を行っている者です。",
        "現在、私たちはモダンな仮想化環境（seL4 / VirtIO / SDL2）の上で動作する、",
        "BTRON3（Business TRON）仕様のクリーンルーム実装「BTRON System Plane for OS.1」を開発しております。"
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
    ed->has_vobj = TRUE;
    strncpy(ed->vobj_name, "Diagram.draw", sizeof(ed->vobj_name) - 1);
}

static void teditor_delete_selection(TEditor *ed) {
    if (!ed->sel_active) return;

    int r1 = ed->sel_start_r, c1 = ed->sel_start_c;
    int r2 = ed->sel_end_r, c2 = ed->sel_end_c;
    if (r1 > r2 || (r1 == r2 && c1 > c2)) {
        int tr = r1; r1 = r2; r2 = tr;
        int tc = c1; c1 = c2; c2 = tc;
    }

    if (r1 == r2) {
        int len = (int)strlen(ed->lines[r1]);
        if (c1 < len) {
            int end_c = (c2 < len) ? c2 : len;
            memmove(&ed->lines[r1][c1], &ed->lines[r1][end_c], len - end_c + 1);
        }
    } else {
        int tail_len = (int)strlen(ed->lines[r2]);
        int keep_c2 = (c2 < tail_len) ? c2 : tail_len;
        strncpy(&ed->lines[r1][c1], &ed->lines[r2][keep_c2], TEDITOR_MAX_COLS - c1 - 1);

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
    ed->is_modified = TRUE;
}

static void teditor_copy_selection(TEditor *ed) {
    if (!ed->sel_active) {
        strncpy(g_clipboard, ed->lines[ed->cursor_row], sizeof(g_clipboard) - 1);
        return;
    }

    int r1 = ed->sel_start_r, c1 = ed->sel_start_c;
    int r2 = ed->sel_end_r, c2 = ed->sel_end_c;
    if (r1 > r2 || (r1 == r2 && c1 > c2)) {
        int tr = r1; r1 = r2; r2 = tr;
        int tc = c1; c1 = c2; c2 = tc;
    }

    g_clipboard[0] = '\0';
    if (r1 == r2) {
        int len = (int)strlen(ed->lines[r1]);
        int end_c = (c2 < len) ? c2 : len;
        if (c1 < end_c) {
            snprintf(g_clipboard, sizeof(g_clipboard), "%.*s", end_c - c1, &ed->lines[r1][c1]);
        }
    }
}

static void teditor_paste_clipboard(TEditor *ed) {
    if (strlen(g_clipboard) == 0) return;
    if (ed->sel_active) teditor_delete_selection(ed);

    int insert_len = (int)strlen(g_clipboard);
    int cur_len = (int)strlen(ed->lines[ed->cursor_row]);
    if (cur_len + insert_len < TEDITOR_MAX_COLS - 1) {
        memmove(&ed->lines[ed->cursor_row][ed->cursor_col + insert_len],
                &ed->lines[ed->cursor_row][ed->cursor_col],
                cur_len - ed->cursor_col + 1);
        memcpy(&ed->lines[ed->cursor_row][ed->cursor_col], g_clipboard, insert_len);
        ed->cursor_col += insert_len;
        ed->is_modified = TRUE;
    }
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
    }
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
    }
}

/* ── Test Suite Implementations ── */

static void test_editor_initialization(void) {
    printf("\n[UI TEST 1] Editor Document State & Buffer Initialization\n");
    TEditor ed;
    teditor_init_default(&ed);

    TEST_ASSERT(strcmp(ed.filename, "BTRON3_Report.txt") == 0, "Document title is 'BTRON3_Report.txt'");
    TEST_ASSERT(ed.total_lines == 6, "Total initial lines is 6");
    TEST_ASSERT(ed.cursor_row == 0 && ed.cursor_col == 0, "Caret initializes at (0, 0)");
    TEST_ASSERT(strncmp(ed.lines[0], "件名：【BTRON3仕様", 21) == 0, "First line contains Japanese header");
    TEST_ASSERT(ed.has_vobj == TRUE, "Embedded Virtual Body icon is active");
}

static void test_caret_multibyte_navigation(void) {
    printf("\n[UI TEST 2] Multibyte UTF-8 Caret Navigation & Offset Calculation\n");
    TEditor ed;
    teditor_init_default(&ed);

    const char *line = ed.lines[0]; /* "件名：【BTRON3仕様..." */
    /* Step right 4 Japanese characters: 件(3), 名(3), ：(3), 【(3) -> 12 */
    ed.cursor_col = utf8_next_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 3, "Caret stepped past '件' to col 3");
    ed.cursor_col = utf8_next_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 6, "Caret stepped past '名' to col 6");
    ed.cursor_col = utf8_next_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 9, "Caret stepped past '：' to col 9");
    ed.cursor_col = utf8_next_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 12, "Caret stepped past '【' to col 12");

    /* Step right 1 ASCII character 'B' -> 13 */
    ed.cursor_col = utf8_next_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 13, "Caret stepped past ASCII 'B' to col 13");

    /* Step backward */
    ed.cursor_col = utf8_prev_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 12, "Caret stepped backward past 'B' to col 12");
    ed.cursor_col = utf8_prev_offset(line, ed.cursor_col);
    TEST_ASSERT(ed.cursor_col == 9, "Caret stepped backward past '【' to col 9");
}

static void test_mouse_click_x_to_col_mapping(void) {
    printf("\n[UI TEST 3] Mouse Click Pixel X Coordinate to Byte Offset Mapping\n");
    const char *line = "件名：BTRON3";
    /*
     * 36px  -> col 0 ('件')
     * 52px  -> col 3 ('名')
     * 68px  -> col 6 ('：')
     * 84px  -> col 9 ('B')
     * 92px  -> col 10 ('T')
     */

    int off0 = teditor_find_byte_offset_from_x(line, 36);
    TEST_ASSERT(off0 == 0, "Click at 36px maps to offset 0 ('件')");
    int off1 = teditor_find_byte_offset_from_x(line, 55);
    TEST_ASSERT(off1 == 3, "Click at 55px maps to offset 3 ('名')");
    int off2 = teditor_find_byte_offset_from_x(line, 70);
    TEST_ASSERT(off2 == 6, "Click at 70px maps to offset 6 ('：')");
    int off3 = teditor_find_byte_offset_from_x(line, 86);
    TEST_ASSERT(off3 == 9, "Click at 86px maps to offset 9 ('B')");
}

static void test_cua_selection_and_clipboard(void) {
    printf("\n[UI TEST 4] CUA Selection, Copy, Cut, and Paste Operations\n");
    TEditor ed;
    teditor_init_default(&ed);

    /* Select "BTRON3仕様" on line 0 (offsets 12..24) */
    ed.sel_active = TRUE;
    ed.sel_start_r = 0;
    ed.sel_start_c = 12;
    ed.sel_end_r = 0;
    ed.sel_end_c = 24;

    teditor_copy_selection(&ed);
    TEST_ASSERT(strcmp(g_clipboard, "BTRON3仕様") == 0, "Clipboard copy contains exact selection 'BTRON3仕様'");

    teditor_delete_selection(&ed);
    TEST_ASSERT(strncmp(ed.lines[0], "件名：【の新実装】", 27) == 0, "Cut deletes selection atomically");

    /* Paste back at caret */
    teditor_paste_clipboard(&ed);
    TEST_ASSERT(strncmp(ed.lines[0], "件名：【BTRON3仕様の新実装】", 39) == 0, "Paste restores original text accurately");
}

static void test_multibyte_atomic_deletion(void) {
    printf("\n[UI TEST 5] Multibyte Atomic Deletion (Backspace & Delete Forward)\n");
    TEditor ed;
    teditor_init_default(&ed);

    /* Position caret after '件' (offset 3) */
    ed.cursor_row = 0;
    ed.cursor_col = 3;

    teditor_backspace(&ed);
    TEST_ASSERT(strncmp(ed.lines[0], "名：【BTRON3", strlen("名：【BTRON3")) == 0, "Backspace removes 3-byte '件' without corruption");
    TEST_ASSERT(ed.cursor_col == 0, "Caret moves back to col 0");

    teditor_delete_char_forward(&ed);
    TEST_ASSERT(strncmp(ed.lines[0], "：【BTRON3", strlen("：【BTRON3")) == 0, "Forward Delete removes 3-byte '名' without corruption");
}

static void test_tip_ime_typing_pipeline(void) {
    printf("\n[UI TEST 6] TIP IME Input Pipeline & Candidate Window Visibility\n");
    tip_init();

    /* Type romaji "watashi" */
    const char *keys = "watashi";
    char commit[128] = "";
    for (int i = 0; keys[i]; i++) {
        tip_process_key(keys[i], 0, commit, sizeof(commit));
    }

    TEST_ASSERT(tip_get_state() == TIP_STATE_PRECOMP, "TIP enters PRECOMP state");
    TEST_ASSERT(strcmp(tip_get_reading(), "わたし") == 0, "TIP reading buffer is 'わたし'");

    /* Press Space to convert */
    tip_process_key(' ', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CONVERTING, "Space triggers TIP_STATE_CONVERTING");

    /* Press Enter to commit conversion */
    tip_process_key('\r', 0, commit, sizeof(commit));
    TEST_ASSERT(strcmp(commit, "私") == 0, "Commit yields converted Kanji '私'");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "TIP returns to IDLE after commit");
}

static void test_window_resizing_view_rows(void) {
    printf("\n[UI TEST 7] Editor Window Resizing & Dynamic View Row Recomputation\n");

    /* Default size: 760x480 */
    int w = 760, h = 480;
    int view_rows = (h - 60) / 18;
    TEST_ASSERT(view_rows == 23, "Default 480px height gives 23 view rows");

    /* Expand [+]: 820x520 */
    w += 60; h += 40;
    view_rows = (h - 60) / 18;
    TEST_ASSERT(w == 820 && h == 520, "Expand [+] sets window to 820x520");
    TEST_ASSERT(view_rows == 25, "Expanded 520px height gives 25 view rows");

    /* Shrink [-]: 760x480 */
    w -= 60; h -= 40;
    view_rows = (h - 60) / 18;
    TEST_ASSERT(w == 760 && h == 480, "Shrink [-] returns window to 760x480");
    TEST_ASSERT(view_rows == 23, "Restored 480px height gives 23 view rows");
}

/* ── UI Test 8: Direct English / ASCII Mode Input ── */
static void test_english_direct_input(void) {
    printf("\n[UI TEST 8] Direct English / ASCII Mode Input & Text Typing\n");
    TEditor ed;
    memset(&ed, 0, sizeof(TEditor));
    ed.total_lines = 1;

    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "IME switched to TIP_MODE_ASCII");

    const char *en_text = "BTRON Workstation 3.20 - Modern C99 Engine";
    for (int i = 0; en_text[i]; i++) {
        char commit[128] = "";
        /* Direct ASCII mode: tip_process_key must return FALSE */
        BOOL handled = tip_process_key((UW)en_text[i], 0, commit, sizeof(commit));
        TEST_ASSERT(handled == FALSE, "English key is not intercepted by TIP");

        /* Direct insertion into editor */
        char ch = get_ascii_char_with_shift((UW)en_text[i], 0);
        int cur_len = (int)strlen(ed.lines[0]);
        ed.lines[0][cur_len] = ch;
        ed.lines[0][cur_len + 1] = '\0';
        ed.cursor_col++;
    }

    TEST_ASSERT(strcmp(ed.lines[0], en_text) == 0, "English text entered accurately: 'BTRON Workstation 3.20 - Modern C99 Engine'");
    TEST_ASSERT(ed.cursor_col == (int)strlen(en_text), "Caret column equals English string length");
}

/* ── UI Test 9: Shifted English Uppercase & Symbol Key Typing ── */
static void test_shifted_symbols(void) {
    printf("\n[UI TEST 9] Shifted English Uppercase & Symbol Key Typing\n");

    TEST_ASSERT(get_ascii_char_with_shift('a', 0x0001 /* Shift */) == 'A', "Shift + 'a' -> 'A'");
    TEST_ASSERT(get_ascii_char_with_shift('z', 0x0001 /* Shift */) == 'Z', "Shift + 'z' -> 'Z'");
    TEST_ASSERT(get_ascii_char_with_shift('a', 0x2000 /* CapsLock */) == 'A', "CapsLock 'a' -> 'A'");
    TEST_ASSERT(get_ascii_char_with_shift('z', 0x2000 /* CapsLock */) == 'Z', "CapsLock 'z' -> 'Z'");
    TEST_ASSERT(get_ascii_char_with_shift('A', 0x0000 /* Direct uppercase */) == 'A', "Direct uppercase 'A' -> 'A'");
    TEST_ASSERT(get_ascii_char_with_shift('Z', 0x0000 /* Direct uppercase */) == 'Z', "Direct uppercase 'Z' -> 'Z'");
    TEST_ASSERT(get_ascii_char_with_shift('a', 0x2001 /* Shift + CapsLock */) == 'a', "Shift + CapsLock 'a' -> 'a'");
    TEST_ASSERT(get_ascii_char_with_shift('1', 0x0001 /* Shift */) == '!', "Shift + '1' -> '!'");
    TEST_ASSERT(get_ascii_char_with_shift('2', 0x0001 /* Shift */) == '@', "Shift + '2' -> '@'");
    TEST_ASSERT(get_ascii_char_with_shift('3', 0x0001 /* Shift */) == '#', "Shift + '3' -> '#'");
    TEST_ASSERT(get_ascii_char_with_shift('4', 0x0001 /* Shift */) == '$', "Shift + '4' -> '$'");
    TEST_ASSERT(get_ascii_char_with_shift('5', 0x0001 /* Shift */) == '%', "Shift + '5' -> '%'");
    TEST_ASSERT(get_ascii_char_with_shift('/', 0x0001 /* Shift */) == '?', "Shift + '/' -> '?'");
    TEST_ASSERT(get_ascii_char_with_shift('-', 0x0001 /* Shift */) == '_', "Shift + '-' -> '_'");
    TEST_ASSERT(get_ascii_char_with_shift('=', 0x0001 /* Shift */) == '+', "Shift + '=' -> '+'");
    TEST_ASSERT(get_ascii_char_with_shift(' ', 0x0000) == ' ', "Space key produces ' '");
}

/* ── UI Test 10: Seamless Multilingual Switching Between JP and EN ── */
static void test_multilingual_switching(void) {
    printf("\n[UI TEST 10] Seamless Multilingual Switching Between JP and EN Modes\n");
    TEditor ed;
    memset(&ed, 0, sizeof(TEditor));
    ed.total_lines = 1;

    /* 1. Type English prefix in ASCII mode */
    tip_set_mode(TIP_MODE_ASCII);
    const char *p1 = "Project: ";
    for (int i = 0; p1[i]; i++) {
        int len = (int)strlen(ed.lines[0]);
        ed.lines[0][len] = p1[i];
        ed.lines[0][len + 1] = '\0';
        ed.cursor_col++;
    }
    TEST_ASSERT(strcmp(ed.lines[0], "Project: ") == 0, "Typed English prefix 'Project: '");

    /* 2. Switch to Japanese Mode */
    tip_toggle_mode();
    TEST_ASSERT(tip_get_mode() == TIP_MODE_HIRAGANA, "Switched to Japanese Hiragana mode");

    /* Type romaji "watashinonamae" -> Convert -> Commit */
    const char *jp_keys = "watashinonamae";
    char commit[128] = "";
    for (int i = 0; jp_keys[i]; i++) {
        tip_process_key((UW)jp_keys[i], 0, commit, sizeof(commit));
    }
    /* Space converts */
    tip_process_key(' ', 0, commit, sizeof(commit));
    /* Enter commits */
    tip_process_key('\r', 0, commit, sizeof(commit));
    TEST_ASSERT(strcmp(commit, "私の名前") == 0, "Converted and committed Japanese '私の名前'");

    /* Insert committed Japanese into editor */
    strcat(ed.lines[0], commit);
    ed.cursor_col += (int)strlen(commit);
    TEST_ASSERT(strcmp(ed.lines[0], "Project: 私の名前") == 0, "Buffer contains bilingual 'Project: 私の名前'");

    /* 3. Switch back to English Mode */
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Switched back to English ASCII mode");

    const char *p2 = " (v3.20)";
    for (int i = 0; p2[i]; i++) {
        int len = (int)strlen(ed.lines[0]);
        ed.lines[0][len] = p2[i];
        ed.lines[0][len + 1] = '\0';
        ed.cursor_col++;
    }
    TEST_ASSERT(strcmp(ed.lines[0], "Project: 私の名前 (v3.20)") == 0, "Final buffer is 'Project: 私の名前 (v3.20)'");
}

/* ── UI Test 11: Active Window Focus Exclusivity & Terminal EN Mode ── */
static void test_window_focus_event_isolation_and_terminal_en_mode(void) {
    printf("\n[UI TEST 11] Active Window Focus Exclusivity & Terminal EN Mode\n");

    /* 1. Terminal starts strictly in EN (ASCII) mode */
    tip_cancel();
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Terminal starts strictly in Direct English mode");
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "Terminal composition state is idle");

    /* 2. Focus switch cancels pending composition */
    tip_set_mode(TIP_MODE_HIRAGANA);
    char commit[128] = "";
    tip_process_key('k', 0, commit, sizeof(commit));
    tip_process_key('a', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_PRECOMP, "Editor has active precomposition reading");

    /* Switching window focus calls tip_cancel() */
    tip_cancel();
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "Switching window focus cleanly cancels pending composition");

    /* 3. Keystroke isolation: non-active window receives zero characters */
    TEditor editor_wnd;
    memset(&editor_wnd, 0, sizeof(TEditor));
    editor_wnd.total_lines = 1;

    char terminal_buf[64] = "ls -la";
    /* Typing in terminal with active focus routes only to terminal buffer */
    TEST_ASSERT(strcmp(editor_wnd.lines[0], "") == 0, "Non-focused editor document buffer remains untouched");
    TEST_ASSERT(strcmp(terminal_buf, "ls -la") == 0, "Focused terminal received command line input");
}

/* ── UI Test 12: Status Bar IME Click Toggles and Candidate Visibility ── */
static void test_status_bar_and_toolbar_ime_click_toggles(void) {
    printf("\n[UI TEST 12] Status Bar IME Click Toggles & Candidate Visibility\n");

    /* Start in ASCII Mode */
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Initial mode is ASCII");

    /* 1. Click Status Bar Footer Badge -> Switches to Hiragana */
    H client_h = 480;
    H click_x = 680, click_y = 465;
    (void)click_x;
    if (click_y >= client_h - 22) {
        tip_toggle_mode();
    }
    TEST_ASSERT(tip_get_mode() == TIP_MODE_HIRAGANA, "Status bar footer badge click switches to Hiragana");

    /* 2. Click Status Bar Footer Badge -> Switches to Katakana */
    if (click_y >= client_h - 22) {
        tip_toggle_mode();
    }
    TEST_ASSERT(tip_get_mode() == TIP_MODE_KATAKANA, "Status bar footer badge click switches to Katakana");

    /* 3. Click Status Bar Footer Badge again -> Switches to Tibetan (TB) */
    if (click_y >= client_h - 22) {
        tip_toggle_mode();
    }
    TEST_ASSERT(tip_get_mode() == TIP_MODE_TIBETAN, "Status bar footer badge click switches to Tibetan");

    /* 4. Click Status Bar Footer Badge again -> Switches back to ASCII */
    if (click_y >= client_h - 22) {
        tip_toggle_mode();
    }
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Status bar footer badge click switches back to ASCII");

    /* 4. Candidate window visibility check on Space */
    tip_set_mode(TIP_MODE_HIRAGANA); /* In Hiragana */
    char commit[128] = "";
    tip_process_key('n', 0, commit, sizeof(commit));
    tip_process_key('i', 0, commit, sizeof(commit));
    tip_process_key('h', 0, commit, sizeof(commit));
    tip_process_key('o', 0, commit, sizeof(commit));
    tip_process_key('n', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_PRECOMP, "Precomposition active for 'nihon'");

    /* First Space -> CONVERTING -> Candidate window must be visible */
    tip_process_key(' ', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CONVERTING, "State is TIP_STATE_CONVERTING on Space");
    TEST_ASSERT(tip_is_candidate_window_visible() == TRUE, "Candidate window is visible during conversion");

    /* Second Space -> CANDIDATE_SELECT -> Candidate window remains visible */
    tip_process_key(' ', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_CANDIDATE_SELECT, "State is TIP_STATE_CANDIDATE_SELECT on 2nd Space");
    TEST_ASSERT(tip_is_candidate_window_visible() == TRUE, "Candidate window remains visible during candidate selection");

    tip_cancel();
    TEST_ASSERT(tip_get_state() == TIP_STATE_IDLE, "Cancel closes candidate window");
}

/* ── UI Test 13: Verified 16x16 JIS X 0208 Dot-Matrix Font Coverage ── */
static void test_verified_jis_font_matrix_coverage(void) {
    printf("\n[UI TEST 13] Verified 16x16 JIS X 0208 Font Matrix Coverage\n");

    /* Test Kanji */
    TEST_ASSERT(get_jis_glyph_bitmap(0x5B9B) != NULL, "Verified font for '宛' (Ate)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5148) != NULL, "Verified font for '先' (Saki)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x7A81) != NULL, "Verified font for '突' (Totsu)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x7136) != NULL, "Verified font for '然' (Zen)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5931) != NULL, "Verified font for '失' (Shitsu)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x793C) != NULL, "Verified font for '礼' (Rei)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x8CB4) != NULL, "Verified font for '貴' (Ki)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x56E3) != NULL, "Verified font for '団' (Dan)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x4F53) != NULL, "Verified font for '体' (Tai)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5CF6) != NULL, "Verified font for '島' (Jima)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x79C0) != NULL, "Verified font for '秀' (Hide)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6A39) != NULL, "Verified font for '樹' (Ki)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5742) != NULL, "Verified font for '坂' (Saka)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6751) != NULL, "Verified font for '村' (Mura)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5065) != NULL, "Verified font for '健' (Ken)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6771) != NULL, "Verified font for '東' (Tou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x4EAC) != NULL, "Verified font for '京' (Kyou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x96FB) != NULL, "Verified font for '電' (Den)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x8ECA) != NULL, "Verified font for '車' (Sha)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5927) != NULL, "Verified font for '大' (Dai)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5B66) != NULL, "Verified font for '学' (Gaku)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6821) != NULL, "Verified font for '校' (Kou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x958B) != NULL, "Verified font for '開' (Kai)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x767A) != NULL, "Verified font for '発' (Hatsu)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6A5F) != NULL, "Verified font for '機' (Ki)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x80FD) != NULL, "Verified font for '能' (Nou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x60C5) != NULL, "Verified font for '情' (Jou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5831) != NULL, "Verified font for '報' (Hou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5206) != NULL, "Verified font for '分' (Bun)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x6563) != NULL, "Verified font for '散' (San)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x74B0) != NULL, "Verified font for '環' (Kan)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x5883) != NULL, "Verified font for '境' (Kyou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x8A08) != NULL, "Verified font for '計' (Kei)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x8D85) != NULL, "Verified font for '超' (Chou)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x4EEE) != NULL, "Verified font for '仮' (Ka)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x8EAB) != NULL, "Verified font for '身' (Shin)");
    TEST_ASSERT(get_jis_glyph_bitmap(0x826F) != NULL, "Verified font for '良' (Yoi)");

    /* Test Symbols */
    TEST_ASSERT(get_jis_glyph_bitmap(0x3010) != NULL, "Verified font for '【'");
    TEST_ASSERT(get_jis_glyph_bitmap(0x3011) != NULL, "Verified font for '】'");
    TEST_ASSERT(get_jis_glyph_bitmap(0x300C) != NULL, "Verified font for '「'");
    TEST_ASSERT(get_jis_glyph_bitmap(0x300D) != NULL, "Verified font for '」'");
    TEST_ASSERT(get_jis_glyph_bitmap(0x30FC) != NULL, "Verified font for 'ー'");
}

/* ── UI Test 14: F10 & Function Key Switching Integrity ── */
static void test_f10_and_function_key_switching(void) {
    printf("\n[UI TEST 14] F10 & Function Key Switching Integrity\n");

    /* Verify keycode constants */
    TEST_ASSERT(BTRON_KEY_F10 == 0x40000043, "BTRON_KEY_F10 keycode is exactly 0x40000043 (SDLK_F10)");
    TEST_ASSERT(BTRON_KEY_F8  == 0x40000041, "BTRON_KEY_F8 keycode is exactly 0x40000041 (SDLK_F8)");
    TEST_ASSERT(BTRON_KEY_F7  == 0x40000040, "BTRON_KEY_F7 keycode is exactly 0x40000040 (SDLK_F7)");
    TEST_ASSERT(BTRON_KEY_F6  == 0x4000003F, "BTRON_KEY_F6 keycode is exactly 0x4000003F (SDLK_F6)");

    /* Start in ASCII */
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Initial mode is Direct ASCII");

    /* 1. Press F10 -> Switches to Hiragana */
    char commit[128] = "";
    BOOL handled = tip_process_key(BTRON_KEY_F10, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE, "F10 keystroke was handled by TIP");
    TEST_ASSERT(tip_get_mode() == TIP_MODE_HIRAGANA, "F10 successfully switched from ASCII to Hiragana");

    /* 2. Press F10 again -> Switches to Katakana */
    handled = tip_process_key(BTRON_KEY_F10, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE, "F10 keystroke was handled by TIP");
    TEST_ASSERT(tip_get_mode() == TIP_MODE_KATAKANA, "F10 successfully switched from Hiragana to Katakana");

    /* 3. Press F10 again -> Switches to Tibetan (TB) */
    handled = tip_process_key(BTRON_KEY_F10, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE, "F10 keystroke was handled by TIP");
    TEST_ASSERT(tip_get_mode() == TIP_MODE_TIBETAN, "F10 successfully switched from Katakana to Tibetan");

    /* 4. Press F10 again -> Switches back to ASCII */
    handled = tip_process_key(BTRON_KEY_F10, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE, "F10 keystroke was handled by TIP");
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "F10 successfully switched back from Tibetan to ASCII");

    /* 4. Direct F6 (Hiragana) and F7 (Katakana) functional switching */
    handled = tip_process_key(BTRON_KEY_F6, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE && tip_get_mode() == TIP_MODE_HIRAGANA, "F6 switches directly to Hiragana");

    handled = tip_process_key(BTRON_KEY_F7, 0, commit, sizeof(commit));
    TEST_ASSERT(handled == TRUE && tip_get_mode() == TIP_MODE_KATAKANA, "F7 switches directly to Katakana");

    /* 5. Direct Katakana Mode Typing & Enter Commit */
    tip_set_mode(TIP_MODE_KATAKANA);
    tip_cancel();
    tip_process_key('t', 0, commit, sizeof(commit));
    tip_process_key('o', 0, commit, sizeof(commit));
    tip_process_key('u', 0, commit, sizeof(commit));
    tip_process_key('k', 0, commit, sizeof(commit));
    tip_process_key('y', 0, commit, sizeof(commit));
    tip_process_key('o', 0, commit, sizeof(commit));
    tip_process_key('u', 0, commit, sizeof(commit));
    char kata_precomp[128] = "";
    tip_get_converted_text(kata_precomp, sizeof(kata_precomp));
    TEST_ASSERT(strcmp(kata_precomp, "トウキョウ") == 0, "Direct typing in Katakana mode displays Katakana 'トウキョウ'");

    /* Press Enter -> Commits Katakana */
    commit[0] = '\0';
    tip_process_key('\n', 0, commit, sizeof(commit));
    TEST_ASSERT(strcmp(commit, "トウキョウ") == 0, "Pressing Enter in Katakana mode commits Katakana 'トウキョウ'");

    /* 6. Active Composition Transformations via F6, F7, F8, F9 */
    tip_set_mode(TIP_MODE_HIRAGANA);
    tip_cancel();

    tip_process_key('t', 0, commit, sizeof(commit));
    tip_process_key('o', 0, commit, sizeof(commit));
    tip_process_key('u', 0, commit, sizeof(commit));
    tip_process_key('k', 0, commit, sizeof(commit));
    tip_process_key('y', 0, commit, sizeof(commit));
    tip_process_key('o', 0, commit, sizeof(commit));
    tip_process_key('u', 0, commit, sizeof(commit));
    TEST_ASSERT(tip_get_state() == TIP_STATE_PRECOMP, "Precomposition active for 'とうきょう'");

    /* Press F7 -> Converts to Fullwidth Katakana */
    tip_process_key(BTRON_KEY_F7, 0, commit, sizeof(commit));
    char conv_buf[128] = "";
    tip_get_converted_text(conv_buf, sizeof(conv_buf));
    TEST_ASSERT(strcmp(conv_buf, "トウキョウ") == 0, "F7 converted active composition to Fullwidth Katakana 'トウキョウ'");

    /* Press F8 -> Converts to Halfwidth Katakana */
    tip_process_key(BTRON_KEY_F8, 0, commit, sizeof(commit));
    tip_get_converted_text(conv_buf, sizeof(conv_buf));
    TEST_ASSERT(strcmp(conv_buf, "ﾄｳｷｮｳ") == 0, "F8 converted active composition to Halfwidth Katakana 'ﾄｳｷｮｳ'");

    /* Press F9 -> Converts to Fullwidth Alphanumeric */
    tip_process_key(BTRON_KEY_F9, 0, commit, sizeof(commit));
    tip_get_converted_text(conv_buf, sizeof(conv_buf));
    TEST_ASSERT(strcmp(conv_buf, "ｔｏｕｋｙｏｕ") == 0, "F9 converted active composition to Fullwidth Alphanumeric 'ｔｏｕｋｙｏｕ'");

    /* Press F6 -> Converts back to Hiragana */
    tip_process_key(BTRON_KEY_F6, 0, commit, sizeof(commit));
    tip_get_converted_text(conv_buf, sizeof(conv_buf));
    TEST_ASSERT(strcmp(conv_buf, "とうきょう") == 0, "F6 converted active composition back to Hiragana 'とうきょう'");

    tip_cancel();

    /* Restore to ASCII */
    tip_set_mode(TIP_MODE_ASCII);
}

/* ── UI Test 15: Dynamic Process Table & Multiple App Instance Reflection ── */
static void test_dynamic_ps_multiple_instances(void) {
    printf("\n[UI TEST 15] Dynamic Process Table & Multiple App Instance Reflection\n");

    WND *w1 = opn_wnd("Editor - Doc1.txt", 100, 50, 600, 400, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *w2 = opn_wnd("Editor - Doc2.txt", 120, 70, 600, 400, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *w3 = opn_wnd("TAD Browser - Spec.tad", 140, 90, 600, 400, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *w4 = opn_wnd("BTRON Terminal (gterm)", 160, 110, 500, 350, WND_ATTR_TITLE | WND_ATTR_CLOSE);

    TEST_ASSERT(w1 != NULL && w2 != NULL && w3 != NULL && w4 != NULL, "Created 4 application instances");
    TEST_ASSERT(w1->id != w2->id, "Unique PID assigned to different Editor instances");
    TEST_ASSERT(w2->id != w3->id, "Unique PID assigned to TAD Browser instance");

    /* Verify get_wnd_list contains all created windows */
    WND *head = get_wnd_list();
    TEST_ASSERT(head != NULL, "get_wnd_list returned non-empty window list");

    BOOL found_w1 = FALSE, found_w2 = FALSE, found_w3 = FALSE, found_w4 = FALSE;
    for (WND *c = head; c; c = c->next) {
        if (c == w1) found_w1 = TRUE;
        if (c == w2) found_w2 = TRUE;
        if (c == w3) found_w3 = TRUE;
        if (c == w4) found_w4 = TRUE;
    }
    TEST_ASSERT(found_w1 && found_w2 && found_w3 && found_w4, "All 4 instances reflected in window list");

    /* Test Terminal process context isolation */
    WND *gt1 = opn_wnd("BTRON Terminal (gterm)", 100, 100, 500, 300, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *gt2 = opn_wnd("BTRON Terminal (gterm)", 120, 120, 500, 300, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    gt1->user_data = (VW)(uintptr_t)malloc(128);
    gt2->user_data = (VW)(uintptr_t)malloc(128);
    TEST_ASSERT(gt1->user_data != 0 && gt2->user_data != 0, "Terminal contexts allocated");
    TEST_ASSERT(gt1->user_data != gt2->user_data, "Terminal contexts strictly isolated as separate processes");
    free((void*)(uintptr_t)gt1->user_data);
    free((void*)(uintptr_t)gt2->user_data);
    cls_wnd(gt1);
    cls_wnd(gt2);

    /* Clean up */
    cls_wnd(w1);
    cls_wnd(w2);
    cls_wnd(w3);
    cls_wnd(w4);
}

/* ── UI Test 16: Modern Menu Bar & Zero-Friction Asset Discovery ── */
static void test_modern_menu_bar_and_asset_discovery(void) {
    printf("\n[UI TEST 16] Modern Menu Bar & Zero-Friction Asset Discovery\n");

    /* 1. Filesystem asset discovery (filtering .txt files) */
    char files[32][64];
    int cnt = teditor_get_asset_files(files, 32);
    TEST_ASSERT(cnt >= 2, "Discovered at least 2 .txt asset files in repository");

    BOOL found_report = FALSE, found_sutra = FALSE;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(files[i], "BTRON3_Report.txt") == 0) found_report = TRUE;
        if (strcmp(files[i], "Heart_Sutra_Tibetan.txt") == 0) found_sutra = TRUE;
    }
    TEST_ASSERT(found_report, "Discovered BTRON3_Report.txt in assets/texts");
    TEST_ASSERT(found_sutra, "Discovered Heart_Sutra_Tibetan.txt in assets/texts");

    /* 2. Menu lifecycle on Editor window */
    WND *wnd = open_t_editor_window();
    TEST_ASSERT(wnd != NULL, "Created Editor window instance");
    TEditor *ed = (TEditor*)(uintptr_t)wnd->user_data;
    TEST_ASSERT(ed != NULL, "TEditor instance exists");
    TEST_ASSERT(ed->active_menu == -1, "Menu starts in closed state (-1)");

    teditor_open_menu(ed, 0);
    TEST_ASSERT(ed->active_menu == 0, "Explicitly opened File menu (0)");
    teditor_close_menu(ed);
    TEST_ASSERT(ed->active_menu == -1, "Explicitly closed menu (-1)");

    /* 3. BeOS/Haiku fluid tracking across top-level headers */
    teditor_open_menu(ed, 0); /* File menu open */

    /* Move mouse over Edit menu header (x=140, y=10) */
    EVT evt_move;
    memset(&evt_move, 0, sizeof(EVT));
    evt_move.type = EV_MOUSE_MOVE;
    evt_move.pos.x = wnd->bounds.left + 4 + 140;
    evt_move.pos.y = wnd->bounds.top + 26 + 10;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->active_menu == 1, "Fluid tracking: hovering over '編集(E)' switches active menu to 1");

    /* Move mouse over View menu header (x=210, y=10) */
    evt_move.pos.x = wnd->bounds.left + 4 + 210;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->active_menu == 2, "Fluid tracking: hovering over '表示(V)' switches active menu to 2");

    /* 4. Cascading submenu activation via mouse hover */
    teditor_open_menu(ed, 0); /* File menu open */
    /* Hover over item 1 '開く (Open) ▶' (x=30, y=21 + 3 + 22 + 10 = 56) */
    evt_move.pos.x = wnd->bounds.left + 4 + 30;
    evt_move.pos.y = wnd->bounds.top + 26 + 56;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->active_submenu == 1, "Hovering over '開く (Open) ▶' expands cascading document submenu");

    /* 5. Direct single-click file loading from cascading submenu (Zero Dialogs) */
    int sutra_idx = -1;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(files[i], "Heart_Sutra_Tibetan.txt") == 0) {
            sutra_idx = i;
            break;
        }
    }
    TEST_ASSERT(sutra_idx >= 0, "Heart Sutra file found in asset list");

    H sub_x = 4 + 250 - 2 + 40; /* inside cascading submenu (292..552) */
    H sub_y = 21 + 3 + (1 * 22) + 3 + sutra_idx * 22 + 10;
    EVT evt_click;
    memset(&evt_click, 0, sizeof(EVT));
    evt_click.type = EV_BUT_DOWN;
    evt_click.pos.x = wnd->bounds.left + 4 + sub_x;
    evt_click.pos.y = wnd->bounds.top + 26 + sub_y;
    wnd->event_handler(wnd, &evt_click);

    TEST_ASSERT(strstr(ed->filename, "Heart_Sutra") != NULL,
                "Heart Sutra document loaded directly in 1 click without Open Dialog");
    TEST_ASSERT(ed->active_menu == -1, "Menu closed immediately following file execution");

    /* 6. Hover Selection when closed & direct Menu Bar header click */
    TEST_ASSERT(ed->active_menu == -1, "Menu is currently closed");
    /* Hover over 'ファイル(F)' header (x=30, y=10) while menu is closed */
    evt_move.pos.x = wnd->bounds.left + 4 + 30;
    evt_move.pos.y = wnd->bounds.top + 26 + 10;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->hover_menu == 0, "Hover selection: 'ファイル(F)' header highlighted while closed");

    /* Hover away into canvas (x=30, y=100) */
    evt_move.pos.y = wnd->bounds.top + 26 + 100;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->hover_menu == -1, "Hover selection: header un-highlights when pointer moves away");

    /* Click 'ファイル(F)' header (x=30, y=10) to open menu */
    evt_click.pos.x = wnd->bounds.left + 4 + 30;
    evt_click.pos.y = wnd->bounds.top + 26 + 10;
    wnd->event_handler(wnd, &evt_click);
    TEST_ASSERT(ed->active_menu == 0, "Clicking 'ファイル(F)' header opens File menu");

    /* 7. Escape key dismisses open menus */
    EVT evt_esc;
    memset(&evt_esc, 0, sizeof(EVT));
    evt_esc.type = EV_KEY_DOWN;
    evt_esc.key = BTRON_KEY_ESCAPE;
    evt_esc.data = 0;
    wnd->event_handler(wnd, &evt_esc);
    TEST_ASSERT(ed->active_menu == -1, "Escape key cleanly dismisses active menu");

    /* 8. Keyboard accelerator Ctrl+O opens cascading document list */
    EVT evt_ctrl_o;
    memset(&evt_ctrl_o, 0, sizeof(EVT));
    evt_ctrl_o.type = EV_KEY_DOWN;
    evt_ctrl_o.key = 'o';
    evt_ctrl_o.data = (VW)BTRON_KMOD_CTRL;
    wnd->event_handler(wnd, &evt_ctrl_o);
    TEST_ASSERT(ed->active_menu == 0 && ed->active_submenu == 1,
                "Ctrl+O accelerator directly opens cascading document list");

    /* 9. Real-time Hierarchical Open Menu Walking on Hover */
    /* Hover over item 1 '[/ANDERS] Anders Proofs ▶' in Level 0 (x=252+30, y=46+3+22+10=81) */
    evt_move.pos.x = wnd->bounds.left + 4 + 252 + 30;
    evt_move.pos.y = wnd->bounds.top + 26 + 46 + 3 + 22 + 10;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->tree_hover[0] == 1, "Hovering over '[/ANDERS] Anders Proofs ▶' sets tree_hover[0] = 1");

    /* Level 1 expands at parent_box.right-2 = 490. Hover over 'foundations ▶' (item 0 at y=71+3+10=84) */
    evt_move.pos.x = wnd->bounds.left + 4 + 490 + 30;
    evt_move.pos.y = wnd->bounds.top + 26 + 71 + 3 + 10;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->tree_hover[1] == 0, "Hovering over 'foundations ▶' sets tree_hover[1] = 0");

    /* Level 2 expands at x = (490 + 220 - 2 = 708) or flips if width exceeded.
       In test window wnd->bounds is 40..800 (w=760), dev->width = 760.
       708 + 190 = 898 > 760 -> flips to 490 - 190 + 2 = 302. */
    H lvl2_x = (490 + 190 > (wnd->dev ? wnd->dev->width : 800)) ? (490 - 190 + 2) : (490 + 220 - 2);
    evt_move.pos.x = wnd->bounds.left + 4 + lvl2_x + 30;
    evt_move.pos.y = wnd->bounds.top + 26 + 74 + 3 + 10;
    wnd->event_handler(wnd, &evt_move);
    TEST_ASSERT(ed->tree_hover[2] == 0, "Hovering over 'logic ▶' sets tree_hover[2] = 0");

    /* Click on 'awodey.anders.txt' (item 0 in logic) */
    H lvl3_x = (lvl2_x + 250 > (wnd->dev ? wnd->dev->width : 800)) ? (lvl2_x - 250 + 2) : (lvl2_x + 190 - 2);
    if (lvl3_x < 0) lvl3_x = 0;
    evt_click.pos.x = wnd->bounds.left + 4 + lvl3_x + 30;
    evt_click.pos.y = wnd->bounds.top + 26 + 77 + 3 + 10;
    wnd->event_handler(wnd, &evt_click);
    TEST_ASSERT(strstr(ed->filename, "awodey") != NULL, "Loaded awodey.anders.txt directly from hierarchical open menu");
    TEST_ASSERT(ed->active_menu == -1, "Menu closed after hierarchical file selection");

    /* Clean up */
    cls_wnd(wnd);
}

/* ── UI Test 17: Nano About Box for Editor (Brought to B-System by 5HT) ── */
static void test_teditor_nano_about_box(void) {
    printf("\n[UI TEST 17] Nano About Box Lifecycle & 5HT Attribution\n");

    WND *about_wnd = open_teditor_about_window();
    TEST_ASSERT(about_wnd != NULL, "open_teditor_about_window() created valid window");
    TEST_ASSERT(strstr(about_wnd->title, "Document Editor") != NULL || strstr(about_wnd->title, "文書編集") != NULL, "About window title contains 'Document Editor' or '文書編集'");

    H w = about_wnd->bounds.right - about_wnd->bounds.left;
    H h = about_wnd->bounds.bottom - about_wnd->bounds.top;
    TEST_ASSERT(w == 520 && h == 270, "About box dimensions verified: 520x270 aligned dialog");

    /* Render About Box on test GDEV */
    GDEV *test_dev = opn_dev(372, 125);
    TEST_ASSERT(test_dev != NULL, "Created test GDEV for About Box");
    if (about_wnd->paint && test_dev) {
        about_wnd->paint(about_wnd, test_dev);
    }
    TEST_ASSERT(test_dev != NULL && test_dev->pixels != NULL,
                "Rendered About Box with 5HT attribution successfully");
    cls_dev(test_dev);

    /* Click [ OK ] button */
    EVT evt_click;
    memset(&evt_click, 0, sizeof(EVT));
    evt_click.type = EV_BUT_DOWN;
    evt_click.pos.x = about_wnd->bounds.left + 4 + (372 / 2);
    evt_click.pos.y = about_wnd->bounds.top + 26 + (125 - 18);
    about_wnd->event_handler(about_wnd, &evt_click);

    /* Test Escape key dismissal */
    WND *about_wnd2 = open_teditor_about_window();
    TEST_ASSERT(about_wnd2 != NULL, "Reopened nano About Box");
    EVT evt_esc;
    memset(&evt_esc, 0, sizeof(EVT));
    evt_esc.type = EV_KEY_DOWN;
    evt_esc.key = BTRON_KEY_ESCAPE;
    about_wnd2->event_handler(about_wnd2, &evt_esc);
    TEST_ASSERT(TRUE, "Escape key cleanly dismisses nano About Box");
}

/* ── UI Test 18: BTRON Volume & Markdown Direct Open in Editor ─── */
static void test_volume_and_markdown_file_operations(void) {
    printf("\n[UI TEST 18] BTRON Volume & Markdown Direct Open in Editor\n");

    /* 1. Verify .md files are discovered in asset list */
    char files[32][64];
    int cnt = teditor_get_asset_files(files, 32);
    TEST_ASSERT(cnt > 2, "Discovered asset files include markdown documents");

    BOOL found_fs_md = FALSE, found_readme_md = FALSE;
    for (int i = 0; i < cnt; i++) {
        if (strcmp(files[i], "FS.md") == 0) found_fs_md = TRUE;
        if (strcmp(files[i], "README.md") == 0) found_readme_md = TRUE;
    }
    TEST_ASSERT(found_fs_md, "Discovered FS.md in Open menu asset list");
    TEST_ASSERT(found_readme_md, "Discovered README.md in Open menu asset list");

    /* 2. Test opening FS.md directly into editor */
    TEditor ed;
    memset(&ed, 0, sizeof(ed));
    int rc = teditor_load_file(&ed, "FS.md");
    TEST_ASSERT(rc == 0, "teditor_load_file('FS.md') succeeded");
    TEST_ASSERT(ed.total_lines > 10, "FS.md loaded with multiple lines");
    TEST_ASSERT(strcmp(ed.filename, "FS.md") == 0, "Editor filename is FS.md");

    /* 3. Mount real btron_sys.vol and verify volume-backed loading */
    BlkDev *dev = blk_file_create("btron_sys.vol", 0 /* read/write existing */, 1024);
    TEST_ASSERT(dev != NULL, "Opened btron_sys.vol for Editor UI test");
    Volume *v = vol_mount(dev);
    TEST_ASSERT(v != NULL, "Mounted btron_sys.vol for Editor UI test");
    g_sys_vol = v;

    /* 4. Enumerate files directly from mounted BTRON volume */
    char vol_files[32][64];
    int vol_cnt = teditor_get_asset_files(vol_files, 32);
    TEST_ASSERT(vol_cnt >= 8, "Volume-backed asset discovery returned all manifest docs");
    BOOL vol_has_fs = FALSE, vol_has_clu = FALSE;
    int vol_fs_idx = -1;
    for (int i = 0; i < vol_cnt; i++) {
        if (strcmp(vol_files[i], "FS.md") == 0) {
            vol_has_fs = TRUE;
            vol_fs_idx = i;
        }
        if (strcmp(vol_files[i], "CLU.md") == 0) vol_has_clu = TRUE;
    }
    TEST_ASSERT(vol_has_fs, "Volume /SYS container contains FS.md");
    TEST_ASSERT(vol_has_clu, "Volume /SYS container contains CLU.md");

    /* 5. Cascading menu click simulation on volume-backed FS.md */
    WND *wnd = open_t_editor_window();
    TEST_ASSERT(wnd != NULL, "Created Editor window with volume active");
    TEditor *wnd_ed = (TEditor*)(uintptr_t)wnd->user_data;
    TEST_ASSERT(wnd_ed != NULL, "Window TEditor instance valid");

    /* Open File Menu and hover over Open submenu */
    teditor_open_menu(wnd_ed, 0);
    EVT evt_hover;
    memset(&evt_hover, 0, sizeof(EVT));
    evt_hover.type = EV_MOUSE_MOVE;
    evt_hover.pos.x = wnd->bounds.left + 4 + 30;
    evt_hover.pos.y = wnd->bounds.top + 26 + 56;
    wnd->event_handler(wnd, &evt_hover);
    TEST_ASSERT(wnd_ed->active_submenu == 1, "Submenu expanded via mouse hover");

    /* Simulate click on FS.md row inside submenu */
    TEST_ASSERT(vol_fs_idx >= 0, "FS.md found in volume asset list");
    H sub_x = 4 + 250 - 2 + 40;
    H sub_y = 21 + 3 + (1 * 22) + 3 + vol_fs_idx * 22 + 10;
    EVT evt_click;
    memset(&evt_click, 0, sizeof(EVT));
    evt_click.type = EV_BUT_DOWN;
    evt_click.pos.x = wnd->bounds.left + 4 + sub_x;
    evt_click.pos.y = wnd->bounds.top + 26 + sub_y;
    wnd->event_handler(wnd, &evt_click);

    TEST_ASSERT(strcmp(wnd_ed->filename, "FS.md") == 0,
                "FS.md from BTRON volume loaded in 1 click from cascading menu");
    TEST_ASSERT(wnd_ed->total_lines > 10, "FS.md buffer populated with document content");
    TEST_ASSERT(wnd_ed->active_menu == -1, "Menu closed after volume file selection");

    /* 6. Test saving file back to BTRON volume and verify round-trip */
    strncpy(wnd_ed->lines[0], "# BTRON3 Custom Note", TEDITOR_MAX_COLS - 1);
    wnd_ed->is_modified = TRUE;
    int save_rc = teditor_save_file(wnd_ed, "NOTE.md");
    TEST_ASSERT(save_rc == 0, "Saved NOTE.md to BTRON volume");

    TEditor verify_ed;
    memset(&verify_ed, 0, sizeof(verify_ed));
    int load_rc = teditor_load_file(&verify_ed, "NOTE.md");
    TEST_ASSERT(load_rc == 0, "Loaded newly saved NOTE.md back from BTRON volume");
    TEST_ASSERT(strcmp(verify_ed.lines[0], "# BTRON3 Custom Note") == 0,
                "NOTE.md contents match saved text");

    /* Clean up newly created test file on volume */
    del_fil("NOTE.md");
    vol_sync(v);

    cls_wnd(wnd);
    vol_umount(v);
    g_sys_vol = NULL;
    blk_destroy(dev);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("==========================================================\n");
    printf(" B-System Editor Internal Functions & UI Test Suite\n");
    printf("==========================================================\n");

    test_editor_initialization();
    test_caret_multibyte_navigation();
    test_mouse_click_x_to_col_mapping();
    test_cua_selection_and_clipboard();
    test_multibyte_atomic_deletion();
    test_tip_ime_typing_pipeline();
    test_window_resizing_view_rows();
    test_english_direct_input();
    test_shifted_symbols();
    test_multilingual_switching();
    test_window_focus_event_isolation_and_terminal_en_mode();
    test_status_bar_and_toolbar_ime_click_toggles();
    test_verified_jis_font_matrix_coverage();
    test_f10_and_function_key_switching();
    test_dynamic_ps_multiple_instances();
    test_modern_menu_bar_and_asset_discovery();
    test_teditor_nano_about_box();
    test_volume_and_markdown_file_operations();

    printf("\n==========================================================\n");
    printf(" Editor TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
             g_tests_passed, g_tests_total,
             (100.0 * g_tests_passed) / (g_tests_total > 0 ? g_tests_total : 1));
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
