/*
 * src/gl/grid_map.h — the viewport map a retained mesh needs, lifted out of the live context once
 *
 * LANE 12f rung (i) of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The seam is
 * drawn where the ABI law forces it: `tgl_grid_map()` and `tgl_grid_map_points()` are the only
 * floating arithmetic in this pair and they live in `src/gl/tinygl/vertex_stage.c`, the one
 * `-mhard-float` unit that already owns the map; `src/gl/backend_eegs.c` is `-msoft-float` and
 * consumes integers.  Nothing here hands a float over a translation-unit boundary, which is the
 * rule that keeps `-msoft-float` an ABI law for the rest of the image (see that file's header).
 *
 * Why a struct and not a callback: the immediate-mode walk maps one vertex at a time and pays a
 * matrix load, a divide and four truncates for the privilege.  A mesh the port already has in
 * hand -- the ribbon's `xb_rib_px`/`xb_rib_py`/`xb_rib_bri`, written this frame by `xmb_wave.c` --
 * wants the same answers for all of its points, from constants that do not move between them.  So
 * the constants are lifted once, the points are mapped in one loop that touches only memory, and
 * the strip writer downstream never does arithmetic at all.
 *
 * Why the map loop is not a second statement of the same truth by policy but by test: it IS a
 * second encoding of `tgl_vertex_to_viewport()`, and a second encoding of a float map is exactly
 * the kind of thing that is byte-correct on a host and wrong on the console.  The gate that keeps
 * them together is `verify/tests/test_grid_map.c`, which links TinyGL and compares every swept
 * point against the real `tgl_vertex_transform()` + `tgl_vertex_to_viewport()` on the same
 * context -- the comparator is the map itself, not a restatement of it.  `tgl_grid_map()`'s return
 * value is the other half of that honesty: the two-constant-per-axis form below is only the same
 * arithmetic when the projection really is diagonal and W really is constant, and it says so
 * rather than assuming it.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_GRID_MAP_H
#define BTRON_GL_GRID_MAP_H

#include <stdint.h>

/* What `tgl_grid_map()` lifts.  The names are the walk's own: `mx`/`xc` are the two non-zero
 * entries of the model-projection's first row (`m[0][0]` and `m[0][3]`), so
 *
 *     pc.X = (x*mx + y*0 + 0*0) + xc
 *
 * is the same three additions the general row computes, with the exact zeros left out; `winv` is
 * the reciprocal of `m[3][3]`, computed the way the viewport map computes it (`1.0 / pc.W`) and for
 * the same reason it is constant here -- `matrix_model_projection_no_w_transform` says the W row
 * has no x, y or z in it.  `scale_*`/`trans_*` are the viewport's, and a viewport set at the
 * window's origin is what makes the pixels this map returns the pixels the canvas blit uses.
 *
 * `a_byte` is the colour-channel byte the context's current alpha truncates to, because a retained
 * mesh carries intensity per point and takes its alpha from the state the caller drew with -- the
 * ribbon's `glColor4f(b, b, b, 1.0f)`.  `smooth` and `blended` are the PRIM word's IIP and ABE, read
 * from the same two places the GS adapter reads them (`current_shade_model` and the ZBuffer's
 * `enable_blend`), so the batch asks for the state the immediate path would have asked for.
 *
 * `vp_x0`..`vp_y1` are the viewport's device-pixel rect, inclusive at both ends.  They are the
 * rect the GS's own SCISSOR is programmed to (the same `client` reaches `eegs_present_send()`), so
 * a mapped point outside them is not a point that fails to be drawn -- it is a point the hardware
 * clips, and these four integers are what `over=` counts it against.  They were also the batch's
 * refusal gate until the first live runs of LANE 12f rung (ii): the ribbon's mesh is 8 px wider than
 * its canvas because `xb_paint()` projects the sheet with the window's size and puts the camera on
 * the client area's, and a gate that asked the clipper's question therefore refused the one mesh the
 * lane was written for, every frame.  The measurement is the ribbon section of
 * `verify/tests/test_grid_map.c` (x_px 0..775 against a rect of 0..767, 32 points per frame, one
 * column, on every one of 2,000 swept wave frames, with the same sheet inside the rect once both
 * sizes are the canvas's).  The derivation of the rect as the clipper's answer restated in integers
 * is kept below, because it is what makes `over=` a statement about geometry rather than about a
 * number nobody can read: a vertex survives the clipper exactly when `-1 <= pc.X <= 1`, and the map
 * sends that interval to `trans - scale` .. `trans + scale`, which `gl_eval_viewport()` makes
 * `xmin` .. `xmin + xsize - 0.5` -- so the truncate of a surviving vertex is inside
 * `[xmin, xmin + xsize - 1]`, and a pixel outside it is a vertex the walk would have clipped.  The
 * one assumption in that chain is `xmin == 0`: the half-pixel slack at the far edge makes the two
 * answers differ by at most one truncate only for a viewport that does not start at the origin, and
 * this port sets every viewport at the origin (`glViewport(0, 0, w, h)` in `xb_paint()`).  The gate
 * is measured, not asserted -- that same suite sweeps points deliberately outside the window and
 * requires that the walk clipped exactly the points the rect rejects.
 *
 * `ok` is set by a successful capture and checked by the consumer.  A zeroed struct of this shape
 * is a *valid-looking* map -- mx = 0, winv = 0, everything truncating to the viewport's centre --
 * and that is the failure mode a flag costs one load to close.  It is also the reason a caller that
 * wants rung (ii)'s two counts in one place can hand the refusal itself to `eegs_grid_strip()`: the
 * zeroed map has `ok` clear, records nothing, and is counted as the slow path.
 *
 * `refuse` names WHICH condition failed, from the list below, and is the one field written on a
 * refusal -- every other byte of `out` keeps whatever the caller put there, so the zeroed-struct
 * contract above is unchanged.  It exists because `slow=` on its own cannot tell a mesh the clipper
 * would have cut from a context whose product is stale, and the first live run of the batch path
 * printed `batch=0 slow=1` on every frame with no way to say which (doc/txt/PS2.txt, LANE 12f rung
 * (iii)).  0 means nothing refused, which is also what a caller sees if it never asked. */
