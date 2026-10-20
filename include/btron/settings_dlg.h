/*
 * B-System (BTRON 3.20) Declarative Dialog & Control Subsystem: settings_dlg.h
 * Unified, declarative controls for Settings Cabinet and B-System applications.
 * Replaces repetitive manual pixel hit-testing with declarative control tables.
 */

#ifndef _BTRON_SETTINGS_DLG_H_
#define _BTRON_SETTINGS_DLG_H_

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/settings_icon.h>
#include <btron/troncode.h>
#include <string.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Control Types ─────────────────────────────────────────────── */
typedef enum {
    SETTINGS_CTL_CHECKBOX = 0,
    SETTINGS_CTL_RADIO,
    SETTINGS_CTL_BUTTON,
    SETTINGS_CTL_CUSTOM
} SETTINGS_CTL_TYPE;

struct SETTINGS_CONTROL;

typedef void (*SETTINGS_CTL_CALLBACK)(WND *wnd, struct SETTINGS_CONTROL *ctl);

/* ── Declarative Control Descriptor ────────────────────────────── */
typedef struct SETTINGS_CONTROL {
    int                    id;
    SETTINGS_CTL_TYPE      type;
    int                    radio_group; /* 0 if not grouped; matching group IDs are mutually exclusive */
    int                    radio_val;   /* Associated integer value */
    RECT                   rect;        /* Relative client-area bounding box { left, top, right, bottom } */
    const char            *label;
    BOOL                  *bound_bool;  /* Direct pointer to boolean state */
    int                   *bound_int;   /* Direct pointer to integer state (for radios) */
    BOOL                  *dirty_flag;  /* Pointer to is_dirty flag (automatically marked TRUE on change) */
    SETTINGS_CTL_CALLBACK  on_change;   /* Callback invoked when state changes or button is clicked */
} SETTINGS_CONTROL;

/* ── Canonical UI Drawing Primitives ───────────────────────────── */

