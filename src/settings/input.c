/*
 * B-System (BTRON 3.20) Settings Applet: input (Input / 入力環境)
 * Acorn RISC OS Style Keyboard & Mouse Configuration
 * Conforming to RISC OS !Configure Keyboard & Mouse Specification
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
#define memset tkl_memset
#define strlen tkl_strlen
#define snprintf tkl_snprintf
#endif

/* Hardware kernel globals for live input configuration */
extern uint32_t g_kbd_repeat_delay_us;
extern uint32_t g_kbd_repeat_interval_us;
extern int      g_kbd_repeat_enabled;
extern int      g_mouse_step_mult;
extern int      g_mouse_swap_select_adjust;

typedef struct {
    WND *wnd;
    /* Keyboard Settings */
    BOOL kbd_repeat_enable;
    int  kbd_delay_sel;    /* 0: 16cs (160ms), 1: 32cs (320ms), 2: 50cs (500ms) */
    int  kbd_rate_sel;     /* 0: 40 cps, 1: 25 cps, 2: 12.5 cps */
    int  kbd_layout_sel;   /* 0: US ANSI, 1: Acorn UK, 2: Japanese JIS, 3: TRON-KBD */

    /* Mouse Settings */
    int  mouse_step_sel;    /* 0: Step 1, 1: Step 2 (RISC OS Default), 2: Step 3, 3: Step 4 */
    BOOL mouse_accel_enable;
    int  mouse_handedness;  /* 0: Right-handed (Select/Menu/Adjust), 1: Left-handed */
    int  dbl_click_sel;     /* 0: 40cs (400ms), 1: 50cs (500ms) */

    /* Double-click test area */
    uint32_t last_test_click_us;
    int      test_click_status;   /* 0: idle, 1: single click, 2: double-click ok */
    uint32_t test_click_delta_ms;

    BOOL is_dirty;
} AppletState_input;

static AppletState_input g_state_input;

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
    drw_tc_string(dev, tx > x ? tx : x + 4, y + 5, label, COLOR_BLACK, pressed ? COLOR_DKGRAY : COLOR_LTGRAY);
}

static void apply_input_settings_to_kernel(void) {
    g_kbd_repeat_enabled = g_state_input.kbd_repeat_enable;

    /* Repeat Delay (*FX 11) */
    if (g_state_input.kbd_delay_sel == 0) {
        g_kbd_repeat_delay_us = 160000U; /* 16 cs = 160 ms (RISC OS Fast) */
    } else if (g_state_input.kbd_delay_sel == 1) {
        g_kbd_repeat_delay_us = 320000U; /* 32 cs = 320 ms (Acorn Default) */
    } else {
        g_kbd_repeat_delay_us = 500000U; /* 50 cs = 500 ms (BBC Standard) */
    }

    /* Repeat Rate (*FX 12) */
    if (g_state_input.kbd_rate_sel == 0) {
        g_kbd_repeat_interval_us = 25000U; /* 40 cps */
    } else if (g_state_input.kbd_rate_sel == 1) {
        g_kbd_repeat_interval_us = 40000U; /* 25 cps */
    } else {
        g_kbd_repeat_interval_us = 80000U; /* 12.5 cps */
    }

    /* Mouse Step Multiplier (*Configure MouseStep CMOS &C2) */
    g_mouse_step_mult = g_state_input.mouse_step_sel + 1; /* 1, 2, 3, 4 */

    /* Select / Adjust handedness */
    g_mouse_swap_select_adjust = g_state_input.mouse_handedness;

    g_state_input.is_dirty = FALSE;
}

static void set_risc_os_defaults(void) {
    g_state_input.kbd_repeat_enable  = TRUE;
    g_state_input.kbd_delay_sel      = 0;     /* 16cs (160ms) RISC OS Fast */
    g_state_input.kbd_rate_sel       = 0;     /* 40 cps Fast */
    g_state_input.kbd_layout_sel     = 0;     /* US ANSI */

    g_state_input.mouse_step_sel     = 1;     /* Step 2: RISC OS Standard Default */
    g_state_input.mouse_accel_enable = TRUE;
    g_state_input.mouse_handedness   = 0;     /* Right-handed (Select/Menu/Adjust) */
    g_state_input.dbl_click_sel      = 1;     /* 50cs (500ms) RISC OS Default */

    apply_input_settings_to_kernel();
}

