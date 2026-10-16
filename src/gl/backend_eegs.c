/*
 * src/gl/backend_eegs.c -- the vertices a clipped triangle becomes, as a GIF group, and the
 * texture switch that goes between them
 *
 * Row 5, steps 5a and 5b of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Everything
 * hard about the GS is in increments 2, 3 and 4, which this file calls and does not duplicate: the
 * field encodings are `ps2_gs_pack.c`'s, the environment is `ps2_gs_env.c`'s, and the group state
 * machine -- NLOOP, the REGS list, where a full buffer is cut and which vertices a continued strip
 * needs -- is `ps2_gs_rec.c`'s.  What is here is the two decisions the raster stage makes: which
 * primitive and which three state flags a triangle carries, and what a device-space vertex means
 * to the GS.  From 5b a third: when the plane a triangle samples is not the plane the stream
 * already points at, the three texture registers go in as a group of their own.  From LANE 12f a
 * fourth: a mesh the application already has in hand is one PRIM and a run of vertices, and needs
 * no triangle loop at all -- see `eegs_grid_strip()`.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "backend_eegs.h"

#include "grid_map.h"                       /* TGL_GRID_REFUSE_*: the `why=` codes */

#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_rec.h"

/* The register set this port's primitives are drawn with, as the PRIM word's CTXT bit.
 *
 * It is 0, and the reason it is worth a paragraph is that the two naming schemes in this port run
 * in opposite directions.  Every environment register this port writes is named for the FIRST
 * context -- `ps2_gs_env.c` emits GS_GP_FRAME_1, GS_GP_SCISSOR_1, GS_GP_ALPHA_1, GS_GP_XYOFFSET_1,
 * and `eegs_texture()` below writes GS_GP_TEX0_1/TEX1_1/CLAMP_1 -- and the GIF's address table
 * pairs those with the integer 0: `GS_REG_FRAME_1` is 4Ch and `GS_REG_FRAME_2` is 4Dh, and
 * ps2sdk's own pairing is `GS_REG_FRAME + context` (draw.c:61, draw_environment.c:38) with the
 * SAME integer going into the primitive's CTXT field (`GS_SET_PRMODE(..., context, ...)` at
 * draw.c:175, `GS_SET_PRIM(..., context, ...)` at draw3d.c:17).  So "context 1" is CTXT=0.
 *
 * CTXT=1 asks the GS for the second register set, and nothing in this port has ever written that
 * set: its FRAME is 0 (page 0, buffer width 0), its SCISSOR is the empty box, its XYOFFSET and
 * ALPHA are reset values.  A stream addressed that way is delivered, acknowledged and rasterised
 * against a window that clips every fragment away -- the whole picture vanishes and every counter
 * this side of the GS keeps saying the frame was sent.  That is what the LANE 19 rung-6 live census
 * found on 2026-10-16 (`ink=0` across the displayed page while the census's own CTXT=0 control
 * filled its box exactly), and it is why the census's controls and this backend must agree on 0. */
#define EEGS_CONTEXT 0u

/* The largest window-local coordinate the vertex word carries, in pixels.  `gs_vertex_coord16()`
 * biases a whole-pixel coordinate by `GS_XYOFFSET_PAGE` and puts the sum in XYZ2's 16-bit field
 * unsigned, so `(px + 2048) * 16` has to stay inside 0..65535 -- exactly -2048 .. +2047 -- and a
 * point outside it does not get clipped, it folds and lands somewhere else on the canvas.  That is
 * the one geometry a whole-mesh batch has to refuse, and `eegs_grid_strip()` refuses it by name. */
#define EEGS_GRID_XY_LIMIT ((int32_t)(GS_XYOFFSET_PAGE / 16u))

/* The depth a recorded vertex carries.  The environment disables the depth test (TEST.ZTEN = 0),
 * so this field reaches no comparator, and the order a 2D layer's primitives reach the screen in is
 * the order the stream presents them in, which is the order the application drew them. */
#define EEGS_Z 0u

static uint64_t s_prim_live;
static int      s_prim_valid;
static uint32_t s_prims;

