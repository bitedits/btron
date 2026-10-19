/*
 * ps2_gs_poster.c — Flat-colour SPRITE batch packet generation for PS2 GS
 *
 * Implements pure GIF packet emission for flat-colour rectangle fills.
 * Follows the architectural principles established in ps2_gs_poster.h:
 * 1. Native CT32 canvas colour packing into GS RGBAQ without endianness conversion.
 * 2. Exact 12.4 fixed-point vertex coordinate transformation via gs_vertex_coord16().
 * 3. Dense REGLIST packet packing (2 quadwords per rectangle).
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

/* R[7:0] G[15:8] B[23:16] A[31:24], which is the CT32 canvas word's own layout, so the low 32 bits
 * of this word ARE `canvas_px`.  Q is 1.0 because the layer is affine. */
static uint64_t poster_rgbaq(uint32_t canvas_px)
{
    return gs_pack_rgbaq(canvas_px & 0xFFu,
                         (canvas_px >> 8) & 0xFFu,
                         (canvas_px >> 16) & 0xFFu,
                         (canvas_px >> 24) & 0xFFu,
                         GS_Q_ONE);
}

/* Validates that a rectangle encloses positive area and lies within valid 12-bit screen bounds. */
static int poster_rect_ok(const gs_poster_rect *r)
{
    if (r->x1 <= r->x0 || r->y1 <= r->y0) return 0;
    if (r->x0 < 0 || r->y0 < 0) return 0;
    if (r->x1 > GS_POSTER_MAX_PX || r->y1 > GS_POSTER_MAX_PX) return 0;
    return 1;
}

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
     * OP=6 (GS_OP_SPRITE) in bits [2:0] with all modifiers clear. */
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);

    /* The flat colour, in native CT32 word order (see poster_rgbaq). */
    const uint64_t rgbaq = poster_rgbaq(canvas_px);

    uint32_t i, written = 0u;

    s_dropped = 0u;

    if (r == 0 || qw == 0 || n == 0u) return -1;

    /* Verify capacity for worst case where every rect survives clipping. */
    if (cap_qw < gs_poster_qwc(n)) return -1;

    /* The tag is written with NLOOP 0 initially and back-patched once the count of non-degenerate
     * rects is final. EOP is set because this packet forms an autonomous DMA transfer.
     * PRE is clear and PRIM in the tag is 0: the primitive word arrives as the first register in the
     * REGLIST loop, guaranteeing execution across all GS revisions. */
    qw[0] = gs_pack_giftag(0u, 1u, 0u, 0u, GS_FLG_REGLIST, GS_POSTER_REGS_N);
    qw[1] = gs_pack_regs(s_regs, GS_POSTER_REGS_N);

    for (i = 0u; i < n; i++) {
        uint64_t *v;

        if (!poster_rect_ok(&r[i])) {
            s_dropped++;
            continue;
        }

        /* Two GIF quadwords per rect, the four registers packed end to end: PRIM in the low half of
         * the first and RGBAQ in the half above it, then the two corners in the second.  That is what
         * FLG=REGLIST means, and it is half the bytes the retired sparse form spent on a group, at the
         * same register count -- so `bg=`'s poster, if it is ever given a caller, moves 239 quadwords
         * for the 119-rect batch instead of 477. */
        v = &qw[2u + (2u * GS_POSTER_QW_PER_RECT) * written];
        v[0] = prim;
        v[1] = rgbaq;
        v[2] = gs_pack_xyz2(gs_vertex_coord16(r[i].x0), gs_vertex_coord16(r[i].y0), 0u);
        v[3] = gs_pack_xyz2(gs_vertex_coord16(r[i].x1), gs_vertex_coord16(r[i].y1), 0u);
        written++;
    }

    if (written == 0u) {
        /* Nothing in it, so nothing gets sent: a tag with NLOOP 0 is not an empty group, it is a
         * malformed packet, and the two quadwords above are forgotten here exactly the way
         * close_group() forgets them -- the caller sees -1 and posts no transfer at all. */
        return -1;
    }

    qw[0] = gs_pack_giftag(written, 1u, 0u, 0u, GS_FLG_REGLIST, GS_POSTER_REGS_N);

    return (int32_t)gs_poster_qwc(written);
}

