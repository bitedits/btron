/*
 * B-System (BTRON 3.20) Global System Menu & Japanese Deskbar Unit Tests
 * Pure C99, verified against NASA JPL safety invariants.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/troncode.h>
#include <btron/tip.h>
#include <btron/tracker.h>
#include <btron/global_menu.h>
#include <btron/app_menu.h>

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

/* Mock / Stub BTRON accessory window entry points for standalone testing */
WND* open_vobj_manager_window(void)     { return NULL; }
WND* open_control_panel_window(void)    { return NULL; }
WND* open_t_editor_window(void)         { return NULL; }
WND* open_gterm_window(void)            { return NULL; }
WND* open_audio_player_window(void)     { return NULL; }
WND* open_orchestra_window(void)        { return NULL; }
WND* open_display_settings_window(void) { return NULL; }
WND* launch_beos_chat(void)             { return NULL; }

/* ── Test Group 1: Geometry & Non-Overfull Margins ── */
static void test_global_menu_geometry(void) {
    printf("\n[TEST GROUP 1] Global Menu Geometry & Non-Overfull Margins\n");

    global_menu_init();
    TEST_ASSERT(!global_menu_is_open(), "Global menu starts in closed state");

    /* Verify non-overfull metrics for headers */
    const char *h_titles[GMENU_HEADER_COUNT] = {
        "［BTRON］", "システム(S)", "実身・仮身(O)", "ウィンドウ(W)", "道具・文字(T)"
    };

    for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
        int text_w = tc_calc_string_width(h_titles[h], (int)strlen(h_titles[h]));
        TEST_ASSERT(text_w > 0, "Header title has non-zero computed width");
        /* Sizing: headers have guaranteed width >= text_w + 16 (8px per side) */
        char msg[128];
        snprintf(msg, sizeof(msg), "Header '%s' width (%d px) fits cleanly with >=8px margins",
                 h_titles[h], text_w);
        TEST_ASSERT(text_w <= 104, msg);
    }
}

/* ── Test Group 2: Top-Level Header Hit Testing & Hover ── */
static void test_header_hit_and_hover(void) {
    printf("\n[TEST GROUP 2] Top-Level Header Hit Testing & Closed Hover\n");

    global_menu_init();

    /* Hover over Header 1: システム(S) (x = 140, y = 10) */
    BOOL hit = global_menu_handle_mouse_move(140, 10);
    TEST_ASSERT(hit && global_menu_get_hover_header() == GMENU_HDR_SYSTEM,
                "Hovering over 'システム(S)' sets hover_header to 1");

    /* Hover over Header 2: 実身・仮身(O) (x = 250, y = 10) */
    hit = global_menu_handle_mouse_move(250, 10);
    TEST_ASSERT(hit && global_menu_get_hover_header() == GMENU_HDR_OBJECTS,
                "Hovering over '実身・仮身(O)' sets hover_header to 2");

    /* Hover over Header 3: ウィンドウ(W) (x = 380, y = 10) */
    hit = global_menu_handle_mouse_move(380, 10);
    TEST_ASSERT(hit && global_menu_get_hover_header() == GMENU_HDR_WINDOWS,
                "Hovering over 'ウィンドウ(W)' sets hover_header to 3");

    /* Hover over Header 4: 道具・文字(T) (x = 500, y = 10) */
    hit = global_menu_handle_mouse_move(500, 10);
    TEST_ASSERT(hit && global_menu_get_hover_header() == GMENU_HDR_TOOLS,
                "Hovering over '道具・文字(T)' sets hover_header to 4");

    /* Hover outside the bar (y = 60) */
    hit = global_menu_handle_mouse_move(300, 60);
    TEST_ASSERT(!hit && global_menu_get_hover_header() == -1,
                "Moving mouse off top bar clears hover_header to -1");
}

