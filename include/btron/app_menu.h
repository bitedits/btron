/*
 * B-TRON Common Application Menu Subsystem
 * Unified in-window menu bar, BeOS fluid hover tracking, and multi-style rendering.
 */

#ifndef _BTRON_APP_MENU_H_
#define _BTRON_APP_MENU_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

#define APP_MENU_MAX_HEADERS     8
#define APP_MENU_MAX_ITEMS       16
#define APP_MENU_BAR_HEIGHT      21
#define APP_MENU_ROW_HEIGHT      22
#define APP_MENU_DROPDOWN_WIDTH  250
#define APP_MENU_SUBMENU_WIDTH   260

typedef enum {
    APP_MENU_STYLE_CLASSIC_3D = 0,  /* T-Editor authentic 3D beveled plate (default) */
    APP_MENU_STYLE_MODERN_CARD = 1  /* Modern white card with soft drop shadow */
} APP_MENU_STYLE;

typedef struct {
    char label[64];
    char accel[16];
    int  cmd_id;
    BOOL is_separator;
    BOOL has_submenu;
    int  submenu_id;
    BOOL is_checked;
    BOOL enabled;
} APP_MENU_ITEM;

typedef struct {
    char title[48];
    RECT rect;
    int  item_count;
    APP_MENU_ITEM items[APP_MENU_MAX_ITEMS];
} APP_MENU_HEADER;

typedef struct {
    APP_MENU_HEADER headers[APP_MENU_MAX_HEADERS];
    int header_count;

    /* Interactive state */
    int active_menu;       /* -1 if closed, 0..header_count-1 when dropdown open */
    int hover_menu;        /* -1 if none, 0..header_count-1 for hovered header */
    int hover_item;        /* -1 if none, 0..item_count-1 for hovered dropdown item */
    int active_submenu;    /* -1 if none, or item index whose submenu is open */
    int hover_subitem;     /* -1 if none, or hovered index in cascading submenu */

    /* Right margin status text (e.g. document title, zoom percent, item count) */
    char right_text[64];

    /* Visual styling (per-bar or default to global) */
    APP_MENU_STYLE style;
    BOOL use_global_style;
} APP_MENU_BAR;

/* ── Global Style Configuration (Wired to Settings Cabinet) ─────────────── */
void           app_menu_set_global_style(APP_MENU_STYLE style);
APP_MENU_STYLE app_menu_get_global_style(void);

/* ── Menu Construction API ─────────────────────────────────────────────── */
void app_menu_init(APP_MENU_BAR *bar, APP_MENU_STYLE style);
int  app_menu_add_header(APP_MENU_BAR *bar, const char *title, H width);
int  app_menu_add_item(APP_MENU_BAR *bar, int header_idx, const char *label, const char *accel, int cmd_id, BOOL enabled);
int  app_menu_add_separator(APP_MENU_BAR *bar, int header_idx);
int  app_menu_add_submenu_item(APP_MENU_BAR *bar, int header_idx, const char *label, int cmd_id, int submenu_id);
void app_menu_set_right_text(APP_MENU_BAR *bar, const char *text);

/* ── Interaction & Event Handlers ──────────────────────────────────────── */
BOOL app_menu_handle_mouse_move(APP_MENU_BAR *bar, H rel_x, H rel_y);
BOOL app_menu_handle_mouse_down(APP_MENU_BAR *bar, H rel_x, H rel_y, int *out_cmd, int *out_subitem);
BOOL app_menu_handle_key(APP_MENU_BAR *bar, UW key, uint16_t mod, int *out_cmd);
void app_menu_close(APP_MENU_BAR *bar);
void app_menu_open(APP_MENU_BAR *bar, int header_idx);

/* ── Rendering Functions ───────────────────────────────────────────────── */
void app_menu_draw_3d_bevel_box(GDEV *dev, const RECT *r);
void app_menu_paint_bar(const APP_MENU_BAR *bar, GDEV *dev);
void app_menu_paint_dropdown(const APP_MENU_BAR *bar, GDEV *dev);
void app_menu_paint_cascading_strings(const APP_MENU_BAR *bar, GDEV *dev, const char items[][64], int count);

/* ── Common Nano About Box Dialog Builder ───────────────────────────────── */
typedef WND* (*AppMenuAboutHookFn)(const char *app_title, const char *jp_title,
                                   const char *desc, const char *attribution,
                                   int x, int y);
void app_menu_set_about_hook(AppMenuAboutHookFn hook);

WND* app_menu_create_about_dialog(const char *app_title, const char *jp_title,
                                  const char *desc, const char *attribution,
                                  int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_APP_MENU_H_ */