#define TGL_GRID_REFUSE_NONE 0u
#define TGL_GRID_REFUSE_ARG 1u      /* a null argument */
#define TGL_GRID_REFUSE_STALE 2u    /* the model-projection product is not this window's yet */
#define TGL_GRID_REFUSE_LIGHT 3u    /* lighting on: colour and pc come from elsewhere */
#define TGL_GRID_REFUSE_TEX 4u      /* TEXTURE_2D bound and enabled: zp.s/zp.t are written */
#define TGL_GRID_REFUSE_W 5u        /* the product's W depends on the vertex */
#define TGL_GRID_REFUSE_SKEW 6u     /* the projection couples X to Y */
#define TGL_GRID_REFUSE_RECT 7u     /* RETIRED, and never produced: a mapped point outside the
                                     * viewport rect used to refuse the batch, which is what the
                                     * first live runs of LANE 12f printed as `why=7`, and what the
                                     * ribbon's 8-px overdraw made a permanent answer.  The rect is
                                     * now the GS's job (SCISSOR, and `over=`); the value is kept so
                                     * the dated rows in doc/md/PS2-GS-LOG.md decode. */
#define TGL_GRID_REFUSE_UNSET 8u    /* a map that was never captured at all (`ok` clear, no reason
                                     * carried) -- the state a caller-zeroed struct arrives in */
#define TGL_GRID_REFUSE_RANGE 9u    /* a mapped point outside the coordinates the GS's vertex word
                                     * can carry (-2048 .. +2047 px about the page origin, the 12.4
                                     * field of XYZ2): the one thing a point outside the rect can
                                     * become that the SCISSOR cannot fix, because the field folds and
                                     * the point lands elsewhere on the canvas.  Named by
                                     * `eegs_grid_strip()`, whose gate it is. */
#define TGL_GRID_REFUSE_NOTEX 10u   /* the textured map's inverse question (LANE 12h rung (i)): no
                                     * TEXTURE_2D bound-and-enabled, or a bound image whose bytes were
                                     * never uploaded (`images[0].pixmap == NULL`).  The walk draws
                                     * that state FLAT -- clip.c's own arm folds an absent plane back
                                     * to no texture -- and a batch that took it would be claiming a
                                     * plane the hardware was never given.  A refusal is not a loss:
                                     * the caller's fallback is the walk, drawing the same flat
                                     * primitive the folding produces. */
typedef struct {
    float    mx, xc;
    float    my, yc;
    float    winv;
    float    scale_x, trans_x;
    float    scale_y, trans_y;
    uint32_t a_byte;
    uint32_t smooth;
    uint32_t blended;
    uint32_t brighten;    /* the live blend is `GL_DST_COLOR, GL_ONE`: dst*(1+src), not src-over --
                           * ABE alone cannot say it, and the GS's two ALPHA laws are one per drawing
                           * context, so the batch has to carry WHICH law the walk would have wanted.
                           * See `eegs_tri_state_t.brighten` and `ps2_gsalpha_probe()`'s M box. */
    int32_t  vp_x0, vp_x1, vp_y0, vp_y1;
    uint32_t refuse;
    uint32_t ok;
} tgl_grid_map_t;

/* One mapped mesh point: the two device pixels the span loops would have walked and the packed
 * colour in the unit `eegs_vertex_t` already uses (r in bits 7:0, g 15:8, b 23:16, a 31:24).  For a
 * grey mesh the three colour bytes are equal by construction, and they are equal to the byte the
 * immediate path puts in the same field -- which is what the VALUES gate measures. */
typedef struct {
    int32_t  x_px, y_px;
    uint32_t rgba;
} tgl_grid_pt_t;

