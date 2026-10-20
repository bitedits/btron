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
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 70, 480, 88 }, "Master Audio Output (Volume: 85% / -3.5 dB Headroom)", &g_state_sound.checks[0], NULL, &g_state_sound.is_dirty, NULL },
    { 1, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 92, 480, 110 }, "Enable System Event Chimes & Terminal Bell Sounds", &g_state_sound.checks[1], NULL, &g_state_sound.is_dirty, NULL },
    { 2, SETTINGS_CTL_RADIO, 1, 0, { 18, 114, 480, 132 }, "VirtIO-Sound / Intel High Definition Audio (HDA)", &g_state_sound.checks[2], NULL, &g_state_sound.is_dirty, NULL },
    { 3, SETTINGS_CTL_RADIO, 1, 0, { 18, 136, 480, 154 }, "BCM2835 PWM Audio Driver Output", &g_state_sound.checks[3], NULL, &g_state_sound.is_dirty, NULL },
    { 4, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 200, 480, 218 }, "Audio Sample Rate: 48,000 Hz / 16-bit Stereo PCM", &g_state_sound.checks[4], NULL, &g_state_sound.is_dirty, NULL },
    { 5, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 222, 480, 240 }, "Low-Latency Buffer: 128 samples (2.6 ms Real-Time DSP)", &g_state_sound.checks[5], NULL, &g_state_sound.is_dirty, NULL }
};

static void paint_sound_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, "sound", "Sound", "音響・音声", "Audio devices & volume control");

    /* Section 1: Audio Master Output */
    RECT s1 = { 10, 56, dev->width - 10, 172 };
    settings_draw_section(dev, &s1, " [1. Audio Master Output] ");

    /* Section 2: Cassette DSP Engine */
    RECT s2 = { 10, 186, dev->width - 10, 258 };
    settings_draw_section(dev, &s2, " [2. Cassette DSP Engine] ");

    settings_draw_controls(dev, s_sound_controls, 6);

    /* Action Buttons */
    settings_draw_action_buttons(dev, g_state_sound.is_dirty);
}

static void handle_sound_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (settings_dispatch_control_event(wnd, s_sound_controls, 6, evt)) return;
    settings_dispatch_action_buttons(wnd, evt, on_sound_default, on_sound_apply);
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
