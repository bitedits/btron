/*
 * B-System (BTRON 3.20) Global System Menu Bar
 * Authentic B-right/V Chokanji (超漢字) & Haiku Desktop Control Subsystem
 *
 * NASA JPL Rule 3 compliant: Bounded state, zero post-boot heap allocations.
 */

#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/tip.h>
#include <btron/about.h>
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

/* Weak linkage declarations for external app launchers */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) WND* open_vobj_manager_window(void);
__attribute__((weak)) WND* open_control_panel_window(void);
__attribute__((weak)) WND* open_t_editor_window(void);
__attribute__((weak)) WND* open_gterm_window(void);
__attribute__((weak)) WND* open_audio_player_window(void);
__attribute__((weak)) WND* open_orchestra_window(void);
__attribute__((weak)) WND* open_about_window(void);
__attribute__((weak)) WND* open_display_settings_window(void);
#else
extern WND* open_vobj_manager_window(void);
extern WND* open_control_panel_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* open_audio_player_window(void);
extern WND* open_orchestra_window(void);
extern WND* open_about_window(void);
extern WND* open_display_settings_window(void);
#endif

#define GMENU_DROPDOWN_WIDTH    280
#define GMENU_ROW_HEIGHT        22

/* Global Menu State Singleton */
typedef struct {
    int active_menu;       /* -1 = closed, 0..4 = active header index */
    int hover_header;      /* -1 = none, 0..4 = hovered header in closed/open state */
    int hover_item;        /* -1 = none, 0..N = hovered item in active dropdown */
    BOOL tip_hover;        /* TRUE if hovering over TIP mode badge */
} GlobalMenuState;

static GlobalMenuState g_gmenu;
static H s_gmenu_scr_w = 1280;

void global_menu_set_screen_width(H w) {
    if (w > 0) s_gmenu_scr_w = w;
}

