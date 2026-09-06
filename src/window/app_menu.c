/*
 * B-TRON Common Application Menu Subsystem
 * Unified in-window menu bar, BeOS fluid hover tracking, and multi-style rendering.
 */

#include <btron/app_menu.h>
#include <btron/troncode.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
extern void* Icalloc(size_t nmemb, size_t sz);
extern void  Ifree(void *ptr);
#define calloc   Icalloc
#define free     Ifree
#define memset   tkl_memset
#define memcpy   tkl_memcpy
#define strlen   tkl_strlen
#define strcmp   tkl_strcmp
#define strncpy  tkl_strncpy
#define strstr   tkl_strstr
#define snprintf tkl_snprintf
#endif

static APP_MENU_STYLE s_global_menu_style = APP_MENU_STYLE_CLASSIC_3D;

void app_menu_set_global_style(APP_MENU_STYLE style) {
    s_global_menu_style = style;
}

APP_MENU_STYLE app_menu_get_global_style(void) {
    return s_global_menu_style;
}

/* ── 3D Beveled Box (Classic Workstation Plate) ─────────────────────────── */
void app_menu_draw_3d_bevel_box(GDEV *dev, const RECT *r) {
    if (!dev || !r) return;
    fill_rec(dev, r, COLOR_LTGRAY);
    drw_rec(dev, r);
    /* 3D highlight: white top and left */
    drw_lin(dev, r->left + 1, r->top + 1, r->right - 2, r->top + 1);
    drw_lin(dev, r->left + 1, r->top + 1, r->left + 1, r->bottom - 2);
    /* 3D shadow: dark gray bottom and right */
    drw_lin(dev, r->left + 1, r->bottom - 2, r->right - 2, r->bottom - 2);
    drw_lin(dev, r->right - 2, r->top + 1, r->right - 2, r->bottom - 2);
}
#define draw_3d_bevel_box app_menu_draw_3d_bevel_box

/* ── 3D Dual-Line Etched Groove (Distance Separator) ────────────────────── */
static void draw_menu_separator_v(GDEV *dev, H x, H y1, H y2) {
    RECT shadow = { x, y1, x + 1, y2 };
    RECT highlight = { x + 1, y1, x + 2, y2 };
    fill_rec(dev, &shadow, COLOR_DKGRAY);
    fill_rec(dev, &highlight, COLOR_WHITE);
}

void app_menu_init(APP_MENU_BAR *bar, APP_MENU_STYLE style) {
    if (!bar) return;
    memset(bar, 0, sizeof(APP_MENU_BAR));
    bar->active_menu = -1;
    bar->hover_menu = -1;
    bar->hover_item = -1;
    bar->active_submenu = -1;
    bar->hover_subitem = -1;
    bar->style = style;
    bar->use_global_style = TRUE;
}

int app_menu_add_header(APP_MENU_BAR *bar, const char *title, H width) {
    if (!bar || bar->header_count >= APP_MENU_MAX_HEADERS) return -1;
    int idx = bar->header_count++;
    APP_MENU_HEADER *hdr = &bar->headers[idx];
    memset(hdr, 0, sizeof(APP_MENU_HEADER));
    strncpy(hdr->title, title ? title : "", sizeof(hdr->title) - 1);

    H left = 4;
    if (idx > 0) {
        left = bar->headers[idx - 1].rect.right + 6;
    }
    hdr->rect.left = left;
    hdr->rect.top = 0;
    hdr->rect.right = left + width;
    hdr->rect.bottom = APP_MENU_BAR_HEIGHT;
    return idx;
}

int app_menu_add_item(APP_MENU_BAR *bar, int header_idx, const char *label, const char *accel, int cmd_id, BOOL enabled) {
    if (!bar || header_idx < 0 || header_idx >= bar->header_count) return -1;
    APP_MENU_HEADER *hdr = &bar->headers[header_idx];
    if (hdr->item_count >= APP_MENU_MAX_ITEMS) return -1;

    int idx = hdr->item_count++;
    APP_MENU_ITEM *it = &hdr->items[idx];
    memset(it, 0, sizeof(APP_MENU_ITEM));
    strncpy(it->label, label ? label : "", sizeof(it->label) - 1);
    if (accel) strncpy(it->accel, accel, sizeof(it->accel) - 1);
    it->cmd_id = cmd_id;
    it->is_separator = FALSE;
    it->has_submenu = FALSE;
    it->submenu_id = -1;
    it->enabled = enabled;
    return idx;
}

int app_menu_add_separator(APP_MENU_BAR *bar, int header_idx) {
    if (!bar || header_idx < 0 || header_idx >= bar->header_count) return -1;
    APP_MENU_HEADER *hdr = &bar->headers[header_idx];
    if (hdr->item_count >= APP_MENU_MAX_ITEMS) return -1;

    int idx = hdr->item_count++;
    APP_MENU_ITEM *it = &hdr->items[idx];
    memset(it, 0, sizeof(APP_MENU_ITEM));
    it->is_separator = TRUE;
    it->cmd_id = 0;
    it->enabled = FALSE;
    return idx;
}

int app_menu_add_submenu_item(APP_MENU_BAR *bar, int header_idx, const char *label, int cmd_id, int submenu_id) {
    if (!bar || header_idx < 0 || header_idx >= bar->header_count) return -1;
    APP_MENU_HEADER *hdr = &bar->headers[header_idx];
    if (hdr->item_count >= APP_MENU_MAX_ITEMS) return -1;

    int idx = hdr->item_count++;
    APP_MENU_ITEM *it = &hdr->items[idx];
    memset(it, 0, sizeof(APP_MENU_ITEM));
    strncpy(it->label, label ? label : "", sizeof(it->label) - 1);
    it->cmd_id = cmd_id;
    it->has_submenu = TRUE;
    it->submenu_id = submenu_id;
    it->enabled = TRUE;
    return idx;
}

