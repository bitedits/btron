/*
 * gs_group_view.h — reading a captured GIF group in the GIF's own unit
 *
 * Shared by the suites that compile ps2_gs_rec.c (test_ps2_gs_rec.c and test_eegs_raster.c),
 * because the law they both have to apply is one law and not two: a GIF quadword is 128 bits and a
 * packed GS register is 64 of them, so a captured group is a stream of *pairs* of uint64_t.  What the
 * two halves of a pair mean is set by the group's own tag, and there are two kinds in here.  A VERTEX
 * group is a dense `FLG=REGLIST`, so each 128-bit unit carries TWO registers of the REGS list, the
 * lower-numbered one in the low half, and a register count the group declares in its own tag: since
 * LANE 19 rung 12f the recorder writes two registers per vertex untextured and three textured, so
 * nothing in this file may assume either number.  An AD group is `FLG=PACKED` with NREG=1, so its low
 * half is the register's value and its high half is that
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
 * Include this after ps2_gs_rec.c (or .h), for GS_REC_REGS_MAX and the two NREG values.
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
 * quadword, so `gs_qw()` alone cannot reach the second of them.
 *
 * The recorder's own half numbering starts at the stream's first half, so where this file says
 * "quadword k" a dense group's k-th 128-bit unit is halves 2k and 2k+1 -- and the header pair (the
 * tag, then its REGS list) is quadword 0 of that numbering. */
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

/* Position `i` of vertex `v` (1-based) of a VERTEX group, counting through the group's own REGS list:
 * the tag pair occupies halves 0 and 1, and from half 2 the data is NLOOP * NREG registers end to end
 * with no gaps, so vertex v's i-th register is half 2 + (v-1)*NREG + i.  Positional because it is the
 * stream's own arithmetic; every reader that cares about a register by NAME goes through gs_vq(). */
static inline uint64_t gs_vpos(const uint64_t *g, uint32_t v, uint32_t i)
{
    return g[2u + (v - 1u) * gs_nreg_of(g) + i];
}

/* The REGS list's nibble for position `i`. */
static inline uint32_t gs_reg_nib(const uint64_t *g, uint32_t i)
{
    return (uint32_t)((gs_regs_of(g) >> (4u * i)) & 0xFu);
}

/* Register `nib` -- a GS_NIB_* value, a NAME and not a position -- of vertex `v` of a VERTEX group.
 *
 * Named by nibble because since LANE 19 rung 12f a recorder group carries two registers when its
 * primitive is untextured and three when it is textured, and RGBAQ is position 0 of one and position
 * 0 of the other while XYZ2 is position 1 of one and position 2 of the other.  An index into a list
 * whose length the group decides is not a name: it is the reason this file used to be able to read a
 * group wrongly and still print a number.  A register the group does not name reads back as all-ones,
 * which no colour, texture coordinate or vertex is, so a suite that asks an untextured group for its
 * ST fails its own check instead of passing on a zero it mistook for a default. */
static inline uint64_t gs_vq(const uint64_t *g, uint32_t v, uint32_t nib)
{
    const uint32_t nreg = gs_nreg_of(g);
    uint32_t i;

    for (i = 0; i < nreg; i++) {
        if (gs_reg_nib(g, i) == nib) return gs_vpos(g, v, i);
    }
    return ~0ULL;
}

/* One whole vertex compared against another, possibly in another group: the registers a re-emitted
 * continuation has to reproduce exactly.  Two groups of different widths share no vertex, because
 * "the same vertex" would then mean a different number of words on each side. */
static inline int gs_vertex_equal(const uint64_t *ga, uint32_t va,
                                  const uint64_t *gb, uint32_t vb)
{
    const uint32_t na = gs_nreg_of(ga), nb = gs_nreg_of(gb);
    uint32_t i;

    if (na != nb) return 0;
    for (i = 0; i < na; i++) {
        if (gs_vpos(ga, va, i) != gs_vpos(gb, vb, i)) return 0;
    }
    return 1;
}

/* The size law the rest of both suites derives from: one tag quadword plus NLOOP * NREG registers,
 * and a dense group's registers are two to a quadword -- with one odd register padded, because the
 * DMA moves whole quadwords and the half past a group's last register is otherwise whatever the
 * buffer held before it.  NREG is a parameter and not a constant: the recorder's two lists are two
 * and three wide, and the count the sink gets has to come from the list the group declared.  The AD
 * form is that law with NREG=1 and one register per quadword, which is why gs_rec_ad()'s count is
 * GS_REC_AD_QWC(n) in ps2_gs_rec.h and not a second arithmetic here. */
static inline uint32_t gs_group_qw(uint32_t nloop, uint32_t nreg)
{
    const uint32_t halves = 2u + nloop * nreg;
    return (halves + 1u) / 2u;
}

/* The copy a capture sink owes the recorder: qwc quadwords is twice that many uint64_t. */
#define GS_QW_BYTES(qwc) ((size_t)(qwc) * 2u * sizeof(uint64_t))

#endif /* GS_GROUP_VIEW_H */
