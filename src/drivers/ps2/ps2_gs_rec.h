/*
 * ps2_gs_rec.h — a vertex stream, recorded as Graphics Synthesizer GIF packets
 *
 * Increment 4 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  This is the part
 * that turns "a triangle strip with these vertices, colours and texture coordinates" into the
 * words the GIF will carry, and it is still not the part that sends them: like the environment
 * in ps2_gs_env.c, it computes and never touches a register or the DMA.  What it adds over
 * increment 3 is the group state machine -- an open primitive, a running NLOOP, and the rule
 * for what to do when the buffer is full.
 *
 * The group's REGS list is PRIM, RGBAQ, ST, XYZ2, and it is a dense `FLG=REGLIST` group, so a vertex
 * is TWO GIF quadwords -- 32 bytes -- because a REGLIST group packs each 64-bit register next to the
 * one the list names after it instead of giving each one a quadword of its own.  PRIM is
 * repeated for every vertex rather than sent once because the GS decrements PRIM's texture,
 * blend and fog flags as each primitive completes (under PRMODE.MODE/OFFSET), so a strip that
 * set them once loses them from its second triangle on; re-sending them is what a UI that wants
 * every triangle textured, blended and fog-free asks for.  The cheaper alternative -- one
 * PRMODE write and three registers a vertex -- is recorded as a follow-up in doc/txt/PS2.txt and
 * waits on a field table this port can gate.
 *
 * The shape this replaced was `FLG=PACKED` with `PRE=1` and one sparse quadword per register -- four
 * per vertex, twice the bytes -- and LANE 19 rung 5 measured it on this GS drawing NOTHING: a
 * multi-register PACKED group never kicks, whatever its primitive comes from, while the identical
 * four registers densely packed filled their box to the pixel (ps2_gs_poster.h carries the census
 * rows).  So these vertex groups were the inert shape, which is the standing candidate explanation
 * for the eegs XMB's "frame only, empty inside".  Whether the picture now arrives is the run that
 * follows this edit -- the census can say whether a recorded-shape packet lands, and only the
 * screen can say whether a frame does -- not something this paragraph may assert.
 *
 * XYZ2 comes last in every vertex because the drawing kick fires on it: colour and texture
 * coordinates have to be in the GS before the vertex that uses them is decoded.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/txt/PS2.txt.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_REC_H
#define PS2_GS_REC_H

#include <stdint.h>

/* Quadwords per vertex: the four registers of the group's REGS list, two of them to a quadword,
 * because that is what `FLG=REGLIST` means and what LANE 19 rung 5 measured as the shape this GS
 * rasterises.  The division is exact only while the register count is even, which it is by
 * construction -- and `make test-ps2-gs-rec` asserts it, because `(GS_REC_REGS_N % 2u)` is not a
 * preprocessor expression at all (`4u` is not a pp-number), so no `#if` can check it. */
#define GS_REC_REGS_N 4u
#define GS_REC_QW_PER_VERT (GS_REC_REGS_N / 2u)

/* Quadwords in a group of `n` AD register writes: one tag plus n data, because the general law is
 * one tag plus NLOOP * NREG *registers*, and each register takes a quadword of its own when FLG is
 * PACKED -- which is the AD form, always NREG=1.  `send_trx_setup_at()` in ps2_gs.c proves the shape
 * on the machine -- a tag and four registers as five quadwords with NLOOP=4 -- so a count that
 * instead gave each register two quadwords would hand channel 2 a QWC larger than the packet and the
 * GIF would decode the overflow as more of it.  A REGLIST group is the same law with the registers
 * packed two to a quadword, which is why a vertex group's count is GS_REC_QW_PER_VERT per loop and
 * not this one. */
#define GS_REC_AD_QWC(n) (1u + (n))

/* The stream buffer, in quadwords: 16 bytes each.  A group that would not fit is split at the
 * nearest boundary that keeps every primitive whole, and the vertices a strip needs to continue
 * are re-emitted, so this size is a latency knob -- how many DMA starts a frame costs -- and not
 * a limit on what an application may draw.  4,096 quadwords is 64 KB of .bss.  The measured XMB
 * frame this was chosen against was ~13,700 quadwords and therefore cost four starts; at two
 * quadwords a vertex rather than four the same vertex count is ~6,900, which is two starts -- that
 * halving is arithmetic from the stride and not a re-measurement, so the next bench run owns the
 * number in the [GSTS] row.  Overridable so the host gate can exercise a split without writing
 * twenty thousand vertices. */
