/*
 * B-System (BTRON 3.20) Settings Applet: input (Input / 入力環境)
 * Acorn RISC OS Style Keyboard & Mouse Configuration
 * Conforming to RISC OS !Configure Keyboard & Mouse Specification
 */

#include <btron/settings.h>
#include <btron/dp.h>
#include <btron/dp_accel.h>
#include <btron/troncode.h>
#include <btron/wnd.h>
#include <btron/settings_icon.h>

#include <btron/libc_shim.h>

/* Hardware kernel globals for live input configuration */
uint32_t g_kbd_repeat_delay_us    = 160000U; /* 16 cs = 160 ms (RISC OS Fast Delay) */
uint32_t g_kbd_repeat_interval_us = 25000U;  /* 40 cps (25 ms interval) */
int      g_kbd_repeat_enabled     = 1;

#if defined(BTRON_PS2_TARGET)
/* LAW 13e (doc/txt/PS2.txt): one gain, one home, several hands.  This is the
 * home -- the multiplier this port's pointer reads, in the 8.8 unit
 * dp_ptr_scale() takes, with 0 meaning "the live source profile's own default".
 * `sens` at the Stage-1 prompt, the block painted below, and the launcher's row
 * in the pointer band all write it; none of them keeps a copy, and the core
 * notices between passes.
 *
 * The stops themselves -- btron_ptr_gain_fp[], btron_ptr_gain_index() and
 * btron_ptr_gain_label() -- are in src/graphics/dp_accel.c behind the same
 * predicate, because that is where the multiplier is applied and a control that
 * named its own numbers would be a second copy of the thing it dials.
 *
 * g_mouse_step_mult and g_mouse_accel_profile -- the two the radios used to set
 * below -- are deliberately NOT defined for this target: LAW 13d left them with
 * no reader on this machine, and a dial the pointer ignores is worse than no
 * dial.  Their absence is the gate, in the same way dp_ptr_riscos()'s is: a PS2
 * row that tries to set one fails at the link rather than surviving as scenery.
 * The Pi 400 keeps both, its curve and its radios, untouched -- and the three
 * declarations under #else are in the order they had before this lane, because
 * the arm64 image's .data layout is the control this change is checked against. */
int32_t g_ptr_gain_fp = 0;                     /* 0: auto, the source profile's own */
int      g_mouse_swap_select_adjust = 0;      /* 0: Right-handed (Select/Menu/Adjust), 1: Left-handed */
#else
int      g_mouse_step_mult          = 2;      /* Default: Step 2 (Archimedes / Haiku natural responsive standard) */
int      g_mouse_swap_select_adjust = 0;      /* 0: Right-handed (Select/Menu/Adjust), 1: Left-handed */
int      g_mouse_accel_profile     = 1;
#endif