/* Static menu headers and static items */
static GMenuHeader g_headers[GMENU_HEADER_COUNT] = {
    {
        .title = "［BTRON］",
        .rect = { 4, 2, 84, 23 },
        .item_count = 0, /* Handled by Deskbar tracker root launcher */
    },
    {
        .title = "システム(S)",
        .rect = { 92, 2, 196, 23 },
        .item_count = 9,
        .items = {
            { "システム情報 (About B-System...)", "Alt+?", GMENU_CMD_SYS_ABOUT, FALSE, FALSE, TRUE },
            { "環境設定 (Settings Cabinet...)",  "Alt+P", GMENU_CMD_SYS_SETTINGS, FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            { "音響機器 (Audio Cassette...)",   "", GMENU_CMD_SYS_AUDIO, FALSE, FALSE, TRUE },
            { "管弦楽演奏 (Orchestra DAW...)",   "", GMENU_CMD_SYS_ORCHESTRA, FALSE, FALSE, TRUE },
            { "画面表示 (Display Settings...)",  "", GMENU_CMD_SYS_DISPLAY, FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            { "デスクトップ再起動 (Restart)",     "", GMENU_CMD_SYS_RESTART, FALSE, FALSE, TRUE },
            { "システムの終了 (Shutdown...)",    "", GMENU_CMD_SYS_SHUTDOWN, FALSE, FALSE, TRUE }
        }
    },
    {
        .title = "実身・仮身(O)",
        .rect = { 202, 2, 322, 23 },
        .item_count = 4,
        .items = {
            { "実身キャビネット (Open Cabinet)", "Alt+O", GMENU_CMD_OBJ_CABINET, FALSE, FALSE, TRUE },
            { "実身・仮身の検索 (Search Fusen)", "Ctrl+F", GMENU_CMD_OBJ_SEARCH, FALSE, FALSE, TRUE },
            { "新規実身の作成 (New Real Object)", "Ctrl+N", GMENU_CMD_OBJ_NEW, FALSE, FALSE, TRUE },
            { "共有実身保管庫 (Shared Storage)",  "", GMENU_CMD_OBJ_STORAGE, FALSE, FALSE, TRUE }
        }
    },
    {
        .title = "ウィンドウ(W)",
        .rect = { 328, 2, 448, 23 },
        .item_count = 5, /* Dynamically expanded with open windows */
        .items = {
            { "重ねて整列 (Cascade Windows)",   "Shift+F5", GMENU_CMD_WND_CASCADE, FALSE, FALSE, TRUE },
            { "並べて整列 (Tile Windows)",      "Shift+F4", GMENU_CMD_WND_TILE, FALSE, FALSE, TRUE },
            { "すべて隠す (Hide All)",          "Ctrl+H",   GMENU_CMD_WND_HIDE_ALL, FALSE, FALSE, TRUE },
            { "次のウィンドウ (Cycle Focus)",    "Alt+Tab",  GMENU_CMD_WND_CYCLE, FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE }
        }
    },
    {
        .title = "道具・文字(T)",
        .rect = { 454, 2, 574, 23 },
        .item_count = 7,
        .items = {
            { "文字パレット (TRON Palette)",     "F12", GMENU_CMD_TOOL_PALETTE, FALSE, FALSE, TRUE },
            { "TRONコード検索 (TRON-Code)",     "",    GMENU_CMD_TOOL_TRONCODE, FALSE, FALSE, TRUE },
            { "Mozc 日本語辞書 (IME Tool)",     "",    GMENU_CMD_TOOL_MOZC_DICT, FALSE, FALSE, TRUE },
            { "---", "", GMENU_CMD_NONE, TRUE, FALSE, FALSE },
            { "文書編集 (Editor)",            "",    GMENU_CMD_TOOL_TEDITOR, FALSE, FALSE, TRUE },
            { "表計算・APL (Matrix)",           "",    GMENU_CMD_TOOL_MATRIX, FALSE, FALSE, TRUE },
            { "端末 (gterm Terminal)",          "",    GMENU_CMD_TOOL_TERMINAL, FALSE, FALSE, TRUE }
        }
    }
};

static void draw_etched_sep_v(GDEV *dev, H x, H y1, H y2) {
    if (!dev) return;
    RECT s1 = { x, y1, x + 1, y2 };
    RECT s2 = { x + 1, y1, x + 2, y2 };
    fill_rec(dev, &s1, COLOR_DKGRAY);
    fill_rec(dev, &s2, COLOR_WHITE);
}

void global_menu_init(void) {
    g_gmenu.active_menu = -1;
    g_gmenu.hover_header = -1;
    g_gmenu.hover_item = -1;
    g_gmenu.tip_hover = FALSE;
}

void global_menu_close(void) {
    g_gmenu.active_menu = -1;
    g_gmenu.hover_item = -1;
    if (tracker_is_menu_open()) {
        tracker_close_menu();
    }
}

BOOL global_menu_is_open(void) {
    return (g_gmenu.active_menu >= 0) || tracker_is_menu_open();
}

int global_menu_get_active(void) {
    return g_gmenu.active_menu;
}

int global_menu_get_hover_header(void) {
    return g_gmenu.hover_header;
}

/* Refresh the Window menu with active open window titles */
static void refresh_window_menu(void) {
    GMenuHeader *whdr = &g_headers[GMENU_HDR_WINDOWS];
    whdr->item_count = 5; /* Reset to static 5 base items */

    WND *w = get_wnd_list();
    int w_idx = 0;
    while (w && whdr->item_count < GMENU_MAX_ITEMS) {
        if (w->visible) {
            GMenuItem *it = &whdr->items[whdr->item_count];
            snprintf(it->label, sizeof(it->label), "%s%.59s",
                     w->focused ? "[*] " : "[ ] ",
                     w->title[0] ? w->title : "ウィンドウ");
            it->shortcut[0] = '\0';
            it->cmd_id = GMENU_CMD_WND_SELECT_BASE + w_idx;
            it->is_separator = FALSE;
            it->is_checked = w->focused;
            it->enabled = TRUE;
            whdr->item_count++;
            w_idx++;
        }
        w = w->next;
    }
}

/* Format Japanese Clock with Kanji Weekday */
static void get_japanese_calendar_string(char *buf, size_t max_len) {
    if (!buf || max_len < 32) return;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    if (!tm_now) {
        snprintf(buf, max_len, "9月4日(金) 00:00:00");
        return;
    }

    static const char *weekdays_jp[7] = {
        "(日)", "(月)", "(火)", "(水)", "(木)", "(金)", "(土)"
    };
    int wday = tm_now->tm_wday;
    if (wday < 0 || wday > 6) wday = 0;

    snprintf(buf, max_len, "%d月%d日%s %02d:%02d:%02d",
             tm_now->tm_mon + 1,
             tm_now->tm_mday,
             weekdays_jp[wday],
             tm_now->tm_hour,
             tm_now->tm_min,
             tm_now->tm_sec);
#else
    snprintf(buf, max_len, "9月4日(金) 12:00:00");
#endif
}

void global_menu_render_bar(GDEV *dev) {
    if (!dev) return;
    if (dev->width > 0) s_gmenu_scr_w = dev->width;

    /* Base Bar Plate (y = 0..25) */
    RECT bar = { 0, 0, dev->width, 25 };
    fill_rec(dev, &bar, COLOR_LTGRAY);
    drw_lin(dev, 0, 25, dev->width, 25);

    /* 1. Header Buttons */
    for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
        const GMenuHeader *hdr = &g_headers[h];
        RECT hr = hdr->rect;

        if (h == GMENU_HDR_BTRON) {
            /* Render Authentic Haiku / BTRON Deskbar Button */
            tracker_render_button(dev);
        } else {
            BOOL is_active = (g_gmenu.active_menu == h);
            BOOL is_hover = (g_gmenu.hover_header == h);

            if (is_active) {
                fill_rec(dev, &hr, COLOR_NAVY);
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_WHITE, 0x00000000);
            } else if (is_hover) {
                fill_rec(dev, &hr, COLOR_WHITE);
                drw_rec(dev, &hr);
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_NAVY, 0x00000000);
            } else {
                drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_BLACK, 0x00000000);
            }
        }

        /* 3D Etched separator following header */
        if (h < GMENU_HEADER_COUNT - 1) {
            H sep_x = (hdr->rect.right + g_headers[h + 1].rect.left) / 2;
            draw_etched_sep_v(dev, sep_x, 3, 23);
        }
    }

    /* Etched separator after the last header */
    draw_etched_sep_v(dev, g_headers[GMENU_HEADER_COUNT - 1].rect.right + 4, 3, 23);

    /* 2. Japanese Calendar Clock Plate (Right-most section) */
    int tray_w = 214;
    RECT tray_r = { (H)(dev->width - tray_w - 4), 3, (H)(dev->width - 4), 23 };
    fill_rec(dev, &tray_r, COLOR_LTGRAY);
    /* 3D Recessed border */
    drw_lin(dev, tray_r.left, tray_r.top, tray_r.right - 1, tray_r.top);
    drw_lin(dev, tray_r.left, tray_r.top, tray_r.left, tray_r.bottom - 1);
    drw_lin(dev, tray_r.left + 1, tray_r.bottom - 1, tray_r.right - 1, tray_r.bottom - 1);
    drw_lin(dev, tray_r.right - 1, tray_r.top + 1, tray_r.right - 1, tray_r.bottom - 1);

    char cal_buf[48];
    get_japanese_calendar_string(cal_buf, sizeof(cal_buf));
    int cal_w = tc_calc_string_width(cal_buf, (int)strlen(cal_buf));
    int cal_x = tray_r.left + (tray_w - cal_w) / 2;
    drw_tc_string(dev, cal_x, 4, cal_buf, COLOR_NAVY, 0x00000000);

    /* 3. Global TIP Mode Badge Button */
    RECT tip_btn = { (H)(tray_r.left - 134), 3, (H)(tray_r.left - 6), 23 };
    COLOR tip_bg = (tip_get_mode() == TIP_MODE_ASCII) ? COLOR_LTGRAY : COLOR_CYAN;
    if (g_gmenu.tip_hover) {
        tip_bg = COLOR_WHITE;
    }
    fill_rec(dev, &tip_btn, tip_bg);
    drw_rec(dev, &tip_btn);

    const char *mode_name = (tip_get_mode() == TIP_MODE_HIRAGANA) ? "あ" :
                            ((tip_get_mode() == TIP_MODE_KATAKANA) ? "ア" :
                             ((tip_get_mode() == TIP_MODE_TIBETAN) ? "བོད" : "A"));
    char tip_str[32];
    snprintf(tip_str, sizeof(tip_str), "[TIP: %s (F10)]", mode_name);
    int tip_w = tc_calc_string_width(tip_str, (int)strlen(tip_str));
    int tip_x = tip_btn.left + (128 - tip_w) / 2;
    drw_tc_string(dev, tip_x, 4, tip_str, COLOR_BLACK, 0x00000000);
}