#ifndef GS_REC_QW_CAP
#define GS_REC_QW_CAP 4096u
#endif

/* Hands one complete group to whoever owns channel 2.  `qwc` is quadwords, the same unit DMAC
 * QWC takes.  A sink that returns having taken the words; the recorder keeps nothing it has
 * sent.  Passing 0 for the sink records without sending, which is what the host gate does while
 * it inspects the words. */
typedef void (*gs_rec_sink)(void *user, const uint64_t *qw, uint32_t qwc);

/* Start over: no open group, no vertices, counters zeroed. */
void gs_rec_init(gs_rec_sink sink, void *user);

/* Which primitive the following vertices make, as the word gs_pack_prim() builds (op, IIP,
 * TME, ABE, context).  Any group still open is closed and sent first, because a GIF tag's
 * REGS list and PRIM belong to one primitive type. */
void gs_rec_prim(uint64_t prim);

/* The colour and the texture coordinate the next vertex takes.  Q is the multiplicative
 * identity: a UI layer is affine, and the GS's ST interpolation is affine too. */
void gs_rec_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
void gs_rec_texcoord(float s, float t);

/* One vertex, in whole screen pixels relative to XYOFFSET's page origin -- which is what
 * gs_vertex_coord16() folds the +2048 and the 12.4 around, so callers hand over plain integers
 * and never a fixed-point number.  The unit is a pixel rather than a 1/16 because that is what
 * the callers have: both backends' vertices come out of the viewport map, whose outputs are
 * `GLint`, and the field's four low bits were therefore always zero.  Returns 0, or -1 if no
 * primitive is open. */
int gs_rec_vertex(int32_t x_px, int32_t y_px, uint32_t z);

/* glEnd: close the open group and send it.  A group with no vertices sends nothing. */
void gs_rec_end(void);

/* One register write in the middle of the stream: `n` writes, one quadword each (the register's
 * 64-bit value in the low half, its GP offset in the high), as a group of their own -- PACKED,
 * NLOOP=n, NREG=1, REGS={AD}.  The open vertex group is sent first, because the vertices before the
 * switch draw with the state before it, and the next gs_rec_prim() opens a fresh group after.
 *
 * This is how a texture changes mid-frame.  TEX0/TEX1/CLAMP are per-context state and the
 * environment packet is written once per frame, so the XMB's icon sheet and its font sheet can
 * only be told apart by a write that arrives between their primitives -- see row 5b in
 * doc/txt/PS2.txt.  Returns 0, or -1 without sending when n is 0-sized past the buffer (a truncated
 * AD group would be a half-programmed texture, which is the failure this layer refuses rather than
 * rounds). */
int gs_rec_ad(const uint64_t *data, const uint32_t *gp_offs, uint32_t n);

/* Counters for the bench: DMA starts made, vertices taken, and the boundary rule refused a
 * split (which must read 0 -- it means a primitive shape arrived that cannot be continued
 * across a tag, and the only honest answer to that is a bigger buffer). */
uint32_t gs_rec_sends(void);
uint32_t gs_rec_verts(void);
uint32_t gs_rec_unsplittable(void);

/* The rectangle the frame's FIRST vertex group provably reaches, in the same local pixels
 * `gs_rec_vertex()` is handed: its top-left corner is XYOFFSET's page point, so (0,0) is the client
 * rect's own top-left.  Returns 0 and fills the four when a group has been sent, -1 before the frame
 * has drawn anything.
 *
 * A GS layer shares its rect with the canvas: the band upload arrives first and the stream draws
 * into it, so wherever the stream does not rasterise, the screen shows the canvas.  That is the
 * condition the compositor checks before it stops filling the plate under a layer, and the edges
 * are the group's own extreme vertices named exactly -- a vertex at 767 reaches 767 and not 768,
 * which is what the whole-pixel unit gives for free now that the packet is built from integers --
 * so a span that covers the rect is a span that covers it, not a span that nearly does. */
int gs_rec_bbox_first(int32_t *l, int32_t *t, int32_t *r, int32_t *b);

#endif /* PS2_GS_REC_H */