typedef struct {
    WND *wnd;
    /* Keyboard Settings */
    BOOL kbd_repeat_enable;
    int  kbd_delay_sel;    /* 0: 16cs (160ms), 1: 32cs (320ms), 2: 50cs (500ms) */
    int  kbd_rate_sel;     /* 0: 40 cps, 1: 25 cps, 2: 12.5 cps */
    int  kbd_layout_sel;   /* 0: US ANSI, 1: Acorn UK, 2: Japanese JIS, 3: TRON-KBD */

    /* Mouse Settings */
#if defined(BTRON_PS2_TARGET)
    int  mouse_gain_sel;    /* index into btron_ptr_gain_fp[]; 0 = the source's own */
#else
    int  mouse_step_sel;    /* 0: Step 1, 1: Step 2 (RISC OS Default), 2: Step 3, 3: Step 4 */
    int  mouse_profile_sel; /* 0: RISC OS Stepped Curve, 1: Haiku Continuous Curve */
#endif
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

static void on_test_click(WND *wnd, SETTINGS_CONTROL *ctl) {
    (void)wnd; (void)ctl;
    uint32_t now_us = 0;
#if defined(_RPI_BCM283x_)
    extern uintptr_t g_mmio_base;
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
}

static SETTINGS_CONTROL s_input_controls[] = {
    /* Keyboard */
    { 0, SETTINGS_CTL_CHECKBOX, 0, 0, { 18, 64, 300, 82 }, "Enable Auto-Repeat (*FX 11/12)", &g_state_input.kbd_repeat_enable, NULL, &g_state_input.is_dirty, NULL },

    { 1, SETTINGS_CTL_RADIO, 1, 0, { 22, 106, 300, 122 }, "Short: 16cs (160ms) [RISC OS Fast]", NULL, &g_state_input.kbd_delay_sel, &g_state_input.is_dirty, NULL },
    { 2, SETTINGS_CTL_RADIO, 1, 1, { 22, 124, 300, 140 }, "Default: 32cs (320ms) [Acorn Archimedes]", NULL, &g_state_input.kbd_delay_sel, &g_state_input.is_dirty, NULL },
    { 3, SETTINGS_CTL_RADIO, 1, 2, { 22, 142, 300, 158 }, "Long: 50cs (500ms) [BBC Micro Standard]", NULL, &g_state_input.kbd_delay_sel, &g_state_input.is_dirty, NULL },

    { 4, SETTINGS_CTL_RADIO, 2, 0, { 22, 186, 300, 202 }, "Fast: 40 cps (25ms interval)", NULL, &g_state_input.kbd_rate_sel, &g_state_input.is_dirty, NULL },
    { 5, SETTINGS_CTL_RADIO, 2, 1, { 22, 204, 300, 220 }, "Standard: 25 cps (40ms interval)", NULL, &g_state_input.kbd_rate_sel, &g_state_input.is_dirty, NULL },
    { 6, SETTINGS_CTL_RADIO, 2, 2, { 22, 222, 300, 238 }, "Slow: 12.5 cps (80ms interval)", NULL, &g_state_input.kbd_rate_sel, &g_state_input.is_dirty, NULL },

    { 7, SETTINGS_CTL_RADIO, 3, 0, { 22, 266, 300, 282 }, "Standard US ANSI 101/104", NULL, &g_state_input.kbd_layout_sel, &g_state_input.is_dirty, NULL },
    { 8, SETTINGS_CTL_RADIO, 3, 1, { 22, 284, 300, 300 }, "Acorn British English (UK)", NULL, &g_state_input.kbd_layout_sel, &g_state_input.is_dirty, NULL },
    { 9, SETTINGS_CTL_RADIO, 3, 2, { 22, 302, 300, 318 }, "Japanese JIS 106 (OADG)", NULL, &g_state_input.kbd_layout_sel, &g_state_input.is_dirty, NULL },
    { 10, SETTINGS_CTL_RADIO, 3, 3, { 22, 320, 300, 338 }, "TRON Ergonomic Multilingual", NULL, &g_state_input.kbd_layout_sel, &g_state_input.is_dirty, NULL },

    /* Mouse */
#if !defined(BTRON_PS2_TARGET)
    { 11, SETTINGS_CTL_RADIO, 4, 0, { 336, 76, 620, 92 }, "Step 1: 1.0x Slow Precision", NULL, &g_state_input.mouse_step_sel, &g_state_input.is_dirty, NULL },
    { 12, SETTINGS_CTL_RADIO, 4, 1, { 336, 94, 620, 108 }, "Step 2: 2.0x Standard (RISC OS CMOS)", NULL, &g_state_input.mouse_step_sel, &g_state_input.is_dirty, NULL },
    { 13, SETTINGS_CTL_RADIO, 4, 2, { 336, 110, 620, 124 }, "Step 3: 3.0x Fast Sweep", NULL, &g_state_input.mouse_step_sel, &g_state_input.is_dirty, NULL },
    { 14, SETTINGS_CTL_RADIO, 4, 3, { 336, 126, 620, 142 }, "Step 4: 4.0x Ultra Velocity", NULL, &g_state_input.mouse_step_sel, &g_state_input.is_dirty, NULL },

    { 15, SETTINGS_CTL_RADIO, 5, 1, { 336, 160, 620, 176 }, "Haiku OS Continuous 2D (Smooth)", NULL, &g_state_input.mouse_profile_sel, &g_state_input.is_dirty, NULL },
    { 16, SETTINGS_CTL_RADIO, 5, 0, { 336, 177, 620, 194 }, "RISC OS Stepped (Archimedes)", NULL, &g_state_input.mouse_profile_sel, &g_state_input.is_dirty, NULL },
#endif

    { 17, SETTINGS_CTL_RADIO, 6, 0, { 336, 212, 620, 228 }, "Right-Hand: [Select] [Menu] [Adjust]", NULL, &g_state_input.mouse_handedness, &g_state_input.is_dirty, NULL },
    { 18, SETTINGS_CTL_RADIO, 6, 1, { 336, 229, 620, 246 }, "Left-Hand:  [Adjust] [Menu] [Select]", NULL, &g_state_input.mouse_handedness, &g_state_input.is_dirty, NULL },

    { 19, SETTINGS_CTL_RADIO, 7, 0, { 336, 264, 620, 280 }, "40 cs (400ms interval)", NULL, &g_state_input.dbl_click_sel, &g_state_input.is_dirty, NULL },
    { 20, SETTINGS_CTL_RADIO, 7, 1, { 336, 281, 620, 298 }, "50 cs (500ms [RISC OS default])", NULL, &g_state_input.dbl_click_sel, &g_state_input.is_dirty, NULL },

    { 21, SETTINGS_CTL_CUSTOM, 0, 0, { 334, 304, 618, 366 }, NULL, NULL, NULL, &g_state_input.is_dirty, on_test_click },
};

#if defined(BTRON_PS2_TARGET)
static SETTINGS_CONTROL s_input_ps2_controls[BTRON_PTR_GAIN_STEPS];
static void init_ps2_input_controls(void) {
    for (int i = 0; i < BTRON_PTR_GAIN_STEPS; i++) {
        H top = (H)(76 + i * 16);
        s_input_ps2_controls[i] = (SETTINGS_CONTROL){
            .id = 100 + i,
            .type = SETTINGS_CTL_RADIO,
            .radio_group = 8,
            .radio_val = i,
            .rect = { 336, top, 620, (H)(top + 15) },
            .bound_int = &g_state_input.mouse_gain_sel,
            .dirty_flag = &g_state_input.is_dirty
        };
    }
}
#endif

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
#if defined(BTRON_PS2_TARGET)
    /* LAW 13e: the one multiplier this port's pointer reads.  The core sees the
     * new value on the next pass, clears the subpixel remainder the old gain
     * made and prints the row -- src/cores/core_ps2.c's ps2_ptr_gain_sync(). */
    g_ptr_gain_fp = btron_ptr_gain_fp[g_state_input.mouse_gain_sel];
#else
    g_mouse_step_mult = g_state_input.mouse_step_sel + 1; /* 1, 2, 3, 4 */
#endif

    /* Select / Adjust handedness */
    g_mouse_swap_select_adjust = g_state_input.mouse_handedness;

    /* Mouse Acceleration Profile (0 = RISC OS, 1 = Haiku) */
#if !defined(BTRON_PS2_TARGET)
    g_mouse_accel_profile = g_state_input.mouse_profile_sel;
#endif

    g_state_input.is_dirty = FALSE;
}