/* LANE 12h rung (i): the same lift for a mesh that samples a sheet.  The position half of the map
 * is `tgl_grid_map()`'s exactly -- the same refusals (STALE/LIGHT/W/SKEW), the same rect, the same
 * `smooth`/`blended` -- and the TEXTURE condition inverts: this one REQUIRES a texture
 * bound-and-enabled with an uploaded plane, because its whole subject is the texel coordinate the
 * grey map cannot say.  The four mapper constants are the upload's own (`texture.c`, one texel of
 * scale per unit of `u`/`v`, half a texel of offset in so a coordinate names a texel's centre), and
 * `pixmap` travels as an opaque key: the plane's VRAM identity belongs to the GS registry
 * (`gs_tex_find()`), which `vertex_stage.c` must not learn about, so only the backend entry that
 * already includes the registry resolves it.  `a_byte` is unused here -- the colour is per-vertex
 * and four-channel, not the draw's one grey -- and rides along because the embed is the honest way
 * to state "the position map, unchanged". */
typedef struct {
    tgl_grid_map_t g;
    float          sscale, soff;
    float          tscale, toff;
    const void    *pixmap;
} tgl_tex_map_t;

/* One mapped textured point: `tgl_grid_pt_t` plus the texel coordinate in the unit the ST register
 * itself carries (the float `gs_vertex()` produces from `zp.s`/`zp.t`, divided by the upload's
 * power-of-two scale on the exponent field), and the full four-byte colour, because an icon quad's
 * (br,br,br,a) and its shadow's (0,0,0,a) are the general per-channel case of the walk's colour
 * line.  This is `eegs_vertex_t`'s shape on purpose: these are the vertices the batch emits. */
typedef struct {
    int32_t  x_px, y_px;
    float    s, t;
    uint32_t rgba;
} tgl_tex_pt_t;


/* Lift the map out of the live context.  Returns 0 and fills `out` when the context is in the shape
 * this struct can represent -- lighting off, no texture bound-and-enabled, a W that does not depend
 * on the vertex, a projection with nothing coupling X to Y, and a model-projection product the
 * vertex walk has actually computed -- and -1, writing only `refuse`, when it is not.  The last of
 * those five is the one that costs nothing to check and would corrupt a frame silently: a matrix
 * change sets `matrix_model_projection_updated` and leaves `matrix_model_projection` holding the
 * PREVIOUS product until the next `glBegin` recomputes it, so a mesh batched between a resize and
 * that `glBegin` would be mapped through the old window.  A -1 is not an error: it is the caller's
 * instruction to draw the mesh the way it always has, and the count of frames that took that answer
 * is what `slow=` in the [GSTS] row is for (LANE 12f rung (ii)).
 *
 * The mesh's planarity is part of the contract rather than of the check: the points this maps are
 * `glVertex2f` points, whose Z is 0 and whose W is 1, so the third and fourth matrix columns
 * contribute exact zeros whatever they hold and are not tested. */
int tgl_grid_map(tgl_grid_map_t *out);

/* Map `n` mesh points, given as three contiguous per-point float arrays -- x, y and intensity, in
 * the caller's own walk order -- into `n` `tgl_grid_pt_t`.  The arrays are the app's grid flattened
 * (`&xb_rib_px[0][0]` is 32x32 floats in row order), so this loop is straight-line: one load of
 * each, one multiply-add each, and stores.  Nothing about the mesh's topology is known here, which
 * is what lets the strip writer own the walk instead of the map. */
void tgl_grid_map_points(const tgl_grid_map_t *m, const float *xs, const float *ys,
                         const float *bri, int n, tgl_grid_pt_t *out);

/* Lift the textured map.  The same contract as `tgl_grid_map()`, one way round: returns 0 and
 * fills `out` when the context is in the shape a textured batch can represent -- a texture
 * bound-and-enabled with an uploaded plane, lighting off, a vertex-independent W, a diagonal
 * projection, a product the walk has computed -- and -1 writing only `refuse` otherwise, with
 * `TGL_GRID_REFUSE_NOTEX` naming the inverse of 12f's texture refusal.  The comparator that keeps
 * this honest is `verify/tests/test_grid_map.c`'s textured sweep: a real uploaded sheet so the
 * mapper constants are the upload's own, vertices run through the live walk, and bit equality on
 * `x_px`/`y_px`/`rgba`/`s`/`t` -- the texel chain reproduced rather than approximated. */
int tgl_grid_map_tex(tgl_tex_map_t *out);

/* Map `n` textured points: positions and colours from `xs`, `ys` and `cols` (four interleaved
 * floats a vertex, r, g, b, a, exactly as the caller hands `glColor4f` today), texel coordinates
 * from `us` and `vs` exactly as the caller hands `glTexCoord2f` today.  Nothing derives `u`/`v` --
 * the app carries them, which is what the ident run's `st=` measured as invariant -- and the map's
 * only texel work is the walk's own two-step conversion, `TGL_TRUNC(u*sscale + soff)` then the
 * ST field's power-of-two scale, spelled from `vertex_stage.c` and `clip.c` so a change to either
 * is a change the differential gate sees.  Straight-line loops in the split shape the o32
 * hard-float ABI requires, and the same refusal to hand a float over a TU boundary: `m` is memory,
 * the points come back as memory. */
void tgl_grid_map_tex_points(const tgl_tex_map_t *m, const float *xs, const float *ys,
                             const float *us, const float *vs, const float *cols, int n,
                             tgl_tex_pt_t *out);

#endif /* BTRON_GL_GRID_MAP_H */