static void paint_input_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Window Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar: Settings window icon 32x32 */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "input", 6, 4, 32, 32);
    char hdr_str[128];
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) — RISC OS Style Options", "Input", "入力環境");
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);

    /* ═══════════════════════════════════════════════════════════════════
     * Section 1: RISC OS Keyboard Configuration (Left Panel)
     * ═══════════════════════════════════════════════════════════════════ */
    RECT s1 = { 10, 48, 314, 380 };
    fill_rec(dev, &s1, COLOR_WHITE);
    drw_rec(dev, &s1);
    drw_tc_string(dev, 16, 40, " [1. Keyboard Configuration (RISC OS)] ", COLOR_NAVY, COLOR_WHITE);

    paint_ui_checkbox(dev, 18, 64, "Enable Auto-Repeat (*FX 11/12)", g_state_input.kbd_repeat_enable, FALSE);

    drw_tc_string(dev, 18, 88, "Auto-Repeat Delay (*FX 11):", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 22, 106, "Short: 16cs (160ms) [RISC OS Fast]", g_state_input.kbd_delay_sel == 0, FALSE);
    paint_ui_radio(dev, 22, 124, "Default: 32cs (320ms) [Acorn Archimedes]", g_state_input.kbd_delay_sel == 1, FALSE);
    paint_ui_radio(dev, 22, 142, "Long: 50cs (500ms) [BBC Micro Standard]", g_state_input.kbd_delay_sel == 2, FALSE);

    drw_tc_string(dev, 18, 168, "Auto-Repeat Rate (*FX 12):", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 22, 186, "Fast: 40 cps (25ms interval)", g_state_input.kbd_rate_sel == 0, FALSE);
    paint_ui_radio(dev, 22, 204, "Standard: 25 cps (40ms interval)", g_state_input.kbd_rate_sel == 1, FALSE);
    paint_ui_radio(dev, 22, 222, "Slow: 12.5 cps (80ms interval)", g_state_input.kbd_rate_sel == 2, FALSE);

    drw_tc_string(dev, 18, 248, "Keyboard Layout (*Configure Country):", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 22, 266, "Standard US ANSI 101/104", g_state_input.kbd_layout_sel == 0, FALSE);
    paint_ui_radio(dev, 22, 284, "Acorn British English (UK)", g_state_input.kbd_layout_sel == 1, FALSE);
    paint_ui_radio(dev, 22, 302, "Japanese JIS 106 (OADG)", g_state_input.kbd_layout_sel == 2, FALSE);
    paint_ui_radio(dev, 22, 320, "TRON Ergonomic Multilingual", g_state_input.kbd_layout_sel == 3, FALSE);

    /* ═══════════════════════════════════════════════════════════════════
     * Section 2: RISC OS Mouse Configuration (Right Panel)
     * ═══════════════════════════════════════════════════════════════════ */
    RECT s2 = { 324, 48, 628, 380 };
    fill_rec(dev, &s2, COLOR_WHITE);
    drw_rec(dev, &s2);
    drw_tc_string(dev, 330, 40, " [2. Mouse Configuration (RISC OS)] ", COLOR_NAVY, COLOR_WHITE);

    drw_tc_string(dev, 332, 64, "Tracking Speed (*Configure MouseStep):", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 336, 82, "Step 1: 1.0x Slow Precision", g_state_input.mouse_step_sel == 0, FALSE);
    paint_ui_radio(dev, 336, 100, "Step 2: 2.0x Standard (RISC OS CMOS)", g_state_input.mouse_step_sel == 1, FALSE);
    paint_ui_radio(dev, 336, 118, "Step 3: 3.0x Fast Sweep", g_state_input.mouse_step_sel == 2, FALSE);
    paint_ui_radio(dev, 336, 136, "Step 4: 4.0x Ultra Velocity", g_state_input.mouse_step_sel == 3, FALSE);

    paint_ui_checkbox(dev, 332, 160, "Stepped Acceleration (Archimedes Curve)", g_state_input.mouse_accel_enable, FALSE);

    drw_tc_string(dev, 332, 184, "Three-Button Model (Select, Menu, Adjust):", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 336, 202, "Right-Hand: [Select] [Menu] [Adjust]", g_state_input.mouse_handedness == 0, FALSE);
    paint_ui_radio(dev, 336, 220, "Left-Hand:  [Adjust] [Menu] [Select]", g_state_input.mouse_handedness == 1, FALSE);

    drw_tc_string(dev, 332, 244, "Double-Click Threshold:", COLOR_NAVY, COLOR_WHITE);
    paint_ui_radio(dev, 336, 262, "40 cs (400ms interval)", g_state_input.dbl_click_sel == 0, FALSE);
    paint_ui_radio(dev, 336, 280, "50 cs (500ms [RISC OS default])", g_state_input.dbl_click_sel == 1, FALSE);

    /* Interactive Double-Click Test Area */
    RECT test_box = { 334, 306, 618, 366 };
    COLOR test_bg = COLOR_LTGRAY;
    if (g_state_input.test_click_status == 2) {
        test_bg = COLOR_GREEN;
    } else if (g_state_input.test_click_status == 1) {
        test_bg = COLOR_YELLOW;
    }
    fill_rec(dev, &test_box, test_bg);
    drw_rec(dev, &test_box);
    drw_lin(dev, test_box.left + 1, test_box.top + 1, test_box.right - 2, test_box.top + 1);
    drw_lin(dev, test_box.left + 1, test_box.top + 1, test_box.left + 1, test_box.bottom - 2);

    if (g_state_input.test_click_status == 2) {
        char msg[64];
        snprintf(msg, sizeof(msg), "[✔] Double-Click OK! (%u ms)", (unsigned int)g_state_input.test_click_delta_ms);
        drw_tc_string(dev, 350, 328, msg, COLOR_WHITE, COLOR_GREEN);
    } else if (g_state_input.test_click_status == 1) {
        drw_tc_string(dev, 350, 328, "[..] Click again to verify double-click", COLOR_BLACK, COLOR_YELLOW);
    } else {
        drw_tc_string(dev, 350, 328, "[ Test Area: Double-Click Here ]", COLOR_DKGRAY, COLOR_LTGRAY);
    }

    /* ═══════════════════════════════════════════════════════════════════
     * Bottom Action Buttons
     * ═══════════════════════════════════════════════════════════════════ */
    H btn_y = dev->height - 36;
    paint_ui_button(dev, dev->width - 340, btn_y, 140, 26, "Default (RISC OS)", FALSE);
    paint_ui_button(dev, dev->width - 185, btn_y, 85, 26, "Apply", g_state_input.is_dirty);
    paint_ui_button(dev, dev->width - 85, btn_y, 75, 26, "Close", FALSE);
}

