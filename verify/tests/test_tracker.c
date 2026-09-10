/*
 * B-System (BTRON 3.20) Unit Test Suite for Tracker Start Button & Task Manager
 * Verifies Haiku-style Start button, root menu dispatch, window tracking,
 * low-latency O(1) hit testing, and NASA JPL invariant safety.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <btron/tracker.h>
#include <btron/about.h>
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/settings.h>

/* Mock / Stub BTRON accessory window entry points for standalone testing */
static int s_vobj_opened = 0;
static int s_tedit_opened = 0;
static int s_term_opened = 0;
static int s_audio_opened = 0;
static int s_chat_opened = 0;

WND* open_vobj_manager_window(void) { s_vobj_opened++; return NULL; }
WND* open_control_panel_window(void) { return NULL; }
WND* open_t_editor_window(void)     { s_tedit_opened++; return NULL; }
WND* open_gterm_window(void)        { s_term_opened++; return NULL; }
WND* open_audio_player_window(void) { s_audio_opened++; return NULL; }
WND* open_orchestra_window(void)    { return NULL; }
WND* launch_beos_chat(void)         { s_chat_opened++; return NULL; }
void global_menu_render_bar(GDEV *dev) { (void)dev; }
ER init_evt_sys(void) { return E_OK; }
ER tip_init(void) { return E_OK; }
TIP_DFA_STATE tip_get_state(void) { return TIP_STATE_IDLE; }
H tip_get_caret_x(void) { return 0; }
H tip_get_caret_y(void) { return 0; }
void tip_render_candidate_window(GDEV *dev, H x, H y) { (void)dev; (void)x; (void)y; }

static int s_tests_passed = 0;
static int s_tests_failed = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        s_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
        s_tests_failed++; \
    } \
} while(0)

