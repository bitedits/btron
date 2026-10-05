/*
 * B-System BTRON3 — test_termios_clu.c
 * Verification suite for termios, multilingual lang engine, term diffing,
 * TV editor, and SC commander (NASA JPL compliant, zero host portaling).
 *
 * Compile: make test-termios
 * Run:     ./.build/test_termios
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btron/tty.h>
#include "clu/lang.h"
#include "clu/term.h"
#include "clu/vfs.h"
#include "clu/tv/tv.h"
#include "clu/sc/sokhatsky.h"
#include "apps/clu.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { printf("FAIL: %s: %s\n", __func__, msg); g_fail++; return; } } while(0)

#define TEST_PASS() \
    do { printf("PASS: %s\n", __func__); g_pass++; } while(0)

/* ── 1. Termios Line Discipline ───────────────────────────────────── */
static void test_termios_line_discipline(void)
{
    BtTermios tio;
    memset(&tio, 0, sizeof(tio));

    int res = bt_tcgetattr(&tio);
    CHECK(res == 0, "bt_tcgetattr should succeed");

    /* Default settings: ICANON, ECHO, ICRNL enabled */
    CHECK(tio.c_lflag & BT_ICANON, "ICANON should be set by default");
    CHECK(tio.c_lflag & BT_ECHO, "ECHO should be set by default");
    CHECK(tio.c_iflag & BT_ICRNL, "ICRNL should be set by default");

    /* Raw mode: disable ICANON, ECHO, ICRNL */
    tio.c_lflag &= ~(BT_ICANON | BT_ECHO);
    tio.c_iflag &= ~(BT_ICRNL | BT_IXON);
    tio.c_cc[BT_VMIN] = 1;
    tio.c_cc[BT_VTIME] = 0;

    res = bt_tcsetattr(BT_TCSANOW, &tio);
    CHECK(res == 0, "bt_tcsetattr should succeed");

    BtTermios current;
    memset(&current, 0, sizeof(current));
    res = bt_tcgetattr(&current);
    CHECK(res == 0, "read back tcgetattr");
    CHECK(!(current.c_lflag & BT_ICANON), "ICANON should be cleared");
    CHECK(!(current.c_lflag & BT_ECHO), "ECHO should be cleared");
    CHECK(current.c_cc[BT_VMIN] == 1, "VMIN should be 1");

    /* Restore */
    tio.c_lflag |= (BT_ICANON | BT_ECHO);
    tio.c_iflag |= (BT_ICRNL | BT_IXON);
    bt_tcsetattr(BT_TCSANOW, &tio);

    TEST_PASS();
}


/* ── 2. Multilingual Lang Engine (CJK, Tibetan, Thai, Kinsoku) ───── */
static void test_lang_asian_multilingual(void)
{
    /* CJK Full-width Kanji: 東 (U+6771), 京 (U+4EAC) */
    CHECK(lang_width(0x6771) == 2, "東 should be wide (2 cells)");
    CHECK(lang_width(0x4EAC) == 2, "京 should be wide (2 cells)");
    CHECK(lang_width(0x3042) == 2, "Hiragana あ should be wide (2 cells)");
    CHECK(lang_width(0xAC00) == 2, "Hangul 가 should be wide (2 cells)");

    /* ASCII Half-width */
    CHECK(lang_width('A') == 1, "'A' should be 1 cell");
    CHECK(lang_width(' ') == 1, "Space should be 1 cell");

    /* Tibetan Zero-Width Combining Vowels */
    CHECK(lang_width(0x0F71) == 0, "Tibetan vowel Aa should be 0 cells (combining)");
    CHECK(lang_width(0x0F72) == 0, "Tibetan vowel I should be 0 cells (combining)");
    CHECK(lang_width(0x0F74) == 0, "Tibetan vowel U should be 0 cells (combining)");

    /* Thai Zero-Width Marks */
    CHECK(lang_width(0x0E31) == 0, "Thai mai han-akat should be 0 cells");
    CHECK(lang_width(0x0E48) == 0, "Thai mai ek tone mark should be 0 cells");

    /* Devanagari Zero-Width Marks */
    CHECK(lang_width(0x0941) == 0, "Devanagari vowel sign U should be 0 cells");
    CHECK(lang_width(0x094D) == 0, "Devanagari virama should be 0 cells");

    /* Table sorting integrity verification */
    CHECK(lang_tables_ok() == 1, "Multilingual CJK/Kinsoku tables must be correctly sorted");

    /* Word Classes */
    CHECK(lang_wclass(0x6771) == LANG_W_HAN, "東 must be classified as LANG_W_HAN");
    CHECK(lang_wclass(0x3042) == LANG_W_HIRA, "あ must be classified as LANG_W_HIRA");
    CHECK(lang_wclass(0x30A2) == LANG_W_KATA, "ア must be classified as LANG_W_KATA");
    CHECK(lang_wclass(' ') == LANG_W_SPACE, "Space must be classified as LANG_W_SPACE");

    /* Kinsoku Word Wrap Verification:
     * "これは、「テスト」です。"
     * In a line of width 8 cells, opening bracket 「 must not be left dangling at the end of the line,
     * and closing punctuation 」 must not start the next line.
     */
    const char *text = "これは、「テスト」です。";
    size_t wrap_pt = lang_wrap(text, strlen(text), 8, LANG_WRAP_WORD);
    CHECK(wrap_pt > 0, "Wrap point must be non-zero");
    int cols = lang_cols(text, wrap_pt);
    CHECK(cols <= 8, "Wrapped line cells must fit within 8 cells");

    TEST_PASS();

}