static const gs_tex_t *s_tex_live;
static uint32_t s_tex_switches;
static uint32_t s_tex_missing;

/* The group-boundary rate, and the one column LANE 12e left behind as an instrument.
 *
 * s_prim_moves counts the times select_prim() actually sent a new PRIM word, each of which closes
 * the open group -- so `pchg=` against `groups=` in the [GSTS] row is the rate and its budget, and
 * EEGS_MAX_GROUPS (256, asserted against LANE 10's ring capacity in eegs_present.c) is what happens
 * when the rate outruns it: the stage table fills and the row's `drop=` counts primitives of this
 * frame's picture that the screen never receives.
 *
 * The four chain counters this rung grew to read its own win (taken / entry / miss / fresh, plus
 * the strip-selection helpers they hung on) are REMOVED, not parked: the measured verdict -- 516 of
 * ~1,838 ribbon triangles chained, every miss costing a list path and a re-entry, `pchg=` 51 -> 470
 * and `drop=` 0 -> 236 for 14% fewer vertices and no movement in f_ribbon -- is in
 * doc/txt/PS2.txt LANE 12e, and a re-lived rung shows up there without new counters, as the
 * `verts=`/`prims=` ratio leaving 3.00.  What it would need first is a group that survives a PRIM
 * change; the recorder's own strip primitive stays implemented and gated in ps2_gs_rec.c. */
static uint32_t s_prim_moves;

/* LANE 12f rung (ii): the two exits a retained mesh has, counted where the decision is made.
 *
 * `batch=` is a mesh drawn as one strip by `eegs_grid_strip()`, `slow=` is a mesh that asked and was
 * refused and is therefore being drawn by the immediate-mode walk instead.  Both are counted inside
 * that one entry -- a refusal returns -1 having recorded nothing, and a caller that could not even
 * capture a map hands the zeroed struct to the same entry, so there is exactly one place the two
 * numbers can come from and they cannot be made to sum to something other than the draws.
 *
 * They are on the row because the batch path has no clipper in front of it -- the clip is the GS's,
 * by the SCISSOR the present programs from the same rect the map carries (see the bbox scan in
 * `eegs_grid_strip()`, and `over=` below for what a frame relied on it for).  The ratio is the
 * evidence: `slow=` rising while the window is a fixed size says the gate is rejecting meshes that
 * used to draw, and `batch=` with a sheet missing an edge says the clip is not doing what the
 * register means.  Neither is a substitute for the user's eyes, which is rung (iii)'s gate.
 *
 * `why=` is the third column of that pair, and the one that makes the other two actionable: a lone
 * `slow=` cannot say whether the mesh left the window or the context was not in the shape a
 * two-constant map can represent, and those two have opposite remedies.  It is the LAST refusal of
 * the frame, encoded by `TGL_GRID_REFUSE_*` in `src/gl/grid_map.h` -- the map's own conditions come
 * in through the refused struct, and the range exit names its own here, so the column has one source
 * whatever the caller passed.  The first live run of the batch path is why it exists: it printed
 * `batch=0 slow=1` on every frame with no way to say which condition refused.
 *
 * `over=` is the fourth and the one the amended gate needs: a batched mesh's points that lie outside
 * the clip rect but inside the vertex word's range, which is the work this frame asked the GS's
 * SCISSOR to do.  It is not a refusal and not an error -- the ribbon's 32 points a frame are the same
 * 32 the immediate path's clipper used to cut, for ~3,260 cycles a vertex -- but a fast path that
 * relied on a register that does not mean what it says would lose a strip of the picture and print
 * nothing, and this is the column that would be nonzero while it did. */
static uint32_t s_grid_batch;
static uint32_t s_grid_slow;
static uint32_t s_grid_why;
static uint32_t s_grid_over;
static int      s_dual_context_en;

void eegs_enable_dual_context(int enable)
{
    s_dual_context_en = enable ? 1 : 0;
}

int eegs_is_dual_context(void)
{
    return s_dual_context_en;
}

