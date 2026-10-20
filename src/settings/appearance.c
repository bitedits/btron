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

static void on_style_beos(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    pmc_set_style(WM_STYLE_BEOS);
    g_state_appearance.is_dirty = TRUE;
}

static void on_style_chokanji(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    pmc_set_style(WM_STYLE_CHOKANJI);
    g_state_appearance.is_dirty = TRUE;
}

static void on_icon_size_32(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    appearance_set_icon_size(BTRON_ICON_SIZE_32);
    g_state_appearance.is_dirty = TRUE;
}

static void on_icon_size_64(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    g_state_appearance.is_dirty = TRUE;
}

static void on_menu_classic(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    g_state_appearance.is_dirty = TRUE;
}

static void on_menu_modern(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    app_menu_set_global_style(APP_MENU_STYLE_MODERN_CARD);
    g_state_appearance.is_dirty = TRUE;
}

static void on_appearance_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 7; i++) g_state_appearance.checks[i] = TRUE;
    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    g_state_appearance.is_dirty = TRUE;
}

static void on_appearance_apply(WND *wnd) {
    (void)wnd;
    g_state_appearance.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_appearance_controls[] = {
    { 0, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 64, 480, 82 }, "Window Style: BeOS Sliding Gold Tab (BeOSスタイル)", NULL, NULL, &g_state_appearance.is_dirty, on_style_beos },
    { 1, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 86, 480, 104 }, "Window Style: Cho-Kanji 3D Bevel (超漢字・PMCスタイル)", NULL, NULL, &g_state_appearance.is_dirty, on_style_chokanji },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 108, 480, 126 }, "Dark Navy High-Contrast Palette (Chapter 7)", &g_state_appearance.checks[1], NULL, &g_state_appearance.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 130, 480, 148 }, "Enable 3D Double Bezel Window Frame Shadows", &g_state_appearance.checks[3], NULL, &g_state_appearance.is_dirty, NULL },

    { 4, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 178, 480, 196 }, "Icon Display Size: Standard 32×32 (標準 32×32 アイコン)", NULL, NULL, &g_state_appearance.is_dirty, on_icon_size_32 },
    { 5, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 200, 480, 218 }, "Icon Display Size: Large 64×64 (大 64×64 高解像度アイコン)", NULL, NULL, &g_state_appearance.is_dirty, on_icon_size_64 },
    { 6, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 226, 480, 244 }, "Menu Style: Classic Chokanji 3D (超漢字3Dベベル)", NULL, NULL, &g_state_appearance.is_dirty, on_menu_classic },
    { 7, SETTINGS_CTL_CUSTOM, 0, 0, { 18, 248, 480, 266 }, "Menu Style: Modern Flat Card (現代風カード)", NULL, NULL, &g_state_appearance.is_dirty, on_menu_modern },

    { 8, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 300, 480, 318 }, "Enable TRON Multilingual 16px Vector/Bitmap Engine", &g_state_appearance.checks[4], NULL, &g_state_appearance.is_dirty, NULL },
    { 9, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 322, 480, 340 }, "Load Classical Tibetan Jomolhari Unicode Plane", &g_state_appearance.checks[5], NULL, &g_state_appearance.is_dirty, NULL },
    { 10, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 344, 480, 362 }, "Enable Subpixel Vector Font Anti-Aliasing", &g_state_appearance.checks[6], NULL, &g_state_appearance.is_dirty, NULL },
};

static void paint_appearance_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "appearance", "Appearance", "外観", "Themes, colours & window styles");

    /* Section 1: System Themes & Visual Appearance */
    RECT s1 = { 10, 56, dev->width - 10, 154 };
    settings_draw_section(dev, &s1, " [1. Window Manager Frame & Theme Appearance] ");
    settings_draw_ui_radio(dev, 18, 64, "Window Style: BeOS Sliding Gold Tab (BeOSスタイル)", g_wm_style == WM_STYLE_BEOS, FALSE);
    settings_draw_ui_radio(dev, 18, 86, "Window Style: Cho-Kanji 3D Bevel (超漢字・PMCスタイル)", g_wm_style == WM_STYLE_CHOKANJI, FALSE);
    settings_draw_ui_radio(dev, 18, 108, "Dark Navy High-Contrast Palette (Chapter 7)", g_state_appearance.checks[1], FALSE);
    settings_draw_ui_checkbox(dev, 18, 130, "Enable 3D Double Bezel Window Frame Shadows", g_state_appearance.checks[3], FALSE);

    /* Section 2: Icon Size & Menu Display Styles */
    RECT s2 = { 10, 170, dev->width - 10, 276 };
    settings_draw_section(dev, &s2, " [2. Icon Size & Menu Display Styles] ");

    /* Icon Size Radio Buttons */
    BOOL is_size_32 = (g_global_icon_size == BTRON_ICON_SIZE_32);
    settings_draw_ui_radio(dev, 18, 178, "Icon Display Size: Standard 32×32 (標準 32×32 アイコン)", is_size_32, FALSE);
    settings_draw_ui_radio(dev, 18, 200, "Icon Display Size: Large 64×64 (大 64×64 高解像度アイコン)", !is_size_32, FALSE);

    /* Menu Style Radio Buttons */
    BOOL is_classic = (app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D);
    settings_draw_ui_radio(dev, 18, 226, "Menu Style: Classic Chokanji 3D (超漢字3Dベベル)", is_classic, FALSE);
    settings_draw_ui_radio(dev, 18, 248, "Menu Style: Modern Flat Card (現代風カード)", !is_classic, FALSE);

    /* Section 3: Typography & Glyph Rendering */
    RECT s3 = { 10, 292, dev->width - 10, 372 };
    settings_draw_section(dev, &s3, " [3. Typography & Glyph Rendering] ");
    settings_draw_ui_checkbox(dev, 18, 300, "Enable TRON Multilingual 16px Vector/Bitmap Engine", g_state_appearance.checks[4], FALSE);
    settings_draw_ui_checkbox(dev, 18, 322, "Load Classical Tibetan Jomolhari Unicode Plane", g_state_appearance.checks[5], FALSE);
    settings_draw_ui_checkbox(dev, 18, 344, "Enable Subpixel Vector Font Anti-Aliasing", g_state_appearance.checks[6], FALSE);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_appearance.is_dirty);
}

static void handle_appearance_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_appearance_controls, 11, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_appearance_default, on_appearance_apply);
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
