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

/* R[7:0] G[15:8] B[23:16] A[31:24], which is the CT32 canvas word's own layout, so the low 32 bits
 * of this word ARE `canvas_px`.  Q is 1.0 because the layer is affine.  One function because the
 * poster and the two reference controls below must not be two models of the same word: the gate that
 * says "the low half equals the pixel handed in" is one claim about one encoder. */
static uint64_t poster_rgbaq(uint32_t canvas_px)
{
    return gs_pack_rgbaq(canvas_px & 0xFFu,
                         (canvas_px >> 8) & 0xFFu,
                         (canvas_px >> 16) & 0xFFu,
                         (canvas_px >> 24) & 0xFFu,
                         GS_Q_ONE);
}

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

    /* The flat colour, in the canvas's own word order -- see poster_rgbaq(). */
    const uint64_t rgbaq = poster_rgbaq(canvas_px);

    uint32_t i, written = 0u;

    s_dropped = 0u;

    if (r == 0 || qw == 0 || n == 0u) return -1;

    /* Room is asked for the worst case -- every rect surviving -- so the loop below cannot overrun,
     * and a caller that gets -1 for a short buffer finds it exactly as it left it. */
    if (cap_qw < gs_poster_qwc(n)) return -1;

    /* The tag is written with NLOOP 0 and back-patched once the count is final, which is
     * gs_rec.c's close_group() doing the same thing for the same reason: the number of rects that
     * reach the bus is not known until they have.  EOP is set because this packet is a DMA of its
     * own.  PRE is CLEAR and the tag's PRIM field is 0: the primitive word arrives as the group's
     * first register, in the loop, which is draw2d.c's shape and the one CYAN proved on this GS.
     * The form this replaced -- FLG=PACKED, PRE=1, PRIM both carried by the tag and named again as
     * the first register, four sparse quadwords per rect -- is what LANE 19 rung 5 measured as
     * drawing nothing while the identical four values densely packed drew at the exact count; the
     * finding is ps2_gs_poster.h's to read, and gs_poster_ref_sprite_packed_packet() is that shape
     * kept as the census's negative control. */
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

/* ── LANE 19 rung 4: the reference-shaped controls ───────────────────────────────────────────
 *
 * The header says what each one isolates -- the GIF format, the sprite's corner nudge, and the
 * three-vertex pipeline -- and this is the part that copies the words.  The source copied is
 * `third_party/ps2sdk/ee/draw/src/draw2d.c`: the flat sprite at 190-211 (`DRAW_SPRITE_NREG` 4 with
 * `GIF_SET_TAG(1,0,0,0,GIF_FLG_REGLIST,4)`, PRIM then RGBAQ in one quadword and the two corners in
 * the next) and the filled triangle at 119-146 (`DRAW_TRIANGLE_NREG` 6, PRIM, RGBAQ, three XYZ2 and
 * a NOP whose data word the SDK writes as 0).  Those two functions are what a shipped PS2 program
 * puts on a real GS, so a packet that matches them is evidence about the port's own shape rather
 * than about anybody's reading of the manual -- and the shape is what rung 3 left standing, since
 * neither sender delivered the port's sprite and the environment's nine registers are the SDK's own
 * nine, field for field (make test-ps2-gs-pack holds the encoders against ps2sdk's GS_SET_* macros).
 *
 * Only the packet's FORM is copied.  The DATA words stay this file's own: the same gs_pack_prim()
 * result, the same canvas-word RGBAQ, the same gs_vertex_coord16() corners as the failing packet, so
 * a control that lands and a poster that does not differ by the tag and the delivery of the register
 * list and nothing else.  The one exception is `nudge`, which exists to be the third variable, and
 * EOP, which the header explains.
 *
 * A third source is copied by the last builder here, and it is this port's own file rather than
 * ps2sdk's: send_trx_setup_at() at ps2_gs.c:220-250, because its PACKED/NREG=1 group of AD quadwords
 * is the one shape on this machine that every console upload proves reaches the GS.  The header's
 * rung-4 note says why that became the question after the stride one closed.
 */

/* The SDK's two register lists, nibble for nibble: draw2d.c's DRAW_SPRITE_REGLIST and
 * DRAW_TRIANGLE_REGLIST.  The triangle's sixth NOP is what makes its six registers an exact number
 * of quadwords, and it is the reason gs_pack_regs() had to learn that the field is ten nibbles wide
 * and not four -- at four, this list would have lost the third vertex and the packet would have
 * described a two-point primitive that never kicks. */
