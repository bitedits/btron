/*
 * Headless proof that a boxed present is lossless -- and cheap -- on the PS2's
 * 800x600 canvas.
 *
 * The PS2 present paints the damaged region of the desktop and uploads only that
 * region (see ps2_paint_bands() in src/cores/core_ps2.c), bounded in columns as well
 * as in rows: the machine priced the two shapes and a full-width band of 16 rows
 * turns out to cost ten times the sprite's own 16x16 box.  That is only correct if a
 * box repaints exactly the pixels a whole-canvas paint would, and only worth
 * anything if it does so without rebuilding the whole desktop underneath the clip.
 * Both halves are asserted here, at 800x600 and again at the 1024x768 the Pi 400
 * uses, so the shared compositor is held to the same law on both.
 *
 * The counter that separates the two halves is g_render_stats.bg_full_calls: the
 * background layer restores from a cached canvas and clips, or it falls back to the
 * procedural rebuild -- a whole-canvas fill, the dot grid and five scaled LZW icon
 * decodes -- which no amount of banding downstream can make cheap.  It used to latch
 * only at 1024x768, so an 800x600 present paid that rebuild once per band and the
 * cursor stayed slow.
 *
 * Increment 9a adds a third law to the same canvas: a GS layer window's body plate is
 * not filled, because the stream that same pass records paints those pixels into VRAM
 * a few microseconds after the band that holds them is uploaded.  That is a refusal to
 * write 384,000 pixels per XMB frame (6.5 ms of the measured 20.4 ms `frame=`), so it
 * is only legitimate while the skipped set is exactly the rect the GS covers -- one
 * pixel wider and the present shows desktop art where a grey plate belongs.  The
 * difference set is therefore pinned from both sides, and the counters that priced the
 * saving are checked against the geometry rather than trusted.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <btron/types.h>
#include <btron/error.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/pmc.h>
#include <btron/desktop.h>
#include <btron/settings.h>
#include <btron/global_menu.h>
#include <btron/tracker.h>
#include <btron/tip.h>
#include <btron/workbench.h>

/* Stub accessory entry points, so desktop.c links without the app set.  The same
 * list test_tracker.c uses; the panel body in particular is a no-op, which is what
 * makes the two composites comparable. */
WND* open_vobj_manager_window(void) { return NULL; }
WND* open_control_panel_window(void) { return NULL; }
WND* open_t_editor_window(void) { return NULL; }
WND* open_gterm_window(void) { return NULL; }
WND* open_audio_player_window(void) { return NULL; }
WND* open_orchestra_window(void) { return NULL; }
WND* open_drivesetup_window(void) { return NULL; }
WND* launch_beos_chat(void) { return NULL; }
WND* open_clarity_window(void) { return NULL; }
WND* open_review_window(void) { return NULL; }
WND* open_xmb_window(void) { return NULL; }
WND* open_glgears_window(void) { return NULL; }
WND* open_lilcu64_demo_window(void) { return NULL; }
/* The rest of the launcher set global_menu.c references, plus the TIP state its
 * bar renderer reads -- the strong-extern half of its weak/strong split. */
WND* open_chat_main_window(void *client) { (void)client; return NULL; }
WND* open_display_settings_window(void) { return NULL; }
WND* open_quake_window(int x, int y, int width, int height)
{ (void)x; (void)y; (void)width; (void)height; return NULL; }
WND* open_msx_window(void) { return NULL; }
WND* open_chokanji_cabinet_window(void) { return NULL; }
WND* open_chokanji_doc_window(void) { return NULL; }
WND* open_chokanji_microscript_window(void) { return NULL; }
WND* open_chokanji_clock_window(void) { return NULL; }
WND* open_chokanji_kconv_window(void) { return NULL; }
WND* open_chokanji_xfconv_window(void) { return NULL; }
WND* open_chokanji_unpack_window(void) { return NULL; }
TIP_INPUT_MODE tip_get_mode(void) { return TIP_MODE_ASCII; }
/* global_menu.c is now linked for real (LANE 12i rung (ii)), so the bar renderer
 * and the open-rect getter come from it; only the TIP toggle is stubbed, because
 * tip_ife.c is not in this suite and no path under test calls it. */
void tip_toggle_mode(void) {}
ER init_evt_sys(void) { return E_OK; }
ER tip_init(void) { return E_OK; }
TIP_DFA_STATE tip_get_state(void) { return TIP_STATE_IDLE; }
H tip_get_caret_x(void) { return 0; }
H tip_get_caret_y(void) { return 0; }
void tip_render_candidate_window(GDEV *dev, H x, H y) { (void)dev; (void)x; (void)y; }
ER snd_evt(const EVT *p_evt) { (void)p_evt; return E_OK; }

/* A clock, so the stage timings this file asserts can be asserted at all.
 *
 * btron_render_perf_us() is a weak `return 0` in src/graphics/dp_core.c and each
 * platform core (core_ps2.c, core_arm64.c) installs a strong one, so the shared host
 * build structurally reads 0 for every timing column.  That makes "the whole-canvas
 * composite reports its stages" unmeasurable rather than false -- a path that reports
 * nothing and a path with no clock print the same zeroes.  This is the host's version
 * of the core's instrument, and the checks below only ask relations between columns
 * (a stage that did work is non-zero, a total covers its parts), which a host clock
 * answers truthfully even though its absolute microseconds mean nothing here. */
uint32_t btron_render_perf_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000);
}

static int s_pass, s_fail, s_skip;

#define CHECK(cond, msg) do {                                        \
    if (cond) { printf("  [PASS] %s\n", msg); s_pass++; }            \
    else { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); s_fail++; } \
} while (0)

/* An assertion this harness cannot build, stated as such rather than rounded into a
 * pass: the run then says what it did not check. */
#define SKIP(msg, reason)                                            \
    do { printf("  [SKIP] %s -- %s\n", (msg), (reason)); s_skip++; } \
while (0)

/* Client art with two properties the comparison needs: every pixel non-zero, so a
 * blit-only band cannot silently pass by writing transparency, and x^y structure,
 * so a band offset or a wrong stride shows up as a mismatch rather than as a
 * plausible-looking image. */
static void paint_pattern(WND *wnd, GDEV *dev)
{
    (void)wnd;
    for (H y = 0; y < dev->height; y++) {
        for (H x = 0; x < dev->width; x++) {
            dev->pixels[(size_t)y * dev->width + x] =
                ((x ^ y) & 16) ? COLOR_YELLOW : COLOR_NAVY;
        }
    }
}