void global_menu_render_overlay(GDEV *dev) {
    if (!dev) return;

    /* If Header 0 is active, render Deskbar Tracker Menu */
    if (g_gmenu.active_menu == GMENU_HDR_BTRON || tracker_is_menu_open()) {
        tracker_render_menu(dev);
        return;
    }

    if (g_gmenu.active_menu < 1 || g_gmenu.active_menu >= GMENU_HEADER_COUNT) {
        return;
    }

    const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
    H menu_x = hdr->rect.left;
    H menu_y = 25;
    H menu_w = GMENU_DROPDOWN_WIDTH;
    H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

    RECT mr = { menu_x, menu_y, menu_x + menu_w, menu_y + menu_h };

    APP_MENU_STYLE style = app_menu_get_global_style();
    if (style == APP_MENU_STYLE_CLASSIC_3D) {
        /* Style 1: Classic Chokanji 3D Beveled Box Plate */
        app_menu_draw_3d_bevel_box(dev, &mr);
    } else {
        /* Style 2: Modern Flat Card with Soft Drop Shadow */
        RECT shadow = { menu_x + 3, menu_y + 3, menu_x + menu_w + 3, menu_y + menu_h + 3 };
        fill_rec(dev, &shadow, COLOR_DKGRAY);

        fill_rec(dev, &mr, COLOR_WHITE);
        drw_rec(dev, &mr);

        drw_lin(dev, mr.left + 1, mr.top + 1, mr.right - 2, mr.top + 1);
        drw_lin(dev, mr.left + 1, mr.top + 1, mr.left + 1, mr.bottom - 2);
    }

    for (int i = 0; i < hdr->item_count; i++) {
        const GMenuItem *it = &hdr->items[i];
        RECT ir = { menu_x + 3, menu_y + 3 + i * GMENU_ROW_HEIGHT,
                    menu_x + menu_w - 3, menu_y + 3 + (i + 1) * GMENU_ROW_HEIGHT };

        if (it->is_separator) {
            H sep_y = (ir.top + ir.bottom) / 2;
            drw_lin(dev, ir.left + 4, sep_y, ir.right - 4, sep_y);
            continue;
        }

        BOOL is_hov = (g_gmenu.hover_item == i);
        if (is_hov) {
            fill_rec(dev, &ir, COLOR_NAVY);
        }

        COLOR txt_col = is_hov ? COLOR_WHITE : (it->enabled ? COLOR_BLACK : COLOR_GRAY);

        /* Item Label */
        drw_tc_string(dev, ir.left + 10, ir.top + 3, it->label, txt_col, 0x00000000);

        /* Keyboard Accelerator right aligned */
        if (it->shortcut[0]) {
            int sc_w = tc_calc_string_width(it->shortcut, (int)strlen(it->shortcut));
            drw_tc_string(dev, ir.right - sc_w - 12, ir.top + 3, it->shortcut, txt_col, 0x00000000);
        }
    }
}