void app_menu_set_right_text(APP_MENU_BAR *bar, const char *text) {
    if (!bar) return;
    if (text) {
        strncpy(bar->right_text, text, sizeof(bar->right_text) - 1);
    } else {
        bar->right_text[0] = '\0';
    }
}

void app_menu_close(APP_MENU_BAR *bar) {
    if (!bar) return;
    bar->active_menu = -1;
    bar->hover_item = -1;
    bar->active_submenu = -1;
    bar->hover_subitem = -1;
}

void app_menu_open(APP_MENU_BAR *bar, int header_idx) {
    if (!bar || header_idx < 0 || header_idx >= bar->header_count) return;
    bar->active_menu = header_idx;
    bar->hover_menu = header_idx;
    bar->hover_item = -1;
    bar->active_submenu = -1;
    bar->hover_subitem = -1;
}

BOOL app_menu_handle_mouse_move(APP_MENU_BAR *bar, H rel_x, H rel_y) {
    if (!bar) return FALSE;

    /* When menu is open: track hot headers, dropdown items, and submenus */
    if (bar->active_menu >= 0) {
        /* 1. Hot header tracking (BeOS fluid gliding across headers) */
        if (rel_y >= 0 && rel_y <= APP_MENU_BAR_HEIGHT) {
            for (int h = 0; h < bar->header_count; h++) {
                if (rel_x >= bar->headers[h].rect.left && rel_x <= bar->headers[h].rect.right) {
                    if (bar->active_menu != h) {
                        bar->active_menu = h;
                        bar->hover_menu = h;
                        bar->hover_item = -1;
                        bar->active_submenu = -1;
                        bar->hover_subitem = -1;
                        return TRUE;
                    }
                }
            }
        }

        const APP_MENU_HEADER *hdr = &bar->headers[bar->active_menu];
        H menu_x = hdr->rect.left;
        H menu_y = APP_MENU_BAR_HEIGHT;
        H menu_w = APP_MENU_DROPDOWN_WIDTH;
        H menu_h = hdr->item_count * APP_MENU_ROW_HEIGHT + 6;

        /* 2. Cascading Submenu Hover Tracking */
        if (bar->active_submenu >= 0 && bar->active_submenu < hdr->item_count) {
            H sub_x = menu_x + menu_w - 2;
            H sub_y = menu_y + 3 + (bar->active_submenu * APP_MENU_ROW_HEIGHT);
            H sub_w = APP_MENU_SUBMENU_WIDTH;
            H sub_h = 32 * APP_MENU_ROW_HEIGHT + 6; /* bounding ceiling */

            if (rel_x >= sub_x && rel_x <= sub_x + sub_w && rel_y >= sub_y && rel_y <= sub_y + sub_h) {
                int sub_idx = (rel_y - (sub_y + 3)) / APP_MENU_ROW_HEIGHT;
                if (sub_idx >= 0) {
                    bar->hover_subitem = sub_idx;
                    return TRUE;
                }
            }
        }

        /* 3. Main Dropdown Item Tracking */
        if (rel_x >= menu_x && rel_x <= menu_x + menu_w && rel_y >= menu_y && rel_y <= menu_y + menu_h) {
            int idx = (rel_y - (menu_y + 3)) / APP_MENU_ROW_HEIGHT;
            if (idx >= 0 && idx < hdr->item_count) {
                if (!hdr->items[idx].is_separator && hdr->items[idx].enabled) {
                    bar->hover_item = idx;
                    if (hdr->items[idx].has_submenu) {
                        bar->active_submenu = idx;
                    } else {
                        bar->active_submenu = -1;
                        bar->hover_subitem = -1;
                    }
                    return TRUE;
                } else {
                    bar->hover_item = -1;
                }
            }
        }
        return FALSE;
    }

    /* When menu is closed: subtle feedback for hovered header */
    if (rel_y >= 0 && rel_y <= APP_MENU_BAR_HEIGHT) {
        int prev_hov = bar->hover_menu;
        bar->hover_menu = -1;
        for (int h = 0; h < bar->header_count; h++) {
            if (rel_x >= bar->headers[h].rect.left && rel_x <= bar->headers[h].rect.right) {
                bar->hover_menu = h;
                break;
            }
        }
        return (bar->hover_menu != prev_hov);
    } else {
        if (bar->hover_menu != -1) {
            bar->hover_menu = -1;
            return TRUE;
        }
    }
    return FALSE;
}

