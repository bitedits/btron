/*
 * src/gl/backend_eegs.c -- PlayStation 2 GS (Graphics Synthesizer) rasterizer backend:
 * vertex packaging, GIF packet groups, and texture plane state switching.
 *
 * Implementation Details:
 * Connects TinyGL's geometry pipeline to the Sony PS2 Graphics Synthesizer (GS).
 * Performs two key tasks:
 * 1. Encodes primitives and device-space vertices into GIF packets (`ps2_gs_rec.c`).
 * 2. Emits texture plane configuration registers (`ps2_gs_pack.c`) dynamically when switching planes.
 *
 * Retained meshes are submitted as single PRIM strips or quad runs (`eegs_grid_strip()`),
 * bypassing immediate-mode looping.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "backend_eegs.h"

#include "grid_map.h"                       /* TGL_GRID_REFUSE_*: the `why=` codes */

#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_rec.h"

/* The register set this port's primitives are drawn with, as the PRIM word's CTXT bit.
 *
 * Hardware Register Context Mapping:
 * The GS provides two register contexts (Context 1 and Context 2).
 * Environment registers emitted by `ps2_gs_env.c` target Context 1:
 * GS_GP_FRAME_1, GS_GP_SCISSOR_1, GS_GP_ALPHA_1, GS_GP_XYOFFSET_1.
 * In the GS GIF register addressing table, Context 1 registers are paired with index 0
 * (e.g. GS_REG_FRAME_1 is 4Ch, with CTXT=0).
 *
 * Thus, Context 1 corresponds to PRIM CTXT=0.
 * PRIM CTXT=1 routes to Context 2, which is configured for destination-brighten blending.
 */
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

/* Destination-brighten blending mode flag:
 * Set when the active PRIM word requests Context 2 (CTXT=1) destination-brighten blending.
 *
 * Evaluates the hardware equation:
 *     Color = (Cd - 0) * As >> 7 + Cd
 *
 * Because this equation modulates by the scalar alpha (As) rather than vertex RGB,
 * `emit_vertex()` relocates the vertex luminance into the alpha channel.
 */
static uint32_t s_brighten_live;

static const gs_tex_t *s_tex_live;
static uint32_t s_tex_switches;
static uint32_t s_tex_missing;
static int      s_depth_test_active;

/* Vertex Colour Transition Diagnostics:
 * Tracks whether the stream contains varying vertex colours or a uniform flat colour.
 *
 * Compares each vertex colour against the previous vertex. For a gradient or dynamic wave
 * lattice, `colchg` scales with `verts`. A flat-shaded stream reports 0 transitions.
 * This diagnostic quickly identifies whether colour loss originates in the software pipeline
 * or in hardware rasterizer shading flags (IIP bit in PRIM).
 */
static uint32_t s_colour_prev;
static uint32_t s_colour_first;
static uint32_t s_colour_last;
static uint32_t s_colour_changes;
static uint32_t s_colour_verts;

/* PRIM Shading Mode Diagnostics:
 * Measures how many declared PRIM words requested Gouraud shading (IIP bit 3)
 * or texture mapping (TME bit 4). Verified against total declarations (`s_prim_decl`).
 */
static uint32_t s_prim_iip;
static uint32_t s_prim_tme;
static uint32_t s_prim_decl;

/* The frame's colour census, taken at the one place every vertex's colour passes: the RGBAQ word
 * is built from `v->rgba` here, so a colour that never reaches this line is not in the stream at
 * all, whatever the application asked for. */
static void count_colour(uint32_t rgba)
{
    if (s_colour_verts == 0u) s_colour_first = rgba;
    if (s_colour_verts != 0u && rgba != s_colour_prev) s_colour_changes++;
    s_colour_prev = rgba;
    s_colour_last = rgba;
    s_colour_verts++;
}

/* Large Triangle & Geometric Area Diagnostics:
 * Distinguishes large full-screen backdrop / gradient quads from small UI elements by area.
 *
 * Evaluates the cross-product of edge vectors to compute doubled area.
 * `wide` counts triangles exceeding `EEGS_WIDE_TRI_PX` (doubled area >= 32,768).
 * `wideflat` counts large triangles with uniform vertex colors.
 * `big_*` records the single widest triangle: its doubled area, corner colors, and smooth flag.
 */
