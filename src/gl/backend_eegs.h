/*
 * src/gl/backend_eegs.h -- the GS raster stage: three device-space vertices, one GIF group,
 * and the texture switch between them
 *
 * Row 5, steps 5a and 5b of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  It is
 * deliberately not a second `gl_ops_t`: the matrix stack, the lighting, the clipping, the texture
 * state and the display lists all stay TinyGL's, and this is the one thing that replaces -- the
 * span loops.  The seam is `GLContext.draw_triangle_front/back`, which `src/gl/tinygl/vertex.c`
 * assigns per polygon mode and `src/gl/tinygl/clip.c` calls after clipping, so what arrives here
 * is already in the same device coordinates the software rasteriser walks.
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
#include "../drivers/ps2/ps2_gs_tex.h"   /* gs_tex_t, the plane a primitive samples */
#include "grid_map.h"                    /* tgl_grid_map_t, tgl_grid_pt_t: the retained mesh */

/* One rasterised vertex, in the units the software span loops used: `x_px`/`y_px` are device
 * pixels in the GL window's own surface (row 0 is the row the canvas blit treats as the window's
 * top), and `s`/`t` are texture coordinates already in texels -- the GS's own unit, since its
 * CLAMP limits are u10.0 texel coordinates -- so no page arithmetic is done here.
 *
 * The pixels are integers because the software rasteriser's are: `ZBufferPoint`'s x and y are
 * `GLint`, and the viewport map that produced them truncated a float to get them, so carrying the
 * truncation a second time -- into the recorder's float parameter and back out again, two libcalls
 * a coordinate on this CPU -- buys a number the packet cannot hold.  The 12.4 sub-pixel bits the
 * GS field has are then always zero, which is what they were before, because nothing on this path
 * ever had a fraction to put in them. */
