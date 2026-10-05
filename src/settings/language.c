/*
 * B-System (BTRON 3.20) Language & IME Settings Application: language.c
 * Applet inside Settings Cabinet (環境設定キャビネット)
 * Configures default IME keyboard shortcuts & behaviors for EN / JP / TB.
 */

#include <btron/language_settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/event.h>
#include <btron/tip.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

static LanguageSettingsApp g_lang_settings;

static void paint_checkbox(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    if (checked) {
        drw_lin(dev, x + 3, y + 8, x + 6, y + 12);
        drw_lin(dev, x + 6, y + 12, x + 12, y + 4);
        drw_lin(dev, x + 3, y + 9, x + 6, y + 13);
        drw_lin(dev, x + 6, y + 13, x + 12, y + 5);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
}

static void paint_button(GDEV *dev, H x, H y, H w, H h, const char *label, BOOL pressed) {
    RECT btn = { x, y, x + w, y + h };
    fill_rec(dev, &btn, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
    drw_rec(dev, &btn);
    H tx = x + (w - (H)strlen(label) * 8) / 2;
    drw_tc_string(dev, tx > x ? tx : x + 4, y + 3, label, COLOR_BLACK, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
}

static void paint_language_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar: Settings window icon is ALWAYS 32x32 */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "language", 6, 4, 32, 32);
    char hdr_str[128];
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) - %s", "Language", "言語・文字", "TRON Code, TIP Mozc & Tibetan Wylie");
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);

    /* Section 1: Multilingual Mode Switching */
    RECT s1 = { 10, 46, dev->width - 10, 110 };
    fill_rec(dev, &s1, COLOR_WHITE);
    drw_rec(dev, &s1);
    drw_tc_string(dev, 16, 38, " [1. Language Switching Shortcuts] ", COLOR_NAVY, COLOR_WHITE);

    drw_tc_string(dev, 20, 52, "Cycle Mode Key (EN -> JP Hiragana -> JP Katakana -> TB):  F10", COLOR_BLACK, COLOR_WHITE);
    drw_tc_string(dev, 20, 70, "Direct Language Keys:  F6 (JP あ) | F7 (JP ア) | F8 (TB བོད) | F9 (Zen/Alphanum)", COLOR_GRAY, COLOR_WHITE);
    drw_tc_string(dev, 20, 88, "Standard Modifier Toggle:  Ctrl + Space / Alt + Space (Hankaku/Zenkaku)", COLOR_GRAY, COLOR_WHITE);

    /* Section 2: Japanese Mozc Keyboard Behavior */
    RECT s2 = { 10, 116, dev->width - 10, 186 };
    fill_rec(dev, &s2, COLOR_WHITE);
    drw_rec(dev, &s2);
    drw_tc_string(dev, 16, 108, " [2. Japanese Mode (JP あ / JP ア)] ", COLOR_NAVY, COLOR_WHITE);

    paint_checkbox(dev, 20, 122, "Space Key triggers Kana-to-Kanji (KKC) Lattice Search", g_lang_settings.current_config.jp_space_is_convert, FALSE);
    paint_checkbox(dev, 20, 142, "Tab Key opens Candidate Suggestion Popup", g_lang_settings.current_config.jp_tab_is_popup, FALSE);
    paint_checkbox(dev, 20, 162, "Fullwidth Space (　 U+3000) inserted when Space is pressed in IDLE", TRUE, FALSE);

    /* Section 3: Tibetan Wylie Keyboard Behavior */
    RECT s3 = { 10, 192, dev->width - 10, 262 };
    fill_rec(dev, &s3, COLOR_WHITE);
    drw_rec(dev, &s3);
    drw_tc_string(dev, 16, 184, " [3. Tibetan Mode (TB བོད)] ", COLOR_NAVY, COLOR_WHITE);

    paint_checkbox(dev, 20, 202, "Space Key directly outputs Tsheg ('་' U+0F0B) syllable delimiter", g_lang_settings.current_config.tb_space_is_tsheg, FALSE);
    paint_checkbox(dev, 20, 222, "Tab Key opens Dharma Dictionary Suggestion Popup", g_lang_settings.current_config.tb_tab_is_popup, FALSE);
    paint_checkbox(dev, 20, 242, "Shift + Space / Ctrl + Space opens Dictionary Popup", g_lang_settings.current_config.tb_shift_space_popup, FALSE);

    /* Section 4: Universal Candidate Popup Navigation */
    RECT s4 = { 10, 268, dev->width - 10, 335 };
    fill_rec(dev, &s4, COLOR_WHITE);
    drw_rec(dev, &s4);
    drw_tc_string(dev, 16, 260, " [4. Candidate Popup Navigation (All Modes)] ", COLOR_NAVY, COLOR_WHITE);

    paint_checkbox(dev, 20, 282, "Up / Down Arrow Keys navigate candidate list (Ctrl+P / Ctrl+N)", g_lang_settings.current_config.arrow_nav_enabled, FALSE);
    paint_checkbox(dev, 20, 302, "1-9 Numeric Keys select candidate item directly", g_lang_settings.current_config.num_select_enabled, FALSE);

    /* Footer Action Buttons */
    H btn_y = dev->height - 35;
    paint_button(dev, dev->width - 320, btn_y, 70, 24, "Default", FALSE);
    paint_button(dev, dev->width - 240, btn_y, 70, 24, "Revert", FALSE);
    paint_button(dev, dev->width - 160, btn_y, 70, 24, "Apply", g_lang_settings.is_dirty);
    paint_button(dev, dev->width - 80, btn_y, 70, 24, "Close", FALSE);
}

void language_settings_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        /* Checkbox Toggles */
        if (rel_x >= 20 && rel_x <= 450) {
            if (rel_y >= 122 && rel_y <= 136) {
                g_lang_settings.current_config.jp_space_is_convert = !g_lang_settings.current_config.jp_space_is_convert;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 142 && rel_y <= 156) {
                g_lang_settings.current_config.jp_tab_is_popup = !g_lang_settings.current_config.jp_tab_is_popup;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 202 && rel_y <= 216) {
                g_lang_settings.current_config.tb_space_is_tsheg = !g_lang_settings.current_config.tb_space_is_tsheg;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 222 && rel_y <= 236) {
                g_lang_settings.current_config.tb_tab_is_popup = !g_lang_settings.current_config.tb_tab_is_popup;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 242 && rel_y <= 256) {
                g_lang_settings.current_config.tb_shift_space_popup = !g_lang_settings.current_config.tb_shift_space_popup;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 282 && rel_y <= 296) {
                g_lang_settings.current_config.arrow_nav_enabled = !g_lang_settings.current_config.arrow_nav_enabled;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 302 && rel_y <= 316) {
                g_lang_settings.current_config.num_select_enabled = !g_lang_settings.current_config.num_select_enabled;
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
        }

        /* Action Buttons */
        H btn_y = client_h - 35;
        if (rel_y >= btn_y && rel_y <= btn_y + 24) {
            if (rel_x >= client_w - 320 && rel_x <= client_w - 250) {
                /* Default */
                tip_reset_default_key_settings();
                tip_get_key_settings(&g_lang_settings.current_config);
                g_lang_settings.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 240 && rel_x <= client_w - 170) {
                /* Revert */
                g_lang_settings.current_config = g_lang_settings.saved_config;
                g_lang_settings.is_dirty = FALSE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 160 && rel_x <= client_w - 90) {
                /* Apply */
                tip_set_key_settings(&g_lang_settings.current_config);
                g_lang_settings.saved_config = g_lang_settings.current_config;
                g_lang_settings.is_dirty = FALSE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
                /* Close */
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
