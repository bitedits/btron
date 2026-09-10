/*
 * Test Suite for B-TRON Common Application Menu Subsystem (app_menu)
 */

#include <btron/app_menu.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_test_total = 0;
static int g_test_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_test_total++; \
    if (cond) { \
        g_test_passed++; \
        printf("  [PASS] %s\n", msg); \
    } else { \
        printf("  [FAIL] %s (Line %d)\n", msg, __LINE__); \
    } \
} while (0)

static void test_menu_construction(void) {
    printf("\n[TEST GROUP 1] Menu Construction & Layout Geometry\n");

    APP_MENU_BAR bar;
    app_menu_init(&bar, APP_MENU_STYLE_CLASSIC_3D);
    TEST_ASSERT(bar.header_count == 0, "Initial header count is 0");
    TEST_ASSERT(bar.active_menu == -1, "Initial active_menu is -1 (closed)");
    TEST_ASSERT(bar.hover_menu == -1, "Initial hover_menu is -1");

    int h0 = app_menu_add_header(&bar, "ファイル(F)", 104);
    int h1 = app_menu_add_header(&bar, "編集(E)", 72);
    int h2 = app_menu_add_header(&bar, "表示(V)", 72);
    int h3 = app_menu_add_header(&bar, "ヘルプ(H)", 88);

    TEST_ASSERT(h0 == 0 && h1 == 1 && h2 == 2 && h3 == 3, "Headers added in sequence 0..3");
    TEST_ASSERT(bar.header_count == 4, "Total header count is 4");
    TEST_ASSERT(bar.headers[0].rect.left == 4, "First header starts at x=4");
    TEST_ASSERT(bar.headers[0].rect.right == 108, "First header right edge is 108");
    TEST_ASSERT(bar.headers[1].rect.left == 114, "Second header has spacing at x=114");

    /* Add items to File header */
    int it0 = app_menu_add_submenu_item(&bar, h0, "開く (Open) ▶", 102, 1);
    int it1 = app_menu_add_separator(&bar, h0);
    int it2 = app_menu_add_item(&bar, h0, "閉じる (Close)", "Ctrl+W", 101, TRUE);

    TEST_ASSERT(it0 == 0, "Submenu item is index 0");
    TEST_ASSERT(bar.headers[0].items[0].has_submenu == TRUE, "Submenu item flagged has_submenu=TRUE");
    TEST_ASSERT(it1 == 1, "Separator is index 1");
    TEST_ASSERT(bar.headers[0].items[1].is_separator == TRUE, "Separator flagged is_separator=TRUE");
    TEST_ASSERT(it2 == 2, "Normal item is index 2");
    TEST_ASSERT(bar.headers[0].items[2].cmd_id == 101, "Command ID assigned properly");

    app_menu_set_right_text(&bar, "[100%] (実身閲覧)");
    TEST_ASSERT(strcmp(bar.right_text, "[100%] (実身閲覧)") == 0, "Right status text updated");
}

static void test_fluid_hover_and_hot_switching(void) {
    printf("\n[TEST GROUP 2] BeOS Fluid Hover Tracking & Hot Header Gliding\n");

    APP_MENU_BAR bar;
    app_menu_init(&bar, APP_MENU_STYLE_CLASSIC_3D);
    app_menu_add_header(&bar, "ファイル(F)", 104);
    app_menu_add_header(&bar, "編集(E)", 72);
    app_menu_add_header(&bar, "表示(V)", 72);

    /* 1. Closed state hover */
    BOOL moved = app_menu_handle_mouse_move(&bar, 50, 10);
    TEST_ASSERT(moved == TRUE, "Hover change detected");
    TEST_ASSERT(bar.hover_menu == 0, "Hovered over header 0");

    app_menu_handle_mouse_move(&bar, 140, 10);
    TEST_ASSERT(bar.hover_menu == 1, "Hovered over header 1");

    app_menu_handle_mouse_move(&bar, 140, 40);
    TEST_ASSERT(bar.hover_menu == -1, "Pointer moved off bar clears hover_menu = -1");

    /* 2. Click to open dropdown */
    int cmd = 0, sub = -1;
    BOOL clicked = app_menu_handle_mouse_down(&bar, 50, 10, &cmd, &sub);
    TEST_ASSERT(clicked == TRUE, "Header click opens menu");
    TEST_ASSERT(bar.active_menu == 0, "Active menu is now header 0");

    /* 3. Hot header gliding (switch to header 1 without click) */
    app_menu_handle_mouse_move(&bar, 140, 10);
    TEST_ASSERT(bar.active_menu == 1, "Gliding over header 1 hot-switches active_menu = 1");

    /* 4. Click header again to toggle closed */
    app_menu_handle_mouse_down(&bar, 140, 10, &cmd, &sub);
    TEST_ASSERT(bar.active_menu == -1, "Clicking active header toggles menu closed");
}

