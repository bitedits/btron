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
    /* FALSE while the client pixmap (dev) does not hold the window's current
     * art: never painted since it was opened, resized (rsz_wnd re-opens dev),
     * or invalidated by the app through inval_wnd().  A composite may only skip
     * the paint callback and blit dev when this is TRUE; blitting an invalid
     * image stamps an empty client area over real pixels. */
    BOOL  img_valid;
} WND;

ER   init_wnd_mgr(GDEV *screen_dev);
WND* opn_wnd(const char *title, H x, H y, H w, H h, UW attr);
ER   cls_wnd(WND *wnd);
ER   top_wnd(WND *wnd);
ER   mov_wnd(WND *wnd, H x, H y);
ER   rsz_wnd(WND *wnd, H w, H h);
ER   wrsz_wnd(WND *wnd, const RECT *r);
ER   inval_wnd(WND *wnd);

/* App/shell-driven damage accumulator.  inval_wnd() and desktop code add rects
 * they repaint; wnd_take_inval_damage() removes and returns their union so the
 * composite loop presents exactly what changed instead of guessing. */
void wnd_inval_damage_rect(const RECT *r);
BOOL wnd_take_inval_damage(RECT *out);

ER   wset_tab_offset(WND *wnd, H offset_x);
ER   wget_tab_rect(const WND *wnd, RECT *tab_rect);
BOOL whit_test_tab(const WND *wnd, H x, H y);
BOOL whit_test_close_btn(const WND *wnd, H x, H y);

void redraw_all_windows(void);
void redraw_all_windows_clip(const RECT *damage, BOOL blit_only);
/* Repaint and composite the focused top-level window only. */
void redraw_top_window(void);

/* TRUE when a composite over `damage` must re-run paint callbacks: some
 * visible window intersecting it has an invalid client image (freshly opened,
 * resized or inval_wnd()ed), so blitting its cached pixmap alone would show
 * stale or empty pixels. */
BOOL wnd_damage_needs_paint(const RECT *damage);
/* TRUE when any visible window still needs a paint pass. */
BOOL wnd_any_image_invalid(void);
/* Union of the bounds of every visible window whose client image is invalid;
 * empty rect (0,0,0,0) when none. */
void wnd_get_invalid_image_bounds(RECT *out);
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

/* Window the held button currently mutates, and how: *kind is
 * WND_INTERACT_DRAG / _SLIDE / _RESIZE, or _NONE when nothing is in flight
 * (then NULL is returned).  Lets the compositor bound a held-move repaint by
 * the geometry that actually changed. */
enum {
    WND_INTERACT_NONE = 0,
    WND_INTERACT_DRAG,
    WND_INTERACT_SLIDE,
    WND_INTERACT_RESIZE
};
WND* wnd_mgr_get_interaction(int *kind);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_WND_H_ */