BOOL app_menu_handle_mouse_down(APP_MENU_BAR *bar, H rel_x, H rel_y, int *out_cmd, int *out_subitem) {
    if (!bar) return FALSE;
    if (out_cmd) *out_cmd = 0;
    if (out_subitem) *out_subitem = -1;

    /* When menu is open: handle item clicks or submenu clicks */
    if (bar->active_menu >= 0) {
        const APP_MENU_HEADER *hdr = &bar->headers[bar->active_menu];
        H menu_x = hdr->rect.left;
        H menu_y = APP_MENU_BAR_HEIGHT;
        H menu_w = APP_MENU_DROPDOWN_WIDTH;
        H menu_h = hdr->item_count * APP_MENU_ROW_HEIGHT + 6;

        /* Click inside cascading submenu */
        if (bar->active_submenu >= 0 && bar->active_submenu < hdr->item_count) {
            H sub_x = menu_x + menu_w - 2;
            H sub_y = menu_y + 3 + (bar->active_submenu * APP_MENU_ROW_HEIGHT);
            H sub_w = APP_MENU_SUBMENU_WIDTH;
            H sub_h = 32 * APP_MENU_ROW_HEIGHT + 6;

            if (rel_x >= sub_x && rel_x <= sub_x + sub_w && rel_y >= sub_y && rel_y <= sub_y + sub_h) {
                int sub_idx = (rel_y - (sub_y + 3)) / APP_MENU_ROW_HEIGHT;
                if (sub_idx >= 0) {
                    if (out_cmd) *out_cmd = hdr->items[bar->active_submenu].cmd_id;
                    if (out_subitem) *out_subitem = sub_idx;
                    app_menu_close(bar);
                    return TRUE;
                }
            }
        }

        /* Click inside active dropdown menu */
        if (rel_x >= menu_x && rel_x <= menu_x + menu_w && rel_y >= menu_y && rel_y <= menu_y + menu_h) {
            int idx = (rel_y - (menu_y + 3)) / APP_MENU_ROW_HEIGHT;
            if (idx >= 0 && idx < hdr->item_count) {
                if (hdr->items[idx].has_submenu) {
                    bar->active_submenu = idx;
                    return TRUE;
                } else if (hdr->items[idx].enabled && !hdr->items[idx].is_separator) {
                    if (out_cmd) *out_cmd = hdr->items[idx].cmd_id;
                    app_menu_close(bar);
                    return TRUE;
                }
            }
        }

        /* Click on a menu bar header toggles/switches */
        if (rel_y >= 0 && rel_y <= APP_MENU_BAR_HEIGHT) {
            for (int h = 0; h < bar->header_count; h++) {
                if (rel_x >= bar->headers[h].rect.left && rel_x <= bar->headers[h].rect.right) {
                    if (bar->active_menu == h) {
                        app_menu_close(bar);
                    } else {
                        app_menu_open(bar, h);
                    }
                    return TRUE;
                }
            }
        }

        /* Clicked outside menu -> dismiss menu */
        app_menu_close(bar);
        return TRUE;
    }

    /* Menu is closed: check if clicking on menu bar header to open */
    if (rel_y >= 0 && rel_y <= APP_MENU_BAR_HEIGHT) {
        for (int h = 0; h < bar->header_count; h++) {
            if (rel_x >= bar->headers[h].rect.left && rel_x <= bar->headers[h].rect.right) {
                app_menu_open(bar, h);
                return TRUE;
            }
        }
    }
    return FALSE;
}

static char app_menu_get_header_mnemonic(const char *title) {
    if (!title) return 0;
    const char *p = title;
    while (*p) {
        if (*p == '(' && p[1] != '\0' && p[2] == ')') {
            char c = p[1];
            if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
            return c;
        }
        p++;
    }
    return 0;
}

