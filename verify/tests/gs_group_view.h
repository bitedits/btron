/*
 * gs_group_view.h — reading a captured GIF group in the GIF's own unit
 *
 * Shared by the two suites that compile ps2_gs_rec.c (test_ps2_gs_rec.c and test_eegs_raster.c),
 * because the law they both have to apply is one law and not two: a GIF quadword is 128 bits and a
 * packed GS register is 64 of them, so a captured group is a stream of *pairs* of uint64_t -- the
 * register's value in the low half of its pair, and in the high half either nothing (a packed
 * register) or the GP register offset (the AD form, the one case where the upper half means
 * something).  A group is therefore one tag quadword plus NLOOP * NREG data quadwords, which is
 * what `send_trx_setup()` in ps2_gs.c proves on the machine when it sends a tag and four AD
 * registers as five quadwords with NLOOP=4, NREG=1.
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

/* Quadword k of a captured group. */
static inline uint64_t gs_qw(const uint64_t *g, uint32_t k)
{
    return g[2u * k];
}

/* The group's REGS nibble list, which lives in the tag quadword's upper half. */
static inline uint64_t gs_regs_of(const uint64_t *g)
{
    return g[1u];
}

/* The upper half of quadword k: padding for a packed register, the GP offset for an AD write. */
static inline uint64_t gs_upper_of(const uint64_t *g, uint32_t k)
{
    return g[2u * k + 1u];
}

/* Register `reg` (0 = PRIM, 1 = RGBAQ, 2 = ST, 3 = XYZ2) of vertex `v` (1-based) of a PACKED
 * vertex group: the tag, then GS_REC_REGS_N quadwords per vertex. */
static inline uint64_t gs_vq(const uint64_t *g, uint32_t v, uint32_t reg)
{
    return gs_qw(g, 1u + GS_REC_REGS_N * (v - 1u) + reg);
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

/* The size law the rest of both suites derives from: one tag quadword plus NLOOP * NREG data
 * quadwords.  The AD form is that law with NREG=1, which is why gs_rec_ad()'s count is
 * GS_REC_AD_QWC(n) in ps2_gs_rec.h and not a second arithmetic here. */
static inline uint32_t gs_group_qw(uint32_t nloop)
{
    return 1u + nloop * GS_REC_REGS_N;
}

/* The copy a capture sink owes the recorder: qwc quadwords is twice that many uint64_t. */
#define GS_QW_BYTES(qwc) ((size_t)(qwc) * 2u * sizeof(uint64_t))

#endif /* GS_GROUP_VIEW_H */
