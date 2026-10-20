/*
 * B-System (BTRON 3.20) Language & IME Settings Application: language.c
 * Applet inside Settings Cabinet (環境設定キャビネット)
 * Configures default IME keyboard shortcuts & behaviors for EN / JP / TB.
 */

#include <btron/settings.h>
#include <btron/language_settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

static LanguageSettingsApp g_lang_settings;

static void on_language_default(WND *wnd) {
    (void)wnd;
    tip_reset_default_key_settings();
    tip_get_key_settings(&g_lang_settings.current_config);
    g_lang_settings.is_dirty = TRUE;
}

static void on_language_revert(WND *wnd) {
    (void)wnd;
    g_lang_settings.current_config = g_lang_settings.saved_config;
    g_lang_settings.is_dirty = FALSE;
}

static void on_language_apply(WND *wnd) {
    (void)wnd;
    tip_set_key_settings(&g_lang_settings.current_config);
    g_lang_settings.saved_config = g_lang_settings.current_config;
    g_lang_settings.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_language_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 122, 450, 136 }, "Space Key triggers Kana-to-Kanji (KKC) Lattice Search", &g_lang_settings.current_config.jp_space_is_convert, NULL, &g_lang_settings.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 142, 450, 156 }, "Tab Key opens Candidate Suggestion Popup", &g_lang_settings.current_config.jp_tab_is_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 202, 450, 216 }, "Space Key directly outputs Tsheg ('་' U+0F0B) syllable delimiter", &g_lang_settings.current_config.tb_space_is_tsheg, NULL, &g_lang_settings.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 222, 450, 236 }, "Tab Key opens Dharma Dictionary Suggestion Popup", &g_lang_settings.current_config.tb_tab_is_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 242, 450, 256 }, "Shift + Space / Ctrl + Space opens Dictionary Popup", &g_lang_settings.current_config.tb_shift_space_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 282, 450, 296 }, "Up / Down Arrow Keys navigate candidate list (Ctrl+P / Ctrl+N)", &g_lang_settings.current_config.arrow_nav_enabled, NULL, &g_lang_settings.is_dirty, NULL },
    { 6, SETTINGS_CTL_CHECKBOX, 0, 0, { 20, 302, 450, 316 }, "1-9 Numeric Keys select candidate item directly", &g_lang_settings.current_config.num_select_enabled, NULL, &g_lang_settings.is_dirty, NULL },
};

static void paint_language_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "language", "Language", "言語・文字", "TRON Code, TIP Mozc & Tibetan Wylie");

    /* Section 1: Multilingual Mode Switching */
    RECT s1 = { 10, 46, dev->width - 10, 110 };
    settings_draw_section(dev, &s1, " [1. Language Switching Shortcuts] ");
    drw_tc_string(dev, 20, 52, "Cycle Mode Key (EN -> JP Hiragana -> JP Katakana -> TB):  F10", COLOR_BLACK, COLOR_WHITE);
    drw_tc_string(dev, 20, 70, "Direct Language Keys:  F6 (JP あ) | F7 (JP ア) | F8 (TB བོད) | F9 (Zen/Alphanum)", COLOR_GRAY, COLOR_WHITE);
    drw_tc_string(dev, 20, 88, "Standard Modifier Toggle:  Ctrl + Space / Alt + Space (Hankaku/Zenkaku)", COLOR_GRAY, COLOR_WHITE);

    /* Section 2: Japanese Mozc Keyboard Behavior */
    RECT s2 = { 10, 116, dev->width - 10, 186 };
    settings_draw_section(dev, &s2, " [2. Japanese Mode (JP あ / JP ア)] ");
    settings_draw_ui_checkbox(dev, 20, 122, "Space Key triggers Kana-to-Kanji (KKC) Lattice Search", g_lang_settings.current_config.jp_space_is_convert, FALSE);
    settings_draw_ui_checkbox(dev, 20, 142, "Tab Key opens Candidate Suggestion Popup", g_lang_settings.current_config.jp_tab_is_popup, FALSE);
    settings_draw_ui_checkbox(dev, 20, 162, "Fullwidth Space (　 U+3000) inserted when Space is pressed in IDLE", TRUE, FALSE);

    /* Section 3: Tibetan Wylie Keyboard Behavior */
    RECT s3 = { 10, 192, dev->width - 10, 262 };
    settings_draw_section(dev, &s3, " [3. Tibetan Mode (TB བོད)] ");
    settings_draw_ui_checkbox(dev, 20, 202, "Space Key directly outputs Tsheg ('་' U+0F0B) syllable delimiter", g_lang_settings.current_config.tb_space_is_tsheg, FALSE);
    settings_draw_ui_checkbox(dev, 20, 222, "Tab Key opens Dharma Dictionary Suggestion Popup", g_lang_settings.current_config.tb_tab_is_popup, FALSE);
    settings_draw_ui_checkbox(dev, 20, 242, "Shift + Space / Ctrl + Space opens Dictionary Popup", g_lang_settings.current_config.tb_shift_space_popup, FALSE);

    /* Section 4: Universal Candidate Popup Navigation */
    RECT s4 = { 10, 268, dev->width - 10, 335 };
    settings_draw_section(dev, &s4, " [4. Candidate Popup Navigation (All Modes)] ");
    settings_draw_ui_checkbox(dev, 20, 282, "Up / Down Arrow Keys navigate candidate list (Ctrl+P / Ctrl+N)", g_lang_settings.current_config.arrow_nav_enabled, FALSE);
    settings_draw_ui_checkbox(dev, 20, 302, "1-9 Numeric Keys select candidate item directly", g_lang_settings.current_config.num_select_enabled, FALSE);

    /* Footer Action Buttons */
    H btn_y = dev->height - 35;
    settings_draw_ui_button(dev, dev->width - 320, btn_y, 70, 24, "Default", FALSE);
    settings_draw_ui_button(dev, dev->width - 240, btn_y, 70, 24, "Revert", FALSE);
    settings_draw_ui_button(dev, dev->width - 160, btn_y, 70, 24, "Apply", g_lang_settings.is_dirty);
    settings_draw_ui_button(dev, dev->width - 80, btn_y, 70, 24, "Close", FALSE);
}

void language_settings_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        if (settings_dispatch_control_event(wnd, s_language_controls, 7, evt)) return;

        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        /* Action Buttons */
        H btn_y = client_h - 35;
        if (rel_y >= btn_y && rel_y <= btn_y + 24) {
            if (rel_x >= client_w - 320 && rel_x <= client_w - 250) {
                on_language_default(wnd);
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 240 && rel_x <= client_w - 170) {
                on_language_revert(wnd);
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 160 && rel_x <= client_w - 90) {
                on_language_apply(wnd);
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
                if (g_lang_settings.is_dirty) {
                    tip_set_key_settings(&g_lang_settings.current_config);
                }
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_language_settings_window(void) {
    memset(&g_lang_settings, 0, sizeof(LanguageSettingsApp));
    tip_get_key_settings(&g_lang_settings.current_config);
    g_lang_settings.saved_config = g_lang_settings.current_config;

    WND *wnd = opn_wnd("BTRONの言語と入力方法の設定 (Language and IME Settings)",
                       80, 50, 640, 420,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;

    g_lang_settings.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_lang_settings;
    wnd->paint = paint_language_settings;
    wnd->event_handler = language_settings_event_handler;
    return wnd;
}
