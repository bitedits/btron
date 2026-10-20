/*
 * B-System (BTRON 3.20) Settings Applet: system (System / 基本情報)
 * Conforming to ./b-system/settings/System.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    BOOL checks[8];
    BOOL is_dirty;
} AppletState_system;

static AppletState_system g_state_system;

static void on_system_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 4; i++) g_state_system.checks[i] = TRUE;
    g_state_system.is_dirty = TRUE;
}

static void on_system_apply(WND *wnd) {
    (void)wnd;
    g_state_system.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_system_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 70, 480, 88 }, "B-System 3.20 (BTRON3 Cleanroom Implementation)", &g_state_system.checks[0], NULL, &g_state_system.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 92, 480, 110 }, "Multi-Arch CPU Support (ARMv7, x86_64, PC-98)", &g_state_system.checks[1], NULL, &g_state_system.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 114, 480, 132 }, "SMP Multiprocessing: 4 Active Preemptive Cores", &g_state_system.checks[2], NULL, &g_state_system.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 136, 480, 154 }, "Memory Status: 1024 MB Physical RAM (874 MB Free)", &g_state_system.checks[3], NULL, &g_state_system.is_dirty, NULL }
};

static void paint_system_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "system", "System", "基本情報", "System Plane, memory & SMP cores");

    /* Section 1: System Plane & Kernel Information */
    RECT s1 = { 10, 56, dev->width - 10, 172 };
    settings_draw_section(dev, &s1, " [1. System Plane & Kernel Information] ");
    settings_draw_controls(dev, s_system_controls, 4);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_system.is_dirty);
}

static void handle_system_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_system_controls, 4, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_system_default, on_system_apply);
}

WND* open_system_settings_window(void) {
    memset(&g_state_system, 0, sizeof(AppletState_system));
    for (int i = 0; i < 4; i++) g_state_system.checks[i] = TRUE;

    WND *wnd = opn_wnd("基本情報 (System)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_system.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_system;
    wnd->paint = paint_system_settings;
    wnd->event_handler = handle_system_event;
    return wnd;
}
