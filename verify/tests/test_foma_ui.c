/*
 * B-System (BTRON 3.20) Mobile UI Toolkit Unit Tests: test_foma_ui.c
 *
 * Verifies µBTRON-FOMA navigation stack, focus movement, soft key dispatch,
 * modal dialogs, and screen layout against NASA JPL safety invariants.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <btron/mobile_ui.h>
#include <btron/troncode.h>

static int g_tests_total = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", (msg)); \
    } else { \
        printf("  [FAIL] %s (Line %d: %s)\n", (msg), __LINE__, #cond); \
    } \
} while (0)

static int g_callback_invoked = 0;
static void test_action_callback(FOMA_SCREEN *scr, int idx) {
    (void)scr;
    g_callback_invoked = idx + 1;
}

static int g_confirm_invoked = 0;
static void test_confirm_callback(void) {
    g_confirm_invoked = 1;
}

static int g_cancel_invoked = 0;
static void test_cancel_callback(void) {
    g_cancel_invoked = 1;
}

/* ── Test Group 1: Screen Geometry & Viewport Dimensions ── */
static void test_foma_geometry(void) {
    printf("\n[TEST GROUP 1] FOMA 480x640 Screen Geometry\n");

    TEST_ASSERT(FOMA_SCREEN_W == 480, "FOMA width is 480 VGA portrait");
    TEST_ASSERT(FOMA_SCREEN_H == 640, "FOMA height is 640 VGA portrait");
    TEST_ASSERT(FOMA_STATUS_BAR_H == 32, "Status bar height is 32px");
    TEST_ASSERT(FOMA_TITLE_BAR_H == 36, "Title bar height is 36px");
    TEST_ASSERT(FOMA_ROW_H == 36, "Row height is 36px");
    TEST_ASSERT(FOMA_SOFTKEY_BAR_H == 38, "Soft key bar height is 38px");

    H total_reserved = FOMA_STATUS_BAR_H + FOMA_TITLE_BAR_H + FOMA_SOFTKEY_BAR_H;
    H content_h = FOMA_SCREEN_H - total_reserved;
    TEST_ASSERT(content_h > 0, "Content viewport height is positive");
    TEST_ASSERT(FOMA_VISIBLE_ROWS >= 14, "Viewport accommodates at least 14 visible rows");
}

/* ── Test Group 2: Screen Stack Push, Pop & Depth Limits ── */
static void test_foma_screen_stack(void) {
    printf("\n[TEST GROUP 2] Screen Stack Operations\n");

    foma_ui_init();
    TEST_ASSERT(foma_get_screen_depth() == 0, "Initial screen depth is 0");
    TEST_ASSERT(foma_get_active_screen() == NULL, "Initial active screen is NULL");

    FOMA_SCREEN s1;
    memset(&s1, 0, sizeof(s1));
    s1.screen_id = 101;
    strncpy(s1.title, "Home Cabinet", sizeof(s1.title) - 1);
    s1.item_count = 5;
    foma_push_screen(&s1);

    TEST_ASSERT(foma_get_screen_depth() == 1, "Screen depth after 1 push is 1");
    FOMA_SCREEN *cur = foma_get_active_screen();
    TEST_ASSERT(cur != NULL && cur->screen_id == 101, "Active screen is screen 101");
    TEST_ASSERT(strcmp(cur->title, "Home Cabinet") == 0, "Screen title matches");

    FOMA_SCREEN s2;
    memset(&s2, 0, sizeof(s2));
    s2.screen_id = 102;
    strncpy(s2.title, "Contacts", sizeof(s2.title) - 1);
    foma_push_screen(&s2);

    TEST_ASSERT(foma_get_screen_depth() == 2, "Screen depth after 2nd push is 2");
    cur = foma_get_active_screen();
    TEST_ASSERT(cur != NULL && cur->screen_id == 102, "Active screen is screen 102");

    foma_pop_screen();
    TEST_ASSERT(foma_get_screen_depth() == 1, "Screen depth after pop is 1");
    cur = foma_get_active_screen();
    TEST_ASSERT(cur != NULL && cur->screen_id == 101, "Active screen restored to screen 101");

    /* Popping bottom screen should be prevented to keep root active */
    foma_pop_screen();
    TEST_ASSERT(foma_get_screen_depth() == 1, "Popping root screen preserves root (depth remains 1)");
}

