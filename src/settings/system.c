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
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "B-System 3.20 (BTRON3 Cleanroom Implementation)", &g_state_system.checks[0], NULL, &g_state_system.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Multi-Arch CPU Support (ARMv7, x86_64, PC-98)", &g_state_system.checks[1], NULL, &g_state_system.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "SMP Multiprocessing: 4 Active Preemptive Cores", &g_state_system.checks[2], NULL, &g_state_system.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Memory Status: 1024 MB Physical RAM (874 MB Free)", &g_state_system.checks[3], NULL, &g_state_system.is_dirty, NULL }
};

static SETTINGS_SECTION s_system_sections[] = {
    { " [1. System Plane & Kernel Information] ", 1, 0, 4, 56, 116, {0}, 0, 0, 0 }
};

static void paint_system_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "system", "System", "基本情報", "System Plane, memory & SMP cores",
                         s_system_sections, 1, s_system_controls, 4, g_state_system.is_dirty);
}

static void handle_system_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_system_sections, 1, s_system_controls, 4, evt,
                                   on_system_default, on_system_apply);
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
