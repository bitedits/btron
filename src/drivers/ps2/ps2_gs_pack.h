/*
 * ps2_gs_pack.h — the Graphics Synthesizer's packet words, built on the EE
 *
 * Increment 2 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  This is the
 * encoding half only: it turns a vertex, a colour, a texture page or a drawing environment
 * into the 64-bit words the GIF puts on the GS's bus, and it does not touch the DMA path,
 * a register, or the canvas.  Nothing calls it in the shipped image yet; the host gate
 * `make test-ps2-gs-pack` is what proves the words, and increments 3 and 4 program the
 * registers and emit the stream.
 *
 * Cleanroom again, like ps2_gs.c: every layout here is the GS manual's own field list,
 * re-derived and then checked against what the machine decodes.  No `.c` from
 * third_party/ps2sdk/ is linked or copied; its headers were read only to cross-check a
 * field's width, and where one of them is wider than the manual (its FRAME and TEX0 macros
 * let PSM and CBA run into reserved bits, which is harmless until a caller passes a value
 * that does not fit) these encoders follow the manual and the gate says so per field.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_PACK_H
#define PS2_GS_PACK_H

#include <stdint.h>

/* ── Which word goes where ──────────────────────────────────────────────
 *
 * A PACKED GIF packet is one tag quadword pair followed by data quadwords, and the tag's
 * REGS list says which register each data quadword's 128 bits feed.  The two namespaces
 * below are not interchangeable: the nibble list selects a field within the packed group,
 * the GP offset is what the AD form (nibble 0x0E) writes, and the offsets below 0x10 happen
 * to share numbers with nibbles 0..0xA by design of the hardware.
 */

/* REGS-list nibbles, a.k.a. the "local" register group. */
#define GS_NIB_PRIM    0x00u
#define GS_NIB_RGBAQ   0x01u
#define GS_NIB_ST      0x02u
#define GS_NIB_UV      0x03u
#define GS_NIB_XYZF2   0x04u
#define GS_NIB_XYZ2    0x05u
#define GS_NIB_TEX0_1  0x06u
#define GS_NIB_TEX0_2  0x07u
#define GS_NIB_CLAMP_1 0x08u
#define GS_NIB_CLAMP_2 0x09u
#define GS_NIB_FOG     0x0Au
#define GS_NIB_XYZF3   0x0Cu   /* like XYZF2, no drawing kick */
#define GS_NIB_XYZ3    0x0Du   /* like XYZ2, no drawing kick */
#define GS_NIB_AD      0x0Eu   /* data quadword carries a GP register write */
#define GS_NIB_NOP     0x0Fu

/* General-purpose register offsets for the AD form.  0x00..0x0A are the same fields as the
 * first nibbles; the ones from 0x14 up have no packed nibble of their own. */
#define GS_GP_PRIM       0x00u
#define GS_GP_RGBAQ      0x01u
#define GS_GP_ST         0x02u
#define GS_GP_UV         0x03u
#define GS_GP_XYZF2      0x04u
#define GS_GP_XYZ2       0x05u
#define GS_GP_TEX0_1     0x06u
#define GS_GP_TEX0_2     0x07u
#define GS_GP_CLAMP_1    0x08u
#define GS_GP_CLAMP_2    0x09u
#define GS_GP_FOG        0x0Au
#define GS_GP_TEX1_1     0x14u
#define GS_GP_TEX1_2     0x15u
#define GS_GP_XYOFFSET_1 0x18u
#define GS_GP_XYOFFSET_2 0x19u
#define GS_GP_PRMODECONT 0x1Au
#define GS_GP_PRMODE     0x1Bu
#define GS_GP_SCANMSK    0x22u
#define GS_GP_TEXA       0x3Bu
#define GS_GP_TEXFLUSH   0x3Fu
#define GS_GP_SCISSOR_1  0x40u
#define GS_GP_SCISSOR_2  0x41u
#define GS_GP_ALPHA_1    0x42u
#define GS_GP_ALPHA_2    0x43u
#define GS_GP_COLCLAMP   0x46u
#define GS_GP_TEST_1     0x47u
#define GS_GP_TEST_2     0x48u
#define GS_GP_PABE       0x49u
#define GS_GP_FRAME_1    0x4Cu
#define GS_GP_FRAME_2    0x4Du
#define GS_GP_ZBUF_1     0x4Eu
#define GS_GP_ZBUF_2     0x4Fu
#define GS_GP_BITBLTBUF  0x50u
#define GS_GP_TRXPOS     0x51u
#define GS_GP_TRXREG     0x52u
#define GS_GP_TRXDIR     0x53u
#define GS_GP_HWREG      0x54u

