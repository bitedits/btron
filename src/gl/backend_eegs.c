/*
 * src/gl/backend_eegs.c -- the three vertices a clipped triangle becomes, as a GIF group, and
 * the texture switch that goes between them
 *
 * Row 5, steps 5a and 5b of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Everything
 * hard about the GS is in increments 2, 3 and 4, which this file calls and does not duplicate: the
 * field encodings are `ps2_gs_pack.c`'s, the environment is `ps2_gs_env.c`'s, and the group state
 * machine -- NLOOP, the REGS list, where a full buffer is cut and which vertices a continued strip
 * needs -- is `ps2_gs_rec.c`'s.  What is here is the two decisions the raster stage makes: which
 * primitive and which three state flags a triangle carries, and what a device-space vertex means
 * to the GS.  From 5b a third: when the plane a triangle samples is not the plane the stream
 * already points at, the three texture registers go in as a group of their own.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "backend_eegs.h"

#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_rec.h"

/* The texture context this port programs is context 1, the one the increment-3 environment stream
 * writes TEX0/TEX1/CLAMP for; context 2 would need a second page plan, and no layer has two
 * textures at once. */
#define EEGS_CONTEXT 1u

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

static uint64_t prim_word(uint32_t op, const eegs_tri_state_t st)
{
    return gs_pack_prim(op, st.smooth ? 1u : 0u, st.textured ? 1u : 0u,
                        st.blended ? 1u : 0u, EEGS_CONTEXT, 0u);
}

/* gs_rec_prim() closes whatever group is open, which is right for a real state change and wrong
 * for a repetition: a ribbon's 1,828 triangles all ask for the same PRIM, and re-asking would be
 * 1,828 groups and 1,828 DMA starts of three vertices each.  So the word is compared, not re-sent,
 * and the group stays open until the state genuinely moves. */
static void select_prim(uint64_t prim)
{
    if (s_prim_valid && s_prim_live == prim) return;

    gs_rec_prim(prim);
    s_prim_live = prim;
    s_prim_valid = 1;
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

void eegs_triangle(const eegs_vertex_t v[3], eegs_tri_state_t st)
{
    st = need_plane(st);
    select_prim(prim_word(GS_OP_TRI, st));
    emit_vertex(&v[0]);
    emit_vertex(&v[1]);
    emit_vertex(&v[2]);
    s_prims++;
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
uint32_t eegs_tex_switches(void) { return s_tex_switches; }
uint32_t eegs_tex_missing(void) { return s_tex_missing; }
