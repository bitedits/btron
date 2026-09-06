/*
 * B-System (BTRON 3.20) Mobile UI Compositor & Renderer: desktop_mobile.c
 *
 * Authentic Cho-Kanji / B-System Mobile HMI Engine for Vertical Screens (480x640 VGA)
 * NASA JPL Rule 3 compliant: Bounded state, zero post-boot heap allocations.
 */

#include <btron/mobile_ui.h>
#include <btron/troncode.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
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
static FOMA_POPUP_MENU g_popup_menu;

#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak))
#endif
void app_menu_set_about_hook(AppMenuAboutHookFn hook) {
    (void)hook;
}

static WND* foma_about_dialog_hook(const char *app_name, const char *jp_title,
                                   const char *desc, const char *attribution,
                                   int x, int y) {
    (void)x; (void)y;
    foma_show_about_dialog(app_name, jp_title, desc, attribution);
    return NULL;
}

/* ── Navigation Stack Management ── */
void foma_ui_init(void) {
    g_screen_depth = 0;
    memset(&g_modal, 0, sizeof(FOMA_MODAL));
    memset(&g_popup_menu, 0, sizeof(FOMA_POPUP_MENU));
    app_menu_set_about_hook(foma_about_dialog_hook);
}
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
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_CONFIRM;
    strncpy(g_modal.title, title ? title : "確認 / Notice", sizeof(g_modal.title) - 1);
    strncpy(g_modal.message, message ? message : "", sizeof(g_modal.message) - 1);
    strncpy(g_modal.btn_left, btn_left ? btn_left : "決定", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, btn_right ? btn_right : "戻る", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
    g_modal.on_confirm = on_confirm;
    g_modal.on_cancel = on_cancel;
}

void foma_show_confirm_dialog(const char *title, const char *target_name, const char *warning_note,
                              void (*on_confirm)(void), void (*on_cancel)(void)) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_CONFIRM;
    strncpy(g_modal.title, title ? title : "実身削除の確認", sizeof(g_modal.title) - 1);
    strncpy(g_modal.detail1, target_name ? target_name : "連絡先：坂村 健", sizeof(g_modal.detail1) - 1);
    strncpy(g_modal.message, warning_note ? warning_note : "この実身を完全に削除しますか？\n※ 関連するすべての仮身リンクが\n　 参照不能（リンク切れ）になります。", sizeof(g_modal.message) - 1);
    strncpy(g_modal.btn_left, "削除実行", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, "取消", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
    g_modal.on_confirm = on_confirm;
    g_modal.on_cancel = on_cancel;
}

void foma_show_properties_dialog(const char *rbody_name, const char *rbody_id, const char *type_name,
                                 int size_bytes, int fusen_cnt, const char *date_str) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_PROPERTIES;
    strncpy(g_modal.title, "実身属性 / Properties", sizeof(g_modal.title) - 1);
    strncpy(g_modal.detail1, rbody_name ? rbody_name : "連絡先：坂村 健", sizeof(g_modal.detail1) - 1);
    strncpy(g_modal.detail2, rbody_id ? rbody_id : "0x002A-8F14-C001", sizeof(g_modal.detail2) - 1);
    strncpy(g_modal.detail3, type_name ? type_name : "TAD Rev 3.20 (テキスト+仮身)", sizeof(g_modal.detail3) - 1);
    snprintf(g_modal.detail4, sizeof(g_modal.detail4), "%d bytes / %d fusen", size_bytes, fusen_cnt);
    strncpy(g_modal.message, date_str ? date_str : "2026-09-05 14:32:08", sizeof(g_modal.message) - 1);
    strncpy(g_modal.btn_left, "属性変更", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, "閉じる", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 1;
}

void foma_show_search_dialog(const char *query_text, const char *ime_mode) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_INPUT;
    strncpy(g_modal.title, "実身・仮身の検索 / Search", sizeof(g_modal.title) - 1);
    strncpy(g_modal.detail1, query_text ? query_text : "坂村 健", sizeof(g_modal.detail1) - 1);
    strncpy(g_modal.detail2, ime_mode ? ime_mode : "[あ/漢] TIP/Mozc", sizeof(g_modal.detail2) - 1);
    strncpy(g_modal.btn_left, "検索開始", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, "中止", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
}

void foma_show_power_dialog(int battery_pct, const char *voltage_str) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_POWER;
    strncpy(g_modal.title, "電源管理 / Power", sizeof(g_modal.title) - 1);
    snprintf(g_modal.detail1, sizeof(g_modal.detail1), "電池残量: %d%% (%s)", battery_pct, voltage_str ? voltage_str : "4.12V");
    strncpy(g_modal.btn_left, "決定", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, "戻る", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
}

