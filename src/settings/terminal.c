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

#include <btron/libc_shim.h>

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

static void on_term_theme_green(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_GREEN; g_state_terminal.local_cfg.fg_color = 0xFF22C55E; }
static void on_term_theme_amber(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_AMBER; g_state_terminal.local_cfg.fg_color = 0xFFF59E0B; }
static void on_term_theme_white(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_WHITE; g_state_terminal.local_cfg.fg_color = 0xFFFFFFFF; }
static void on_term_theme_cyan(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_CYAN;  g_state_terminal.local_cfg.fg_color = 0xFF38BDF8; }
static void on_term_theme_light(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.theme = TERM_THEME_LIGHT; g_state_terminal.local_cfg.fg_color = 0xFF0F172A; }

static void on_term_font_12(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_12; }
static void on_term_font_16(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_16; }
static void on_term_font_20(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.font_size = TERM_FONT_20; }

static void on_term_scroll_100k(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 100000; }
static void on_term_scroll_300(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 300; }
static void on_term_scroll_1000(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 1000; }
static void on_term_scroll_4096(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 4096; }
static void on_term_scroll_10k(WND *wnd, SETTINGS_CONTROL *ctl)  { (void)wnd; (void)ctl; g_state_terminal.local_cfg.scrollback_lines = 10000; }

static void on_term_cur_underline(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_UNDERLINE; }
static void on_term_cur_block(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_BLOCK; }
static void on_term_cur_bar(WND *wnd, SETTINGS_CONTROL *ctl)       { (void)wnd; (void)ctl; g_state_terminal.local_cfg.cursor_style = TERM_CURSOR_BAR; }

static void on_term_trans_opaque(WND *wnd, SETTINGS_CONTROL *ctl) { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_OPAQUE; }
static void on_term_trans_80(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_80; }
static void on_term_trans_60(WND *wnd, SETTINGS_CONTROL *ctl)     { (void)wnd; (void)ctl; g_state_terminal.local_cfg.transparency = TERM_TRANSPARENCY_60; }

static SETTINGS_CONTROL s_term_controls[] = {
    /* Section 1: Themes */
    { 0, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 74, 270, 94 }, "グリーン (Matrix Green)", NULL, NULL, &g_state_terminal.is_dirty, on_term_theme_green },
    { 1, SETTINGS_CTL_CUSTOM, 0, 0, { 270, 74, 528, 94 }, "アンバー (Amber CRT)", NULL, NULL, &g_state_terminal.is_dirty, on_term_theme_amber },
    { 2, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 94, 270, 114 }, "白黒 (White on Black)", NULL, NULL, &g_state_terminal.is_dirty, on_term_theme_white },
    { 3, SETTINGS_CTL_CUSTOM, 0, 0, { 270, 94, 528, 114 }, "BTRON青 (Cyan on Navy)", NULL, NULL, &g_state_terminal.is_dirty, on_term_theme_cyan },
    { 4, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 114, 270, 134 }, "ライト (Paper Light)", NULL, NULL, &g_state_terminal.is_dirty, on_term_theme_light },

    /* Section 2: Font Size */
    { 5, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 176, 187, 196 }, "小 12px (32行×100桁)", NULL, NULL, &g_state_terminal.is_dirty, on_term_font_12 },
    { 6, SETTINGS_CTL_CUSTOM, 0, 0, { 187, 176, 352, 196 }, "標準 16px (24行×80桁)", NULL, NULL, &g_state_terminal.is_dirty, on_term_font_16 },
    { 7, SETTINGS_CTL_CUSTOM, 0, 0, { 352, 176, 528, 196 }, "大 20px (18行×64桁)", NULL, NULL, &g_state_terminal.is_dirty, on_term_font_20 },

    /* Section 3: Scrollback & Cursor */
    { 8, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 238, 121, 258 }, "履歴 100K行", NULL, NULL, &g_state_terminal.is_dirty, on_term_scroll_100k },
    { 9, SETTINGS_CTL_CUSTOM, 0, 0, { 121, 238, 220, 258 }, "履歴 300行 (標準)", NULL, NULL, &g_state_terminal.is_dirty, on_term_scroll_300 },
    { 10, SETTINGS_CTL_CUSTOM, 0, 0, { 220, 238, 319, 258 }, "履歴 1000行 (大)", NULL, NULL, &g_state_terminal.is_dirty, on_term_scroll_1000 },
    { 11, SETTINGS_CTL_CUSTOM, 0, 0, { 319, 238, 418, 258 }, "履歴 4096行", NULL, NULL, &g_state_terminal.is_dirty, on_term_scroll_4096 },
    { 12, SETTINGS_CTL_CUSTOM, 0, 0, { 418, 238, 528, 258 }, "履歴 10K行 (最大)", NULL, NULL, &g_state_terminal.is_dirty, on_term_scroll_10k },

    { 13, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 258, 187, 278 }, "カーソル下線 (_)", NULL, NULL, &g_state_terminal.is_dirty, on_term_cur_underline },
    { 14, SETTINGS_CTL_CUSTOM, 0, 0, { 187, 258, 352, 278 }, "ブロックカーソル (█)", NULL, NULL, &g_state_terminal.is_dirty, on_term_cur_block },
    { 15, SETTINGS_CTL_CUSTOM, 0, 0, { 352, 258, 528, 278 }, "バーカーソル (|)", NULL, NULL, &g_state_terminal.is_dirty, on_term_cur_bar },

    /* Section 4: Transparency */
    { 16, SETTINGS_CTL_CUSTOM, 0, 0, { 22, 320, 187, 340 }, "不透明 (100% Opaque)", NULL, NULL, &g_state_terminal.is_dirty, on_term_trans_opaque },
    { 17, SETTINGS_CTL_CUSTOM, 0, 0, { 187, 320, 352, 340 }, "20%減光 (80% Dimmed)", NULL, NULL, &g_state_terminal.is_dirty, on_term_trans_80 },
    { 18, SETTINGS_CTL_CUSTOM, 0, 0, { 352, 320, 528, 340 }, "40%減光 (60% Dimmed)", NULL, NULL, &g_state_terminal.is_dirty, on_term_trans_60 },
};