BOOL app_menu_handle_key(APP_MENU_BAR *bar, UW key, uint16_t mod, int *out_cmd) {
    if (!bar) return FALSE;
    if (out_cmd) *out_cmd = 0;

    BOOL is_alt = ((mod & (BTRON_KMOD_LALT | BTRON_KMOD_RALT | 0x0300)) != 0);
    BOOL is_ctrl = ((mod & (BTRON_KMOD_CTRL | 0x00C0)) != 0);

    /* 1. Alt + Letter mnemonic (e.g. Alt+F -> ファイル(F), Alt+T -> 端末(T)) */
    if (is_alt) {
        char key_char = 0;
        if (key >= 'a' && key <= 'z') {
            key_char = (char)(key - 'a' + 'A');
        } else if (key >= 'A' && key <= 'Z') {
            key_char = (char)key;
        }
        if (key_char != 0) {
            for (int h = 0; h < bar->header_count; h++) {
                if (app_menu_get_header_mnemonic(bar->headers[h].title) == key_char) {
                    if (bar->active_menu == h) {
                        app_menu_close(bar);
                    } else {
                        app_menu_open(bar, h);
                        bar->hover_item = 0;
                    }
                    return TRUE;
                }
            }
        }
    }

    /* 2. F2 / Menu key: toggle or cycle menu bar */
    if (key == BTRON_KEY_F2 || key == 0x4000003B) {
        if (bar->active_menu < 0) {
            app_menu_open(bar, 0);
            bar->hover_item = 0;
        } else {
            bar->active_menu = (bar->active_menu + 1) % bar->header_count;
            bar->hover_item = 0;
        }
        return TRUE;
    }

    /* 3. When an active dropdown menu is open */
    if (bar->active_menu >= 0 && bar->active_menu < bar->header_count) {
        APP_MENU_HEADER *hdr = &bar->headers[bar->active_menu];

        /* Escape: dismiss menu */
        if (key == BTRON_KEY_ESCAPE || key == 27) {
            app_menu_close(bar);
            return TRUE;
        }

        /* Up Arrow: select previous enabled item */
        if (key == BTRON_KEY_UP || key == 0x40000052) {
            int prev = bar->hover_item;
            for (int step = 0; step < hdr->item_count; step++) {
                prev = (prev - 1 + hdr->item_count) % hdr->item_count;
                if (!hdr->items[prev].is_separator && hdr->items[prev].enabled) {
                    bar->hover_item = prev;
                    break;
                }
            }
            return TRUE;
        }

        /* Down Arrow: select next enabled item */
        if (key == BTRON_KEY_DOWN || key == 0x40000051) {
            int next = bar->hover_item;
            for (int step = 0; step < hdr->item_count; step++) {
                next = (next + 1) % hdr->item_count;
                if (!hdr->items[next].is_separator && hdr->items[next].enabled) {
                    bar->hover_item = next;
                    break;
                }
            }
            return TRUE;
        }

        /* Left Arrow: navigate to previous menu header */
        if (key == BTRON_KEY_LEFT || key == 0x40000050) {
            bar->active_menu = (bar->active_menu - 1 + bar->header_count) % bar->header_count;
            bar->hover_item = 0;
            return TRUE;
        }

        /* Right Arrow: navigate to next menu header */
        if (key == BTRON_KEY_RIGHT || key == 0x4000004F) {
            bar->active_menu = (bar->active_menu + 1) % bar->header_count;
            bar->hover_item = 0;
            return TRUE;
        }

        /* Return / Space: execute selected item */
        if (key == BTRON_KEY_RETURN || key == 13 || key == '\n' || key == '\r' ||
            key == 0x40000058 || key == ' ') {
            if (bar->hover_item >= 0 && bar->hover_item < hdr->item_count) {
                if (hdr->items[bar->hover_item].enabled && !hdr->items[bar->hover_item].is_separator) {
                    if (out_cmd) *out_cmd = hdr->items[bar->hover_item].cmd_id;
                    app_menu_close(bar);
                    return TRUE;
                }
            }
            app_menu_close(bar);
            return TRUE;
        }

        /* 1..9 Numeric keypad direct item selection */
        if (key >= '1' && key <= '9') {
            int idx = key - '1';
            if (idx >= 0 && idx < hdr->item_count) {
                if (hdr->items[idx].enabled && !hdr->items[idx].is_separator) {
                    if (out_cmd) *out_cmd = hdr->items[idx].cmd_id;
                    app_menu_close(bar);
                    return TRUE;
                }
            }
        }

        /* Eat any other key while dropdown is open */
        return TRUE;
    }

    /* 4. Ctrl + Accelerator execution while menu is closed */
    if (is_ctrl) {
        char target_key = 0;
        if (key >= 'a' && key <= 'z') target_key = (char)(key - 'a' + 'A');
        else if (key >= 'A' && key <= 'Z') target_key = (char)key;

        if (target_key != 0) {
            char target_accel[16];
            snprintf(target_accel, sizeof(target_accel), "Ctrl+%c", target_key);
            for (int h = 0; h < bar->header_count; h++) {
                for (int i = 0; i < bar->headers[h].item_count; i++) {
                    if (bar->headers[h].items[i].enabled &&
                        strcmp(bar->headers[h].items[i].accel, target_accel) == 0) {
                        if (out_cmd) *out_cmd = bar->headers[h].items[i].cmd_id;
                        return TRUE;
                    }
                }
            }
        }
    }

    return FALSE;
}

/* ── Menu Bar Painting ─────────────────────────────────────────────────── */
void app_menu_paint_bar(const APP_MENU_BAR *bar, GDEV *dev) {
    if (!bar || !dev) return;

    RECT bar_rect = { 0, 0, dev->width, APP_MENU_BAR_HEIGHT };
    fill_rec(dev, &bar_rect, COLOR_LTGRAY);
    drw_lin(dev, 0, APP_MENU_BAR_HEIGHT, dev->width, APP_MENU_BAR_HEIGHT);

    for (int h = 0; h < bar->header_count; h++) {
        const APP_MENU_HEADER *hdr = &bar->headers[h];
        RECT hr = hdr->rect;
        if (bar->active_menu == h) {
            fill_rec(dev, &hr, COLOR_NAVY);
            drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_WHITE, 0x00000000);
        } else if (bar->hover_menu == h) {
            fill_rec(dev, &hr, COLOR_WHITE);
            drw_rec(dev, &hr);
            drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_NAVY, 0x00000000);
        } else {
            drw_tc_string(dev, hr.left + 8, hr.top + 3, hdr->title, COLOR_BLACK, 0x00000000);
        }

        /* 3D Etched Distance Groove between headers */
        if (h < bar->header_count - 1) {
            H sep_x = (hdr->rect.right + bar->headers[h + 1].rect.left) / 2;
            draw_menu_separator_v(dev, sep_x, 3, APP_MENU_BAR_HEIGHT - 2);
        }
    }

    if (bar->header_count > 0) {
        draw_menu_separator_v(dev, bar->headers[bar->header_count - 1].rect.right + 3, 3, APP_MENU_BAR_HEIGHT - 2);
    }

    /* Right Margin Status Text */
    if (bar->right_text[0] != '\0') {
        int text_w = tc_calc_string_width(bar->right_text, (int)strlen(bar->right_text));
        int text_x = dev->width - text_w - 14;
        if (bar->header_count == 0 || text_x > bar->headers[bar->header_count - 1].rect.right + 20) {
            draw_menu_separator_v(dev, text_x - 8, 3, APP_MENU_BAR_HEIGHT - 2);
            drw_tc_string(dev, text_x, 3, bar->right_text, COLOR_NAVY, COLOR_LTGRAY);
        }
    }
}

