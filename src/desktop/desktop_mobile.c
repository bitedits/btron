/*
 * B-System (BTRON 3.20) Mobile UI Compositor & Renderer: desktop_mobile.c
 *
 * Authentic Cho-Kanji / B-System Mobile HMI Engine for Vertical Screens (480x640 VGA)
 * NASA JPL Rule 3 compliant: Bounded state, zero post-boot heap allocations.
 */

#include <btron/mobile_ui.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <string.h>
#include <time.h>
#else
#include <libstr.h>
#define snprintf tkl_snprintf
#define strlen   tkl_strlen
#define strncpy  tkl_strncpy
#endif

/* ── Bounded Screen Navigation Stack ── */
static FOMA_SCREEN g_screen_stack[FOMA_MAX_SCREEN_DEPTH];
static int g_screen_depth = 0;
static FOMA_MODAL g_modal;

/* Helper to draw a horizontal line with specified color */
static inline void draw_hline(GDEV *dev, H x1, H y, H x2, COLOR col) {
    if (x2 <= x1) return;
    RECT r = { x1, y, x2, y + 1 };
    fill_rec(dev, &r, col);
}

/* Helper to draw a vertical line with specified color */
static inline void draw_vline(GDEV *dev, H x, H y1, H y2, COLOR col) {
    if (y2 <= y1) return;
    RECT r = { x, y1, x + 1, y2 };
    fill_rec(dev, &r, col);
}

/* Helper to draw a hollow rectangle with specified border color */
static inline void draw_frame(GDEV *dev, const RECT *r, COLOR col) {
    draw_hline(dev, r->left, r->top, r->right, col);
    draw_hline(dev, r->left, r->bottom - 1, r->right, col);
    draw_vline(dev, r->left, r->top, r->bottom, col);
    draw_vline(dev, r->right - 1, r->top, r->bottom, col);
}

/* ── Navigation Stack Management ── */
void foma_ui_init(void) {
    g_screen_depth = 0;
    memset(&g_modal, 0, sizeof(FOMA_MODAL));
}

void foma_push_screen(const FOMA_SCREEN *scr) {
    if (!scr || g_screen_depth >= FOMA_MAX_SCREEN_DEPTH) return;
    g_screen_stack[g_screen_depth] = *scr;
    /* Ensure valid focus index */
    if (g_screen_stack[g_screen_depth].item_count > 0 &&
        g_screen_stack[g_screen_depth].focus_index < 0) {
        g_screen_stack[g_screen_depth].focus_index = 0;
    }
    g_screen_depth++;
}

void foma_pop_screen(void) {
    if (g_screen_depth > 1) {
        g_screen_depth--;
    }
}

FOMA_SCREEN* foma_get_active_screen(void) {
    if (g_screen_depth <= 0) return NULL;
    return &g_screen_stack[g_screen_depth - 1];
}

int foma_get_screen_depth(void) {
    return g_screen_depth;
}

/* ── Focus and Navigation Control ── */
void foma_nav_move_focus(FOMA_SCREEN *scr, int delta) {
    if (!scr || scr->item_count <= 0) return;

    int new_idx = scr->focus_index;
    int attempts = 0;

    do {
        new_idx += delta;
        if (new_idx < 0) {
            new_idx = scr->item_count - 1;
        } else if (new_idx >= scr->item_count) {
            new_idx = 0;
        }
        attempts++;
        /* Skip non-selectable items (separators, headers) */
    } while (attempts < scr->item_count &&
             (scr->items[new_idx].type == FOMA_ITEM_SEPARATOR ||
              scr->items[new_idx].type == FOMA_ITEM_HEADER));

    scr->focus_index = new_idx;

    /* Adjust scrolling viewport if needed */
    int vis = FOMA_VISIBLE_ROWS;
    if (scr->focus_index < scr->top_index) {
        scr->top_index = scr->focus_index;
    } else if (scr->focus_index >= scr->top_index + vis) {
        scr->top_index = scr->focus_index - vis + 1;
    }
}

void foma_nav_set_focus(FOMA_SCREEN *scr, int index) {
    if (!scr || index < 0 || index >= scr->item_count) return;
    if (scr->items[index].type == FOMA_ITEM_SEPARATOR ||
        scr->items[index].type == FOMA_ITEM_HEADER) return;

    scr->focus_index = index;
    int vis = FOMA_VISIBLE_ROWS;
    if (scr->focus_index < scr->top_index) {
        scr->top_index = scr->focus_index;
    } else if (scr->focus_index >= scr->top_index + vis) {
        scr->top_index = scr->focus_index - vis + 1;
    }
}

