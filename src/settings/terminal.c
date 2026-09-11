/*
 * B-System (BTRON 3.20) Settings Applet: terminal (Terminal / 端末・通信)
 * Conforming to ./b-system/settings/Terminal.html and B-right/V / Chokanji specs.
 */

#include <btron/settings.h>
#include <btron/terminal_settings.h>
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

static TERMINAL_SETTINGS g_term_settings = {
    .theme            = TERM_THEME_WHITE,
    .fg_color         = 0xFFFFFFFF,
    .bg_color         = 0xCC000000,
    .font_size        = TERM_FONT_16,
    .scrollback_lines = 300,
    .cursor_style     = TERM_CURSOR_UNDERLINE,
    .transparency     = TERM_TRANSPARENCY_80
};

COLOR terminal_get_effective_bg(const TERMINAL_SETTINGS *st) {
    if (!st) return 0xCC000000;
    uint32_t alpha = 0xCC;
    if (st->transparency == TERM_TRANSPARENCY_OPAQUE) alpha = 0xFF;
    else if (st->transparency == TERM_TRANSPARENCY_60) alpha = 0x99;

    uint32_t base_rgb = 0x000000;
    if (st->theme == TERM_THEME_CYAN)  base_rgb = 0x0A1120;
    else if (st->theme == TERM_THEME_LIGHT) base_rgb = 0xE2E8F0;

    return (alpha << 24) | (base_rgb & 0x00FFFFFF);
}

void terminal_get_settings(TERMINAL_SETTINGS *out) {
    if (!out) return;
    *out = g_term_settings;
}

void terminal_set_settings(const TERMINAL_SETTINGS *in) {
    if (!in) return;
    g_term_settings = *in;
    g_term_settings.bg_color = terminal_get_effective_bg(&g_term_settings);
}

void terminal_reset_settings(void) {
    g_term_settings.theme = TERM_THEME_WHITE;
    g_term_settings.fg_color = 0xFFFFFFFF;
    g_term_settings.bg_color = 0xCC000000;
    g_term_settings.font_size = TERM_FONT_16;
    g_term_settings.scrollback_lines = 300;
    g_term_settings.cursor_style = TERM_CURSOR_UNDERLINE;
    g_term_settings.transparency = TERM_TRANSPARENCY_80;
}

typedef struct {
    WND *wnd;
    TERMINAL_SETTINGS local_cfg;
    BOOL is_dirty;
} AppletState_terminal;

static AppletState_terminal g_state_terminal;

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

