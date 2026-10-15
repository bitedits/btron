/*
 * verify/tests/test_grid_map.c -- LANE 12f gate (a): the VALUES a retained mesh gets
 *
 * `tgl_grid_map()` (`src/gl/tinygl/vertex_stage.c`) lifts five floats out of the live context --
 * the projection's two non-zero entries per axis, the reciprocal of its W, and the viewport's
 * scale and translation -- and `tgl_grid_map_points()` then maps a whole mesh with them, in one
 * loop, without visiting the matrix stack or the vertex walk again.  That is a SECOND ENCODING of
 * `tgl_vertex_to_viewport()`, and a second encoding of a float map is exactly the thing that is
 * byte-correct on a host and wrong on a console: the batch's answer is what reaches the GS as a
 * pixel coordinate, and a difference of one in `x_px` is a one-pixel shear of the XMB ribbon that
 * nobody would call a bug rather than call the picture.
 *
 * So the comparator here is the walk itself, not a restatement of it.  This suite links TinyGL and
 * drives the real API the way `xb_paint()` does -- viewport, `glOrtho(0,w,h,0,-1,1)`, identity
 * modelview, blend on, lighting and texture off -- then submits every swept point through
 * `glColor4f()` + `glVertex2f()` and reads the `ZBufferPoint` that the shipped
 * `tgl_vertex_transform()` + `tgl_vertex_to_viewport()` wrote for it.  The batch's answer for the
 * same (x, y, intensity, alpha) comes from one `tgl_grid_map()` and one `tgl_grid_map_points()`.
 * The two must agree BIT FOR BIT on both device pixels and on all four colour bytes -- not within a
 * tolerance, because the integers are what a pixel is made of.
 *
 * Three arithmetic choices could differ between the two encodings.  Each was put through a mutation
 * and the sweep was required to notice -- and two of the three turned out not to be reachable here,
 * which is written down rather than left as a claim:
 *
 *   - ASSOCIATION.  The walk computes `pcx * winv * scale + trans`, which parses as
 *     `((pcx*winv)*scale)+trans`; the batch spells it that way because that is the walk's own
 *     parse.  It is NOT a difference this suite can see: `tgl_grid_map()` refuses a context unless
 *     `winv` is a constant, and for the app's projection that constant is exactly 1.0f -- checked as
 *     one of the coefficient assertions above -- and a float multiply by 1.0f is exact.  A mutation
 *     that re-associated the product (`pcx * (winv * scale_x)`) was measured and did NOT fail, and
 *     the honest reading is that the spelling matters only for a projection whose W is not 1, which
 *     the ribbon does not have.  The spelling is kept because it is the walk's, not because it is
 *     load-bearing.
 *   - CONTRACTION.  `x * mx + xc` is an FMA candidate on a host that has one, and the walk's
 *     `((x*m0 + y*m1) + z*m2) + m3` is not.  The Emotion Engine has no FMA at all -- the shipped
 *     `tgl_vertex_to_viewport()` disassembles as `mul.s`, `mul.s`, `add.s` -- so this suite is built
 *     `-ffp-contract=off` (see the `%.gm.o` rule).  Measured on the host that ran the controls in
 *     this file (Apple clang 21, arm64), dropping the flag leaves the suite green, so the flag is
 *     insurance against a compiler that contracts by default (gcc's `-ffp-contract=fast`), not the
 *     reason the comparison passes here.
 *   - TRUNCATION, and the precision behind it.  This is the one that is measured to bite.  Every
 *     float -> int in `vertex_stage.c` goes through `TGL_TRUNC()`; the two encodings reach the same
 *     one, and a chain carried in double intermediates instead differs from the image's float chain
 *     by a few ulp of the RESULT -- far smaller than a quarter pixel, so an interior lattice cannot
 *     see it.  The pixel-edge probes (see `add_edge_probes`) are what make it visible: the same
 *     mutation, `(double)pcx * winv * scale_x + trans_x` truncated back to float, was measured to
 *     move 1,932 device pixels at 640x480, 252 at 101x67 and 246 at 64x48, all of them on the probes
 *     and none of them on a lattice point.  A suite that swept only interiors would have passed a
 *     map that is wrong at every pixel boundary.
 *
 * The sweeps: positions on a quarter-pixel lattice over the whole window, so every `x_px` transition
 * of every column is crossed -- including a window of prime size (101x67), where `mx = 2/101` and
 * `scale = 50.25` are both inexact and the identity `x_px == round(x)` cannot hold by construction;
 * intensities in four interleaved families -- the app's own ramp, the exact float where the channel
 * byte's truncate changes, and one float on either side of it by `nextafterf()`; alphas the same
 * way, because the batch lifts ONE `a_byte` for a whole mesh where the walk computes it per vertex,
 * which is a difference in shape if not in value.
 *
 * The refusals are the other half of the honesty: the two-constant-per-axis form is only this
 * arithmetic under conditions, and `tgl_grid_map()` says -1 when they do not hold.  Each refusal is
 * paired with the same state restored and accepted, so a -1 cannot be earned by something else the
 * context happens to lack.
 *
 * Two built-in controls, because a gate that cannot fail is not an instrument:
 *
 *   - PERTURBATION: one good map is copied and each of its coefficients -- `mx`, `xc`, `my`, `yc`,
 *     `scale_x`, `trans_x`, `winv` -- and then its `a_byte` is nudged in turn.  Every nudge must
     break the comparison.  This is what proves the sweep reads those fields.
 *   - FRESHNESS: a context whose model-projection product has not been computed since its last
 *     matrix change must be refused.  `glOrtho` leaves `matrix_model_projection_updated` set and
 *     `matrix_model_projection` holding the PREVIOUS product -- so a map lifted before the frame's
 *     first `glBegin` would take the ribbon through last frame's window for one frame and print
 *     nothing to say so.  The control is that the refusal is tested on a product that is still
 *     diagonal with a constant W from the previous flush, so freshness is the only condition left
 *     that could be refusing it.
 *
 * LANE 12h rung (i) gave the same instrument a second subject: `tgl_grid_map_tex()` and
 * `tgl_grid_map_tex_points()` carry a TEXTURE_2D-bound mesh through the walk's own texel chain --
 * the truncate of `u*sscale + soff` and then the ST field's power-of-two scale -- and the textured
 * section below sweeps a real uploaded sheet against that chain, five fields bit for bit, with the
 * refusal list inverted (`why=10`, NOTEX, for a sheet that is absent rather than present).  The
 * grey map's `why=4` and the textured map's `why=10` are asserted to be complements in the same
 * state, which is what keeps the two maps' conditions from drifting apart unnoticed.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* zgl.h FIRST: it owns GLContext, the vertex record and the colour-channel constants, and it
 * resolves <GL/gl.h> -- including that header's `inline void glOrtho(...){}`, which is a no-op for
 * an app and has to be shadowed.  gl_dispatch.h after it, because that header #defines glOrtho,
 * glVertex2f and glColor4f into calls through the backend's op table, and a function declaration
 * whose name expands into a dispatch call does not compile. */
#include "../../src/gl/tinygl/zgl.h"
#include "../../src/gl/gl_dispatch.h"
#include "../../src/gl/grid_map.h"
#include "../../src/gl/backend_tinygl.h"
#include "../../src/apps/xmb_wave.h"   /* the ribbon's own mesh, for the run that names why=7 */
#include "../../src/drivers/ps2/ps2_gs_pack.h" /* gs_float_scale_pow2(), the ST field's unit */

/* backend_tinygl.c's banner lines and gl_dispatch.c's bracketing clock; this link has no core, and
 * dp_core.c's weak defaults are not in it.  Both answers are the ones a host build gives. */
void uart_puts_raw(const char *s) { (void)s; }
unsigned btron_render_perf_us(void) { return 0u; }

static int g_total, g_failed;

/* A check whose message carries the measured numbers: a sweep that compares 350,000 points and
 * reports only "ok" cannot be read back six months from now. */
static void checkf(long ok, const char *fmt, ...)
{
    char msg[256];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);

    g_total++;
    if (ok > 0) printf("  [PASS] %s\n", msg);
    else { g_failed++; printf("  [FAIL] %s\n", msg); }
}

/* ── the window, and the state the app puts it in ──────────────────────────────────────
 *
 * One surface, three sizes: the PS2's real canvas, a prime-sized window whose map is inexact in
 * every coefficient, and a small window dense enough to sweep whole.  The buffer is bytes rather
 * than PIXELs because ZB_open writes the canvas through the caller's pointer and this test never
 * reads it -- only the vertex record is the subject.
 */
static unsigned char s_fb[640 * 480 * 4];
static int s_w = 640, s_h = 480;

/* xb_paint()'s own setup for a ribbon frame, in the order xmb.c writes it.  The blend FUNCTION
 * differs between the ribbon (GL_DST_COLOR/GL_ONE) and the rest of the frame and nothing in the map
 * reads it; GL_BLEND being on is the one blend fact the map does read (`blended`), so it is on. */
