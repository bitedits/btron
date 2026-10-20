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

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_SETTINGS_DLG_H_ */