void foma_show_call_dialog(const char *caller_name, const char *phone_num, const char *duration) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_CALL;
    strncpy(g_modal.title, "[3G] 音声着信 / Voice Call", sizeof(g_modal.title) - 1);
    strncpy(g_modal.detail1, caller_name ? caller_name : "坂村 健", sizeof(g_modal.detail1) - 1);
    strncpy(g_modal.detail2, phone_num ? phone_num : "090-XXXX-XXXX", sizeof(g_modal.detail2) - 1);
    strncpy(g_modal.detail3, duration ? duration : "00:06", sizeof(g_modal.detail3) - 1);
    strncpy(g_modal.btn_left, "応答", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_center, "保留", sizeof(g_modal.btn_center) - 1);
    strncpy(g_modal.btn_right, "拒否", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
}

void foma_show_about_dialog(const char *app_name, const char *jp_title, const char *desc, const char *attribution) {
    memset(&g_modal, 0, sizeof(g_modal));
    g_modal.is_active = TRUE;
    g_modal.type = FOMA_MODAL_ABOUT;
    char title_buf[64];
    snprintf(title_buf, sizeof(title_buf), "%s について", jp_title ? jp_title : (app_name ? app_name : "アプリ"));
    strncpy(g_modal.title, title_buf, sizeof(g_modal.title) - 1);
    char full_app[64];
    snprintf(full_app, sizeof(full_app), "%s (%s)", jp_title ? jp_title : "アプリ", app_name ? app_name : "App");
    strncpy(g_modal.detail1, full_app, sizeof(g_modal.detail1) - 1);
    strncpy(g_modal.message, desc ? desc : "B-System 3.20 Native Application", sizeof(g_modal.message) - 1);
    strncpy(g_modal.detail2, attribution ? attribution : "Brought to B-System by 5HT", sizeof(g_modal.detail2) - 1);
    strncpy(g_modal.btn_center, "確認 (OK)", sizeof(g_modal.btn_center) - 1);
    strncpy(g_modal.btn_left, "確認", sizeof(g_modal.btn_left) - 1);
    strncpy(g_modal.btn_right, "閉じる", sizeof(g_modal.btn_right) - 1);
    g_modal.selected_btn = 0;
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

/* ── Popup Menu Support ── */
void foma_show_popup_menu(const char *title, const char *items[], const char *badges[], int count, int focus_idx) {
    memset(&g_popup_menu, 0, sizeof(g_popup_menu));
    g_popup_menu.is_active = TRUE;
    strncpy(g_popup_menu.title, title ? title : "操作メニュー (1-6)", sizeof(g_popup_menu.title) - 1);
    if (count > 8) count = 8;
    g_popup_menu.item_count = count;
    for (int i = 0; i < count; i++) {
        if (items && items[i]) {
            strncpy(g_popup_menu.items[i], items[i], sizeof(g_popup_menu.items[i]) - 1);
        }
        if (badges && badges[i]) {
            strncpy(g_popup_menu.badges[i], badges[i], sizeof(g_popup_menu.badges[i]) - 1);
        }
    }
    g_popup_menu.focus_index = (focus_idx >= 0 && focus_idx < count) ? focus_idx : 0;
}

void foma_close_popup_menu(void) {
    g_popup_menu.is_active = FALSE;
}

BOOL foma_is_popup_menu_active(void) {
    return g_popup_menu.is_active;
}

FOMA_POPUP_MENU* foma_get_active_popup_menu(void) {
    return &g_popup_menu;
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

    if (modal->type == FOMA_MODAL_PROPERTIES) {
        /* Real Body Properties Sheet (Height 380, Width 420) */
        H box_w = 420;
        H box_h = 380;
        H bx = (FOMA_SCREEN_W - box_w) / 2;
        H by = (FOMA_SCREEN_H - box_h) / 2 - 10;

        RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
        fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

        RECT box_r = { bx, by, bx + box_w, by + box_h };
        fill_rec(dev, &box_r, COLOR_WHITE);
        draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
        RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
        draw_frame(dev, &inner_r, FOMA_COL_GOLD);

        /* Title Bar */
        RECT hdr_r = { bx, by, bx + box_w, by + 32 };
        fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + box_w - 60, by + 8, "[属性]", FOMA_COL_GOLD, FOMA_COL_TITLE_BG);

        /* Subhead Real Body Plate */
        RECT sub_r = { bx + 12, by + 40, bx + box_w - 12, by + 72 };
        fill_rec(dev, &sub_r, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        draw_frame(dev, &sub_r, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + 20, by + 48, "実身: ", FOMA_COL_TITLE_BG, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        drw_tc_string(dev, bx + 65, by + 48, modal->detail1, COLOR_BLACK, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        drw_tc_string(dev, bx + box_w - 60, by + 48, "[実身]", COLOR_WHITE, FOMA_COL_TITLE_BG);

        /* Metadata rows */
        const char *keys[] = {
            "実身番号 (ID)",
            "データ種別",
            "作成日時",
            "サイズ/仮身",
            "アクセス権",
            "保存場所"
        };
        const char *vals[6];
        vals[0] = modal->detail2[0] ? modal->detail2 : "0x002A-8F14-C001";
        vals[1] = modal->detail3[0] ? modal->detail3 : "TAD Rev 3.20 (テキスト+仮身)";
        vals[2] = modal->message[0] ? modal->message : "2026-09-05 14:32:08";
        vals[3] = modal->detail4[0] ? modal->detail4 : "4,896 B / 3 fusen";
        vals[4] = "読込・書込可能 (RW / 0644)";
        vals[5] = "HFDS VirtIO-Block (/btron0)";

        for (int i = 0; i < 6; i++) {
            H ry = by + 80 + i * 36;
            RECT rr = { bx + 12, ry, bx + box_w - 12, ry + 32 };
            COLOR rbg = (i % 2 == 0) ? ARGB(0xFF, 0xF8, 0xFA, 0xFC) : ARGB(0xFF, 0xEE, 0xF2, 0xF6);
            fill_rec(dev, &rr, rbg);
            draw_frame(dev, &rr, ARGB(0xFF, 0xCB, 0xD5, 0xE1));

            drw_tc_string(dev, bx + 18, ry + 8, keys[i], FOMA_COL_TITLE_BG, rbg);
            drw_tc_string(dev, bx + 130, ry + 8, vals[i], COLOR_BLACK, rbg);
        }

        /* Buttons */
        H btn_w = 140;
        H btn_h = 34;
        H btn_y = by + box_h - 46;

        H b1_x = bx + 40;
        RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
        COLOR b1_bg = (modal->selected_btn == 0) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b1_fg = (modal->selected_btn == 0) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b1_r, b1_bg);
        draw_frame(dev, &b1_r, COLOR_DKGRAY);
        H l1_w = tc_calc_string_width(modal->btn_left, 24);
        drw_tc_string(dev, b1_x + (btn_w - l1_w) / 2, btn_y + 8, modal->btn_left, b1_fg, b1_bg);

        H b2_x = bx + box_w - 40 - btn_w;
        RECT b2_r = { b2_x, btn_y, b2_x + btn_w, btn_y + btn_h };
        COLOR b2_bg = (modal->selected_btn == 1) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b2_fg = (modal->selected_btn == 1) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b2_r, b2_bg);
        draw_frame(dev, &b2_r, COLOR_DKGRAY);
        H l2_w = tc_calc_string_width(modal->btn_right, 24);
        drw_tc_string(dev, b2_x + (btn_w - l2_w) / 2, btn_y + 8, modal->btn_right, b2_fg, b2_bg);
        return;
    }

    if (modal->type == FOMA_MODAL_INPUT) {
        /* Search / Input Dialog with TIP Virtual IME (Height 340, Width 420) */
        H box_w = 420;
        H box_h = 340;
        H bx = (FOMA_SCREEN_W - box_w) / 2;
        H by = (FOMA_SCREEN_H - box_h) / 2 - 10;

        RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
        fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

        RECT box_r = { bx, by, bx + box_w, by + box_h };
        fill_rec(dev, &box_r, COLOR_WHITE);
        draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
        RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
        draw_frame(dev, &inner_r, FOMA_COL_GOLD);

        /* Title */
        RECT hdr_r = { bx, by, bx + box_w, by + 32 };
        fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);

        /* Keyword Label + IME Badge */
        drw_tc_string(dev, bx + 16, by + 46, "検索キー (Keyword):", FOMA_COL_TITLE_BG, COLOR_WHITE);
        RECT ime_r = { bx + box_w - 150, by + 42, bx + box_w - 14, by + 66 };
        fill_rec(dev, &ime_r, ARGB(0xFF, 0x02, 0x84, 0xC7));
        drw_tc_string(dev, bx + box_w - 144, by + 46, "[あ/漢] TIP/Mozc", COLOR_WHITE, ARGB(0xFF, 0x02, 0x84, 0xC7));

        /* Input field box */
        RECT in_r = { bx + 16, by + 74, bx + box_w - 16, by + 112 };
        fill_rec(dev, &in_r, ARGB(0xFF, 0xF8, 0xFA, 0xFC));
        draw_frame(dev, &in_r, FOMA_COL_FOCUS_BG);
        RECT in_r2 = { bx + 17, by + 75, bx + box_w - 17, by + 111 };
        draw_frame(dev, &in_r2, FOMA_COL_TITLE_BG);

        /* Text inside input box + cursor */
        const char *txt = modal->detail1[0] ? modal->detail1 : "坂村 健";
        drw_tc_string(dev, bx + 24, by + 85, txt, COLOR_BLACK, ARGB(0xFF, 0xF8, 0xFA, 0xFC));
        H tw = tc_calc_string_width(txt, 32);
        RECT caret = { bx + 24 + tw + 2, by + 83, bx + 24 + tw + 5, by + 103 };
        fill_rec(dev, &caret, COLOR_BLACK);

        /* Search scope options (Radio Buttons) */
        RECT scp_r = { bx + 16, by + 124, bx + box_w - 16, by + 210 };
        fill_rec(dev, &scp_r, ARGB(0xFF, 0xF1, 0xF5, 0xF9));
        draw_frame(dev, &scp_r, ARGB(0xFF, 0xCB, 0xD5, 0xE1));

        drw_tc_string(dev, bx + 24, by + 132, "検索対象範囲 (Scope):", COLOR_DKGRAY, ARGB(0xFF, 0xF1, 0xF5, 0xF9));
        drw_tc_string(dev, bx + 32, by + 154, "(*) 現在のキャビネット内 (Current)", FOMA_COL_TITLE_BG, ARGB(0xFF, 0xF1, 0xF5, 0xF9));
        drw_tc_string(dev, bx + 32, by + 180, "( ) 全実身 (外部 VirtIO-Block 含む)", COLOR_BLACK, ARGB(0xFF, 0xF1, 0xF5, 0xF9));

        /* Checkbox Option */
        RECT chk_r = { bx + 16, by + 220, bx + box_w - 16, by + 252 };
        fill_rec(dev, &chk_r, ARGB(0xFF, 0xEE, 0xF2, 0xF6));
        drw_tc_string(dev, bx + 24, by + 228, "[X] 仮身 (Fusen) リンクを検索対象に含める", FOMA_COL_TITLE_BG, ARGB(0xFF, 0xEE, 0xF2, 0xF6));

        /* Buttons */
        H btn_w = 140;
        H btn_h = 34;
        H btn_y = by + box_h - 48;

        H b1_x = bx + 40;
        RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
        COLOR b1_bg = (modal->selected_btn == 0) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b1_fg = (modal->selected_btn == 0) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b1_r, b1_bg);
        draw_frame(dev, &b1_r, COLOR_DKGRAY);
        H l1_w = tc_calc_string_width(modal->btn_left, 24);
        drw_tc_string(dev, b1_x + (btn_w - l1_w) / 2, btn_y + 8, modal->btn_left, b1_fg, b1_bg);

        H b2_x = bx + box_w - 40 - btn_w;
        RECT b2_r = { b2_x, btn_y, b2_x + btn_w, btn_y + btn_h };
        COLOR b2_bg = (modal->selected_btn == 1) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b2_fg = (modal->selected_btn == 1) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b2_r, b2_bg);
        draw_frame(dev, &b2_r, COLOR_DKGRAY);
        H l2_w = tc_calc_string_width(modal->btn_right, 24);
        drw_tc_string(dev, b2_x + (btn_w - l2_w) / 2, btn_y + 8, modal->btn_right, b2_fg, b2_bg);
        return;
    }

    if (modal->type == FOMA_MODAL_POWER) {
        /* Power & Sleep Dialog */
        H box_w = 400;
        H box_h = 320;
        H bx = (FOMA_SCREEN_W - box_w) / 2;
        H by = (FOMA_SCREEN_H - box_h) / 2;

        RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
        fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

        RECT box_r = { bx, by, bx + box_w, by + box_h };
        fill_rec(dev, &box_r, COLOR_WHITE);
        draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
        RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
        draw_frame(dev, &inner_r, FOMA_COL_GOLD);

        /* Title */
        RECT hdr_r = { bx, by, bx + box_w, by + 32 };
        fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);

        /* Battery Status Box */
        RECT bat_r = { bx + 16, by + 44, bx + box_w - 16, by + 82 };
        fill_rec(dev, &bat_r, ARGB(0xFF, 0x0F, 0x17, 0x2A));
        drw_tc_string(dev, bx + 24, by + 52, modal->detail1, COLOR_GOLD, ARGB(0xFF, 0x0F, 0x17, 0x2A));

        /* Battery bar blocks */
        H bar_x = bx + box_w - 120;
        for (int b = 0; b < 6; b++) {
            RECT bseg = { bar_x + b * 14, by + 52, bar_x + b * 14 + 10, by + 72 };
            fill_rec(dev, &bseg, (b < 5) ? ARGB(0xFF, 0x22, 0xC5, 0x5E) : COLOR_DKGRAY);
        }

        /* Power Options list */
        const char *pwr_opts[] = {
            "1. スリープ (省電力待受モード)",
            "2. 再起動 (T-Kernel 2.0 リセット)",
            "3. 電源切断 (Power Off)",
            "4. 画面・キーロック (誤操作防止)"
        };
        for (int i = 0; i < 4; i++) {
            H oy = by + 94 + i * 36;
            RECT or_ = { bx + 16, oy, bx + box_w - 16, oy + 32 };
            BOOL is_foc = (i == 0);
            if (is_foc) {
                fill_rec(dev, &or_, FOMA_COL_FOCUS_BG);
                draw_frame(dev, &or_, COLOR_WHITE);
                drw_tc_string(dev, bx + 24, oy + 8, "▶", COLOR_YELLOW, FOMA_COL_FOCUS_BG);
                drw_tc_string(dev, bx + 42, oy + 8, pwr_opts[i], COLOR_WHITE, FOMA_COL_FOCUS_BG);
            } else {
                fill_rec(dev, &or_, ARGB(0xFF, 0xF1, 0xF5, 0xF9));
                drw_tc_string(dev, bx + 42, oy + 8, pwr_opts[i], COLOR_BLACK, ARGB(0xFF, 0xF1, 0xF5, 0xF9));
            }
        }

        /* Buttons */
        H btn_w = 120;
        H btn_h = 32;
        H btn_y = by + box_h - 46;

        H b1_x = bx + 40;
        RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
        COLOR b1_bg = (modal->selected_btn == 0) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b1_fg = (modal->selected_btn == 0) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b1_r, b1_bg);
        draw_frame(dev, &b1_r, COLOR_DKGRAY);
        H l1_w = tc_calc_string_width(modal->btn_left, 24);
        drw_tc_string(dev, b1_x + (btn_w - l1_w) / 2, btn_y + 8, modal->btn_left, b1_fg, b1_bg);

        H b2_x = bx + box_w - 40 - btn_w;
        RECT b2_r = { b2_x, btn_y, b2_x + btn_w, btn_y + btn_h };
        COLOR b2_bg = (modal->selected_btn == 1) ? FOMA_COL_FOCUS_BG : FOMA_COL_SOFTKEY_BG;
        COLOR b2_fg = (modal->selected_btn == 1) ? COLOR_WHITE : COLOR_BLACK;
        fill_rec(dev, &b2_r, b2_bg);
        draw_frame(dev, &b2_r, COLOR_DKGRAY);
        H l2_w = tc_calc_string_width(modal->btn_right, 24);
        drw_tc_string(dev, b2_x + (btn_w - l2_w) / 2, btn_y + 8, modal->btn_right, b2_fg, b2_bg);
        return;
    }

    if (modal->type == FOMA_MODAL_CALL) {
        /* 3G Voice Call Alert */
        H box_w = 420;
        H box_h = 310;
        H bx = (FOMA_SCREEN_W - box_w) / 2;
        H by = (FOMA_SCREEN_H - box_h) / 2 - 10;

        RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
        fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

        RECT box_r = { bx, by, bx + box_w, by + box_h };
        fill_rec(dev, &box_r, COLOR_WHITE);
        draw_frame(dev, &box_r, ARGB(0xFF, 0x0F, 0x76, 0x6E));
        RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
        draw_frame(dev, &inner_r, FOMA_COL_GOLD);

        /* Title */
        RECT hdr_r = { bx, by, bx + box_w, by + 32 };
        fill_rec(dev, &hdr_r, ARGB(0xFF, 0x0F, 0x76, 0x6E));
        drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, ARGB(0xFF, 0x0F, 0x76, 0x6E));

        /* Active Call Banner */
        RECT call_banner = { bx + 16, by + 44, bx + box_w - 16, by + 76 };
        fill_rec(dev, &call_banner, ARGB(0xFF, 0x11, 0x5E, 0x59));
        drw_tc_string(dev, bx + 24, by + 52, "[3G] FOMA 音声通話 着信中...", COLOR_WHITE, ARGB(0xFF, 0x11, 0x5E, 0x59));
        drw_tc_string(dev, bx + box_w - 100, by + 52, modal->detail3, COLOR_GOLD, ARGB(0xFF, 0x11, 0x5E, 0x59));

        /* Caller Information Card */
        RECT c_card = { bx + 16, by + 86, bx + box_w - 16, by + 196 };
        fill_rec(dev, &c_card, ARGB(0xFF, 0xF0, 0xFD, 0xFA));
        draw_frame(dev, &c_card, ARGB(0xFF, 0x99, 0xF6, 0xE4));

        drw_tc_string(dev, bx + 28, by + 98, "発信者: ", COLOR_DKGRAY, ARGB(0xFF, 0xF0, 0xFD, 0xFA));
        drw_tc_string(dev, bx + 90, by + 98, modal->detail1, FOMA_COL_TITLE_BG, ARGB(0xFF, 0xF0, 0xFD, 0xFA));

        drw_tc_string(dev, bx + 28, by + 128, "電話番号: ", COLOR_DKGRAY, ARGB(0xFF, 0xF0, 0xFD, 0xFA));
        drw_tc_string(dev, bx + 100, by + 128, modal->detail2, COLOR_BLACK, ARGB(0xFF, 0xF0, 0xFD, 0xFA));

        drw_tc_string(dev, bx + 28, by + 158, "グループ: あいうえお (TRON Project)", COLOR_BLACK, ARGB(0xFF, 0xF0, 0xFD, 0xFA));

        /* Three Action Buttons */
        H btn_w = 110;
        H btn_h = 36;
        H btn_y = by + box_h - 52;

        /* Button 1: 応答 (Green) */
        H b1_x = bx + 20;
        RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
        fill_rec(dev, &b1_r, ARGB(0xFF, 0x16, 0xA3, 0x4A));
        draw_frame(dev, &b1_r, COLOR_WHITE);
        H l1_w = tc_calc_string_width(modal->btn_left, 24);
        drw_tc_string(dev, b1_x + (btn_w - l1_w) / 2, btn_y + 10, modal->btn_left, COLOR_WHITE, ARGB(0xFF, 0x16, 0xA3, 0x4A));

        /* Button 2: 保留 (Gold) */
        H b2_x = bx + (box_w - btn_w) / 2;
        RECT b2_r = { b2_x, btn_y, b2_x + btn_w, btn_y + btn_h };
        fill_rec(dev, &b2_r, ARGB(0xFF, 0xCA, 0x8A, 0x04));
        draw_frame(dev, &b2_r, COLOR_WHITE);
        H l2_w = tc_calc_string_width(modal->btn_center, 24);
        drw_tc_string(dev, b2_x + (btn_w - l2_w) / 2, btn_y + 10, modal->btn_center, COLOR_WHITE, ARGB(0xFF, 0xCA, 0x8A, 0x04));

        /* Button 3: 拒否 (Dark Red/Gray) */
        H b3_x = bx + box_w - 20 - btn_w;
        RECT b3_r = { b3_x, btn_y, b3_x + btn_w, btn_y + btn_h };
        fill_rec(dev, &b3_r, ARGB(0xFF, 0xDC, 0x26, 0x26));
        draw_frame(dev, &b3_r, COLOR_WHITE);
        H l3_w = tc_calc_string_width(modal->btn_right, 24);
        drw_tc_string(dev, b3_x + (btn_w - l3_w) / 2, btn_y + 10, modal->btn_right, COLOR_WHITE, ARGB(0xFF, 0xDC, 0x26, 0x26));
        return;
    }

    if (modal->type == FOMA_MODAL_ABOUT) {
        /* FOMA Application About Box (Height 420, Width 444) */
        H box_w = 444;
        H box_h = 420;
        H bx = (FOMA_SCREEN_W - box_w) / 2;
        H by = (FOMA_SCREEN_H - box_h) / 2 - 10;

        RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
        fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

        RECT box_r = { bx, by, bx + box_w, by + box_h };
        fill_rec(dev, &box_r, COLOR_WHITE);
        draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
        RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
        draw_frame(dev, &inner_r, FOMA_COL_GOLD);

        /* Title Bar */
        RECT hdr_r = { bx, by, bx + box_w, by + 32 };
        fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);
        drw_tc_string(dev, bx + box_w - 60, by + 8, "[情報]", FOMA_COL_GOLD, FOMA_COL_TITLE_BG);

        /* App Banner Plate (Icon + Name + Ver) */
        RECT sub_r = { bx + 12, by + 40, bx + box_w - 12, by + 82 };
        fill_rec(dev, &sub_r, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        draw_frame(dev, &sub_r, FOMA_COL_TITLE_BG);

        /* Decorative icon badge */
        RECT badge = { bx + 20, by + 45, bx + 52, by + 77 };
        fill_rec(dev, &badge, COLOR_LTGRAY);
        draw_frame(dev, &badge, COLOR_DKGRAY);
        char initial[2] = { (char)(modal->detail1[0] ? modal->detail1[0] : 'B'), 0 };
        drw_tc_string(dev, bx + 31, by + 53, initial, COLOR_NAVY, COLOR_LTGRAY);

        drw_tc_string(dev, bx + 62, by + 48, modal->detail1, FOMA_COL_TITLE_BG, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        drw_tc_string(dev, bx + 62, by + 65, "B-System 3.20 (FOMA Mobile Edition)", COLOR_DKGRAY, ARGB(0xFF, 0xE2, 0xE8, 0xF0));
        drw_tc_string(dev, bx + box_w - 68, by + 48, "[アプリ]", COLOR_WHITE, FOMA_COL_TITLE_BG);

        /* Metadata rows */
        const char *keys[] = {
            "機能詳細",
            "基盤OS",
            "画面表示",
            "文字仕様",
            "開発提供",
            "配布仕様"
        };
        const char *vals[6];
        vals[0] = modal->message[0] ? modal->message : "BTRON3 Native Application";
        vals[1] = "Sakamura T-Kernel 2.0 (Target 10: FOMA)";
        vals[2] = "480x640 VGA 縦画面 (Portrait HMI)";
        vals[3] = "TRON-Code 多国語文字 / TAD Rev 3.20";
        vals[4] = modal->detail2[0] ? modal->detail2 : "Brought to B-System by 5HT";
        vals[5] = "Copyright 2026 Synrc. MIT License.";

        for (int i = 0; i < 6; i++) {
            H ry = by + 90 + i * 44;
            RECT rr = { bx + 12, ry, bx + box_w - 12, ry + 38 };
            COLOR rbg = (i % 2 == 0) ? ARGB(0xFF, 0xF8, 0xFA, 0xFC) : ARGB(0xFF, 0xEE, 0xF2, 0xF6);
            fill_rec(dev, &rr, rbg);
            draw_frame(dev, &rr, ARGB(0xFF, 0xCB, 0xD5, 0xE1));

            drw_tc_string(dev, bx + 18, ry + 11, keys[i], FOMA_COL_TITLE_BG, rbg);

            char val_buf[128];
            strncpy(val_buf, vals[i], sizeof(val_buf) - 1);
            val_buf[sizeof(val_buf) - 1] = '\0';
            int max_w = rr.right - (bx + 96) - 8;
            while (strlen(val_buf) > 0 && tc_calc_string_width(val_buf, (int)strlen(val_buf)) > max_w) {
                val_buf[strlen(val_buf) - 1] = '\0';
            }
            drw_tc_string(dev, bx + 96, ry + 11, val_buf, COLOR_BLACK, rbg);
        }

        /* Single Center OK Button */
        H btn_w = 160;
        H btn_h = 36;
        H btn_y = by + box_h - 46;
        H btn_x = bx + (box_w - btn_w) / 2;
        RECT b_r = { btn_x, btn_y, btn_x + btn_w, btn_y + btn_h };
        COLOR b_bg = FOMA_COL_FOCUS_BG;
        COLOR b_fg = COLOR_WHITE;
        fill_rec(dev, &b_r, b_bg);
        draw_frame(dev, &b_r, COLOR_DKGRAY);
        const char *btn_lbl = modal->btn_center[0] ? modal->btn_center : "確認 (OK)";
        H lbl_w = tc_calc_string_width(btn_lbl, 24);
        drw_tc_string(dev, btn_x + (btn_w - lbl_w) / 2, btn_y + 9, btn_lbl, b_fg, b_bg);
        return;
    }

    /* Default FOMA_MODAL_CONFIRM (Delete Alert / Confirmation) */
    H box_w = 400;
    H box_h = 280;
    H bx = (FOMA_SCREEN_W - box_w) / 2;
    H by = (FOMA_SCREEN_H - box_h) / 2;

    RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
    fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

    RECT box_r = { bx, by, bx + box_w, by + box_h };
    fill_rec(dev, &box_r, COLOR_WHITE);
    draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
    RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
    draw_frame(dev, &inner_r, FOMA_COL_GOLD);

    /* Modal Title Bar */
    RECT hdr_r = { bx, by, bx + box_w, by + 32 };
    fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
    drw_tc_string(dev, bx + 12, by + 8, modal->title, COLOR_WHITE, FOMA_COL_TITLE_BG);

    /* Warning alert header */
    RECT warn_r = { bx + 16, by + 44, bx + 160, by + 70 };
    fill_rec(dev, &warn_r, ARGB(0xFF, 0xDC, 0x26, 0x26));
    drw_tc_string(dev, bx + 22, by + 48, "[！] 警告 (Alert)", COLOR_WHITE, ARGB(0xFF, 0xDC, 0x26, 0x26));

    if (modal->detail1[0]) {
        RECT tgt_r = { bx + 16, by + 76, bx + box_w - 16, by + 106 };
        fill_rec(dev, &tgt_r, ARGB(0xFF, 0xFE, 0xF2, 0xF2));
        draw_frame(dev, &tgt_r, ARGB(0xFF, 0xFE, 0xCA, 0xCA));
        drw_tc_string(dev, bx + 22, by + 82, "対象実身: ", COLOR_DKGRAY, ARGB(0xFF, 0xFE, 0xF2, 0xF2));
        drw_tc_string(dev, bx + 95, by + 82, modal->detail1, ARGB(0xFF, 0x99, 0x1B, 0x1B), ARGB(0xFF, 0xFE, 0xF2, 0xF2));
    }

    /* Message lines */
    drw_tc_string(dev, bx + 16, by + 118, modal->message, COLOR_BLACK, COLOR_WHITE);

    /* Dialog Buttons */
    H btn_w = 130;
    H btn_h = 34;
    H btn_y = by + box_h - 48;

    /* Button 1 (Left) */
    H b1_x = bx + 40;
    RECT b1_r = { b1_x, btn_y, b1_x + btn_w, btn_y + btn_h };
    COLOR b1_bg = (modal->selected_btn == 0) ? ARGB(0xFF, 0xDC, 0x26, 0x26) : FOMA_COL_SOFTKEY_BG;
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

/* ── Rendering: Popup Menu ── */
void foma_render_popup_menu(GDEV *dev, const FOMA_POPUP_MENU *menu) {
    if (!dev || !menu || !menu->is_active || menu->item_count <= 0) return;

    H box_w = 400;
    H row_h = 34;
    H box_h = 36 + menu->item_count * row_h + 10;
    H bx = (FOMA_SCREEN_W - box_w) / 2;
    H by = (FOMA_SCREEN_H - box_h) / 2 + 10;

    /* Drop shadow */
    RECT shadow = { bx + 6, by + 6, bx + box_w + 6, by + box_h + 6 };
    fill_rec(dev, &shadow, ARGB(0xFF, 0x10, 0x18, 0x20));

    /* Panel plate */
    RECT box_r = { bx, by, bx + box_w, by + box_h };
    fill_rec(dev, &box_r, ARGB(0xFF, 0xF5, 0xF7, 0xFA));
    draw_frame(dev, &box_r, FOMA_COL_TITLE_BG);
    RECT inner_r = { bx + 1, by + 1, bx + box_w - 1, by + box_h - 1 };
    draw_frame(dev, &inner_r, COLOR_WHITE);

    /* Menu Header */
    RECT hdr_r = { bx, by, bx + box_w, by + 32 };
    fill_rec(dev, &hdr_r, FOMA_COL_TITLE_BG);
    drw_tc_string(dev, bx + 12, by + 8, menu->title, COLOR_WHITE, FOMA_COL_TITLE_BG);
    H badge_w = tc_calc_string_width("MENU", 4);
    drw_tc_string(dev, bx + box_w - badge_w - 12, by + 8, "MENU", FOMA_COL_GOLD, FOMA_COL_TITLE_BG);

    /* Menu items */
    for (int i = 0; i < menu->item_count; i++) {
        H iy = by + 34 + i * row_h;
        BOOL is_foc = (i == menu->focus_index);
        RECT ir = { bx + 4, iy, bx + box_w - 4, iy + row_h - 2 };

        if (is_foc) {
            fill_rec(dev, &ir, FOMA_COL_FOCUS_BG);
            draw_frame(dev, &ir, COLOR_WHITE);
            drw_tc_string(dev, bx + 10, iy + 8, "▶", COLOR_YELLOW, FOMA_COL_FOCUS_BG);
        } else {
            COLOR bg = (i % 2 == 0) ? ARGB(0xFF, 0xEB, 0xEE, 0xF2) : COLOR_WHITE;
            fill_rec(dev, &ir, bg);
        }

        /* Title */
        COLOR fg = is_foc ? COLOR_WHITE : COLOR_BLACK;
        COLOR bg = is_foc ? FOMA_COL_FOCUS_BG : ((i % 2 == 0) ? ARGB(0xFF, 0xEB, 0xEE, 0xF2) : COLOR_WHITE);
        drw_tc_string(dev, bx + 28, iy + 8, menu->items[i], fg, bg);

        /* Optional badge */
        if (menu->badges[i][0]) {
            H bw = tc_calc_string_width(menu->badges[i], 16);
            H b_x = bx + box_w - bw - 16;
            if (is_foc) {
                drw_tc_string(dev, b_x, iy + 8, menu->badges[i], COLOR_YELLOW, FOMA_COL_FOCUS_BG);
            } else {
                RECT br = { b_x - 6, iy + 5, b_x + bw + 6, iy + 25 };
                fill_rec(dev, &br, FOMA_COL_BADGE_BG);
                drw_tc_string(dev, b_x, iy + 8, menu->badges[i], FOMA_COL_BADGE_TXT, FOMA_COL_BADGE_BG);
            }
        }
    }
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

    /* 6. Popup Menu Overlay (if open) */
    if (g_popup_menu.is_active) {
        foma_render_popup_menu(dev, &g_popup_menu);
    }
}
