/*
 * ps2_gs_poster.h — Flat-colour fill rasterisation for PS2 Graphics Synthesizer
 *
 * Architecture & Principles:
 * A "poster" is a batch of flat-colour 2D rectangular fills issued directly to the
 * Graphics Synthesizer (GS) via the Graphics Interface (GIF) using SPRITE primitives.
 * When large regions of the framebuffer (such as the desktop background) share a uniform
 * colour, rasterising them via the GS rasteriser frees CPU cycles and avoids saturating
 * the main memory bus with software memory fills.
 *
 * Packet Format & Hardware Invariants:
 * 1. Transfer Format: A poster transfer is structured as a single GIF packet with:
 *      NLOOP = n, NREG = 4, FLG = REGLIST, PRE = 0.
 *    Four registers (PRIM, RGBAQ, XYZ2 corner 1, XYZ2 corner 2) are transmitted per rect.
 *    In REGLIST mode, each pair of 64-bit registers is densely packed into a single 128-bit
 *    quadword (2 quadwords per rectangle).
 *    Note on GS Hardware Quirks: Testing on physical GS hardware confirmed that multi-register
 *    PACKED bursts with PRE=1 do not reliably execute primitive kicks; the dense REGLIST
 *    layout with explicit register loops is required.
 *
 * 2. Colour Format & Endianness:
 *    The colour is accepted as a native CT32 32-bit canvas pixel word (A[31:24] B[23:16] G[15:8] R[7:0]).
 *    Because the GS RGBAQ register unpacks R into bits [7:0], G into [15:8], B into [23:16], and
 *    A into [31:24], the low 32 bits of the RGBAQ register directly mirror the framebuffer
 *    pixel without requiring RGB-to-BGR byte swapping.
 *
 * 3. Coordinate System & Subpixel Units:
 *    Screen coordinates are converted from integer pixels to the GS 12.4 fixed-point format
 *    via gs_vertex_coord16(), using a +2048 pixel (32768 subpixel) page offset.
 *
 * 4. Purity & Isolation:
 *    The packet builder is pure: it operates strictly on a caller-provided memory buffer,
 *    enabling deterministic verification in cleanroom unit tests without hardware dependencies.
 *
 * Zero proprietary Sony SDK code: register encodings follow ps2_gs_pack.h.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_POSTER_H
#define PS2_GS_POSTER_H

#include <stdint.h>

/* Register writes per rect: the primitive word, the flat colour, and the two corners.  The sprite
 * needs both corners in ONE loop -- a SPRITE is complete when its second XYZ2 arrives -- which is
 * why this is 4 and not the recorder's 4 (whose fourth is ST) and not 2. */
#define GS_POSTER_REGS_N 4u

/* GIF quadwords per rect.  This is HALF the register count because the group is a REGLIST list: in
 * that format the NREG 64-bit registers of one loop are packed densely into NREG/2 128-bit units,
 * the first in the low half, and LANE 19 rung 5 measured that this GS does not honour the sparse
 * form of the same list (see the rung-5 note below the reference builders).  The division is exact
 * only while the register count is even, which it is by construction, and make test-ps2-gs-poster
 * asserts it -- a fifth register would round down here and hand the GIF a group one word short. */
#define GS_POSTER_QW_PER_RECT (GS_POSTER_REGS_N / 2u)

/* Quadwords in a batch of `n` rects: one tag pair plus two data quadwords per rect.  Derived, never
 * repeated at the call site, so a caller cannot be handed a short buffer by a change here. */
#define GS_POSTER_QWC(n) (1u + GS_POSTER_QW_PER_RECT * (n))

/* One rect of the batch, in the units the compositor already uses: x0/y0 the first pixel it
 * covers, x1/y1 the first pixel it does NOT.  A four-wide int array instead of a RECT so this file
 * stays out of the desktop's headers -- the caller converts, and the conversion is gated below. */
typedef struct {
    int32_t x0, y0, x1, y1;
} gs_poster_rect;

/* Quadwords a caller must have room for to posterise `n` rects. */
uint32_t gs_poster_qwc(uint32_t n);

/* Fill exactly gs_poster_qwc(n) quadwords at `qw` with one packed SPRITE group of `n` rects, all
 * the colour the canvas word `canvas_px` says.  `cap_qw` is the room the caller has: a batch that
 * does not fit returns -1 and leaves the buffer alone, because half a frame of flat teal is a
 * picture with a hole in it and no receipt.
 *
 * Returns the quadwords written, or -1 for a bad argument (no rects, a short buffer, a null
 * pointer).  A SPRITE whose second corner is not strictly beyond its first draws nothing, so
 * degenerate rects are refused the same way -- and counted, because a caller that silently loses
 * rects is a caller whose `bg=` fell for a reason the [GSTS] row cannot show. */
int32_t gs_poster_packet(const gs_poster_rect *r, uint32_t n, uint32_t canvas_px,
                         uint64_t *qw, uint32_t cap_qw);

/* How many of the rects handed in were refused as degenerate at the time the packet was built.
 * Cleared by each gs_poster_packet() call, and read by the caller's diagnostic row -- the same
 * shape as gs_rec_unsplittable(), which is the precedent for counting a loss the packet itself
 * cannot carry. */
uint32_t gs_poster_dropped(void);

