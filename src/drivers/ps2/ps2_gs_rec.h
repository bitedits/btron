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
 * The group's REGS list is TWO registers -- RGBAQ then XYZ2 -- when the primitive is untextured and
 * THREE -- RGBAQ, ST, XYZ2 -- when it is textured, and the group is a dense `FLG=REGLIST`, so a
 * register costs its own 64 bits: a vertex is one quadword or one and a half, not the two this file
 * used to write.  PRIM is not in the list at all.  The tag carries it with PRE set AND every group is
 * preceded by an AD write of the same word, because LANE 19 rung 12u measured that the tag's field
 * alone buys nothing on this GS.
 *
 * This is the shape LANE 19 measured, one variable at a time, on this GS and not in a manual.  A
 * triangle whose three corners carry three different colours has to shade, and the recorder's
 * four-register list came out of the census as ONE FLAT COLOUR -- the first vertex's letter filled
 * the box -- while three hand-built shapes came out shading.  Rungs 12c/12d/12e ran six boxes in one
 * certified band (the magenta control returned 1024/1024, so the census was reading the page the
 * stream had really written) and the rows that decided this paragraph were:
 *
 *   A/B recorder, PRE=1 NREG4 {NOP,RGBAQ,ST,XYZ2} ....... painted=960/945, other=0 -> FLAT R / FLAT B
 *   C       AD(PRIM,RGBAQ) + PRE=0 NREG2 {RGBAQ,XYZ2} .. other=840 -> INTERPOLATES
 *   D       no AD group,  PRE=1 NREG2 {RGBAQ,XYZ2} ..... other=609 -> INTERPOLATES
 *   E       AD(PRIM,RGBAQ) + PRE=0 NREG3 {RGBAQ,ST,XYZ2} + pad ... other=609 -> INTERPOLATES
 *   F       no AD group,  PRE=1 NREG3 {RGBAQ,ST,XYZ2} + pad ..... other=441 -> INTERPOLATES
 *
 * Those rows said four things, and rung 12u re-read the second of them.  Of the variables that run
 * isolated in that grid: an odd NREG with a trailing pad half-quadword is survivable (E, F), an ST
 * slot in the chain is what a textured stream wants rather than suffers (E, F), the four-register
 * list is what killed the gradient (A, B), and -- this is the claim 12u falsified -- *PRE=1 with PRIM
 * in the tag is not the problem* (D, F).  Boxes D and F are not that claim's falsification, because
 * in that grid they are not alone: box C's AD preamble writes PRIM=TRI with IIP set two packets
 * earlier in the same transfer, and a register is state, so D and F drew with C's primitive and
 * shaded with C's IIP.  Rung 12u sent D's captured group with nothing in front of it, on a band
 * cleared to the canvas first, and the answer came back different:
 *
 *   V0 the group as captured, PRE=1 .......... painted=1, bbox x580-580 -> one pixel at the apex
 *   V1 the group with PRE cleared ............ painted=1, bbox x580-580 -> the tag field rules nothing
 *   V2 AD(PRIM=TRI,IIP=1) then the group ..... painted=610, field=609, bbox x539-620 -> TRIANGLE
 *   V3 AD(PRIM=TRI,IIP=0) then the group ..... painted=610, field=0,   bbox x539-620 -> TRIANGLE
 *
 * So the AD write is what programs the primitive, and the two writes that differ in IIP alone are
 * what decides whether the GS interpolates.  A recorder group that never writes PRIM therefore draws
 * with whatever the last AD packet left there -- in this port the frame's own depth clear
 * (eegs_present.c) ends on PRIM=SPRITE, so three vertices of a "triangle" become one quad and a
 * leftover kick, which is the flat rectangles-and-dots page the operator sees.  The recorder's fix is
 * V2's shape: gs_rec_prim()'s word goes out as an AD write in front of every vertex group, and the
 * tag keeps naming the same word under PRE=1 so a machine that does read the field agrees with the
 * one that does not.
 *
 * The old claim this header used to carry was that PRIM has to be repeated for every vertex because
 * the GS decrements PRIM's texture, blend and fog flags as each primitive completes (PRMODE.MODE/
 * OFFSET).  That is still refuted -- 12u's V2 is box D's captured group, three loops of the
 * two-register untextured chain, behind ONE prim write, and all three loops came out interpolated --
 * and what replaces the claim is not a cheaper packet but a cheaper *place* for the word: the write
 * is once per group, not once per vertex, and not once per frame either.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/txt/PS2.txt.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_REC_H