static void app_state(void)
{
    glViewport(0, 0, s_w, s_h);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)s_w, (double)s_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glDisable(GL_LIGHT0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

/* Compute the model-projection product the way the immediate path does, so a map captured after this
 * is compared against the same product the next glBegin would use. */
static void flush_matrices(void)
{
    glBegin(GL_POINTS);
    glEnd();
}

/* ── the sweep ───────────────────────────────────────────────────────────────────────── */

#define SWEEP_MAX 120000

static float *s_xs, *s_ys, *s_bri;
static tgl_grid_pt_t *s_pts;
static int s_n;

/* The channel byte's flip points: `grid_channel_byte()` truncates `v * COLOR_CORRECTED_MULT_MASK +
 * COLOR_MIN_MULT` and takes bits 23:16, so byte `k+1` begins where that product crosses
 * (k+1)*65536 -- the float below.  A rounding error in the colour path lives at a flip point and
 * nowhere else, so the sweep puts one there. */
static float chan_edge(int k)
{
    double v = ((double)(k + 1) * 65536.0 - (double)COLOR_MIN_MULT) /
               (double)COLOR_CORRECTED_MULT_MASK;

    if (v <= 0.0) return 0.0f;
    if (v >= 1.0) return 1.0f;
    return (float)v;
}

/* Four interleaved intensity families: the app's ramp, the flip point, one float below it, one
 * float above it. */
static float bri_at(int i)
{
    const int band = (i / 254) & 3;
    const int k = i % 254;

    if (band == 0) return (float)(i % 256) / 255.0f;
    if (band == 1) return chan_edge(k);
    if (band == 2) return nextafterf(chan_edge(k), 0.0f);
    return nextafterf(chan_edge(k), 1.0f / 0.0f);
}

static void add_pt(int *i, float x, float y, int bidx)
{
    if (*i >= SWEEP_MAX) return;
    s_xs[*i] = x;
    s_ys[*i] = y;
    s_bri[*i] = bri_at(bidx);
    (*i)++;
}

/* A quarter-pixel lattice over the whole window.  The walk truncates a viewport coordinate, so an
 * integer pixel edge is the transition and quarter-pixel steps land on both sides of every one.
 * The far edge stays out of the sweep: at x == w the product `x * (2/w)` sits exactly on the clip
 * volume's boundary, where the walk is entitled to clip the vertex and there would be no
 * `tgl_vertex_to_viewport()` answer to compare against. */
static int sweep_dense(void)
{
    int i = 0, row = 0;
    float y;

    for (y = 0.0f; y < (float)s_h - 0.2f; y += 0.25f, row++) {
        float x;
        int b = 0;

        for (x = 0.0f; x < (float)s_w - 0.2f; x += 0.25f, b++)
            add_pt(&i, x, y, b + row * 7);
    }
    return i;
}

/* The real canvas: every column of four rows and every row of three columns at the same
 * quarter-pixel step.  A whole 640x480 lattice would be 4.9 million points, and the map is
 * separable -- `x_px` is a function of `x` alone, `y_px` of `y` alone -- so bands across both axes
 * cross every boundary the window has. */
static int sweep_bands(void)
{
    static const float ys[4] = { 0.0f, 239.75f, 240.0f, 479.75f };
    static const float xs[3] = { 0.0f, 319.75f, 639.75f };
    int i = 0, r, c, b;
    float v;

    for (r = 0; r < 4; r++)
        for (v = 0.0f, b = 0; v < 639.8f; v += 0.25f, b++)
            add_pt(&i, v, ys[r], b + r * 31);
    for (c = 0; c < 3; c++)
        for (v = 0.0f, b = 0; v < 479.8f; v += 0.25f, b++)
            add_pt(&i, xs[c], v, b + c * 17);
    return i;
}

/* The float x whose viewport value is exactly the integer `n`: the map is monotone in x, so this is
 * its inverse, solved in double and then probed as a float.  This is what "straddle every pixel
 * boundary of the window" means as an instrument rather than as a phrase.
 *
 * The lattice above lands on a pixel edge to within about a quarter of a pixel, which is enough to
 * catch a wrong coefficient and NOT enough to catch a wrong PRECISION: a chain carried in double
 * intermediates differs from the image's float chain by a few units in the last place of the
 * RESULT, and only a point that close to a truncate's transition can show it.  How close is
 * arithmetic, not guesswork -- at x = 640 one float ulp is 6.1e-5, which through mx*scale_x is
 * about 6e-5 of a pixel, so a double-vs-float difference of ~2 ulp of the pixel (1.2e-4) is TWO
 * PROBE STEPS at ±1 ulp and is invisible to them.  Hence ±8: sixteen ulp spans ~1e-3 of a pixel,
 * an order of magnitude more than the error being hunted, so the outer probes are on opposite
 * sides of the transition while the middle one still sits essentially on it.  The first and last
 * edges are skipped because their roots are the clip volume's own faces, where the walk declines to
 * map the vertex at all.
 *
 * See the control this was found by: replacing the map loop's `pcx * winv * scale_x + trans_x` with
 * the same arithmetic in double leaves every quarter-pixel lattice point unaffected and moves the
 * boundary probes. */
#define EDGE_PROBES 5
static const int k_edge_off[EDGE_PROBES] = { -8, -1, 0, 1, 8 };

static void add_edge_probes(int *i, float scale, float trans, float mcoef, float coff,
                            int n_px, float fixed, int vertical, int bidx)
{
    int n, k;

    for (n = 1; n < n_px - 1; n++) {
        double root = (((double)n - (double)trans) / (double)scale - (double)coff) / (double)mcoef;
        float x = (float)root;

        for (k = 0; k < EDGE_PROBES; k++) {
            float v = x;
            int o = k_edge_off[k], steps = o < 0 ? -o : o;

            for (; steps > 0; steps--) v = nextafterf(v, o < 0 ? 0.0f : 1.0f / 0.0f);
            if (!(v >= 0.0f && v < (float)(vertical ? s_h : s_w))) continue;
            if (vertical) add_pt(i, fixed, v, bidx + n * EDGE_PROBES + k);
            else          add_pt(i, v, fixed, bidx + n * EDGE_PROBES + k);
        }
    }
}

/* Edge probes on both axes, at the map's own lifted constants.  The counts are recorded because a
 * probe dropped by the window bounds must not be misread as a probe that agreed. */
static int s_edge0, s_xedge_n, s_yedge_n;
static int s_gate0;

static int sweep_pixel_edges(const tgl_grid_map_t *m)
{
    int i = s_n;

    s_edge0 = i;
    add_edge_probes(&i, m->scale_x, m->trans_x, m->mx, m->xc, s_w, 0.5f, 0, 5);
    s_xedge_n = i - s_edge0;
    add_edge_probes(&i, m->scale_y, m->trans_y, m->my, m->yc, s_h, 0.5f, 1, 11);
    s_yedge_n = i - s_edge0 - s_xedge_n;
    return i;
}

/* ── the comparison ──────────────────────────────────────────────────────────────────── */

/* One byte of a walk channel: `zp.r` is the truncate of `v * 0xfe0000 + 0xffff` masked to 24 bits,
 * and the GS adapter takes `>> 16` off it -- the same two steps `grid_channel_byte()` performs on
 * the same field. */
static uint32_t walk_byte(GLint zp) { return (uint32_t)(zp >> 16) & 0xFFu; }

/* Feed the same mesh through the real walk and hold its answers against the batch's. */
static void compare_mesh(float alpha, const tgl_grid_pt_t *pts, const char *tag,
                         long *bad_px, long *bad_col, long *skipped, int report,
                         uint32_t *lo_out, uint32_t *hi_out, int *edges_out)
{
    GLContext *c = gl_get_context();
    int i, prev_x = -999999, prev_y = -999999, edges = 0;
    uint32_t lo = 255u, hi = 0u;

    glBegin(GL_POINTS);
    for (i = 0; i < s_n; i++) {
        const GLVertex *v;
        uint32_t by;

        glColor4f(s_bri[i], s_bri[i], s_bri[i], alpha);
        glVertex2f(s_xs[i], s_ys[i]);

        v = &c->vertex[0];
        if (v->clip_code != 0) { (*skipped)++; continue; }

        if (v->zp.x != prev_x || v->zp.y != prev_y) { edges++; prev_x = v->zp.x; prev_y = v->zp.y; }
        by = pts[i].rgba & 0xFFu;
        if (by < lo) lo = by;
        if (by > hi) hi = by;

        if (v->zp.x != pts[i].x_px || v->zp.y != pts[i].y_px) {
            (*bad_px)++;
            if (report && *bad_px <= 4)
                printf("      pixel  %s (%9.4f,%9.4f) b=%.7f: walk (%d,%d) batch (%d,%d)\n",
                       tag, s_xs[i], s_ys[i], s_bri[i], v->zp.x, v->zp.y,
                       pts[i].x_px, pts[i].y_px);
        }
        if (walk_byte(v->zp.r) != (pts[i].rgba & 0xFFu) ||
            walk_byte(v->zp.g) != ((pts[i].rgba >> 8) & 0xFFu) ||
            walk_byte(v->zp.b) != ((pts[i].rgba >> 16) & 0xFFu) ||
            walk_byte(v->zp.a) != ((pts[i].rgba >> 24) & 0xFFu)) {
            (*bad_col)++;
            if (report && *bad_col <= 4)
                printf("      colour %s (%9.4f,%9.4f) b=%.7f: walk %02x %02x %02x %02x"
                       " batch %02x %02x %02x %02x\n",
                       tag, s_xs[i], s_ys[i], s_bri[i],
                       (unsigned)walk_byte(v->zp.r), (unsigned)walk_byte(v->zp.g),
                       (unsigned)walk_byte(v->zp.b), (unsigned)walk_byte(v->zp.a),
                       (unsigned)(pts[i].rgba & 0xFFu), (unsigned)((pts[i].rgba >> 8) & 0xFFu),
                       (unsigned)((pts[i].rgba >> 16) & 0xFFu),
                       (unsigned)((pts[i].rgba >> 24) & 0xFFu));
        }
    }
    glEnd();
    if (edges_out) *edges_out = edges;
    if (lo_out) *lo_out = lo;
    if (hi_out) *hi_out = hi;
}

/* LANE 12f rung (ii), measured against the clipper instead of asserted.
 *
 * `eegs_grid_strip()` does not clip: it records the mesh it is handed, and the GS cuts that mesh with
 * the SCISSOR `eegs_present_send()` programs from the rect below.  That is only the same picture as
 * the immediate path drew if the rect answers the clipper's question, so this measures the two
 * directions of that equivalence -- and since the amendment both of them are about pixels on the
 * screen rather than about a refusal, because the batch no longer refuses on the rect at all:
 *
 *   - a point the walk does NOT clip whose pixel the rect REJECTS is a piece of the picture the
 *     SCISSOR would cut and the walk would have drawn.  Until the amendment this class was the batch
 *     refusing work it could have done -- it cost frames and broke nothing, and `slow=` counted it --
 *     and it is now `over=`, counted against the same rect in `verify/tests/test_eegs_raster.c`.
 *     `wrong == 0` here is the claim that the two questions are one question, which is what makes
 *     `over=` a statement about geometry instead of about a number nobody can read.
 *   - a point the walk DOES clip whose pixel the rect ACCEPTS is the batch drawing a vertex the walk
 *     would have removed.  This class is not empty and cannot be, because the map truncates toward
 *     zero: `x = -0.5` in a 640-wide window has `pc.X` just under -1, so the clipper rejects it, and
 *     its viewport value is -0.5, whose truncate is 0 -- inside the rect.  What makes it harmless is
 *     measured rather than waved at: every such point lands ON a rect edge (`vp_x0`, `vp_x1`, `vp_y0`
 *     or `vp_y1`), and the boundary is exactly where the clipper puts the vertex it cuts -- a clip at
 *     `pc.X = -1` maps to `trans - scale`, which `gl_eval_viewport()` makes the integer `xmin`.  So the
 *     pixel the batch records is the pixel the clipped vertex would have been moved to, and the two
 *     paths differ by nothing on the screen.  The inference half of that sentence is the clipper's, and
 *     it is labelled: the equality of the two pixels at the boundary is asserted here, the claim that
 *     `gl_clip()` produces the boundary pixel is TinyGL's own geometry, and `xb_paint()` draws the
 *     ribbon strictly inside its window so no real frame lands in the band at all -- which is what the
 *     `band=` count on this row is for.
 *
 * The probes are the two classes, on both axes, at distances that cross the band's edges: a thousand
 * pixels outside, half a pixel outside, a float's worth outside, a float's worth inside, and the same
 * at the far edge. */
static int sweep_outside(int w, int h)
{
    float xv[14], yv[14];
    int i, idx = s_n;

    for (i = 0; i < 14; i++) {
        static const float d[14] = { -1000.0f, -100.0f, -10.0f, -1.0f, -0.5f, -0.01f, -0.0001f,
                                      0.0f, 0.0001f, 0.01f, 0.5f, 1.0f, 100.0f, 1000.0f };
        xv[i] = (i < 7 ? d[i] : (float)w + d[i]);
        yv[i] = (i < 7 ? d[i] : (float)h + d[i]);
    }
    for (i = 0; i < 14; i++) {
        add_pt(&idx, xv[i], (float)h * 0.5f, 7 + i);
        add_pt(&idx, (float)w * 0.5f, yv[i], 21 + i);
    }
    add_pt(&idx, xv[3], yv[3], 41);            /* the four corners, outside on both axes */
    add_pt(&idx, xv[12], yv[12], 42);
    add_pt(&idx, xv[4], yv[10], 43);
    add_pt(&idx, xv[9], yv[5], 44);
    return idx;
}

static void compare_clip_gate(const tgl_grid_map_t *m, const char *label)
{
    GLContext *c = gl_get_context();
    int i, wrong = 0, band = 0, clipped = 0, reported = 0;

    glBegin(GL_POINTS);
    for (i = s_gate0; i < s_n; i++) {
        const int inside = (s_pts[i].x_px >= m->vp_x0 && s_pts[i].x_px <= m->vp_x1 &&
                            s_pts[i].y_px >= m->vp_y0 && s_pts[i].y_px <= m->vp_y1);

        glColor4f(s_bri[i], s_bri[i], s_bri[i], 1.0f);
        glVertex2f(s_xs[i], s_ys[i]);
        if (c->vertex[0].clip_code == 0) {
            if (!inside) {
                wrong++;
                if (reported++ < 4)
                    printf("      gate    %s (%9.4f,%9.4f): walk drew it, rect rejects pixel"
                           " (%d,%d)\n", label, s_xs[i], s_ys[i], s_pts[i].x_px, s_pts[i].y_px);
            }
        } else {
            clipped++;
            if (inside) {
                const int on_edge = (s_pts[i].x_px == m->vp_x0 || s_pts[i].x_px == m->vp_x1 ||
                                    s_pts[i].y_px == m->vp_y0 || s_pts[i].y_px == m->vp_y1);
                if (!on_edge) {
                    wrong++;
                    if (reported++ < 4)
                        printf("      gate    %s (%9.4f,%9.4f): walk clipped it and the rect's"
                               " interior keeps pixel (%d,%d), not an edge\n",
                               label, s_xs[i], s_ys[i], s_pts[i].x_px, s_pts[i].y_px);
                }
                band++;
            }
        }
    }
    glEnd();

    checkf(wrong == 0,
           "%s: the rect accepts every vertex the walk draws, and clips only onto its own edges"
           " (%d points disagree)", label, wrong);
    checkf(clipped >= 20, "%s: %d of the 32 outside probes were clipped by the real clipper,"
           " so the comparison is not vacuous", label, clipped);
    printf("    %s: clip gate -- %d probes outside the window, %d clipped, %d of them inside the"
           " rect (the check above is what says each one landed on an edge)\n",
           label, s_n - s_gate0, clipped, band);
}

/* One window size: sweep it, and check the batch's answers against the walk's at every alpha. */
static void run_window(const char *label, int w, int h, int dense){
    float alphas[6];
    int n_alphas = 0, a, i;
    tgl_grid_map_t m, base;
    long bad_px = 0, bad_col = 0, skipped = 0, compared = 0;

    memset(&m, 0, sizeof m);
    memset(&base, 0, sizeof base);

    s_w = w; s_h = h;

    /* The app's own route to a window: gl_init() installs TinyGL's op table and opens the ZBuffer
     * over the caller's buffer at exactly this size. */
    tinygl_backend_shutdown();
    gl_init(GL_BACKEND_TINYGL, w, h, s_fb);
    app_state();
    s_n = dense ? sweep_dense() : sweep_bands();
    if (s_n <= 0) { checkf(0, "%s: the sweep produced no points", label); return; }

    alphas[n_alphas++] = 1.0f;                            /* the ribbon's own, opaque */
    alphas[n_alphas++] = 0.5f;
    alphas[n_alphas++] = 0.0f;                            /* fully transparent: a_byte 0 */
    /* Three probes of the alpha byte's own flip point: on it, and one float either side. */
    alphas[n_alphas++] = chan_edge(127);
    alphas[n_alphas++] = nextafterf(chan_edge(127), 0.0f);
    alphas[n_alphas++] = nextafterf(chan_edge(127), 1.0f / 0.0f);

    /* ── FRESHNESS, the paired way: accepted once the walk has computed its product, refused after
     * a matrix change that has not been flushed, and the refused product is shown to still be
     * diagonal with a constant W -- so freshness is the only condition that can be refusing it.
     * The matrix change here is the app's own ortho again, so the stale product is also the CORRECT
     * one: this is about who is allowed to read it, not about its value. ── */
    flush_matrices();
    memset(&m, 0, sizeof m);
    checkf(tgl_grid_map(&m) == 0 && m.refuse == TGL_GRID_REFUSE_NONE,
           "%s: accepted once the walk has computed the product at glBegin", label);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)s_w, (double)s_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    {
        const GLfloat *pm = &gl_ctx.matrix_model_projection.m[0][0];

        memset(&m, 0, sizeof m);
        m.refuse = 0xFFFFFFFFu;
        checkf(tgl_grid_map(&m) == -1 && m.refuse == TGL_GRID_REFUSE_STALE,
               "%s: refused after a matrix change, before the next glBegin, by name (why=%u)",
               label, (unsigned)m.refuse);
        checkf(gl_ctx.matrix_model_projection_updated != 0 &&
               gl_ctx.matrix_model_projection_no_w_transform != 0 &&
               pm[1] == 0.0f && pm[4] == 0.0f,
               "%s: ...while that product is still diagonal with a constant W, so freshness is the"
               " only thing that could refuse it (updated=%d)", label,
               (int)gl_ctx.matrix_model_projection_updated);
    }
    flush_matrices();
    memset(&m, 0, sizeof m);
    checkf(tgl_grid_map(&m) == 0 && m.refuse == TGL_GRID_REFUSE_NONE,
           "%s: accepted again once glBegin has recomputed it -- the refusal was the flag, not the"
           " state", label);

    /* ── the lifted coefficients, against the window the app asked for ── */
    {
        const GLfloat *pm = &gl_ctx.matrix_model_projection.m[0][0];
        const double rl = (double)w - 0.0;      /* r - l, the ortho's own x extent */
        const double tb = 0.0 - (double)h;      /* t - b, negative: the page is flipped */

        checkf(m.mx == pm[0] && m.xc == pm[3] && m.my == pm[5] && m.yc == pm[7],
               "%s: the lift reads the four entries the walk indexes (m[0], m[3], m[5], m[7])", label);
        checkf(m.mx == (GLfloat)(2.0 / rl) &&
               m.xc == (GLfloat)(-(rl + 0.0) / rl) &&
               m.my == (GLfloat)(2.0 / tb) &&
               m.yc == (GLfloat)(-(0.0 + (double)h) / tb),
               "%s: mx == 2/w, xc == -(r+l)/(r-l), my == 2/(t-b), yc == -(t+b)/(t-b) -- the app's"
               " ortho exactly", label);
        checkf(m.winv == 1.0f,
               "%s: pc.W is exactly 1.0f for this projection, so winv is the constant 1.0f the"
               " divide returns", label);
        checkf(m.scale_x == (GLfloat)((w - 0.5) / 2.0) &&
               m.trans_x == (GLfloat)((w - 0.5) / 2.0) &&
               m.scale_y == (GLfloat)(-((h - 0.5) / 2.0)) &&
               m.trans_y == (GLfloat)((h - 0.5) / 2.0),
               "%s: scale and translation are gl_eval_viewport()'s own, including scale.Y negative",
               label);
        checkf(m.ok == 1u && m.blended == 1u,
               "%s: a captured map says so (ok), and GL_BLEND on reads as ABE set (blended=%u)",
               label, (unsigned)m.blended);
        checkf(m.vp_x0 == 0 && m.vp_x1 == (int32_t)w - 1 &&
               m.vp_y0 == 0 && m.vp_y1 == (int32_t)h - 1,
               "%s: the clip rect is the viewport's own pixels, inclusive at both edges (%d..%d x"
               " %d..%d)", label, (int)m.vp_x0, (int)m.vp_x1, (int)m.vp_y0, (int)m.vp_y1);
    }

    /* ── the boundary probes, appended to the lattice with the constants just lifted ── */
    s_n = sweep_pixel_edges(&m);
    printf("    %s: %d lattice + %d pixel-edge probes appended\n", label, s_edge0, s_n - s_edge0);

    /* ── the VALUES, at every alpha ── */
    for (a = 0; a < n_alphas; a++) {
        long b_px = 0, b_col = 0, b_skip = 0;
        uint32_t lo = 255u, hi = 0u;
        int edges = 0;
        char tag[32];

        glColor4f(0.5f, 0.5f, 0.5f, alphas[a]);   /* the alpha the batch must lift */
        if (tgl_grid_map(&m) != 0) {
            checkf(0, "%s: alpha %.7f -- the map was refused, so its values cannot be compared",
                   label, alphas[a]);
            continue;
        }
        tgl_grid_map_points(&m, s_xs, s_ys, s_bri, s_n, s_pts);

        snprintf(tag, sizeof tag, "a=%.6g", (double)alphas[a]);
        compare_mesh(alphas[a], s_pts, tag, &b_px, &b_col, &b_skip, a == 0 ? 1 : 0,
                     a == 0 ? &lo : NULL, a == 0 ? &hi : NULL, a == 0 ? &edges : NULL);

        if (a == 0) {
            int straddled = 0, edges_tot = 0, n;

            /* Each edge contributes EDGE_PROBES probes; the y-block starts after the x-block's own
             * recorded length, so a probe the window bounds dropped is not counted as an edge that
             * agreed.  If the outer two probes give the same pixel, the sweep never crossed that
             * transition and the boundary claim is decoration -- so the crossing is measured. */
            for (n = 0; n + EDGE_PROBES <= s_xedge_n; n += EDGE_PROBES, edges_tot++)
                if (s_pts[s_edge0 + n].x_px != s_pts[s_edge0 + n + EDGE_PROBES - 1].x_px) straddled++;
            for (n = 0; n + EDGE_PROBES <= s_yedge_n; n += EDGE_PROBES, edges_tot++)
                if (s_pts[s_edge0 + s_xedge_n + n].y_px !=
                    s_pts[s_edge0 + s_xedge_n + n + EDGE_PROBES - 1].y_px) straddled++;
            checkf(straddled * 2 > edges_tot,
                   "%s: %d of %d pixel-edge probes cross the truncate between 8 float ulp below the"
                   " root and 8 above", label, straddled, edges_tot);

            printf("    %s: %d points, %d distinct pixels, channel bytes %u..%u\n",
                   label, s_n, edges, (unsigned)lo, (unsigned)hi);
            checkf(edges > w / 2 && edges > h / 2,
                   "%s: the sweep crosses more than half the window's pixel edges on both axes (%d)",
                   label, edges);
            checkf(lo == 0u && hi >= 253u,
                   "%s: the sweep's intensities reach both ends of the channel byte (%u..%u)",
                   label, (unsigned)lo, (unsigned)hi);
        }
        compared += (long)s_n - b_skip;
        checkf(b_skip == 0, "%s: every swept point was inside the clip volume (%ld clipped)",
               label, b_skip);
        checkf(b_px == 0 && b_col == 0,
               "%s: batch == walk on both pixels and all four bytes at alpha %.7f (%d points, %ld+%ld"
               " differ)", label, alphas[a], s_n, b_px, b_col);
        bad_px += b_px; bad_col += b_col; skipped += b_skip;
    }

    checkf(compared > 30000,
           "%s: %ld vertices went through the real walk -- the sweep is not vacuous",
           label, compared);

    /* ── PERTURBATION control ── a gate that cannot fail is not an instrument.  Each field is the
     * only thing the mutation touches, and the map is re-captured at the alpha compare_mesh uses,
     * so a reported mismatch is the mutation's and not a state difference. ── */
    glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
    if (tgl_grid_map(&base) != 0) {
        checkf(0, "%s: the perturbation control could not capture a base map", label);
    } else {
        static const char *what[] = { "mx", "xc", "my", "yc", "scale_x", "trans_x", "winv", "a_byte" };
        int p;

        for (p = 0; p < 8; p++) {
            tgl_grid_map_t bad = base;
            long q_px = 0, q_col = 0, q_skip = 0;

            switch (p) {
            case 0: bad.mx      *= 1.01f; break;
            case 1: bad.xc      += 0.5f;  break;
            case 2: bad.my      *= 1.01f; break;
            case 3: bad.yc      += 0.5f;  break;
            case 4: bad.scale_x *= 1.01f; break;
            case 5: bad.trans_x += 1.0f;  break;
            case 6: bad.winv    *= 1.25f; break;
            default: bad.a_byte ^= 0x40u; break;
            }
            if (memcmp(&bad, &base, sizeof bad) == 0) {
                checkf(0, "%s: perturbation of %s changed nothing to perturb", label, what[p]);
                continue;
            }
            tgl_grid_map_points(&bad, s_xs, s_ys, s_bri, s_n, s_pts);
            compare_mesh(1.0f, s_pts, "perturbed", &q_px, &q_col, &q_skip, 0, NULL, NULL, NULL);
            checkf(q_px + q_col > 0,
                   "%s: nudging %s alone moves %ld pixels and %ld colours -- the comparison reads"
                   " that field", label, what[p], q_px, q_col);
        }
        /* Put the mesh back the way the passing checks saw it, so the log's last word is the gate's
         * answer and not the control's. */
        tgl_grid_map_points(&base, s_xs, s_ys, s_bri, s_n, s_pts);
    }

    /* A zeroed struct of this shape is a VALID-LOOKING map -- mx 0, winv 0, every point truncating
     * to one corner -- which is why `ok` exists: map_points must write nothing through it. */
    {
        tgl_grid_map_t zero;
        long kept = 1;

        memset(&zero, 0, sizeof zero);
        for (i = 0; i < s_n; i++) {
            s_pts[i].x_px = 777777; s_pts[i].y_px = -777777; s_pts[i].rgba = 0xDEADBEEFu;
        }
        tgl_grid_map_points(&zero, s_xs, s_ys, s_bri, s_n, s_pts);
        for (i = 0; i < s_n; i++)
            if (s_pts[i].x_px != 777777 || s_pts[i].y_px != -777777 || s_pts[i].rgba != 0xDEADBEEFu)
                kept = 0;
        checkf(kept, "%s: a map with ok unset writes no points at all", label);
    }

    /* ── rung (ii): the clip gate, on probes the VALUES sweep had to leave out ──
     * Appended last, because a point outside the window cannot go through the comparison above: that
     * one asserts the walk mapped everything it was given, and these are the points it refuses to. */
    glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
    if (tgl_grid_map(&m) != 0) {
        checkf(0, "%s: the clip gate could not capture a map", label);
    } else {
        s_gate0 = s_n;
        s_n = sweep_outside(w, h);
        tgl_grid_map_points(&m, s_xs, s_ys, s_bri, s_n, s_pts);
        compare_clip_gate(&m, label);
        s_n = s_gate0;                     /* the lattice keeps its size for the next window */
    }

    printf("    %s totals: %ld pixel mismatches, %ld colour mismatches, %ld clipped\n",
           label, bad_px, bad_col, skipped);
}