static void global_menu_execute_cmd(int cmd) {
    global_menu_close();

    switch (cmd) {
        case GMENU_CMD_SYS_ABOUT:
            if (open_about_window) open_about_window();
            break;
        case GMENU_CMD_SYS_SETTINGS:
            if (open_control_panel_window) open_control_panel_window();
            break;
        case GMENU_CMD_SYS_AUDIO:
            if (open_audio_player_window) open_audio_player_window();
            break;
        case GMENU_CMD_SYS_ORCHESTRA:
            if (open_orchestra_window) open_orchestra_window();
            break;
        case GMENU_CMD_SYS_DISPLAY:
            if (open_display_settings_window) open_display_settings_window();
            break;
        case GMENU_CMD_SYS_RESTART:
            break;
        case GMENU_CMD_SYS_SHUTDOWN:
            break;

        case GMENU_CMD_OBJ_CABINET:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;
        case GMENU_CMD_OBJ_SEARCH:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;
        case GMENU_CMD_OBJ_NEW:
            if (open_t_editor_window) open_t_editor_window();
            break;
        case GMENU_CMD_OBJ_STORAGE:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;

        case GMENU_CMD_WND_CASCADE:
            wnd_cascade_all();
            break;
        case GMENU_CMD_WND_TILE:
            wnd_tile_all();
            break;
        case GMENU_CMD_WND_HIDE_ALL:
            wnd_hide_all();
            break;
        case GMENU_CMD_WND_CYCLE:
            wnd_cycle_focus();
            break;

        case GMENU_CMD_TOOL_PALETTE:
        case GMENU_CMD_TOOL_TRONCODE:
        case GMENU_CMD_TOOL_MOZC_DICT:
            break;
        case GMENU_CMD_TOOL_TEDITOR:
            if (open_t_editor_window) open_t_editor_window();
            break;
        case GMENU_CMD_TOOL_MATRIX:
            break;
        case GMENU_CMD_TOOL_TERMINAL:
            if (open_gterm_window) open_gterm_window();
            break;

        default:
            if (cmd >= GMENU_CMD_WND_SELECT_BASE) {
                int target_w_idx = cmd - GMENU_CMD_WND_SELECT_BASE;
                WND *w = get_wnd_list();
                int idx = 0;
                while (w) {
                    if (w->visible) {
                        if (idx == target_w_idx) {
                            top_wnd(w);
                            break;
                        }
                        idx++;
                    }
                    w = w->next;
                }
            }
            break;
    }
}