#define EEGS_WIDE_TRI_PX 32768u    /* doubled area threshold: 256x128 pixel wedge */

static uint32_t s_wide_tri;
static uint32_t s_wide_flat;
static uint32_t s_big_area;
static uint32_t s_big_c0, s_big_c1, s_big_c2;
static uint32_t s_big_smooth;

static void count_wide(const eegs_vertex_t *a, const eegs_vertex_t *b, const eegs_vertex_t *c,
                       uint32_t smooth)
{
    const int32_t ux = b->x_px - a->x_px, uy = b->y_px - a->y_px;
    const int32_t vx = c->x_px - a->x_px, vy = c->y_px - a->y_px;
    int32_t cross = ux * vy - uy * vx;
    uint32_t area2;

    if (cross < 0) cross = -cross;
    area2 = (uint32_t)cross;
    if (area2 < EEGS_WIDE_TRI_PX) return;

    s_wide_tri++;
    if (a->rgba == b->rgba && b->rgba == c->rgba) s_wide_flat++;
    if (area2 <= s_big_area) return;

    s_big_area = area2;
    s_big_c0 = a->rgba;
    s_big_c1 = b->rgba;
    s_big_c2 = c->rgba;
    s_big_smooth = smooth;
}

/* PRIM Re-declaration & GIF Group Boundary Rate:
 * Counts transitions where `select_prim()` emits a distinct PRIM word, closing the active group.
 * Monitored against `EEGS_MAX_GROUPS` (256) to ensure the stage buffer is never overrun.
 */
static uint32_t s_prim_moves;

/* Retained Mesh Execution Metrics:
 * - `s_grid_batch`: Meshes batched and submitted directly as continuous strips or quad runs.
 * - `s_grid_slow`: Meshes refused (falling back to immediate-mode walk).
 * - `s_grid_why`: Reason code for refusal (`TGL_GRID_REFUSE_*` in `grid_map.h`).
 * - `s_grid_over`: Count of points lying outside the viewport rect that are scissored by hardware.
 */
static uint32_t s_grid_batch;
static uint32_t s_grid_slow;
static uint32_t s_grid_why;
static uint32_t s_grid_over;

/* Dual-Context Blending Support:
 * Context 1 provides standard source-over blending: (Cs - Cd) * As >> 7 + Cd.
 * Context 2 provides destination-brighten blending: (Cd - 0) * As >> 7 + Cd.
 * Enabled by default to support both blending equations concurrently across drawing passes.
 */
static int      s_dual_context_en = 1;

void eegs_enable_dual_context(int enable)
{
    s_dual_context_en = enable ? 1 : 0;
}

int eegs_is_dual_context(void)
{
    return s_dual_context_en;
}

/* PRIM Chain Record Tracking:
 * Records drawing layers partitioned by distinct PRIM declarations.
 *
 * Each chain captures the PRIM state, vertex and primitive counts, bounding box,
 * and first/last vertex colors. Bounding box initializes with `l > r` to represent an empty chain.
 */
#define EEGS_CHAIN_EMPTY ((int32_t)0x7FFFFFFF)

static eegs_chain_rec s_chain[EEGS_CHAIN_MAX];
static eegs_chain_rec s_chain_open;
static uint32_t s_chain_n;
static uint32_t s_chain_drop;
static uint32_t s_chain_live;

/* Diagnostic Chain Limit:
 * Restricts rendering to the first N chain declarations (0..N-1), discarding subsequent commands.
 * `s_chain_refusing` tracks whether the active chain exceeds the cutoff limit.
 */
static uint32_t s_chain_limit;
static uint32_t s_chain_opened;
static uint32_t s_chain_refused;
static uint32_t s_chain_refusing;

void     eegs_chain_set_limit(uint32_t n) { s_chain_limit = n; s_chain_refusing = 0u; }
uint32_t eegs_chain_limit(void)  { return s_chain_limit; }
uint32_t eegs_chain_refused(void) { return s_chain_refused; }