#define PS2_GS_REC_H

#include <stdint.h>

/* Registers per vertex, by the primitive's own texture bit: two untextured (RGBAQ, XYZ2) and three
 * textured (RGBAQ, ST, XYZ2).  Both counts are what LANE 19 rung 12e measured as shading; the four
 * this file used to name -- a leading NOP slot plus PRIM's old per-vertex seat -- is what measured
 * flat.  GS_REC_REGS_MAX sizes the recorder's per-vertex register set and is the longest of the two,
 * because a re-emitted strip continuation has to reproduce the colour and texture coordinate it
 * first carried regardless of which list the open group is using.
 *
 * There is deliberately no `GS_REC_REGS_N` any more.  It was a compile-time constant for a stream
 * whose register count varies with the primitive, and every consumer that trusted it -- the stage
 * decoder, the group-view helper, three host suites -- had to be found and made to read the count
 * out of the tag instead.  Deleting the macro is how the build finds them; a vertex counted by a
 * constant that one group in ten contradicts is a malformed packet with a green test line under it. */
#define GS_REC_REGS_MAX   3u
#define GS_REC_NREG_TEX   3u
#define GS_REC_NREG_UNTEX 2u

/* Quadwords in a group of `n` AD register writes: one tag plus n data, because the general law is
 * one tag plus NLOOP * NREG *registers*, and each register takes a quadword of its own when FLG is
 * PACKED -- which is the AD form, always NREG=1.  `send_trx_setup_at()` in ps2_gs.c proves the shape
 * on the machine -- a tag and four registers as five quadwords with NLOOP=4 -- so a count that
 * instead gave each register two quadwords would hand channel 2 a QWC larger than the packet and the
 * GIF would decode the overflow as more of it.  A vertex group is the same law with the registers
 * packed end to end two to a quadword, which is why its count comes from its own NREG and not from
 * this macro -- and why an odd one needs the pad half-quadword gs_rec_end() appends. */
#define GS_REC_AD_QWC(n) (1u + (n))

/* The stream buffer, in quadwords: 16 bytes each.  A group that would not fit is split at the
 * nearest boundary that keeps every primitive whole, and the vertices a strip needs to continue
 * are re-emitted, so this size is a latency knob -- how many DMA starts a frame costs -- and not
 * a limit on what an application may draw.  4,096 quadwords is 64 KB of .bss.  The measured XMB
 * frame this was chosen against was ~13,700 quadwords and therefore cost four starts at the sparse
 * shape's four quadwords a vertex; the dense two-register chain this file emits now costs one, and
 * the textured one one and a half.  Those are arithmetic from the stride and not re-measurements, so
 * the next bench run owns the [GSTS] row's number.  Overridable so the host gate can exercise a
 * split without writing twenty thousand vertices. */
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
 * REGS list and PRIM belong to one primitive type.  The word itself reaches the GS in front of
 * the next vertex group, as an AD register write (see the header comment: rung 12u measured
 * that the tag's own PRIM field is not what programs it on this machine), so declaring a
 * primitive that never gets a vertex costs nothing on the bus. */
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
 * across a tag, and the only honest answer to that is a bigger buffer).  `gs_rec_prim_writes()`
 * is the subset of those starts that program a vertex group's primitive: one per vertex group
 * sent, so `sends - prim_writes - texture_switches` is the frame's vertex-group count and the
 * row that pairs them is what prices rung 12u's fix. */
uint32_t gs_rec_sends(void);
uint32_t gs_rec_verts(void);
uint32_t gs_rec_unsplittable(void);
uint32_t gs_rec_prim_writes(void);

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