/* ── Dropdown Overlay Painting (Supports Classic 3D & Modern Card) ─────── */
void app_menu_paint_dropdown(const APP_MENU_BAR *bar, GDEV *dev) {
    if (!bar || !dev || bar->active_menu < 0 || bar->active_menu >= bar->header_count) return;

    APP_MENU_STYLE active_style = bar->use_global_style ? s_global_menu_style : bar->style;

    const APP_MENU_HEADER *hdr = &bar->headers[bar->active_menu];
    H menu_x = hdr->rect.left;
    H menu_y = APP_MENU_BAR_HEIGHT;
    H menu_w = APP_MENU_DROPDOWN_WIDTH;
    H menu_h = hdr->item_count * APP_MENU_ROW_HEIGHT + 6;

    RECT menu_box = { menu_x, menu_y, menu_x + menu_w, menu_y + menu_h };

    if (active_style == APP_MENU_STYLE_CLASSIC_3D) {
        /* Style 1: Editor Authentic 3D Beveled Box Plate */
        draw_3d_bevel_box(dev, &menu_box);
    } else {
        /* Style 2: Modern Flat Card with Soft Drop Shadow */
        RECT shadow = { menu_x + 3, menu_y + 3, menu_x + menu_w + 3, menu_y + menu_h + 3 };
        fill_rec(dev, &shadow, COLOR_DKGRAY);

        fill_rec(dev, &menu_box, COLOR_WHITE);
        drw_rec(dev, &menu_box);
        drw_lin(dev, menu_box.left + 1, menu_box.top + 1, menu_box.right - 2, menu_box.top + 1);
        drw_lin(dev, menu_box.left + 1, menu_box.top + 1, menu_box.left + 1, menu_box.bottom - 2);
    }

    for (int i = 0; i < hdr->item_count; i++) {
        const APP_MENU_ITEM *it = &hdr->items[i];
        RECT ir = { menu_x + 3, menu_y + 3 + i * APP_MENU_ROW_HEIGHT,
                    menu_x + menu_w - 3, menu_y + 3 + (i + 1) * APP_MENU_ROW_HEIGHT };

        if (it->is_separator) {
            H sep_y = (ir.top + ir.bottom) / 2;
            drw_lin(dev, ir.left + 4, sep_y, ir.right - 4, sep_y);
            continue;
        }

        BOOL is_hov = (bar->hover_item == i && it->enabled);
        if (is_hov) {
            fill_rec(dev, &ir, COLOR_NAVY);
        }

        COLOR txt_col = is_hov ? COLOR_WHITE : (it->enabled ? COLOR_BLACK : COLOR_GRAY);
        COLOR acc_col = is_hov ? COLOR_LTGRAY : (it->enabled ? COLOR_DKGRAY : COLOR_GRAY);

        drw_tc_string(dev, ir.left + 8, ir.top + 3, it->label, txt_col, 0x00000000);

        if (it->accel[0] != '\0') {
            int acc_w = tc_calc_string_width(it->accel, (int)strlen(it->accel));
            drw_tc_string(dev, ir.right - acc_w - 10, ir.top + 3, it->accel, acc_col, 0x00000000);
        }

        if (it->has_submenu) {
            drw_tc_string(dev, ir.right - 18, ir.top + 3, "▶", txt_col, 0x00000000);
        }
    }
}

/* ── Cascading Submenu Painting for Strings ────────────────────────────── */
void app_menu_paint_cascading_strings(const APP_MENU_BAR *bar, GDEV *dev, const char items[][64], int count) {
    if (!bar || !dev || bar->active_menu < 0 || bar->active_submenu < 0 || count <= 0) return;

    APP_MENU_STYLE active_style = bar->use_global_style ? s_global_menu_style : bar->style;

    const APP_MENU_HEADER *hdr = &bar->headers[bar->active_menu];
    H menu_x = hdr->rect.left;
    H menu_y = APP_MENU_BAR_HEIGHT;
    H menu_w = APP_MENU_DROPDOWN_WIDTH;

    H sub_x = menu_x + menu_w - 2;
    H sub_y = menu_y + 3 + (bar->active_submenu * APP_MENU_ROW_HEIGHT);
    H sub_w = APP_MENU_SUBMENU_WIDTH;
    H sub_h = count * APP_MENU_ROW_HEIGHT + 6;

    RECT sub_box = { sub_x, sub_y, sub_x + sub_w, sub_y + sub_h };

    if (active_style == APP_MENU_STYLE_CLASSIC_3D) {
        draw_3d_bevel_box(dev, &sub_box);
    } else {
        RECT shadow = { sub_x + 3, sub_y + 3, sub_x + sub_w + 3, sub_y + sub_h + 3 };
        fill_rec(dev, &shadow, COLOR_DKGRAY);

        fill_rec(dev, &sub_box, COLOR_WHITE);
        drw_rec(dev, &sub_box);
        drw_lin(dev, sub_box.left + 1, sub_box.top + 1, sub_box.right - 2, sub_box.top + 1);
        drw_lin(dev, sub_box.left + 1, sub_box.top + 1, sub_box.left + 1, sub_box.bottom - 2);
    }

    for (int f = 0; f < count; f++) {
        RECT sir = { sub_x + 3, sub_y + 3 + f * APP_MENU_ROW_HEIGHT,
                     sub_x + sub_w - 3, sub_y + 3 + (f + 1) * APP_MENU_ROW_HEIGHT };
        BOOL is_sub_hov = (bar->hover_subitem == f);
        if (is_sub_hov) {
            fill_rec(dev, &sir, COLOR_NAVY);
        }
        COLOR sub_txt_col = is_sub_hov ? COLOR_WHITE : COLOR_BLACK;
        drw_tc_string(dev, sir.left + 8, sir.top + 3, items[f], sub_txt_col, 0x00000000);
    }
}

