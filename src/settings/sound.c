/*
 * B-System (BTRON 3.20) Settings Applet: sound (Sound / 音響・音声)
 * Conforming to ./b-system/settings/Sound.html
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
} AppletState_sound;

static AppletState_sound g_state_sound;

static void on_sound_default(WND *wnd) {
    (void)wnd;
    g_state_sound.checks[0] = TRUE;
    g_state_sound.checks[1] = TRUE;
    g_state_sound.checks[2] = TRUE;
    g_state_sound.checks[3] = FALSE;
    g_state_sound.checks[4] = TRUE;
    g_state_sound.checks[5] = TRUE;
    g_state_sound.is_dirty = TRUE;
}

static void on_sound_apply(WND *wnd) {
    (void)wnd;
    g_state_sound.is_dirty = FALSE;
}

static SETTINGS_CONTROL s_sound_controls[] = {
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Master Audio Output (Volume: 85% / -3.5 dB Headroom)", &g_state_sound.checks[0], NULL, &g_state_sound.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Enable System Event Chimes & Terminal Bell Sounds", &g_state_sound.checks[1], NULL, &g_state_sound.is_dirty, NULL },
    { 2, SETTINGS_CTL_RADIO, 1, 0, {0}, "VirtIO-Sound / Intel High Definition Audio (HDA)", &g_state_sound.checks[2], NULL, &g_state_sound.is_dirty, NULL },
    { 3, SETTINGS_CTL_RADIO, 1, 0, {0}, "BCM2835 PWM Audio Driver Output", &g_state_sound.checks[3], NULL, &g_state_sound.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Audio Sample Rate: 48,000 Hz / 16-bit Stereo PCM", &g_state_sound.checks[4], NULL, &g_state_sound.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, {0}, "Low-Latency Buffer: 128 samples (2.6 ms Real-Time DSP)", &g_state_sound.checks[5], NULL, &g_state_sound.is_dirty, NULL }
};

static SETTINGS_SECTION s_sound_sections[] = {
    { " [1. Audio Master Output] ", 1, 0, 4, 56, 116, {0} },
    { " [2. Cassette DSP Engine] ", 1, 4, 2, 186, 72, {0} }
};

static void paint_sound_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    settings_draw_dialog(dev, "sound", "Sound", "音響・音声", "Audio devices & volume control",
                         s_sound_sections, 2, s_sound_controls, 6, g_state_sound.is_dirty);
}

static void handle_sound_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    settings_dispatch_dialog_event(wnd, s_sound_sections, 2, s_sound_controls, 6, evt,
                                   on_sound_default, on_sound_apply);
}

WND* open_sound_settings_window(void) {
    memset(&g_state_sound, 0, sizeof(AppletState_sound));
    g_state_sound.checks[0] = TRUE;
    g_state_sound.checks[1] = TRUE;
    g_state_sound.checks[2] = TRUE;
    g_state_sound.checks[3] = FALSE;
    g_state_sound.checks[4] = TRUE;
    g_state_sound.checks[5] = TRUE;

    WND *wnd = opn_wnd("音響・音声 (Sound)",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_sound.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_sound;
    wnd->paint = paint_sound_settings;
    wnd->event_handler = handle_sound_event;
    return wnd;
}
