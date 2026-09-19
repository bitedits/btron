/*
 * B-System (BTRON 3.20) Workbench Event Coordinator Header: workbench.h
 * Cleanroom Specification-based architecture for Sakamura BTRON3 Workbench.
 */

#ifndef _BTRON_WORKBENCH_H_
#define _BTRON_WORKBENCH_H_

#include <btron/types.h>
#include <btron/dp.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize Workbench menus and tracking for screen width */
void workbench_init(H w);

/* Unified BTRON 3.20 Workbench Event Dispatcher */
void workbench_process_event(GDEV *screen, const EVT *ev);

/* Render complete Workbench desktop with overlays and cursor */
void workbench_render(GDEV *screen, H w, H h);

/* Re-render ONLY the transient overlays (open menus) onto the existing
 * backbuffer, skipping the full desktop composite (background + every window
 * + panel).  An open menu's overlay repaints its whole rect each call, so this
 * is ghost-free.  Used for menu-hover mouse moves so the full composite does
 * not run per mouse-move and starve the 1 ms INPUT plane. */
void workbench_render_overlay_only(GDEV *screen);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_WORKBENCH_H_ */