/* Primitive op codes (PRIM field OP). */
#define GS_OP_POINT   0u
#define GS_OP_LINE    1u
#define GS_OP_LINESTP 2u
#define GS_OP_TRI     3u
#define GS_OP_TRISTP  4u
#define GS_OP_TRIFAN  5u
#define GS_OP_SPRITE  6u

/* A quad is two triangles, and a strip is what a GL_QUADS' four vertices become: the bar
 * and every icon in the XMB are quads, so TRI-strips are the shape that reaches the GS. */
#define GS_NLOOP_QUAD 2u

/* Q is the perspective term the GS divides by, held as IEEE-754 single.  A 2D UI wants the
 * multiplicative identity, and no VU is needed to say so. */
#define GS_Q_ONE 0x3F800000u

/* The page offset: vertices are 12.4 fixed relative to this, so a 12-bit signed range of
 * +/-2048 px around it.  gs_vertex_coord16() folds it in once, at the conversion. */
#define GS_XYOFFSET_PAGE (2048u << 4)

/* GIF packet formats (tag field FLG). */
#define GS_FLG_PACKED  0u
#define GS_FLG_REGLIST 1u
#define GS_FLG_IMAGE   2u

/* ── Vertex and colour words ───────────────────────────────────────────── */

/* PRIM: OP[2:0] IIP[3] TME[4] FGE[5] ABE[6] AA1[7] FST[8] CTXT[9] FIX[11:10].
 * IIP is Gouraud (per-vertex colour interpolation), TME turns texturing on, ABE turns the
 * ALPHA register's blend on, CTXT picks texture context 1 or 2.  FIX is the alpha-method
 * select, left to the caller. */
uint64_t gs_pack_prim(uint32_t op, uint32_t iip, uint32_t tme, uint32_t abe,
                      uint32_t ctxt, uint32_t fix);

/* RGBAQ: R[7:0] G[15:8] B[23:16] A[31:24] Q[63:32].  Q is passed as its bit pattern
 * (GS_Q_ONE, or whatever a float's bits are) so this file needs no float type. */
uint64_t gs_pack_rgbaq(uint32_t r, uint32_t g, uint32_t b, uint32_t a, uint32_t q);

/* ST: S[31:0] and T[63:32], each an IEEE-754 single texture coordinate.  The GS
 * interpolates these affinely, which is exactly right for a UI and is the reason there is
 * no perspective-correct texturing to emulate. */
uint64_t gs_pack_st(float s, float t);

/* UV: U[13:0] V[29:16], the fixed-point texture coordinate (u12.4 for the low bits, the
 * high bits coming from TEX0's context).  Provided because TEX0/CLAMP arithmetic is cheaper
 * in 16.16 for a page-aligned atlas. */
uint64_t gs_pack_uv(uint32_t u, uint32_t v);

/* Screen pixels to the GS's 12.4 field with the page offset folded in: 0 is 0x8000, and the
 * field's own resolution is 1/16 px.  The unit of this function is a WHOLE pixel, because that
 * is the unit every producer in the tree has: the vertices arrive from `ZBufferPoint`, whose x
 * and y are `GLint`, and the poster's corners are integers too, so the four low bits of the 12.4
 * word were always zero and the value was reached by converting an integer to a float,
 * multiplying that float by 16.0f in soft float -- two libcalls, 87 cycles on this CPU -- and
 * converting the product back to the integer it started from.  `px * 16` is representable in
 * binary32 for every |px| < 2^20 and truncating an integer-valued float returns that integer, so
 * the shift is not an approximation of the old expression: it is the same word by a shorter
 * route, and `make test-ps2-gs-pack` holds the old float spelling on the host as an oracle and
 * sweeps the two against each other over the range.  Truncation-not-rounding is still what the
 * hardware's own ftoi4 does, and is still why a sprite's edge does not move. */