static uint64_t prim_word(uint32_t op, const eegs_tri_state_t st)
{
    uint32_t ctxt = EEGS_CONTEXT;
    if (s_dual_context_en) {
        ctxt = st.blended ? 1u : 0u;
    }
    return gs_pack_prim(op, st.smooth ? 1u : 0u, st.textured ? 1u : 0u,
                        st.blended ? 1u : 0u, ctxt, 0u);
}

/* gs_rec_prim() closes whatever group is open, which is right for a real state change and wrong
 * for a repetition: a ribbon's 1,828 triangles all ask for the same PRIM, and re-asking would be
 * 1,828 groups and 1,828 DMA starts of three vertices each.  So the word is compared, not re-sent,
 * and the group stays open until the state genuinely moves -- which is why the send is counted (see
 * s_prim_moves above) rather than trusted: the rate is what the group budget is spent on. */
static void select_prim(uint64_t prim)
{
    if (s_prim_valid && s_prim_live == prim)
        return;

    gs_rec_prim(prim);
    s_prim_live = prim;
    s_prim_valid = 1;
    s_prim_moves++;
}

/* One vertex's colour and texture coordinate, then the vertex itself.  The order is the recorder's
 * REGS list's -- PRIM, RGBAQ, ST, XYZ2 -- and it is not free: the drawing kick fires on XYZ2, so
 * the state a vertex is drawn with has to be in the GS before it. */
static void emit_vertex(const eegs_vertex_t *v)
{
    gs_rec_color(v->rgba & 0xFFu, (v->rgba >> 8) & 0xFFu,
                 (v->rgba >> 16) & 0xFFu, (v->rgba >> 24) & 0xFFu);
    gs_rec_texcoord(v->s, v->t);
    gs_rec_vertex(v->x_px, v->y_px, EEGS_Z);
}

void eegs_frame_begin(gs_rec_sink sink, void *user)
{
    gs_rec_init(sink, user);
    s_prim_valid = 0;
    s_prim_live = 0u;
    s_prims = 0u;
    s_tex_live = 0;
    s_tex_switches = 0u;
    s_tex_missing = 0u;
    s_prim_moves = 0u;
    s_grid_batch = 0u;
    s_grid_slow = 0u;
    s_grid_why = TGL_GRID_REFUSE_NONE;
    s_grid_over = 0u;
}

void eegs_frame_end(void)
{
    gs_rec_end();
    s_prim_valid = 0;
    s_tex_live = 0;
}

/* The plane the following primitives sample, or none.
 *
 * Three AD writes, one group: `TEX0` says where the plane is, `TEX1` how it is sampled and
 * `CLAMP` what a coordinate that leaves it does.  They travel as a group of their own because the
 * environment packet that could carry them is written once per frame and this port's frame binds
 * two sheets -- see ps2_gs_tex.h's three laws.
 *
 * Pointing at *nothing* sends nothing: `TME` is in the PRIM word, so an untextured primitive reads
 * no texture whatever `TEX0` holds, and the page it holds is the one the next switch names again.
 * What it does do is clear the stream's plane, because a textured primitive must never inherit one
 * it was not given. */
void eegs_texture(const gs_tex_t *t)
{
    /* The trio in ps2_gs_env.c's order -- CLAMP, TEX0, TEX1 -- so that "the three texture
     * registers travel in this order" has one answer in this port, in the frame's packet and
     * between its groups alike. */
    static const uint32_t regs[3] = { GS_GP_CLAMP_1, GS_GP_TEX0_1, GS_GP_TEX1_1 };
    uint64_t words[3];

    if (t == s_tex_live) return;

    s_tex_live = t;
    if (t == 0) return;

    words[0] = t->clamp;
    words[1] = t->tex0;
    words[2] = t->tex1;
    gs_rec_ad(words, regs, 3u);

    /* gs_rec_ad() sent the open group, so the PRIM that group carried is no longer live state and
     * the next vertex needs it re-declared -- which is the same reason select_prim() compares a
     * word rather than trusting one. */
    s_prim_valid = 0;
    s_tex_switches++;
}

/* A textured primitive with no plane in the stream would read the page the last switch left in
 * `TEX0`: a different texture's pixels, drawn with no evidence attached anywhere.  So the state is
 * corrected here, where the PRIM word is built, and the correction counted -- a non-zero
 * `eegs_tex_missing()` is the bench saying an uploaded sheet drew flat. */
