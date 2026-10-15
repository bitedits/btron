/*
 * LANE 12i rung (ii), the COUNT half of the gate -- see the block comment in
 * verify/tests/test_desktop_clip.c (`overlay_plate_drawn_once`).
 *
 * The decision rows "the shipped dispatch calls the tracker renderer once" and
 * "the retired two-call block called it twice" need the calls counted, and a
 * `-Dtracker_render_menu=counted_tracker_render_menu` rename counts them -- but
 * the rename hits EVERY TU it compiles, definitions included: in tracker.c it
 * would rename the real rasteriser away, and inside the counting wrapper itself
 * it would rewrite the delegation into self-recursion.  So the wrapper lives
 * here, in a TU the harness compiles WITHOUT the rename, where
 * `tracker_render_menu` below is the real symbol from src/desktop/tracker.c.
 * The renamed TUs (workbench.c, global_menu.c) call
 * `counted_tracker_render_menu`, which lands here, increments, and delegates.
 *
 * In the normal build nothing routes through this file -- the harness's loop
 * asserts SKIP for exactly that reason -- and `counted_calls` never leaves 0.
 */
#include <btron/types.h>
#include <btron/dp.h>

void tracker_render_menu(GDEV *dev);

static unsigned s_tracker_calls;

void counted_tracker_render_menu(GDEV *dev)
{
    s_tracker_calls++;
    tracker_render_menu(dev);
}

void counted_calls_reset(void) { s_tracker_calls = 0; }

unsigned counted_calls(void) { return s_tracker_calls; }
