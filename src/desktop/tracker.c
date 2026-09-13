/*
 * B-System (BTRON 3.20) Haiku-Style Start [BTRON] Button & Task Tracker
 * Pure C99, O(1) low-latency, bounded verifiable state machine.
 */

#include <btron/tracker.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <btron/apps.h>
#include <btron/about.h>
#include <btron/app_menu.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <string.h>
#endif

/* Freestanding-safe string helpers */
static void tracker_safe_copy(char *dst, const char *src, int max_len) {
    if (!dst || max_len <= 0) return;
    int i = 0;
    if (src) {
        while (src[i] && i < max_len - 1) {
            dst[i] = src[i];
            i++;
        }
    }
    dst[i] = '\0';
}

static void tracker_format_wnd_title(char *dst, int max_len, BOOL focused, const char *title) {
    if (!dst || max_len <= 0) return;
    int pos = 0;
    const char *prefix = focused ? "[*] " : "[ ] ";
    while (prefix[pos] && pos < max_len - 1) {
        dst[pos] = prefix[pos];
        pos++;
    }
    if (title && title[0]) {
        int i = 0;
        while (title[i] && pos < max_len - 1) {
            dst[pos++] = title[i++];
        }
    } else {
        const char *def = "Window";
        int i = 0;
        while (def[i] && pos < max_len - 1) {
            dst[pos++] = def[i++];
        }
    }
    dst[pos] = '\0';
}

/* Forward declarations of BTRON application entry points with weak linkage */
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak, weak_import)) WND* open_vobj_manager_window(void);
__attribute__((weak, weak_import)) WND* open_control_panel_window(void);
__attribute__((weak, weak_import)) WND* open_t_editor_window(void);
__attribute__((weak, weak_import)) WND* open_gterm_window(void);
__attribute__((weak, weak_import)) WND* open_audio_player_window(void);
__attribute__((weak, weak_import)) WND* open_orchestra_window(void);
__attribute__((weak, weak_import)) WND* open_drivesetup_window(void);
__attribute__((weak, weak_import)) WND* launch_beos_chat(void);
#else
extern WND* open_vobj_manager_window(void);
extern WND* open_control_panel_window(void);
extern WND* open_t_editor_window(void);
extern WND* open_gterm_window(void);
extern WND* open_audio_player_window(void);
extern WND* open_orchestra_window(void);
extern WND* open_drivesetup_window(void);
extern WND* launch_beos_chat(void);
#endif

/* Static Tracker Singleton - NASA JPL Rule: No runtime heap allocations */
static TRACKER g_tracker;

/* Invariant verification */
BOOL tracker_verify_invariants(void) {
    if ((int)g_tracker.state < TRACKER_STATE_NORMAL || (int)g_tracker.state > TRACKER_STATE_OPEN) {
        return FALSE;
    }
    if (g_tracker.item_count < 0 || g_tracker.item_count > TRACKER_MAX_ITEMS) {
        return FALSE;
    }
    if (g_tracker.hover_index < -1 || g_tracker.hover_index >= g_tracker.item_count) {
        return FALSE;
    }
    if (g_tracker.btn_rect.right <= g_tracker.btn_rect.left ||
        g_tracker.btn_rect.bottom <= g_tracker.btn_rect.top) {
        return FALSE;
    }
    if (g_tracker.menu_rect.right <= g_tracker.menu_rect.left ||
        g_tracker.menu_rect.bottom <= g_tracker.menu_rect.top) {
        return FALSE;
    }
    return TRUE;
}

const TRACKER* tracker_get_state(void) {
    return &g_tracker;
}

static void tracker_add_item(TRACKER_CMD_TYPE type, const char *label, WND *target_wnd) {
    if (g_tracker.item_count >= TRACKER_MAX_ITEMS) return;
    H idx = g_tracker.item_count;
    g_tracker.items[idx].type = type;
    g_tracker.items[idx].target_wnd = target_wnd;
    g_tracker.items[idx].enabled = (type != TRACKER_CMD_SEPARATOR);
    
    if (label) {
        tracker_safe_copy(g_tracker.items[idx].label, label, sizeof(g_tracker.items[idx].label));
    } else {
        g_tracker.items[idx].label[0] = '\0';
    }
    g_tracker.item_count++;
}

