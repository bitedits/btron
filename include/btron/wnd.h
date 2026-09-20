/*
 * B-TRON Specification Compatible Header: wnd.h
 * Sakamura Window Manager Primitives & Structure definitions.
 */

#ifndef _BTRON_WND_H_
#define _BTRON_WND_H_

#include <btron/types.h>
#include <btron/error.h>
#include <btron/dp.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WND_ATTR_TITLE       (1 << 0)
#define WND_ATTR_CLOSE       (1 << 1)
#define WND_ATTR_MAX         (1 << 2)
#define WND_ATTR_RESIZE      (1 << 3)
#define WND_ATTR_BORDER      (1 << 4)
#define WND_ATTR_COMPACT_TAB (1 << 5)
#define WND_ATTR_SLIDING_TAB (1 << 6)

typedef struct WND {
    ID    id;
    char  pad0[12];    /* 12 bytes alignment padding -> title starts at offset 16 (16-byte aligned!) */
    char  title[64];
    RECT  bounds;      /* starts at offset 80 (8-byte aligned!) */
    RECT  client;      /* starts at offset 88 (8-byte aligned!) */
    UW    attr;
    BOOL  visible;
    BOOL  focused;
    H     tab_offset_x;/* Horizontal offset of compact sliding tab from window left */
    H     tab_width;   /* Dynamic compact tab width */
    GDEV  *dev;        /* starts at offset 112 (8-byte aligned!) */
    void (*paint)(struct WND *wnd, GDEV *dev);
    void (*event_handler)(struct WND *wnd, const EVT *evt);
    void (*destroy)(struct WND *wnd);
    /* Returns TRUE when this window's in-app menu bar dropdown is open.  Lets
     * the workbench loop repaint only the top window (and present just its
     * bounds) on menu hover/open/close instead of a full desktop composite.
     * NULL for windows without an in-app menu. */
    BOOL (*menu_open)(struct WND *wnd);
    VW    user_data;
    struct WND *next;
    struct WND *prev;
} WND;

ER   init_wnd_mgr(GDEV *screen_dev);
WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr);
ER   cls_wnd(WND *wnd);
ER   top_wnd(WND *wnd);
ER   mov_wnd(WND *wnd, H x, H y);
ER   rsz_wnd(WND *wnd, H w, H h);
ER   wrsz_wnd(WND *wnd, const RECT *r);
ER   inval_wnd(WND *wnd);

ER   wset_tab_offset(WND *wnd, H offset_x);
ER   wget_tab_rect(const WND *wnd, RECT *tab_rect);
BOOL whit_test_tab(const WND *wnd, H x, H y);
BOOL whit_test_close_btn(const WND *wnd, H x, H y);

void redraw_all_windows(void);
void redraw_all_windows_clip(const RECT *damage, BOOL blit_only);
/* Repaint and composite the focused top-level window only. */
void redraw_top_window(void);
WND* find_wnd_at(H x, H y);
WND* get_top_wnd(void);
BOOL wnd_mgr_contains(const WND *wnd);

/* Union of all visible window bounds; empty rect (0,0,0,0) when none. */
void wnd_get_union_bounds(RECT *out);
WND* get_wnd_list(void);
GDEV* wnd_mgr_get_screen(void);

/* Desktop Window Layout Management */
void wnd_cascade_all(void);
void wnd_tile_all(void);
void wnd_hide_all(void);
void wnd_cycle_focus(void);

/* BTRON 3.20 Window Manager Interaction & Event Dispatcher */
BOOL wnd_mgr_handle_event(const EVT *ev);
BOOL wnd_mgr_is_interacting(void);
WND* wnd_mgr_get_drag_target(void);
BOOL wnd_mgr_flush_resize(void);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_WND_H_ */