/* ── the refusals, each with the same state restored and accepted ─────────────────────── */

/* Apply a state the two-constant form cannot represent, flush the product so freshness cannot be
 * the reason, expect -1; undo it, flush again, expect 0.  Without the second half a -1 would only
 * say the context was unhappy about something.
 *
 * `want` is the refusal's own code, and it is asserted because the bare -1 is the instrument's whole
 * weakness: the [GSTS] row's `slow=` says a mesh fell back and cannot say why, and this suite is the
 * only place the seven conditions are each separately provable.  A code that is right for the wrong
 * reason would still refuse, so the accepted half is checked for `refuse == 0` as well. */
static void refusal(const char *why, uint32_t want, void (*apply)(void), void (*undo)(void))
{
    tgl_grid_map_t m;
    int refused, accepted;
    uint32_t got;

    memset(&m, 0, sizeof m);
    apply();
    flush_matrices();
    refused = (tgl_grid_map(&m) == -1);
    got = m.refuse;
    undo();
    flush_matrices();
    memset(&m, 0, sizeof m);
    accepted = (tgl_grid_map(&m) == 0 && m.refuse == TGL_GRID_REFUSE_NONE);

    checkf(refused && accepted,
           "%s: refused (%d), and accepted again once the state is undone (%d)",
           why, refused, accepted);
    checkf(got == want,
           "%s: and named the condition that refused it (why=%u, want %u)",
           why, (unsigned)got, (unsigned)want);
}

