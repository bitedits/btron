/*
 * ps2_gs_rec.h — a vertex stream, recorded as Graphics Synthesizer GIF packets
 *
 * Increment 4 of the plan of record (doc/md/PS2.md, "The EE/GS backend").  This is the part
 * that turns "a triangle strip with these vertices, colours and texture coordinates" into the
 * words the GIF will carry, and it is still not the part that sends them: like the environment
 * in ps2_gs_env.c, it computes and never touches a register or the DMA.  What it adds over
 * increment 3 is the group state machine -- an open primitive, a running NLOOP, and the rule
 * for what to do when the buffer is full.
 *
 * The group's REGS list is PRIM, RGBAQ, ST, XYZ2 -- four quadwords a vertex, 64 bytes.  PRIM is
 * repeated for every vertex rather than sent once because the GS decrements PRIM's texture,
 * blend and fog flags as each primitive completes (under PRMODE.MODE/OFFSET), so a strip that
 * set them once loses them from its second triangle on; re-sending them is what a UI that wants
 * every triangle textured, blended and fog-free asks for.  The cheaper alternative -- one
 * PRMODE write and three quadwords a vertex -- is recorded as a follow-up in doc/md/PS2.md and
 * waits on a field table this port can gate.
 *
 * XYZ2 comes last in every vertex because the drawing kick fires on it: colour and texture
 * coordinates have to be in the GS before the vertex that uses them is decoded.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/md/PS2.md.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_REC_H
#define PS2_GS_REC_H

#include <stdint.h>

/* Quadwords per vertex: the four registers of the group's REGS list. */
#define GS_REC_REGS_N 4u

/* The stream buffer, in quadwords: 16 bytes each.  A group that would not fit is split at the
 * nearest boundary that keeps every primitive whole, and the vertices a strip needs to continue
 * are re-emitted, so this size is a latency knob -- how many DMA starts a frame costs -- and not
 * a limit on what an application may draw.  4,096 quadwords is 64 KB of .bss, and a measured XMB
 * frame of ~13,700 quadwords therefore costs it about four starts, which is the number row 5 of
 * the plan keeps an eye on.  Overridable so the host gate can exercise a split without writing
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

/* One vertex, in screen pixels relative to XYOFFSET's page origin -- which is what
 * gs_vertex_coord16() folds the +2048 and the 12.4 around, so callers hand over plain floats
 * and never a fixed-point number.  Returns 0, or -1 if no primitive is open. */
int gs_rec_vertex(float x_px, float y_px, uint32_t z);

/* glEnd: close the open group and send it.  A group with no vertices sends nothing. */
void gs_rec_end(void);

/* A register write in the middle of the stream: n AD pairs (a register's 64-bit value and the
 * quadword whose low byte is its GP offset) as a group of their own -- PACKED, NLOOP=n, NREG=1,
 * REGS={AD}.  The open vertex group is sent first, because the vertices before the switch draw
 * with the state before it, and the next gs_rec_prim() opens a fresh group after.
 *
 * This is how a texture changes mid-frame.  TEX0/TEX1/CLAMP are per-context state and the
 * environment packet is written once per frame, so the XMB's icon sheet and its font sheet can
 * only be told apart by a write that arrives between their primitives -- see row 5b in
 * doc/md/PS2.md.  Returns 0, or -1 without sending when n is 0-sized past the buffer (a truncated
 * AD group would be a half-programmed texture, which is the failure this layer refuses rather than
 * rounds). */
int gs_rec_ad(const uint64_t *data, const uint32_t *gp_offs, uint32_t n);

/* Counters for the bench: DMA starts made, vertices taken, and the boundary rule refused a
 * split (which must read 0 -- it means a primitive shape arrived that cannot be continued
 * across a tag, and the only honest answer to that is a bigger buffer). */
uint32_t gs_rec_sends(void);
uint32_t gs_rec_verts(void);
uint32_t gs_rec_unsplittable(void);

#endif /* PS2_GS_REC_H */
