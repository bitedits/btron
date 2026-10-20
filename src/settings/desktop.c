/*
 * B-System (BTRON 3.20) Settings Applet: desktop (Desktop / デスクトップ)
 * Conforming to ./b-system/settings/Desktop.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    BOOL checks[10];
    BOOL is_dirty;
} AppletState_desktop;

static AppletState_desktop g_state_desktop;

static void on_desktop_default(WND *wnd) {
    (void)wnd;
    g_state_desktop.checks[0] = TRUE;
    g_state_desktop.checks[1] = TRUE;
    g_state_desktop.checks[2] = TRUE;
    g_state_desktop.checks[3] = TRUE;
    g_state_desktop.checks[4] = FALSE;
    g_state_desktop.checks[5] = TRUE;
    g_state_desktop.is_dirty = TRUE;
}

static void on_desktop_apply(WND *wnd) {
    (void)wnd;
    g_state_desktop.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_desktop_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 70, 480, 88 }, "Enable Real Body / Virtual Body (実身・仮身) Dual Storage", &g_state_desktop.checks[0], NULL, &g_state_desktop.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 92, 480, 110 }, "Magnetic Snap-to-Grid for Desktop Icons (48x48px)", &g_state_desktop.checks[1], NULL, &g_state_desktop.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 114, 480, 132 }, "Double-Click executes associated Application Viewer", &g_state_desktop.checks[2], NULL, &g_state_desktop.is_dirty, NULL },
    { 3, SETTINGS_CTL_RADIO, 1, 0, { 18, 178, 480, 196 }, "Classic Solid Teal (RGB 0x008080) Retro Surface", &g_state_desktop.checks[3], NULL, &g_state_desktop.is_dirty, NULL },
    { 4, SETTINGS_CTL_RADIO, 1, 0, { 18, 200, 480, 218 }, "TAD Fusen Geometric Tile Background Pattern", &g_state_desktop.checks[4], NULL, &g_state_desktop.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 222, 480, 240 }, "Dock Deskbar to Top Edge with Task Tracker", &g_state_desktop.checks[5], NULL, &g_state_desktop.is_dirty, NULL }
};

static void paint_desktop_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "desktop", "Desktop", "デスクトップ", "Workbench behaviour & icon grid");

    /* Section 1: Workbench Virtual Object Modes */
    RECT s1 = { 10, 56, dev->width - 10, 150 };
    settings_draw_section(dev, &s1, " [1. Workbench Virtual Object Modes] ");

    /* Section 2: Background Surface & Patterns */
    RECT s2 = { 10, 164, dev->width - 10, 258 };
    settings_draw_section(dev, &s2, " [2. Background Surface & Patterns] ");

    settings_draw_controls(dev, s_desktop_controls, 6);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_desktop.is_dirty);
}

static void handle_desktop_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_desktop_controls, 6, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_desktop_default, on_desktop_apply);
}

WND* open_desktop_settings_window(void) {
    memset(&g_state_desktop, 0, sizeof(AppletState_desktop));
    g_state_desktop.checks[0] = TRUE;
    g_state_desktop.checks[1] = TRUE;
    g_state_desktop.checks[2] = TRUE;
    g_state_desktop.checks[3] = TRUE;
    g_state_desktop.checks[4] = FALSE;
    g_state_desktop.checks[5] = TRUE;

    WND *wnd = opn_wnd("デスクトップ設定 (Desktop Settings)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_desktop.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_desktop;
    wnd->paint = paint_desktop_settings;
    wnd->event_handler = handle_desktop_event;
    return wnd;
}