static void paint_terminal_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* ── Header Bar: icon is ALWAYS 32x32 ────────────────────────── */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "terminal", 6, 4, 32, 32);
    drw_tc_string(dev, 46, 6,  "Terminal Settings – 端末・通信環境設定",           COLOR_BLACK, COLOR_LTGRAY);
    drw_tc_string(dev, 46, 22, "B-System Workstation Console, VT100 Emulator & Colours", COLOR_DKGRAY, COLOR_LTGRAY);

    TERMINAL_SETTINGS *cfg = &g_state_terminal.local_cfg;

    /* Layout constants */
    H M    = 12;          /* outer horizontal margin */
    H P    = 10;          /* inner padding inside group box */
    H LH   = 20;          /* radio row height */
    H GAP  = 14;          /* vertical gap between sections */
    H HDR_H = 40;         /* header bar height */
    H LBL_OVERLAP = 8;    /* how many px the label sits above the box top */

    /* ── Section 1: 配色テーマ (Colour Theme) ─────────────────── */
    H s1_top = HDR_H + GAP + LBL_OVERLAP;   /* 64 */
    H s1_bot = s1_top + P + LH * 3 + P;     /* 64 + 10 + 60 + 10 = 144 */
    RECT sec1 = { M, s1_top, dev->width - M, s1_bot };
    fill_rec(dev, &sec1, COLOR_WHITE);
    drw_rec(dev, &sec1);
    drw_tc_string(dev, M + P - 2, s1_top - LBL_OVERLAP, " [1] 配色テーマ (Colour Theme) ", COLOR_NAVY, COLOR_WHITE);

    paint_ui_radio(dev, M + P, s1_top + P,            "グリーン (Matrix Green)",  cfg->theme == TERM_THEME_GREEN, FALSE);
    paint_ui_radio(dev, dev->width / 2, s1_top + P,   "アンバー (Amber CRT)",     cfg->theme == TERM_THEME_AMBER, FALSE);
    paint_ui_radio(dev, M + P, s1_top + P + LH,       "白黒 (White on Black)",    cfg->theme == TERM_THEME_WHITE, FALSE);
    paint_ui_radio(dev, dev->width / 2, s1_top + P + LH, "BTRON青 (Cyan on Navy)",cfg->theme == TERM_THEME_CYAN,  FALSE);
    paint_ui_radio(dev, M + P, s1_top + P + LH * 2,   "ライト (Paper Light)",     cfg->theme == TERM_THEME_LIGHT, FALSE);

    /* ── Section 2: 文字サイズ (Font Size) ────────────────────── */
    H s2_top = s1_bot + GAP + LBL_OVERLAP;  /* 144 + 14 + 8 = 166 */
    H s2_bot = s2_top + P + LH + P;         /* 166 + 10 + 20 + 10 = 206 */
    RECT sec2 = { M, s2_top, dev->width - M, s2_bot };
    fill_rec(dev, &sec2, COLOR_WHITE);
    drw_rec(dev, &sec2);
    drw_tc_string(dev, M + P - 2, s2_top - LBL_OVERLAP, " [2] 文字・行間 (Font Size) ", COLOR_NAVY, COLOR_WHITE);

    H col3 = (dev->width - 2 * M - 2 * P) / 3;  /* third-column step */
    paint_ui_radio(dev, M + P,             s2_top + P, "小 12px (32行×100桁)",  cfg->font_size == TERM_FONT_12, FALSE);
    paint_ui_radio(dev, M + P + col3,      s2_top + P, "標準 16px (24行×80桁)", cfg->font_size == TERM_FONT_16, FALSE);
    paint_ui_radio(dev, M + P + col3 * 2,  s2_top + P, "大 20px (18行×64桁)",  cfg->font_size == TERM_FONT_20, FALSE);

    /* ── Section 3: スクロール履歴・カーソル (Scrollback & Cursor) ─────── */
    H s3_top = s2_bot + GAP + LBL_OVERLAP;  /* 206 + 14 + 8 = 228 */
    H s3_bot = s3_top + P + LH * 2 + P;     /* 228 + 10 + 40 + 10 = 288 */
    RECT sec3 = { M, s3_top, dev->width - M, s3_bot };
    fill_rec(dev, &sec3, COLOR_WHITE);
    drw_rec(dev, &sec3);
    drw_tc_string(dev, M + P - 2, s3_top - LBL_OVERLAP, " [3] 履歴・カーソル (Scrollback & Cursor) ", COLOR_NAVY, COLOR_WHITE);

    H col5 = (dev->width - 2 * M - 2 * P) / 5;  /* fifth-column step */
    paint_ui_radio(dev, M + P,             s3_top + P,       "履歴 100行",           cfg->scrollback_lines == 100,   FALSE);
    paint_ui_radio(dev, M + P + col5,      s3_top + P,       "履歴 300行 (標準)",    cfg->scrollback_lines == 300,   FALSE);
    paint_ui_radio(dev, M + P + col5 * 2,  s3_top + P,       "履歴 1000行 (大)",    cfg->scrollback_lines == 1000,  FALSE);
    paint_ui_radio(dev, M + P + col5 * 3,  s3_top + P,       "履歴 4096行",          cfg->scrollback_lines == 4096,  FALSE);
    paint_ui_radio(dev, M + P + col5 * 4,  s3_top + P,       "履歴 10K行 (最大)",    cfg->scrollback_lines == 10000, FALSE);
    paint_ui_radio(dev, M + P,             s3_top + P + LH,  "カーソル下線 (_)",    cfg->cursor_style == TERM_CURSOR_UNDERLINE, FALSE);
    paint_ui_radio(dev, M + P + col3,      s3_top + P + LH,  "ブロックカーソル (█)",cfg->cursor_style == TERM_CURSOR_BLOCK,     FALSE);
    paint_ui_radio(dev, M + P + col3 * 2,  s3_top + P + LH,  "バーカーソル (|)",    cfg->cursor_style == TERM_CURSOR_BAR,       FALSE);

    /* ── Section 4: 画面透過度 (Transparency) ──────────────────── */
    H s4_top = s3_bot + GAP + LBL_OVERLAP;  /* 288 + 14 + 8 = 310 */
    H s4_bot = s4_top + P + LH + P;         /* 310 + 10 + 20 + 10 = 350 */
    RECT sec4 = { M, s4_top, dev->width - M, s4_bot };
    fill_rec(dev, &sec4, COLOR_WHITE);
    drw_rec(dev, &sec4);
    drw_tc_string(dev, M + P - 2, s4_top - LBL_OVERLAP, " [4] 背景透過・減光 (Background Transparency) ", COLOR_NAVY, COLOR_WHITE);

    paint_ui_radio(dev, M + P,             s4_top + P, "不透明 (100% Opaque)",  cfg->transparency == TERM_TRANSPARENCY_OPAQUE, FALSE);
    paint_ui_radio(dev, M + P + col3,      s4_top + P, "20%減光 (80% Dimmed)",  cfg->transparency == TERM_TRANSPARENCY_80,     FALSE);
    paint_ui_radio(dev, M + P + col3 * 2,  s4_top + P, "40%減光 (60% Dimmed)",  cfg->transparency == TERM_TRANSPARENCY_60,     FALSE);

    /* ── Action Buttons ─────────────────────────────────────── */
    paint_ui_button(dev, dev->width - 240, dev->height - 36, 110, 26, "標準に戻す",    FALSE);
    paint_ui_button(dev, dev->width - 122, dev->height - 36, 110, 26, "適用 (Apply)",  FALSE);
}