static void on_lighting(void)  { glEnable(GL_LIGHTING); }
static void off_lighting(void) { glDisable(GL_LIGHTING); }
static void on_texture(void)   { glEnable(GL_TEXTURE_2D); }
static void off_texture(void)  { glDisable(GL_TEXTURE_2D); }

/* A rotation about Z puts sin(theta) in m[1] and -sin(theta) in m[4]: the two couplings the batch
 * drops.  A rotation about Y would not -- its coupling reaches into Z, and a `glVertex2f` point's Z
 * is an exact zero, which is exactly why the check is on m[1] and m[4] rather than on the matrix. */
static void off_diagonal(void) { glMatrixMode(GL_MODELVIEW); glRotatef(30.0f, 0.0f, 0.0f, 1.0f); }
static void on_diagonal(void)  { glMatrixMode(GL_MODELVIEW); glLoadIdentity(); }

/* A perspective projection makes W depend on the vertex, which is what
 * `matrix_model_projection_no_w_transform` records at glBegin.  Refusing it is not caution: with a
 * varying W the constant `winv` this map divides by is simply the wrong number. */
static void w_varies(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
}
static void w_constant(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)s_w, (double)s_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
}

static void run_refusals(void)
{
    refusal("a lit mesh takes its colour from gl_shade_vertex(), not the current colour",
            TGL_GRID_REFUSE_LIGHT, on_lighting, off_lighting);
    refusal("a textured mesh carries zp.s/zp.t that this map does not compute",
            TGL_GRID_REFUSE_TEX, on_texture, off_texture);
    refusal("a projection that couples X to Y is not two constants an axis",
            TGL_GRID_REFUSE_SKEW, off_diagonal, on_diagonal);
    refusal("a W that depends on the vertex is not a constant reciprocal",
            TGL_GRID_REFUSE_W, w_varies, w_constant);

    /* The map's other two fields follow the state they name, or gate (b) would be building its PRIM
     * word out of an invention.  Zeroed before each call, because a refusal writes every field but
     * `refuse` of `out`, and an assertion about a field that was never written is not an assertion. */
    {
        static tgl_grid_map_t m;

        glShadeModel(GL_FLAT);
        flush_matrices();
        checkf(tgl_grid_map(&m) == 0 && m.smooth == 0u,
               "GL_FLAT reads as IIP clear (smooth=%u)", (unsigned)m.smooth);
        glShadeModel(GL_SMOOTH);
        flush_matrices();
        checkf(tgl_grid_map(&m) == 0 && m.smooth == 1u,
               "GL_SMOOTH reads as IIP set (smooth=%u)", (unsigned)m.smooth);

        glDisable(GL_BLEND);
        flush_matrices();
        checkf(tgl_grid_map(&m) == 0 && m.blended == 0u,
               "GL_BLEND off reads as ABE clear (blended=%u)", (unsigned)m.blended);
        glEnable(GL_BLEND);
        flush_matrices();
        checkf(tgl_grid_map(&m) == 0 && m.blended == 1u,
               "GL_BLEND on reads as ABE set (blended=%u)", (unsigned)m.blended);
    }
}

