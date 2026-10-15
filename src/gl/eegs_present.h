/*
 * eegs_present.h — the recorded GIF stream, put on the bus after the canvas is
 *
 * Increment 5c of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Everything that
 * *decides* what the stream holds is upstream and gated on a host: the geometry is recorded by
 * ps2_gs_rec.c, the environment words are built by ps2_gs_env.c, and the texture trio is emitted
 * mid-stream by backend_eegs.c.  This file is the one step that cannot be gated there, because it
 * is an ordering rule about hardware:
 *
 *   The canvas upload goes first, then the environment, then the primitives.
 *
 * Both halves of that order are forced.  The upload has to precede the stream because the GS
 * blends into the pixels already in VRAM -- a layer drawn under the upload would be painted over
 * by it -- and the stream has to follow the paint callback because the callback is what produces
 * it, while the callback runs inside the composite that the upload is of.  Those two facts
 * overlap: no DMA can happen during the paint, so the groups are staged here and sent after.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/txt/PS2.txt.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_EEGS_PRESENT_H
#define BTRON_GL_EEGS_PRESENT_H

#include <btron/types.h>

/* Start a frame's staging: reset the buffer and open the recorder on it.  Call before the
 * composite pass that runs the layer window's paint callback. */
void eegs_present_begin(void);

/* Close the recorder's last group.  The words are staged, not sent -- see the ordering rule
 * above -- so this makes no DMA. */
void eegs_present_end(void);

/* Send what this frame staged, over the window client rect `client` in screen pixels: one
 * environment packet addressed at that rect, then one DMAC start per staged group.  Returns the
 * number of staged groups sent -- the environment is one further DMA and is not in that count,
 * which is why the console prints it as `groups=` and not as a bus call total.  -1 when the
 * environment could not be built from `client` (a rect the GS cannot address), in which case
 * nothing was sent at all.
 *
 * A frame that staged nothing sends nothing and returns 0, which is the honest answer for a
 * layer that did not repaint: there is no stream to put on the bus, and the canvas standing
 * alone is what the screen should show. */
int eegs_present_send(const RECT *client);