static eegs_tri_state_t need_plane(eegs_tri_state_t st)
{
    if (st.textured && s_tex_live == 0) {
        st.textured = 0u;
        s_tex_missing++;
    }
    return st;
}

/* One triangle, three vertices, in the group its state already carries.
 *
 * This is LANE 12e's measured PARK, written as one sentence in the shape the code has to be: asking
 * the GS to continue a strip saved 14% of the vertices and moved nothing the user can see, because
 * a continuation is only affordable inside a group and a missed continuation costs a group.  Read
 * `verts=`/`prims=` in the [GSTS] row against this function before re-living it -- the rung is worth
 * taking again only with a group that survives a PRIM change, and 12f's batch entry is the shape
 * that gets the ribbon's per-vertex cost down without needing one.  doc/txt/PS2.txt LANE 12e holds
 * the numbers, and ps2_gs_rec.c still implements the primitive itself. */
void eegs_triangle(const eegs_vertex_t v[3], eegs_tri_state_t st)
{
    st = need_plane(st);
    select_prim(prim_word(GS_OP_TRI, st));
    emit_vertex(&v[0]);
    emit_vertex(&v[1]);
    emit_vertex(&v[2]);
    s_prims++;
}

/* One mapped mesh point, in the unit the recorder takes.  `s`/`t` are 0 because the map this call
 * consumes refuses a context with a texture enabled -- and 0 is what `gs_vertex()` puts in an
 * untextured vertex, so the ST register this writes is the ST register the walk would have written. */
static void emit_point(const tgl_grid_pt_t *p)
{
    eegs_vertex_t v;

    v.x_px = p->x_px;
    v.y_px = p->y_px;
    v.s = 0.0f;
    v.t = 0.0f;
    v.rgba = p->rgba;
    emit_vertex(&v);
}

/* The XMB ribbon's walk, as one strip.  LANE 12f rung (i): the mesh is retained -- `xmb_wave.c`
 * wrote its positions and intensities this frame -- so the draw is a walk over arrays this file is
 * handed, not a thousand calls into a general-purpose immediate-mode pipeline.
 *
 * What it does is one PRIM and then the vertices, in `xb_draw_ribbon()`'s own order: for every row
 * pair, down the columns, with the odd row walked backwards so the strip never has to be broken at a
 * turn-around.  The recorder owns everything else -- the REGS list, the running NLOOP, where a full
 * buffer is cut, and the two vertices a continued strip needs (`reemit_continuation()` in
 * ps2_gs_rec.c), which is why a mesh of any size fits in a stream whose buffer does not.
 *
 * The strip is the primitive LANE 12e measured and parked *as a backend decision*, and the two are
 * not the same thing: 12e kept drawing lists through `eegs_triangle()` and asked the GS to continue
 * each triangle, so every missed continuation cost a group and the group rate went 20x over a
 * 256-entry budget.  Here there is one PRIM for the whole mesh, so the group count is a function of
 * the buffer, not of the geometry -- two groups for the ribbon's 1,984 vertices against the parked
 * build's 51.  Its own numbers are in doc/txt/PS2.txt LANE 12f, and the ratio that shows it is the
 * `verts=`/`prims=` one in the [GSTS] row leaving 3.00.
 *
 * NOT CLIPPED, and gated instead (LANE 12f rung (ii), amended by rung (iii)'s measurement).  The
 * immediate path sends the clipper every triangle and the clipper is what decides that a vertex
 * outside the volume does not reach the stream; this entry has no clipper in front of it.  What it
 * does have is the GS's own clip window: `eegs_present_send()` programs SCISSOR from the same client
 * rect the map carries as `vp_x0`..`vp_y1`, and XYOFFSET from the same origin, so a primitive that
 * crosses the rect is cut by the hardware exactly as the walk's clipper used to cut it.  The gate
 * therefore asks the one question the SCISSOR cannot answer -- can XYZ2 REPRESENT this point at all
 * (+/-2048 px, `EEGS_GRID_XY_LIMIT`) -- and a point outside the rect but inside that field is TAKEN
 * and counted in `over=` instead of refused.  It refuses before anything is recorded, so a refusal
 * leaves the stream exactly as it found it and the caller draws the ribbon the way it always did.
 * The rect is still derived from the clipper rather than invented -- a vertex survives exactly when
 * `-1 <= pc.X <= 1`, which the viewport map carries to `xmin + xsize - 0.5` at the outside, and a
 * truncate of anything at or below that is at or below `xmin + xsize - 1`, so `over=` counts against
 * the clipper's own answer restated in integers, for ~4 cycles a point against the walk's ~3,260.
 * Both halves are measured on the host against the real clipper, point by point, in
 * `verify/tests/test_grid_map.c`. */
