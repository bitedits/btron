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

/* An overlay is drawn over every window, including one the Graphics Synthesizer owns
 * (doc/txt/PS2.txt, increment 5c), and that layer arrives *after* the canvas upload -- so an
 * overlay that is not registered here is drawn, uploaded, and then buried.  Both getters answer
 * FALSE when nothing is open, which is the common case and costs two calls. */
static void workbench_cover_overlays(void)
{
    RECT r;
    if (global_menu_get_open_rect(&r)) wnd_gl_cover_add(&r);
    if (tracker_get_menu_rect(&r)) wnd_gl_cover_add(&r);
}

void workbench_render(GDEV *screen, H w, H h) {
    if (!screen) return;
    redraw_baremetal_desktop(screen, w, h);

    /* LANE 12i's `ovl=` column.  Everything below runs after
     * redraw_baremetal_desktop() has closed the composite's own window, so the
     * present's render bracket bills these microseconds and none of the six
     * comp= stages does -- which is the composite's unnamed residue, and it is
     * timed here because the port cannot see inside this call graph. */
    const uint32_t t_ovl = btron_render_perf_us();

    /* LANE 12i rung (ii): the overlay renderer OWNS the tracker plate.  With the
     * launcher's list up, `global_menu_render_overlay()` takes its first branch
     * and calls `tracker_render_menu()` itself (src/desktop/global_menu.c:406),
     * so the call that used to stand below this one drew the same plate a second
     * time -- ~4.5 ms of the measured 9.0 ms `ovl=` block, per composite.  The
     * dispatch is byte-exact by state.  The state the retired block drew twice
     * (tracker open) can never reach the else arm, because `global_menu_is_open()`
     * is `(active_menu >= 0) || tracker_is_menu_open()`; what the else arm does
     * see is the nothing-open frame, and there the call returns at
     * `tracker_render_menu()`'s own state guard without touching the device -- a
     * measured no-op, held byte-for-byte by the differential in
     * verify/tests/test_desktop_clip.c.  The arm restates the rule for that case;
     * if the delegation above ever stops covering the tracker, this is the line
     * that must change with it. */
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

    /* Same column, same reason: this path is the overlay block on its own, with
     * no composite window around it to close first. */
    const uint32_t t_ovl = btron_render_perf_us();

    /* Same overlay logic as workbench_render() but WITHOUT the full desktop
     * composite: the backbuffer already holds the desktop and the menu's own
     * rect is repainted in full by the overlay renderer, so skipping
     * redraw_baremetal_desktop() leaves no stale highlight.  Same rung (ii)
     * dispatch -- the plate is drawn once here too, or the two paths would
     * disagree about who owns it. */
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