uint32_t gs_vertex_coord16(int32_t px);

/* `f * 2^-shift`, exactly, without a multiply.
 *
 * The ST field is the two texel coordinates' own IEEE-754 bits (gs_pack_st() takes them and does
 * no arithmetic), and the one place this port has a texel coordinate to put in it divides a
 * fixed-point integer by its own scale -- `1.0f / (1 << (ZB_POINT_*_FRAC_BITS + 1))`, a power of
 * two by construction.  A power-of-two scale changes no mantissa: it moves the exponent field by
 * `shift`, so for a value that is already a normal number the whole product is one subtraction on
 * the bits, and on this CPU the alternative is a soft-float libcall -- 34 cycles, against the
 * handful of integer instructions the fold costs.
 *
 * The three cases where that reasoning does not hold are named and sent to the multiply instead,
 * which is exact for them anyway: zero (its bits would wrap into a huge exponent), a value whose
 * biased exponent is at or below `shift` (the answer leaves the normal range, and a bit
 * subtraction cannot round), and a value whose biased exponent is 0xFF (an infinity or a NaN,
 * which are not numbers to rescale).  Everything a binary32 conversion of a `GLint` produces is
 * normal and at least 127, so the fold path is the only one the vertex path takes, and the guard
 * is here so that a future caller does not have to know that.
 *
 * `make test-ps2-gs-pack` sweeps this against the multiply over the integers a vertex can carry
 * and over zero, -0, both infinities, a NaN, a subnormal and the exponent-field edges, and
 * requires the same bits on both sides. */
static inline float gs_float_scale_pow2(float f, uint32_t shift)
{
    union { float f; uint32_t u; } b;

    b.f = f;

    /* The biased exponent field, and nothing else: the two ends are excluded, so what is left is
     * neither zero, subnormal, infinite nor NaN, and a normal's bits can be rewritten. */
    if ((b.u & 0x7F800000u) != 0u && (b.u & 0x7F800000u) != 0x7F800000u &&
        ((b.u >> 23) & 0xFFu) > shift)
        b.u -= shift << 23;
    else
        b.f = f * (1.0f / (float)(1u << shift));   /* the spelling the callers had before */

    return b.f;
}

/* XYZ2: X[15:0] Y[31:16] Z[63:32].  Z is a 32-bit fixed depth; a 2D layer passes 0.  The
 * drawing kick fires on the second XYZ2 of a strip, so this word is emitted last in every
 * vertex group. */
uint64_t gs_pack_xyz2(uint32_t x16, uint32_t y16, uint32_t z);

/* XYZF2: the same with Z[55:32] truncated to 24 bits and FOG[63:56] in the top byte. */
uint64_t gs_pack_xyzf2(uint32_t x16, uint32_t y16, uint32_t z, uint32_t fog);

/* ── Drawing-environment words ─────────────────────────────────────────── */

/* TEX0: TBA[13:0] TBW[19:14] PSM[25:20] TW[29:26] TH[33:30] TCC[34] TFUNC[36:35]
 *        CBA[49:37] CPSM[54:51] CSM[55] CSA[60:56] CLD[63:61]
 * TBA and CBA are in units of 256 bytes; TW/TH are the exponent of the page's width and
 * height (a 64x64 page is TW=6, TH=6); TCC 0=RGB0 1=RGBA; TFUNC 0=modulate. */
uint64_t gs_pack_tex0(uint32_t tba, uint32_t tbw, uint32_t psm, uint32_t tw, uint32_t th,
                      uint32_t tint, uint32_t tfunc, uint32_t cba, uint32_t cpsm,
                      uint32_t csm, uint32_t csa, uint32_t cld);

/* TEX1: LCM[0] MXL[4:2] MMAG[5] MMIN[8:6] MTBA[9] L[20:19] K[43:32].
 * MMAG 0=NEAREST 1=LINEAR; MMIN 0=NEAREST (no mipmap) is what a UI at 1:1 wants. */