static void set_risc_os_defaults(void) {
    g_state_input.kbd_repeat_enable  = TRUE;
    g_state_input.kbd_delay_sel      = 0;     /* 16cs (160ms) RISC OS Fast */
    g_state_input.kbd_rate_sel       = 0;     /* 40 cps Fast */
    g_state_input.kbd_layout_sel     = 0;     /* US ANSI */

#if defined(BTRON_PS2_TARGET)
    g_state_input.mouse_gain_sel     = 0;     /* Auto: this source profile's own gain */
#else
    g_state_input.mouse_step_sel     = 0;     /* Step 1: 1.0x Precision */
    g_state_input.mouse_profile_sel  = 1;     /* Haiku OS Continuous (1) or RISC OS (0) */
#endif
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
#if defined(BTRON_PS2_TARGET)
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) — Pointer Gain & Keys", "Input", "入力環境");
#else
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) — RISC OS & Haiku Input", "Input", "入力環境");
#endif
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);

    /* ═══════════════════════════════════════════════════════════════════
     * Section 1: Keyboard Configuration (Left Panel)
     * ═══════════════════════════════════════════════════════════════════ */
    RECT s1 = { 10, 48, 314, 380 };
    fill_rec(dev, &s1, COLOR_WHITE);
    drw_rec(dev, &s1);
    drw_tc_string(dev, 16, 40, " [1. Keyboard Configuration (RISC OS)] ", COLOR_NAVY, COLOR_WHITE);

    settings_draw_ui_checkbox(dev, 18, 64, "Enable Auto-Repeat (*FX 11/12)", g_state_input.kbd_repeat_enable, FALSE);

    drw_tc_string(dev, 18, 88, "Auto-Repeat Delay (*FX 11):", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 22, 106, "Short: 16cs (160ms) [RISC OS Fast]", g_state_input.kbd_delay_sel == 0, FALSE);
    settings_draw_ui_radio(dev, 22, 124, "Default: 32cs (320ms) [Acorn Archimedes]", g_state_input.kbd_delay_sel == 1, FALSE);
    settings_draw_ui_radio(dev, 22, 142, "Long: 50cs (500ms) [BBC Micro Standard]", g_state_input.kbd_delay_sel == 2, FALSE);

    drw_tc_string(dev, 18, 168, "Auto-Repeat Rate (*FX 12):", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 22, 186, "Fast: 40 cps (25ms interval)", g_state_input.kbd_rate_sel == 0, FALSE);
    settings_draw_ui_radio(dev, 22, 204, "Standard: 25 cps (40ms interval)", g_state_input.kbd_rate_sel == 1, FALSE);
    settings_draw_ui_radio(dev, 22, 222, "Slow: 12.5 cps (80ms interval)", g_state_input.kbd_rate_sel == 2, FALSE);

    drw_tc_string(dev, 18, 248, "Keyboard Layout (*Configure Country):", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 22, 266, "Standard US ANSI 101/104", g_state_input.kbd_layout_sel == 0, FALSE);
    settings_draw_ui_radio(dev, 22, 284, "Acorn British English (UK)", g_state_input.kbd_layout_sel == 1, FALSE);
    settings_draw_ui_radio(dev, 22, 302, "Japanese JIS 106 (OADG)", g_state_input.kbd_layout_sel == 2, FALSE);
    settings_draw_ui_radio(dev, 22, 320, "TRON Ergonomic Multilingual", g_state_input.kbd_layout_sel == 3, FALSE);

    /* ═══════════════════════════════════════════════════════════════════
     * Section 2: Mouse Configuration (Right Panel)
     * ═══════════════════════════════════════════════════════════════════ */
    RECT s2 = { 324, 48, 628, 380 };
    fill_rec(dev, &s2, COLOR_WHITE);
    drw_rec(dev, &s2);