int eegs_grid_strip(const tgl_grid_map_t *m, const tgl_grid_pt_t *pts, int rows, int cols)
{
    eegs_tri_state_t st;
    uint32_t         over = 0u;
    const int npts = rows * cols;
    int r, c, i, submitted = 0;

    if (m == 0 || pts == 0 || rows < 2 || cols < 1) {
        s_grid_slow++;
        s_grid_why = TGL_GRID_REFUSE_ARG;
        return -1;
    }
    if (!m->ok) {
        /* The caller hands the refused map over rather than routing around it, which is what keeps
         * the two counts in one place -- and the refused map carries its reason with it.  A struct
         * with no reason in it was never captured at all, and that is its own code: `ok` clear with
         * `refuse` zero is exactly what a caller-zeroed map looks like, and reading it as "nothing
         * refused" would make the column lie. */
        s_grid_slow++;
        s_grid_why = m->refuse ? m->refuse : TGL_GRID_REFUSE_UNSET;
        return -1;
    }

    /* rung (ii), as amended by the ribbon's own measurement: a whole mesh is batched when the GS can
     * REPRESENT every point of it, and the clip rect is not the question -- the SCISSOR
     * `eegs_present_send()` programs from the same client rect is what cuts a primitive that crosses
     * it, which is exactly what the walk's clipper was doing for ~3,260 cycles a vertex.  Run before
     * anything is recorded, so a refusal leaves the stream exactly as it found it.  The scan is over
     * the mesh's `rows * cols` points rather than the `(rows-1) * cols * 2` vertices the zigzag
     * submits, because the two sets are the same -- every row is touched, as `r` or as `r + 1`, for
     * every `r` in the walk -- and the point set is the one the caller's arrays hold.
     *
     * What is refused is a point outside XYZ2's 12.4 field, -2048 .. +2047 px about the page origin
     * (`GS_XYOFFSET_PAGE`, ps2_gs_pack.h; asymmetric by one because `gs_vertex_coord16()` adds that
     * offset to the coordinate and the sum goes through a 16-bit unsigned field).  Such a coordinate
     * does not get clipped, it folds, and the triangle lands somewhere else on the canvas.  What is
     * COUNTED is a point outside the rect and inside that range -- `over=`, the work this frame asked
     * the GS's clip window to do.  The first
     * version of this gate refused on the rect and the batch path therefore never ran at all: the
     * app projects the sheet with the window's 776x536 and draws it into the client's 768x500, so
     * the ribbon's last column maps to x_px 775 against a rect of 0..767, every frame, on every one
     * of the 2,000 wave frames `verify/tests/test_grid_map.c` sweeps.  Its 32 points are this
     * column's `over=32`, and the row plus the user's eyes are what say the SCISSOR means it.
     *
     * The running total is local and committed only after the scan completes, for the same reason the
     * vertices are: `over=` counts the points of this frame's TAKEN batches, so a frame that refused
     * mid-scan must leave the column exactly where it found it -- a refusal that counted would put
     * the geometry of a mesh the GS never saw onto the row that describes what the GS did see. */
    for (i = 0; i < npts; i++) {
        if (pts[i].x_px < -EEGS_GRID_XY_LIMIT || pts[i].x_px >= EEGS_GRID_XY_LIMIT ||
            pts[i].y_px < -EEGS_GRID_XY_LIMIT || pts[i].y_px >= EEGS_GRID_XY_LIMIT) {
            s_grid_slow++;
            s_grid_why = TGL_GRID_REFUSE_RANGE;
            return -1;
        }
        if (pts[i].x_px < m->vp_x0 || pts[i].x_px > m->vp_x1 ||
            pts[i].y_px < m->vp_y0 || pts[i].y_px > m->vp_y1)
            over++;
    }
    s_grid_over += over;

    /* What the immediate path does per triangle, once per draw: an untextured primitive must not
     * inherit a plane the stream is still pointing at, and the walk's first ribbon triangle cleared
     * it exactly like this.  Passing 0 sends nothing -- `TME` in the PRIM word is what keeps an
     * untextured primitive off the page. */
    eegs_texture(0);

    st.smooth = m->smooth ? 1u : 0u;
    st.textured = 0u;
    st.blended = m->blended ? 1u : 0u;

    select_prim(prim_word(GS_OP_TRISTP, st));

    for (r = 0; r < rows - 1; r++) {
        for (c = 0; c < cols; c++) {
            const int col = (r & 1) ? (cols - 1 - c) : c;

            emit_point(&pts[r * cols + col]);
            emit_point(&pts[(r + 1) * cols + col]);
            submitted += 2;
        }
    }

    /* A strip of n vertices is n-2 triangles, and they are the same triangles the list path drew --
     * which is the claim gate (b) of this rung measures, per triangle, payload by payload. */
    if (submitted >= 3) s_prims += (uint32_t)(submitted - 2);
    s_grid_batch++;
    return submitted;
}

