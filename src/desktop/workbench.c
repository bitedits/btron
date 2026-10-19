/*
 * B-System (BTRON 3.20) Workbench Event Coordinator: workbench.c
 * Pure Specification-based implementation of Sakamura BTRON / BTRON3 Architecture.
 */

#include <btron/workbench.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/desktop.h>
#include <btron/tip.h>

void workbench_init(H w) {
    tracker_init();
    global_menu_init();
    global_menu_set_screen_width(w);
}

void workbench_process_event(GDEV *screen, const EVT *ev) {
    (void)screen;
    if (!ev) return;

    if (ev->type == EV_BUT_DOWN) {
        /* 1. Global System Menu Bar (Deskbar & Sakamura Chokanji Top Menus) */
        if (global_menu_handle_mouse_down(ev->pos.x, ev->pos.y)) {
            return;
        }
        if (global_menu_is_open()) {
            global_menu_close();
        }

        /* 2. Tracker Start Menu */
        if (tracker_handle_mouse_down(ev->pos.x, ev->pos.y)) {
            return;
        }
        if (tracker_is_menu_open()) {
            tracker_close_menu();
        }

        /* 3. Window Manager (Hit-testing, focus, corner resize, dragging, sliding tab, close, client) */
        if (wnd_mgr_handle_event(ev)) {
            return;
        }

        /* 4. Desktop Virtual Object Icons (Cabinet, Editor, Terminal, Sound, Chat) */
        desktop_handle_click(ev->pos.x, ev->pos.y);

    } else if (ev->type == EV_BUT_UP) {
        tracker_handle_mouse_up(ev->pos.x, ev->pos.y);
        wnd_mgr_handle_event(ev);

    } else if (ev->type == EV_MOUSE_MOVE) {
        set_baremetal_mouse_pos(ev->pos.x, ev->pos.y);
        if (global_menu_is_open() || ev->pos.y <= 25) {
            global_menu_handle_mouse_move(ev->pos.x, ev->pos.y);
        }
        if (tracker_is_menu_open()) {
            tracker_handle_mouse_move(ev->pos.x, ev->pos.y);
        }
        if (wnd_mgr_is_interacting() || ev->button != 0) {
            wnd_mgr_handle_event(ev);
        }

    } else if (ev->type == EV_KEY_DOWN) {
        if (global_menu_handle_key(ev->key, ev->data)) {
            return;
        }
        if (tracker_handle_key(ev->key)) {
            return;
        }
        wnd_mgr_handle_event(ev);
    }
}

/* Desktop Overlays (Global Menu Bar & Tracker Start Menu):
 * Overlays are rendered on top of all application windows, including hardware-accelerated
 * 3D surfaces (e.g. PS2 Graphics Synthesizer layers). Registering overlay rectangles via
 * `wnd_gl_cover_add()` ensures hardware compositing properly preserves menu regions.
 */
static void workbench_cover_overlays(void)
{
    RECT r;
    if (global_menu_get_open_rect(&r)) wnd_gl_cover_add(&r);
    if (tracker_get_menu_rect(&r)) wnd_gl_cover_add(&r);
}

void workbench_render(GDEV *screen, H w, H h) {
    if (!screen) return;
    redraw_baremetal_desktop(screen, w, h);

    const uint32_t t_ovl = btron_render_perf_us();

    /* Overlay Rendering Dispatch:
     * When any system menu or Tracker menu is active, `global_menu_render_overlay()`
     * manages rendering and internally coordinates with `tracker_render_menu()`
     * to prevent redundant drawing passes.
     */
    if (global_menu_is_open()) {
        global_menu_render_overlay(screen);
    } else {
        tracker_render_menu(screen);
    }
    workbench_cover_overlays();

    /* Overlay mouse cursor on top of active menus if enabled for backbuffer */
    if (g_cursor_in_backbuffer) {
        H mx = 0, my = 0;
        get_baremetal_mouse_pos(&mx, &my);
        draw_baremetal_mouse_cursor(screen, mx, my, w, h);
    }

    btron_render_stat_max(&g_render_stats.overlay_us, btron_render_perf_us() - t_ovl);
}

void workbench_render_damage(GDEV *screen, const RECT *damage) {
    if (!screen || !damage) return;
    redraw_baremetal_desktop_rect(screen, damage);
}

void workbench_render_damage_paint(GDEV *screen, const RECT *damage) {
    if (!screen || !damage) return;
    redraw_baremetal_desktop_rect_paint(screen, damage);
}

void workbench_render_overlay_only(GDEV *screen) {
    if (!screen) return;

    const uint32_t t_ovl = btron_render_perf_us();

    /* Fast Overlay Repaint:
     * Repaints only the active menu region on top of the existing backbuffer
     * without triggering a full desktop re-composite.
     */
    if (global_menu_is_open()) {
        global_menu_render_overlay(screen);
    } else {
        tracker_render_menu(screen);
    }
    workbench_cover_overlays();

    if (g_cursor_in_backbuffer) {
        H w = screen->width, h = screen->height;
        H mx = 0, my = 0;
        get_baremetal_mouse_pos(&mx, &my);
        draw_baremetal_mouse_cursor(screen, mx, my, w, h);
    }

    btron_render_stat_max(&g_render_stats.overlay_us, btron_render_perf_us() - t_ovl);
}