#if defined(BTRON_PS2_TARGET)
    drw_tc_string(dev, 330, 40, " [2. Pointer Configuration (proportional)] ", COLOR_NAVY, COLOR_WHITE);
#else
    drw_tc_string(dev, 330, 40, " [2. Mouse Configuration (Haiku & RISC OS)] ", COLOR_NAVY, COLOR_WHITE);
#endif

#if defined(BTRON_PS2_TARGET)
    drw_tc_string(dev, 332, 62, "Pointer Gain (this port has no curve):",
                  COLOR_NAVY, COLOR_WHITE);
    for (int i = 0; i < BTRON_PTR_GAIN_STEPS; i++)
        settings_draw_ui_radio(dev, 336, (H)(78 + i * 16), btron_ptr_gain_label(i),
                               g_state_input.mouse_gain_sel == i, FALSE);
#else
    drw_tc_string(dev, 332, 62, "Tracking Speed (*Configure MouseStep):", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 336, 78, "Step 1: 1.0x Slow Precision", g_state_input.mouse_step_sel == 0, FALSE);
    settings_draw_ui_radio(dev, 336, 94, "Step 2: 2.0x Standard (RISC OS CMOS)", g_state_input.mouse_step_sel == 1, FALSE);
    settings_draw_ui_radio(dev, 336, 110, "Step 3: 3.0x Fast Sweep", g_state_input.mouse_step_sel == 2, FALSE);
    settings_draw_ui_radio(dev, 336, 126, "Step 4: 4.0x Ultra Velocity", g_state_input.mouse_step_sel == 3, FALSE);

    drw_tc_string(dev, 332, 146, "Acceleration Engine Profile:", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 336, 162, "Haiku OS Continuous 2D (Smooth)", g_state_input.mouse_profile_sel == 1, FALSE);
    settings_draw_ui_radio(dev, 336, 178, "RISC OS Stepped (Archimedes)", g_state_input.mouse_profile_sel == 0, FALSE);