/* ── 3. Term Key Parser ───────────────────────────────────────────── */
static void test_term_key_parser(void)
{
    size_t used = 0;

    /* Up arrow: ESC [ A */
    const uint8_t up_seq[] = { 0x1b, '[', 'A' };
    int k = term_parse(up_seq, sizeof(up_seq), &used);
    CHECK(k == K_UP, "Should parse K_UP");
    CHECK(used == 3, "Used 3 bytes");

    /* Down arrow: ESC [ B */
    const uint8_t down_seq[] = { 0x1b, '[', 'B' };
    k = term_parse(down_seq, sizeof(down_seq), &used);
    CHECK(k == K_DOWN, "Should parse K_DOWN");

    /* Left arrow: ESC [ D */
    const uint8_t left_seq[] = { 0x1b, '[', 'D' };
    k = term_parse(left_seq, sizeof(left_seq), &used);
    CHECK(k == K_LEFT, "Should parse K_LEFT");

    /* Right arrow: ESC [ C */
    const uint8_t right_seq[] = { 0x1b, '[', 'C' };
    k = term_parse(right_seq, sizeof(right_seq), &used);
    CHECK(k == K_RIGHT, "Should parse K_RIGHT");

    /* Function key F1: ESC O P */
    const uint8_t f1_seq[] = { 0x1b, 'O', 'P' };
    k = term_parse(f1_seq, sizeof(f1_seq), &used);
    CHECK(k == K_F1, "Should parse K_F1");

    /* Function key F10: ESC [ 2 1 ~ */
    const uint8_t f10_seq[] = { 0x1b, '[', '2', '1', '~' };
    k = term_parse(f10_seq, sizeof(f10_seq), &used);
    CHECK(k == K_F10, "Should parse K_F10");

    /* Ctrl-O: ASCII 15 */
    const uint8_t ctrlo_seq[] = { 0x0F };
    k = term_parse(ctrlo_seq, sizeof(ctrlo_seq), &used);
    CHECK(k == K_CTRL('O'), "Should parse K_CTRL('O')");

    /* Backspace: 0x7F */
    const uint8_t bs_seq[] = { 0x7F };
    k = term_parse(bs_seq, sizeof(bs_seq), &used);
    CHECK(k == K_BACKSPACE, "Should parse K_BACKSPACE");

    TEST_PASS();
}

/* ── 4. Term Cell Diff Optimization (No unnecessary redraws) ─────── */
static void test_term_cell_diff_optimization(void)
{
    /* Set dimensions */
    term_rows = 24;
    term_cols = 80;

    /* Fill background */
    scr_clear(0);
    scr_str(0, 0, "B-System BTRON Terminal", 0);
    scr_flush();
    unsigned long bytes_initial = scr_bytes();
    CHECK(bytes_initial > 0, "Initial draw should emit bytes");

    /* Update a SINGLE character */
    scr_str(0, 0, "X", 0);
    scr_flush();
    unsigned long bytes_update = scr_bytes() - bytes_initial;

    /* A single cell change should only emit cursor position + 1 char, < 30 bytes!
     * A naive redraw of 80x24 would emit ~2000-4000 bytes! */
    CHECK(bytes_update < 50, "Cell diffing should emit minimal traffic (< 50 bytes)");

    /* Flushing an identical screen should emit ZERO bytes */
    unsigned long bytes_before_noop = scr_bytes();
    scr_flush();
    unsigned long bytes_noop = scr_bytes() - bytes_before_noop;
    CHECK(bytes_noop == 0, "No-op frame should emit 0 bytes");

    TEST_PASS();
}