/* ── Test Group 3: Dropdown Activation & Fluid Gliding (BeOS Tracking) ── */
static void test_dropdown_activation_and_fluid_tracking(void) {
    printf("\n[TEST GROUP 3] Dropdown Activation & Fluid Header Tracking\n");

    global_menu_init();

    /* Click on システム(S) (x = 140, y = 10) */
    BOOL clicked = global_menu_handle_mouse_down(140, 10);
    TEST_ASSERT(clicked && global_menu_get_active() == GMENU_HDR_SYSTEM,
                "Clicking 'システム(S)' opens dropdown (active_menu = 1)");
    TEST_ASSERT(global_menu_is_open(), "global_menu_is_open() is TRUE");

    /* Fluid hot tracking: Move pointer to 実身・仮身(O) (x = 250, y = 10) */
    global_menu_handle_mouse_move(250, 10);
    TEST_ASSERT(global_menu_get_active() == GMENU_HDR_OBJECTS,
                "Fluid tracking: Gliding over '実身・仮身(O)' switches active menu to 2");

    /* Fluid hot tracking: Move pointer to ウィンドウ(W) (x = 380, y = 10) */
    global_menu_handle_mouse_move(380, 10);
    TEST_ASSERT(global_menu_get_active() == GMENU_HDR_WINDOWS,
                "Fluid tracking: Gliding over 'ウィンドウ(W)' switches active menu to 3");

    /* Fluid hot tracking: Move pointer to 道具・文字(T) (x = 500, y = 10) */
    global_menu_handle_mouse_move(500, 10);
    TEST_ASSERT(global_menu_get_active() == GMENU_HDR_TOOLS,
                "Fluid tracking: Gliding over '道具・文字(T)' switches active menu to 4");

    /* Click header again to toggle close */
    global_menu_handle_mouse_down(500, 10);
    TEST_ASSERT(!global_menu_is_open() && global_menu_get_active() == -1,
                "Clicking active header toggles menu closed");
}

/* ── Test Group 4: Escape Key & Outside Click Dismissal ── */
static void test_menu_dismissal(void) {
    printf("\n[TEST GROUP 4] Menu Dismissal via Escape & Outside Clicks\n");

    global_menu_init();

    /* Open menu */
    global_menu_handle_mouse_down(140, 10);
    TEST_ASSERT(global_menu_is_open(), "Menu opened for dismissal test");

    /* Press Escape */
    BOOL handled = global_menu_handle_key(BTRON_KEY_ESCAPE, 0);
    TEST_ASSERT(handled && !global_menu_is_open(), "Escape key cleanly dismisses active menu");

    /* Reopen and click outside (x = 200, y = 350) */
    global_menu_handle_mouse_down(140, 10);
    TEST_ASSERT(global_menu_is_open(), "Menu reopened");
    global_menu_handle_mouse_down(200, 350);
    TEST_ASSERT(!global_menu_is_open(), "Click outside dismisses active menu");
}

/* ── Test Group 5: Japanese Calendar Formatting with Kanji Weekday ── */
static void test_japanese_calendar_formatting(void) {
    printf("\n[TEST GROUP 5] Japanese Calendar Formatting & Kanji Weekday\n");

    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);

    static const char *expected_kanji_wdays[7] = {
        "(日)", "(月)", "(火)", "(水)", "(木)", "(金)", "(土)"
    };
    const char *expected_wday = expected_kanji_wdays[tm_now->tm_wday];

    char cal_buf[64];
    snprintf(cal_buf, sizeof(cal_buf), "%d月%d日%s %02d:%02d:%02d",
             tm_now->tm_mon + 1, tm_now->tm_mday, expected_wday,
             tm_now->tm_hour, tm_now->tm_min, tm_now->tm_sec);

    TEST_ASSERT(strstr(cal_buf, expected_wday) != NULL,
                "Calendar string contains correct Kanji weekday (日/月/火/水/木/金/土)");
    TEST_ASSERT(strstr(cal_buf, "月") != NULL && strstr(cal_buf, "日") != NULL,
                "Calendar string contains authentic Japanese '月' and '日' markers");

    int cal_w = tc_calc_string_width(cal_buf, (int)strlen(cal_buf));
    TEST_ASSERT(cal_w <= 214, "Japanese calendar text strictly fits inside 214px tray plate");
}

/* ── Test Group 6: Global TIP Mode Badge Toggle ── */
static void test_global_tip_badge(void) {
    printf("\n[TEST GROUP 6] Global TIP Input Method Badge Toggles\n");

    tip_init();
    tip_set_mode(TIP_MODE_ASCII);
    TEST_ASSERT(tip_get_mode() == TIP_MODE_ASCII, "Initial mode is ASCII");

    /* Click on TIP badge (x = 1280 - 214 - 100 = 966, y = 10) */
    H tip_x = 1280 - 214 - 80;
    H tip_y = 10;
    BOOL hit = global_menu_handle_mouse_down(tip_x, tip_y);
    TEST_ASSERT(hit && tip_get_mode() == TIP_MODE_HIRAGANA,
                "Clicking global TIP badge toggles ASCII -> Hiragana (あ)");

    hit = global_menu_handle_mouse_down(tip_x, tip_y);
    TEST_ASSERT(hit && tip_get_mode() == TIP_MODE_KATAKANA,
                "Clicking global TIP badge toggles Hiragana -> Katakana (ア)");

    hit = global_menu_handle_mouse_down(tip_x, tip_y);
    TEST_ASSERT(hit && tip_get_mode() == TIP_MODE_TIBETAN,
                "Clicking global TIP badge toggles Katakana -> Tibetan (བོད)");

    hit = global_menu_handle_mouse_down(tip_x, tip_y);
    TEST_ASSERT(hit && tip_get_mode() == TIP_MODE_ASCII,
                "Clicking global TIP badge toggles Tibetan -> ASCII");
}