/* ── Diagnostic Reference Controls & Packet Shapes ──────────────────────────────
 *
 * The following builders provide reference packet shapes used by hardware probes
 * and cleanroom unit tests to verify GS rasterisation properties:
 *
 * 1. gs_poster_ref_sprite_packet:
 *    Standard reference SPRITE packet matching the Sony GS REGLIST protocol
 *    (GIF_FLG_REGLIST, NREG=4: PRIM, RGBAQ, XYZ2, XYZ2) with dense 64-bit register packing.
 *    Optionally applies standard subpixel corner nudges (-7 / +9 in 1/16th px).
 *
 * 2. gs_poster_ref_sprite_ad_packet:
 *    Alternative state-write SPRITE packet (FLG=PACKED, NREG=1, GIF_REG_AD)
 *    writing the 4 registers via explicit A+D address/data pairs.
 *
 * 3. gs_poster_ref_sprite_packed_packet:
 *    Negative control emitting sparse PACKED multi-register bursts (1 register per quadword).
 *    Used to verify that the hardware or emulator correctly differentiates between
 *    REGLIST and sparse PACKED encoding.
 *
 * 4. gs_poster_ref_tri_packet:
 *    Reference flat TRIANGLE packet (FLG=REGLIST, NREG=6: PRIM, RGBAQ, XYZ2*3, NOP).
 *
 * 5. gs_poster_ref_sprite_blend_packet:
 *    Reference SPRITE with Alpha Blend Enable (ABE=1) and explicit source alpha,
 *    used to isolate and verify the GS blending pipeline.
 */
#define GS_REF_SPRITE_QWC 3u    /* One tag quadword plus two dense data quadwords (4 registers) */
#define GS_REF_TRI_QWC    4u    /* One tag quadword plus three dense data quadwords (6 registers) */

/* Page offset in 1/16-pixel vertex units: 2048.0f * 16 = 32768 (GS_XYOFFSET_PAGE).
 * Subpixel edge adjustments (-7 / +9 subpixels) match conventional rasterisation nudges. */
#define GS_REF_PAGE       (2048u << 4)
#define GS_REF_START_ADJ  7u    /* 2047.5625f * 16 = 32761 = GS_REF_PAGE - GS_REF_START_ADJ */
#define GS_REF_END_ADJ    9u    /* 2048.5625f * 16 = 32777 = GS_REF_PAGE + GS_REF_END_ADJ */

#define GS_REF_AD_QWC     5u    /* One tag quadword plus four AD quadwords, one register each */
#define GS_REF_PACKED_QWC 5u    /* One tag quadword plus four sparse data quadwords, NREG 4 */

/* Fill exactly GS_REF_SPRITE_QWC quadwords with a reference flat SPRITE:
 * FLG=REGLIST, PRE=0, NREG=4, with registers packed densely into two quadwords.
 * `nudge`: 0 for unadjusted pixel coordinates, 1 for subpixel boundary adjustment (-7/+9).
 * Returns GS_REF_SPRITE_QWC, or -1 on validation error or degenerate rect. */
int32_t gs_poster_ref_sprite_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                    uint64_t *qw, uint32_t cap_qw);

/* Fill exactly GS_REF_AD_QWC quadwords with a reference flat SPRITE emitted via
 * A+D register writes: NLOOP 4, NREG 1, FLG=PACKED, PRE=0, address/data pairs for
 * GS_REG_PRIM, GS_REG_RGBAQ, and GS_REG_XYZ2 (twice).
 * Returns GS_REF_AD_QWC, or -1 on invalid argument or degenerate rect. */
int32_t gs_poster_ref_sprite_ad_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                       uint64_t *qw, uint32_t cap_qw);

/* Diagnostic negative control: emits a multi-register PACKED SPRITE (FLG=PACKED, NREG=4).
 * Returns GS_REF_PACKED_QWC, or -1 on invalid argument. */
int32_t gs_poster_ref_sprite_packed_packet(const gs_poster_rect *r, uint32_t canvas_px,
                                           uint32_t pre, uint64_t *qw, uint32_t cap_qw);

/* Fill exactly GS_REF_TRI_QWC quadwords with a reference flat TRIANGLE:
 * FLG=REGLIST, NREG=6 (PRIM, RGBAQ, XYZ2 x 3, NOP).
 * `v` points to 6 int32_t coordinates (x0, y0, x1, y1, x2, y2).
 * Returns GS_REF_TRI_QWC, or -1 if collinear, out of bounds, or buffer too small. */
int32_t gs_poster_ref_tri_packet(const int32_t *v, uint32_t canvas_px,
                                 uint64_t *qw, uint32_t cap_qw);

/* Reference SPRITE with Alpha Blend Enable (ABE):
 * Emits a dense REGLIST SPRITE with PRIM carrying `abe & 1` and RGBAQ carrying
 * `src_alpha` in its alpha byte, enabling direct verification of the GS blending equation.
 * Returns GS_REF_SPRITE_QWC, or -1 on invalid argument. */
int32_t gs_poster_ref_sprite_blend_packet(const gs_poster_rect *r, uint32_t canvas_rgb,
                                          uint32_t src_alpha, uint32_t abe,
                                          uint64_t *qw, uint32_t cap_qw);

#endif /* PS2_GS_POSTER_H */