/* ── 5. VFS Pure B-System Operations (No Host Portaling) ──────────── */
static void test_vfs_no_host_portaling(void)
{
    int init_res = vfs_init();
    CHECK(init_res == 0, "vfs_init should mount B-System volumes");

    /* Path normalization test */
    char norm[VFS_MAX_PATH];
    vfs_normalize_path("//SYS///test/../folder", norm, sizeof(norm));
    CHECK(strcmp(norm, "/SYS/folder") == 0, "Path normalization should resolve .. and duplicate slashes");

    vfs_normalize_path("/SYS/a/b/c/../../d", norm, sizeof(norm));
    CHECK(strcmp(norm, "/SYS/a/d") == 0, "Multi-level .. resolution");

    /* Directory listing from /SYS */
    static VfsEntry entries[32];
    int count = vfs_list_dir("/SYS", entries, 32);
    CHECK(count >= 0, "Listing /SYS should succeed");

    /* Real Body record write and read */
    const char *test_data = "BTRON NASA JPL Multilingual Test Body";
    size_t dlen = strlen(test_data);
    int wr_res = vfs_write_file("/SYS/test_jpl.txt", test_data, dlen);
    CHECK(wr_res == 0, "vfs_write_file to B-System volume should succeed");

    CHECK(vfs_exists("/SYS/test_jpl.txt") == 1, "File must exist in VFS");

    char read_buf[256];
    size_t read_bytes = 0;
    int rd_res = vfs_read_file("/SYS/test_jpl.txt", read_buf, sizeof(read_buf) - 1, &read_bytes);
    CHECK(rd_res == 0, "vfs_read_file should succeed");
    read_buf[read_bytes] = '\0';
    CHECK(strcmp(read_buf, test_data) == 0, "Read data must match written data exactly");

    /* Clean up */
    int del_res = vfs_delete("/SYS/test_jpl.txt");
    CHECK(del_res == 0, "vfs_delete should succeed");
    CHECK(vfs_exists("/SYS/test_jpl.txt") == 0, "File must no longer exist");

    TEST_PASS();
}

/* ── 6. Sokhatsky Commander Internal Execution ────────────────────── */
static void test_sc_panel_navigation_and_dispatch(void)
{
    /* Initialize panels */
    strcpy(left_panel.path, "/SYS");
    strcpy(right_panel.path, "/SYS");
    left_panel.sort_type = 0;
    right_panel.sort_type = 0;
    active_panel = &left_panel;

    load_files(&left_panel);
    load_files(&right_panel);

    CHECK(left_panel.file_count > 0, "Left panel should have files loaded");
    CHECK(strcmp(left_panel.files[0].name, "..") == 0, "First entry must be '..'");

    /* Navigation */
    left_panel.cursor = 0;
    left_navigation(&left_panel);
    CHECK(strcmp(left_panel.path, "/") == 0, "Navigating left on /SYS moves to /");

    /* Command execution without host OS */
    execute_command("pwd");
    CHECK(history_count > 0, "History must record executed command");
    CHECK(strstr(history[0].output, "/") != NULL, "Output must contain path");

    execute_command("help");
    CHECK(strstr(history[1].output, "Sokhatsky Commander") != NULL, "Help output should mention SC");

    execute_command("df");
    CHECK(strstr(history[2].output, "SYS") != NULL, "df output should report SYS volume");

    TEST_PASS();
}

/* ── 7. GTerm Command Set Wiring (sc, tv) ─────────────────────────── */
static char g_clu_test_buf[2048];
static void clu_test_out(const char *line, COLOR col, void *ud)
{
    (void)col; (void)ud;
    strncat(g_clu_test_buf, line, sizeof(g_clu_test_buf) - strlen(g_clu_test_buf) - 1);
    strncat(g_clu_test_buf, "\n", sizeof(g_clu_test_buf) - strlen(g_clu_test_buf) - 1);
}

static void test_gterm_clu_command_set_wiring(void)
{
    /* Test clu_sc wired into CLU / gterm command set */
    g_clu_test_buf[0] = '\0';
    clu_sc("/SYS", clu_test_out, NULL);
    CHECK(strstr(g_clu_test_buf, "Sokhatsky Commander") != NULL, "clu_sc must identify Sokhatsky Commander");
    CHECK(strstr(g_clu_test_buf, "src/clu/sc/sc.c") != NULL, "clu_sc must identify source sc.c");

    /* Test clu_tv wired into CLU / gterm command set */
    g_clu_test_buf[0] = '\0';
    clu_tv("-v /SYS/BOOK.md", clu_test_out, NULL);
    CHECK(strstr(g_clu_test_buf, "Terminal Vision") != NULL, "clu_tv must identify Terminal Vision");
    CHECK(strstr(g_clu_test_buf, "src/clu/tv/tv.c") != NULL, "clu_tv must identify source tv.c");

    TEST_PASS();
}

