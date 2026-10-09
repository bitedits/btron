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
void global_menu_render_bar(GDEV *dev) { (void)dev; }
ER init_evt_sys(void) { return E_OK; }
ER tip_init(void) { return E_OK; }
TIP_DFA_STATE tip_get_state(void) { return TIP_STATE_IDLE; }
H tip_get_caret_x(void) { return 0; }
H tip_get_caret_y(void) { return 0; }
void tip_render_candidate_window(GDEV *dev, H x, H y) { (void)dev; (void)x; (void)y; }

static int s_pass, s_fail;

#define CHECK(cond, msg) do {                                        \
    if (cond) { printf("  [PASS] %s\n", msg); s_pass++; }            \
    else { printf("  [FAIL] %s (line %d)\n", msg, __LINE__); s_fail++; } \
} while (0)

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

int main(void)
{
    uint32_t misses;

    printf("==========================================================\n");
    printf(" Desktop Boxed-Present Losslessness (PS2 800x600, Pi 1024x768)\n");
    printf("==========================================================\n");

    /* Fixed before any composite: the icon plates the background draws are laid
     * out from this setting, and the reference and the bands must agree on it. */
    appearance_set_icon_size(BTRON_ICON_SIZE_32);

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
    printf("  BAND PRESENT RESULTS: %d / %d checks passed (%.1f%%)\n",
           s_pass, s_pass + s_fail, 100.0 * s_pass / (s_pass + s_fail));
    printf("==========================================================\n");
    return s_fail ? 1 : 0;
}
