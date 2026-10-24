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
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Associate TAD Hypertext (.tad) with TAD Browser", &g_state_media.checks[0], NULL, &g_state_media.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Associate Plain-Text (.txt, .c) with Editor", &g_state_media.checks[1], NULL, &g_state_media.is_dirty, NULL },
    { 2, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Associate Form Documents (.pdf) with PDF TAD Reader", &g_state_media.checks[2], NULL, &g_state_media.is_dirty, NULL },
    { 3, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Associate Audio Streams (.wav, .pcm) with Media Deck", &g_state_media.checks[3], NULL, &g_state_media.is_dirty, NULL }
};

static SETTINGS_SECTION s_media_sections[] = {
    { " [1. File Type Associations] ", 1, 0, 4, 56, 116, {0}, 0, 0, 0 }
};

static void paint_media_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "media", "Media", "メディア", "Default handlers & associations",
                         s_media_sections, 1, s_media_controls, 4, g_state_media.is_dirty);
}

static void handle_media_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_media_sections, 1, s_media_controls, 4, evt,
                                   on_media_default, on_media_apply);
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