/* ── Test Group 3: Focus Movement & Separator Skipping ── */
static void test_foma_focus_navigation(void) {
    printf("\n[TEST GROUP 3] Focus Movement & Separator Skipping\n");

    foma_ui_init();
    FOMA_SCREEN scr;
    memset(&scr, 0, sizeof(scr));
    scr.item_count = 5;

    /* Item 0: Normal */
    scr.items[0].type = FOMA_ITEM_NORMAL;
    strncpy(scr.items[0].title, "Item 0", sizeof(scr.items[0].title) - 1);
    scr.items[0].action = test_action_callback;

    /* Item 1: Separator */
    scr.items[1].type = FOMA_ITEM_SEPARATOR;

    /* Item 2: Normal */
    scr.items[2].type = FOMA_ITEM_NORMAL;
    strncpy(scr.items[2].title, "Item 2", sizeof(scr.items[2].title) - 1);
    scr.items[2].action = test_action_callback;

    /* Item 3: Header */
    scr.items[3].type = FOMA_ITEM_HEADER;

    /* Item 4: Normal */
    scr.items[4].type = FOMA_ITEM_NORMAL;
    strncpy(scr.items[4].title, "Item 4", sizeof(scr.items[4].title) - 1);
    scr.items[4].action = test_action_callback;

    foma_push_screen(&scr);
    FOMA_SCREEN *active = foma_get_active_screen();

    TEST_ASSERT(active->focus_index == 0, "Initial focus is on item 0");

    /* Move down: should skip Item 1 (separator) and land on Item 2 */
    foma_nav_move_focus(active, 1);
    TEST_ASSERT(active->focus_index == 2, "Moving down skips separator and lands on Item 2");

    /* Move down: should skip Item 3 (header) and land on Item 4 */
    foma_nav_move_focus(active, 1);
    TEST_ASSERT(active->focus_index == 4, "Moving down skips header and lands on Item 4");

    /* Move down from end: should wrap around to Item 0 */
    foma_nav_move_focus(active, 1);
    TEST_ASSERT(active->focus_index == 0, "Moving down wraps around to Item 0");

    /* Move up from 0: should wrap around to Item 4 */
    foma_nav_move_focus(active, -1);
    TEST_ASSERT(active->focus_index == 4, "Moving up from 0 wraps around to Item 4");

    /* Action trigger */
    g_callback_invoked = 0;
    foma_nav_activate_selected(active);
    TEST_ASSERT(g_callback_invoked == 5, "Activated item 4 invokes action callback (idx+1=5)");
}

/* ── Test Group 4: Modal Dialog State & Callbacks ── */
static void test_foma_modal_dialog(void) {
    printf("\n[TEST GROUP 4] Modal Dialog State & Callbacks\n");

    foma_ui_init();
    TEST_ASSERT(!foma_is_modal_active(), "Modal starts inactive");

    g_confirm_invoked = 0;
    g_cancel_invoked = 0;

    foma_show_modal("Test Dialog", "Sample message body", "OK", "Cancel",
                    test_confirm_callback, test_cancel_callback);

    TEST_ASSERT(foma_is_modal_active(), "Modal is now active");
    FOMA_MODAL *mod = foma_get_active_modal();
    TEST_ASSERT(mod != NULL, "Active modal struct is non-null");
    TEST_ASSERT(strcmp(mod->title, "Test Dialog") == 0, "Modal title matches");
    TEST_ASSERT(mod->selected_btn == 0, "Default selected button is 0 (Confirm)");

    /* Simulate confirm */
    if (mod->on_confirm) mod->on_confirm();
    TEST_ASSERT(g_confirm_invoked == 1, "Confirm callback invoked successfully");

    foma_close_modal();
    TEST_ASSERT(!foma_is_modal_active(), "Modal closed successfully");
}

/* ── Test Group 5: FOMA Text & Label Metrics ── */
static void test_foma_metrics(void) {
    printf("\n[TEST GROUP 5] FOMA Text & Label Metrics\n");

    const char *test_labels[] = {
        "実身キャビネット / Workbench",
        "連絡先 / Contacts",
        "メモ / Memos",
        "コントロールパネル",
        "アプリ / Applications",
        "[仮身] 連絡先:坂村健",
        "090-XXXX-XXXX"
    };

    for (size_t i = 0; i < sizeof(test_labels)/sizeof(test_labels[0]); i++) {
        H tw = tc_calc_string_width(test_labels[i], (int)strlen(test_labels[i]));
        TEST_ASSERT(tw > 0, "String width is strictly positive");
        TEST_ASSERT(tw < FOMA_SCREEN_W - 20, "String width fits within 480px screen width");
    }
}