/* ── Common About Box Dialog Implementation with 32x32 Raster Icon ─────── */
typedef struct {
    char title_full[128];
    char app_name[64];
    char desc[128];
    char attribution[64];
} AboutDialogData;

static int decode_and_draw_about_gif(GDEV *dev, const char *filepath, int dst_x, int dst_y) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#define ABOUT_ICON_LZW_DICT   4096
#define ABOUT_ICON_MAX_PIXELS (64 * 64)

static uint16_t s_about_gif_prefix[ABOUT_ICON_LZW_DICT];
static uint8_t  s_about_gif_suffix[ABOUT_ICON_LZW_DICT];
static uint8_t  s_about_gif_stack[ABOUT_ICON_LZW_DICT + 1];
static uint8_t  s_about_gif_raw[ABOUT_ICON_MAX_PIXELS];

    if (!dev || !dev->pixels || !filepath) return -1;
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return -1;

    uint8_t hdr[13];
    if (fread(hdr, 1, 13, fp) != 13) { fclose(fp); return -1; }
    if (memcmp(hdr, "GIF87a", 6) != 0 && memcmp(hdr, "GIF89a", 6) != 0) { fclose(fp); return -1; }

    uint8_t flags = hdr[10];
    int has_gct = (flags & 0x80) != 0;
    int gct_size = 1 << ((flags & 0x07) + 1);
    uint32_t gct[256];
    memset(gct, 0, sizeof(gct));

    if (has_gct) {
        uint8_t gct_raw[768];
        if (fread(gct_raw, 1, gct_size * 3, fp) != (size_t)(gct_size * 3)) { fclose(fp); return -1; }
        for (int i = 0; i < gct_size; i++) {
            gct[i] = (gct_raw[i*3] << 16) | (gct_raw[i*3+1] << 8) | gct_raw[i*3+2];
        }
    }

    int trans_idx = -1;
    int img_w = 0, img_h = 0;
    int img_read = 0;

    while (!feof(fp)) {
        int b = fgetc(fp);
        if (b == EOF || b == 0x3B) break;
        if (b == 0x21) {
            int ext_label = fgetc(fp);
            if (ext_label == 0xF9) {
                int block_size = fgetc(fp);
                if (block_size == 4) {
                    uint8_t gce[4];
                    if (fread(gce, 1, 4, fp) == 4 && (gce[0] & 0x01)) trans_idx = gce[3];
                }
                while (1) { int l = fgetc(fp); if (l <= 0) break; fseek(fp, l, SEEK_CUR); }
            } else {
                while (1) { int l = fgetc(fp); if (l <= 0) break; fseek(fp, l, SEEK_CUR); }
            }
        } else if (b == 0x2C) {
            uint8_t idesc[9];
            if (fread(idesc, 1, 9, fp) != 9) break;
            img_w = idesc[4] | (idesc[5] << 8);
            img_h = idesc[6] | (idesc[7] << 8);
            uint8_t iflags = idesc[8];
            int has_lct = (iflags & 0x80) != 0;
            int lct_size = 1 << ((iflags & 0x07) + 1);
            uint32_t lct[256];
            memset(lct, 0, sizeof(lct));
            uint32_t *palette = gct;
            if (has_lct) {
                uint8_t lct_raw[768];
                if (fread(lct_raw, 1, lct_size * 3, fp) != (size_t)(lct_size * 3)) break;
                for (int i = 0; i < lct_size; i++) {
                    lct[i] = (lct_raw[i*3] << 16) | (lct_raw[i*3+1] << 8) | lct_raw[i*3+2];
                }
                palette = lct;
            }

            int min_code_size = fgetc(fp);
            if (min_code_size < 2 || min_code_size > 8) break;
            int clear_code = 1 << min_code_size;
            int eoi_code = clear_code + 1;
            int code_size = min_code_size + 1;
            int code_mask = (1 << code_size) - 1;
            int next_code = eoi_code + 1;

            int bit_count = 0;
            uint32_t bit_buf = 0;
            int pixel_count = 0;
            int total_pixels = img_w * img_h;
            if (total_pixels > ABOUT_ICON_MAX_PIXELS) total_pixels = ABOUT_ICON_MAX_PIXELS;

            int old_code = -1, first_char = 0, stack_top = 0;
            for (int i = 0; i < clear_code; i++) {
                s_about_gif_prefix[i] = 0;
                s_about_gif_suffix[i] = (uint8_t)i;
            }

            uint8_t sub_buf[256];
            int sub_len = 0, sub_pos = 0;

            while (pixel_count < total_pixels) {
                while (bit_count < code_size) {
                    if (sub_pos >= sub_len) {
                        sub_len = fgetc(fp);
                        if (sub_len <= 0) break;
                        if (fread(sub_buf, 1, sub_len, fp) != (size_t)sub_len) break;
                        sub_pos = 0;
                    }
                    bit_buf |= ((uint32_t)sub_buf[sub_pos++] << bit_count);
                    bit_count += 8;
                }
                if (bit_count < code_size) break;
                int code = bit_buf & code_mask;
                bit_buf >>= code_size;
                bit_count -= code_size;

                if (code == clear_code) {
                    code_size = min_code_size + 1;
                    code_mask = (1 << code_size) - 1;
                    next_code = eoi_code + 1;
                    old_code = -1;
                    continue;
                }
                if (code == eoi_code) break;

                int cur_code = code;
                if (cur_code >= next_code) {
                    s_about_gif_stack[stack_top++] = (uint8_t)first_char;
                    cur_code = old_code;
                }
                while (cur_code >= clear_code && cur_code < ABOUT_ICON_LZW_DICT) {
                    s_about_gif_stack[stack_top++] = s_about_gif_suffix[cur_code];
                    cur_code = s_about_gif_prefix[cur_code];
                }
                first_char = s_about_gif_suffix[cur_code];
                s_about_gif_stack[stack_top++] = (uint8_t)first_char;

                while (stack_top > 0 && pixel_count < total_pixels) {
                    s_about_gif_raw[pixel_count++] = s_about_gif_stack[--stack_top];
                }

                if (old_code >= 0 && next_code < ABOUT_ICON_LZW_DICT) {
                    s_about_gif_prefix[next_code] = old_code;
                    s_about_gif_suffix[next_code] = (uint8_t)first_char;
                    next_code++;
                    if (next_code > code_mask && code_size < 12) {
                        code_size++;
                        code_mask = (1 << code_size) - 1;
                    }
                }
                old_code = code;
            }

            if (pixel_count > 0) {
                for (int y = 0; y < img_h && y < 32; y++) {
                    int out_y = dst_y + y;
                    if (out_y < dev->clip.top || out_y >= dev->clip.bottom) continue;
                    if (out_y < 0 || out_y >= dev->height) continue;
                    for (int x = 0; x < img_w && x < 32; x++) {
                        int out_x = dst_x + x;
                        if (out_x < dev->clip.left || out_x >= dev->clip.right) continue;
                        if (out_x < 0 || out_x >= dev->width) continue;
                        uint8_t p_idx = s_about_gif_raw[y * img_w + x];
                        if (p_idx == trans_idx) continue;
                        uint32_t col = palette[p_idx];
                        dev->pixels[out_y * dev->width + out_x] = (COLOR)(0xFF000000 | col);
                    }
                }
                img_read = 1;
            }
            break;
        }
    }
    fclose(fp);
    return img_read ? 0 : -1;
