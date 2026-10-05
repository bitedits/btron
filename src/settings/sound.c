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

/* 3D Crisp Graphical Checkbox */
static void paint_ui_checkbox(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    /* Sunken 3D shadow lines */
    drw_lin(dev, x + 1, y + 2, x + 14, y + 2);
    drw_lin(dev, x + 1, y + 2, x + 1, y + 15);

    if (checked) {
        /* Bold checkmark [✔] */
        drw_lin(dev, x + 3, y + 8, x + 6, y + 12);
        drw_lin(dev, x + 3, y + 9, x + 6, y + 13);
        drw_lin(dev, x + 4, y + 8, x + 7, y + 12);

        drw_lin(dev, x + 6, y + 12, x + 12, y + 4);
        drw_lin(dev, x + 6, y + 13, x + 12, y + 5);
        drw_lin(dev, x + 7, y + 12, x + 13, y + 4);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
}

/* 3D Crisp Graphical Radio Button */
static void paint_ui_radio(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    if (checked) {
        RECT dot = { x + 4, y + 5, x + 11, y + 12 };
        fill_rec(dev, &dot, COLOR_NAVY);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
}

/* 3D Push Button */
static void paint_ui_button(GDEV *dev, H x, H y, H w, H h, const char *label, BOOL pressed) {
    RECT btn = { x, y, x + w, y + h };
    fill_rec(dev, &btn, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
    drw_rec(dev, &btn);

    if (!pressed) {
        drw_lin(dev, x + 1, y + 1, x + w - 2, y + 1);
        drw_lin(dev, x + 1, y + 1, x + 1, y + h - 2);
    }

    H tx = x + (w - (H)strlen(label) * 8) / 2;
    drw_tc_string(dev, tx > x ? tx : x + 4, y + 4, label, COLOR_BLACK, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
}

static void paint_sound_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar: Settings window icon is ALWAYS 32x32 */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "sound", 6, 4, 32, 32);
    char hdr_str[128];
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) - %s", "Sound", "音響・音声", "Audio devices & volume control");
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);

    /* Section 1: Audio Master Output */
    RECT s1 = { 10, 56, dev->width - 10, 172 };
    fill_rec(dev, &s1, COLOR_WHITE);
    drw_rec(dev, &s1);
    drw_tc_string(dev, 16, 48, " [1. Audio Master Output] ", COLOR_NAVY, COLOR_WHITE);
    paint_ui_checkbox(dev, 18, 70, "Master Audio Output (Volume: 85% / -3.5 dB Headroom)", g_state_sound.checks[0], FALSE);
    paint_ui_checkbox(dev, 18, 92, "Enable System Event Chimes & Terminal Bell Sounds", g_state_sound.checks[1], FALSE);
    paint_ui_radio(dev, 18, 114, "VirtIO-Sound / Intel High Definition Audio (HDA)", g_state_sound.checks[2], FALSE);
    paint_ui_radio(dev, 18, 136, "BCM2835 PWM Audio Driver Output", g_state_sound.checks[3], FALSE);

    /* Section 2: Cassette DSP Engine */
    RECT s2 = { 10, 186, dev->width - 10, 258 };
    fill_rec(dev, &s2, COLOR_WHITE);
    drw_rec(dev, &s2);
    drw_tc_string(dev, 16, 178, " [2. Cassette DSP Engine] ", COLOR_NAVY, COLOR_WHITE);
    paint_ui_checkbox(dev, 18, 200, "Audio Sample Rate: 48,000 Hz / 16-bit Stereo PCM", g_state_sound.checks[4], FALSE);
    paint_ui_checkbox(dev, 18, 222, "Low-Latency Buffer: 128 samples (2.6 ms Real-Time DSP)", g_state_sound.checks[5], FALSE);

    /* Action Buttons */
    H btn_y = dev->height - 35;
    paint_ui_button(dev, dev->width - 240, btn_y, 70, 24, "Default", FALSE);
    paint_ui_button(dev, dev->width - 160, btn_y, 70, 24, "Apply", g_state_sound.is_dirty);
    paint_ui_button(dev, dev->width - 80, btn_y, 70, 24, "Close", FALSE);
}

static void handle_sound_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 70 && rel_y <= 88) {
            g_state_sound.checks[0] = !g_state_sound.checks[0];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 92 && rel_y <= 110) {
            g_state_sound.checks[1] = !g_state_sound.checks[1];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 114 && rel_y <= 132) {
            g_state_sound.checks[2] = !g_state_sound.checks[2];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 136 && rel_y <= 154) {
            g_state_sound.checks[3] = !g_state_sound.checks[3];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 200 && rel_y <= 218) {
            g_state_sound.checks[4] = !g_state_sound.checks[4];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        if (rel_x >= 18 && rel_x <= 480 && rel_y >= 222 && rel_y <= 240) {
            g_state_sound.checks[5] = !g_state_sound.checks[5];
            g_state_sound.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }
        /* Action Buttons */
        H btn_y = client_h - 35;
        if (rel_y >= btn_y && rel_y <= btn_y + 24) {
            if (rel_x >= client_w - 240 && rel_x <= client_w - 170) {
                /* Default */
                for (int i = 0; i < 6; i++) g_state_sound.checks[i] = TRUE;
                g_state_sound.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 160 && rel_x <= client_w - 90) {
                /* Apply */
                g_state_sound.is_dirty = FALSE;
                redraw_all_windows();
                return;
            }
            if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
                /* Close */
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_sound_settings_window(void) {
    memset(&g_state_sound, 0, sizeof(AppletState_sound));
    for (int i = 0; i < 6; i++) g_state_sound.checks[i] = TRUE;

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
