/*
 * B-System (BTRON 3.20) Settings Applet: network (Network / 通信網)
 * Conforming to ./b-system/settings/Network.html
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    BOOL checks[9];
    BOOL is_dirty;
} AppletState_network;

static AppletState_network g_state_network;

static void on_network_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 5; i++) g_state_network.checks[i] = TRUE;
    g_state_network.is_dirty = TRUE;
}

static void on_network_apply(WND *wnd) {
    (void)wnd;
    g_state_network.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_network_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 70, 480, 88 }, "Enable Primary Interface: eth0 (VirtIO-Net / Realtek)", &g_state_network.checks[0], NULL, &g_state_network.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 92, 480, 110 }, "Automatic DHCP Configuration (IPv4 / IPv6)", &g_state_network.checks[1], NULL, &g_state_network.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 114, 480, 132 }, "Primary DNS Resolution: 1.1.1.1, 8.8.8.8", &g_state_network.checks[2], NULL, &g_state_network.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 178, 480, 196 }, "Enable XMPP Instant Messaging Client Protocol", &g_state_network.checks[3], NULL, &g_state_network.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 200, 480, 218 }, "Non-blocking Sockets Subsystem (T-Kernel TCP/IP)", &g_state_network.checks[4], NULL, &g_state_network.is_dirty, NULL }
};

static void paint_network_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "network", "Network", "通信網", "Interfaces, DHCP, DNS & VirtIO-Net");

    /* Section 1: Network Interface Configuration */
    RECT s1 = { 10, 56, dev->width - 10, 150 };
    settings_draw_section(dev, &s1, " [1. Network Interface Configuration] ");

    /* Section 2: Messaging & Remote Services */
    RECT s2 = { 10, 164, dev->width - 10, 236 };
    settings_draw_section(dev, &s2, " [2. Messaging & Remote Services] ");

    settings_draw_controls(dev, s_network_controls, 5);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_network.is_dirty);
}

static void handle_network_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_network_controls, 5, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_network_default, on_network_apply);
}

WND* open_network_settings_window(void) {
    memset(&g_state_network, 0, sizeof(AppletState_network));
    for (int i = 0; i < 5; i++) g_state_network.checks[i] = TRUE;

    WND *wnd = opn_wnd("通信ネットワーク設定 (Network Settings)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_network.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_network;
    wnd->paint = paint_network_settings;
    wnd->event_handler = handle_network_event;
    return wnd;
}