void foma_nav_activate_selected(FOMA_SCREEN *scr) {
    if (!scr || scr->focus_index < 0 || scr->focus_index >= scr->item_count) return;
    FOMA_ITEM *it = &scr->items[scr->focus_index];
    if (it->action) {
        it->action(scr, scr->focus_index);
    }
}

/* ── Soft Key Trigger Handlers ── */
void foma_trigger_softkey_left(FOMA_SCREEN *scr) {
    if (!scr) return;
    /* Primary action: activate currently focused item */
    foma_nav_activate_selected(scr);
}

void foma_trigger_softkey_center(FOMA_SCREEN *scr) {
    if (!scr) return;
    /* Default center key: if screen has a custom action, invoke it */
    if (scr->item_count > 0 && scr->focus_index >= 0) {
        /* Open System Menu or secondary info */
    }
}

void foma_trigger_softkey_right(FOMA_SCREEN *scr) {
    (void)scr;
    /* Standard keitai behavior: back to previous screen */
    if (g_screen_depth > 1) {
        foma_pop_screen();
    }
}

/* ── Modal Dialog Support ── */
void foma_show_modal(const char *title, const char *message,
                     const char *btn_left, const char *btn_right,
                     void (*on_confirm)(void), void (*on_cancel)(void)) {
    g_modal.is_active = TRUE;
    strncpy(g_modal.title, title ? title : "確認 / Notice", sizeof(g_modal.title) - 1);
    strncpy(g_modal.message, message ? message : "", sizeof(g_modal.message) - 1);
    strncpy(g_modal.btn_left, btn_left ? btn_left : "決定", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, btn_right ? btn_right : "戻る", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
    g_modal.on_confirm = on_confirm;
    g_modal.on_cancel = on_cancel;
}

void foma_close_modal(void) {
    g_modal.is_active = FALSE;
}

BOOL foma_is_modal_active(void) {
    return g_modal.is_active;
}

FOMA_MODAL* foma_get_active_modal(void) {
    return &g_modal;
}

/* ── Rendering: Status Bar ── */
void foma_render_status_bar(GDEV *dev, const char *carrier, const char *clock_str,
                            int battery_bars, int signal_bars) {
    if (!dev) return;

    RECT bar_r = { 0, 0, FOMA_SCREEN_W, FOMA_STATUS_BAR_H };
    fill_rec(dev, &bar_r, FOMA_COL_STATUS_BG);

    /* 1. Carrier Logo / Tag on left */
    const char *crr = (carrier && carrier[0]) ? carrier : "B-TRON FOMA";
    drw_tc_string(dev, 8, 8, crr, COLOR_CYAN, FOMA_COL_STATUS_BG);

    /* 2. Clock in center */
    char time_buf[16];
    if (clock_str && clock_str[0]) {
        strncpy(time_buf, clock_str, sizeof(time_buf) - 1);
    } else {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        time_t now = time(NULL);
        struct tm *tm = localtime(&now);
        if (tm) {
            snprintf(time_buf, sizeof(time_buf), "%02d:%02d", tm->tm_hour, tm->tm_min);
        } else {
            strcpy(time_buf, "22:14");
        }
#else
        strcpy(time_buf, "22:14");
#endif
    }
    H tw = tc_calc_string_width(time_buf, 16);
    H tx = (FOMA_SCREEN_W - tw) / 2;
    drw_tc_string(dev, tx, 8, time_buf, COLOR_WHITE, FOMA_COL_STATUS_BG);

    /* 3. 3G Antenna Signal Meter (Tiered vertical bars) */
    H sig_x = FOMA_SCREEN_W - 100;
    H sig_y_base = 22;
    for (int i = 0; i < 4; i++) {
        H bar_h = 4 + i * 4; /* heights: 4, 8, 12, 16 */
        H bx = sig_x + i * 6;
        H by = sig_y_base - bar_h;
        RECT sr = { bx, by, bx + 4, sig_y_base };
        COLOR sc = (i < signal_bars) ? COLOR_GREEN : COLOR_DKGRAY;
        fill_rec(dev, &sr, sc);
    }
    /* "3G" label */
    drw_tc_string(dev, sig_x + 28, 8, "3G", COLOR_GOLD, FOMA_COL_STATUS_BG);

    /* 4. Battery Block Indicator (e.g. 4 blocks: ████) */
    H bat_x = FOMA_SCREEN_W - 44;
    H bat_y = 9;
    RECT bat_frame = { bat_x, bat_y, bat_x + 36, bat_y + 14 };
    draw_frame(dev, &bat_frame, COLOR_WHITE);
    /* Tip of battery */
    RECT bat_tip = { bat_x + 36, bat_y + 3, bat_x + 38, bat_y + 11 };
    fill_rec(dev, &bat_tip, COLOR_WHITE);

    /* Fill battery blocks */
    for (int b = 0; b < 4; b++) {
        RECT br = { bat_x + 2 + b * 8, bat_y + 2, bat_x + 8 + b * 8, bat_y + 12 };
        COLOR bc = (b < battery_bars) ? COLOR_GOLD : ARGB(0xFF, 0x40, 0x40, 0x40);
        fill_rec(dev, &br, bc);
    }

    /* Bottom accent line */
    draw_hline(dev, 0, FOMA_STATUS_BAR_H - 1, FOMA_SCREEN_W, FOMA_COL_GOLD);
}

/* ── Rendering: Title Bar ── */
void foma_render_title_bar(GDEV *dev, const char *title, const char *subtitle) {
    if (!dev) return;

    RECT r = { 0, FOMA_STATUS_BAR_H, FOMA_SCREEN_W, FOMA_STATUS_BAR_H + FOMA_TITLE_BAR_H };
    fill_rec(dev, &r, FOMA_COL_TITLE_BG);

    /* Top and bottom double border */
    draw_hline(dev, 0, r.top, FOMA_SCREEN_W, ARGB(0xFF, 0x00, 0x88, 0xDD));
    draw_hline(dev, 0, r.bottom - 1, FOMA_SCREEN_W, ARGB(0xFF, 0x00, 0x1A, 0x33));

    /* Title string */
    const char *t = (title && title[0]) ? title : "実身キャビネット";
    drw_tc_string(dev, 12, r.top + 9, t, COLOR_WHITE, FOMA_COL_TITLE_BG);

    /* Right-aligned subtitle / item count */
    if (subtitle && subtitle[0]) {
        H sw = tc_calc_string_width(subtitle, 32);
        drw_tc_string(dev, FOMA_SCREEN_W - sw - 12, r.top + 9, subtitle,
                      ARGB(0xFF, 0x99, 0xCC, 0xFF), FOMA_COL_TITLE_BG);
    }
}

/* ── Rendering: List View ── */
void foma_render_list(GDEV *dev, const FOMA_SCREEN *scr) {
    if (!dev || !scr) return;

    RECT list_r = { 0, FOMA_LIST_TOP, FOMA_SCREEN_W, FOMA_LIST_BOTTOM };
    fill_rec(dev, &list_r, FOMA_COL_PANEL_BG);

    int vis = FOMA_VISIBLE_ROWS;
    int end_idx = scr->top_index + vis;
    if (end_idx > scr->item_count) end_idx = scr->item_count;

    for (int i = scr->top_index; i < end_idx; i++) {
        const FOMA_ITEM *it = &scr->items[i];
        int row_rel = i - scr->top_index;
        H ry = FOMA_LIST_TOP + row_rel * FOMA_ROW_H;
        RECT row_r = { 0, ry, FOMA_SCREEN_W, ry + FOMA_ROW_H };

        BOOL is_focused = (i == scr->focus_index);

        if (it->type == FOMA_ITEM_SEPARATOR) {
            /* Draw centered dashed separator line */
            draw_hline(dev, 24, ry + FOMA_ROW_H / 2, FOMA_SCREEN_W - 24, FOMA_COL_SEPARATOR);
            continue;
        }

        if (it->type == FOMA_ITEM_HEADER) {
            /* Group header band (e.g. あいうえおグループ) */
            RECT hr = { 0, ry, FOMA_SCREEN_W, ry + FOMA_ROW_H };
            fill_rec(dev, &hr, ARGB(0xFF, 0xD8, 0xE2, 0xEC));
            draw_hline(dev, 0, ry + FOMA_ROW_H - 1, FOMA_SCREEN_W, ARGB(0xFF, 0xB0, 0xC0, 0xD0));
            drw_tc_string(dev, 16, ry + 10, it->title, FOMA_COL_TITLE_BG, ARGB(0xFF, 0xD8, 0xE2, 0xEC));
            continue;
        }

        /* Normal or Virtual Body row */
        if (is_focused) {
            /* High-contrast inverted focus highlight */
            fill_rec(dev, &row_r, FOMA_COL_FOCUS_BG);
            /* Inner focus outline */
            draw_frame(dev, &row_r, ARGB(0xFF, 0x66, 0xBB, 0xFF));
            /* Focus indicator arrow: ▶ */
            drw_tc_string(dev, 8, ry + 10, "▶", COLOR_YELLOW, FOMA_COL_FOCUS_BG);
        } else {
            /* Alternating row background for readability */
            if (i % 2 == 1) {
                fill_rec(dev, &row_r, FOMA_COL_ROW_ALT);
            }
            draw_hline(dev, 16, ry + FOMA_ROW_H - 1, FOMA_SCREEN_W - 16, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        }

        /* Determine text colors */
        COLOR txt_col = is_focused ? FOMA_COL_FOCUS_TXT :
            (it->type == FOMA_ITEM_VIRTUAL_BODY ? FOMA_COL_FUSEN : COLOR_BLACK);
        COLOR bg_col = is_focused ? FOMA_COL_FOCUS_BG :
            ((i % 2 == 1) ? FOMA_COL_ROW_ALT : FOMA_COL_PANEL_BG);

        /* Render Item Title */
        drw_tc_string(dev, 32, ry + 10, it->title, txt_col, bg_col);

        /* Render Badge / Counter / Detail on right */
        if (it->badge[0] != '\0') {
            H bw = tc_calc_string_width(it->badge, 32);
            H bx = FOMA_SCREEN_W - bw - 28;

            if (is_focused) {
                /* In focus mode, badge text matches highlight */
                drw_tc_string(dev, bx, ry + 10, it->badge, COLOR_YELLOW, FOMA_COL_FOCUS_BG);
            } else {
                /* Render badge pill */
                RECT pill = { bx - 6, ry + 6, bx + bw + 6, ry + FOMA_ROW_H - 6 };
                fill_rec(dev, &pill, FOMA_COL_BADGE_BG);
                draw_frame(dev, &pill, ARGB(0xFF, 0xB0, 0xC4, 0xDE));
                drw_tc_string(dev, bx, ry + 10, it->badge, FOMA_COL_BADGE_TXT, FOMA_COL_BADGE_BG);
            }
        }
    }

    /* Scroll Indicator Bar on right edge if needed */
    if (scr->item_count > vis) {
        H track_top = FOMA_LIST_TOP + 4;
        H track_bottom = FOMA_LIST_BOTTOM - 4;
        H track_h = track_bottom - track_top;
        H thumb_h = (track_h * vis) / scr->item_count;
        if (thumb_h < 16) thumb_h = 16;
        H thumb_y = track_top + (track_h - thumb_h) * scr->top_index / (scr->item_count - vis);

        RECT track_r = { FOMA_SCREEN_W - 8, track_top, FOMA_SCREEN_W - 2, track_bottom };
        fill_rec(dev, &track_r, ARGB(0xFF, 0xD0, 0xD8, 0xE0));

        RECT thumb_r = { FOMA_SCREEN_W - 8, thumb_y, FOMA_SCREEN_W - 2, thumb_y + thumb_h };
        fill_rec(dev, &thumb_r, FOMA_COL_FOCUS_BG);
    }
}

/* ── Rendering: Soft Key Footer ── */
void foma_render_softkey_bar(GDEV *dev, const char *left_lbl, const char *mid_lbl, const char *right_lbl) {
    if (!dev) return;

    H sy = FOMA_SCREEN_H - FOMA_SOFTKEY_BAR_H;
    RECT bar_r = { 0, sy, FOMA_SCREEN_W, FOMA_SCREEN_H };
    fill_rec(dev, &bar_r, ARGB(0xFF, 0x2A, 0x34, 0x3F));

    /* Top gold border line */
    draw_hline(dev, 0, sy, FOMA_SCREEN_W, FOMA_COL_GOLD);

    /* 3 Button Plates */
    H btn_w = (FOMA_SCREEN_W - 16) / 3;
    const char *labels[3] = {
        (left_lbl && left_lbl[0]) ? left_lbl : "[選択]",
        (mid_lbl && mid_lbl[0]) ? mid_lbl : "[メニュー]",
        (right_lbl && right_lbl[0]) ? right_lbl : "[戻る]"
    };

    for (int b = 0; b < 3; b++) {
        H bx = 4 + b * (btn_w + 4);
        RECT br = { bx, sy + 4, bx + btn_w, FOMA_SCREEN_H - 4 };

        /* 3D Bevel effect */
        fill_rec(dev, &br, FOMA_COL_SOFTKEY_BG);
        draw_hline(dev, br.left, br.top, br.right, FOMA_COL_SOFTKEY_HI);
        draw_vline(dev, br.left, br.top, br.bottom, FOMA_COL_SOFTKEY_HI);
        draw_hline(dev, br.left, br.bottom - 1, br.right, FOMA_COL_SOFTKEY_SH);
        draw_vline(dev, br.right - 1, br.top, br.bottom, FOMA_COL_SOFTKEY_SH);

        /* Center label */
        H lw = tc_calc_string_width(labels[b], 24);
        H lx = br.left + (btn_w - lw) / 2;
        drw_tc_string(dev, lx, br.top + 7, labels[b], COLOR_BLACK, FOMA_COL_SOFTKEY_BG);
    }
}

/* ── Rendering: Modal Dialog ── */
void foma_render_modal(GDEV *dev, const FOMA_MODAL *modal) {
    if (!dev || !modal || !modal->is_active) return;

    H box_w = 380;
    H box_h = 240;
    H bx = (FOMA_SCREEN_W - box_w) / 2;
    H by = (FOMA_SCREEN_H - box_h) / 2;

    RECT shadow = { bx + 4, by + 4, bx + box_w + 4, by + box_h + 4 };
    fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

    RECT box_r = { bx, by, bx + box_w, by + box_h };
    fill_rec(dev, &box_r, COLOR_WHITE);
    draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);

    /* Modal Title Bar */
    RECT hdr_r = { bx, by, bx + box_w, by + 32 };
    fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
    drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);

    /* Message lines (wrap simple) */
    drw_tc_string(dev, bx + 16, by + 50, modal->message, COLOR_BLACK, COLOR_WHITE);

    /* Dialog Buttons */
    H btn_w = 120;
    H btn_h = 32;
    H btn_y = by + box_h - 48;

    /* Button 1 (Left) */
    H b1_x = bx + 40;
    RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
    COLOR b1_bg = (modal->selected_btn == 0) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
    COLOR b1_fg = (modal->selected_btn == 0) ? COLOR_WHITE : COLOR_BLACK;
    fill_rec(dev, &b1_r, b1_bg);
    draw_frame(dev, &b1_r, COLOR_DKGRAY);
    H l1_w = tc_calc_string_width(modal->btn_left, 24);
    drw_tc_string(dev, b1_x + (btn_w - l1_w) / 2, btn_y + 8, modal->btn_left, b1_fg, b1_bg);

    /* Button 2 (Right) */
    H b2_x = bx + box_w - 40 - btn_w;
    RECT b2_r = { b2_x, btn_y, b2_x + btn_w, btn_y + btn_h };
    COLOR b2_bg = (modal->selected_btn == 1) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
    COLOR b2_fg = (modal->selected_btn == 1) ? COLOR_WHITE : COLOR_BLACK;
    fill_rec(dev, &b2_r, b2_bg);
    draw_frame(dev, &b2_r, COLOR_DKGRAY);
    H l2_w = tc_calc_string_width(modal->btn_right, 24);
    drw_tc_string(dev, b2_x + (btn_w - l2_w) / 2, btn_y + 8, modal->btn_right, b2_fg, b2_bg);
}

/* ── Master Composition Entry Point ── */
void foma_render_desktop(GDEV *dev, const FOMA_SCREEN *scr) {
    if (!dev) return;

    if (!scr) {
        scr = foma_get_active_screen();
    }
    if (!scr) return;

    /* 1. Status Bar */
    foma_render_status_bar(dev, "B-TRON FOMA", NULL, 4, 4);

    /* 2. Title Bar */
    foma_render_title_bar(dev, scr->title, scr->subtitle);

    /* 3. List or Custom Content */
    if (scr->custom_render_hook) {
        scr->custom_render_hook(dev, scr);
    } else {
        foma_render_list(dev, scr);
    }

    /* 4. Soft Key Footer */
    foma_render_softkey_bar(dev, scr->softkey_left, scr->softkey_center, scr->softkey_right);

    /* 5. Modal Overlay (if open) */
    if (g_modal.is_active) {
        foma_render_modal(dev, &g_modal);
    }
}