static void chain_begin(uint64_t prim)
{
    if (s_chain_live) {
        if (s_chain_n < EEGS_CHAIN_MAX) s_chain[s_chain_n++] = s_chain_open;
        else s_chain_drop++;
    }
    s_chain_open.prim = (uint32_t)prim;
    s_chain_open.nverts = 0u;
    s_chain_open.nprims = 0u;
    s_chain_open.l = EEGS_CHAIN_EMPTY;
    s_chain_open.t = EEGS_CHAIN_EMPTY;
    s_chain_open.r = 0;
    s_chain_open.b = 0;
    s_chain_open.c0 = 0u;
    s_chain_open.clast = 0u;
    s_chain_live = 1u;
    s_chain_opened++;
}

static void chain_vertex(const eegs_vertex_t *v)
{
    if (!s_chain_live) return;
    if (s_chain_open.nverts == 0u) s_chain_open.c0 = v->rgba;
    s_chain_open.clast = v->rgba;
    if (s_chain_open.nverts == 0u || v->x_px < s_chain_open.l) s_chain_open.l = v->x_px;
    if (s_chain_open.nverts == 0u || v->x_px > s_chain_open.r) s_chain_open.r = v->x_px;
    if (s_chain_open.nverts == 0u || v->y_px < s_chain_open.t) s_chain_open.t = v->y_px;
    if (s_chain_open.nverts == 0u || v->y_px > s_chain_open.b) s_chain_open.b = v->y_px;
    s_chain_open.nverts++;
}

/* One primitive completed in the open chain.  Rung 24's flag is checked here rather than at the
 * three submitters because it is the chain's record that has to stay honest: a refused chain's
 * primitive counted into the last accepted one would print `verts=6 prims=2698`, and the reader
 * compares this column against `verts=` to decide whether the geometry arrived. */
static void chain_prim(void)
{
    if (s_chain_live && !s_chain_refusing) s_chain_open.nprims++;
}

uint32_t eegs_chain_count(void) { return s_chain_n; }
uint32_t eegs_chain_dropped(void) { return s_chain_drop; }

const eegs_chain_rec *eegs_chain_at(uint32_t i)
{
    return i < s_chain_n ? &s_chain[i] : 0;
}

static uint64_t prim_word(uint32_t op, const eegs_tri_state_t st)
{
    /* Blending Context Selection:
     * Context 1 provides standard source-over blending for UI icons and text.
     * Context 2 provides destination-brighten blending for translucent waves/ribbons.
     *
     * Textured primitives remain in Context 1 to ensure correct sampling from `TEX0_1`.
     * Untextured primitives requesting `brighten` are routed to Context 2 (CTXT=1).
     */
    uint32_t ctxt = EEGS_CONTEXT;
    if (s_dual_context_en && st.brighten && !st.textured) {
        ctxt = 1u;
    }
    return gs_pack_prim(op, st.smooth ? 1u : 0u, st.textured ? 1u : 0u,
                        st.blended ? 1u : 0u, ctxt, 0u);
}

/* PRIM Word State Caching:
 * Closes the active group only when the PRIM word genuinely changes.
 * Avoids starting redundant DMA transfers when consecutive primitives share state.
 */
static void select_prim(uint64_t prim)
{
    /* Update brighten state from the PRIM word (ABE bit 6 and CTXT bit 9) */
    s_brighten_live = ((prim & (1ULL << 6)) != 0u && (prim & (1ULL << 9)) != 0u) ? 1u : 0u;

    if (s_prim_valid && s_prim_live == prim)
        return;

    /* Record shading mode diagnostics (IIP = Gouraud, TME = Texture Enable) */
    if (prim & (1ULL << 3)) s_prim_iip++;
    if (prim & (1ULL << 4)) s_prim_tme++;

    /* Enforce diagnostic cutoff limit */
    if (s_chain_limit != 0u && s_chain_opened >= s_chain_limit) {
        s_chain_refused++;
        s_chain_refusing = 1u;
        return;
    }
    s_prim_decl++;

    chain_begin(prim);
    gs_rec_prim(prim);
    s_prim_live = prim;
    s_prim_valid = 1;
    s_prim_moves++;
}

/* Vertex Packaging:
 * Emits vertex colour, texture coordinates, and XYZ2 position into the active GIF group.
 * The register order follows the open group's REGS sequence (RGBAQ, then ST, then XYZ2 kick).
 */