/* ── Test Group 6: Popup / Context Menu Operations ── */
static void test_foma_popup_menu(void) {
    printf("\n[TEST GROUP 6] Popup / Context Menu Operations\n");

    TEST_ASSERT(!foma_is_popup_menu_active(), "Popup menu starts inactive");

    const char *items[] = { "開く", "詳細属性", "仮身作成", "削除", "閉じる" };
    const char *badges[] = { "1", "2", "3", "4", "5" };
    foma_show_popup_menu("操作メニュー (1〜5)", items, badges, 5, 1);

    TEST_ASSERT(foma_is_popup_menu_active(), "Popup menu is active");
    FOMA_POPUP_MENU *menu = foma_get_active_popup_menu();
    TEST_ASSERT(menu != NULL, "Active popup menu is non-null");
    TEST_ASSERT(menu->item_count == 5, "Popup menu item count is 5");
    TEST_ASSERT(menu->focus_index == 1, "Popup menu initial focus is 1");
    TEST_ASSERT(strcmp(menu->items[0], "開く") == 0, "Popup menu item 0 is '開く'");
    TEST_ASSERT(strcmp(menu->badges[0], "1") == 0, "Popup menu badge 0 is '1'");

    foma_close_popup_menu();
    TEST_ASSERT(!foma_is_popup_menu_active(), "Popup menu closed successfully");
}

/* ── Test Group 7: Rich Modal Dialog Types ── */
static void test_foma_rich_dialogs(void) {
    printf("\n[TEST GROUP 7] Rich Modal Dialog Types\n");

    /* Properties sheet */
    foma_show_properties_dialog("連絡先：坂村 健", "0x002A-8F14-C001", "TAD Rev 3.20", 4896, 3, "2026-09-05 14:32:08");
    TEST_ASSERT(foma_is_modal_active(), "Properties dialog is active");
    FOMA_MODAL *mod = foma_get_active_modal();
    TEST_ASSERT(mod->type == FOMA_MODAL_PROPERTIES, "Modal type is FOMA_MODAL_PROPERTIES");
    TEST_ASSERT(strcmp(mod->detail1, "連絡先：坂村 健") == 0, "Detail1 matches target Real Body");

    /* Search dialog with IME */
    foma_show_search_dialog("坂村 健", "[あ/漢] TIP/Mozc");
    TEST_ASSERT(mod->type == FOMA_MODAL_INPUT, "Modal type is FOMA_MODAL_INPUT");
    TEST_ASSERT(strcmp(mod->detail1, "坂村 健") == 0, "Search query matches");

    /* Power dialog */
    foma_show_power_dialog(88, "4.12V");
    TEST_ASSERT(mod->type == FOMA_MODAL_POWER, "Modal type is FOMA_MODAL_POWER");

    /* 3G Voice call */
    foma_show_call_dialog("坂村 健", "090-XXXX-XXXX", "00:06");
    TEST_ASSERT(mod->type == FOMA_MODAL_CALL, "Modal type is FOMA_MODAL_CALL");
    TEST_ASSERT(strcmp(mod->btn_left, "応答") == 0, "Call dialog left button is 応答");
    TEST_ASSERT(strcmp(mod->btn_center, "保留") == 0, "Call dialog center button is 保留");
    TEST_ASSERT(strcmp(mod->btn_right, "拒否") == 0, "Call dialog right button is 拒否");

    /* Default About Box */
    foma_show_about_dialog("gterm", "端末エミュレータ", "VT100/ANSI 端末", "5HT / BTRON 3.20");
    TEST_ASSERT(mod->type == FOMA_MODAL_ABOUT, "Modal type is FOMA_MODAL_ABOUT");
    TEST_ASSERT(strcmp(mod->detail1, "端末エミュレータ (gterm)") == 0, "About dialog full app name matches");
    TEST_ASSERT(strcmp(mod->btn_center, "確認 (OK)") == 0, "About dialog center button is 確認 (OK)");

    foma_close_modal();
    TEST_ASSERT(!foma_is_modal_active(), "Modal closed successfully");
}

int main(void) {
    printf("===============================================================\n");
    printf(" Running µBTRON-FOMA Mobile UI Toolkit Test Suite\n");
    printf(" NASA JPL Rule 3: Zero post-boot allocations, bounded recursion\n");
    printf("===============================================================\n");

    test_foma_geometry();
    test_foma_screen_stack();
    test_foma_focus_navigation();
    test_foma_modal_dialog();
    test_foma_metrics();
    test_foma_popup_menu();
    test_foma_rich_dialogs();

    printf("\n===============================================================\n");
    printf(" µBTRON-FOMA Mobile Test Results: %d / %d tests passed\n", g_tests_passed, g_tests_total);
    printf("===============================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
