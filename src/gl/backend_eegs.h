/*
 * src/gl/backend_eegs.h -- the GS raster stage: three device-space vertices, one GIF group
 *
 * Row 5, step 5a of the plan of record (doc/md/PS2.md, "The EE/GS backend").  It is deliberately
 * not a second `gl_ops_t`: the matrix stack, the lighting, the clipping, the texture state and the
 * display lists all stay TinyGL's, and this is the one thing that replaces -- the span loops.  The
 * seam is `GLContext.draw_triangle_front/back`, which `src/gl/tinygl/vertex.c` assigns per
 * polygon mode and `src/gl/tinygl/clip.c` calls after clipping, so what arrives here is already
 * in the same device coordinates the software rasteriser walks.
 *
 * Hence the law this file is built on: the coordinates are passed through, never re-derived.  The
 * y direction, the viewport map and the window's own origin are already in `zp.x`/`zp.y` -- which
 * is what makes the GS layer and the canvas layer agree without a second model of either, and why
 * there is no projection here to get subtly wrong.
 *
 * The input is a plain struct of scalars rather than `GLVertex` pointers, so that the whole of
 * this mapping can be compiled and gated on a host with no GL context to build.  The adapter that
 * reads the `GLVertex` fields lives with them, in `clip.c`.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_BACKEND_EEGS_H
#define BTRON_GL_BACKEND_EEGS_H

#include <stdint.h>

#include "../drivers/ps2/ps2_gs_rec.h"   /* gs_rec_sink, the type a frame's stream is handed to */

/* One rasterised vertex, in the units the software span loops used: `x_px`/`y_px` are device
 * pixels in the GL window's own surface (row 0 is the row the canvas blit treats as the window's
 * top), and `s`/`t` are texture coordinates already in texels -- the GS's own unit, since its
 * CLAMP limits are u10.0 texel coordinates -- so no page arithmetic is done here. */
typedef struct {
    float    x_px, y_px;
    float    s, t;
    uint32_t rgba;            /* r in bits 7:0, g 15:8, b 23:16, a 31:24 */
} eegs_vertex_t;

/* Which of the three state flags the triangle carries: Gouraud colour, a texture read, and the
 * ALPHA register's blend.  They are the PRIM word's IIP, TME and ABE, and they are the only
 * primitive state a recorded triangle needs, because the environment packet owns everything else.
 */
typedef struct {
    unsigned smooth : 1;      /* IIP: interpolate the three colours, else the first one */
    unsigned textured : 1;    /* TME */
    unsigned blended : 1;     /* ABE */
} eegs_tri_state_t;

/* A frame starts here: the recorder's counters are per-frame and its open group must not survive
 * into the next one.  The sink is the caller's because it is the presentation seam's business, not
 * this layer's -- 0 records without sending, which is what a build that has no GS present does, and
 * what the host gate inspects. */
void eegs_frame_begin(gs_rec_sink sink, void *user);

/* Any vertices the application drew but did not finish (an open glBegin with no glEnd). */
void eegs_frame_end(void);

/* One clipped triangle.  Three vertices, always: TinyGL has already split strips and fans. */
void eegs_triangle(const eegs_vertex_t v[3], const eegs_tri_state_t st);

/* One clipped segment (a line's two endpoints) and one clipped point.  The XMB's message-box
 * border is a GL_LINE_LOOP, so the line path is not a future convenience -- it is on screen. */
void eegs_line(const eegs_vertex_t v[2], const eegs_tri_state_t st);
void eegs_point(const eegs_vertex_t v[1], const eegs_tri_state_t st);

/* Primitives handed to the recorder -- triangles, segments and points counted together, because
 * the question a caller asks is "was this layer recorded at all", and a frame of line loops would
 * read as nothing if only triangles counted.  Reset with the frame. */
uint32_t eegs_prims(void);
uint32_t eegs_verts(void);

#endif /* BTRON_GL_BACKEND_EEGS_H */