/* 3D Crisp Graphical Checkbox */
static inline void settings_draw_ui_checkbox(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    if (!dev) return;
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    if (checked) {
        drw_lin(dev, x + 3, y + 7, x + 6, y + 11);
        drw_lin(dev, x + 3, y + 8, x + 6, y + 12);
        drw_lin(dev, x + 3, y + 9, x + 6, y + 13);
        drw_lin(dev, x + 4, y + 8, x + 7, y + 12);

        drw_lin(dev, x + 6, y + 12, x + 12, y + 4);
        drw_lin(dev, x + 6, y + 13, x + 12, y + 5);
        drw_lin(dev, x + 7, y + 12, x + 13, y + 4);
    }

    if (focused && label) {
        RECT f_r = { x - 2, y - 1, x + 24 + (H)strlen(label) * 8, y + 18 };
        drw_rec(dev, &f_r);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    if (label) {
        drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
    }
}

/* 3D Crisp Graphical Radio Button */
static inline void settings_draw_ui_radio(GDEV *dev, H x, H y, const char *label, BOOL checked, BOOL focused) {
    if (!dev) return;
    RECT box = { x, y + 1, x + 15, y + 16 };
    fill_rec(dev, &box, COLOR_WHITE);
    drw_rec(dev, &box);

    if (checked) {
        RECT dot = { x + 4, y + 5, x + 11, y + 12 };
        fill_rec(dev, &dot, COLOR_NAVY);
    }

    if (focused && label) {
        RECT f_r = { x - 2, y - 1, x + 24 + (H)strlen(label) * 8, y + 18 };
        drw_rec(dev, &f_r);
    }

    COLOR text_col = focused ? COLOR_NAVY : COLOR_BLACK;
    if (label) {
        drw_tc_string(dev, x + 22, y, label, text_col, COLOR_WHITE);
    }
}

/* 3D Push Button */
static inline void settings_draw_ui_button(GDEV *dev, H x, H y, H w, H h, const char *label, BOOL pressed) {
    if (!dev || !label) return;
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

/* Common Header Bar (Always with 32x32 GIF Icon) */
static inline void settings_draw_header(GDEV *dev, const char *icon_name,
                                        const char *title, const char *title_ja,
                                        const char *subtitle) {
    if (!dev) return;
    RECT hdr = { 0, 0, dev->width, 40 };
    fill_rec(dev, &hdr, COLOR_LTGRAY);
    drw_lin(dev, 0, 40, dev->width, 40);
    if (icon_name) {
        draw_setting_gif_icon_scaled(dev, icon_name, 6, 4, 32, 32);
    }
    char hdr_str[160];
    snprintf(hdr_str, sizeof(hdr_str), "[Settings Cabinet] %s (%s) - %s",
             title ? title : "", title_ja ? title_ja : "", subtitle ? subtitle : "");
    drw_tc_string(dev, 46, 12, hdr_str, COLOR_BLACK, COLOR_LTGRAY);
}

/* Common Group Section Box */
static inline void settings_draw_section(GDEV *dev, const RECT *r, const char *title) {
    if (!dev || !r) return;
    fill_rec(dev, r, COLOR_WHITE);
    drw_rec(dev, r);
    if (title) {
        drw_tc_string(dev, r->left + 6, r->top - 8, title, COLOR_NAVY, COLOR_WHITE);
    }
}

/* Common Action Buttons: Default, Apply, Close */
static inline void settings_draw_action_buttons(GDEV *dev, BOOL is_dirty) {
    if (!dev) return;
    H btn_y = dev->height - 35;
    settings_draw_ui_button(dev, dev->width - 240, btn_y, 70, 24, "Default", FALSE);
    settings_draw_ui_button(dev, dev->width - 160, btn_y, 70, 24, "Apply", is_dirty);
    settings_draw_ui_button(dev, dev->width - 80, btn_y, 70, 24, "Close", FALSE);
}

/* ── Declarative Control Drawing ───────────────────────────────── */
static inline void settings_draw_control(GDEV *dev, const SETTINGS_CONTROL *ctl) {
    if (!dev || !ctl) return;

    BOOL checked = FALSE;
    if (ctl->bound_bool) {
        checked = *ctl->bound_bool;
    } else if (ctl->bound_int) {
        checked = (*ctl->bound_int == ctl->radio_val);
    }

    if (ctl->type == SETTINGS_CTL_CHECKBOX) {
        settings_draw_ui_checkbox(dev, ctl->rect.left, ctl->rect.top, ctl->label, checked, FALSE);
    } else if (ctl->type == SETTINGS_CTL_RADIO) {
        settings_draw_ui_radio(dev, ctl->rect.left, ctl->rect.top, ctl->label, checked, FALSE);
    } else if (ctl->type == SETTINGS_CTL_BUTTON) {
        H w = ctl->rect.right - ctl->rect.left;
        H h = ctl->rect.bottom - ctl->rect.top;
        settings_draw_ui_button(dev, ctl->rect.left, ctl->rect.top, w, h, ctl->label, FALSE);
    }
}

static inline void settings_draw_controls(GDEV *dev, const SETTINGS_CONTROL *ctls, int count) {
    if (!dev || !ctls) return;
    for (int i = 0; i < count; i++) {
        settings_draw_control(dev, &ctls[i]);
    }
}

/* ── Declarative Event Dispatching ─────────────────────────────── */
static inline BOOL settings_dispatch_control_event(WND *wnd, SETTINGS_CONTROL *ctls, int count, const EVT *evt) {
    if (!wnd || !ctls || !evt) return FALSE;
    if (evt->type != EV_BUT_DOWN) return FALSE;

    H rel_x = evt->pos.x - wnd->client.left;
    H rel_y = evt->pos.y - wnd->client.top;

    for (int i = 0; i < count; i++) {
        SETTINGS_CONTROL *c = &ctls[i];
        if (rel_x >= c->rect.left && rel_x <= c->rect.right &&
            rel_y >= c->rect.top  && rel_y <= c->rect.bottom) {

            if (c->type == SETTINGS_CTL_CHECKBOX) {
                if (c->bound_bool) {
                    *c->bound_bool = !(*c->bound_bool);
                }
                if (c->dirty_flag) *c->dirty_flag = TRUE;
                if (c->on_change) c->on_change(wnd, c);
            } else if (c->type == SETTINGS_CTL_RADIO) {
                if (c->bound_int) {
                    *c->bound_int = c->radio_val;
                }
                if (c->bound_bool) {
                    if (c->radio_group != 0) {
                        for (int j = 0; j < count; j++) {
                            if (ctls[j].type == SETTINGS_CTL_RADIO &&
                                ctls[j].radio_group == c->radio_group &&
                                ctls[j].bound_bool) {
                                *ctls[j].bound_bool = (&ctls[j] == c);
                            }
                        }
                    } else {
                        *c->bound_bool = TRUE;
                    }
                }
                if (c->dirty_flag) *c->dirty_flag = TRUE;
                if (c->on_change) c->on_change(wnd, c);
            } else if (c->type == SETTINGS_CTL_BUTTON || c->type == SETTINGS_CTL_CUSTOM) {
                if (c->on_change) c->on_change(wnd, c);
            }

            inval_wnd(wnd);
            redraw_all_windows();
            return TRUE;
        }
    }
    return FALSE;
}

/* Standard Action Button Dispatcher (Default, Apply, Close) */
static inline BOOL settings_dispatch_action_buttons(WND *wnd, const EVT *evt,
                                                    void (*on_default)(WND *wnd),
                                                    void (*on_apply)(WND *wnd)) {
    if (!wnd || !evt) return FALSE;
    if (evt->type != EV_BUT_DOWN) return FALSE;

    H rel_x = evt->pos.x - wnd->client.left;
    H rel_y = evt->pos.y - wnd->client.top;
    H client_w = wnd->client.right - wnd->client.left;
    H client_h = wnd->client.bottom - wnd->client.top;
    H btn_y = client_h - 35;

    if (rel_y >= btn_y && rel_y <= btn_y + 24) {
        if (rel_x >= client_w - 240 && rel_x <= client_w - 170) {
            /* Default Button */
            if (on_default) on_default(wnd);
            inval_wnd(wnd);
            redraw_all_windows();
            return TRUE;
        }
        if (rel_x >= client_w - 160 && rel_x <= client_w - 90) {
            /* Apply Button */
            if (on_apply) on_apply(wnd);
            inval_wnd(wnd);
            redraw_all_windows();
            return TRUE;
        }
        if (rel_x >= client_w - 80 && rel_x <= client_w - 10) {
            /* Close Button */
            cls_wnd(wnd);
            return TRUE;
        }
    }
    return FALSE;
}

/* ── Responsive Auto-Layouter ─────────────────────────────────── */

typedef struct {
    H margin_x;         /* Left/right window edge margin (default: 10) */
    H margin_top;       /* Space between 40px header and first section (default: 16 -> top = 56) */
    H section_gap;      /* Vertical spacing between sections (default: 14) */
    H padding_x;        /* Left/right padding inside section (default: 8) */
    H padding_top;      /* Padding from section top to first control (default: 14) */
    H padding_bottom;   /* Padding below last control to section bottom (default: 14) */
    H row_height;       /* Height of each control row (default: 18) */
    H row_gap;          /* Spacing between rows (default: 4) */
    H footer_h;         /* Reserved footer height for action buttons (default: 35) */
} SETTINGS_LAYOUT_METRICS;

static inline SETTINGS_LAYOUT_METRICS settings_default_metrics(void) {
    SETTINGS_LAYOUT_METRICS m = {
        .margin_x = 10,
        .margin_top = 16,
        .section_gap = 14,
        .padding_x = 8,
        .padding_top = 14,
        .padding_bottom = 14,
        .row_height = 18,
        .row_gap = 4,
        .footer_h = 35
    };
    return m;
}

typedef struct SETTINGS_SECTION {
    const char        *title;        /* Section title, e.g. " [1. Access Control Lists (ACL)] " */
    int                columns;      /* 1 for vertical list, 2..5 for multi-column grid */
    int                first_index;  /* Index in controls[] where this section starts */
    int                count;        /* Number of controls in this section */
    H                  start_y;      /* If > 0, explicit section top; if 0, auto-computed after previous section */
    H                  height;       /* If > 0, explicit section height; if 0, auto-computed from rows */
    RECT               rect;         /* Output: section bounding box calculated responsively */
    H                  padding_top;  /* If > 0, override top padding inside this section */
    H                  row_height;   /* If > 0, override row height inside this section */
    H                  row_gap;      /* If > 0, override row gap inside this section */
} SETTINGS_SECTION;

/* Auto-layout a standard single-column dialog (with multi-column sections) */
static inline void settings_layout_dialog(H width, H height,
                                          SETTINGS_SECTION *sections, int section_count,
                                          SETTINGS_CONTROL *controls, int total_controls,
                                          const SETTINGS_LAYOUT_METRICS *opt_metrics) {
    (void)height;
    if (!sections || section_count <= 0) return;
    SETTINGS_LAYOUT_METRICS m = opt_metrics ? *opt_metrics : settings_default_metrics();

    H cur_y = 40 + m.margin_top;

    for (int s = 0; s < section_count; s++) {
        SETTINGS_SECTION *sec = &sections[s];
        int cols = sec->columns > 0 ? sec->columns : 1;
        int rows = (sec->count + cols - 1) / cols;

        H p_top = sec->padding_top > 0 ? sec->padding_top : m.padding_top;
        H r_h   = sec->row_height > 0  ? sec->row_height  : m.row_height;
        H r_gap = sec->row_gap > 0     ? sec->row_gap     : m.row_gap;

        H content_h = (rows > 0) ? (rows * r_h + (rows - 1) * r_gap) : 0;
        H sec_h = sec->height > 0 ? sec->height : (p_top + content_h + m.padding_bottom);
        H top = sec->start_y > 0 ? sec->start_y : cur_y;

        sec->rect.left   = m.margin_x;
        sec->rect.top    = top;
        sec->rect.right  = width - m.margin_x;
        sec->rect.bottom = top + sec_h;

        if (controls && sec->count > 0 && sec->first_index >= 0) {
            H avail_w = (sec->rect.right - sec->rect.left) - 2 * m.padding_x;
            H col_w = (cols > 0 && avail_w > 0) ? (avail_w / cols) : avail_w;

            for (int k = 0; k < sec->count; k++) {
                int c_idx = sec->first_index + k;
                if (c_idx >= total_controls) break;
                SETTINGS_CONTROL *c = &controls[c_idx];

                int col = k % cols;
                int row = k / cols;

                c->rect.left   = sec->rect.left + m.padding_x + col * col_w;
                c->rect.right  = (col == cols - 1) ? (sec->rect.right - m.padding_x) : (c->rect.left + col_w - 4);
                c->rect.top    = sec->rect.top + p_top + row * (r_h + r_gap);
                c->rect.bottom = c->rect.top + r_h;
            }
        }

        cur_y = sec->rect.bottom + m.section_gap;
    }
}

/* Auto-layout a two-column dialog (e.g. Input Settings: Keyboard on left, Mouse on right) */
static inline void settings_layout_columns(H width, H height,
                                           SETTINGS_SECTION *left_secs, int left_count,
                                           SETTINGS_SECTION *right_secs, int right_count,
                                           SETTINGS_CONTROL *controls, int total_controls,
                                           const SETTINGS_LAYOUT_METRICS *opt_metrics) {
    (void)height;
    SETTINGS_LAYOUT_METRICS m = opt_metrics ? *opt_metrics : settings_default_metrics();
    H col_w = (width - 3 * m.margin_x) / 2;

    /* Left Column */
    H cur_y = 40 + m.margin_top;
    for (int s = 0; s < left_count; s++) {
        SETTINGS_SECTION *sec = &left_secs[s];
        int cols = sec->columns > 0 ? sec->columns : 1;
        int rows = (sec->count + cols - 1) / cols;

        H p_top = sec->padding_top > 0 ? sec->padding_top : m.padding_top;
        H r_h   = sec->row_height > 0  ? sec->row_height  : m.row_height;
        H r_gap = sec->row_gap > 0     ? sec->row_gap     : m.row_gap;

        H content_h = (rows > 0) ? (rows * r_h + (rows - 1) * r_gap) : 0;
        H sec_h = sec->height > 0 ? sec->height : (p_top + content_h + m.padding_bottom);
        H top = sec->start_y > 0 ? sec->start_y : cur_y;

        sec->rect.left   = m.margin_x;
        sec->rect.top    = top;
        sec->rect.right  = m.margin_x + col_w;
        sec->rect.bottom = top + sec_h;

        if (controls && sec->count > 0 && sec->first_index >= 0) {
            H avail_w = (sec->rect.right - sec->rect.left) - 2 * m.padding_x;
            H cell_w = (cols > 0 && avail_w > 0) ? (avail_w / cols) : avail_w;
            for (int k = 0; k < sec->count; k++) {
                int c_idx = sec->first_index + k;
                if (c_idx >= total_controls) break;
                SETTINGS_CONTROL *c = &controls[c_idx];
                int col = k % cols;
                int row = k / cols;
                c->rect.left   = sec->rect.left + m.padding_x + col * cell_w;
                c->rect.right  = (col == cols - 1) ? (sec->rect.right - m.padding_x) : (c->rect.left + cell_w - 4);
                c->rect.top    = sec->rect.top + p_top + row * (r_h + r_gap);
                c->rect.bottom = c->rect.top + r_h;
            }
        }
        cur_y = sec->rect.bottom + m.section_gap;
    }

    /* Right Column */
    cur_y = 40 + m.margin_top;
    H right_left = m.margin_x * 2 + col_w;
    for (int s = 0; s < right_count; s++) {
        SETTINGS_SECTION *sec = &right_secs[s];
        int cols = sec->columns > 0 ? sec->columns : 1;
        int rows = (sec->count + cols - 1) / cols;

        H p_top = sec->padding_top > 0 ? sec->padding_top : m.padding_top;
        H r_h   = sec->row_height > 0  ? sec->row_height  : m.row_height;
        H r_gap = sec->row_gap > 0     ? sec->row_gap     : m.row_gap;

        H content_h = (rows > 0) ? (rows * r_h + (rows - 1) * r_gap) : 0;
        H sec_h = sec->height > 0 ? sec->height : (p_top + content_h + m.padding_bottom);
        H top = sec->start_y > 0 ? sec->start_y : cur_y;

        sec->rect.left   = right_left;
        sec->rect.top    = top;
        sec->rect.right  = width - m.margin_x;
        sec->rect.bottom = top + sec_h;

        if (controls && sec->count > 0 && sec->first_index >= 0) {
            H avail_w = (sec->rect.right - sec->rect.left) - 2 * m.padding_x;
            H cell_w = (cols > 0 && avail_w > 0) ? (avail_w / cols) : avail_w;
            for (int k = 0; k < sec->count; k++) {
                int c_idx = sec->first_index + k;
                if (c_idx >= total_controls) break;
                SETTINGS_CONTROL *c = &controls[c_idx];
                int col = k % cols;
                int row = k / cols;
                c->rect.left   = sec->rect.left + m.padding_x + col * cell_w;
                c->rect.right  = (col == cols - 1) ? (sec->rect.right - m.padding_x) : (c->rect.left + cell_w - 4);
                c->rect.top    = sec->rect.top + p_top + row * (r_h + r_gap);
                c->rect.bottom = c->rect.top + r_h;
            }
        }
        cur_y = sec->rect.bottom + m.section_gap;
    }
}

/* Canonical Dialog Rendering (Single Column of Sections) */
static inline void settings_draw_dialog(GDEV *dev, const char *icon_name,
                                        const char *title, const char *title_ja,
                                        const char *subtitle,
                                        SETTINGS_SECTION *sections, int section_count,
                                        SETTINGS_CONTROL *controls, int total_controls,
                                        BOOL is_dirty) {
    if (!dev) return;

    /* Background fill & frame */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, icon_name, title, title_ja, subtitle);

    /* Responsive Auto-Layout */
    settings_layout_dialog(dev->width, dev->height, sections, section_count,
                           controls, total_controls, NULL);

    /* Draw Sections */
    for (int s = 0; s < section_count; s++) {
        settings_draw_section(dev, &sections[s].rect, sections[s].title);
    }

    /* Draw Controls */
    settings_draw_controls(dev, controls, total_controls);

    /* Action Buttons (Default, Apply, Close) */
    settings_draw_action_buttons(dev, is_dirty);
}

/* Canonical Dialog Rendering (Two Column Split) */
static inline void settings_draw_dialog_columns(GDEV *dev, const char *icon_name,
                                                const char *title, const char *title_ja,
                                                const char *subtitle,
                                                SETTINGS_SECTION *left_secs, int left_count,
                                                SETTINGS_SECTION *right_secs, int right_count,
                                                SETTINGS_CONTROL *controls, int total_controls,
                                                BOOL is_dirty) {
    if (!dev) return;

    /* Background fill & frame */
    RECT r = { 0, 0, dev->width, dev->height };
    fill_rec(dev, &r, COLOR_WHITE);
    drw_rec(dev, &r);

    /* Header Bar */
    settings_draw_header(dev, icon_name, title, title_ja, subtitle);

    /* Responsive Auto-Layout */
    settings_layout_columns(dev->width, dev->height, left_secs, left_count,
                            right_secs, right_count, controls, total_controls, NULL);

    /* Draw Sections */
    for (int s = 0; s < left_count; s++) {
        settings_draw_section(dev, &left_secs[s].rect, left_secs[s].title);
    }
    for (int s = 0; s < right_count; s++) {
        settings_draw_section(dev, &right_secs[s].rect, right_secs[s].title);
    }

    /* Draw Controls */
    settings_draw_controls(dev, controls, total_controls);

    /* Action Buttons (Default, Apply, Close) */
    settings_draw_action_buttons(dev, is_dirty);
}

/* Responsive Dialog Event Dispatcher (Single Column) */
static inline BOOL settings_dispatch_dialog_event(WND *wnd,
                                                  SETTINGS_SECTION *sections, int section_count,
                                                  SETTINGS_CONTROL *controls, int total_controls,
                                                  const EVT *evt,
                                                  void (*on_default)(WND *wnd),
                                                  void (*on_apply)(WND *wnd)) {
    if (!wnd || !evt) return FALSE;
    if (evt->type != EV_BUT_DOWN) return FALSE;

    H client_w = wnd->client.right - wnd->client.left;
    H client_h = wnd->client.bottom - wnd->client.top;

    /* Recalculate layout for current window geometry */
    settings_layout_dialog(client_w, client_h, sections, section_count,
                           controls, total_controls, NULL);

    if (settings_dispatch_control_event(wnd, controls, total_controls, evt)) {
        return TRUE;
    }
    return settings_dispatch_action_buttons(wnd, evt, on_default, on_apply);
}

/* Responsive Dialog Event Dispatcher (Two Column) */
static inline BOOL settings_dispatch_dialog_columns_event(WND *wnd,
                                                          SETTINGS_SECTION *left_secs, int left_count,
                                                          SETTINGS_SECTION *right_secs, int right_count,
                                                          SETTINGS_CONTROL *controls, int total_controls,
                                                          const EVT *evt,
                                                          void (*on_default)(WND *wnd),
                                                          void (*on_apply)(WND *wnd)) {
    if (!wnd || !evt) return FALSE;
    if (evt->type != EV_BUT_DOWN) return FALSE;

    H client_w = wnd->client.right - wnd->client.left;
    H client_h = wnd->client.bottom - wnd->client.top;

    settings_layout_columns(client_w, client_h, left_secs, left_count,
                            right_secs, right_count, controls, total_controls, NULL);

    if (settings_dispatch_control_event(wnd, controls, total_controls, evt)) {
        return TRUE;
    }
    return settings_dispatch_action_buttons(wnd, evt, on_default, on_apply);
}

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SETTINGS_DLG_H_ */