static void paint_terminal_settings(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;

    /* Background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    draw_setting_gif_icon_scaled(dev, "terminal", 6, 4, 32, 32);
    drw_tc_string(dev, 46, 6,  "Terminal Settings – 端末・通信環境設定",           COLOR_BLACK, COLOR_LTGRAY);
    drw_tc_string(dev, 46, 22, "B-System Workstation Console, VT100 Emulator & Colours", COLOR_DKGRAY, COLOR_LTGRAY);

    TERMINAL_SETTINGS *cfg = &g_state_terminal.local_cfg;

    /* Layout constants */
    H M    = 12;
    H P    = 10;
    H LH   = 20;
    H GAP  = 14;
    H HDR_H = 40;
    H LBL_OVERLAP = 8;

    /* ── Section 1: 配色テーマ (Colour Theme) ─────────────────── */
    H s1_top = HDR_H + GAP + LBL_OVERLAP;   /* 64 */
    H s1_bot = s1_top + P + LH * 3 + P;     /* 144 */
    RECT sec1 = { M, s1_top, dev->width - M, s1_bot };
    settings_draw_section(dev, &sec1, " [1] 配色テーマ (Colour Theme) ");

    settings_draw_ui_radio(dev, M + P, s1_top + P,            "グリーン (Matrix Green)",  cfg->theme == TERM_THEME_GREEN, FALSE);
    settings_draw_ui_radio(dev, dev->width / 2, s1_top + P,   "アンバー (Amber CRT)",     cfg->theme == TERM_THEME_AMBER, FALSE);
    settings_draw_ui_radio(dev, M + P, s1_top + P + LH,       "白黒 (White on Black)",    cfg->theme == TERM_THEME_WHITE, FALSE);
    settings_draw_ui_radio(dev, dev->width / 2, s1_top + P + LH, "BTRON青 (Cyan on Navy)",cfg->theme == TERM_THEME_CYAN,  FALSE);
    settings_draw_ui_radio(dev, M + P, s1_top + P + LH * 2,   "ライト (Paper Light)",     cfg->theme == TERM_THEME_LIGHT, FALSE);

    /* ── Section 2: 文字サイズ (Font Size) ────────────────────── */
    H s2_top = s1_bot + GAP + LBL_OVERLAP;  /* 166 */
    H s2_bot = s2_top + P + LH + P;         /* 206 */
    RECT sec2 = { M, s2_top, dev->width - M, s2_bot };
    settings_draw_section(dev, &sec2, " [2] 文字・行間 (Font Size) ");

    H col3 = (dev->width - 2 * M - 2 * P) / 3;
    settings_draw_ui_radio(dev, M + P,             s2_top + P, "小 12px (32行×100桁)",  cfg->font_size == TERM_FONT_12, FALSE);
    settings_draw_ui_radio(dev, M + P + col3,      s2_top + P, "標準 16px (24行×80桁)", cfg->font_size == TERM_FONT_16, FALSE);
    settings_draw_ui_radio(dev, M + P + col3 * 2,  s2_top + P, "大 20px (18行×64桁)",  cfg->font_size == TERM_FONT_20, FALSE);

    /* ── Section 3: スクロール履歴・カーソル (Scrollback & Cursor) ─────── */
    H s3_top = s2_bot + GAP + LBL_OVERLAP;  /* 228 */
    H s3_bot = s3_top + P + LH * 2 + P;     /* 288 */
    RECT sec3 = { M, s3_top, dev->width - M, s3_bot };
    settings_draw_section(dev, &sec3, " [3] 履歴・カーソル (Scrollback & Cursor) ");

    H col5 = (dev->width - 2 * M - 2 * P) / 5;
    settings_draw_ui_radio(dev, M + P,             s3_top + P,       "履歴 100K行",           cfg->scrollback_lines == 100000,   FALSE);
    settings_draw_ui_radio(dev, M + P + col5,      s3_top + P,       "履歴 300行 (標準)",    cfg->scrollback_lines == 300,   FALSE);
    settings_draw_ui_radio(dev, M + P + col5 * 2,  s3_top + P,       "履歴 1000行 (大)",    cfg->scrollback_lines == 1000,  FALSE);
    settings_draw_ui_radio(dev, M + P + col5 * 3,  s3_top + P,       "履歴 4096行",          cfg->scrollback_lines == 4096,  FALSE);
    settings_draw_ui_radio(dev, M + P + col5 * 4,  s3_top + P,       "履歴 10K行 (最大)",    cfg->scrollback_lines == 10000, FALSE);
    settings_draw_ui_radio(dev, M + P,             s3_top + P + LH,  "カーソル下線 (_)",    cfg->cursor_style == TERM_CURSOR_UNDERLINE, FALSE);
    settings_draw_ui_radio(dev, M + P + col3,      s3_top + P + LH,  "ブロックカーソル (█)",cfg->cursor_style == TERM_CURSOR_BLOCK,     FALSE);
    settings_draw_ui_radio(dev, M + P + col3 * 2,  s3_top + P + LH,  "バーカーソル (|)",    cfg->cursor_style == TERM_CURSOR_BAR,       FALSE);

    /* ── Section 4: 画面透過度 (Transparency) ──────────────────── */
    H s4_top = s3_bot + GAP + LBL_OVERLAP;  /* 310 */
    H s4_bot = s4_top + P + LH + P;         /* 350 */
    RECT sec4 = { M, s4_top, dev->width - M, s4_bot };
    settings_draw_section(dev, &sec4, " [4] 背景透過・減光 (Background Transparency) ");

    settings_draw_ui_radio(dev, M + P,             s4_top + P, "不透明 (100% Opaque)",  cfg->transparency == TERM_TRANSPARENCY_OPAQUE, FALSE);
    settings_draw_ui_radio(dev, M + P + col3,      s4_top + P, "20%減光 (80% Dimmed)",  cfg->transparency == TERM_TRANSPARENCY_80,     FALSE);
    settings_draw_ui_radio(dev, M + P + col3 * 2,  s4_top + P, "40%減光 (60% Dimmed)",  cfg->transparency == TERM_TRANSPARENCY_60,     FALSE);

    /* ── Action Buttons ─────────────────────────────────────── */
    settings_draw_ui_button(dev, dev->width - 240, dev->height - 36, 110, 26, "標準に戻す",    FALSE);
    settings_draw_ui_button(dev, dev->width - 122, dev->height - 36, 110, 26, "適用 (Apply)",  FALSE);
}

static void handle_terminal_settings_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        if (settings_dispatch_control_event(wnd, s_term_controls, 19, evt)) return;

        H rel_x = evt->pos.x - wnd->client.left;
        H rel_y = evt->pos.y - wnd->client.top;
        H client_w = wnd->client.right - wnd->client.left;
        H btn_y = (wnd->client.bottom - wnd->client.top) - 36;

        /* Buttons */
        if (rel_y >= btn_y && rel_y <= btn_y + 26) {
            if (rel_x >= client_w - 240 && rel_x < client_w - 130) {
                terminal_reset_settings();
                terminal_get_settings(&g_state_terminal.local_cfg);
                g_state_terminal.is_dirty = FALSE;
                inval_wnd(wnd);
                redraw_all_windows();
                return;
            } else if (rel_x >= client_w - 122 && rel_x < client_w - 12) {
                terminal_set_settings(&g_state_terminal.local_cfg);
                g_state_terminal.is_dirty = FALSE;
                cls_wnd(wnd);
                return;
            }
        }
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