void tracker_refresh_windows(void) {
    g_tracker.item_count = 0;

    /* 1. Core Knowledge & System Applications */
    tracker_add_item(TRACKER_CMD_CABINET,   "実身・仮身 (Cabinet)", NULL);
    tracker_add_item(TRACKER_CMD_SETTINGS,  "環境設定 (Control Panel)", NULL);
    tracker_add_item(TRACKER_CMD_TEDITOR,   "Editor (文書編集)", NULL);
    tracker_add_item(TRACKER_CMD_MATRIX,    "Matrix (表計算・APL)", NULL);
    tracker_add_item(TRACKER_CMD_TERMINAL,  "Terminal (gterm 端末)", NULL);
    tracker_add_item(TRACKER_CMD_AUDIODECK, "Cassette (カセットデッキ)", NULL);
    tracker_add_item(TRACKER_CMD_ORCHESTRA, "管弦楽・MIDI (Orchestra)", NULL);
    tracker_add_item(TRACKER_CMD_DRIVESETUP, "DriveSetup (ディスク管理)", NULL);
    tracker_add_item(TRACKER_CMD_CHAT,      "Mail & Chat (対話通信)", NULL);
    tracker_add_item(TRACKER_CMD_SEPARATOR, "------------------------", NULL);

    /* 2. Dynamic Active Window / Task Tracking (Haiku Deskbar window list) */
    WND *w = get_wnd_list();
    H tracked_wnds = 0;
    while (w && tracked_wnds < 8 && g_tracker.item_count < TRACKER_MAX_ITEMS - 3) {
        char item_buf[48];
        tracker_format_wnd_title(item_buf, sizeof(item_buf), w->focused, w->title);
        tracker_add_item(TRACKER_CMD_WND_FOCUS, item_buf, w);
        tracked_wnds++;
        w = w->next;
    }

    if (tracked_wnds > 0) {
        tracker_add_item(TRACKER_CMD_SEPARATOR, "------------------------", NULL);
    }

    /* 3. System Management */
    tracker_add_item(TRACKER_CMD_ABOUT,    "システム情報 (About BTRON)", NULL);
    tracker_add_item(TRACKER_CMD_RESTART,  "デスクトップ再起動", NULL);

    /* 4. Calculate size of widest menu item before drop down so item never overflows */
    H menu_w = tracker_calc_widest_item_width();

    /* Recalculate menu geometry dynamically */
    g_tracker.menu_rect.left = g_tracker.btn_rect.left;
    g_tracker.menu_rect.top = 26;
    g_tracker.menu_rect.right = g_tracker.menu_rect.left + menu_w;
    g_tracker.menu_rect.bottom = 26 + (g_tracker.item_count * TRACKER_ITEM_HEIGHT) + 6;
}

H tracker_calc_text_width(const char *text) {
    if (!text) return 0;
    H total_w = 0;
    const unsigned char *p = (const unsigned char *)text;
    while (*p) {
        if (*p == '\n') {
            p++;
            continue;
        }
        if (*p < 0x80) {
            total_w += 8;
            p++;
        } else if ((*p & 0xE0) == 0xC0) {
            total_w += 16;
            p += (p[1] != '\0' ? 2 : 1);
        } else if ((*p & 0xF0) == 0xE0) {
            total_w += 16;
            p += (p[1] && p[2] ? 3 : 1);
        } else if ((*p & 0xF8) == 0xF0) {
            total_w += 16;
            p += (p[1] && p[2] && p[3] ? 4 : 1);
        } else {
            total_w += 8;
            p++;
        }
    }
    return total_w;
}

