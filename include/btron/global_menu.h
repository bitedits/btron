/*
 * B-System (BTRON 3.20) — Global Top-Bar Menu: global_menu.h
 *
 * Systematic 5-header bar layout (left → right):
 *
 *  Idx  Title           Purpose
 *  ─────────────────────────────────────────────────────────────
 *   0   ［BTRON］       Deskbar (Tracker app launcher) + power strip
 *   1   システム(S)     System-wide configuration (settings, display, audio…)
 *   2   アプリ(A)       All launchable applications at one level
 *   3   実身・仮身(O)   BTRON real/virtual-object operations
 *   4   ウィンドウ(W)   Window management + live open-window list
 */

#ifndef _BTRON_GLOBAL_MENU_H_
#define _BTRON_GLOBAL_MENU_H_

#include <btron/types.h>
#include <btron/dp.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GMENU_MAX_ITEMS     21
#define GMENU_HEADER_COUNT   5

/* ── Header indices ──────────────────────────────────────────── */
#define GMENU_HDR_BTRON     0   /* ［BTRON］ Deskbar + power commands    */
#define GMENU_HDR_SYSTEM    1   /* システム(S) System configuration      */
#define GMENU_HDR_APPS      2   /* アプリ(A)  All applications           */
#define GMENU_HDR_OBJECTS   3   /* 実身・仮身(O) Object operations       */
#define GMENU_HDR_WINDOWS   4   /* ウィンドウ(W) Window management       */

/* ── Command IDs ─────────────────────────────────────────────── */
enum {
    GMENU_CMD_NONE = 0,

    /* ── ［BTRON］ power strip ── */
    GMENU_CMD_SYS_SLEEP      = 101,
    GMENU_CMD_SYS_RESTART    = 102,
    GMENU_CMD_SYS_SHUTDOWN   = 103,
    GMENU_CMD_SYS_QUIT       = 104,

    /* ── システム(S) configuration ── */
    GMENU_CMD_SYS_ABOUT      = 110,
    GMENU_CMD_SYS_SETTINGS   = 111,
    GMENU_CMD_SYS_DISPLAY    = 112,
    GMENU_CMD_SYS_AUDIO      = 113,

    /* ── アプリ(A) — general B-System apps ── */
    GMENU_CMD_APP_TEDITOR    = 200,
    GMENU_CMD_APP_TERMINAL   = 201,
    GMENU_CMD_APP_AUDIO      = 202,
    GMENU_CMD_APP_ORCHESTRA  = 203,
    GMENU_CMD_APP_CHAT       = 204,
    GMENU_CMD_APP_DRIVESETUP = 205,
    GMENU_CMD_APP_CLARITY    = 206,
    GMENU_CMD_APP_PAINT      = 207,

    /* ── アプリ(A) — 超漢字 Cho-Kanji suite ── */
    GMENU_CMD_APP_CK_CABINET     = 210,
    GMENU_CMD_APP_CK_DOC         = 211,
    GMENU_CMD_APP_CK_MICROSCRIPT = 212,
    GMENU_CMD_APP_CK_CLOCK       = 213,
    GMENU_CMD_APP_CK_KCONV       = 214,
    GMENU_CMD_APP_CK_XFCONV      = 215,
    GMENU_CMD_APP_CK_UNPACK      = 216,

    /* ── Appearance (shared between システム and アプリ) ── */
    GMENU_CMD_APP_STYLE_CLASSIC  = 220,
    GMENU_CMD_APP_STYLE_MODERN   = 221,

    /* ── 実身・仮身(O) ── */
    GMENU_CMD_OBJ_CABINET    = 301,
    GMENU_CMD_OBJ_SEARCH     = 302,
    GMENU_CMD_OBJ_NEW        = 303,
    GMENU_CMD_OBJ_STORAGE    = 304,

    /* ── ウィンドウ(W) ── */
    GMENU_CMD_WND_CASCADE    = 401,
    GMENU_CMD_WND_TILE       = 402,
    GMENU_CMD_WND_HIDE_ALL   = 403,
    GMENU_CMD_WND_CYCLE      = 404,
    GMENU_CMD_WND_SELECT_BASE = 420   /* + window index */
};

/* ── Back-compat aliases (legacy call-sites compile unchanged) ─ */
#define GMENU_CMD_SYS_ORCHESTRA  GMENU_CMD_APP_ORCHESTRA
#define GMENU_CMD_CK_CABINET     GMENU_CMD_APP_CK_CABINET
#define GMENU_CMD_CK_DOC         GMENU_CMD_APP_CK_DOC
#define GMENU_CMD_CK_MICROSCRIPT GMENU_CMD_APP_CK_MICROSCRIPT
#define GMENU_CMD_CK_CLOCK       GMENU_CMD_APP_CK_CLOCK
#define GMENU_CMD_CK_KCONV       GMENU_CMD_APP_CK_KCONV
#define GMENU_CMD_CK_XFCONV      GMENU_CMD_APP_CK_XFCONV
#define GMENU_CMD_CK_UNPACK      GMENU_CMD_APP_CK_UNPACK
#define GMENU_CMD_CK_PMC_STYLE_CLASSIC GMENU_CMD_APP_STYLE_CLASSIC
#define GMENU_CMD_CK_PMC_STYLE_MODERN  GMENU_CMD_APP_STYLE_MODERN
#define GMENU_CMD_TOOL_TEDITOR   GMENU_CMD_APP_TEDITOR
#define GMENU_CMD_TOOL_TERMINAL  GMENU_CMD_APP_TERMINAL
#define GMENU_CMD_TOOL_CLARITY   GMENU_CMD_APP_CLARITY
#define GMENU_CMD_TOOL_PALETTE   GMENU_CMD_SYS_SETTINGS
#define GMENU_CMD_TOOL_TRONCODE  GMENU_CMD_SYS_SETTINGS
#define GMENU_CMD_TOOL_MOZC_DICT GMENU_CMD_SYS_SETTINGS
#define GMENU_CMD_TOOL_MATRIX    GMENU_CMD_APP_DRIVESETUP
#define GMENU_HDR_CHOKANJI       GMENU_HDR_APPS

/* ── Data structures ─────────────────────────────────────────── */
typedef struct {
    char label[64];
    char shortcut[16];
    int  cmd_id;
    BOOL is_separator;
    BOOL is_checked;
    BOOL enabled;
} GMenuItem;

typedef struct {
    const char *title;
    RECT        rect;
    int         item_count;
    GMenuItem   items[GMENU_MAX_ITEMS];
} GMenuHeader;

/* ── Public API ──────────────────────────────────────────────── */
void global_menu_init(void);
void global_menu_render_bar(GDEV *dev);
void global_menu_render_overlay(GDEV *dev);
BOOL global_menu_handle_mouse_move(H x, H y);
BOOL global_menu_handle_mouse_down(H x, H y);
BOOL global_menu_handle_key(UW key, VW mod);
void global_menu_close(void);
BOOL global_menu_is_open(void);

BOOL global_menu_get_open_rect(RECT *out);
int  global_menu_get_active(void);
int  global_menu_get_hover_header(void);
void global_menu_set_screen_width(H w);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_GLOBAL_MENU_H_ */