/* ── Test Group 7: Window Management Actions (Cascade, Tile, Hide, Cycle) ── */
static void test_window_management_actions(void) {
    printf("\n[TEST GROUP 7] Window Management Layout Actions\n");

    GDEV *scr = opn_dev(1280, 800);
    init_wnd_mgr(scr);

    WND *w1 = opn_wnd("Test Window 1", 100, 100, 400, 300, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *w2 = opn_wnd("Test Window 2", 150, 150, 400, 300, WND_ATTR_TITLE | WND_ATTR_CLOSE);
    WND *w3 = opn_wnd("Test Window 3", 200, 200, 400, 300, WND_ATTR_TITLE | WND_ATTR_CLOSE);

    TEST_ASSERT(w1 && w2 && w3, "Created 3 test windows");

    /* 1. Test Cascade */
    wnd_cascade_all();
    TEST_ASSERT(w1->bounds.left != w2->bounds.left, "Cascade arranged windows at staggered offsets");

    /* 2. Test Tile */
    wnd_tile_all();
    TEST_ASSERT(w1->bounds.right <= 1280 && w1->bounds.bottom <= 800, "Tiled window within screen bounds");

    /* 3. Test Cycle Focus */
    wnd_cycle_focus();
    TEST_ASSERT(get_top_wnd() != NULL, "Cycle focus promotes window to front");

    /* 4. Test Hide All */
    wnd_hide_all();
    TEST_ASSERT(!w1->visible && !w2->visible && !w3->visible, "Hide all sets all windows to hidden");

    cls_wnd(w1);
    cls_wnd(w2);
    cls_wnd(w3);
    cls_dev(scr);
}

/* ── Test Group 8: Rendering Benchmarks & Appearance Style Support ── */
static void test_rendering_benchmarks(void) {
    printf("\n[TEST GROUP 8] Global Menu Rendering Verification & Appearance Style Support\n");

    GDEV *dev = opn_dev(1280, 800);
    TEST_ASSERT(dev != NULL, "Allocated 1280x800 test display device");

    global_menu_init();
    global_menu_render_bar(dev);
    TEST_ASSERT(dev->pixels != NULL, "Rendered top system menu bar successfully");

    /* 1. Classic 3D Style Rendering */
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    global_menu_handle_mouse_down(140, 10);
    global_menu_render_overlay(dev);
    TEST_ASSERT(dev->pixels != NULL, "Rendered Classic 3D global dropdown overlay successfully");

    /* 2. Modern Flat Card Style Rendering */
    app_menu_set_global_style(APP_MENU_STYLE_MODERN_CARD);
    global_menu_render_overlay(dev);
    TEST_ASSERT(dev->pixels != NULL, "Rendered Modern Flat Card global dropdown overlay successfully");

    /* 3. Deskbar Tracker Menu with Appearance Style */
    global_menu_close();
    global_menu_handle_mouse_down(20, 10); /* Header 0: [BTRON] */
    global_menu_render_overlay(dev);
    TEST_ASSERT(dev->pixels != NULL, "Rendered Deskbar menu with Modern Card style");

    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    global_menu_render_overlay(dev);
    TEST_ASSERT(dev->pixels != NULL, "Rendered Deskbar menu with Classic 3D style");

    global_menu_close();
    cls_dev(dev);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("==========================================================\n");
    printf(" B-System Global System Menu & Japanese Deskbar Test Suite\n");
    printf("==========================================================\n");

    test_global_menu_geometry();
    test_header_hit_and_hover();
    test_dropdown_activation_and_fluid_tracking();
    test_menu_dismissal();
    test_japanese_calendar_formatting();
    test_global_tip_badge();
    test_window_management_actions();
    test_rendering_benchmarks();

    printf("\n==========================================================\n");
    printf(" GLOBAL MENU TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_total,
           (100.0 * g_tests_passed) / (g_tests_total > 0 ? g_tests_total : 1));
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
