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
} eegs_present_stats;

void eegs_present_get_stats(eegs_present_stats *out);

#endif /* BTRON_GL_EEGS_PRESENT_H */
