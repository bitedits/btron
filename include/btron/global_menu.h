#ifndef _BTRON_GLOBAL_MENU_H_
#define _BTRON_GLOBAL_MENU_H_

#include <btron/types.h>
#include <btron/dp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GMENU_MAX_ITEMS     20
#define GMENU_HEADER_COUNT  5

/* Global Menu Header Indices */
#define GMENU_HDR_BTRON     0   /* ［BTRON］ Deskbar Start */
#define GMENU_HDR_SYSTEM    1   /* システム(S) */
#define GMENU_HDR_OBJECTS   2   /* 実身・仮身(O) */
#define GMENU_HDR_WINDOWS   3   /* ウィンドウ(W) */
#define GMENU_HDR_TOOLS     4   /* 道具・文字(T) */

/* Global Menu Command IDs */
enum {
    GMENU_CMD_NONE = 0,

    /* システム(S) */
    GMENU_CMD_SYS_ABOUT = 101,
    GMENU_CMD_SYS_SETTINGS,
    GMENU_CMD_SYS_AUDIO,
    GMENU_CMD_SYS_ORCHESTRA,
    GMENU_CMD_SYS_DISPLAY,
    GMENU_CMD_SYS_RESTART,
    GMENU_CMD_SYS_SHUTDOWN,

    /* 実身・仮身(O) */
    GMENU_CMD_OBJ_CABINET = 201,
    GMENU_CMD_OBJ_SEARCH,
    GMENU_CMD_OBJ_NEW,
    GMENU_CMD_OBJ_STORAGE,

    /* ウィンドウ(W) */
    GMENU_CMD_WND_CASCADE = 301,
    GMENU_CMD_WND_TILE,
    GMENU_CMD_WND_HIDE_ALL,
    GMENU_CMD_WND_CYCLE,
    GMENU_CMD_WND_SELECT_BASE = 320, /* + wnd index */

    /* 道具・文字(T) */
    GMENU_CMD_TOOL_PALETTE = 401,
    GMENU_CMD_TOOL_TRONCODE,
    GMENU_CMD_TOOL_MOZC_DICT,
    GMENU_CMD_TOOL_TEDITOR,
    GMENU_CMD_TOOL_MATRIX,
    GMENU_CMD_TOOL_TERMINAL,
    GMENU_CMD_TOOL_CLARITY
};

typedef struct {
    char label[64];
    char shortcut[16];
    int cmd_id;
    BOOL is_separator;
    BOOL is_checked;
    BOOL enabled;
} GMenuItem;

typedef struct {
    const char *title;
    RECT rect;
    int item_count;
    GMenuItem items[GMENU_MAX_ITEMS];
} GMenuHeader;

/* API Declarations */
void global_menu_init(void);
void global_menu_render_bar(GDEV *dev);
void global_menu_render_overlay(GDEV *dev);
BOOL global_menu_handle_mouse_move(H x, H y);
BOOL global_menu_handle_mouse_down(H x, H y);
BOOL global_menu_handle_key(UW key, VW mod);
void global_menu_close(void);
BOOL global_menu_is_open(void);
int  global_menu_get_active(void);
int  global_menu_get_hover_header(void);
void global_menu_set_screen_width(H w);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_GLOBAL_MENU_H_ */