static const uint32_t s_ref_sprite_regs[4] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_XYZ2, GS_NIB_XYZ2
};
static const uint32_t s_ref_tri_regs[6] = {
    GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_XYZ2, GS_NIB_XYZ2, GS_NIB_XYZ2, GS_NIB_NOP
};
/* The AD group's REGS field, one nibble deep: packet2_utils.h's GIF_REG_AD, and the value
 * send_trx_setup_at() writes straight into the tag's high half at ps2_gs.c:230.  Every register in
 * that group is an AD write, so the list is four identical nibbles there and one here -- the REGS
 * field is per loop, and this packet's loop is one register long four times over. */
static const uint32_t s_ref_ad_reg[1] = { GS_NIB_AD };

/* The sprite's four corner fields, in the 1/16-pixel units the XYZ2 register takes.  One copy of this
 * arithmetic because the two reference sprites below must differ ONLY in how their registers are
 * delivered -- if the nudged corner pair were written out twice, a later edit to one form would make
 * the pair of controls a statement about the coordinates as well as about the group's shape. */
static void ref_sprite_corners(const gs_poster_rect *r, uint32_t nudge, uint32_t c[4])
{
    c[0] = gs_vertex_coord16(r->x0);  c[1] = gs_vertex_coord16(r->y0);
    c[2] = gs_vertex_coord16(r->x1);  c[3] = gs_vertex_coord16(r->y1);
    if (nudge) {
        /* draw2d.c:12-19 -- START_OFFSET 2047.5625f on the left/top corner and END_OFFSET
         * 2048.5625f on the right/bottom one, which in the field's own 1/16-pixel units is -7 and +9
         * around the page origin this port already adds.  The SDK's way of making a rect cover the
         * pixel whose centre sits on each edge; the poster does not do it, and rung 4 asked whether
         * that is why it draws nothing.  (It is not: CYAN and MAGENTA both landed on the first run,
         * CYAN at its exact 7,360 px and MAGENTA at 7,567 because the nudge widens the box by half a
         * pixel per edge.  The offsets are kept because they are the SDK's and cost nothing.) */
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

    /* The same refusal as gs_poster_packet(), and for the same reason only sharper: a control that
     * draws nothing is indistinguishable from a format that rasterises nothing, which is the one
     * thing this rung must not be able to produce. */
    if (!poster_rect_ok(r)) return -1;

    ref_sprite_corners(r, nudge, c);

    /* draw2d.c's tag, unaltered: FLG=REGLIST, NREG=4, PRE clear because the primitive arrives as the
     * group's first register rather than being carried by the tag.  The alternative that used to sit
     * beside this line -- the same dense body under this file's own PACKED/PRE=1 tag -- is not a
     * packet: a PACKED group takes one quadword per register, so NREG=4 wants four quadwords and the
     * two below leave the GIF short by two.  Sending it hung the channel and cost a whole census run
     * (see the header); gs_poster_ref_sprite_ad_packet() is the second SDK precedent instead. */
    qw[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u);
    qw[1] = gs_pack_regs(s_ref_sprite_regs, 4u);
    qw[2] = prim;
    qw[3] = rgbaq;
    qw[4] = gs_pack_xyz2(c[0], c[1], 0u);
    qw[5] = gs_pack_xyz2(c[2], c[3], 0u);

    return (int32_t)GS_REF_SPRITE_QWC;
}

/* The same four registers as four AD writes, which is the only other way ps2sdk puts a drawing
 * primitive on the bus and the way this port has already proven it can: send_trx_setup_at() at
 * ps2_gs.c:220 sends NLOOP 4, NREG 1, FLG=PACKED and one AD nibble in the REGS field, and every
 * console text upload that reaches the screen rides that shape.  Here the four registers are the
 * drawing ones, so the group is a sprite told in the state-write style, and the last XYZ2 write is
 * what kicks it -- the same rule the dense form above relies on.
 *
 * Each data quadword is [value | GP address]: that is what FLG=PACKED with GIF_REG_AD means, and it
 * is why this packet is five quadwords where the REGLIST one is three.  The high half is not padding
 * here and must not be zeroed, which is the mistake the header's retired arithmetic made in reverse. */
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

/* ── LANE 19 rung 5: PRE, alone on the table ────────────────────────────────────────────────
 *
 * rung 4 closed three of the four candidate variables and left one sentence standing: the packet
 * that ships differs from the packet that draws in "FLG=PACKED carrying a four-register REGS list
 * with PRE=1".  That is still TWO decisions written as one, and the census cannot tell them apart
 * until a packet exists that separates them.  This is that packet: the shipping body, quadword for
 * quadword, with `pre` the only argument the caller changes.
 *
 * The body comes from this file, not from ps2sdk -- which is deliberate.  ps2sdk has no precedent
 * here to copy (every GIF_FLG_PACKED group in third_party/ps2sdk is NREG=1 with GIF_REG_AD), so a
 * multi-register PACKED group is a shape only this port emits, and the question is whether the
 * shape is inert or one bit inside it is.  That makes the honest control the shipping packet
 * itself, and the value in the tag the only novelty -- so with `pre` 1 this builder is
 * gs_poster_packet() with one rect, and the poster gate asserts it word for word.  Without that
 * assertion a census row saying "ORANGE landed" would be evidence about a packet that resembles
 * RED's, not about RED's, which is the difference between a one-argument fix and a guess.
 *
 * nudge is not a variable here: ref_sprite_corners() is called with 0, because that is what
 * gs_poster_packet() does with its own corners and CYAN-vs-MAGENTA already showed the offsets
 * change the box by half a pixel per edge and nothing else. */
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

    /* One tag quadword (tag fields in the high half, the REGS list in the low one) and four data
     * quadwords, one 64-bit register each with its upper half zero -- the length the hung
     * dense-under-PACKED control proved this tag asks for.  NLOOP 1 because this is a transfer of
     * one sprite, not a batch, and EOP set for the same reason the other controls set it. */
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

    /* Twice the signed area, in the integer pixels the caller speaks: with the vertices at v[0..1],
     * v[2..3] and v[4..5] that is (x1-x0)(y2-y0) - (x2-x0)(y1-y0).  Zero is three collinear points,
     * which encloses nothing -- and a control that encloses nothing cannot answer whether the
     * three-vertex pipeline works. */
    area = (v[2] - v[0]) * (v[5] - v[1]) - (v[4] - v[0]) * (v[3] - v[1]);
    if (area == 0) return -1;

    qw[0] = gs_pack_giftag(1u, 1u, 0u, 0u, GS_FLG_REGLIST, 6u);
    qw[1] = gs_pack_regs(s_ref_tri_regs, 6u);
    qw[2] = prim;
    qw[3] = rgbaq;
    qw[4] = gs_pack_xyz2(gs_vertex_coord16(v[0]), gs_vertex_coord16(v[1]), 0u);
    qw[5] = gs_pack_xyz2(gs_vertex_coord16(v[2]), gs_vertex_coord16(v[3]), 0u);
    qw[6] = gs_pack_xyz2(gs_vertex_coord16(v[4]), gs_vertex_coord16(v[5]), 0u);
    qw[7] = 0u;      /* the NOP's data word, which draw2d.c:141 writes as 0 for the same reason */

    return (int32_t)GS_REF_TRI_QWC;
}