uint64_t gs_pack_tex1(uint32_t lcm, uint32_t mxl, uint32_t mmag, uint32_t mmin,
                      uint32_t mtba, uint32_t l, uint32_t k);

/* MMAG/MMIN arguments with names, so a caller says what it means instead of passing a 1 that
 * only this header's comment can decode.  MMIN has eight values (the mipmap chain this port does
 * not build); 0 is the no-mipmap nearest, which is the only one that means anything here. */
#define GS_FILTER_NEAREST 0u
#define GS_FILTER_LINEAR  1u

/* CLAMP WMS/WMT arguments.  0 clamps to 0, 1 repeats with the plane's own period, 2 clamps to
 * the edge texel; TinyGL's sampler masks its coordinates, which is 1. */
#define GS_CLAMP_ZERO 0u
#define GS_REPEAT     1u
#define GS_CLAMP_EDGE 2u

/* CLAMP: WMS[1:0] WMT[3:2] MINU[13:4] MAXU[23:14] MINV[33:24] MAXV[43:34]
 * The clamp limits are u10.0 texture coordinates in the page, not pixels. */
uint64_t gs_pack_clamp(uint32_t wms, uint32_t wmt, uint32_t minu, uint32_t maxu,
                       uint32_t minv, uint32_t maxv);

/* XYOFFSET: X[15:0] Y[47:32], each 12.4.  The manual's rule is subtractive -- "when a vertex
 * kick occurs, XYOFFSET is subtracted from the vertex's X and Y coordinates" -- so this is the
 * page origin: 0x8000 (= 2048 px in 12.4) puts a window-relative vertex on screen pixel 0, and
 * adding the window's own x/y to both halves moves that origin to where the window is. */
uint64_t gs_pack_xyoffset(uint32_t x16, uint32_t y16);

/* SCISSOR: X0[10:0] X1[26:16] Y0[42:32] Y1[58:48], plain unsigned screen pixels (0..2047),
 * unlike a vertex -- there is no 12.4 scaling and no 2048 offset here.  This is the window's
 * clip rect. */
uint64_t gs_pack_scissor(uint32_t x0, uint32_t x1, uint32_t y0, uint32_t y1);

/* ALPHA: A[1:0] B[3:2] C[5:4] D[7:6] FIX[39:32].  The equation is
 * `result = (color1 - color2) * alpha >> 7 + color3`, and the four selects name their operands
 * out of two sets that share nothing but an ordinal, which is why they get names here:
 *
 *   colour selects (A, B, D): 0 = Cs (the primitive's own colour), 1 = Cd (the pixel already in
 *   the frame buffer), 2 = 0.
 *   alpha select (C):        0 = As (the primitive's RGBAQ alpha), 1 = Ad (the destination's),
 *   2 = the FIX field.
 *
 * Increment 5c pins the one combination this port programs -- source over destination, `(Cs-Cd)*As
 * >> 7 + Cd` -- and it is the blend the software path already applies, the `tglblend_alpha` arm
 * of zbuffer.h for `GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA`, so the GS layer and the canvas agree
 * on what the same triangle looks like wherever they meet.  ABE in PRIM selects between it and no
 * blend at all; the As that reaches it is the vertex's, which is why RGBAQ's alpha is written
 * 0..255. */
#define GS_BLEND_CS   0u
#define GS_BLEND_CD   1u
#define GS_BLEND_ZERO 2u
#define GS_ALPHA_AS   0u
#define GS_ALPHA_AD   1u
#define GS_ALPHA_FIX  2u

uint64_t gs_pack_alpha(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t fix);

/* TEST: ATEN[0] ATMETH[3:1] ATREF[11:4] ATFAIL[13:12] DATEN[14] DATMD[15] ZTEN[16]
 *        ZTMETH[18:17].  ZTEN 0 disables the depth test, which is the 2D UI's answer;
 * ATFAIL 0=keep the pixel 1=kill it 2=kill it and leave the destination alone. */
uint64_t gs_pack_test(uint32_t atest_en, uint32_t atest_meth, uint32_t atest_ref,
                      uint32_t atest_fail, uint32_t dtest_en, uint32_t dtest_md,
                      uint32_t ztest_en, uint32_t ztest_meth);