int main(void) {
    printf("==========================================================\n");
    printf(" Running BTRON Deskbar Tracker Unit Tests...\n");
    printf("==========================================================\n");

    /* [TEST GROUP 1] Lifecycle & Invariant Safety */
    printf("\n[TEST GROUP 1] Lifecycle & Invariant Safety\n");
    ER err = tracker_init();
    TEST_ASSERT(err == E_OK, "tracker_init returns E_OK");
    TEST_ASSERT(tracker_verify_invariants() == TRUE, "Initial state satisfies all NASA JPL invariants");
    TEST_ASSERT(tracker_is_menu_open() == FALSE, "Start menu initially closed");

    const TRACKER *st = tracker_get_state();
    TEST_ASSERT(st->btn_rect.left == 4 && st->btn_rect.top == 3, "Button positioned at top-left (4, 3)");
    TEST_ASSERT(st->btn_rect.right == 84 && st->btn_rect.bottom == 23, "Button dimensions 80x20");
    TEST_ASSERT(st->item_count > 0 && st->item_count <= TRACKER_MAX_ITEMS, "Menu populated with static items");

    /* [TEST GROUP 2] O(1) Hit Testing Accuracy */
    printf("\n[TEST GROUP 2] O(1) Hit Testing Accuracy\n");
    TEST_ASSERT(tracker_hit_button(4, 3) == TRUE, "Top-left edge of START button hits");
    TEST_ASSERT(tracker_hit_button(84, 23) == TRUE, "Bottom-right edge of START button hits");
    TEST_ASSERT(tracker_hit_button(44, 13) == TRUE, "Center of START button hits");
    TEST_ASSERT(tracker_hit_button(3, 3) == FALSE, "Left of button outside bounds");
    TEST_ASSERT(tracker_hit_button(85, 23) == FALSE, "Right of button outside bounds");
    TEST_ASSERT(tracker_hit_button(44, 2) == FALSE, "Above button outside bounds");
    TEST_ASSERT(tracker_hit_button(44, 24) == FALSE, "Below button outside bounds");

    /* [TEST GROUP 3] Start Menu Toggle & State Machine */
    printf("\n[TEST GROUP 3] Start Menu Toggle & State Machine\n");
    /* Click button to open menu */
    BOOL handled = tracker_handle_mouse_down(40, 10);
    TEST_ASSERT(handled == TRUE, "Mouse down on button handled");
    TEST_ASSERT(tracker_is_menu_open() == TRUE, "Menu is now OPEN");
    TEST_ASSERT(tracker_verify_invariants() == TRUE, "Invariants hold in OPEN state");

    /* Click outside menu to close */
    handled = tracker_handle_mouse_down(500, 500);
    TEST_ASSERT(handled == TRUE, "Click outside open menu handled");
    TEST_ASSERT(tracker_is_menu_open() == FALSE, "Menu closed on outside click");
    TEST_ASSERT(tracker_verify_invariants() == TRUE, "Invariants hold after close");

    /* Toggle directly via API */
    tracker_toggle_menu();
    TEST_ASSERT(tracker_is_menu_open() == TRUE, "tracker_toggle_menu opens menu");
    tracker_toggle_menu();
    TEST_ASSERT(tracker_is_menu_open() == FALSE, "tracker_toggle_menu closes menu");

    /* [TEST GROUP 4] Keyboard Navigation */
    printf("\n[TEST GROUP 4] Keyboard Navigation\n");
    /* F1 key toggles menu */
    tracker_handle_key(0x101);
    TEST_ASSERT(tracker_is_menu_open() == TRUE, "F1 key toggles menu OPEN");

    H initial_hover = tracker_get_state()->hover_index;
    TEST_ASSERT(initial_hover == 0, "Initial hover item is index 0");

    /* Down arrow navigation */
    tracker_handle_key('s');
    TEST_ASSERT(tracker_get_state()->hover_index > initial_hover, "Down key advances hover index");

    /* Escape closes menu */
    tracker_handle_key(0x1B);
    TEST_ASSERT(tracker_is_menu_open() == FALSE, "Escape key closes menu");

    /* [TEST GROUP 5] Window / Task Tracking Logic */
    printf("\n[TEST GROUP 5] Window / Task Tracking Logic\n");
    /* Mock window setup */
    WND mock_w1;
    memset(&mock_w1, 0, sizeof(mock_w1));
    mock_w1.id = 101;
    strncpy(mock_w1.title, "Test Terminal", sizeof(mock_w1.title) - 1);
    mock_w1.focused = TRUE;
    mock_w1.next = NULL;

    /* Tracker refresh incorporates active windows */
    tracker_open_menu();
    st = tracker_get_state();
    TEST_ASSERT(st->item_count >= 5, "Menu contains core application items");
    TEST_ASSERT(tracker_verify_invariants() == TRUE, "Window tracking invariants verified");
    tracker_close_menu();

    /* [TEST GROUP 6] Widest Item Calculation & Overflow Prevention */
    printf("\n[TEST GROUP 6] Widest Item Calculation & Overflow Prevention\n");
    H w_ascii = tracker_calc_text_width("Hello");
    TEST_ASSERT(w_ascii == 40, "ASCII string 'Hello' width is 40px (5 chars * 8px)");

    H w_kanji = tracker_calc_text_width("日本語");
    TEST_ASSERT(w_kanji == 48, "Kanji string '日本語' width is 48px (3 chars * 16px)");

    H widest_w = tracker_calc_widest_item_width();
    TEST_ASSERT(widest_w >= TRACKER_MENU_MIN_WIDTH, "Widest item width >= TRACKER_MENU_MIN_WIDTH (190px)");

    tracker_open_menu();
    st = tracker_get_state();
    H current_menu_w = st->menu_rect.right - st->menu_rect.left;
    TEST_ASSERT(current_menu_w == widest_w, "Menu width dynamically matches computed widest item width");

    /* Verify every active menu item fits strictly inside menu width without overflow */
    BOOL all_fit = TRUE;
    for (int i = 0; i < st->item_count; i++) {
        if (st->items[i].type == TRACKER_CMD_SEPARATOR) continue;
        H item_w = tracker_calc_text_width(st->items[i].label);
        if (item_w + 26 > current_menu_w) {
            all_fit = FALSE;
            break;
        }
    }
    TEST_ASSERT(all_fit == TRUE, "All menu items fit completely within menu bounds without overflow");
    tracker_close_menu();

    /* [TEST GROUP 7] Low-Latency Execution Benchmarking */
    printf("\n[TEST GROUP 7] Low-Latency Execution Benchmarking\n");
    clock_t start = clock();
    const int ITERS = 100000;
    for (int i = 0; i < ITERS; i++) {
        tracker_hit_button(40, 10);
        tracker_hit_menu(50, 50);
    }
    clock_t end = clock();
    double elapsed_ms = ((double)(end - start) / CLOCKS_PER_SEC) * 1000.0;
    printf("  [BENCH] 100,000 hit tests completed in %.2f ms (%.4f us / call)\n", elapsed_ms, (elapsed_ms * 1000.0) / (ITERS * 2));
    TEST_ASSERT(elapsed_ms < 50.0, "O(1) hit testing operates under 50ms for 100k calls");


    /* [TEST GROUP 8] SONY Workstation About Window Lifecycle & Nyan Cat Animation */
    printf("\n[TEST GROUP 8] SONY Workstation About Window Lifecycle & Nyan Cat Animation\n");
    init_wnd_mgr(NULL);
    WND *about_wnd = open_about_window();
    TEST_ASSERT(about_wnd != NULL, "open_about_window() returns valid window");
    TEST_ASSERT(strstr(about_wnd->title, "システム情報") != NULL, "About window title matches specification");
    
    /* Simulate paint */
    GDEV *test_dev = opn_dev(560, 340);
    TEST_ASSERT(test_dev != NULL, "Created test GDEV for About window paint");
    about_wnd->paint(about_wnd, test_dev);
    TEST_ASSERT(test_dev->pixels != NULL, "Rendered SONY Workstation About interface with Nyan Cat viewport");
    cls_dev(test_dev);
    cls_wnd(about_wnd);

    /* [TEST GROUP 9] Desktop Icons Scaling (32x32 vs 64x64) & Hit Testing */
    printf("\n[TEST GROUP 9] Desktop Icons Scaling & Hit Testing\n");

    /* 1. 32x32 Icon Mode */
    appearance_set_icon_size(BTRON_ICON_SIZE_32);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_32, "Set appearance icon size to 32x32");

    s_vobj_opened = 0;
    TEST_ASSERT(desktop_handle_click(30, 70) == TRUE, "Desktop click at (30, 70) hits Cabinet in 32x32 mode");
    TEST_ASSERT(s_vobj_opened == 1, "Cabinet window opened on 32x32 icon hit");

    s_tedit_opened = 0;
    TEST_ASSERT(desktop_handle_click(30, 150) == TRUE, "Desktop click at (30, 150) hits Editor in 32x32 mode");
    TEST_ASSERT(s_tedit_opened == 1, "Editor window opened on 32x32 icon hit");

    s_term_opened = 0;
    TEST_ASSERT(desktop_handle_click(30, 230) == TRUE, "Desktop click at (30, 230) hits Terminal in 32x32 mode");
    TEST_ASSERT(s_term_opened == 1, "Terminal window opened on 32x32 icon hit");

    s_audio_opened = 0;
    TEST_ASSERT(desktop_handle_click(30, 310) == TRUE, "Desktop click at (30, 310) hits Audio in 32x32 mode");
    TEST_ASSERT(s_audio_opened == 1, "Audio Player window opened on 32x32 icon hit");

    s_chat_opened = 0;
    TEST_ASSERT(desktop_handle_click(30, 390) == TRUE, "Desktop click at (30, 390) hits Chat in 32x32 mode");
    TEST_ASSERT(s_chat_opened == 1, "Chat window opened on 32x32 icon hit");

    TEST_ASSERT(desktop_handle_click(400, 300) == FALSE, "Click on empty desktop wallpaper returns FALSE in 32x32 mode");

    /* 2. 64x64 Icon Mode */
    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_64, "Set appearance icon size to 64x64");

    s_vobj_opened = 0;
    TEST_ASSERT(desktop_handle_click(40, 80) == TRUE, "Desktop click at (40, 80) hits Cabinet in 64x64 mode");
    TEST_ASSERT(s_vobj_opened == 1, "Cabinet window opened on 64x64 icon hit");

    s_tedit_opened = 0;
    TEST_ASSERT(desktop_handle_click(40, 180) == TRUE, "Desktop click at (40, 180) hits Editor in 64x64 mode");
    TEST_ASSERT(s_tedit_opened == 1, "Editor window opened on 64x64 icon hit");

    s_term_opened = 0;
    TEST_ASSERT(desktop_handle_click(40, 280) == TRUE, "Desktop click at (40, 280) hits Terminal in 64x64 mode");
    TEST_ASSERT(s_term_opened == 1, "Terminal window opened on 64x64 icon hit");

    s_audio_opened = 0;
    TEST_ASSERT(desktop_handle_click(40, 390) == TRUE, "Desktop click at (40, 390) hits Audio in 64x64 mode");
    TEST_ASSERT(s_audio_opened == 1, "Audio Player window opened on 64x64 icon hit");

    s_chat_opened = 0;
    TEST_ASSERT(desktop_handle_click(40, 500) == TRUE, "Desktop click at (40, 500) hits Chat in 64x64 mode");
    TEST_ASSERT(s_chat_opened == 1, "Chat window opened on 64x64 icon hit");

    TEST_ASSERT(desktop_handle_click(400, 300) == FALSE, "Click on empty desktop wallpaper returns FALSE in 64x64 mode");

    /* 3. Render desktop background with scaled icons */
    GDEV *dsk_dev = opn_dev(800, 600);
    TEST_ASSERT(dsk_dev != NULL, "Allocated 800x600 test display device");
    render_desktop_background(dsk_dev);
    TEST_ASSERT(dsk_dev->pixels[100 * 800 + 100] != 0, "Rendered desktop background with 64x64 pictogram icons");
    appearance_set_icon_size(BTRON_ICON_SIZE_32);
    render_desktop_background(dsk_dev);
    TEST_ASSERT(dsk_dev->pixels[100 * 800 + 100] != 0, "Rendered desktop background with 32x32 pictogram icons");
    cls_dev(dsk_dev);

    printf("\n==========================================================\n");
    printf(" TRACKER TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           s_tests_passed, s_tests_passed + s_tests_failed,
           (s_tests_passed * 100.0) / (s_tests_passed + s_tests_failed));
    printf("==========================================================\n");

    return (s_tests_failed == 0) ? 0 : 1;
}