/* The XMB menu's quads, as one textured batch: LANE 12h rung (i), the same lane's sentence --
 * "stop paying for generality where the data is retained" -- applied to the mesh the app has in
 * hand a thousand times a second because it carries texel coordinates the grey map cannot say.
 *
 * The gates are `eegs_grid_strip()`'s, unchanged and for the unchanged reasons: refusal before
 * anything is recorded, the XYZ2 representability scan with `over=` counting what the SCISSOR is
 * asked to cut, and the same `batch=`/`slow=`/`why=`/`over=` columns, because a mesh is a mesh
 * whichever entry takes it and two sets of counters would be two models of one decision.
 *
 * What differs is the texture, and it is handled exactly as the walk handles it, line for line:
 * `eegs_texture(gs_tex_find(m->pixmap))` mirrors `gl_draw_triangle_fill()`'s own pairing -- the
 * registry answer, whatever it is, pointed at the stream once for the whole run rather than once
 * per triangle, which is also what the walk's word-comparing `select_prim` ends up doing when a
 * run of triangles shares a plane -- and `st.textured` asks for what the caller asked for, so a
 * plane that never got a VRAM address demotes through `need_plane()` and counts `texmiss` exactly
 * as the immediate path would.  Then each quad splits (v0,v1,v2) and (v0,v2,v3), the GL_QUADS
 * split `vertex.c` itself makes, and the vertices go through `eegs_triangle()`: its word compare
 * keeps the run inside one PRIM group, and the recorder owns everything downstream.
 *
 * Colour and texels are not this function's business: they arrive mapped, in `tgl_tex_pt_t`, bit-
 * identical to what `tgl_vertex_to_viewport()` + `gs_vertex()` would have produced -- the claim
 * `verify/tests/test_grid_map.c`'s textured sweep measures, because a texel coordinate off by one
 * truncate samples the wrong texel and shows up on the screen, not in a log row. */