#define SCRATCH 0xDEADBEEFu

/* One canvas, painted two ways.  `cols` and `rows` are the present's own box: cols
 * == w is the full-width band the PS2 used to present, and 16x16 is the sprite's own
 * sweep, which is what the loop hands the compositor now.  Returns the number of
 * differing pixels between the boxed result and the whole-canvas reference, and
 * reports how many background rebuilds the boxed pass needed. */
static unsigned compare_banded(H w, H h, H cols, H rows, uint32_t *misses_out)
{
    static COLOR fb[1024 * 768];
    COLOR *ref;
    const size_t bytes = (size_t)w * h * sizeof(COLOR);
    GDEV *screen = opn_dev_vram(w, h, fb);
    if (!screen) { printf("  [FAIL] opn_dev_vram(%d,%d)\n", w, h); s_fail++; return 1; }
    init_wnd_mgr(screen);

    /* The sprite is excluded on purpose: the PS2 band draws it itself, after the
     * composite, and a present that clipped it differently would be a different
     * question from the one asked here. */
    g_cursor_in_backbuffer = 0;

    WND *a = opn_wnd("Banded A", 40, 60, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    WND *b = opn_wnd("Banded B", (H)(w - 340), (H)(h - 260), 320, 240,
                     WND_ATTR_TITLE | WND_ATTR_BORDER);
    if (!a || !b) { printf("  [FAIL] opn_wnd\n"); s_fail++; return 1; }
    a->paint = paint_pattern;
    b->paint = paint_pattern;
    inval_wnd(a);
    inval_wnd(b);

    redraw_baremetal_desktop(screen, w, h);           /* reference, and it latches the cache */
    ref = (COLOR *)malloc(bytes);
    if (!ref) { printf("  [FAIL] out of memory\n"); s_fail++; return 1; }
    memcpy(ref, fb, bytes);

    const uint32_t misses0 = g_render_stats.bg_full_calls;

    memset(fb, 0xFF, bytes);                           /* nothing left of the reference */
    for (H y = 0; y < h; y = (H)(y + rows)) {
        const H y1 = (H)(y + rows > h ? h : y + rows);
        for (H x = 0; x < w; x = (H)(x + cols)) {
            const H x1 = (H)(x + cols > w ? w : x + cols);
            RECT band = { x, y, x1, y1 };
            /* The PS2 present's exact decision, not a convenient one: a box whose
             * windows are invalid must re-run their paint callbacks. */
            if (wnd_damage_needs_paint(&band)) redraw_baremetal_desktop_rect_paint(screen, &band);
            else                               redraw_baremetal_desktop_rect(screen, &band);
        }
    }

    unsigned diff = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) if (ref[i] != fb[i]) diff++;
    *misses_out = g_render_stats.bg_full_calls - misses0;

    cls_wnd(a);
    cls_wnd(b);
    free(ref);
    return diff;
}

/* One client image plus a tally of how often the compositor asked for it.  The tally
 * is the part the pixel comparison cannot see: re-rendering a window whose image is
 * already valid produces exactly the right pixels, so on a screen it is indistinguishable
 * from blitting it -- and on the PS2 it costs the app's whole frame again.  55 ms of
 * every XMB frame was that ([XMBT] cycle's comp= column, doc/txt/PS2.txt). */
static const WND *s_tally_a;
static const WND *s_tally_b;
static unsigned s_paints_a, s_paints_b;

static void paint_counted(WND *wnd, GDEV *dev)
{
    if (wnd == s_tally_a) s_paints_a++;
    else if (wnd == s_tally_b) s_paints_b++;
    paint_pattern(wnd, dev);
}

/* Two windows share the band, one app invalidates.  The walk has to render the stale
 * image and blit the valid one, which is the decision the composite used to make for
 * the whole band rather than per window. */
