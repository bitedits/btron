/*
 * gs_group_view.h — reading a captured GIF group in the GIF's own unit
 *
 * Shared by the two suites that compile ps2_gs_rec.c (test_ps2_gs_rec.c and test_eegs_raster.c),
 * because the law they both have to apply is one law and not two: a GIF quadword is 128 bits and a
 * packed GS register is 64 of them, so a captured group is a stream of *pairs* of uint64_t.  What the
 * two halves of a pair mean is set by the group's own tag, and there are two kinds in here.  A VERTEX
 * group is a dense `FLG=REGLIST`, so each 128-bit unit carries TWO registers of the REGS list, the
 * lower-numbered one in the low half, and a vertex is GS_REC_QW_PER_VERT quadwords.  An AD group is
 * `FLG=PACKED` with NREG=1, so its low half is the register's value and its high half is that
 * register's GP offset, and one register is one whole quadword -- the shape `send_trx_setup()` in
 * ps2_gs.c proves on the machine when it sends a tag and four AD registers as five quadwords with
 * NLOOP=4, NREG=1.
 *
 * Both are one instance of the same law, which is that a group carries NLOOP * NREG *registers*;
 * what a register costs in quadwords is the two formats' difference, and it is the reason this file
 * indexes by halves as well as by quadwords.
 *
 * The reason this is a header rather than a paragraph repeated per file is the history: the
 * recorder first shipped storing one 64-bit word per register and handing that count to the DMA as
 * a quadword count, and *both* suites stayed green, because each had independently written its
 * index arithmetic in words.  Half the bytes on the bus and every second register routed to the
 * wrong place is not something a test can assert twice; it is something one statement of the unit
 * can.
 *
 * Include this after ps2_gs_rec.c (or .h), for GS_REC_REGS_N.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef GS_GROUP_VIEW_H
#define GS_GROUP_VIEW_H

#include <stdint.h>

/* Quadword k of a captured group: its LOW half.  A group is a stream of 128-bit units, so `k` is in
 * the unit the DMA counts and the answer is a 64-bit register only when the group's format puts one
 * there alone. */
static inline uint64_t gs_qw(const uint64_t *g, uint32_t k)
{
    return g[2u * k];
}

/* Half `h` of quadword `k` -- 0 low, 1 high.  This is the accessor a dense REGLIST group needs and a
 * sparse one never did: under REGLIST the two registers a REGS list names in order sit in the same
 * quadword, so `gs_qw()` alone cannot reach the second of them. */
static inline uint64_t gs_half(const uint64_t *g, uint32_t k, uint32_t h)
{
    return g[2u * k + h];
}

/* The group's REGS nibble list, which lives in the tag quadword's upper half. */
static inline uint64_t gs_regs_of(const uint64_t *g)
{
    return g[1u];
}

/* The upper half of quadword k: the next register of the loop for a dense REGLIST group, the GP
 * offset for an AD write. */
static inline uint64_t gs_upper_of(const uint64_t *g, uint32_t k)
{
    return gs_half(g, k, 1u);
}

/* The GIF tag's own fields, read where ps2_gs_pack.c writes them: NLOOP[14:0], EOP[15],
 * PRE[46], PRIM[57:47], FLG[59:58], NREG[63:60].  These are for a *decoder* -- something that has
 * to say which primitive a group asks for and how many vertices it carries without restating the
 * encoder's arithmetic -- and they are the same field positions the hand literals in these suites
 * were written from. */
static inline uint32_t gs_nloop_of(const uint64_t *g)
{
    return (uint32_t)(gs_qw(g, 0u) & 0x7FFFu);
}

static inline uint32_t gs_eop_of(const uint64_t *g)
{
    return (uint32_t)((gs_qw(g, 0u) >> 15) & 1u);
}

static inline uint32_t gs_pre_of(const uint64_t *g)
{
    return (uint32_t)((gs_qw(g, 0u) >> 46) & 1u);
}

/* FLG[59:58]: 0 = PACKED (one register per quadword), 1 = REGLIST (the list packed end to end).  The
 * suites read it because the recorder's whole format claim lives in this field and in the stride it
 * implies -- a group that says REGLIST while its registers are spaced out is a group whose second
 * register lands in the third one's place. */
static inline uint32_t gs_flg_of(const uint64_t *g)
{
    return (uint32_t)((gs_qw(g, 0u) >> 58) & 3u);
}

static inline uint32_t gs_prim_of(const uint64_t *g)
{
    return (uint32_t)((gs_qw(g, 0u) >> 47) & 0x7FFu);
}

/* OP[2:0] of the group's PRIM: GS_OP_TRI, GS_OP_TRISTP, ... The rest of PRIM is the state, which a
 * group-level reader cannot separate, so it stays available as `gs_prim_of()`. */
static inline uint32_t gs_op_of(const uint64_t *g)
{
    return gs_prim_of(g) & 7u;
}

static inline uint32_t gs_nreg_of(const uint64_t *g)
{
    return (uint32_t)(gs_qw(g, 0u) >> 60);
}

/* Register `reg` (0 = PRIM, 1 = RGBAQ, 2 = ST, 3 = XYZ2) of vertex `v` (1-based) of a VERTEX group:
 * the tag, then GS_REC_QW_PER_VERT quadwords per vertex with the REGS list packed two registers to
 * each, so a register lives at quadword 1 + per-vertex * (v-1) + reg/2, half `reg & 1` of it.  Under
 * the sparse PACKED shape this file used to emit, reg/2 was always reg and the half was always low --
 * which is exactly why the expression is written from the register number and not from a literal. */
static inline uint64_t gs_vq(const uint64_t *g, uint32_t v, uint32_t reg)
{
    return gs_half(g, 1u + GS_REC_QW_PER_VERT * (v - 1u) + (reg >> 1u), reg & 1u);
}

/* One whole vertex compared against another, possibly in another group: the four registers a
 * re-emitted continuation has to reproduce exactly. */
static inline int gs_vertex_equal(const uint64_t *ga, uint32_t va,
                                  const uint64_t *gb, uint32_t vb)
{
    uint32_t i;

    for (i = 0; i < GS_REC_REGS_N; i++) {
        if (gs_vq(ga, va, i) != gs_vq(gb, vb, i)) return 0;
    }
    return 1;
}

/* The size law the rest of both suites derives from: one tag quadword plus NLOOP * NREG registers,
 * and a vertex group's NREG registers are two to a quadword.  The AD form is that law with NREG=1 and
 * one register per quadword, which is why gs_rec_ad()'s count is GS_REC_AD_QWC(n) in ps2_gs_rec.h and
 * not a second arithmetic here. */
static inline uint32_t gs_group_qw(uint32_t nloop)
{
    return 1u + nloop * GS_REC_QW_PER_VERT;
}

/* The copy a capture sink owes the recorder: qwc quadwords is twice that many uint64_t. */
#define GS_QW_BYTES(qwc) ((size_t)(qwc) * 2u * sizeof(uint64_t))

#endif /* GS_GROUP_VIEW_H */