/* What the last send did, for the bench row.  Read after eegs_present_send(). */
typedef struct {
    uint32_t groups;         /* GIF groups put on the bus */
    uint32_t qw;             /* quadwords they carried */
    uint32_t verts;          /* vertices recorded this frame */
    uint32_t prims;          /* primitives recorded this frame */
    uint32_t dropped;        /* groups the staging buffer could not hold: must read 0 */
    uint32_t unsplittable;   /* the recorder refused a split: must read 0 */
    uint32_t tex_switches;   /* mid-stream texture register groups */
    uint32_t tex_missing;    /* a textured primitive with no plane: must read 0 */
    /* LANE 12e's survivor: every PRIM re-declaration closes a group, so this is the rate at which
     * the group budget `EEGS_MAX_GROUPS` is spent, and `dropped` is what it costs to outrun it.  The
     * four `chain_*` this row grew for the strip continuation are gone with the rung; its measured
     * verdict, and the `verts`/`prims` ratio that shows a re-lived one, are in doc/txt/PS2.txt. */
    uint32_t prim_moves;     /* PRIM words actually re-sent, each of which closes a group */
    /* LANE 12f rung (ii): the two exits a retained mesh has at `eegs_grid_strip()`.  `grid_batch` is
     * a mesh recorded as one strip, `grid_slow` a mesh the batch refused -- an uncapturable context
     * or a mapped point outside the GS's vertex range -- so the application drew it through the
     * immediate-mode walk instead.  They are columns rather than an assertion because the entry does
     * not clip: the ratio is the only headless evidence that the gate is refusing the frames it
     * should and no others. */
    uint32_t grid_batch, grid_slow;
    /* Which condition made the last `grid_slow` (a `TGL_GRID_REFUSE_*` code, 0 when nothing has
     * refused this frame).  It travels with the pair because the pair is not actionable on its own:
     * `slow=` from a mesh too big for the vertex word and `slow=` from a stale matrix product are
     * the same number about opposite problems. */
    uint32_t grid_why;
    /* The points this frame's TAKEN batches left the clip rect: the amount of clipping the GS's
     * SCISSOR is being asked to do, which is what turns `batch=` from a hope into a reading.  The
     * XMB ribbon's is 32 a frame -- its last column, projected with the window's width and drawn
     * into the client area's -- and `over=` nonzero with the sheet's edge intact on the monitor is
     * the pair that says the reliance is real. */
    uint32_t grid_over;
    /* Does the stream plate its own rect?  The canvas upload precedes it, so wherever the stream
     * does not rasterise the screen shows the canvas, and the compositor is entitled to stop
     * filling the plate under a layer only while that is a null statement.  `clip_w`/`clip_h` are
     * the rect the environment packet scissors, in local pixels; `b0` is the rect the frame's first
     * vertex group provably reaches, in the same units, inclusive at both ends and valid only when
     * `has_bbox` is set.  `plate_covers` is the comparison done where the rect is known: 1 when the
     * first group spans the clip on all four edges, so the reading side has one word to trust
     * rather than four numbers to subtract. */
    uint32_t clip_w, clip_h;
    int32_t  b0_l, b0_t, b0_r, b0_b;
    uint32_t has_bbox;
    uint32_t plate_covers;
    /* LANE 12h's deciding row, and it is compiled only under `PS2_IDENT=1` -- with the dial at 0
     * these read 0 and no hash is folded anywhere.  The unit is the staged group, because a group
     * is what a replay would re-send: this frame's group `i` is compared with the previous
     * presented frame's group `i` by a 64-bit hash folded over its quadwords as the sink copies
     * them.  Identical bytes always hash identical, so `same_*` can only be wrong by a collision
     * while `diff_*` is exact, which is what makes the pair usable as a decision.  `tail_*` counts
     * the groups only one of the two frames has: the compare is positional on purpose, since a
     * replay can only re-send a span that sits at the same index, and an inserted group shifts
     * every later one.  `ident_frames` is which presentation this is -- the first has no
     * predecessor and is not evidence. */
    uint32_t ident_frames;
    uint32_t ident_groups_prev;   /* groups the previous presented frame staged */
    uint32_t same_groups, same_qw;
    uint32_t diff_groups, diff_qw;
    uint32_t tail_groups, tail_qw;
    uint32_t diff_first, diff_last;
    uint32_t diff_more;           /* differences past the eight `eegs_present_ident_at()` names */
    /* The same positional compare split by GS register, over the aligned prefix: how many groups had
     * an invariant XYZ2 (the drawing kick), RGBAQ (the colour) or ST (the texels) lane on its own,
     * whether or not the rest of the group moved.  Only vertex groups -- a tag whose NREG field is 4,
     * whose data quadwords are PRIM, RGBAQ, ST, XYZ2 from quadword 1 -- are in these; an AD group is
     * counted by `same_*`/`diff_*` and folds no lane.  `ident_shape` is the XYZ2 lane read as the
     * DIFFERENCE between consecutive vertices instead of the coordinate, so a group counts in it
     * exactly when it is a rigid translation of last frame's same-index group -- the column that
     * tells a re-solved mesh apart from a moved one.  Each lane is one 32-bit FNV lane, so a lane's
     * INVARIANT claim can only be wrong by a 2^-32 collision while its differing claim is exact, the
     * same one-directional rule the 64-bit group pair holds at 2^-64.  `ident_vertex_groups` is this
     * frame's own vertex-group count -- `groups` minus it is the AD groups -- which is what lets the
     * four lane columns be read as arithmetic against the meshes rather than against all the groups. */
    uint32_t ident_xyz, ident_rgb, ident_st, ident_shape;
    uint32_t ident_vertex_groups;
} eegs_present_stats;

void eegs_present_get_stats(eegs_present_stats *out);

/* The indices of this frame's groups whose hash differs from the same-index group of the previous
 * presented frame, as a short comma list ending in `+n` when there were more than eight.  A static
 * buffer owned by the present, valid until the next send; `""` when the dial is off or nothing
 * differs. */
const char *eegs_present_ident_at(void);

/* Groups this frame has handed to the stage so far -- the recorder's running total, readable from
 * inside the paint callback.  It is what attributes a `[GSTID]` `at=` index to a named stage, so a
 * caller that marks the phase boundaries needs no second model of the stream. */
uint32_t eegs_present_groups_staged(void);

#endif /* BTRON_GL_EEGS_PRESENT_H */