static void test_item_clicks_and_key_dismissal(void) {
    printf("\n[TEST GROUP 3] Item Clicks, Submenus & Key Dismissal\n");

    APP_MENU_BAR bar;
    app_menu_init(&bar, APP_MENU_STYLE_CLASSIC_3D);
    int h0 = app_menu_add_header(&bar, "ファイル(F)", 104);
    app_menu_add_submenu_item(&bar, h0, "開く (Open) ▶", 200, 1);
    app_menu_add_separator(&bar, h0);
    app_menu_add_item(&bar, h0, "保存 (Save)", "Ctrl+S", 201, TRUE);

    /* Open header 0 */
    app_menu_open(&bar, h0);
    TEST_ASSERT(bar.active_menu == 0, "Menu opened");

    /* Move over '保存 (Save)' at row 2 (y = 21 + 3 + 2*22 + 10 = 68) */
    app_menu_handle_mouse_move(&bar, 50, 68);
    TEST_ASSERT(bar.hover_item == 2, "Hovered item is index 2");

    /* Click on '保存 (Save)' */
    int cmd = 0, sub = -1;
    BOOL handled = app_menu_handle_mouse_down(&bar, 50, 68, &cmd, &sub);
    TEST_ASSERT(handled == TRUE, "Item click handled");
    TEST_ASSERT(cmd == 201, "Dispatched command 201");
    TEST_ASSERT(bar.active_menu == -1, "Menu closed after command execution");

    /* Reopen and test Escape dismissal */
    app_menu_open(&bar, h0);
    TEST_ASSERT(bar.active_menu == 0, "Menu reopened");
    BOOL key_handled = app_menu_handle_key(&bar, BTRON_KEY_ESCAPE, 0, &cmd);
    TEST_ASSERT(key_handled == TRUE, "Escape key handled");
    TEST_ASSERT(bar.active_menu == -1, "Escape dismissed menu");
}

static void test_menu_styles_and_rendering(void) {
    printf("\n[TEST GROUP 4] Menu Styles (Classic 3D vs. Modern Card) & Rendering\n");

    TEST_ASSERT(app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D, "Default style is Classic 3D");
    app_menu_set_global_style(APP_MENU_STYLE_MODERN_CARD);
    TEST_ASSERT(app_menu_get_global_style() == APP_MENU_STYLE_MODERN_CARD, "Global style toggled to Modern Card");
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    TEST_ASSERT(app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D, "Global style restored to Classic 3D");

    /* Allocate virtual device for paint verification */
    GDEV dev;
    memset(&dev, 0, sizeof(dev));
    dev.width = 640;
    dev.height = 400;
    COLOR pixels[640 * 400];
    dev.pixels = pixels;
    dev.clip.left = 0; dev.clip.top = 0; dev.clip.right = 640; dev.clip.bottom = 400;

    APP_MENU_BAR bar;
    app_menu_init(&bar, APP_MENU_STYLE_CLASSIC_3D);
    int h0 = app_menu_add_header(&bar, "ファイル(F)", 104);
    app_menu_add_item(&bar, h0, "新規 (New)", "Ctrl+N", 101, TRUE);
    app_menu_add_item(&bar, h0, "閉じる (Close)", "Ctrl+W", 102, TRUE);
    app_menu_set_right_text(&bar, "Test App");

    /* Paint bar */
    app_menu_paint_bar(&bar, &dev);
    TEST_ASSERT(dev.pixels[0] != 0, "Menu bar pixels rendered");

    /* Open and paint Classic 3D dropdown */
    app_menu_open(&bar, h0);
    app_menu_paint_dropdown(&bar, &dev);
    TEST_ASSERT(dev.pixels[30 * 640 + 20] != 0, "Classic 3D dropdown pixels rendered");

    /* Paint Modern Card dropdown */
    bar.style = APP_MENU_STYLE_MODERN_CARD;
    bar.use_global_style = FALSE;
    app_menu_paint_dropdown(&bar, &dev);
    TEST_ASSERT(dev.pixels[30 * 640 + 20] != 0, "Modern Card dropdown pixels rendered");

    /* Paint Cascading Submenu */
    static const char sample_files[3][64] = { "doc1.tad", "doc2.tad", "doc3.tad" };
    bar.active_submenu = 0;
    app_menu_paint_cascading_strings(&bar, &dev, sample_files, 3);
    TEST_ASSERT(dev.pixels[50 * 640 + 260] != 0, "Cascading submenu pixels rendered");
}

static void test_nano_about_box(void) {
    printf("\n[TEST GROUP 5] Common Nano About Box Dialog & 5HT Attribution\n");

    WND *wnd = app_menu_create_about_dialog("CommonApp", "共通アプリ", "Test Application Description", "Brought to B-System by 5HT", 200, 150);
    TEST_ASSERT(wnd != NULL, "Created nano About box dialog window");
    H w = wnd->bounds.right - wnd->bounds.left;
    H h = wnd->bounds.bottom - wnd->bounds.top;
    TEST_ASSERT(w == 520 && h == 270, "About dialog has aligned 520x270 dimensions");
    TEST_ASSERT(strstr(wnd->title, "CommonApp") != NULL, "Dialog title contains app name");

    /* Dismiss via Escape */
    EVT ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = EV_KEY_DOWN;
    ev.key = BTRON_KEY_ESCAPE;
    wnd->event_handler(wnd, &ev);
}

int main(void) {
    printf("==========================================================\n");
    printf(" B-System Common Application Menu Subsystem Test Suite\n");
    printf(" Testing Deduplicated Menu Engine, 3D Bevel & Styles\n");
    printf("==========================================================\n");

    test_menu_construction();
    test_fluid_hover_and_hot_switching();
    test_item_clicks_and_key_dismissal();
    test_menu_styles_and_rendering();
    test_nano_about_box();

    printf("\n==========================================================\n");
    printf(" APP MENU TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_test_passed, g_test_total, (g_test_passed * 100.0) / g_test_total);
    printf("==========================================================\n");

    return (g_test_passed == g_test_total) ? 0 : 1;
}
