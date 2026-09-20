/*
 * B-TRON Specification Compatible Header: desktop.h
 * BTRON Desktop Shell & System Panel definitions.
 */

#ifndef _BTRON_DESKTOP_H_
#define _BTRON_DESKTOP_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    H width;
    H height;
    GDEV *screen;
    BOOL running;
} BTRON_DESKTOP;

ER init_desktop(H width, H height);
ER init_desktop_vram(H width, H height, COLOR *vram_ptr);
void run_desktop_loop(void);
void render_desktop_background(GDEV *dev);
void render_desktop_background_rect(GDEV *dev, const RECT *damage);
void render_system_panel(GDEV *dev);
BOOL desktop_handle_click(H x, H y);
BTRON_DESKTOP* get_btron_desktop(void);
GDEV* init_baremetal_desktop(uint32_t *fb, uint32_t w, uint32_t h);
void redraw_baremetal_desktop(GDEV *screen, H w, H h);
void redraw_baremetal_desktop_rect(GDEV *screen, const RECT *damage);
void draw_baremetal_mouse_cursor(GDEV *screen, H mx, H my, H w, H h);
void draw_baremetal_cursor_raw(volatile uint32_t *pixels, H mx, H my, H w, H h);
extern int g_cursor_in_backbuffer;
void set_baremetal_mouse_pos(H x, H y);
void get_baremetal_mouse_pos(H *x, H *y);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DESKTOP_H_ */