#else
    (void)dev; (void)filepath; (void)dst_x; (void)dst_y;
    return -1;
#endif
}

static void draw_about_app_icon(GDEV *dev, const char *app_name, int dst_x, int dst_y) {
    if (!dev || !dev->pixels) return;

    char name_lower[32];
    memset(name_lower, 0, sizeof(name_lower));
    int nlen = 0;

    if (app_name) {
        if (strstr(app_name, "Cabinet") || strstr(app_name, "キャビネット")) {
            strncpy(name_lower, "cabinet", sizeof(name_lower) - 1);
        } else if (strstr(app_name, "Editor") || strstr(app_name, "文書編集")) {
            strncpy(name_lower, "teditor", sizeof(name_lower) - 1);
        } else if (strstr(app_name, "Browser") || strstr(app_name, "TAD") || strstr(app_name, "実身閲覧")) {
            strncpy(name_lower, "browser", sizeof(name_lower) - 1);
        } else if (strstr(app_name, "gterm") || strstr(app_name, "terminal") ||
                   strstr(app_name, "Terminal") || strstr(app_name, "端末")) {
            strncpy(name_lower, "terminal", sizeof(name_lower) - 1);
        } else if (strstr(app_name, "Cassette") || strstr(app_name, "カセット") ||
                   strstr(app_name, "cassette")) {
            strncpy(name_lower, "cassette", sizeof(name_lower) - 1);
        } else if (strstr(app_name, "Orchestra") || strstr(app_name, "管弦楽") ||
                   strstr(app_name, "Music") || strstr(app_name, "music")) {
            strncpy(name_lower, "music", sizeof(name_lower) - 1);
        } else {
            for (int i = 0; app_name[i] && nlen < 30; i++) {
                char c = app_name[i];
                if (c == ' ' || c == '(') break;
                if (c == '-' || c == '_') continue;
                if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
                    name_lower[nlen++] = c;
                }
            }
        }
    }

    const char *prefixes[] = {
        "assets/icons/",
        "assets/",
        "assets/apps/",
        "../assets/icons/",
        "../assets/",
        NULL
    };

    int loaded = -1;
    if (name_lower[0]) {
        for (int p = 0; prefixes[p]; p++) {
            char path[128];
            snprintf(path, sizeof(path), "%s%s.gif", prefixes[p], name_lower);
            if (decode_and_draw_about_gif(dev, path, dst_x, dst_y) == 0) {
                loaded = 0;
                break;
            }
        }
    }

    if (loaded != 0) {
        /* Fallback decorative 32x32 retro beveled icon badge */
        RECT badge = { dst_x, dst_y, dst_x + 32, dst_y + 32 };
        fill_rec(dev, &badge, COLOR_LTGRAY);
        drw_rec(dev, &badge);
        drw_lin(dev, dst_x + 1, dst_y + 1, dst_x + 30, dst_y + 1);
        drw_lin(dev, dst_x + 1, dst_y + 1, dst_x + 1, dst_y + 30);
        char initial[2] = { (char)((app_name && app_name[0]) ? app_name[0] : 'B'), 0 };
        if (initial[0] >= 'a' && initial[0] <= 'z') initial[0] -= ('a' - 'A');
        drw_tc_string(dev, dst_x + 11, dst_y + 8, initial, COLOR_NAVY, 0x00000000);
    }
}

