/*
 * src/gl/backend_eegs.c -- the three vertices a clipped triangle becomes, as a GIF group
 *
 * Row 5, step 5a of the plan of record (doc/md/PS2.md, "The EE/GS backend").  Everything hard
 * about the GS is in increments 2, 3 and 4, which this file calls and does not duplicate: the
 * field encodings are `ps2_gs_pack.c`'s, the environment is `ps2_gs_env.c`'s, and the group state
 * machine -- NLOOP, the REGS list, where a full buffer is cut and which vertices a continued
 * strip needs -- is `ps2_gs_rec.c`'s.  What is here is the one decision the raster stage makes:
 * which primitive and which three state flags a triangle carries, and what a device-space vertex
 * means to the GS.
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
}

void eegs_frame_end(void)
{
    gs_rec_end();
    s_prim_valid = 0;
}

void eegs_triangle(const eegs_vertex_t v[3], const eegs_tri_state_t st)
{
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