static void emit_vertex(const eegs_vertex_t *v)
{
    float s = v->s;
    float t = v->t;
    uint32_t rgba;

    if (s_chain_refusing) return;
    chain_vertex(v);
    rgba = v->rgba;

    /* In Context 2 destination-brighten mode:
     *     Color = (Cd - 0) * As >> 7 + Cd
     *
     * The hardware modulates destination colour by scalar alpha (As = b * 128 / 255).
     * Relocate maximum colour luminance into As so that the hardware computes the intended brightness.
     */
    if (s_brighten_live) {
        uint32_t r = rgba & 0xFFu;
        uint32_t g = (rgba >> 8) & 0xFFu;
        uint32_t b = (rgba >> 16) & 0xFFu;
        uint32_t max = r > g ? r : g;
        if (b > max) max = b;
        rgba = (rgba & 0x00FFFFFFu) | ((uint32_t)((max * 128u + 127u) / 255u) << 24);
    }

    if (s_tex_live != 0 && (s_tex_live->tw_exp != 0 || s_tex_live->th_exp != 0)) {
        s = gs_float_scale_pow2(s, s_tex_live->tw_exp);
        t = gs_float_scale_pow2(t, s_tex_live->th_exp);
    }
    gs_rec_color(rgba & 0xFFu, (rgba >> 8) & 0xFFu,
                 (rgba >> 16) & 0xFFu, (rgba >> 24) & 0xFFu);
    count_colour(rgba);
    gs_rec_texcoord(s, t);
    gs_rec_vertex(v->x_px, v->y_px, v->z);
}

void eegs_frame_begin(gs_rec_sink sink, void *user)
{
    gs_rec_init(sink, user);
    s_prim_valid = 0;
    s_prim_live = 0u;
    s_brighten_live = 0u;
    s_prims = 0u;
    s_tex_live = 0;
    s_tex_switches = 0u;
    s_tex_missing = 0u;
    s_prim_moves = 0u;
    s_colour_prev = s_colour_first = s_colour_last = 0u;
    s_colour_changes = s_colour_verts = 0u;
    s_prim_iip = s_prim_tme = s_prim_decl = 0u;
    s_wide_tri = s_wide_flat = s_big_area = 0u;
    s_big_c0 = s_big_c1 = s_big_c2 = s_big_smooth = 0u;
    s_grid_batch = 0u;
    s_grid_slow = 0u;
    s_grid_why = TGL_GRID_REFUSE_NONE;
    s_grid_over = 0u;
    s_depth_test_active = 0;
    s_chain_n = 0u;
    s_chain_drop = 0u;
    s_chain_live = 0u;
    s_chain_opened = 0u;
    s_chain_refused = 0u;
    s_chain_refusing = 0u;
}

void eegs_frame_end(void)
{
    gs_rec_end();
    /* The last chain has no following declaration to close it, and the present calls this before it
     * hands the staged groups to the GIF, so a reader at the [GSTS] row sees every chain of the frame
     * it is describing -- including this one. */
    if (s_chain_live) {
        if (s_chain_n < EEGS_CHAIN_MAX) s_chain[s_chain_n++] = s_chain_open;
        else s_chain_drop++;
        s_chain_live = 0u;
    }
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
    if (st.depth_test) s_depth_test_active = 1;
    st = need_plane(st);
    count_wide(&v[0], &v[1], &v[2], st.smooth);
    select_prim(prim_word(GS_OP_TRI, st));
    emit_vertex(&v[0]);
    emit_vertex(&v[1]);
    emit_vertex(&v[2]);
    s_prims++;
    chain_prim();
}

/* One mapped mesh point, in the unit the recorder takes.  `s`/`t` are 0 because the map this call
 * consumes refuses a context with a texture enabled -- and 0 is what `gs_vertex()` puts in an
 * untextured vertex, so the ST register this writes is the ST register the walk would have written. */
static void emit_point(const tgl_grid_pt_t *p)
{
    eegs_vertex_t v;

    v.x_px = p->x_px;
    v.y_px = p->y_px;
    v.z = 0u;
    v.s = 0.0f;
    v.t = 0.0f;
    v.rgba = p->rgba;
    emit_vertex(&v);
}