/* ── LANE 12h rung (i): the textured map, against the walk's texel chain ────────────────
 *
 * The grey gate above proves the batch's position arithmetic; the menu's quads add a texel
 * coordinate the grey map cannot say, and the walk arrives at the ST register's float by TWO steps:
 * `zp.s = TGL_TRUNC(u*sscale + soff)` in `tgl_vertex_to_viewport()`'s own line, and then
 * `gs_float_scale_pow2((float)zp.s, ZB_POINT_S_FRAC_BITS + 1)` in `clip.c`'s `gs_vertex()`.
 * `tgl_grid_map_tex_points()` is the third encoding of that chain, so the comparator here is again
 * the walk itself, never a restatement of it: a real 96x64 RGBA sheet uploaded through the shipped
 * `glTexImage2D` -- a NON power-of-two extent, so the plane is padded to 128x64 and the mapper
 * constants are texture.c's own, the ones whose products are inexact for every `u` that is not a
 * texel corner -- and every swept vertex run through `glTexCoord2f` + `glColor4f` + `glVertex2f` on
 * the live context.  Five things must agree BIT FOR BIT per vertex: the two device pixels, all four
 * colour bytes (per-channel now -- an icon quad is `(br,br,br,a)` and its shadow `(0,0,0,a)`, the
 * general case of the grey path's one replicated byte), and the two ST floats.
 *
 * The texel-edge probes are the precision control, for the reason the pixel-edge probes carry above:
 * a quarter-texel lattice cannot see a chain carried in the wrong precision, only a point ON a
 * truncate's transition can.  The roots are solved in double from the upload's own constants at each
 * texel boundary (`fp = n * 2^shift`) and probed at ±8 float ulp, the same instrument the position
 * sweep uses turned onto `u`/`v`.
 *
 * The refusal half INVERTS 12f's texture clause: this map REQUIRES the sheet and refuses without it
 * -- flag off, nothing bound, or bound-but-never-uploaded, which is the state `clip.c` folds back to
 * a FLAT draw and a batch must not claim a plane for.  Each is paired with the same state restored
 * and accepted, and the four 12f refusals are re-asked here with the sheet live so no -1 can be
 * earned by something else the context lacks.  The grey map's own `why=4` is asserted in the same
 * state: the two maps are exact complements on TEXTURE_2D, which is what keeps their conditions
 * from drifting apart in silence.
 */
#define TEX_SHEET_W 96
#define TEX_SHEET_H 64

static float *s_us, *s_vs, *s_cols;      /* s_cols: four interleaved floats a vertex */
static tgl_tex_pt_t *s_tpts;
static GLuint s_tex, s_tex_blank;

static void add_tp(int *i, float x, float y, float u, float v, int bidx)
{
    if (*i >= SWEEP_MAX) return;
    s_xs[*i] = x;
    s_ys[*i] = y;
    s_us[*i] = u;
    s_vs[*i] = v;
    /* Four independent channels, each from its own offset into the four families of `bri_at`: the
     * batch maps r/g/b/a separately where the grey path replicated one byte, so every byte needs
     * its own flip-point coverage. */
    s_cols[*i * 4 + 0] = bri_at(bidx);
    s_cols[*i * 4 + 1] = bri_at(bidx + 307);
    s_cols[*i * 4 + 2] = bri_at(bidx + 613);
    s_cols[*i * 4 + 3] = bri_at(bidx + 919);
    (*i)++;
}

/* Quarter-texel lattices on both axes of the sheet, over [0,1). */
static float tex_u(int k) { return (float)(k % (TEX_SHEET_W * 4)) / (float)(TEX_SHEET_W * 4); }
static float tex_v(int k) { return (float)(k % (TEX_SHEET_H * 4)) / (float)(TEX_SHEET_H * 4); }

/* Positions: the same banded quarter-pixel lattice as `sweep_bands()`, so the position half of the
 * textured map is swept over every pixel edge of the real canvas; texel coordinates cycle through
 * their own quarter-texel lattices with unrelated strides, so no point is on a texel corner by
 * coincidence of the position index. */
static int sweep_tex(void)
{
    static const float ys[4] = { 0.0f, 239.75f, 240.0f, 479.75f };
    static const float xs[3] = { 0.0f, 319.75f, 639.75f };
    int i = 0, r, c, b;
    float v;

    for (r = 0; r < 4; r++)
        for (v = 0.0f, b = 0; v < 639.8f; v += 0.25f, b++)
            add_tp(&i, v, ys[r], tex_u(b + r * 97), tex_v(b * 3 + r * 41), b + r * 31);
    for (c = 0; c < 3; c++)
        for (v = 0.0f, b = 0; v < 479.8f; v += 0.25f, b++)
            add_tp(&i, xs[c], v, tex_u(b * 5 + c * 71), tex_v(b + c * 17), b + c * 17 + 401);
    return i;
}

/* The float u whose fixed point is exactly the texel boundary `n << shift`: the chain is monotone
 * in u, so this is its inverse, solved in double and probed as a float at ±8 ulp with the same
 * offsets the pixel-edge probes use and for the same arithmetic reason (see that comment).  The
 * first and last boundaries stay out: their roots are the sheet's own edges, where a coordinate at
 * 1.0 wraps by masking and the comparison would be about the wrap, not the truncate. */
static int s_texedge0, s_uedge_n, s_vedge_n;

static void add_tex_edge_probes(int *i, float scale, float coff, int shift, int n_tex,
                                int vertical, int bidx)
{
    int n, k;

    for (n = 1; n < n_tex - 1; n++) {
        double root = ((double)n * (double)(1 << shift) - (double)coff) / (double)scale;
        float q = (float)root;

        for (k = 0; k < EDGE_PROBES; k++) {
            float v = q;
            int o = k_edge_off[k], steps = o < 0 ? -o : o;

            for (; steps > 0; steps--) v = nextafterf(v, o < 0 ? 0.0f : 1.0f / 0.0f);
            if (!(v >= 0.0f && v < 1.0f)) continue;
            /* Positions sit inside the window, away from every pixel edge: these probes are here
             * for the texel truncate, and the position sweep above owns the pixel one. */
            if (vertical) add_tp(i, 319.75f, 239.5f, 0.5f, v, bidx + n * EDGE_PROBES + k);
            else          add_tp(i, 319.75f, 239.5f, v, 0.5f, bidx + n * EDGE_PROBES + k);
        }
    }
}

static uint32_t tex_bits(float v)
{
    uint32_t b;
    memcpy(&b, &v, sizeof b);
    return b;
}

/* Feed the same mesh through the real walk -- texel coordinates included -- and hold its answers
 * against the batch's on all five fields. */