/* ── LANE 19 rung 7: the one control that touches the ALPHA stage ──────────────────────────────
 *
 * Rungs 1-6 could not see the blend stage at all, and that is a fact about the controls rather than
 * about the GS: every one of them, the shipping emitter included, is
 * `gs_pack_prim(op, IIP=0, TME=0, ABE=0, CTXT, FIX)`.  ABE clear means PRIM's alpha-blending enable is off,
 * which bypasses the ALPHA register entirely -- a fragment goes straight out as its RGBAQ colour --
 * so a wrong ALPHA word is invisible to a census of these packets.  That is the blind spot rung 6
 * drives into: its instrument asks "does the page differ from its canvas", and a blended fragment
 * whose equation collapses to `+ color3(=Cd)` paints the destination's own value back, which IS its
 * canvas word, so `diff_all=0` reads identically for "the blend is a no-op" and "no fragment at all".
 *
 * The only way out is a control that turns ABE on and lets the PIXEL name which of the two ALPHA
 * field-role models this GS implements.  This builder is CYAN's dense SPRITE shape -- the one rung 5
 * proved rasterises -- with exactly two words changed: PRIM carries `abe`, and RGBAQ carries
 * `src_alpha` in its own byte rather than the canvas word's.  Forcing the alpha here is the whole
 * point: the vertex's As is one of the blend's operands, and a control whose As came from a canvas
 * word whose A byte nobody set would have a third unmeasured variable.  Everything else -- the tag,
 * the four-register list, the un-nudged corners, GS_REF_SPRITE_QWC -- is gs_poster_ref_sprite_packet's
 * to the bit, so a census that has this box and CYAN's disagree is a statement about ABE and the
 * ALPHA register and nothing else.
 *
 * It is diagnostic, not production: like the other controls its only caller is a census verb
 * (`gsalpha` in core_ps2.c), which sets env.reg_alpha around it and reads the pixels back. */
int32_t gs_poster_ref_sprite_blend_packet(const gs_poster_rect *r, uint32_t canvas_rgb,
                                          uint32_t src_alpha, uint32_t abe,
                                          uint64_t *qw, uint32_t cap_qw)
{
    /* ABE is the variable this builder exists to carry; IIP stays 0 (flat) and TME 0 (untextured), so
     * the only thing reaching the ALPHA stage is a vertex's own RGBAQ against the destination. */
    const uint64_t prim = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, abe & 1u, 0u, 0u);
    /* The colour's three low bytes are the canvas word's own R/G/B; the alpha is the caller's, which
     * is what the blender multiplies by.  Q stays the affine identity. */
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