/* Retained Mesh Triangle Strip Submission:
 * Submits a precomputed vertex mesh (`rows` x `cols`) as a continuous zigzag triangle strip.
 *
 * Algorithm:
 * Traverses row pairs down columns, reversing odd rows to maintain winding order without
 * breaking the strip. The recorder (`ps2_gs_rec.c`) manages buffer partitioning and
 * strip continuation vertices (`reemit_continuation()`).
 *
 * Hardware Scissoring & Coordinate Range Validation:
 * Rather than clipping triangles in software, validates that all mesh points lie within
 * the hardware coordinate range (-2048 .. +2047 px, `EEGS_GRID_XY_LIMIT`).
 * Points outside the viewport are scissored in hardware by the GS SCISSOR register.
 * Any point outside the viewport is tallied in `over` to track hardware scissoring utilization.
 */
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
        s_grid_slow++;
        s_grid_why = m->refuse ? m->refuse : TGL_GRID_REFUSE_UNSET;
        return -1;
    }

    /* Validate that all mesh points fit within the 12.4 fixed-point vertex field
     * (-2048 .. +2047 px about the page origin). Points outside this range cannot be
     * represented in XYZ2 and must be refused. Points within range but outside the
     * active viewport are counted in `over` and clipped by hardware SCISSOR.
     */
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

    /* Clear texture binding for untextured strip */
    eegs_texture(0);

    st.smooth = m->smooth ? 1u : 0u;
    st.textured = 0u;
    st.blended = m->blended ? 1u : 0u;
    st.brighten = m->brighten ? 1u : 0u;
    st.depth_test = 0u;

    select_prim(prim_word(GS_OP_TRISTP, st));

    for (r = 0; r < rows - 1; r++) {
        for (c = 0; c < cols; c++) {
            const int col = (r & 1) ? (cols - 1 - c) : c;

            emit_point(&pts[r * cols + col]);
            emit_point(&pts[(r + 1) * cols + col]);
            submitted += 2;
        }
    }

    /* A strip of n vertices generates n - 2 triangles */
    if (submitted >= 3) s_prims += (uint32_t)(submitted - 2);
    if (s_chain_live && submitted >= 3) s_chain_open.nprims += (uint32_t)(submitted - 2);
    s_grid_batch++;
    return submitted;
}

/* Retained Textured Mesh Quad Run:
 * Submits an array of textured quads (`nquads` groups of four mapped points),
 * decomposing each quad into two triangles (v0, v1, v2) and (v0, v2, v3).
 *
 * Resolves the texture plane binding once for the entire batch.
 * Applies coordinate range validation and hardware scissoring identically to `eegs_grid_strip()`.
 */
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

    /* Validate 12.4 fixed-point coordinate bounds and accumulate hardware scissoring count */
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

    /* Bind texture plane for the quad batch */
    eegs_texture(gs_tex_find(m->pixmap));

    st.smooth   = m->g.smooth ? 1u : 0u;
    st.textured = 1u;
    st.blended  = m->g.blended ? 1u : 0u;
    st.brighten = m->g.brighten ? 1u : 0u;
    st.depth_test = 0u;

    for (q = 0; q < nquads; q++) {
        const tgl_tex_pt_t *p = &pts[q * 4];
        eegs_vertex_t       v[3];
        int                 k;

        /* Quad decomposition: (v0, v1, v2) then (v0, v2, v3) */
        for (k = 0; k < 3; k++) {
            const tgl_tex_pt_t *s = (k == 0) ? &p[0] : (k == 1) ? &p[1] : &p[2];
            v[k].x_px = s->x_px;
            v[k].y_px = s->y_px;
            v[k].z    = 0u;
            v[k].s    = s->s;
            v[k].t    = s->t;
            v[k].rgba = s->rgba;
        }
        eegs_triangle(v, st);

        v[1].x_px = p[2].x_px; v[1].y_px = p[2].y_px; v[1].z = 0u;
        v[1].s = p[2].s; v[1].t = p[2].t; v[1].rgba = p[2].rgba;
        v[2].x_px = p[3].x_px; v[2].y_px = p[3].y_px; v[2].z = 0u;
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
    if (st.depth_test) s_depth_test_active = 1;
    select_prim(prim_word(GS_OP_LINE, st));
    emit_vertex(&v[0]);
    emit_vertex(&v[1]);
    s_prims++;
    chain_prim();
}

