/*
 * ps2_gs_poster.c — the flat-colour SPRITE batch, as words
 *
 * See ps2_gs_poster.h for what a poster is and for the two laws this file exists to enforce:
 * a poster's colour is a canvas word, and a poster's vertex is the recorder's vertex word -- the
 * same gs_vertex_coord16() that every recorded stream is built from, not a second encoding of it.
 * LANE 9b, doc/txt/PS2.txt "The lanes" row 9, the poster half.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_poster.h"

#include "ps2_gs_pack.h"

/* The largest corner coordinate the vertex field can hold: XYOFFSET sits the page at 2048 px and
 * the field is 12 bits signed, so 2047 px is the last corner that lands on this side of the page
 * and 2048 px wraps its own 16-bit field to 0 -- which is not off the edge of the screen, it is the
 * top-left corner.  gs_vertex_coord16() wraps identically, so the guard is deliberately stricter
 * than the shared encoder it otherwise mirrors: no canvas this port draws is 2048 px wide, and a
 * caller that asks for it is a caller that clipped nothing. */
#define GS_POSTER_MAX_PX 2047L

/* The REGS list: the primitive word, the flat colour, and the sprite's two corners in the same
 * loop.  ST's nibble (0x02) from the recorder's list is replaced by a second XYZ2 (0x05), because
 * a SPRITE is complete when its second drawing kick arrives and an ST in the list would be a
 * register write with no vertex to attach it to. */
static const uint32_t s_regs[GS_POSTER_REGS_N] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_XYZ2, GS_NIB_XYZ2
};

static uint32_t s_dropped;

/* Does this rect draw anything at all, and does it draw where the caller means?  A sprite whose
 * second corner is not strictly beyond its first rasterises no pixels, and a corner at or past the
 * page's own origin wraps to the far side of it. */
static int poster_rect_ok(const gs_poster_rect *r)
{
    if (r->x1 <= r->x0 || r->y1 <= r->y0) return 0;
    if (r->x0 < 0 || r->y0 < 0) return 0;
    if (r->x1 > GS_POSTER_MAX_PX || r->y1 > GS_POSTER_MAX_PX) return 0;
    return 1;
}

/* The corner word a sprite vertex takes is the recorder's own encoder, called directly.
 *
 * This file used to carry a second one -- the same law as `(px << 4) + GS_XYOFFSET_PAGE`, written
 * out again -- and LANE 12a removed it.  The two were gated against each other, which is the shape
 * of argument that has to end when there is only one spelling: the remaining check that the shift is
 * still the encoding the float path used is doc/txt/PS2.txt's LANE 12a gate, in
 * verify/tests/test_ps2_gs_pack.c's coord_matches_float(), which compares the shipped encoder with
 * the retired float expression over the whole coordinate range.  A poster and a recorded vertex that
 * reach the same page corner must reach the same WORD, and after this edit that is a property of
 * there being one function rather than of two agreeing. */

uint32_t gs_poster_qwc(uint32_t n)
{
    return n == 0u ? 0u : GS_POSTER_QWC(n);
}

uint32_t gs_poster_dropped(void)
{
    return s_dropped;
}

int32_t gs_poster_packet(const gs_poster_rect *r, uint32_t n, uint32_t canvas_px,
                         uint64_t *qw, uint32_t cap_qw)
{
    /* A sprite with no interpolation, no texture and no blend: flat colour straight from RGBAQ.
     * Hand-derived, and the gate checks it against the literal 0x6 rather than against this
     * expression again -- OP=6 in bits [2:0] with every other field clear. */
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);

    /* R[7:0] G[15:8] B[23:16] A[31:24], which is the CT32 canvas word's own layout, so the low 32
     * bits of this word ARE `canvas_px`.  Q is 1.0 because the layer is affine. */
    const uint64_t rgbaq = gs_pack_rgbaq(canvas_px & 0xFFu,
                                         (canvas_px >> 8) & 0xFFu,
                                         (canvas_px >> 16) & 0xFFu,
                                         (canvas_px >> 24) & 0xFFu,
                                         GS_Q_ONE);

    uint32_t i, written = 0u;

    s_dropped = 0u;

    if (r == 0 || qw == 0 || n == 0u) return -1;

    /* Room is asked for the worst case -- every rect surviving -- so the loop below cannot overrun,
     * and a caller that gets -1 for a short buffer finds it exactly as it left it. */
    if (cap_qw < gs_poster_qwc(n)) return -1;

    /* The tag is written with NLOOP 0 and back-patched once the count is final, which is
     * gs_rec.c's close_group() doing the same thing for the same reason: the number of rects that
     * reach the bus is not known until they have.  EOP is set because this packet is a DMA of its
     * own, and PRE with the tag's PRIM field hands the GS the primitive word before the first
     * corner does -- the same word is then re-sent as the group's first data register, which
     * ps2_gs_pack.h calls the norm rather than a redundancy to remove. */
    qw[0] = gs_pack_giftag(0u, 1u, 1u, prim, GS_FLG_PACKED, GS_POSTER_REGS_N);
    qw[1] = gs_pack_regs(s_regs, GS_POSTER_REGS_N);

    for (i = 0u; i < n; i++) {
        uint64_t *v;

        if (!poster_rect_ok(&r[i])) {
            s_dropped++;
            continue;
        }

        /* Four GIF quadwords per rect, value in the low 64 bits and zero above: the shape
         * gs_rec.c's write_vertex() uses and the shape the ring's QWC arithmetic counts, one
         * quadword per register write. */
        v = &qw[2u + (GS_POSTER_REGS_N * 2u) * written];
        v[0] = prim;   v[1] = 0u;
        v[2] = rgbaq;  v[3] = 0u;
        v[4] = gs_pack_xyz2(gs_vertex_coord16(r[i].x0), gs_vertex_coord16(r[i].y0), 0u);  v[5] = 0u;
        v[6] = gs_pack_xyz2(gs_vertex_coord16(r[i].x1), gs_vertex_coord16(r[i].y1), 0u);  v[7] = 0u;
        written++;
    }

    if (written == 0u) {
        /* Nothing in it, so nothing gets sent: a tag with NLOOP 0 is not an empty group, it is a
         * malformed packet, and the two quadwords above are forgotten here exactly the way
         * close_group() forgets them -- the caller sees -1 and posts no transfer at all. */
        return -1;
    }

    qw[0] = gs_pack_giftag(written, 1u, 1u, prim, GS_FLG_PACKED, GS_POSTER_REGS_N);

    return (int32_t)gs_poster_qwc(written);
}