BOOL global_menu_handle_mouse_move(H x, H y) {
    /* Check TIP badge hover */
    H sw = s_gmenu_scr_w > 0 ? s_gmenu_scr_w : 1280;
    RECT tip_btn = { (H)(sw - 214 - 134 - 4), 3, (H)(sw - 214 - 4 - 6), 23 };
    g_gmenu.tip_hover = (x >= tip_btn.left && x <= tip_btn.right && y >= tip_btn.top && y <= tip_btn.bottom);

    /* 1. When a menu is actively open */
    if (g_gmenu.active_menu >= 0) {
        /* Check if hovering over top headers to switch menu (BeOS hot tracking) */
        if (y >= 0 && y <= 25) {
            for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
                if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                    if (g_gmenu.active_menu != h) {
                        g_gmenu.active_menu = h;
                        g_gmenu.hover_header = h;
                        g_gmenu.hover_item = -1;
                        if (h == GMENU_HDR_BTRON) {
                            tracker_open_menu();
                        } else {
                            if (tracker_is_menu_open()) tracker_close_menu();
                            if (h == GMENU_HDR_WINDOWS) refresh_window_menu();
                        }
                    }
                    return TRUE;
                }
            }
        }

        /* Check if hovering inside active dropdown menu */
        if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
            const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
            H menu_x = hdr->rect.left;
            H menu_y = 25;
            H menu_w = GMENU_DROPDOWN_WIDTH;
            H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

            if (x >= menu_x && x <= menu_x + menu_w && y >= menu_y && y <= menu_y + menu_h) {
                int item_idx = (y - (menu_y + 3)) / GMENU_ROW_HEIGHT;
                if (item_idx >= 0 && item_idx < hdr->item_count) {
                    if (!hdr->items[item_idx].is_separator && hdr->items[item_idx].enabled) {
                        g_gmenu.hover_item = item_idx;
                    } else {
                        g_gmenu.hover_item = -1;
                    }
                }
                return TRUE;
            } else {
                g_gmenu.hover_item = -1;
            }
        }

        /* Tracker menu hover tracking */
        if (g_gmenu.active_menu == GMENU_HDR_BTRON && tracker_is_menu_open()) {
            return tracker_handle_mouse_move(x, y);
        }

        return FALSE;
    }

    /* 2. When no menu is open: track top-level header hover highlighting */
    if (y >= 0 && y <= 25) {
        for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
            if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                g_gmenu.hover_header = h;
                return TRUE;
            }
        }
    }

    g_gmenu.hover_header = -1;
    return FALSE;
}