void eegs_point(const eegs_vertex_t v[1], const eegs_tri_state_t st)
{
    if (st.depth_test) s_depth_test_active = 1;
    select_prim(prim_word(GS_OP_POINT, st));
    emit_vertex(&v[0]);
    s_prims++;
    chain_prim();
}

void eegs_clear(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;
    st.smooth = 0u;
    st.textured = 0u;
    st.blended = 0u;
    st.brighten = 0u;                    /* a clear is a clear: it replaces, whatever the blend state */
    st.depth_test = 0u;

    /* (v0, v1, v2): (x0,y0) -> (x1,y0) -> (x1,y1) */
    v[0].x_px = x0; v[0].y_px = y0; v[0].z = 0u; v[0].s = 0.0f; v[0].t = 0.0f; v[0].rgba = rgba;
    v[1].x_px = x1; v[1].y_px = y0; v[1].z = 0u; v[1].s = 0.0f; v[1].t = 0.0f; v[1].rgba = rgba;
    v[2].x_px = x1; v[2].y_px = y1; v[2].z = 0u; v[2].s = 0.0f; v[2].t = 0.0f; v[2].rgba = rgba;
    eegs_triangle(v, st);

    /* (v0, v2, v3): (x0,y0) -> (x1,y1) -> (x0,y1) */
    v[0].x_px = x0; v[0].y_px = y0; v[0].z = 0u; v[0].s = 0.0f; v[0].t = 0.0f; v[0].rgba = rgba;
    v[1].x_px = x1; v[1].y_px = y1; v[1].z = 0u; v[1].s = 0.0f; v[1].t = 0.0f; v[1].rgba = rgba;
    v[2].x_px = x0; v[2].y_px = y1; v[2].z = 0u; v[2].s = 0.0f; v[2].t = 0.0f; v[2].rgba = rgba;
    eegs_triangle(v, st);
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
uint32_t eegs_colour_changes(void) { return s_colour_changes; }
uint32_t eegs_colour_first(void) { return s_colour_first; }
uint32_t eegs_colour_last(void) { return s_colour_last; }
uint32_t eegs_colour_verts(void) { return s_colour_verts; }
uint32_t eegs_prim_iip(void) { return s_prim_iip; }
uint32_t eegs_prim_tme(void) { return s_prim_tme; }
uint32_t eegs_prim_decl(void) { return s_prim_decl; }

uint32_t eegs_wide_tri(void)    { return s_wide_tri; }
uint32_t eegs_wide_flat(void)   { return s_wide_flat; }
uint32_t eegs_big_area(void)    { return s_big_area; }
uint32_t eegs_big_c0(void)      { return s_big_c0; }
uint32_t eegs_big_c1(void)      { return s_big_c1; }
uint32_t eegs_big_c2(void)      { return s_big_c2; }
uint32_t eegs_big_smooth(void)  { return s_big_smooth; }
int      eegs_depth_test_active(void) { return s_depth_test_active; }

void eegs_set_window_origin(int32_t win_x, int32_t win_y,
                            int32_t clip_x0, int32_t clip_y0,
                            int32_t clip_x1, int32_t clip_y1)
{
    static const uint32_t regs[4] = {
        GS_GP_XYOFFSET_1, GS_GP_SCISSOR_1,
        GS_GP_XYOFFSET_2, GS_GP_SCISSOR_2
    };
    uint64_t words[4];

    if (clip_x1 < clip_x0 || clip_y1 < clip_y0) return;

    words[0] = gs_pack_xyoffset(GS_XYOFFSET_PAGE - ((uint32_t)win_x << 4),
                                GS_XYOFFSET_PAGE - ((uint32_t)win_y << 4));
    words[1] = gs_pack_scissor((uint32_t)clip_x0, (uint32_t)clip_x1,
                               (uint32_t)clip_y0, (uint32_t)clip_y1);
    words[2] = words[0];
    words[3] = words[1];

    gs_rec_ad(words, regs, 4u);
    s_prim_valid = 0;
}