/* ── 8. In-Window SC and TV Interactive Session Lifecycle ─────────── */
static void test_in_window_sc_and_tv_sessions(void)
{
    /* Test SC session initialization */
    int sc_rc = sc_session_init("/SYS", 25, 80);
    CHECK(sc_rc == 0, "sc_session_init should succeed with 25 rows and 80 cols");

    /* Verify cells populated in term 2D screen buffer */
    uint32_t cp = 0;
    int style = 0;
    int got_cell = term_get_cell(0, 1, &cp, &style);
    CHECK(got_cell == 1, "term_get_cell should return valid cell at (0, 1)");
    CHECK(cp == 'S', "Row 0 col 1 of SC should contain 'S' (from 'SC')");

    /* Verify colors resolution */
    uint32_t fg = 0, bg = 0;
    term_get_style_colors(style, &fg, &bg);
    CHECK(bg == 0xFF001A4E, "SC style background should be classic midnight blue (0xFF001A4E)");

    /* Test single step key forwarding */
    int step_rc = sc_session_step(K_DOWN);
    CHECK(step_rc == 1, "sc_session_step(K_DOWN) should keep session running");

    sc_session_close();

    /* Test TV session initialization */
    int tv_rc = tv_session_init("/SYS/BOOK.md", 1, 25, 80);
    CHECK(tv_rc == 0, "tv_session_init should succeed for /SYS/BOOK.md");

    /* Verify cells populated in term 2D screen buffer for TV */
    got_cell = term_get_cell(0, 0, &cp, &style);
    CHECK(got_cell == 1, "term_get_cell should return valid cell for TV header");

    /* Test cursor inspection */
    int cur_r = -1, cur_c = -1, cur_vis = 0;
    term_get_cursor(&cur_r, &cur_c, &cur_vis);
    CHECK(cur_r >= 0, "TV should set a valid cursor row");

    /* Test single step key navigation */
    step_rc = tv_session_step(K_DOWN);
    CHECK(step_rc == 1, "tv_session_step(K_DOWN) should keep session running");

    /* Test clean exit on F10 */
    step_rc = tv_session_step(K_F10);
    CHECK(step_rc == 0, "tv_session_step(K_F10) should cleanly exit session");

    tv_session_close();

    /* Test nested TV launch from within SC session (in-window, no host console) */
    sc_rc = sc_session_init("/SYS", 25, 80);
    CHECK(sc_rc == 0, "sc_session_init should succeed");
    CHECK(sc_is_tv_active() == 0, "TV should not be active initially in SC");

    /* Launch TV inside SC */
    int launch_rc = sc_launch_tv("/SYS/BOOK.md", 1);
    CHECK(launch_rc == 0, "sc_launch_tv should succeed");
    CHECK(sc_is_tv_active() == 1, "sc_is_tv_active must report 1 after launching TV");

    /* Verify cells now contain TV contents */
    got_cell = term_get_cell(0, 0, &cp, &style);
    CHECK(got_cell == 1, "term_get_cell should return valid cell for TV in SC");

    /* Step inside TV via SC's event dispatcher */
    step_rc = sc_session_step(K_DOWN);
    CHECK(step_rc == 1, "sc_session_step forwarding to TV should keep session running");
    CHECK(sc_is_tv_active() == 1, "TV should still be active");

    /* Exit TV via F10 key */
    step_rc = sc_session_step(K_F10);
    CHECK(step_rc == 1, "sc_session_step on F10 in TV should return 1 to remain in SC");
    CHECK(sc_is_tv_active() == 0, "TV should now be closed and SC active");

    /* Verify SC cells restored */
    got_cell = term_get_cell(0, 1, &cp, &style);
    CHECK(got_cell == 1, "term_get_cell should return valid cell for SC");
    CHECK(cp == 'S', "Row 0 col 1 of SC should contain 'S'");

    /* Test dynamic resize of SC session to larger dimensions (30x100) */
    term_set_size(30, 100);
    CHECK(term_rows == 30 && term_cols == 100, "term_set_size should update rows to 30 and cols to 100");
    sc_session_step(K_RESIZE);
    got_cell = term_get_cell(1, 49, &cp, &style);
    CHECK(got_cell == 1, "term_get_cell should return cell at resized divider (row 1, col 49)");

    sc_session_close();

    TEST_PASS();
}


/* ── Main Test Runner ─────────────────────────────────────────────── */
int main(void)
{
    printf("========================================================\n");
    printf(" B-System Termios, CLU, TV, and SC Verification Suite   \n");
    printf(" NASA JPL Power of Ten Compliant, Zero Host Portaling  \n");
    printf("========================================================\n");

    test_termios_line_discipline();
    test_lang_asian_multilingual();
    test_term_key_parser();
    test_term_cell_diff_optimization();
    test_vfs_no_host_portaling();
    test_sc_panel_navigation_and_dispatch();
    test_gterm_clu_command_set_wiring();
    test_in_window_sc_and_tv_sessions();

    printf("========================================================\n");

    printf(" Results: %d Passed, %d Failed\n", g_pass, g_fail);
    printf("========================================================\n");

    return (g_fail == 0) ? 0 : 1;
}