BOOL global_menu_handle_mouse_down(H x, H y) {
    /* 1. Check Global TIP Mode Badge Click */
    H sw = s_gmenu_scr_w > 0 ? s_gmenu_scr_w : 1280;
    RECT tip_btn = { (H)(sw - 214 - 134 - 4), 3, (H)(sw - 214 - 4 - 6), 23 };
    if (x >= tip_btn.left && x <= tip_btn.right && y >= tip_btn.top && y <= tip_btn.bottom) {
        tip_toggle_mode();
        return TRUE;
    }

    /* 2. Check Top Header Clicks (y = 0..25) */
    if (y >= 0 && y <= 25) {
        for (int h = 0; h < GMENU_HEADER_COUNT; h++) {
            if (x >= g_headers[h].rect.left && x <= g_headers[h].rect.right) {
                if (g_gmenu.active_menu == h) {
                    /* Click on already open header closes it */
                    global_menu_close();
                } else {
                    g_gmenu.active_menu = h;
                    g_gmenu.hover_header = h;
                    g_gmenu.hover_item = -1;
                    if (h == GMENU_HDR_BTRON) {
                        tracker_open_menu();
                    } else {
                        if (tracker_is_menu_open()) tracker_close_menu();
                        if (h == GMENU_HDR_WINDOWS) refresh_window_menu();
                    }
                }
                return TRUE;
            }
        }
    }

    /* 3. Check clicks inside active dropdown menu */
    if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
        const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
        H menu_x = hdr->rect.left;
        H menu_y = 25;
        H menu_w = GMENU_DROPDOWN_WIDTH;
        H menu_h = hdr->item_count * GMENU_ROW_HEIGHT + 6;

        if (x >= menu_x && x <= menu_x + menu_w && y >= menu_y && y <= menu_y + menu_h) {
            int item_idx = (y - (menu_y + 3)) / GMENU_ROW_HEIGHT;
            if (item_idx >= 0 && item_idx < hdr->item_count) {
                const GMenuItem *it = &hdr->items[item_idx];
                if (!it->is_separator && it->enabled) {
                    global_menu_execute_cmd(it->cmd_id);
                }
            }
            return TRUE;
        }
    }

    /* 4. Click on Tracker root menu */
    if (g_gmenu.active_menu == GMENU_HDR_BTRON && tracker_is_menu_open()) {
        if (tracker_handle_mouse_down(x, y)) {
            if (!tracker_is_menu_open()) {
                g_gmenu.active_menu = -1;
            }
            return TRUE;
        }
    }

    /* 5. Click outside closes any active global menu */
    if (g_gmenu.active_menu >= 0) {
        global_menu_close();
        return TRUE;
    }

    return FALSE;
}

BOOL global_menu_handle_key(UW key, VW mod) {
    (void)mod;
    if (!global_menu_is_open()) return FALSE;

    if (key == BTRON_KEY_ESCAPE) {
        global_menu_close();
        return TRUE;
    }

    /* Enter or Space triggers active item */
    if (key == '\r' || key == '\n' || key == ' ') {
        if (g_gmenu.active_menu > 0 && g_gmenu.active_menu < GMENU_HEADER_COUNT) {
            const GMenuHeader *hdr = &g_headers[g_gmenu.active_menu];
            if (g_gmenu.hover_item >= 0 && g_gmenu.hover_item < hdr->item_count) {
                global_menu_execute_cmd(hdr->items[g_gmenu.hover_item].cmd_id);
                return TRUE;
            }
        }
    }

    return FALSE;
}