H tracker_calc_widest_item_width(void) {
    H max_w = TRACKER_MENU_MIN_WIDTH;
    const H PADDING = 28; /* Left accent bar + text offset (10px) + right padding (18px) */

    for (H i = 0; i < g_tracker.item_count; i++) {
        if (g_tracker.items[i].type == TRACKER_CMD_SEPARATOR) continue;
        H item_text_w = tracker_calc_text_width(g_tracker.items[i].label);
        H item_total_w = item_text_w + PADDING;
        if (item_total_w > max_w) {
            max_w = item_total_w;
        }
    }
    return max_w;
}

ER tracker_init(void) {
    g_tracker.state = TRACKER_STATE_NORMAL;
    
    /* Top-left START button in system panel */
    g_tracker.btn_rect.left = 4;
    g_tracker.btn_rect.top = 3;
    g_tracker.btn_rect.right = 4 + TRACKER_BTN_WIDTH;
    g_tracker.btn_rect.bottom = 3 + TRACKER_BTN_HEIGHT;
    
    g_tracker.hover_index = -1;
    g_tracker.launch_count = 0;

    tracker_refresh_windows();
    return E_OK;
}

BOOL tracker_hit_button(H x, H y) {
    return (x >= g_tracker.btn_rect.left && x <= g_tracker.btn_rect.right &&
            y >= g_tracker.btn_rect.top && y <= g_tracker.btn_rect.bottom);
}

BOOL tracker_hit_menu(H x, H y) {
    if (g_tracker.state != TRACKER_STATE_OPEN) return FALSE;
    return (x >= g_tracker.menu_rect.left && x <= g_tracker.menu_rect.right &&
            y >= g_tracker.menu_rect.top && y <= g_tracker.menu_rect.bottom);
}

BOOL tracker_is_menu_open(void) {
    return (g_tracker.state == TRACKER_STATE_OPEN);
}

void tracker_open_menu(void) {
    tracker_refresh_windows();
    g_tracker.state = TRACKER_STATE_OPEN;
    g_tracker.hover_index = 0;
}

void tracker_close_menu(void) {
    g_tracker.state = TRACKER_STATE_NORMAL;
    g_tracker.hover_index = -1;
}

void tracker_toggle_menu(void) {
    if (tracker_is_menu_open()) {
        tracker_close_menu();
    } else {
        tracker_open_menu();
    }
}

static void tracker_execute_item(H index) {
    if (index < 0 || index >= g_tracker.item_count) return;
    TRACKER_ITEM *item = &g_tracker.items[index];
    if (!item->enabled) return;

    g_tracker.launch_count++;

    switch (item->type) {
        case TRACKER_CMD_CABINET:
            if (open_vobj_manager_window) open_vobj_manager_window();
            break;
        case TRACKER_CMD_SETTINGS:
            if (open_control_panel_window) open_control_panel_window();
            break;
        case TRACKER_CMD_TEDITOR:
            if (open_t_editor_window) open_t_editor_window();
            break;
        case TRACKER_CMD_TERMINAL:
            if (open_gterm_window) open_gterm_window();
            break;
        case TRACKER_CMD_AUDIODECK:
            if (open_audio_player_window) open_audio_player_window();
            break;
        case TRACKER_CMD_ORCHESTRA:
            if (open_orchestra_window) open_orchestra_window();
            break;
        case TRACKER_CMD_DRIVESETUP:
            if (open_drivesetup_window) open_drivesetup_window();
            break;
        case TRACKER_CMD_CHAT:
            if (launch_beos_chat) launch_beos_chat();
            break;
        case TRACKER_CMD_WND_FOCUS:
            if (item->target_wnd) {
                top_wnd(item->target_wnd);
                item->target_wnd->focused = TRUE;
            }
            break;
        case TRACKER_CMD_ABOUT:
            open_about_window();
            break;
        case TRACKER_CMD_RESTART:
        case TRACKER_CMD_SHUTDOWN:
        default:
            break;
    }
}