static void handle_input_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;

        /* 1. Left Panel (Keyboard) */
        /* Enable Auto-Repeat */
        if (rel_x >= 18 && rel_x <= 300 && rel_y >= 64 && rel_y <= 82) {
            g_state_input.kbd_repeat_enable = !g_state_input.kbd_repeat_enable;
            g_state_input.is_dirty = TRUE;
            redraw_all_windows();
            return;
        }

        /* Repeat Delay */
        if (rel_x >= 22 && rel_x <= 300) {
            if (rel_y >= 106 && rel_y <= 122) {
                g_state_input.kbd_delay_sel = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 124 && rel_y <= 140) {
                g_state_input.kbd_delay_sel = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 142 && rel_y <= 158) {
                g_state_input.kbd_delay_sel = 2;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Repeat Rate */
            if (rel_y >= 186 && rel_y <= 202) {
                g_state_input.kbd_rate_sel = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 204 && rel_y <= 220) {
                g_state_input.kbd_rate_sel = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 222 && rel_y <= 238) {
                g_state_input.kbd_rate_sel = 2;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Layout */
            if (rel_y >= 266 && rel_y <= 282) {
                g_state_input.kbd_layout_sel = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 284 && rel_y <= 300) {
                g_state_input.kbd_layout_sel = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 302 && rel_y <= 318) {
                g_state_input.kbd_layout_sel = 2;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 320 && rel_y <= 338) {
                g_state_input.kbd_layout_sel = 3;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
        }

        /* 2. Right Panel (Mouse) */
        if (rel_x >= 332 && rel_x <= 620) {
            /* Tracking Speed (MouseStep) */
            if (rel_y >= 82 && rel_y <= 98) {
                g_state_input.mouse_step_sel = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 100 && rel_y <= 116) {
                g_state_input.mouse_step_sel = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 118 && rel_y <= 134) {
                g_state_input.mouse_step_sel = 2;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 136 && rel_y <= 152) {
                g_state_input.mouse_step_sel = 3;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Stepped Acceleration */
            if (rel_y >= 160 && rel_y <= 178) {
                g_state_input.mouse_accel_enable = !g_state_input.mouse_accel_enable;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Handedness */
            if (rel_y >= 202 && rel_y <= 218) {
                g_state_input.mouse_handedness = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 220 && rel_y <= 236) {
                g_state_input.mouse_handedness = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Double-Click threshold */
            if (rel_y >= 262 && rel_y <= 278) {
                g_state_input.dbl_click_sel = 0;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }
            if (rel_y >= 280 && rel_y <= 298) {
                g_state_input.dbl_click_sel = 1;
                g_state_input.is_dirty = TRUE;
                redraw_all_windows();
                return;
            }

            /* Interactive Double-Click Test Area */
            if (rel_y >= 306 && rel_y <= 366) {
                extern uintptr_t g_mmio_base;
                uint32_t now_us = 0;
#if !defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 1
                now_us = *(volatile uint32_t *)(g_mmio_base + 0x00003004UL);
#else
                static uint32_t s_mock_clock = 0;
                now_us = (s_mock_clock += 150000);
#endif
                uint32_t limit_us = (g_state_input.dbl_click_sel == 0) ? 400000U : 500000U;
                uint32_t delta = now_us - g_state_input.last_test_click_us;

                if (g_state_input.test_click_status == 1 && delta <= limit_us) {
                    g_state_input.test_click_status = 2;
                    g_state_input.test_click_delta_ms = delta / 1000U;
                } else {
                    g_state_input.test_click_status = 1;
                }
                g_state_input.last_test_click_us = now_us;
                redraw_all_windows();
                return;
            }
        }

        /* 3. Action Buttons */
        H btn_y = client_h - 36;
        if (rel_y >= btn_y && rel_y <= btn_y + 26) {
            /* Default (RISC OS) */
            if (rel_x >= client_w - 340 && rel_x <= client_w - 200) {
                set_risc_os_defaults();
                redraw_all_windows();
                return;
            }
            /* Apply */
            if (rel_x >= client_w - 185 && rel_x <= client_w - 100) {
                apply_input_settings_to_kernel();
                redraw_all_windows();
                return;
            }
            /* Close */
            if (rel_x >= client_w - 85 && rel_x <= client_w - 10) {
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_input_settings_window(void) {
    memset(&g_state_input, 0, sizeof(AppletState_input));

    /* Sync initial state from live kernel globals */
    g_state_input.kbd_repeat_enable  = g_kbd_repeat_enabled;
    g_state_input.kbd_delay_sel      = (g_kbd_repeat_delay_us <= 160000U) ? 0 :
                                       ((g_kbd_repeat_delay_us <= 320000U) ? 1 : 2);
    g_state_input.kbd_rate_sel       = (g_kbd_repeat_interval_us <= 25000U) ? 0 :
                                       ((g_kbd_repeat_interval_us <= 40000U) ? 1 : 2);
    g_state_input.kbd_layout_sel     = 0;

    g_state_input.mouse_step_sel     = (g_mouse_step_mult >= 1 && g_mouse_step_mult <= 4) ?
                                       (g_mouse_step_mult - 1) : 1;
    g_state_input.mouse_accel_enable = TRUE;
    g_state_input.mouse_handedness   = g_mouse_swap_select_adjust ? 1 : 0;
    g_state_input.dbl_click_sel      = 1; /* 50cs (500ms) RISC OS default */

    WND *wnd = opn_wnd("Input & Pointer (入力環境) — RISC OS Style",
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_input.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_input;
    wnd->paint = paint_input_settings;
    wnd->event_handler = handle_input_event;
    return wnd;
}