#endif

    drw_tc_string(dev, 332, 198, "Three-Button Model (Select, Menu, Adjust):", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 336, 214, "Right-Hand: [Select] [Menu] [Adjust]", g_state_input.mouse_handedness == 0, FALSE);
    settings_draw_ui_radio(dev, 336, 230, "Left-Hand:  [Adjust] [Menu] [Select]", g_state_input.mouse_handedness == 1, FALSE);

    drw_tc_string(dev, 332, 250, "Double-Click Threshold:", COLOR_NAVY, COLOR_WHITE);
    settings_draw_ui_radio(dev, 336, 266, "40 cs (400ms interval)", g_state_input.dbl_click_sel == 0, FALSE);
    settings_draw_ui_radio(dev, 336, 282, "50 cs (500ms [RISC OS default])", g_state_input.dbl_click_sel == 1, FALSE);

    /* Interactive Double-Click Test Area */
    RECT test_box = { 334, 304, 618, 366 };
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
#if defined(BTRON_PS2_TARGET)
    settings_draw_ui_button(dev, dev->width - 340, btn_y, 140, 26, "Defaults", FALSE);
#else
    settings_draw_ui_button(dev, dev->width - 340, btn_y, 140, 26, "Default (RISC OS)", FALSE);
#endif
    settings_draw_ui_button(dev, dev->width - 185, btn_y, 85, 26, "Apply", g_state_input.is_dirty);
    settings_draw_ui_button(dev, dev->width - 85, btn_y, 75, 26, "Close", FALSE);
}

static void handle_input_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        if (settings_dispatch_control_event(wnd, s_input_controls, (int)(sizeof(s_input_controls)/sizeof(s_input_controls[0])), evt)) return;
#if defined(BTRON_PS2_TARGET)
        if (settings_dispatch_control_event(wnd, s_input_ps2_controls, BTRON_PTR_GAIN_STEPS, evt)) return;
#endif

        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H client_h = wnd->client.bottom - wnd->client.top;
        H btn_y = client_h - 36;

        if (rel_y >= btn_y && rel_y <= btn_y + 26) {
            /* Default (RISC OS / Haiku) */
            if (rel_x >= client_w - 340 && rel_x <= client_w - 200) {
                set_risc_os_defaults();
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            }
            /* Apply */
            if (rel_x >= client_w - 185 && rel_x <= client_w - 100) {
                apply_input_settings_to_kernel();
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            }
            /* Close */
            if (rel_x >= client_w - 85 && rel_x <= client_w - 10) {
                g_state_input.wnd = NULL;
                cls_wnd(wnd);
                return;
            }
        }
    }
}

WND* open_input_settings_window(void) {
    if (g_state_input.wnd) {
        for (WND *c = get_wnd_list(); c; c = c->next) {
            if (c == g_state_input.wnd) {
                return g_state_input.wnd;
            }
        }
        g_state_input.wnd = NULL;
    }

    /* Sync initial state from live kernel globals */
    g_state_input.kbd_repeat_enable  = g_kbd_repeat_enabled;
    g_state_input.kbd_delay_sel      = (g_kbd_repeat_delay_us <= 160000U) ? 0 :
                                       ((g_kbd_repeat_delay_us <= 320000U) ? 1 : 2);
    g_state_input.kbd_rate_sel       = (g_kbd_repeat_interval_us <= 25000U) ? 0 :
                                       ((g_kbd_repeat_interval_us <= 40000U) ? 1 : 2);
    g_state_input.kbd_layout_sel     = 0;

#if defined(BTRON_PS2_TARGET)
    init_ps2_input_controls();
    g_state_input.mouse_gain_sel     = btron_ptr_gain_index(g_ptr_gain_fp);
#else
    g_state_input.mouse_step_sel     = (g_mouse_step_mult >= 1 && g_mouse_step_mult <= 4) ?
                                       (g_mouse_step_mult - 1) : 0;
    g_state_input.mouse_profile_sel  = g_mouse_accel_profile;
#endif
    g_state_input.mouse_accel_enable = TRUE;
    g_state_input.mouse_handedness   = g_mouse_swap_select_adjust ? 1 : 0;
    g_state_input.dbl_click_sel      = 1; /* 50cs (500ms) RISC OS default */

#if defined(BTRON_PS2_TARGET)
    WND *wnd = opn_wnd("Input & Pointer (入力環境) — Pointer Gain & Buttons",
#else
    WND *wnd = opn_wnd("Input & Pointer (入力環境) — RISC OS & Haiku Options",
#endif
                       80, 45, 640, 430,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;
    g_state_input.wnd = wnd;
    wnd->user_data = (VW)(uintptr_t)&g_state_input;
    wnd->paint = paint_input_settings;
    wnd->event_handler = handle_input_event;
    return wnd;
}
