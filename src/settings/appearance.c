/*
 * B-System (BTRON 3.20) Settings Applet: appearance (Appearance / 外観)
 * Conforming to ./b-system/settings/Appearance.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>
#include <btron/app_menu.h>
#include "../window/pmc.h"

#include <btron/libc_shim.h>

/* Global Icon Display Size Preference: Defaults to 64x64 */
static BTRON_ICON_SIZE g_global_icon_size = BTRON_ICON_SIZE_64;

BTRON_ICON_SIZE appearance_get_icon_size(void) {
    return g_global_icon_size;
}

void appearance_set_icon_size(BTRON_ICON_SIZE size) {
    if (size == BTRON_ICON_SIZE_32 || size == BTRON_ICON_SIZE_64) {
        g_global_icon_size = size;
    }
}

typedef struct {
    WND *wnd;
    BOOL checks[11];
    BOOL is_dirty;
} AppletState_appearance;

static AppletState_appearance g_state_appearance;

/* 3D Crisp Graphical Checkbox */
static void paint_ui_checkbox(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    /* Sunken 3D shadow lines */
    drw_lin(dev, x + 1, y + 2, x + 14, y + 2);
    drw_lin(dev, x + 1, y + 2, x + 1, y + 15);

    if (checked) {
        /* Bold checkmark [✔] */
        drw_lin(dev, x + 3, y + 8, x + 6, y + 12);
        drw_lin(dev, x + 3, y + 9, x + 6, y + 13);
        drw_lin(dev, x + 4, y + 8, x + 7, y + 12);

        drw_lin(dev, x + 6, y + 12, x + 12, y + 4);
        drw_lin(dev, x + 6, y + 13, x + 12, y + 5);
        drw_lin(dev, x + 7, y + 12, x + 13, y + 4);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
}

/* 3D Crisp Graphical Radio Button */
static void paint_ui_radio(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    if (checked) {
        RECT dot = { x + 4, y + 5, x + 11, y + 12 };
        fill_rec(dev, &dot, COLOR_NAVY);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
}

/* 3D Push Button */
static void paint_ui_button(GDEV *dev, H x, H y, H w, H h, const char *label, BOOL pressed) {
    RECT btn = { x, y, x + w, y + h };
    fill_rec(dev, &btn, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
    drw_rec(dev, &btn);

    if (!pressed) {
        drw_lin(dev, x + 1, y + 1, x + w - 2, y + 1);
        drw_lin(dev, x + 1, y + 1, x + 1, y + h - 2);
    }

    H tx = x + (w - (H)strlen(label) * 8) / 2;
    drw_tc_string(dev, tx > x ? tx : x + 4, y + 4, label, COLOR_BLACK, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
}

static void paint_appearance_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar: Settings window icon is ALWAYS 32x32 */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "appearance", 6, 4, 32, 32);
    char hdr_str[128];
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) - %s", "Appearance", "外観", "Themes, colours & window styles");
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);

    /* Section 1: System Themes & Visual Appearance */
    RECT s1 = { 10, 56, dev->width - 10, 154 };
    fill_rec(dev, &s1, COLOR_WHITE);
    drw_rec(dev, &s1);
    drw_tc_string(dev, 16, 48, " [1. Window Manager Frame & Theme Appearance] ", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 18, 64, "Window Style: BeOS Sliding Gold Tab (BeOSスタイル)", g_wm_style == WM_STYLE_BEOS, FALSE);
    paint_ui_radio(dev, 18, 86, "Window Style: Cho-Kanji 3D Bevel (超漢字・PMCスタイル)", g_wm_style == WM_STYLE_CHOKANJI, FALSE);
    paint_ui_radio(dev, 18, 108, "Dark Navy High-Contrast Palette (Chapter 7)", g_state_appearance.checks[1], FALSE);
    paint_ui_checkbox(dev, 18, 130, "Enable 3D Double Bezel Window Frame Shadows", g_state_appearance.checks[3], FALSE);

    /* Section 2: Icon Size & Menu Display Styles */
    RECT s2 = { 10, 170, dev->width - 10, 276 };
    fill_rec(dev, &s2, COLOR_WHITE);
    drw_rec(dev, &s2);
    drw_tc_string(dev, 16, 162, " [2. Icon Size & Menu Display Styles] ", COLOR_NAVY, COLOR_WHITE);

    /* Icon Size Radio Buttons */
    BOOL is_size_32 = (g_global_icon_size == BTRON_ICON_SIZE_32);
    paint_ui_radio(dev, 18, 178, "Icon Display Size: Standard 32×32 (標準 32×32 アイコン)", is_size_32, FALSE);
    paint_ui_radio(dev, 18, 200, "Icon Display Size: Large 64×64 (大 64×64 高解像度アイコン)", !is_size_32, FALSE);

    /* Menu Style Radio Buttons */
    BOOL is_classic = (app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D);
    paint_ui_radio(dev, 18, 226, "Menu Style: Classic Chokanji 3D (超漢字3Dベベル)", is_classic, FALSE);
    paint_ui_radio(dev, 18, 248, "Menu Style: Modern Flat Card (現代風カード)", !is_classic, FALSE);

    /* Section 3: Typography & Glyph Rendering */
    RECT s3 = { 10, 292, dev->width - 10, 372 };
    fill_rec(dev, &s3, COLOR_WHITE);
    drw_rec(dev, &s3);
    drw_tc_string(dev, 16, 284, " [3. Typography & Glyph Rendering] ", COLOR_NAVY, COLOR_WHITE);
    paint_ui_checkbox(dev, 18, 300, "Enable TRON Multilingual 16px Vector/Bitmap Engine", g_state_appearance.checks[4], FALSE);
    paint_ui_checkbox(dev, 18, 322, "Load Classical Tibetan Jomolhari Unicode Plane", g_state_appearance.checks[5], FALSE);
    paint_ui_checkbox(dev, 18, 344, "Enable Subpixel Vector Font Anti-Aliasing", g_state_appearance.checks[6], FALSE);

    /* Action Buttons */
    H btn_y = dev->height - 35;
    paint_ui_button(dev, dev->width - 240, btn_y, 70, 24, "Default", FALSE);
    paint_ui_button(dev, dev->width - 160, btn_y, 70, 24, "Apply", g_state_appearance.is_dirty);
    paint_ui_button(dev, dev->width - 80, btn_y, 70, 24, "Close", FALSE);
}