/* ── Diagnostic Reference Controls & Packet Implementations ───────────────────
 *
 * The functions below implement reference GIF packet shapes to validate GS
 * rasteriser behavior and packet decoding across revisions.
 */

/* Register lists for reference SPRITE (4 registers) and TRIANGLE (6 registers, NOP-padded). */
static const uint32_t s_ref_sprite_regs[4] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_XYZ2, GS_NIB_XYZ2
};
static const uint32_t s_ref_tri_regs[6] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_XYZ2, GS_NIB_XYZ2, GS_NIB_XYZ2, GS_NIB_NOP
};
/* Single-register A+D loop descriptor for state-write packet generation. */
static const uint32_t s_ref_ad_reg[1] = { GS_NIB_AD };

/* Compute subpixel corner coordinates with optional -7/+9 subpixel boundary adjustments. */
static void ref_sprite_corners(const gs_poster_rect *r, uint32_t nudge, uint32_t c[4])
{
    c[0] = gs_vertex_coord16(r->x0);  c[1] = gs_vertex_coord16(r->y0);
    c[2] = gs_vertex_coord16(r->x1);  c[3] = gs_vertex_coord16(r->y1);
    if (nudge) {
        c[0] -= GS_REF_START_ADJ;  c[1] -= GS_REF_START_ADJ;
        c[2] += GS_REF_END_ADJ;    c[3] += GS_REF_END_ADJ;
    }
}

int32_t gs_poster_ref_sprite_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                    uint64_t *qw, uint32_t cap_qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);
    const uint64_t rgbaq = poster_rgbaq(canvas_px);
    uint32_t c[4];

    if (r == 0 || qw == 0) return -1;
    if (cap_qw < GS_REF_SPRITE_QWC) return -1;
    if (!poster_rect_ok(r)) return -1;

    ref_sprite_corners(r, nudge, c);

    /* Dense REGLIST packet: FLG=REGLIST, NREG=4, PRE=0. PRIM arrives as first register. */
    qw[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u);
    qw[1] = gs_pack_regs(s_ref_sprite_regs, 4u);
    qw[2] = prim;
    qw[3] = rgbaq;
    qw[4] = gs_pack_xyz2(c[0], c[1], 0u);
    qw[5] = gs_pack_xyz2(c[2], c[3], 0u);

    return (int32_t)GS_REF_SPRITE_QWC;
}

/* State-write SPRITE packet: NLOOP 4, NREG 1, FLG=PACKED with GIF_REG_AD. */
int32_t gs_poster_ref_sprite_ad_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                       uint64_t *qw, uint32_t cap_qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);
    const uint64_t rgbaq = poster_rgbaq(canvas_px);
    uint32_t c[4];

    if (r == 0 || qw == 0) return -1;
    if (cap_qw < GS_REF_AD_QWC) return -1;
    if (!poster_rect_ok(r)) return -1;

    ref_sprite_corners(r, nudge, c);

    qw[0] = gs_pack_giftag(4u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
    qw[1] = gs_pack_regs(s_ref_ad_reg, 1u);
    qw[2] = prim;                 qw[3] = GS_GP_PRIM;
    qw[4] = rgbaq;                qw[5] = GS_GP_RGBAQ;
    qw[6] = gs_pack_xyz2(c[0], c[1], 0u);  qw[7] = GS_GP_XYZ2;
    qw[8] = gs_pack_xyz2(c[2], c[3], 0u);  qw[9] = GS_GP_XYZ2;

    return (int32_t)GS_REF_AD_QWC;
}

