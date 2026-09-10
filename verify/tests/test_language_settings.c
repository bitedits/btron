#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/tip.h>
#include <btron/settings.h>
#include <btron/language_settings.h>
#include <btron/event.h>

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do {     g_tests_total++;     if (cond) {         printf("  [PASS] %s\n", msg);         g_tests_passed++;     } else {         printf("  [FAIL] %s (Line %d: %s)\n", msg, __LINE__, #cond);     } } while(0)

int main(void) {
    printf("==========================================================\n");
    printf(" Running Settings Cabinet & Control Panel Test Suite\n");
    printf("==========================================================\n");

    /* 1. Default Key Settings Verification */
    printf("\n[TEST GROUP 1] Default TIP Key Settings\n");
    tip_reset_default_key_settings();
    TIP_KEY_SETTINGS s;
    tip_get_key_settings(&s);
    TEST_ASSERT(s.mode_toggle_key == BTRON_KEY_F10, "Default mode toggle key is F10");
    TEST_ASSERT(s.direct_jp_hira_key == BTRON_KEY_F6, "Default direct Hiragana key is F6");
    TEST_ASSERT(s.direct_jp_kata_key == BTRON_KEY_F7, "Default direct Katakana key is F7");
    TEST_ASSERT(s.direct_tb_key == BTRON_KEY_F8, "Default direct Tibetan key is F8");
    TEST_ASSERT(s.jp_space_is_convert == TRUE, "Default JP Space is Kanji conversion");
    TEST_ASSERT(s.jp_tab_is_popup == TRUE, "Default JP Tab is candidate popup");
    TEST_ASSERT(s.tb_space_is_tsheg == TRUE, "Default TB Space is Tsheg delimiter");
    TEST_ASSERT(s.tb_tab_is_popup == TRUE, "Default TB Tab is dictionary popup");
    TEST_ASSERT(s.arrow_nav_enabled == TRUE, "Default Arrow navigation is enabled");
    TEST_ASSERT(s.num_select_enabled == TRUE, "Default 1-9 numeric selection is enabled");

    /* 2. Modify & Apply Dynamic Key Settings */
    printf("\n[TEST GROUP 2] Dynamic Key Settings Modification\n");
    s.tb_space_is_tsheg = FALSE;
    s.mode_toggle_key = 0x200;
    tip_set_key_settings(&s);

    TIP_KEY_SETTINGS s2;
    tip_get_key_settings(&s2);
    TEST_ASSERT(s2.tb_space_is_tsheg == FALSE, "Modified tb_space_is_tsheg to FALSE");
    TEST_ASSERT(s2.mode_toggle_key == 0x200, "Modified mode_toggle_key to 0x200");

    /* 3. Settings Cabinet Window Lifecycle */
    printf("\n[TEST GROUP 3] Language Settings Window Lifecycle\n");
    init_wnd_mgr(NULL);
    WND *wnd_lang = open_language_settings_window();
    TEST_ASSERT(wnd_lang != NULL, "open_language_settings_window() successfully created window");
    TEST_ASSERT(strstr(wnd_lang->title, "Language") != NULL || strstr(wnd_lang->title, "言語") != NULL, "Window title contains 'Language' or '言語'");
    cls_wnd(wnd_lang);

    /* 4. Control Panel Applet Registry Verification */
    printf("\n[TEST GROUP 4] Control Panel Applet Registry\n");
    int count = settings_get_app_count();
    TEST_ASSERT(count == 11, "Control panel registers exactly 11 settings applets");

    for (int i = 1; i <= count; i++) {
        const SETTINGS_APP_INFO *info = settings_get_app_info((SETTINGS_APP_ID)i);
        TEST_ASSERT(info != NULL, "App info is non-null");
        TEST_ASSERT(info->open_func != NULL, "Applet has valid open_func");
        printf("    -> Registered Applet %d: %s (%s) - %s\n", i, info->title, info->title_ja, info->desc);
    }

    /* 5. Open & Validate All 10 Settings Applets */
    printf("\n[TEST GROUP 5] Instantiating All 10 Settings Applets\n");
    WND *w;

    w = open_control_panel_window();
    TEST_ASSERT(w != NULL && (strstr(w->title, "Control Panel") != NULL || strstr(w->title, "コントロールパネル") != NULL || strstr(w->title, "Preferences Panel") != NULL), "Control Panel Hub opened");
    cls_wnd(w);

    w = open_appearance_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Appearance") != NULL, "Appearance Settings opened");
    cls_wnd(w);

    w = open_desktop_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Desktop") != NULL, "Desktop Settings opened");
    cls_wnd(w);

    w = open_display_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Display") != NULL, "Display Settings opened");
    cls_wnd(w);

    w = open_input_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Input") != NULL, "Input Settings opened");
    cls_wnd(w);

    w = open_sound_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Sound") != NULL, "Sound Settings opened");
    cls_wnd(w);

    w = open_network_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Network") != NULL, "Network Settings opened");
    cls_wnd(w);

    w = open_media_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Media") != NULL, "Media Settings opened");
    cls_wnd(w);

    w = open_security_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "Security") != NULL, "Security Settings opened");
    cls_wnd(w);

    w = open_system_settings_window();
    TEST_ASSERT(w != NULL && strstr(w->title, "System") != NULL, "System Settings opened");
    cls_wnd(w);

    /* 6. Relative Mouse Coordinate Interaction Tests (Window Position Offsets) */
    printf("\n[TEST GROUP 6] Relative Mouse Coordinates at Shifted Window Positions\n");
    w = open_control_panel_window();
    TEST_ASSERT(w != NULL, "Control Panel opened for coordinate tests");
    /* Move window to bottom-right offset */
    mov_wnd(w, 400, 200);

    /* Click on second grid item: Desktop (col=1, row=0) */
    H client_w = w->client.right - w->client.left;
    H item_w = (client_w - 48) / 2;
    H item1_rel_x = 16 + 1 * (item_w + 16) + 10;
    H item1_rel_y = 60 + 10;

    EVT click_evt;
    memset(&click_evt, 0, sizeof(EVT));
    click_evt.type = EV_BUT_DOWN;
    click_evt.pos.x = w->client.left + item1_rel_x;
    click_evt.pos.y = w->client.top + item1_rel_y;

    w->event_handler(w, &click_evt);
    TEST_ASSERT(get_top_wnd() != NULL, "Event dispatched relative to shifted window");
    cls_wnd(w);

    /* Test Language Settings checkbox click when window is moved */
    tip_reset_default_key_settings();
    wnd_lang = open_language_settings_window();
    mov_wnd(wnd_lang, 350, 180);

    tip_get_key_settings(&s);
    BOOL orig_space_conv = s.jp_space_is_convert;

    memset(&click_evt, 0, sizeof(EVT));
    click_evt.type = EV_BUT_DOWN;
    click_evt.pos.x = wnd_lang->client.left + 25;
    click_evt.pos.y = wnd_lang->client.top + 125; /* JP Space checkbox toggle */

    wnd_lang->event_handler(wnd_lang, &click_evt);

    /* Click Apply button */
    H l_w = wnd_lang->client.right - wnd_lang->client.left;
    H l_h = wnd_lang->client.bottom - wnd_lang->client.top;
    click_evt.pos.x = wnd_lang->client.left + l_w - 120;
    click_evt.pos.y = wnd_lang->client.top + l_h - 25;
    wnd_lang->event_handler(wnd_lang, &click_evt);

    tip_get_key_settings(&s2);
    TEST_ASSERT(s2.jp_space_is_convert != orig_space_conv, "Checkbox clicked and applied via relative coordinates");
    cls_wnd(wnd_lang);

    /* [TEST GROUP 7] Icon Size Preferences (32x32 vs 64x64) and Settings Windows */
    printf("\n[TEST GROUP 7] Icon Size Preferences and Window Headers\n");

    appearance_set_icon_size(BTRON_ICON_SIZE_32);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_32, "Icon size set to 32x32");

    WND *w_cp32 = open_control_panel_window();
    TEST_ASSERT(w_cp32 != NULL, "Control Panel opened with 32x32 icon setting");
    TEST_ASSERT((w_cp32->bounds.bottom - w_cp32->bounds.top) == 520, "Control Panel height is 520 in 32x32 mode");
    cls_wnd(w_cp32);

    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_64, "Icon size set to 64x64");

    WND *w_cp64 = open_control_panel_window();
    TEST_ASSERT(w_cp64 != NULL, "Control Panel opened with 64x64 icon setting");
    TEST_ASSERT((w_cp64->bounds.bottom - w_cp64->bounds.top) == 660, "Control Panel height is 660 in 64x64 mode");
    cls_wnd(w_cp64);

    /* Test Appearance setting radio button clicks */
    WND *w_app = open_appearance_settings_window();
    TEST_ASSERT(w_app != NULL, "Appearance settings window opened");

    memset(&click_evt, 0, sizeof(EVT));
    click_evt.type = EV_BUT_DOWN;
    click_evt.pos.x = w_app->client.left + 30;
    click_evt.pos.y = w_app->client.top + 185; /* 32x32 radio */
    w_app->event_handler(w_app, &click_evt);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_32, "Clicking 32x32 radio button switches size to 32");

    click_evt.pos.y = w_app->client.top + 205; /* 64x64 radio */
    w_app->event_handler(w_app, &click_evt);
    TEST_ASSERT(appearance_get_icon_size() == BTRON_ICON_SIZE_64, "Clicking 64x64 radio button switches size to 64");
    cls_wnd(w_app);

    /* Test rendering a settings window with 32x32 icon header */
    WND *w_dsk = open_desktop_settings_window();
    TEST_ASSERT(w_dsk != NULL, "Desktop settings window opened with 32x32 icon header");
    COLOR pix[640 * 360];
    GDEV dev_test = {
        .pixels = pix,
        .width = 640,
        .height = 360,
        .clip = { 0, 0, 640, 360 }
    };
    w_dsk->paint(w_dsk, &dev_test);
    TEST_ASSERT(dev_test.pixels[10 * 640 + 10] != 0, "Desktop settings painted surface with 32x32 header");
    cls_wnd(w_dsk);

    printf("\n==========================================================\n");
    printf(" SETTINGS TEST RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_total,
           (100.0 * g_tests_passed) / (g_tests_total > 0 ? g_tests_total : 1));
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