static void compare_tex_mesh(const tgl_tex_pt_t *pts, long *bad_px, long *bad_col, long *bad_st,
                             long *skipped, uint32_t *lo_out, uint32_t *hi_out)
{
    GLContext *c = gl_get_context();
    int i;
    uint32_t lo = 255u, hi = 0u;

    glBegin(GL_POINTS);
    for (i = 0; i < s_n; i++) {
        const GLVertex *v;
        uint32_t by;
        float want_s, want_t;

        glTexCoord2f(s_us[i], s_vs[i]);
        glColor4f(s_cols[i * 4 + 0], s_cols[i * 4 + 1], s_cols[i * 4 + 2], s_cols[i * 4 + 3]);
        glVertex2f(s_xs[i], s_ys[i]);

        v = &c->vertex[0];
        if (v->clip_code != 0) { (*skipped)++; continue; }

        by = pts[i].rgba & 0xFFu;
        if (by < lo) lo = by;
        if (by > hi) hi = by;

        if (v->zp.x != pts[i].x_px || v->zp.y != pts[i].y_px) {
            (*bad_px)++;
            if (*bad_px <= 4)
                printf("      tex pixel (%9.4f,%9.4f): walk (%d,%d) batch (%d,%d)\n",
                       s_xs[i], s_ys[i], v->zp.x, v->zp.y, pts[i].x_px, pts[i].y_px);
        }
        if (walk_byte(v->zp.r) != (pts[i].rgba & 0xFFu) ||
            walk_byte(v->zp.g) != ((pts[i].rgba >> 8) & 0xFFu) ||
            walk_byte(v->zp.b) != ((pts[i].rgba >> 16) & 0xFFu) ||
            walk_byte(v->zp.a) != ((pts[i].rgba >> 24) & 0xFFu)) {
            (*bad_col)++;
            if (*bad_col <= 4)
                printf("      tex colour (%9.4f,%9.4f) c=%.7f,%.7f,%.7f,%.7f: walk %02x %02x %02x"
                       " %02x batch %02x %02x %02x %02x\n",
                       s_xs[i], s_ys[i], s_cols[i * 4], s_cols[i * 4 + 1], s_cols[i * 4 + 2],
                       s_cols[i * 4 + 3],
                       (unsigned)walk_byte(v->zp.r), (unsigned)walk_byte(v->zp.g),
                       (unsigned)walk_byte(v->zp.b), (unsigned)walk_byte(v->zp.a),
                       (unsigned)(pts[i].rgba & 0xFFu), (unsigned)((pts[i].rgba >> 8) & 0xFFu),
                       (unsigned)((pts[i].rgba >> 16) & 0xFFu),
                       (unsigned)((pts[i].rgba >> 24) & 0xFFu));
        }
        /* The comparator's texel answer is the walk's own two steps, read off the vertex record the
         * shipped `tgl_vertex_to_viewport()` wrote: `zp.s` for the truncate, and the ST scale
         * spelled with the same constant `clip.c` uses because this suite links neither `clip.c`'s
         * private define nor a second idea of what the shift is. */
        want_s = gs_float_scale_pow2((float)v->zp.s, ZB_POINT_S_FRAC_BITS + 1);
        want_t = gs_float_scale_pow2((float)v->zp.t, ZB_POINT_T_FRAC_BITS + 1);
        if (memcmp(&want_s, &pts[i].s, sizeof want_s) != 0 ||
            memcmp(&want_t, &pts[i].t, sizeof want_t) != 0) {
            (*bad_st)++;
            if (*bad_st <= 4)
                printf("      tex texel (%9.4f,%9.4f) u=%.8g v=%.8g: walk s=%08x t=%08x"
                       " batch s=%08x t=%08x\n",
                       s_xs[i], s_ys[i], (double)s_us[i], (double)s_vs[i],
                       tex_bits(want_s), tex_bits(want_t), tex_bits(pts[i].s), tex_bits(pts[i].t));
        }
    }
    glEnd();
    if (lo_out) *lo_out = lo;
    if (hi_out) *hi_out = hi;
}

/* The textured pair's own refusal helper: same paired shape as `refusal()`, asking
 * `tgl_grid_map_tex()`. */
static void refusal_tex(const char *why, uint32_t want, void (*apply)(void), void (*undo)(void))
{
    tgl_tex_map_t m;
    int refused, accepted;
    uint32_t got;

    memset(&m, 0, sizeof m);
    apply();
    flush_matrices();
    refused = (tgl_grid_map_tex(&m) == -1);
    got = m.g.refuse;
    undo();
    flush_matrices();
    memset(&m, 0, sizeof m);
    accepted = (tgl_grid_map_tex(&m) == 0 && m.g.refuse == TGL_GRID_REFUSE_NONE);

    checkf(refused && accepted,
           "%s: refused (%d), and accepted again once the state is undone (%d)",
           why, refused, accepted);
    checkf(got == want, "%s: and named the condition that refused it (why=%u, want %u)",
           why, (unsigned)got, (unsigned)want);
}

static void tex_off(void)    { glDisable(GL_TEXTURE_2D); }
static void tex_on(void)     { glEnable(GL_TEXTURE_2D); }
static void tex_unbind(void) { glBindTexture(GL_TEXTURE_2D, 0); }
static void tex_rebind(void) { glBindTexture(GL_TEXTURE_2D, s_tex); }
static void tex_blank(void)  { glBindTexture(GL_TEXTURE_2D, s_tex_blank); }
static void tex_real(void)   { glBindTexture(GL_TEXTURE_2D, s_tex); }