static void handle_terminal_settings_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        TERMINAL_SETTINGS *cfg = &g_state_terminal.local_cfg;

        /* Layout mirrors paint function (same constants) */
        H M = 12, P = 10, LH = 20, GAP = 14, HDR_H = 40, LBL_OVERLAP = 8;
        H client_w = wnd->client.right - wnd->client.left;
        H col3 = (client_w - 2 * M - 2 * P) / 3;

        /* Section 1 row y-ranges */
        H s1_top = HDR_H + GAP + LBL_OVERLAP;             /* 64 */
        H s1r0   = s1_top + P;                             /* 74  — row 0 (Green/Amber) */
        H s1r1   = s1_top + P + LH;                        /* 94  — row 1 (White/Cyan) */
        H s1r2   = s1_top + P + LH * 2;                    /* 114 — row 2 (Light) */
        H s1_bot = s1_top + P + LH * 3 + P;               /* 144 */

        /* Section 2 row y-ranges */
        H s2_top = s1_bot + GAP + LBL_OVERLAP;             /* 166 */
        H s2r0   = s2_top + P;                             /* 176 */
        H s2_bot = s2_top + P + LH + P;                   /* 206 */

        /* Section 3 row y-ranges */
        H s3_top = s2_bot + GAP + LBL_OVERLAP;             /* 228 */
        H s3r0   = s3_top + P;                             /* 238 — scrollback */
        H s3r1   = s3_top + P + LH;                        /* 258 — cursor */
        H s3_bot = s3_top + P + LH * 2 + P;               /* 288 */

        /* Section 4 row y-ranges */
        H s4_top = s3_bot + GAP + LBL_OVERLAP;             /* 310 */
        H s4r0   = s4_top + P;                             /* 320 */

        /* Column x-ranges (3 equal columns) */
        H c0x = M + P;
        H c1x = M + P + col3;
        H c2x = M + P + col3 * 2;
        H half = client_w / 2;  /* Section 1 uses half-width split */