static void handle_appearance_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        /* Section 1: Themes & Bezel */
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 64 && rel_y <= 82) {
            pmc_set_style(WM_STYLE_BEOS);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 86 && rel_y <= 104) {
            pmc_set_style(WM_STYLE_CHOKANJI);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 108 && rel_y <= 126) {
            g_state_appearance.checks[2] = !g_state_appearance.checks[2];
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 130 && rel_y <= 148) {
            g_state_appearance.checks[3] = !g_state_appearance.checks[3];
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }

        /* Section 2: Icon Size Options */
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 178 && rel_y <= 196) {
            appearance_set_icon_size(BTRON_ICON_SIZE_32);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 200 && rel_y <= 218) {
            appearance_set_icon_size(BTRON_ICON_SIZE_64);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }

        /* Section 2: Menu Style Options */
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 226 && rel_y <= 244) {
            app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 248 && rel_y <= 266) {
            app_menu_set_global_style(APP_MENU_STYLE_MODERN_CARD);
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }

        /* Section 3: Typography */
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 300 && rel_y <= 318) {
            g_state_appearance.checks[4] = !g_state_appearance.checks[4];
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 322 && rel_y <= 340) {
            g_state_appearance.checks[5] = !g_state_appearance.checks[5];
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 344 && rel_y <= 362) {
            g_state_appearance.checks[6] = !g_state_appearance.checks[6];
            g_state_appearance.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }

        /* Action Buttons */
        H btn_y = client_h - 35;
        if (rel_y >= btn_y && rel_y <= btn_y + 24) {
            if (rel_x >= client_w - 240 && rel_x <= client_w - 170) {
                /* Default */
                for (int i = 0; i < 7; i++) g_state_appearance.checks[i] = TRUE;
                appearance_set_icon_size(BTRON_ICON_SIZE_64);
                app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
                g_state_appearance.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 160 && rel_x <= client_w - 90) {
                /* Apply */
                g_state_appearance.is_dirty = FALSE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
                /* Close */
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_appearance_settings_window(void) {
    memset(&g_state_appearance, 0, sizeof(AppletState_appearance));
    for (int i = 0; i < 7; i++) g_state_appearance.checks[i] = TRUE;

    WND *wnd = opn_wnd("外観設定 (Appearance Settings)",
                       80, 45, 640, 440,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_appearance.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_appearance;
    wnd->paint = paint_appearance_settings;
    wnd->event_handler = handle_appearance_event;
    return wnd;
}