BOOL tracker_handle_mouse_down(H x, H y) {
    /* 1. Click on START [BTRON] Button */
    if (tracker_hit_button(x, y)) {
        tracker_toggle_menu();
        return TRUE;
    }

    /* 2. Click inside open menu */
    if (tracker_is_menu_open()) {
        if (tracker_hit_menu(x, y)) {
            H rel_y = y - (g_tracker.menu_rect.top + 3);
            if (rel_y >= 0) {
                H idx = rel_y / TRACKER_ITEM_HEIGHT;
                if (idx >= 0 && idx < g_tracker.item_count) {
                    tracker_execute_item(idx);
                    tracker_close_menu();
                    return TRUE;
                }
            }
        }
        /* Clicked outside menu -> dismiss menu */
        tracker_close_menu();
        return TRUE;
    }

    return FALSE;
}

BOOL tracker_handle_mouse_move(H x, H y) {
    if (tracker_is_menu_open()) {
        if (tracker_hit_menu(x, y)) {
            H rel_y = y - (g_tracker.menu_rect.top + 3);
            if (rel_y >= 0) {
                H idx = rel_y / TRACKER_ITEM_HEIGHT;
                if (idx >= 0 && idx < g_tracker.item_count) {
                    if (g_tracker.items[idx].type != TRACKER_CMD_SEPARATOR) {
                        g_tracker.hover_index = idx;
                        return TRUE;
                    }
                }
            }
        }
        return TRUE;
    }

    /* Hover state on START button */
    if (tracker_hit_button(x, y)) {
        if (g_tracker.state == TRACKER_STATE_NORMAL) {
            g_tracker.state = TRACKER_STATE_HOVER;
            return TRUE;
        }
    } else {
        if (g_tracker.state == TRACKER_STATE_HOVER) {
            g_tracker.state = TRACKER_STATE_NORMAL;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL tracker_handle_mouse_up(H x, H y) {
    (void)x;
    (void)y;
    return FALSE;
}

BOOL tracker_handle_key(W key_code) {
    /* F1 or Super key toggles start menu */
    if (key_code == 0x101 /* F1 */ || key_code == 0x120 /* Super */) {
        tracker_toggle_menu();
        return TRUE;
    }

    if (!tracker_is_menu_open()) return FALSE;

    /* Escape closes menu */
    if (key_code == 0x1B) {
        tracker_close_menu();
        return TRUE;
    }

    /* Down arrow */
    if (key_code == 0x112 || key_code == 's') {
        H next = g_tracker.hover_index + 1;
        while (next < g_tracker.item_count && g_tracker.items[next].type == TRACKER_CMD_SEPARATOR) {
            next++;
        }
        if (next < g_tracker.item_count) {
            g_tracker.hover_index = next;
        }
        return TRUE;
    }

    /* Up arrow */
    if (key_code == 0x111 || key_code == 'w') {
        H prev = g_tracker.hover_index - 1;
        while (prev >= 0 && g_tracker.items[prev].type == TRACKER_CMD_SEPARATOR) {
            prev--;
        }
        if (prev >= 0) {
            g_tracker.hover_index = prev;
        }
        return TRUE;
    }

    /* Enter activates item */
    if (key_code == 0x0D || key_code == ' ') {
        if (g_tracker.hover_index >= 0 && g_tracker.hover_index < g_tracker.item_count) {
            tracker_execute_item(g_tracker.hover_index);
            tracker_close_menu();
            return TRUE;
        }
    }

    return TRUE;
}

void tracker_render_button(GDEV *dev) {
    if (!dev) return;

    RECT *r = &g_tracker.btn_rect;
    BOOL is_open = (g_tracker.state == TRACKER_STATE_OPEN || g_tracker.state == TRACKER_STATE_PRESSED);

    if (is_open) {
        /* Pressed / Open state: Inverted 3D bevel */
        fill_rec(dev, r, COLOR_NAVY);
        drw_rec(dev, r);
        /* Bevel shadow on top/left, light on bottom/right */
        drw_lin(dev, r->left, r->top, r->right - 1, r->top);
        drw_lin(dev, r->left, r->top, r->left, r->bottom - 1);
        drw_tc_string(dev, r->left + 4, r->top + 3, "［BTRON］", COLOR_WHITE, COLOR_NAVY);
    } else {
        /* Haiku-Style Start Button: Sleek bevel with blue/yellow accent */
        fill_rec(dev, r, COLOR_LTGRAY);
        drw_rec(dev, r);

        /* 3D Highlight Bevel (Top & Left) */
        RECT hl_t = { r->left + 1, r->top + 1, r->right - 1, r->top + 2 };
        fill_rec(dev, &hl_t, COLOR_WHITE);
        RECT hl_l = { r->left + 1, r->top + 1, r->left + 2, r->bottom - 1 };
        fill_rec(dev, &hl_l, COLOR_WHITE);

        /* 3D Shadow Bevel (Bottom & Right) */
        RECT sh_b = { r->left + 1, r->bottom - 2, r->right - 1, r->bottom - 1 };
        fill_rec(dev, &sh_b, COLOR_GRAY);
        RECT sh_r = { r->right - 2, r->top + 1, r->right - 1, r->bottom - 1 };
        fill_rec(dev, &sh_r, COLOR_GRAY);

        /* Haiku-style Start Accent bar (Left edge yellow/orange strip) */
        RECT accent = { r->left + 3, r->top + 3, r->left + 6, r->bottom - 3 };
        fill_rec(dev, &accent, COLOR_RED);

        COLOR text_col = (g_tracker.state == TRACKER_STATE_HOVER) ? COLOR_NAVY : COLOR_BLACK;
        drw_tc_string(dev, r->left + 10, r->top + 3, "［BTRON］", text_col, 0x00000000);
    }
}

void tracker_render_menu(GDEV *dev) {
    if (!dev || g_tracker.state != TRACKER_STATE_OPEN) return;

    RECT *mr = &g_tracker.menu_rect;

    APP_MENU_STYLE style = app_menu_get_global_style();
    if (style == APP_MENU_STYLE_CLASSIC_3D) {
        /* Style 1: Classic Chokanji 3D Beveled Box Plate */
        app_menu_draw_3d_bevel_box(dev, mr);
    } else {
        /* Style 2: Modern Flat Card with Soft Drop Shadow */
        RECT shadow = { mr->left + 3, mr->top + 3, mr->right + 3, mr->bottom + 3 };
        fill_rec(dev, &shadow, COLOR_DKGRAY);

        fill_rec(dev, mr, COLOR_WHITE);
        drw_rec(dev, mr);
        drw_lin(dev, mr->left + 1, mr->top + 1, mr->right - 2, mr->top + 1);
        drw_lin(dev, mr->left + 1, mr->top + 1, mr->left + 1, mr->bottom - 2);
    }

    /* Left accent vertical stripe (Haiku Deskbar leaf blue bar) */
    RECT bar = { mr->left + 1, mr->top + 1, mr->left + 4, mr->bottom - 1 };
    fill_rec(dev, &bar, COLOR_NAVY);

    /* Render Menu Items */
    H y = mr->top + 3;
    for (H i = 0; i < g_tracker.item_count; i++) {
        TRACKER_ITEM *it = &g_tracker.items[i];
        RECT row_r = { mr->left + 5, y, mr->right - 2, y + TRACKER_ITEM_HEIGHT };

        if (it->type == TRACKER_CMD_SEPARATOR) {
            /* Separator etched line */
            H sep_y = y + (TRACKER_ITEM_HEIGHT / 2);
            drw_lin(dev, mr->left + 8, sep_y, mr->right - 8, sep_y);
        } else {
            if (i == g_tracker.hover_index) {
                /* Hover Highlight (BTRON / Haiku Blue) */
                fill_rec(dev, &row_r, COLOR_NAVY);
                drw_tc_string(dev, mr->left + 10, y + 2, it->label, COLOR_WHITE, COLOR_NAVY);
            } else {
                COLOR col = (it->type == TRACKER_CMD_WND_FOCUS) ? COLOR_NAVY : COLOR_BLACK;
                drw_tc_string(dev, mr->left + 10, y + 2, it->label, col, 0x00000000);
            }
        }
        y += TRACKER_ITEM_HEIGHT;
    }
}