#define HIT_ROW(rel_y, row_y)    ((rel_y) >= (row_y) && (rel_y) < (row_y) + LH)
#define REPAINT() do { g_state_terminal.is_dirty = TRUE; wnd->paint(wnd, wnd->dev); } while(0)

        /* ── Section 1: Colour Theme ──────────────────────────────── */
        if (HIT_ROW(rel_y, s1r0)) {
            if      (rel_x >= c0x && rel_x < half) { cfg->theme = TERM_THEME_GREEN; cfg->fg_color = 0xFF22C55E; REPAINT(); }
            else if (rel_x >= half)                 { cfg->theme = TERM_THEME_AMBER; cfg->fg_color = 0xFFF59E0B; REPAINT(); }
        } else if (HIT_ROW(rel_y, s1r1)) {
            if      (rel_x >= c0x && rel_x < half) { cfg->theme = TERM_THEME_WHITE; cfg->fg_color = 0xFFFFFFFF; REPAINT(); }
            else if (rel_x >= half)                 { cfg->theme = TERM_THEME_CYAN;  cfg->fg_color = 0xFF38BDF8; REPAINT(); }
        } else if (HIT_ROW(rel_y, s1r2)) {
            if      (rel_x >= c0x && rel_x < half) { cfg->theme = TERM_THEME_LIGHT; cfg->fg_color = 0xFF0F172A; REPAINT(); }
        }

        /* ── Section 2: Font Size ──────────────────────────────────── */
        if (HIT_ROW(rel_y, s2r0)) {
            if      (rel_x >= c0x && rel_x < c1x) { cfg->font_size = TERM_FONT_12; REPAINT(); }
            else if (rel_x >= c1x && rel_x < c2x) { cfg->font_size = TERM_FONT_16; REPAINT(); }
            else if (rel_x >= c2x)                 { cfg->font_size = TERM_FONT_20; REPAINT(); }
        }

        /* ── Section 3: Scrollback & Cursor ───────────────────────── */
        if (HIT_ROW(rel_y, s3r0)) {
            H col5 = (client_w - 2 * M - 2 * P) / 5;
            if      (rel_x >= c0x && rel_x < c0x + col5)            { cfg->scrollback_lines = 100;   REPAINT(); }
            else if (rel_x >= c0x + col5 && rel_x < c0x + col5 * 2)     { cfg->scrollback_lines = 300;   REPAINT(); }
            else if (rel_x >= c0x + col5 * 2 && rel_x < c0x + col5 * 3) { cfg->scrollback_lines = 1000;  REPAINT(); }
            else if (rel_x >= c0x + col5 * 3 && rel_x < c0x + col5 * 4) { cfg->scrollback_lines = 4096;  REPAINT(); }
            else if (rel_x >= c0x + col5 * 4)                        { cfg->scrollback_lines = 10000; REPAINT(); }
        } else if (HIT_ROW(rel_y, s3r1)) {
            if      (rel_x >= c0x && rel_x < c1x) { cfg->cursor_style = TERM_CURSOR_UNDERLINE; REPAINT(); }
            else if (rel_x >= c1x && rel_x < c2x) { cfg->cursor_style = TERM_CURSOR_BLOCK;     REPAINT(); }
            else if (rel_x >= c2x)                 { cfg->cursor_style = TERM_CURSOR_BAR;       REPAINT(); }
        }

        /* ── Section 4: Transparency ───────────────────────────────── */
        if (HIT_ROW(rel_y, s4r0)) {
            if      (rel_x >= c0x && rel_x < c1x) { cfg->transparency = TERM_TRANSPARENCY_OPAQUE; REPAINT(); }
            else if (rel_x >= c1x && rel_x < c2x) { cfg->transparency = TERM_TRANSPARENCY_80;     REPAINT(); }
            else if (rel_x >= c2x)                 { cfg->transparency = TERM_TRANSPARENCY_60;     REPAINT(); }
        }

        /* ── Buttons ────────────────────────────────────────────────── */
        H btn_y = (wnd->client.bottom - wnd->client.top) - 36;
        if (rel_y >= btn_y && rel_y <= btn_y + 26) {
            if (rel_x >= client_w - 240 && rel_x < client_w - 130) {
                terminal_reset_settings();
                terminal_get_settings(&g_state_terminal.local_cfg);
                g_state_terminal.is_dirty = FALSE;
                wnd->paint(wnd, wnd->dev);
            } else if (rel_x >= client_w - 122 && rel_x < client_w - 12) {
                terminal_set_settings(&g_state_terminal.local_cfg);
                g_state_terminal.is_dirty = FALSE;
                cls_wnd(wnd);
            }
        }

#undef HIT_ROW
#undef REPAINT
    }
}

WND* open_terminal_settings_window(void) {
    terminal_get_settings(&g_state_terminal.local_cfg);
    g_state_terminal.is_dirty = FALSE;

    H win_w = 540;
    H win_h = 450;   /* 40px header + 4 sections×(~80px) + 14px gaps×3 + 36px buttons + margins */
    WND *wnd = opn_wnd("端末・通信環境設定 (Terminal Settings)",
                       (1280 - win_w) / 2 + 20, (800 - win_h) / 2 + 20, win_w, win_h,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (!wnd) return NULL;

    g_state_terminal.wnd = wnd;
    wnd->paint = paint_terminal_settings;
    wnd->event_handler = handle_terminal_settings_event;
    return wnd;
}