/* Multi-register PACKED SPRITE (negative control): FLG=PACKED, NREG=4. */
int32_t gs_poster_ref_sprite_packed_packet(const gs_poster_rect *r, uint32_t canvas_px,
                                           uint32_t pre, uint64_t *qw, uint32_t cap_qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);
    const uint64_t rgbaq = poster_rgbaq(canvas_px);
    uint32_t c[4];

    if (r == 0 || qw == 0) return -1;
    if (cap_qw < GS_REF_PACKED_QWC) return -1;
    if (!poster_rect_ok(r)) return -1;

    ref_sprite_corners(r, 0u, c);

    qw[0] = gs_pack_giftag(1u, 1u, pre ? 1u : 0u, prim, GS_FLG_PACKED, GS_POSTER_REGS_N);
    qw[1] = gs_pack_regs(s_regs, GS_POSTER_REGS_N);
    qw[2] = prim;   qw[3] = 0u;
    qw[4] = rgbaq;  qw[5] = 0u;
    qw[6] = gs_pack_xyz2(c[0], c[1], 0u);  qw[7] = 0u;
    qw[8] = gs_pack_xyz2(c[2], c[3], 0u);  qw[9] = 0u;

    return (int32_t)GS_REF_PACKED_QWC;
}

int32_t gs_poster_ref_tri_packet(const int32_t *v, uint32_t canvas_px,
                                 uint64_t *qw, uint32_t cap_qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_TRI, 0u, 0u, 0u, 0u, 0u);
    const uint64_t rgbaq = poster_rgbaq(canvas_px);
    int32_t area;
    int i;

    if (v == 0 || qw == 0) return -1;
    if (cap_qw < GS_REF_TRI_QWC) return -1;

    for (i = 0; i < 6; i++) {
        if (v[i] < 0 || v[i] > GS_POSTER_MAX_PX) return -1;
    }

    /* Signed 2D cross-product area check: rejects collinear points that enclose zero area. */
    area = (v[2] - v[0]) * (v[5] - v[1]) - (v[4] - v[0]) * (v[3] - v[1]);
    if (area == 0) return -1;

    qw[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_REGLIST, 6u);
    qw[1] = gs_pack_regs(s_ref_tri_regs, 6u);
    qw[2] = prim;
    qw[3] = rgbaq;
    qw[4] = gs_pack_xyz2(gs_vertex_coord16(v[0]), gs_vertex_coord16(v[1]), 0u);
    qw[5] = gs_pack_xyz2(gs_vertex_coord16(v[2]), gs_vertex_coord16(v[3]), 0u);
    qw[6] = gs_pack_xyz2(gs_vertex_coord16(v[4]), gs_vertex_coord16(v[5]), 0u);
    qw[7] = 0u;      /* NOP padding data word */

    return (int32_t)GS_REF_TRI_QWC;
}

/* Reference SPRITE with Alpha Blend Enable (ABE=1) and explicit source alpha. */
int32_t gs_poster_ref_sprite_blend_packet(const gs_poster_rect *r, uint32_t canvas_rgb,
                                          uint32_t src_alpha, uint32_t abe,
                                          uint64_t *qw, uint32_t cap_qw)
{
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, abe & 1u, 0u, 0u);
    const uint64_t rgbaq = gs_pack_rgbaq(canvas_rgb & 0xFFu,
                                         (canvas_rgb >> 8) & 0xFFu,
                                         (canvas_rgb >> 16) & 0xFFu,
                                         src_alpha & 0xFFu, GS_Q_ONE);
    uint32_t c[4];

    if (r == 0 || qw == 0) return -1;
    if (cap_qw < GS_REF_SPRITE_QWC) return -1;
    if (!poster_rect_ok(r)) return -1;

    ref_sprite_corners(r, 0u, c);

    qw[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u);
    qw[1] = gs_pack_regs(s_ref_sprite_regs, 4u);
    qw[2] = prim;
    qw[3] = rgbaq;
    qw[4] = gs_pack_xyz2(c[0], c[1], 0u);
    qw[5] = gs_pack_xyz2(c[2], c[3], 0u);

    return (int32_t)GS_REF_SPRITE_QWC;
}