/* FRAME: FBA[8:0] FBW[21:16] PSM[27:24] FMSK[63:32].  FBA is the VRAM base in units of
 * 2048 *words* (8192 bytes) -- not the 256-byte page a texture or transfer address uses --
 * and that is what makes a 9-bit field cover the whole 4 MB: 512 x 8192 = 4,194,304.
 * FBW is the buffer width in units of 64 pixels.  PSM is four bits here -- the six the
 * field is sometimes drawn as would run into FMSK. */
uint64_t gs_pack_frame(uint32_t fba, uint32_t fbw, uint32_t psm, uint32_t fmsk);

/* ZBUF: ZBA[8:0] ZSM[27:24] ZMSK[32].  ZBA is in the same 8192-byte pages as FRAME.FBA. */
uint64_t gs_pack_zbuf(uint32_t zba, uint32_t zsm, uint32_t zmsk);

/* ── Transfer words (Host -> Local and back) ──────────────────────────────
 *
 * These four are the only words in this file whose positions are already proved by a screen:
 * the canvas upload in ps2_gs.c has programmed them by hand-written shifts since before this
 * plan existed, and 5b moved that path onto the encoders below and requires the same words out.
 * That fixes the *destination* half by measurement.  The source half (SBA/SBW/SPL/SPI) is
 * ignored by a Host->Local transfer -- its source is the quadword stream itself -- so its
 * positions are a header reading and nothing in this port exercises them until a row reads VRAM
 * back. */

/* BITBLTBUF: SBA[13:0] SBW[21:16] SPSM[29:24] DBA[45:32] DBW[53:48] DPSM[61:56].
 * SBA/DBA are 256-byte pages (the same unit TEX0.TBA uses); SBW/DBW are buffer widths in units
 * of 64 pixels; the two PSM arguments are the pixel-format code in the low bits and the
 * bit-select/interleave fields above them, passed as one value because a CT32 transfer wants
 * zeros in all of them. */
uint64_t gs_pack_bitbltbuf(uint32_t sba, uint32_t sbw, uint32_t spsm,
                           uint32_t dba, uint32_t dbw, uint32_t dpsm);

/* TRXPOS: SASX[10:0] SASY[26:16] DASX[42:32] DASY[58:48] DIR[60:59].  Source and destination
 * top-left in pixels of the area being moved, and DIR the source-area read direction (0 for a
 * Host->Local stream, which has no source rectangle). */
uint64_t gs_pack_trxpos(uint32_t sasx, uint32_t sasy, uint32_t dasx, uint32_t dasy,
                        uint32_t dir);

/* TRXREG: RRW[11:0] RRH[43:32].  The transfer's size in pixels: width 0..4095, height 0..4095
 * lines, counted from the destination position. */
uint64_t gs_pack_trxreg(uint32_t w, uint32_t h);

/* TRXDIR: INCD[1:0].  0 = Host->Local, 1 = Local->Host, 2 = Local->Local, 3 = deactivate. */
uint64_t gs_pack_trxdir(uint32_t dir);

/* ── Stream assembly ───────────────────────────────────────────────────── */

/* GIFTAG: NLOOP[14:0] EOP[15] ID[45:16] PRE[46] PRIM[57:47] FLG[59:58] NREG[63:60].
 * PRIM in the tag is the whole PRIM word for the packed group -- what gs_pack_prim() builds
 * is also the first data quadword, and the hardware uses the tag copy for the group's first
 * primitive only, so passing it twice is the norm, not a redundancy to remove. */
uint64_t gs_pack_giftag(uint32_t nloop, uint32_t eop, uint32_t pre, uint64_t prim,
                        uint32_t flg, uint32_t nreg);

/* The tag's second quadword: up to 4 four-bit register selects, first in the lowest bits. */
uint64_t gs_pack_regs(const uint32_t *regs, uint32_t n);

/* One AD pair: the register's 64-bit value, then the quadword whose low byte is the
 * register's GP offset.  Returns the cursor advanced by the pair. */
uint64_t *gs_pack_pair(uint64_t *cursor, uint64_t data, uint32_t gp_offset);

#endif /* PS2_GS_PACK_H */
