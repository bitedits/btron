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
    int wm_style_sel;   /* 0: BeOS, 1: Cho-Kanji */
    int icon_size_sel;  /* 0: 32x32, 1: 64x64 */
    int menu_style_sel; /* 0: Classic, 1: Modern */
    BOOL checks[11];
    BOOL is_dirty;
} AppletState_appearance;

static AppletState_appearance g_state_appearance;

static void on_style_beos(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    pmc_set_style(WM_STYLE_BEOS);
    g_state_appearance.wm_style_sel = 0;
    g_state_appearance.is_dirty = TRUE;
}

static void on_style_chokanji(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    pmc_set_style(WM_STYLE_CHOKANJI);
    g_state_appearance.wm_style_sel = 1;
    g_state_appearance.is_dirty = TRUE;
}

static void on_icon_size_32(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    appearance_set_icon_size(BTRON_ICON_SIZE_32);
    g_state_appearance.icon_size_sel = 0;
    g_state_appearance.is_dirty = TRUE;
}

static void on_icon_size_64(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    g_state_appearance.icon_size_sel = 1;
    g_state_appearance.is_dirty = TRUE;
}

static void on_menu_classic(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    g_state_appearance.menu_style_sel = 0;
    g_state_appearance.is_dirty = TRUE;
}

static void on_menu_modern(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    app_menu_set_global_style(APP_MENU_STYLE_MODERN_CARD);
    g_state_appearance.menu_style_sel = 1;
    g_state_appearance.is_dirty = TRUE;
}

static void on_appearance_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 7; i++) g_state_appearance.checks[i] = TRUE;
    appearance_set_icon_size(BTRON_ICON_SIZE_64);
    app_menu_set_global_style(APP_MENU_STYLE_CLASSIC_3D);
    pmc_set_style(WM_STYLE_BEOS);
    g_state_appearance.wm_style_sel = 0;
    g_state_appearance.icon_size_sel = 1;
    g_state_appearance.menu_style_sel = 0;
    g_state_appearance.is_dirty = TRUE;
}

static void on_appearance_apply(WND *wnd) {
    (void)wnd;
    g_state_appearance.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_appearance_controls[] = {
    { 0, SETTINGS_CTL_RADIO, 1, 0, {0}, "Window Style: BeOS Sliding Gold Tab (BeOSスタイル)", NULL, &g_state_appearance.wm_style_sel, &g_state_appearance.is_dirty, on_style_beos },
    { 1, SETTINGS_CTL_RADIO, 1, 1, {0}, "Window Style: Cho-Kanji 3D Bevel (超漢字・PMCスタイル)", NULL, &g_state_appearance.wm_style_sel, &g_state_appearance.is_dirty, on_style_chokanji },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Dark Navy High-Contrast Palette (Chapter 7)", &g_state_appearance.checks[1], NULL, &g_state_appearance.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable 3D Double Bezel Window Frame Shadows", &g_state_appearance.checks[3], NULL, &g_state_appearance.is_dirty, NULL },

    { 4, SETTINGS_CTL_RADIO, 2, 0, {0}, "Icon Display Size: Standard 32×32 (標準 32×32 アイコン)", NULL, &g_state_appearance.icon_size_sel, &g_state_appearance.is_dirty, on_icon_size_32 },
    { 5, SETTINGS_CTL_RADIO, 2, 1, {0}, "Icon Display Size: Large 64×64 (大 64×64 高解像度アイコン)", NULL, &g_state_appearance.icon_size_sel, &g_state_appearance.is_dirty, on_icon_size_64 },
    { 6, SETTINGS_CTL_RADIO, 3, 0, {0}, "Menu Style: Classic Chokanji 3D (超漢字3Dベベル)", NULL, &g_state_appearance.menu_style_sel, &g_state_appearance.is_dirty, on_menu_classic },
    { 7, SETTINGS_CTL_RADIO, 3, 1, {0}, "Menu Style: Modern Flat Card (現代風カード)", NULL, &g_state_appearance.menu_style_sel, &g_state_appearance.is_dirty, on_menu_modern },

    { 8, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable TRON Multilingual 16px Vector/Bitmap Engine", &g_state_appearance.checks[4], NULL, &g_state_appearance.is_dirty, NULL },
    { 9, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Load Classical Tibetan Jomolhari Unicode Plane", &g_state_appearance.checks[5], NULL, &g_state_appearance.is_dirty, NULL },
    { 10, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable Subpixel Vector Font Anti-Aliasing", &g_state_appearance.checks[6], NULL, &g_state_appearance.is_dirty, NULL },
};

static SETTINGS_SECTION s_appearance_sections[] = {
    { " [1. Window Manager Frame & Theme Appearance] ", 1, 0, 4, 56, 98, {0}, 8, 18, 4 },
    { " [2. Icon Size & Menu Display Styles] ", 1, 4, 4, 170, 106, {0}, 8, 18, 4 },
    { " [3. Typography & Glyph Rendering] ", 1, 8, 3, 292, 80, {0}, 8, 18, 4 }
};

static void paint_appearance_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    g_state_appearance.wm_style_sel = (g_wm_style == WM_STYLE_BEOS) ? 0 : 1;
    g_state_appearance.icon_size_sel = (appearance_get_icon_size() == BTRON_ICON_SIZE_32) ? 0 : 1;
    g_state_appearance.menu_style_sel = (app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D) ? 0 : 1;

    settings_draw_dialog(dev, "appearance", "Appearance", "外観", "Themes, colours & window styles",
                         s_appearance_sections, 3,
                         s_appearance_controls, 11,
                         g_state_appearance.is_dirty);
}

static void handle_appearance_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd,
                                   s_appearance_sections, 3,
                                   s_appearance_controls, 11,
                                   evt, on_appearance_default, on_appearance_apply);
}

WND* open_appearance_settings_window(void) {
    memset(&g_state_appearance, 0, sizeof(AppletState_appearance));
    for (int i = 0; i < 7; i++) g_state_appearance.checks[i] = TRUE;
    g_state_appearance.wm_style_sel = (g_wm_style == WM_STYLE_BEOS) ? 0 : 1;
    g_state_appearance.icon_size_sel = (appearance_get_icon_size() == BTRON_ICON_SIZE_32) ? 0 : 1;
    g_state_appearance.menu_style_sel = (app_menu_get_global_style() == APP_MENU_STYLE_CLASSIC_3D) ? 0 : 1;

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