static void run_tex(void)
{
    tgl_tex_map_t m, base;
    tgl_grid_map_t g;
    static unsigned char pixels[TEX_SHEET_W * TEX_SHEET_H * 4];
    const GLImage *im;
    long bad_px = 0, bad_col = 0, bad_st = 0, skipped = 0, compared = 0;
    uint32_t lo = 255u, hi = 0u;
    int i, p, r;

    tinygl_backend_shutdown();
    gl_init(GL_BACKEND_TINYGL, 640, 480, s_fb);
    s_w = 640; s_h = 480;
    app_state();                         /* leaves TEXTURE_2D off and nothing bound */

    for (i = 0; i < TEX_SHEET_W * TEX_SHEET_H; i++) {
        pixels[i * 4 + 0] = (unsigned char)(i * 7);
        pixels[i * 4 + 1] = (unsigned char)(i * 13);
        pixels[i * 4 + 2] = (unsigned char)(i);
        pixels[i * 4 + 3] = (unsigned char)(255 - (i & 0xFF));
    }
    glGenTextures(1, &s_tex);
    glBindTexture(GL_TEXTURE_2D, s_tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TEX_SHEET_W, TEX_SHEET_H, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, pixels);
    glEnable(GL_TEXTURE_2D);
    flush_matrices();

    checkf(tgl_grid_map_tex(NULL) == -1,
           "tex: tgl_grid_map_tex(NULL) is refused rather than dereferenced");
    memset(&m, 0, sizeof m);
    checkf(tgl_grid_map_tex(&m) == 0 && m.g.refuse == TGL_GRID_REFUSE_NONE && m.g.ok == 1u,
           "tex: a bound-and-uploaded sheet with TEXTURE_2D on is captured (why=%u)",
           (unsigned)m.g.refuse);

    /* The sheet is the app's own route to a plane: uploaded at 96x64, allocated at 128x64, and the
     * mapper constants are texture.c's formula at the UPLOADED extent -- not the padded one.  A map
     * that lifted the padded extent instead would put every texel 1/384 of the sheet off, which is
     * exactly the wrongness a lattice sweep exists to catch. */
    im = &gl_ctx.current_texture->images[0];
    checkf(im->xsize == TEX_SHEET_W && im->ysize == TEX_SHEET_H &&
           im->xsize_pad == 128 && im->ysize_pad == TEX_SHEET_H && m.pixmap == im->pixmap,
           "tex: plane is the upload's 96x64 extent padded to %dx%d, and the map's key is that"
           " plane's pointer", (int)im->xsize_pad, (int)im->ysize_pad);
    {
        const GLint sf = ZB_POINT_S_FRAC_BITS + 1, tf = ZB_POINT_T_FRAC_BITS + 1;

        checkf(m.soff == (GLfloat)(1 << ZB_POINT_S_FRAC_BITS) &&
               m.sscale == (GLfloat)(((GLint)TEX_SHEET_W << sf) - (1 << ZB_POINT_S_FRAC_BITS)) &&
               m.toff == (GLfloat)(1 << ZB_POINT_T_FRAC_BITS) &&
               m.tscale == (GLfloat)(((GLint)TEX_SHEET_H << tf) - (1 << ZB_POINT_T_FRAC_BITS)),
               "tex: mapper constants are texture.c's own at the uploaded extent (sscale=%.0f"
               " soff=%.0f tscale=%.0f toff=%.0f)",
               (double)m.sscale, (double)m.soff, (double)m.tscale, (double)m.toff);
    }

    /* The complement clause, asserted in the same state the sweep then runs in. */
    memset(&g, 0, sizeof g);
    checkf(tgl_grid_map(&g) == -1 && g.refuse == TGL_GRID_REFUSE_TEX,
           "tex: the GREY map refuses this same state by name (why=%u) -- the two maps are exact"
           " complements on TEXTURE_2D", (unsigned)g.refuse);

    /* ── the VALUES ── */
    s_n = sweep_tex();
    r = s_n;
    add_tex_edge_probes(&s_n, m.sscale, m.soff, ZB_POINT_S_FRAC_BITS + 1, TEX_SHEET_W, 0, 5);
    s_uedge_n = s_n - r;
    s_texedge0 = r;
    add_tex_edge_probes(&s_n, m.tscale, m.toff, ZB_POINT_T_FRAC_BITS + 1, TEX_SHEET_H, 1, 11);
    s_vedge_n = s_n - r - s_uedge_n;
    printf("    tex: %d lattice points + %d u-edge + %d v-edge probes\n",
           s_texedge0, s_uedge_n, s_vedge_n);

    tgl_grid_map_tex_points(&m, s_xs, s_ys, s_us, s_vs, s_cols, s_n, s_tpts);
    compare_tex_mesh(s_tpts, &bad_px, &bad_col, &bad_st, &skipped, &lo, &hi);
    compared += s_n - skipped;
    checkf(skipped == 0, "tex: every swept point was inside the clip volume (%ld clipped)", skipped);
    checkf(bad_px == 0 && bad_col == 0 && bad_st == 0,
           "tex: batch == walk on pixels, all four bytes AND both ST floats at every point"
           " (%d points, %ld+%ld+%ld differ)", s_n, bad_px, bad_col, bad_st);
    checkf(compared > 15000,
           "tex: %ld vertices went through the real walk with a texture live -- the sweep is not"
           " vacuous", compared);
    checkf(lo <= 2u && hi >= 250u,
           "tex: the sweep's red bytes reach both ends of the channel byte (%u..%u)",
           (unsigned)lo, (unsigned)hi);

    /* Each texel edge contributes EDGE_PROBES probes; a probe dropped by the sheet bounds is not
     * counted.  If the outer two agree, that edge was never crossed and the precision claim is
     * decoration -- so the crossing is measured, the way the pixel sweep measures its own. */
    {
        int straddled = 0, tot = 0, n;

        for (n = 0; n + EDGE_PROBES <= s_uedge_n; n += EDGE_PROBES, tot++)
            if (memcmp(&s_tpts[s_texedge0 + n].s, &s_tpts[s_texedge0 + n + EDGE_PROBES - 1].s,
                       sizeof(float)) != 0) straddled++;
        for (n = 0; n + EDGE_PROBES <= s_vedge_n; n += EDGE_PROBES, tot++)
            if (memcmp(&s_tpts[s_texedge0 + s_uedge_n + n].t,
                       &s_tpts[s_texedge0 + s_uedge_n + n + EDGE_PROBES - 1].t,
                       sizeof(float)) != 0) straddled++;
        checkf(straddled * 2 > tot,
               "tex: %d of %d texel-edge probes cross the fixed-point truncate between 8 float ulp"
               " below the root and 8 above", straddled, tot);
    }

    /* ── PERTURBATION ── the four lifted mapper constants and two position fields, each alone.
     * Every nudge must move something the comparison reads; a field it leaves untouched is a field
     * the sweep cannot see, which is the failure this control exists to make impossible. ── */
    base = m;
    {
        static const char *what[] = { "sscale", "soff", "tscale", "toff", "g.mx", "g.winv" };

        for (p = 0; p < 6; p++) {
            tgl_tex_map_t bad = base;
            long q_px = 0, q_col = 0, q_st = 0, q_skip = 0;

            switch (p) {
            case 0: bad.sscale  *= 1.01f; break;
            case 1: bad.soff    += 1.0f;  break;
            case 2: bad.tscale  *= 1.01f; break;
            case 3: bad.toff    += 1.0f;  break;
            case 4: bad.g.mx    *= 1.01f; break;
            default: bad.g.winv *= 1.25f; break;
            }
            if (memcmp(&bad, &base, sizeof bad) == 0) {
                checkf(0, "tex: perturbation of %s changed nothing to perturb", what[p]);
                continue;
            }
            tgl_grid_map_tex_points(&bad, s_xs, s_ys, s_us, s_vs, s_cols, s_n, s_tpts);
            compare_tex_mesh(s_tpts, &q_px, &q_col, &q_st, &q_skip, NULL, NULL);
            checkf(q_px + q_col + q_st > 0,
                   "tex: nudging %s alone moves %ld pixels, %ld colours and %ld texels -- the"
                   " comparison reads that field", what[p], q_px, q_col, q_st);
        }
        tgl_grid_map_tex_points(&base, s_xs, s_ys, s_us, s_vs, s_cols, s_n, s_tpts);
    }

    /* The zeroed-struct contract, for the new pair as for the old one. */
    {
        tgl_tex_map_t zero;
        long kept = 1;

        memset(&zero, 0, sizeof zero);
        for (i = 0; i < s_n; i++) {
            s_tpts[i].x_px = 777777; s_tpts[i].y_px = -777777;
            s_tpts[i].s = 12345.0f;  s_tpts[i].t = -54321.0f;
            s_tpts[i].rgba = 0xDEADBEEFu;
        }
        tgl_grid_map_tex_points(&zero, s_xs, s_ys, s_us, s_vs, s_cols, s_n, s_tpts);
        for (i = 0; i < s_n; i++)
            if (s_tpts[i].x_px != 777777 || s_tpts[i].y_px != -777777 ||
                s_tpts[i].s != 12345.0f || s_tpts[i].t != -54321.0f ||
                s_tpts[i].rgba != 0xDEADBEEFu)
                kept = 0;
        checkf(kept, "tex: a map with ok unset writes no points at all");
    }

    /* ── the refusals, sheet live ── the three NOTEX shapes first (flag, bind, upload), then 12f's
     * own conditions re-asked with all three satisfied, then freshness. ── */
    refusal_tex("TEXTURE_2D off: the texel coordinate this map exists to convert is not written",
                TGL_GRID_REFUSE_NOTEX, tex_off, tex_on);
    refusal_tex("nothing bound: there is no sheet, no mapper constant and no plane",
                TGL_GRID_REFUSE_NOTEX, tex_unbind, tex_rebind);
    glGenTextures(1, &s_tex_blank);
    refusal_tex("bound but never uploaded: the walk folds that state to a FLAT draw, and a batch"
                " that took it would claim a plane the hardware never got",
                TGL_GRID_REFUSE_NOTEX, tex_blank, tex_real);
    refusal_tex("a lit mesh takes its colour from gl_shade_vertex(), not the current colour",
                TGL_GRID_REFUSE_LIGHT, on_lighting, off_lighting);
    refusal_tex("a projection that couples X to Y is not two constants an axis",
                TGL_GRID_REFUSE_SKEW, off_diagonal, on_diagonal);
    refusal_tex("a W that depends on the vertex is not a constant reciprocal",
                TGL_GRID_REFUSE_W, w_varies, w_constant);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, (double)s_w, (double)s_h, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    {
        const GLfloat *pm = &gl_ctx.matrix_model_projection.m[0][0];

        memset(&m, 0, sizeof m);
        m.g.refuse = 0xFFFFFFFFu;
        checkf(tgl_grid_map_tex(&m) == -1 && m.g.refuse == TGL_GRID_REFUSE_STALE,
               "tex: refused after a matrix change, before the next glBegin, by name (why=%u)",
               (unsigned)m.g.refuse);
        checkf(gl_ctx.matrix_model_projection_updated != 0 &&
               gl_ctx.matrix_model_projection_no_w_transform != 0 &&
               pm[1] == 0.0f && pm[4] == 0.0f &&
               gl_ctx.current_texture != NULL && gl_ctx.current_texture->images[0].pixmap != NULL,
               "tex: ...while the sheet is live and that product is still diagonal with a constant"
               " W, so freshness is the only thing that could refuse it (updated=%d)",
               (int)gl_ctx.matrix_model_projection_updated);
    }
    flush_matrices();
    memset(&m, 0, sizeof m);
    checkf(tgl_grid_map_tex(&m) == 0 && m.g.refuse == TGL_GRID_REFUSE_NONE,
           "tex: accepted again once glBegin has recomputed the product -- the refusal was the"
           " flag, not the state");

    printf("    tex totals: %ld pixel, %ld colour, %ld texel mismatches over %ld compared\n",
           bad_px, bad_col, bad_st, compared);

    glDisable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

/* ── the ribbon's own mesh, at the console's own two sizes ─────────────────────────────────
 *
 * The first live run of LANE 12f's batch path answered `batch=0 slow=1 why=7` on every frame of
 * the bench, which is the rect gate refusing the one mesh the lane was written for.  This section
 * says what leaves the rect, in the mesh's own numbers, and separates the two candidate causes: the
 * sheet could be waving outside the window, in which case the walk's clipper has been cutting it all
 * along and the overhang is the picture; or the two sizes the app uses could be different, in which
 * case the mesh is bigger than its canvas by a fixed, structural amount and the question becomes what
 * the GS does with the overhang.  It is the second, which is why the gate that refused on the rect is
 * now a gate on the vertex word's range and these 32 points are a count (`over=`) instead of a
 * refusal.
 *
 * The two sizes are real.  `xb_paint()` steps the wave with `s_w`/`s_h`, which `open_xmb_window()`
 * sets from the WINDOW (desk 800x600 -> 776x536 at xmb.c:3312-3316, 3338), and then puts the
 * camera on `dev->width`/`dev->height`, which is the CLIENT area the surface was made at -- the
 * shipped frame's own `[GSTS]` row prints that as `clip=768x500`, and prints the window's rect as
 * `dmg=12,20,788,556`, i.e. exactly 776x536 at (12,20).  So the sheet is projected onto a rectangle
 * 8 px wider and 36 px taller than the one it is drawn into.  The background quad does that on
 * purpose (`xb_draw_bg()` puts its corners at `(0,s_h)` and `(s_w,0)`, overdraw so no canvas edge
 * can show through); the ribbon inherits it, and the clipper has been cutting its right-hand column
 * ever since.
 *
 * What follows measures that claim rather than restating it: `xb_wave_step()` is the shipped pass,
 * `tgl_grid_map()`/`tgl_grid_map_points()` are the shipped batch's own map, and the rect the points
 * are compared against is the shipped `vp_x0..vp_y1` out of the captured map.  The sweep runs the
 * wave over the period of its slowest term (t/10 and -7t/100 both come back inside 200), so the
 * extents are the sheet's, not one frame's.
 */
#define RIB_T_SPAN 200.0f
#define RIB_T_STEP 0.1f

static unsigned char s_fb_canvas[768 * 500 * 4];

static uint32_t rib_bits(float v)
{
    uint32_t b;
    memcpy(&b, &v, sizeof b);
    return b;
}

typedef struct {
    int frames;
    int bad_map;          /* frames whose map was refused: the arrangement is then unmeasured */
    int out_pts;          /* worst over the sweep: points outside the captured rect */
    int out_cols;         /* distinct columns they occupy */
    int out_rows;         /* distinct rows */
    int over_x1, under_x0, over_y1, under_y0;
    int min_x, max_x, min_y, max_y;
    int offenders_rows;   /* frames with at least one point outside */
} rib_scan_t;

static void ribbon_scan(const char *label, int canvas_w, int canvas_h,
                        int proj_w, int proj_h, rib_scan_t *a)
{
    tgl_grid_map_t m;
    const int n = XB_RIBBON_ROWS * XB_RIBBON_COLS;
    unsigned char col_out[XB_RIBBON_COLS], row_out[XB_RIBBON_ROWS];
    int r, c;
    float t;

    memset(a, 0, sizeof *a);
    a->min_x = a->min_y = 1 << 30;
    a->max_x = a->max_y = -(1 << 30);
    memset(col_out, 0, sizeof col_out);
    memset(row_out, 0, sizeof row_out);

    tinygl_backend_shutdown();
    gl_init(GL_BACKEND_TINYGL, canvas_w, canvas_h, s_fb_canvas);
    s_w = canvas_w; s_h = canvas_h;
    app_state();

    /* The call site's order, `glBegin` included: the product the map reads is the one the walk
     * computes there, and the current colour is the one the batch lifts the mesh's alpha from. */
    glBegin(GL_TRIANGLE_STRIP);
    glColor4f(0.5f, 0.5f, 0.5f, 1.0f);
    memset(&m, 0, sizeof m);
    if (tgl_grid_map(&m) != 0) {
        a->bad_map = 1;
        glEnd();
        checkf(0, "%s: the map was refused (why=%u), so nothing about its mesh was measured",
               label, (unsigned)m.refuse);
        return;
    }

    for (t = 0.0f; t < RIB_T_SPAN; t += RIB_T_STEP) {
        int frame_out = 0;
        int i;

        xb_wave_step(rib_bits(t), proj_w, proj_h);
        tgl_grid_map_points(&m, &xb_rib_px[0][0], &xb_rib_py[0][0], &xb_rib_bri[0][0], n, s_pts);

        for (i = 0; i < n; i++) {
            const int x = s_pts[i].x_px, y = s_pts[i].y_px;
            int bad = 0;

            if (x < a->min_x) a->min_x = x;
            if (x > a->max_x) a->max_x = x;
            if (y < a->min_y) a->min_y = y;
            if (y > a->max_y) a->max_y = y;

            if (x < m.vp_x0)      { a->under_x0++; bad = 1; }
            if (x > m.vp_x1)      { a->over_x1++;  bad = 1; }
            if (y < m.vp_y0)      { a->under_y0++; bad = 1; }
            if (y > m.vp_y1)      { a->over_y1++;  bad = 1; }
            if (bad) {
                frame_out++;
                col_out[i % XB_RIBBON_COLS] = 1u;
                row_out[i / XB_RIBBON_COLS] = 1u;
            }
        }
        if (frame_out > a->out_pts) a->out_pts = frame_out;
        if (frame_out) a->offenders_rows++;
        a->frames++;
    }
    glEnd();

    a->out_cols = 0;
    for (c = 0; c < XB_RIBBON_COLS; c++) a->out_cols += col_out[c] != 0u;
    a->out_rows = 0;
    for (r = 0; r < XB_RIBBON_ROWS; r++) a->out_rows += row_out[r] != 0u;

    printf("    %s: canvas %dx%d, projection %dx%d -- x_px %d..%d, y_px %d..%d, rect %d..%d x %d..%d"
           " | %d of %d frames have a point outside, %d points worst case, in %d column(s) and %d"
           " row(s) (x1:%d x0:%d y1:%d y0:%d)\n",
           label, canvas_w, canvas_h, proj_w, proj_h,
           a->min_x, a->max_x, a->min_y, a->max_y,
           (int)m.vp_x0, (int)m.vp_x1, (int)m.vp_y0, (int)m.vp_y1,
           a->offenders_rows, a->frames, a->out_pts, a->out_cols, a->out_rows,
           a->over_x1, a->under_x0, a->over_y1, a->under_y0);
}

static void run_ribbon(void)
{
    rib_scan_t console, square;
    const int n = XB_RIBBON_ROWS * XB_RIBBON_COLS;

    /* (1) The console's own arrangement, reproduced: 768x500 canvas, 776x536 projection. */
    ribbon_scan("the shipped window pair", 768, 500, 776, 536, &console);
    /* (2) The control that isolates the cause: the same sheet projected at the canvas size. */
    ribbon_scan("one size for both", 768, 500, 768, 500, &square);

    checkf(console.frames > 0 && square.frames > 0 && console.bad_map == 0 && square.bad_map == 0,
           "both arrangements reached a captured map, so both extents below are measured (%d/%d"
           " frames)", console.frames, square.frames);

    /* The overhang, named.  Every point that leaves does so on the same edge and in the same column,
     * because the mesh's x comes from the column index alone (`s_gxv[c]`, one value per column) --
     * so the claim "the sheet's last column is off the canvas" is checkable as a count, not as an
     * impression, and it says the wave is NOT what leaves the window. */
    checkf(console.out_pts == XB_RIBBON_ROWS && console.offenders_rows == console.frames &&
           console.out_cols == 1 && console.out_rows == XB_RIBBON_ROWS &&
           console.over_x1 == XB_RIBBON_ROWS * console.frames &&
           console.under_x0 == 0 && console.over_y1 == 0 && console.under_y0 == 0,
           "the console's ribbon leaves the clip rect on exactly its last column, every"
           " frame, on x_px > vp_x1 alone (%d points, %d column(s), x1=%d of %d outside)"
           " -- the 32 that `over=` prints, restated as geometry",
           console.out_pts, console.out_cols, console.over_x1,
           XB_RIBBON_ROWS * console.frames);

    /* The falsifying control for that reading: with one size for both, the selfsame sheet -- the
     * same wave, the same amplitude, the same mesh -- has nothing outside the rect.  So the
     * overhang is the size pair, not the surface leaving the window, and a gate that asked the
     * clipper's question would keep answering it for the wrong reason. */
    checkf(square.out_pts == 0 && square.offenders_rows == 0 &&
           square.over_x1 == 0 && square.over_y1 == 0 &&
           square.under_x0 == 0 && square.under_y0 == 0,
           "the same mesh projected at the canvas size is entirely inside the rect (%d..%d x %d..%d),"
           " so the wave never leaves the window by itself",
           square.min_x, square.max_x, square.min_y, square.max_y);

    /* And the amount of the overhang, which is what the next rung has to be priced against: 8 px of
     * x on a 768 px canvas, one column of 32, and the whole sheet inside the -2048 .. +2047 px the
     * GS's XYZ2 field can represent about the page origin. */
    checkf(console.max_x - 767 > 0 && console.max_x < 2048 && console.min_x >= -2048 &&
           console.max_y < 2048 && console.min_y >= -2048,
           "the overhang is %d px past vp_x1 and every point of the mesh is inside the -2048..+2047"
           " px the vertex word can carry (x %d..%d, y %d..%d)",
           console.max_x - 767, console.min_x, console.max_x, console.min_y, console.max_y);

    /* The mesh the batch is asked to take is the one the walk draws: same 1,024 points, so a scan
     * that walked off the end of either array would show up here as the wrong point count rather
     * than as a plausible-looking extent. */
    checkf(n == (XB_RIBBON_ROWS - 1) * XB_RIBBON_COLS + XB_RIBBON_COLS,
           "the swept mesh is the %d points the zigzag submits %d vertices over", n,
           (XB_RIBBON_ROWS - 1) * XB_RIBBON_COLS * 2);
}

int main(void)
{
    tgl_grid_map_t uncaptured;

    s_xs  = malloc(SWEEP_MAX * sizeof *s_xs);
    s_ys  = malloc(SWEEP_MAX * sizeof *s_ys);
    s_bri = malloc(SWEEP_MAX * sizeof *s_bri);
    s_pts = malloc(SWEEP_MAX * sizeof *s_pts);
    s_us   = malloc(SWEEP_MAX * sizeof *s_us);
    s_vs   = malloc(SWEEP_MAX * sizeof *s_vs);
    s_cols = malloc(SWEEP_MAX * 4 * sizeof *s_cols);
    s_tpts = malloc(SWEEP_MAX * sizeof *s_tpts);
    if (!s_xs || !s_ys || !s_bri || !s_pts || !s_us || !s_vs || !s_cols || !s_tpts) {
        fprintf(stderr, "[GRIDMAP] out of memory\n");
        return 1;
    }

    /* The pair's own contract, checked before any sweep leans on it. */
    checkf(tgl_grid_map(NULL) == -1, "tgl_grid_map(NULL) is refused rather than dereferenced");
    memset(&uncaptured, 0, sizeof uncaptured);
    tgl_grid_map_points(&uncaptured, NULL, NULL, NULL, 8, NULL);
    checkf(1, "tgl_grid_map_points() survives NULL arrays on an uncaptured map, and returns");

    run_window("640x480 (the PS2 canvas, banded)", 640, 480, 0);
    run_window("101x67 (prime: every coefficient inexact, dense)", 101, 67, 1);
    run_window("64x48 (dense)", 64, 48, 1);

    /* Refusals on the last window, whose state is still the app's. */
    run_refusals();

    /* LANE 12h rung (i): the textured map, its sheet, its texel chain and its inverted refusals. */
    run_tex();

    /* The mesh the lane was written for, and the reason its first live run was refused. */
    run_ribbon();

    printf("[GRIDMAP] checks=%d failures=%d\n", g_total, g_failed);
    return g_failed != 0;
}
