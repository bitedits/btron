/*
 * B-System (BTRON 3.20) Settings Applet: security (Security / 保全・権限)
 * Conforming to ./b-system/settings/Security.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    BOOL checks[7];
    BOOL is_dirty;
} AppletState_security;

static AppletState_security g_state_security;

static void on_security_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 3; i++) g_state_security.checks[i] = TRUE;
    g_state_security.is_dirty = TRUE;
}

static void on_security_apply(WND *wnd) {
    (void)wnd;
    g_state_security.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_security_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enforce Owner Read/Write/Execute Permission Policies", &g_state_security.checks[0], NULL, &g_state_security.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Isolate Virtual Body References across Foreign Domains", &g_state_security.checks[1], NULL, &g_state_security.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable Ring 0 Kernel / Ring 3 Application Sandbox", &g_state_security.checks[2], NULL, &g_state_security.is_dirty, NULL }
};

static SETTINGS_SECTION s_security_sections[] = {
    { " [1. Access Control Lists (ACL)] ", 1, 0, 3, 56, 94, {0} }
};

static void paint_security_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "security", "Security", "保全・権限", "Real-object ACLs & permissions",
                         s_security_sections, 1, s_security_controls, 3, g_state_security.is_dirty);
}

static void handle_security_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_security_sections, 1, s_security_controls, 3, evt,
                                   on_security_default, on_security_apply);
}

WND* open_security_settings_window(void) {
    memset(&g_state_security, 0, sizeof(AppletState_security));
    for (int i = 0; i < 3; i++) g_state_security.checks[i] = TRUE;

    WND *wnd = opn_wnd("保全・権限 (Security)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_security.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_security;
    wnd->paint = paint_security_settings;
    wnd->event_handler = handle_security_event;
    return wnd;
}
