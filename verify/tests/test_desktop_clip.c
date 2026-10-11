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
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <btron/types.h>
#include <btron/error.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/desktop.h>
#include <btron/settings.h>

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
WND* open_xmb_window(void) { return NULL; }
WND* open_glgears_window(void) { return NULL; }
WND* open_lilcu64_demo_window(void) { return NULL; }
void global_menu_render_bar(GDEV *dev) { (void)dev; }
ER init_evt_sys(void) { return E_OK; }
ER tip_init(void) { return E_OK; }
TIP_DFA_STATE tip_get_state(void) { return TIP_STATE_IDLE; }
H tip_get_caret_x(void) { return 0; }
H tip_get_caret_y(void) { return 0; }
void tip_render_candidate_window(GDEV *dev, H x, H y) { (void)dev; (void)x; (void)y; }

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
 * every XMB frame was that ([XMBT] cycle's comp= column, doc/md/PS2.md). */
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
 * doc/md/PS2.md.  It now goes through the builder that owns the cache and fills the same
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