int eegs_tex_quads(const tgl_tex_map_t *m, const tgl_tex_pt_t *pts, int nquads)
{
    eegs_tri_state_t st;
    uint32_t         over = 0u;
    const int        npts = nquads * 4;
    int              q, i;

    if (m == 0 || pts == 0 || nquads < 1) {
        s_grid_slow++;
        s_grid_why = TGL_GRID_REFUSE_ARG;
        return -1;
    }
    if (!m->g.ok) {
        s_grid_slow++;
        s_grid_why = m->g.refuse ? m->g.refuse : TGL_GRID_REFUSE_UNSET;
        return -1;
    }

    /* The same scan as the strip's, over the four corners the caller's array holds: representable
     * in the vertex word or the whole batch is refused, outside the rect and inside the field is
     * counted and left to the hardware's clip.  Local until the scan completes, for the strip's
     * reason -- a refusal must not put geometry the GS never saw onto the row. */
    for (i = 0; i < npts; i++) {
        if (pts[i].x_px < -EEGS_GRID_XY_LIMIT || pts[i].x_px >= EEGS_GRID_XY_LIMIT ||
            pts[i].y_px < -EEGS_GRID_XY_LIMIT || pts[i].y_px >= EEGS_GRID_XY_LIMIT) {
            s_grid_slow++;
            s_grid_why = TGL_GRID_REFUSE_RANGE;
            return -1;
        }
        if (pts[i].x_px < m->g.vp_x0 || pts[i].x_px > m->g.vp_x1 ||
            pts[i].y_px < m->g.vp_y0 || pts[i].y_px > m->g.vp_y1)
            over++;
    }
    s_grid_over += over;

    /* The walk's pairing, once for the run.  A 0 answer clears the stream's plane and the
     * `texmiss` demotion below is what keeps a textured primitive from reading the page `TEX0`
     * happens to hold -- the identical two moves `gl_draw_triangle_fill()` makes per triangle. */
    eegs_texture(gs_tex_find(m->pixmap));

    st.smooth   = m->g.smooth ? 1u : 0u;
    st.textured = 1u;                    /* the caller's state: the map required the sheet */
    st.blended  = m->g.blended ? 1u : 0u;

    for (q = 0; q < nquads; q++) {
        const tgl_tex_pt_t *p = &pts[q * 4];
        eegs_vertex_t       v[3];
        int                 k;

        /* vertex.c:270-278's own split: (v0,v1,v2) then (v0,v2,v3).  An edge_flag gymnastics the
         * walk wraps around it does not reach the GS -- the quad's four colours are the quad's
         * four colours whichever triangle carries them, and flat vs smooth is decided by the
         * lifted `smooth`, as the walk decides it. */
        for (k = 0; k < 3; k++) {
            const tgl_tex_pt_t *s = (k == 0) ? &p[0] : (k == 1) ? &p[1] : &p[2];
            v[k].x_px = s->x_px;
            v[k].y_px = s->y_px;
            v[k].s    = s->s;
            v[k].t    = s->t;
            v[k].rgba = s->rgba;
        }
        eegs_triangle(v, st);

        v[1].x_px = p[2].x_px; v[1].y_px = p[2].y_px;
        v[1].s = p[2].s; v[1].t = p[2].t; v[1].rgba = p[2].rgba;
        v[2].x_px = p[3].x_px; v[2].y_px = p[3].y_px;
        v[2].s = p[3].s; v[2].t = p[3].t; v[2].rgba = p[3].rgba;
        eegs_triangle(v, st);
    }

    s_grid_batch++;
    return nquads * 2;                   /* triangles, the unit the strip entry returned vertices
                                          * in because it submits a vertex stream; this one submits
                                          * through `eegs_triangle()` and counts prims there. */
}

void eegs_line(const eegs_vertex_t v[2], const eegs_tri_state_t st)
{
    select_prim(prim_word(GS_OP_LINE, st));
    emit_vertex(&v[0]);
    emit_vertex(&v[1]);
    s_prims++;
}

void eegs_point(const eegs_vertex_t v[1], const eegs_tri_state_t st)
{
    select_prim(prim_word(GS_OP_POINT, st));
    emit_vertex(&v[0]);
    s_prims++;
}

uint32_t eegs_prims(void) { return s_prims; }
uint32_t eegs_verts(void) { return gs_rec_verts(); }
uint32_t eegs_grid_batch(void) { return s_grid_batch; }
uint32_t eegs_grid_slow(void) { return s_grid_slow; }
uint32_t eegs_grid_why(void) { return s_grid_why; }
uint32_t eegs_grid_over(void) { return s_grid_over; }
uint32_t eegs_tex_switches(void) { return s_tex_switches; }
uint32_t eegs_tex_missing(void) { return s_tex_missing; }
uint32_t eegs_prim_moves(void) { return s_prim_moves; }
