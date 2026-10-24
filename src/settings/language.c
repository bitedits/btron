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
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Space Key triggers Kana-to-Kanji (KKC) Lattice Search", &g_lang_settings.current_config.jp_space_is_convert, NULL, &g_lang_settings.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Tab Key opens Candidate Suggestion Popup", &g_lang_settings.current_config.jp_tab_is_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Space Key directly outputs Tsheg ('་' U+0F0B) syllable delimiter", &g_lang_settings.current_config.tb_space_is_tsheg, NULL, &g_lang_settings.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Tab Key opens Dharma Dictionary Suggestion Popup", &g_lang_settings.current_config.tb_tab_is_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Shift + Space / Ctrl + Space opens Dictionary Popup", &g_lang_settings.current_config.tb_shift_space_popup, NULL, &g_lang_settings.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Up / Down Arrow Keys navigate candidate list (Ctrl+P / Ctrl+N)", &g_lang_settings.current_config.arrow_nav_enabled, NULL, &g_lang_settings.is_dirty, NULL },
    { 6, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "1-9 Numeric Keys select candidate item directly", &g_lang_settings.current_config.num_select_enabled, NULL, &g_lang_settings.is_dirty, NULL },
};

static SETTINGS_SECTION s_language_sections[] = {
    { " [1. Language Switching Shortcuts] ", 0, 0, 0, 46, 64, {0}, 0, 0, 0 },
    { " [2. Japanese Mode (JP あ / JP ア)] ", 1, 0, 2, 116, 70, {0}, 6, 14, 6 },
    { " [3. Tibetan Mode (TB བོད)] ", 1, 2, 3, 192, 70, {0}, 10, 14, 6 },
    { " [4. Candidate Popup Navigation (All Modes)] ", 1, 5, 2, 268, 67, {0}, 14, 14, 6 },
};

static void paint_language_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    settings_draw_dialog(dev, "language", "Language", "言語・文字", "TRON Code, TIP Mozc & Tibetan Wylie",
                         s_language_sections, 4,
                         s_language_controls, 7,
                         g_lang_settings.is_dirty);

    /* Section 1 informational text */
    H left_x = s_language_sections[0].rect.left + 10;
    drw_tc_string(dev, left_x, 52, "Cycle Mode Key (EN -> JP Hiragana -> JP Katakana -> TB):  F10", COLOR_BLACK, COLOR_WHITE);
    drw_tc_string(dev, left_x, 70, "Direct Language Keys:  F6 (JP あ) | F7 (JP ア) | F8 (TB བོད) | F9 (Zen/Alphanum)", COLOR_GRAY, COLOR_WHITE);
    drw_tc_string(dev, left_x, 88, "Standard Modifier Toggle:  Ctrl + Space / Alt + Space (Hankaku/Zenkaku)", COLOR_GRAY, COLOR_WHITE);

    /* Section 2 informational note */
    settings_draw_ui_checkbox(dev, s_language_sections[1].rect.left + 8, 162,
                              "Fullwidth Space (　 U+3000) inserted when Space is pressed in IDLE", TRUE, FALSE);
}

void language_settings_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd,
                                   s_language_sections, 4,
                                   s_language_controls, 7,
                                   evt, on_language_default, on_language_apply);
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
