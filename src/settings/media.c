/*
 * B-System (BTRON 3.20) Settings Applet: media (Media / メディア)
 * Conforming to ./b-system/settings/Media.html
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
} AppletState_media;

static AppletState_media g_state_media;

static void on_media_default(WND *wnd) {
    (void)wnd;
    for (int i = 0; i < 4; i++) g_state_media.checks[i] = TRUE;
    g_state_media.is_dirty = TRUE;
}

static void on_media_apply(WND *wnd) {
    (void)wnd;
    g_state_media.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_media_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 70, 480, 88 }, "Associate TAD Hypertext (.tad) with TAD Browser", &g_state_media.checks[0], NULL, &g_state_media.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 92, 480, 110 }, "Associate Plain-Text (.txt, .c) with Editor", &g_state_media.checks[1], NULL, &g_state_media.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 114, 480, 132 }, "Associate Form Documents (.pdf) with PDF TAD Reader", &g_state_media.checks[2], NULL, &g_state_media.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 136, 480, 154 }, "Associate Audio Streams (.wav, .pcm) with Media Deck", &g_state_media.checks[3], NULL, &g_state_media.is_dirty, NULL }
};

static void paint_media_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "media", "Media", "メディア", "Default handlers & associations");

    /* Section 1: File Type Associations */
    RECT s1 = { 10, 56, dev->width - 10, 172 };
    settings_draw_section(dev, &s1, " [1. File Type Associations] ");
    settings_draw_controls(dev, s_media_controls, 4);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_media.is_dirty);
}

static void handle_media_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_media_controls, 4, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_media_default, on_media_apply);
}

WND* open_media_settings_window(void) {
    memset(&g_state_media, 0, sizeof(AppletState_media));
    for (int i = 0; i < 4; i++) g_state_media.checks[i] = TRUE;

    WND *wnd = opn_wnd("メディア (Media Settings)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_media.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_media;
    wnd->paint = paint_media_settings;
    wnd->event_handler = handle_media_event;
    return wnd;
}