typedef struct {
    int32_t  x_px, y_px;
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

/* Enable or disable dual-context interleaving (Context 1 for opaque, Context 2 for translucent). */
void eegs_enable_dual_context(int enable);
int  eegs_is_dual_context(void);

/* One clipped triangle.  TinyGL has already split strips and fans, so a triangle arrives as three
 * vertices whether or not it continues the one before it -- and when its shared edge is bit-for-bit
 * the edge the GS is already standing on, only the third of them is sent, under the strip primitive
 * the recorder implements.  See backend_eegs.c's eegs_triangle() for what that buys and what it
 * costs.  The state is taken by value and corrected against the plane the stream points at, so a
 * caller that asked for a texture with no plane behind it gets an untextured triangle and a count. */
void eegs_triangle(const eegs_vertex_t v[3], eegs_tri_state_t st);

/* One clipped segment (a line's two endpoints) and one clipped point.  The XMB's message-box
 * border is a GL_LINE_LOOP, so the line path is not a future convenience -- it is on screen. */
void eegs_line(const eegs_vertex_t v[2], const eegs_tri_state_t st);
void eegs_point(const eegs_vertex_t v[1], const eegs_tri_state_t st);

/* A retained mesh, drawn as one strip: `rows` x `cols` mapped points in row order, walked in the
 * ribbon's zigzag, for a total of `(rows-1)*cols*2` vertices and that many minus two triangles.
 *
 * LANE 12f rung (i), and the reason it exists is the price in doc/txt/PS2.txt: the immediate-mode
 * walk costs ~3,260 cycles a vertex on this CPU and ~2,600 of those are integer work the mesh does
 * not need, because the mesh's geometry is already in arrays.  The arithmetic this needs is the
 * viewport map's, which is why it takes a `tgl_grid_map_t` captured once per draw and an array of
 * `tgl_grid_pt_t` rather than floats: the float half lives in the one unit compiled to do it on
 * COP1, and nothing here crosses a translation-unit boundary as a float value.
 *
 * Returns the vertices submitted, or -1 and nothing recorded when the map is not a captured one, the
 * grid has no rows to span, or any mapped point falls outside the coordinates the GS's vertex word
 * carries.  A -1 is the caller's instruction to draw the mesh the way it always has; the two exits
 * are counted here, on this entry's own return, and printed as `batch=`/`slow=` in the [GSTS] row
 * (LANE 12f rung (ii)).
 *
 * NOT CLIPPED -- the GS clips it, and what this entry gates is a coordinate the GS cannot represent.
 * The immediate path clips every triangle against the view volume before it records it; this writes
 * the mesh it was handed, after one integer scan proving every mapped point lies inside the
 * -2048 .. +2047 px about the page origin that `gs_vertex_coord16()` can put in XYZ2's 16-bit field.
 * A point outside the clip rect and inside that range is TAKEN and counted in `eegs_grid_over()`,
 * because `eegs_present_send()`
 * programs the GS's SCISSOR to exactly that rect -- the same `client` the vertices' XYOFFSET comes
 * from -- so an overhanging primitive is cut by the hardware rather than by ~3,260 EE cycles a
 * vertex.  It used to refuse on the rect instead, which is derived and measured in `grid_map.h` and
 * `verify/tests/test_grid_map.c`: true of the clipper, and fatal for the one mesh this lane was
 * written for, because the app projects the XMB's sheet with the window's size and draws it into the
 * client area's, so its last column is 8 px outside on every frame of the wave's period.  The scan
 * runs before anything is recorded, so a refusal leaves the stream as it found it. */
int eegs_grid_strip(const tgl_grid_map_t *m, const tgl_grid_pt_t *pts, int rows, int cols);

/* A retained textured mesh, drawn as a run of quads: `nquads` groups of four mapped points in the
 * app's own corner order, each split (v0,v1,v2) and (v0,v2,v3) exactly as `vertex.c` splits
 * GL_QUADS, for twice that many triangles.  LANE 12h rung (i) -- the menu's icons and glyph rows,
 * the same sentence as the strip's applied to the mesh that carries texel coordinates: the map is
 * `tgl_grid_map_tex()`'s, the points are `tgl_grid_map_tex_points()`'s, and the plane is resolved
 * here (`gs_tex_find(m->pixmap)`), once for the run, because the registry is this stage's
 * vocabulary and not the float unit's.  The gates, the refusal-before-recording law, and the
 * `batch=`/`slow=`/`why=`/`over=` counters are `eegs_grid_strip()`'s unchanged -- one mesh, one
 * decision, one set of columns whichever entry takes it.  Returns the triangles submitted, or -1
 * with the caller's map counted as the slow path. */
int eegs_tex_quads(const tgl_tex_map_t *m, const tgl_tex_pt_t *pts, int nquads);

/* The two exits of the entry above, counted since `eegs_frame_begin()`.  Read for the [GSTS] row;
 * they sum to the number of retained meshes this frame asked to batch, because the counting happens
 * on the one return path rather than in each caller.  `eegs_grid_why()` is which of them refused
 * last -- a `TGL_GRID_REFUSE_*` code from `grid_map.h`, `TGL_GRID_REFUSE_NONE` when nothing refused
 * -- because `slow=` alone cannot separate a mesh too large for the vertex word from a context the
 * two-constant map cannot represent, and those two want opposite fixes.  `eegs_grid_over()` is the
 * points the taken batches asked the GS's clip window to cut, which is what says the reliance is
 * being exercised rather than assumed; a refused mesh adds nothing to it, for the same reason it
 * adds nothing to the stream. */
uint32_t eegs_grid_batch(void);
uint32_t eegs_grid_slow(void);
uint32_t eegs_grid_why(void);
uint32_t eegs_grid_over(void);

/* Point the following primitives at a texture plane, or at none.  Row 5b's whole step: the
 * environment packet writes `TEX0` once per frame and the XMB binds the icon sheet and the font
 * sheet in the same one, so a plane change has to arrive between the vertex groups -- as a
 * recorder AD group of `TEX0`, `TEX1`, `CLAMP`, which is what `gs_tex_register()` planned when the
 * app uploaded the bytes.
 *
 * `t` is the caller's `gs_tex_find()` answer for the bound image: a plane, or 0 when no texture is
 * bound *or* when the bound plane never got a VRAM address.  Either 0 clears what the stream
 * points at, because a textured primitive with no plane must not read the page the last one left
 * in `TEX0` -- `eegs_triangle()` turns its `TME` off and counts the miss instead.
 *
 * Calling it with the plane the stream already points at emits nothing, which is what keeps the
 * ribbon's 1,828 consecutive triangles in one run of groups. */
void eegs_texture(const gs_tex_t *t);

/* Primitives handed to the recorder -- triangles, segments and points counted together, because
 * the question a caller asks is "was this layer recorded at all", and a frame of line loops would
 * read as nothing if only triangles counted.  Reset with the frame. */
uint32_t eegs_prims(void);
uint32_t eegs_verts(void);

/* The plane changes the stream actually paid for, and the textured primitives that had no plane
 * to sample and were therefore recorded untextured.  The second is the one a bench logs: a non-zero
 * count means an uploaded texture is drawing as a flat quad, which is a registry problem rather
 * than a GS one, and it is the only evidence a headless run has of it. */
uint32_t eegs_tex_switches(void);
uint32_t eegs_tex_missing(void);

/* How many times the stream re-declared its PRIM.  Every one of them closes the open group, so this
 * is the group-boundary rate and `EEGS_MAX_GROUPS` in `eegs_present.h` is its budget: when the rate
 * outruns the budget the stage table fills and `drop=` counts the primitives this frame drew and the
 * screen did not get.  LANE 12e's four `chain_*` exits are gone with the rung they measured -- its
 * verdict, and the `verts=`/`prims=` ratio that shows a re-lived one, are in doc/txt/PS2.txt.  Resets
 * with the frame, like the two above. */
uint32_t eegs_prim_moves(void);

#endif /* BTRON_GL_BACKEND_EEGS_H */