static void stale_window_selectivity(void)
{
    static COLOR fb[800 * 600];
    COLOR *ref;
    unsigned diff = 0;
    const RECT whole = { 0, 0, 800, 600 };

    GDEV *screen = opn_dev_vram(800, 600, fb);
    if (!screen) { printf("  [FAIL] opn_dev_vram(800,600)\n"); s_fail++; return; }
    init_wnd_mgr(screen);
    g_cursor_in_backbuffer = 0;

    WND *a = opn_wnd("Stale A", 40, 60, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    WND *b = opn_wnd("Fresh B", 200, 150, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    if (!a || !b) { printf("  [FAIL] opn_wnd\n"); s_fail++; return; }
    s_tally_a = a;
    s_tally_b = b;
    a->paint = paint_counted;
    b->paint = paint_counted;

    /* Reference: the walk with both images declared stale, which is the only case that
     * has to render both, and the picture the selective pass below has to reproduce
     * pixel for pixel from one render and one blit. */
    inval_wnd(a);
    inval_wnd(b);
    s_paints_a = s_paints_b = 0;
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    CHECK(s_paints_a == 1u && s_paints_b == 1u,
          "a composite over two stale images renders both, so the tally is wired");
    ref = (COLOR *)malloc(sizeof fb);
    if (!ref) { printf("  [FAIL] out of memory\n"); s_fail++; return; }
    memcpy(ref, fb, sizeof fb);

    memset(fb, 0xFF, sizeof fb);
    inval_wnd(a);                          /* B keeps the image it already has */
    s_paints_a = s_paints_b = 0;
    CHECK(wnd_damage_needs_paint(&whole),
          "one stale window is still enough to rule out a blit-only pass");
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    CHECK(s_paints_a == 1u, "the stale window is rendered");
    CHECK(s_paints_b == 0u, "the window with a valid image is blitted, not re-rendered");
    for (size_t i = 0; i < sizeof fb / sizeof *ref; i++) if (ref[i] != fb[i]) diff++;
    printf("  selective composite: %u differing pixels, A rendered %u, B rendered %u\n",
           diff, s_paints_a, s_paints_b);
    CHECK(diff == 0, "one render and one blit give exactly the pixels two renders give");

    /* The other half of the law: rendering is what makes an image valid again, so a
     * second pass over the same band must not ask for it a second time. */
    s_paints_a = s_paints_b = 0;
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    CHECK(s_paints_a == 0u && s_paints_b == 0u,
          "and the pass after it renders neither -- the band is settled");

    cls_wnd(a);
    cls_wnd(b);
    s_tally_a = s_tally_b = NULL;
    free(ref);
}

/* The whole-canvas composite has to report the stages it runs.
 *
 * `redraw_baremetal_desktop()` does the same four jobs as the damage composite --
 * background, windows, panel, test bars -- and used to time none of them.  On the PS2,
 * where an open overlay or a canvas-wide damage takes that path, its cost reached the
 * log only as the residue between a present's `render=` and its window columns: 25 ms was
 * guessed there until this path reported its own stages, which measured 12.3 ms of desktop
 * art (9.9 of it the background restore) against a 277 ms frame -- see consequence 2 of
 * doc/txt/PS2.txt.  It now goes through the builder that owns the cache and fills the same
 * columns, and a path that reports nothing is indistinguishable, from the outside, from one
 * that reports zero.
 */
static void whole_canvas_reports_its_stages(void)
{
    static COLOR fb[800 * 600];
    const H w = 800, h = 600;
    const size_t bytes = (size_t)w * h * sizeof(COLOR);
    char msg[256];
    RENDER_STATS st;
    unsigned diff = 0;
    COLOR *before;
    GDEV *screen = opn_dev_vram(w, h, fb);

    printf("\n==========================================================\n");
    printf(" Whole-Canvas Composite Self-Reporting (PS2 800x600)\n");
    printf("==========================================================\n");
    if (!screen) { printf("  [FAIL] opn_dev_vram(800,600)\n"); s_fail++; return; }
    init_wnd_mgr(screen);
    g_cursor_in_backbuffer = 0;

    redraw_baremetal_desktop(screen, w, h);            /* latches the cache at this size */
    btron_render_stats_take(&st);                      /* and its latching pass is not the question */

    before = (COLOR *)malloc(bytes);
    if (!before) { printf("  [FAIL] out of memory\n"); s_fail++; return; }
    memcpy(before, fb, bytes);

    redraw_baremetal_desktop(screen, w, h);            /* the measured pass, counters clean */
    btron_render_stats_take(&st);

    for (size_t i = 0; i < (size_t)w * h; i++) if (before[i] != fb[i]) diff++;

    printf("  whole-canvas composite: bg=%u frame=%u paint=%u blit=%u panel=%u bars=%u comp=%u us,"
           " %u background rebuilds\n",
           st.bg_us, st.frame_us, st.paint_us, st.blit_us, st.panel_us, st.bars_us, st.comp_us,
           st.bg_full_calls);

    CHECK(st.bg_full_calls == 0u, "the second whole-canvas composite rebuilds no background");
    CHECK(st.bg_us > 0u, "and it reports the background restore it did");
    CHECK(st.bars_us > 0u, "and it reports the test bars it filled");
    /* The panel's timing window is real, but its body is one of the stubs at the top of
     * this file (global_menu_render_bar is a no-op here), so the only work it encloses is
     * the two-row gold accent -- which at host speed can land inside a single tick.
     * Whether the device's panel costs anything is this harness cannot say. */
    SKIP("and it reports the system panel", "the panel bar is stubbed in this harness");
    snprintf(msg, sizeof(msg), "and its total covers the stages it names (%u >= %u)",
             st.comp_us, st.bg_us + st.panel_us + st.bars_us);
    CHECK(st.comp_us >= st.bg_us + st.panel_us + st.bars_us, msg);
    CHECK(diff == 0u, "two whole-canvas composites are the same picture");

    free(before);
}

/* Increment 9a: the body plate yields the rect the stream owns -- and nothing else.
 *
 * `frame=` on the PS2 measures 20.4 ms of a 112.7 ms XMB composite, and the two-point
 * regression over `body=` (715,360 px -> 13,069 us against 1,109,568 px -> 20,380 us) puts
 * the marginal rate at the same 18.5 ns/px as the average, with an intercept of about
 * -200 us: that stage is essentially one whole-body flat fill per window.  A layer window
 * is then drawn twice, once into the canvas by the fill and once into VRAM by the stream the
 * same pass records, and only the second copy reaches the screen, because the present uploads
 * the band first and sends the stream after (doc/txt/PS2.txt row 9a: -6,547 us measured at
 * `plate=covered`).  Refusing 384,000 px a frame is legitimate only while the refused set is
 * exactly the rect the GS covers -- one pixel wider is a hole of desktop art where a grey
 * plate belongs, and no bench row would show it.  So this asserts the set equality, not the
 * saving.
 *
 * Four composites of the same desktop give it from both sides:
 *   desktop  -- no windows at all: what a skipped rect has to show,
 *   recorded -- the walk that re-runs the layer's paint callback, so a stream exists and the
 *               plate may be refused,
 *   plate    -- a blit-only walk over the same live layer window: no stream is staged, so the
 *               plate stays, which is the other half of `records` being the predicate,
 *   painted  -- the same recording walk with the layer flag cleared: the Pi 400's data state,
 *               and the picture the shared compositor drew before this change.
 * Then a 32x32 box, because a box is what the present actually hands the compositor, and a
 * window that keeps its resize grip, because the grip is canvas-drawn art inside the hole.
 */
static void layer_plate_yields_the_stream_rect(void)
{
    static COLOR fb[800 * 600];
    const H w = 800, h = 600;
    const size_t npx = (size_t)w * h;
    const size_t bytes = npx * sizeof(COLOR);
    const RECT whole = { 0, 0, 800, 600 };
    char msg[256];
    RENDER_STATS st;
    COLOR *desktop, *recorded, *plate, *painted;

    printf("\n==========================================================\n");
    printf(" GS Layer Body Plate: the Fill Yields the Rect the Stream Covers\n");
    printf("==========================================================\n");

    GDEV *screen = opn_dev_vram(w, h, fb);
    if (!screen) { printf("  [FAIL] opn_dev_vram(800,600)\n"); s_fail++; return; }
    init_wnd_mgr(screen);
    g_cursor_in_backbuffer = 0;

    /* The plate is the BeOS frame branch's; the Chokanji branch has its own painter and no
     * LTGRAY body fill to skip, so under that chrome these checks would read as a broken
     * skip rather than as a different style. */
    if (pmc_get_style() != WM_STYLE_BEOS) {
        printf("  [FAIL] the walk is chromed to WM_STYLE_%d, not BEOS\n", (int)pmc_get_style());
        s_fail++;
        return;
    }

    desktop = (COLOR *)malloc(bytes);
    recorded = (COLOR *)malloc(bytes);
    plate = (COLOR *)malloc(bytes);
    painted = (COLOR *)malloc(bytes);
    if (!desktop || !recorded || !plate || !painted) {
        printf("  [FAIL] out of memory\n");
        s_fail++;
        free(desktop); free(recorded); free(plate); free(painted);
        return;
    }

    redraw_baremetal_desktop(screen, w, h);   /* windowless reference; latches the cache here */
    memcpy(desktop, fb, bytes);

    WND *lay = opn_wnd("Layer", 200, 120, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    WND *plain = opn_wnd("Plain Plate", 520, 340, 240, 180, WND_ATTR_TITLE | WND_ATTR_BORDER);
    if (!lay || !plain) {
        printf("  [FAIL] opn_wnd\n");
        s_fail++;
        free(desktop); free(recorded); free(plate); free(painted);
        return;
    }
    /* An ordinary window in the same walk, disjoint from the layer one: its plate is the
     * control for "the refusal is per window, not per pass", and it is the case the PS2
     * desktop still has three of. */
    lay->paint = paint_pattern;
    plain->paint = paint_pattern;
    /* opn_wnd() adds WND_ATTR_RESIZE to every bordered window, and the corner grip it then
     * draws is canvas-drawn art *inside* the client rect -- the XMB window clears the
     * attribute for exactly that reason.  Cleared here so the four comparisons below are
     * single-variable; the attribute is put back for its own case at the end. */
    lay->attr &= (UW)~WND_ATTR_RESIZE;
    plain->attr &= (UW)~WND_ATTR_RESIZE;
    /* The only other writer of this flag is egl_create_window_surface(), compiled in only
     * under -DBTRON_GL_PREFER_EEGS.  That is why the lane can be gated on the flag instead
     * of on an #ifdef and still provably leave the Pi 400's walk alone: clearing it here is
     * the Pi's data state, not a simulation of one. */
    lay->gl_layer = TRUE;

    /* The walk's own geometry, restated here rather than reused: `buried` in
     * draw_retro_window_frame(), `wnd->client`, and the rect the environment packet is
     * addressed at (s_gl_client, built in the walk from dest_x/dest_y and the client device)
     * have to be one rectangle, and a test that took the hole from the code would not say
     * so. */
    const H title_h = WND_TITLE_HEIGHT;
    const H border = 4;                      /* draw_retro_window_frame()'s own number */
    const RECT lay_body = { lay->bounds.left, (H)(lay->bounds.top + title_h),
                            lay->bounds.right, lay->bounds.bottom };
    const RECT hole = { (H)(lay_body.left + border), (H)(lay_body.top + border),
                        (H)(lay_body.left + border + lay->dev->width),
                        (H)(lay_body.top + border + lay->dev->height) };
    const uint32_t hole_px = (uint32_t)(hole.right - hole.left) * (uint32_t)(hole.bottom - hole.top);
    const uint32_t body_px = (uint32_t)(lay_body.right - lay_body.left) *
                             (uint32_t)(lay_body.bottom - lay_body.top);
    const uint32_t plain_px = (uint32_t)(plain->bounds.right - plain->bounds.left) *
                              (uint32_t)(plain->bounds.bottom - (plain->bounds.top + title_h));
    snprintf(msg, sizeof(msg), "the plate's hole, the window's client rect and the stream's clip "
             "are one rectangle %d,%d,%d,%d (%u px)",
             hole.left, hole.top, hole.right, hole.bottom, hole_px);
    CHECK(lay->client.left == hole.left && lay->client.top == hole.top &&
          lay->client.right == hole.right && lay->client.bottom == hole.bottom, msg);

    /* 1. The recording pass. */
    inval_wnd(lay);
    inval_wnd(plain);
    btron_render_stats_take(&st);
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    btron_render_stats_take(&st);
    memcpy(recorded, fb, bytes);
    printf("  recording walk: body=%upx skip=%upx (rect %u, layer body %u, plain body %u)\n",
           st.frame_body_px, st.frame_body_skip_px, hole_px, body_px, plain_px);
    snprintf(msg, sizeof(msg), "the recording walk skips exactly the plate (%u px, the rect's %u)",
             st.frame_body_skip_px, hole_px);
    CHECK(st.frame_body_skip_px == hole_px, msg);
    snprintf(msg, sizeof(msg), "and still counts the ring plus the ordinary window's whole body "
             "(%u vs %u)", st.frame_body_px, (body_px - hole_px) + plain_px);
    CHECK(st.frame_body_px == (body_px - hole_px) + plain_px, msg);

    /* 2. A pass over the same live layer window that records nothing: the callback does not
     *    run, so no stream is staged, so the plate has to be there.  This is the half the
     *    `records` predicate exists for -- a skip here would put desktop art where a plate
     *    belongs and show nothing wrong on a bench row. */
    btron_render_stats_take(&st);
    redraw_baremetal_desktop_rect(screen, &whole);          /* blit_only */
    btron_render_stats_take(&st);
    memcpy(plate, fb, bytes);
    CHECK(st.frame_body_skip_px == 0u,
          "a walk that sends no stream skips no plate, even over a live layer window");
    snprintf(msg, sizeof(msg), "and fills both bodies whole (%u vs %u)", st.frame_body_px,
             body_px + plain_px);
    CHECK(st.frame_body_px == body_px + plain_px, msg);

    /* 3. The Pi 400's walk, reached by clearing the flag the EEGS backend would have set. */
    lay->gl_layer = FALSE;
    inval_wnd(lay);
    inval_wnd(plain);
    btron_render_stats_take(&st);
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    btron_render_stats_take(&st);
    memcpy(painted, fb, bytes);
    CHECK(st.frame_body_skip_px == 0u, "with no layer window in the walk, nothing is skipped");
    snprintf(msg, sizeof(msg), "and the shared compositor fills both bodies (%u vs %u)",
             st.frame_body_px, body_px + plain_px);
    CHECK(st.frame_body_px == body_px + plain_px, msg);

    /* The set equality, from both sides at once.  Nothing outside the hole may differ
     * between the recording walk and the walk that fills the plate -- the border, the double
     * inset, the title tab, the ring and every other window are byte-identical -- and inside
     * it all three expected pictures have to be what each pass says they are. */
    unsigned outside = 0, inside = 0, hole_not_desktop = 0, hole_not_plate = 0, hole_not_art = 0;
    unsigned gray_in_desktop = 0;
    const COLOR *lay_px = lay->dev->pixels;
    for (H y = 0; y < h; y++) {
        for (H x = 0; x < w; x++) {
            const size_t i = (size_t)y * w + x;
            if (!(x >= hole.left && x < hole.right && y >= hole.top && y < hole.bottom)) {
                if (recorded[i] != plate[i]) outside++;
                continue;
            }
            if (recorded[i] != desktop[i]) hole_not_desktop++;
            if (plate[i] != COLOR_LTGRAY) hole_not_plate++;
            if (painted[i] != lay_px[(size_t)(y - hole.top) * lay->dev->width + (x - hole.left)])
                hole_not_art++;
            if (desktop[i] == COLOR_LTGRAY) gray_in_desktop++;
            if (recorded[i] != plate[i]) inside++;
        }
    }
    printf("  recorded vs plate: %u differing px outside the hole, %u inside (the hole is %u px)\n",
           outside, inside, hole_px);
    CHECK(outside == 0u, "no pixel outside the client rect changes when the plate is skipped");
    snprintf(msg, sizeof(msg), "every pixel of the hole is the pixel the two walks disagree on "
             "(%u of %u)", inside, hole_px);
    CHECK(inside == hole_px, msg);
    CHECK(hole_not_desktop == 0u,
          "and what a skipped hole holds is the desktop, exactly where a windowless canvas has it");
    CHECK(hole_not_plate == 0u, "what the non-recording walk leaves there is the plate itself");
    CHECK(hole_not_art == 0u,
          "and with the layer flag cleared the hole is the blitted client art again -- the Pi's picture");
    CHECK(gray_in_desktop == 0u,
          "the desktop under the hole is never the plate's grey, so the two sets cannot coincide");

    /* 4. The same decision at a box's size, which is the shape the present actually drives.
     *    The stream's environment packet scissors the client rect, not the band, so a band
     *    that reaches the layer refuses the whole rect: the count is the decision's, not the
     *    writes'.  (The same is true of `body=` before this change, which is why a clipped
     *    band's frame= looked twenty times faster than the store rate allows.) */
    lay->gl_layer = TRUE;
    for (size_t i = 0; i < npx; i++) fb[i] = SCRATCH;
    const RECT box = { (H)(hole.left - 8), (H)(hole.top - 8), (H)(hole.left + 24), (H)(hole.top + 24) };
    btron_render_stats_take(&st);
    redraw_baremetal_desktop_rect_paint(screen, &box);
    btron_render_stats_take(&st);
    unsigned box_wrong = 0, box_leaked = 0;
    for (H y = 0; y < h; y++) {
        for (H x = 0; x < w; x++) {
            const size_t i = (size_t)y * w + x;
            if (x >= box.left && x < box.right && y >= box.top && y < box.bottom) {
                if (fb[i] != recorded[i]) box_wrong++;
            } else if (fb[i] != SCRATCH) {
                box_leaked++;
            }
        }
    }
    printf("  32x32 box over the hole's corner: skip=%upx body=%upx, %u box px disagreeing with "
           "the whole-canvas walk, %u px written outside the box\n",
           st.frame_body_skip_px, st.frame_body_px, box_wrong, box_leaked);
    snprintf(msg, sizeof(msg), "a 1,024 px box reports the whole %u px rect as skipped -- the "
             "refusal is the stream's rect, not the band's area", hole_px);
    CHECK(st.frame_body_skip_px == hole_px, msg);
    CHECK(box_wrong == 0u, "and the box still reproduces the whole-canvas walk's pixels inside itself");
    CHECK(box_leaked == 0u, "while writing nothing outside its own rect: the skip does not widen the band");

    /* 5. A layer window that keeps its resize grip.  The grip sits inside the client rect and
     *    is drawn by the decoration's own fill_rec, not by the plate, so the refusal must not
     *    reach it: on the device the hole is fully buried by the stream only because the app
     *    asked for no grip, and the compositor cannot assume that of every layer window. */
    lay->attr |= WND_ATTR_RESIZE;
    inval_wnd(lay);
    btron_render_stats_take(&st);
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    btron_render_stats_take(&st);
    const RECT grip = { (H)(lay_body.right - 14), (H)(lay_body.bottom - 14),
                        (H)(lay_body.right - 2), (H)(lay_body.bottom - 2) };
    const H gx1 = grip.right < hole.right ? grip.right : hole.right;
    const H gy1 = grip.bottom < hole.bottom ? grip.bottom : hole.bottom;
    unsigned grip_desktop = 0, hole_rest_wrong = 0;
    for (H y = grip.top; y < gy1; y++) {
        for (H x = grip.left; x < gx1; x++) {
            if (fb[(size_t)y * w + x] == desktop[(size_t)y * w + x]) grip_desktop++;
        }
    }
    for (H y = hole.top; y < grip.top; y++) {
        for (H x = hole.left; x < hole.right; x++) {
            if (fb[(size_t)y * w + x] != desktop[(size_t)y * w + x]) hole_rest_wrong++;
        }
    }
    printf("  grip on: skip=%upx body=%upx, %u of %u grip px showing desktop through, "
           "%u px of the hole above the grip not holding desktop art\n",
           st.frame_body_skip_px, st.frame_body_px, grip_desktop,
           (uint32_t)(gy1 - grip.top) * (uint32_t)(gx1 - grip.left), hole_rest_wrong);
    CHECK(grip_desktop == 0u,
          "the corner grip inside the hole is still drawn -- the skip is the plate, not every fill in the rect");
    CHECK(hole_rest_wrong == 0u, "and the rest of the hole stays unfilled with the attribute back on");
    snprintf(msg, sizeof(msg), "the count stays the rect's %u px even though the grip writes %u of "
             "them (body=%u)", hole_px,
             (uint32_t)(gy1 - grip.top) * (uint32_t)(gx1 - grip.left), st.frame_body_px);
    CHECK(st.frame_body_skip_px == hole_px && st.frame_body_px == (body_px - hole_px) + plain_px,
          msg);

    cls_wnd(lay);            /* closes the layer: it resets the pending stream and the cover list */
    cls_wnd(plain);
    free(desktop); free(recorded); free(plate); free(painted);
}

/* ── LANE 22 (task #165): what the freeze actually refuses ──────────────────
 *
 * The paint worker's whole saving is a pass in which the application's frame is NOT painted, and
 * the first build of it measured exactly nothing: the [XMBT] cycle's `paint=` column stayed at
 * 21.4 ms on every pass of a run whose [PW] row claimed 68% of passes had refused the app.  The
 * reason is in one term.  A layer window is stale in two senses at once -- its pixmap is
 * perpetually dirty, because an animating app invalidates its own client rect every frame, and its
 * picture is not in that pixmap anyway -- and the walk had been asking `!img_valid || (gl_layer &&
 * !frozen)`, so the app's own invalidation outranked the freeze and re-ran the callback the pass
 * had just refused.  `wnd_owes_callback()` makes the layer's schedule the PASS's and only the
 * pass's, which is the difference these four counts measure:
 *
 *  1. the control, so a tally that never fires cannot certify anything;
 *  2. frozen + stale: the layer is refused while the ordinary window in the same walk still
 *     renders -- the refusal is per window, not per pass;
 *  3. `wnd_damage_needs_paint()` agrees, so a frozen layer with nothing else stale lets the band
 *     take its blit-only composite instead of a paint walk that paints nothing;
 *  4. the debt is not hidden (`img_valid` stays false) and the freeze does not outlive its pass.
 */
static void freeze_refuses_the_layer_not_the_pixmap(void)
{
    static COLOR fb[800 * 600];
    const RECT whole = { 0, 0, 800, 600 };
    char msg[256];

    GDEV *screen = opn_dev_vram(800, 600, fb);
    if (!screen) { printf("  [FAIL] opn_dev_vram(800,600)\n"); s_fail++; return; }
    init_wnd_mgr(screen);
    g_cursor_in_backbuffer = 0;

    WND *lay = opn_wnd("Frozen Layer", 60, 60, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    WND *plain = opn_wnd("Plain Again", 420, 300, 300, 200, WND_ATTR_TITLE | WND_ATTR_BORDER);
    if (!lay || !plain) { printf("  [FAIL] opn_wnd\n"); s_fail++; return; }
    s_tally_a = lay;
    s_tally_b = plain;
    lay->paint = paint_counted;
    plain->paint = paint_counted;
    lay->attr &= (UW)~WND_ATTR_RESIZE;
    plain->attr &= (UW)~WND_ATTR_RESIZE;
    lay->gl_layer = TRUE;

    /* 1. The control: unfrozen, both stale, both render. */
    inval_wnd(lay);
    inval_wnd(plain);
    s_paints_a = s_paints_b = 0;
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    CHECK(s_paints_a == 1u && s_paints_b == 1u,
          "unfrozen and stale, the walk renders the layer and the ordinary window: the tally is wired");

    /* 2. The law the PS2's own measurement refuted first.  Both images are dirtied again -- the
     *    app animating is what dirties a layer's pixmap, and it does not stop because the pass
     *    decided to refuse it -- and the freeze outranks that for the layer alone. */
    inval_wnd(lay);
    inval_wnd(plain);
    s_paints_a = s_paints_b = 0;
    wnd_gl_freeze(1);
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    wnd_gl_freeze(0);
    snprintf(msg, sizeof msg, "a frozen pass runs the ordinary window's callback (%u) and not the "
             "layer's (%u) -- the refusal is per window, and an app that dirtied its own rect does "
             "not outrank it", s_paints_b, s_paints_a);
    CHECK(s_paints_a == 0u && s_paints_b == 1u, msg);
    CHECK(lay->img_valid == FALSE,
          "and the refusal leaves the debt standing: img_valid is not set by a callback that never ran");

    /* 3. The same law one level up, where it decides the composite's SHAPE rather than one
     *    window's work: a frozen layer with nothing else stale cannot rule out a blit-only pass. */
    inval_wnd(lay);
    plain->img_valid = TRUE;
    wnd_gl_freeze(1);
    CHECK(wnd_damage_needs_paint(&whole) == FALSE,
          "a frozen layer and no other stale image: this band needs no paint composite at all");
    s_paints_a = s_paints_b = 0;
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    wnd_gl_freeze(0);
    CHECK(s_paints_a == 0u && s_paints_b == 0u, "and the walk that agrees runs neither callback");
    CHECK(wnd_damage_needs_paint(&whole) == TRUE,
          "with the latch down the same stale layer is owed its stream again");

    /* 4. Unfrozen, the callback returns -- which is the falsifier for a freeze that could outlive
     *    its pass and leave a window never painted again. */
    inval_wnd(plain);
    s_paints_a = s_paints_b = 0;
    redraw_baremetal_desktop_rect_paint(screen, &whole);
    snprintf(msg, sizeof msg, "one clear re-opens the layer's callback (%u) and the plain one (%u)",
             s_paints_a, s_paints_b);
    CHECK(s_paints_a == 1u && s_paints_b == 1u, msg);

    lay->gl_layer = FALSE;               /* the stream state is this case's, not the tree's */
    cls_wnd(lay);
    cls_wnd(plain);
}

/* ── LANE 12i rung (ii): the tracker plate is drawn once, not twice ─────────
 *
 * The composite's named residue is this overlay block (PS2.txt LANE 12i), and
 * with the launcher's list up it called `tracker_render_menu()` twice per pass:
 * once inside `global_menu_render_overlay()` (src/desktop/global_menu.c:406)
 * and once by the workbench's own `if (tracker_is_menu_open())`, which the
 * merged tree replaced with a dispatch.  Three independent facts are asserted,
 * because each can fail on its own:
 *
 *  1. BYTE-FOR-BYTE: the two-call block and the production dispatch produce
 *     identical canvases, in every state -- tracker open alone, the BTRON
 *     header with it (the state the wave runs in), a dropdown with the tracker
 *     closed (the state that must NOT change), and nothing open.
 *  2. THE CENSUS: after the block, one further `tracker_render_menu()` changes
 *     zero pixels -- so the refused call could never have written anything,
 *     which is the book's "every store is opaque" claim turned into a
 *     measurement.  It is shown DETECTING a change on the first draw first;
 *     a census that never counts anything would make 0 pass vacuously.
 *  3. THE COUNT: a second harness build compiles every TU but src/desktop/tracker.c
 *     and this one with `-Dtracker_render_menu=counted_tracker_render_menu`, so
 *     the rename lands on the CALL SITES in workbench.c and global_menu.c and on
 *     this file's duplicate-block call, while the definition keeps its real name;
 *     counted_tracker_render_menu lives in the small TU
 *     verify/tests/test_desktop_clip_count_port.c, compiled WITHOUT the rename,
 *     which increments and delegates.  There the retired block shape counts two
 *     in the tracker-open state and the production path counts exactly one.  In
 *     the NORMAL build nothing routes through the wrapper, so the loop below
 *     prints SKIP for those two rows -- a 0==0 that could never fail is not a
 *     gate.
 */

void counted_tracker_render_menu(GDEV *dev);
void counted_calls_reset(void);
unsigned counted_calls(void);

static COLOR s_cfb[2][800 * 600];
static RECT s_pl;      /* the tracker plate's rect, from the unit itself */

/* One composite + overlay block into `fb`, exactly as `workbench_render()`
 * orders it; `dup` selects the retired two-call shape, else the shipped
 * dispatch.  The cursor is off -- the sprite is a different lane's question. */
static void overlay_canvas(int state, COLOR *fb, int dup)
{
    global_menu_init();
    tracker_init();
    global_menu_set_screen_width(800);
    g_cursor_in_backbuffer = 0;
    /* Absolute state, never incremental: global_menu_init()/tracker_init() do
     * not close a menu the PREVIOUS case left open, and last-run's tracker was
     * exactly the kind of fact this harness must not inherit. */
    global_menu_close();
    if (state == 1) tracker_open_menu();
    if (state == 2) global_menu_handle_mouse_down(45, 10);   /* BTRON header */
    if (state == 3) {                                        /* dropdown, no tracker */
        global_menu_handle_mouse_down(45, 10);
        global_menu_handle_mouse_move(150, 10);              /* hot-switch to SYSTEM */
    }
    const BOOL want_tr = (state == 1 || state == 2);
    const BOOL got_tr = tracker_is_menu_open();
    const int act = global_menu_get_active();
    /* State 1 is the tracker ALONE (active_menu stays -1; the launcher opens it
     * without a header); state 2 is the header click, which takes active_menu
     * with it; state 3 is a dropdown with the tracker closed. */
    const BOOL ok_state = (want_tr == got_tr) &&
        (state == 0 || state == 1 ? act < 0 :
         state == 3 ? act == GMENU_HDR_SYSTEM : act >= 0);
    if (!ok_state) {
        printf("  [FAIL] overlay state %d set-up: tracker=%d active=%d\n", state, got_tr, act);
        s_fail++;
        return;
    }
    GDEV *screen = opn_dev_vram(800, 600, fb);
    init_wnd_mgr(screen);
    memset(fb, 0, sizeof s_cfb[0]);
    redraw_baremetal_desktop(screen, 800, 600);
    if (dup) {
        /* The retired shape, written out: two state questions, each guarding its
         * own call.  Both tracker spellings are the wrapper's name, so the
         * rename build counts every call either shape makes. */
        if (global_menu_is_open()) global_menu_render_overlay(screen);
        if (tracker_is_menu_open()) counted_tracker_render_menu(screen);
    } else {
        /* The shipped shape: one dispatch.  The else arm fires exactly when
         * nothing is open, and the renderer's own state guard answers it with
         * zero writes -- the byte-for-byte row below is that claim. */
        if (global_menu_is_open()) global_menu_render_overlay(screen);
        else                       counted_tracker_render_menu(screen);
    }
}

/* Changed-pixel count inside the plate rect between two snapshots. */
static unsigned census_plate(COLOR *before)
{
    unsigned changed = 0;
    for (H y = s_pl.top; y < s_pl.bottom; y++)
        for (H x = s_pl.left; x < s_pl.right; x++)
            if (s_cfb[0][(size_t)y * 800 + x] != before[(size_t)y * 800 + x]) changed++;
    return changed;
}

static unsigned canvas_diff(const COLOR *a, const COLOR *b)
{
    unsigned d = 0;
    for (size_t i = 0; i < 800u * 600u; i++) if (a[i] != b[i]) d++;
    return d;
}

static void overlay_plate_drawn_once(void)
{
    static const char *names[4] = { "nothing open", "tracker open",
                                    "BTRON + tracker", "dropdown, tracker closed" };
    static COLOR plate0[800 * 600];

    /* The census must be able to see a change at all: snapshot, draw the plate
     * once from a cleared canvas, and require a non-zero difference over the
     * rect the tracker itself reports. */
    overlay_canvas(0, s_cfb[0], 0);
    memcpy(plate0, s_cfb[0], sizeof plate0);
    overlay_canvas(1, s_cfb[0], 0);
    if (!tracker_get_menu_rect(&s_pl) || s_pl.right <= s_pl.left ||
        s_pl.bottom <= s_pl.top) {
        SKIP("tracker plate census detects a change", "tracker_get_menu_rect gave no rect");
        return;
    }
    unsigned plate_px = (unsigned)(s_pl.right - s_pl.left) * (s_pl.bottom - s_pl.top);
    counted_calls_reset();
    CHECK(census_plate(plate0) > 0 && census_plate(plate0) <= plate_px,
          "the plate census detects a plate drawn where none was");
    counted_calls_reset();

    for (int st = 0; st < 4; st++) {
        char msg[96];
        counted_calls_reset();
        overlay_canvas(st, s_cfb[0], 1);                    /* retired: two calls */
        unsigned dup_calls = counted_calls();
        counted_calls_reset();
        overlay_canvas(st, s_cfb[1], 0);                    /* shipped: dispatch */
        unsigned one_calls = counted_calls();
        counted_calls_reset();
#ifndef tracker_render_menu
        (void)dup_calls; (void)one_calls;   /* counts only decide under the rename */
#endif

        unsigned d = canvas_diff(s_cfb[0], s_cfb[1]);
        snprintf(msg, sizeof msg, "%s: dispatch canvas is byte-identical to the two-call block", names[st]);
        CHECK(d == 0, msg);

        /* Idempotence on the production canvas, and only meaningful where the
         * plate exists at all (states 1 and 2).  The CALL COUNTS are decision
         * rows only under the `-Dtracker_render_menu=...` rename -- in a normal
         * build nothing routes through the wrapper, and there the harness says
         * SKIP rather than letting 0==0 pass vacuously. */
        memcpy(plate0, s_cfb[1], sizeof plate0);
        GDEV *after = opn_dev_vram(800, 600, s_cfb[1]);
        if (st == 1 || st == 2) {
            /* Reset first: under the rename this very call routes through the
             * counter, and a leak here would land in the NEXT state's reading. */
            counted_calls_reset();
            tracker_render_menu(after);
            snprintf(msg, sizeof msg, "%s: one more tracker call changes zero pixels", names[st]);
            CHECK(census_plate(plate0) == 0, msg);
#ifdef tracker_render_menu
            snprintf(msg, sizeof msg, "%s: the shipped path calls tracker_render_menu once", names[st]);
            CHECK(one_calls == 1u, msg);
            snprintf(msg, sizeof msg, "%s: the retired block called it twice", names[st]);
            CHECK(dup_calls == 2u, msg);
#else
            snprintf(msg, sizeof msg, "%s: tracker_render_menu called 1x vs 2x", names[st]);
            SKIP(msg, "count needs the -Dtracker_render_menu rename build; run it separately");
#endif
        }
        /* The two states whose plate never gets drawn, counted apart because the
         * shapes spend them differently (the book's decision table): a dropdown
         * takes the dispatch's if arm with the tracker closed -- 0 vs 0; nothing
         * open takes the else arm, whose single guarded call the renderer itself
         * refuses -- 1 vs 0, and the byte-for-byte row above is what says that
         * call changed no pixel. */
#ifdef tracker_render_menu
        else if (st == 0) {
            snprintf(msg, sizeof msg, "%s: the else arm fires once and draws nothing", names[st]);
            CHECK(one_calls == 1u && dup_calls == 0u, msg);
        } else {
            snprintf(msg, sizeof msg, "%s: neither shape calls the tracker renderer", names[st]);
            CHECK(one_calls == 0u && dup_calls == 0u, msg);
        }
#else
        else {
            snprintf(msg, sizeof msg, "%s: tracker_render_menu called 1x/0x vs 0x", names[st]);
            SKIP(msg, "count needs the -Dtracker_render_menu rename build; run it separately");
        }
#endif
    }

    /* The instrument rung (i) landed: workbench_render()'s own bracket fills
     * `ovl=` on a target with a clock.  The host clock is real (see the top of
     * this file), so this asserts the wiring, not a magnitude. */
    static COLOR fb[800 * 600];
    g_render_stats.overlay_us = 0;
    GDEV *wscreen = opn_dev_vram(800, 600, fb);
    init_wnd_mgr(wscreen);
    g_cursor_in_backbuffer = 0;
    global_menu_init();
    tracker_init();
    global_menu_set_screen_width(800);
    tracker_open_menu();
    workbench_render(wscreen, 800, 600);
    CHECK(g_render_stats.overlay_us > 0,
          "workbench_render() times the overlay block into the ovl= column");
}

int main(void)
{
    uint32_t misses;

    printf("==========================================================\n");
    printf(" Desktop Boxed-Present Losslessness (PS2 800x600, Pi 1024x768)\n");
    printf("==========================================================\n");

    /* Fixed before any composite: the icon plates the background draws are laid
     * out from this setting, and the reference and the bands must agree on it. */
    appearance_set_icon_size(BTRON_ICON_SIZE_32);

    stale_window_selectivity();
    whole_canvas_reports_its_stages();
    layer_plate_yields_the_stream_rect();
    freeze_refuses_the_layer_not_the_pixmap();
    overlay_plate_drawn_once();

    /* 800x600 is the PS2's workbench.  Both halves failing here is the bug this
     * port had: the composite rebuilt the desktop per band, and the cursor stayed
     * slow however few rows were uploaded. */
    unsigned d16 = compare_banded(800, 600, 800, 16, &misses);
    printf("  800x600 in 16-row bands: %u differing pixels, %u background rebuilds\n", d16, misses);
    CHECK(d16 == 0, "800x600 banded present reproduces the whole-canvas pixels");
    CHECK(misses == 0, "800x600 banded present rebuilds no background (cache clips)");

    unsigned d40 = compare_banded(800, 600, 800, 40, &misses);
    printf("  800x600 in 40-row bands: %u differing pixels, %u background rebuilds\n", d40, misses);
    CHECK(d40 == 0, "800x600 holds at a coarser band");
    CHECK(misses == 0, "800x600 coarser band still clips the background");

    /* The shapes the present makes now that a band is a rectangle: the sprite's own
     * 16x16 box, and the 80x32 box a fast sweep dirties.  Clipping in x is the half
     * this file never asserted, and it is the half the tenfold saving is only real
     * if it holds -- a box that lost the plate under it would read as a quicker
     * present of the wrong desktop.  Neither size divides the canvas, so the ragged
     * last row and column are covered by these two as well. */
    unsigned b16 = compare_banded(800, 600, 16, 16, &misses);
    printf("  800x600 in 16x16 boxes: %u differing pixels, %u background rebuilds\n", b16, misses);
    CHECK(b16 == 0, "800x600 boxed present reproduces the whole-canvas pixels");
    CHECK(misses == 0, "800x600 boxed present clips the background in x");

    unsigned b80 = compare_banded(800, 600, 80, 32, &misses);
    printf("  800x600 in 80x32 boxes: %u differing pixels, %u background rebuilds\n", b80, misses);
    CHECK(b80 == 0, "800x600 holds at a swept box");
    CHECK(misses == 0, "800x600 swept box clips the background");

    /* 1024x768 is the Pi 400's canvas, and the only size the cache used to latch
     * at.  A regression here would be the Pi losing art it used to draw. */
    unsigned dpi = compare_banded(1024, 768, 1024, 16, &misses);
    printf("  1024x768 in 16-row bands: %u differing pixels, %u background rebuilds\n", dpi, misses);
    CHECK(dpi == 0, "1024x768 banded present is unchanged");
    CHECK(misses == 0, "1024x768 keeps clipping the background");

    /* Switching canvas size is the one thing that legitimately misses the cache:
     * the buffer holds one canvas, so the first rect composite at a new size falls
     * back to the rebuild -- and re-keys the latch, so the next one clips.  Counting
     * exactly one proves the store is keyed by the device rather than pinned to one
     * resolution, which is the defect that made every PS2 band expensive. */
    static COLOR fb[1024 * 768];
    GDEV *small = opn_dev_vram(800, 600, fb);
    init_wnd_mgr(small);
    const RECT top = { 0, 0, 800, 16 };
    uint32_t misses1 = g_render_stats.bg_full_calls;
    render_desktop_background_rect(small, &top);      /* 1024x768 latch: one miss */
    uint32_t misses2 = g_render_stats.bg_full_calls;
    render_desktop_background_rect(small, &top);      /* 800x600 latch: none */
    CHECK(misses2 == misses1 + 1,
          "the first rect composite at a new canvas size rebuilds exactly once");
    CHECK(g_render_stats.bg_full_calls == misses2,
          "and the next one at that size clips out of the re-keyed cache");

    printf("\n==========================================================\n");
    printf("  BAND PRESENT RESULTS: %d / %d checks passed (%.1f%%), %d skipped\n",
           s_pass, s_pass + s_fail, 100.0 * s_pass / (s_pass + s_fail), s_skip);
    printf("==========================================================\n");
    return s_fail ? 1 : 0;
}
