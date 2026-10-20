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
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable Real Body / Virtual Body (実身・仮身) Dual Storage", &g_state_desktop.checks[0], NULL, &g_state_desktop.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Magnetic Snap-to-Grid for Desktop Icons (48x48px)", &g_state_desktop.checks[1], NULL, &g_state_desktop.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Double-Click executes associated Application Viewer", &g_state_desktop.checks[2], NULL, &g_state_desktop.is_dirty, NULL },
    { 3, SETTINGS_CTL_RADIO, 1, 0, {0}, "Classic Solid Teal (RGB 0x008080) Retro Surface", &g_state_desktop.checks[3], NULL, &g_state_desktop.is_dirty, NULL },
    { 4, SETTINGS_CTL_RADIO, 1, 0, {0}, "TAD Fusen Geometric Tile Background Pattern", &g_state_desktop.checks[4], NULL, &g_state_desktop.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Dock Deskbar to Top Edge with Task Tracker", &g_state_desktop.checks[5], NULL, &g_state_desktop.is_dirty, NULL }
};

static SETTINGS_SECTION s_desktop_sections[] = {
    { " [1. Workbench Virtual Object Modes] ", 1, 0, 3, 56, 94, {0} },
    { " [2. Background Surface & Patterns] ",   1, 3, 3, 164, 94, {0} }
};

static void paint_desktop_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "desktop", "Desktop", "デスクトップ", "Workbench behaviour & icon grid",
                         s_desktop_sections, 2, s_desktop_controls, 6, g_state_desktop.is_dirty);
}

static void handle_desktop_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_desktop_sections, 2, s_desktop_controls, 6, evt,
                                   on_desktop_default, on_desktop_apply);
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