static void paint_about_dialog(WND *wnd, GDEV *dev) {
    if (!wnd || !dev) return;
    AboutDialogData *data = (AboutDialogData*)(uintptr_t)wnd->user_data;

    /* Window background */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_LTGRAY);
    drw_rec(dev, &r);

    /* Inner white card */
    RECT card = { 8, 8, dev->width - 8, dev->height - 38 };
    fill_rec(dev, &card, COLOR_WHITE);
    drw_rec(dev, &card);
    /* Subtle 3D bevel on card */
    drw_lin(dev, card.left + 1, card.top + 1, card.right - 2, card.top + 1);
    drw_lin(dev, card.left + 1, card.top + 1, card.left + 1, card.bottom - 2);

    if (data) {
        /* 32x32 icon, vertically centred in the card */
        H icon_x = card.left + 14;
        H icon_y = card.top + (card.bottom - card.top - 32) / 2;
        draw_about_app_icon(dev, data->app_name, icon_x, icon_y);

        /* Vertical separator after icon */
        H sep_x = icon_x + 32 + 12;
        drw_lin(dev, sep_x, card.top + 8, sep_x, card.bottom - 8);

        /* Aligned text block */
        H text_x = sep_x + 12;
        H text_y = card.top + 14;
        drw_tc_string(dev, text_x, text_y,      data->app_name,   COLOR_NAVY,  0x00000000);
        drw_tc_string(dev, text_x, text_y + 22, data->desc,        COLOR_DKGRAY,0x00000000);

        /* Horizontal separator */
        H hsep_y = text_y + 46;
        drw_lin(dev, sep_x + 6, hsep_y, card.right - 10, hsep_y);

        drw_tc_string(dev, text_x, hsep_y + 8,  data->attribution, COLOR_BLACK, 0x00000000);
        drw_tc_string(dev, text_x, hsep_y + 26, "B-System (BTRON3 3.20) TRON ITRON BTRON HMI NET-TRON",
                      COLOR_DKGRAY, 0x00000000);
    }

    /* 3D OK Button */
    H btn_w = 80, btn_h = 24;
    H btn_x = (dev->width - btn_w) / 2;
    H btn_y = dev->height - 32;
    RECT ok_btn = { btn_x, btn_y, btn_x + btn_w, btn_y + btn_h };
    fill_rec(dev, &ok_btn, COLOR_LTGRAY);
    drw_rec(dev, &ok_btn);
    drw_lin(dev, ok_btn.left + 1, ok_btn.top + 1, ok_btn.right - 2, ok_btn.top + 1);
    drw_lin(dev, ok_btn.left + 1, ok_btn.top + 1, ok_btn.left + 1, ok_btn.bottom - 2);
    drw_tc_string(dev, btn_x + (btn_w - 16) / 2, btn_y + 4, "OK", COLOR_BLACK, 0x00000000);
}

static void handle_about_dialog_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = evt->pos.x - (wnd->bounds.left + 4);
        H rel_y = evt->pos.y - (wnd->bounds.top + 26);
        H btn_w = 80, btn_h = 24;
        H dev_w = wnd->dev ? wnd->dev->width  : (wnd->bounds.right - wnd->bounds.left - 8);
        H dev_h = wnd->dev ? wnd->dev->height : (wnd->bounds.bottom - wnd->bounds.top - 30);
        H btn_x = (dev_w - btn_w) / 2;
        H btn_y = dev_h - 32;
        if (rel_x >= btn_x && rel_x <= btn_x + btn_w && rel_y >= btn_y && rel_y <= btn_y + btn_h) {
            cls_wnd(wnd);
        }
    } else if (evt->type == EV_KEY_DOWN) {
        if (evt->key == BTRON_KEY_ESCAPE || evt->key == 0x0D || evt->key == ' ' || evt->key == 27) {
            cls_wnd(wnd);
        }
    }
}

static void destroy_about_dialog(WND *wnd) {
    if (wnd && wnd->user_data) {
        free((void*)(uintptr_t)wnd->user_data);
        wnd->user_data = (VW)0;
    }
}

static AppMenuAboutHookFn s_about_dialog_hook = NULL;

void app_menu_set_about_hook(AppMenuAboutHookFn hook) {
    s_about_dialog_hook = hook;
}

WND* app_menu_create_about_dialog(const char *app_name, const char *jp_title,
                                  const char *desc, const char *attribution,
                                  int x, int y) {
    if (s_about_dialog_hook) {
        return s_about_dialog_hook(app_name, jp_title, desc, attribution, x, y);
    }

    AboutDialogData *data = (AboutDialogData*)calloc(1, sizeof(AboutDialogData));
    if (!data) return NULL;

    snprintf(data->title_full, sizeof(data->title_full), "%s (%s)", jp_title ? jp_title : "バージョン情報", app_name);
    snprintf(data->app_name, sizeof(data->app_name), "%s 3.20 (%s)", jp_title ? jp_title : "バージョン情報", app_name);
    snprintf(data->desc, sizeof(data->desc), "%s", desc ? desc : "BTRON Application");
    snprintf(data->attribution, sizeof(data->attribution), "%s", attribution ? attribution : "Brought to B-System by 5HT");

    WND *wnd = opn_wnd(data->title_full, x, y, 520, 270,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (wnd) {
        wnd->user_data = (VW)(uintptr_t)data;
        wnd->paint = paint_about_dialog;
        wnd->event_handler = handle_about_dialog_event;
        wnd->destroy = destroy_about_dialog;
    } else {
        free(data);
    }
    return wnd;
}
