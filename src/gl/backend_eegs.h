/*
 * src/gl/backend_eegs.h -- PlayStation 2 GS (Graphics Synthesizer) raster stage:
 * device-space vertex packaging, GIF packet groups, and texture plane state switching.
 *
 * Architecture Overview:
 * TinyGL provides the matrix stack, lighting, clipping, texture state, and display lists.
 * This backend replaces the software span rasterization loops with direct hardware rendering
 * to the Sony PS2 Graphics Synthesizer (GS).
 *
 * Coordinates are passed directly through from the window device space rather than re-derived.
 * The viewport mapping, window origin, and y-direction are preserved in `zp.x`/`zp.y` to ensure
 * exact alignment between the GS hardware layers and the 2D canvas layers.
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
    uint32_t z;
    float    s, t;
    uint32_t rgba;            /* r in bits 7:0, g 15:8, b 23:16, a 31:24 */
} eegs_vertex_t;

/* Which of the three state flags the triangle carries: Gouraud colour, a texture read, and the
 * ALPHA register's blend. They map to the PRIM word's IIP, TME, and ABE bits.
 *
 * `brighten` represents destination-brighten blending: dst * (1 + src).
 * The GS hardware ALPHA blender evaluates:
 *     Color = (A - B) * C >> 7 + D
 * In Context 2, ALPHA is configured as `(Cd - 0) * As >> 7 + Cd` = `Cd * (1 + As/128)`.
 * Enabling `brighten` routes the primitive to Context 2 (CTXT=1) and instructs the vertex
 * emitter to place the colour luminance into the scalar alpha channel (As).
 */
typedef struct {
    unsigned smooth : 1;      /* IIP: interpolate vertex colours; 0 = flat shade */
    unsigned textured : 1;    /* TME: enable texture mapping */
    unsigned blended : 1;     /* ABE: enable alpha blending */
    unsigned brighten : 1;    /* Destination-brighten blend (CTXT=1, brightness in As) */
    unsigned depth_test : 1;  /* Z test enable */
} eegs_tri_state_t;

/* PRIM Chain Record: describes a sequence of primitives sharing an identical PRIM state word.
 *
 * Tracks individual drawing passes, vertex counts, submitted primitives, bounding box,
 * and initial/final vertex colours to enable fine-grained draw-call inspection.
 */
#define EEGS_CHAIN_MAX 40u

typedef struct {
    uint32_t prim;            /* PRIM word: OP[2:0] IIP[3] TME[4] FGE[5] ABE[6] AA1[7] FST[8] CTXT[9] */
    uint32_t nverts;          /* Total vertices emitted in this chain */
    uint32_t nprims;          /* Total primitives completed in this chain */
    int32_t  l, t, r, b;      /* Bounding box in local pixels (inclusive); l > r if empty */
    uint32_t c0, clast;       /* First and last RGBA colour words in the chain */
} eegs_chain_rec;

/* Chains closed by the currently staged frame. Valid after `eegs_frame_end()`. */
uint32_t eegs_chain_count(void);
uint32_t eegs_chain_dropped(void);
const eegs_chain_rec *eegs_chain_at(uint32_t i);

/* Chain Cutoff Limit: diagnostic cutoff for stream bisection.
 *
 * Limits rendering to the first N chains (0..N-1), discarding subsequent declarations.
 * Used during diagnostics to isolate rendering passes and verify multi-layer compositing.
 * Set to 0 to disable limiting (default production behavior).
 */
void     eegs_chain_set_limit(uint32_t n);
uint32_t eegs_chain_limit(void);
uint32_t eegs_chain_refused(void);     /* Count of chain declarations discarded */

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
void eegs_clear(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgba);


/* A retained mesh, drawn as one continuous triangle strip: `rows` x `cols` mapped points
 * in row order, traversed in alternating zigzag order, producing `(rows-1)*cols*2` vertices
 * and `(rows-1)*cols*2 - 2` triangles.
 *
 * Performance Architecture:
 * Bypasses the general-purpose immediate-mode vertex pipeline. Takes a precomputed `tgl_grid_map_t`
 * and array of integer `tgl_grid_pt_t` points.
 *
 * Hardware Scissoring vs. Software Clipping:
 * Rather than clipping every triangle against the view volume in software, the entire mesh is
 * submitted directly to the GS hardware after validating that all points fit within the 12.4
 * fixed-point coordinate range (-2048 .. +2047 px, `gs_vertex_coord16()`). Points outside the
 * active viewport are clipped in hardware by the GS SCISSOR register, saving substantial CPU cycles.
 *
 * Returns vertices submitted, or -1 if the map is invalid or coordinates exceed representation limits.
 */
int eegs_grid_strip(const tgl_grid_map_t *m, const tgl_grid_pt_t *pts, int rows, int cols);

/* A retained textured mesh, drawn as a sequence of textured quads: `nquads` groups of four
 * mapped points split into two triangles (v0, v1, v2) and (v0, v2, v3).
 *
 * Resolves texture binding (`gs_tex_find(m->pixmap)`) once for the entire batch. Applies the same
 * coordinate range validation and hardware scissoring as `eegs_grid_strip()`.
 * Returns triangles submitted, or -1 on refusal.
 */
int eegs_tex_quads(const tgl_tex_map_t *m, const tgl_tex_pt_t *pts, int nquads);

/* Batch rendering metrics for retained meshes:
 * - `eegs_grid_batch()`: Count of retained meshes accepted and batched as strips/quads.
 * - `eegs_grid_slow()`: Count of retained meshes refused (falling back to immediate-mode walk).
 * - `eegs_grid_why()`: Last refusal reason code (`TGL_GRID_REFUSE_*` from `grid_map.h`).
 * - `eegs_grid_over()`: Total vertex count that fell outside viewport bounds but was clipped
 *   by hardware SCISSOR.
 */
uint32_t eegs_grid_batch(void);
uint32_t eegs_grid_slow(void);
uint32_t eegs_grid_why(void);
uint32_t eegs_grid_over(void);

/* Dynamic Texture Plane Switching:
 * Directs subsequent primitives to sample texture plane `t`, or unbinds texture if NULL.
 *
 * Emits an AD (Address + Data) packet with `TEX0`, `TEX1`, and `CLAMP` registers between GIF
 * vertex groups. Redundant calls with the currently bound texture are elided to avoid breaking
 * the active GIF packet group.
 */
void eegs_texture(const gs_tex_t *t);

/* Primitives handed to the recorder -- triangles, segments and points counted together.
 * Reset with each frame. */
uint32_t eegs_prims(void);
uint32_t eegs_verts(void);

/* Texture Plane Diagnostic Counters:
 * - `eegs_tex_switches()`: Number of active texture plane changes emitted.
 * - `eegs_tex_missing()`: Number of textured primitives demoted to untextured due to an
 *   unregistered or missing texture plane.
 */
uint32_t eegs_tex_switches(void);
uint32_t eegs_tex_missing(void);

/* State Change Rate:
 * Counts the number of times PRIM was re-declared. Each declaration closes the open GIF group.
 * Monitored against `EEGS_MAX_GROUPS` to prevent ring buffer exhaustion.
 */
uint32_t eegs_prim_moves(void);

/* Vertex Colour Variance Diagnostics:
 * Tracks transitions in emitted vertex colours to diagnose shading issues:
 * - `changes`: Count of adjacent vertices with differing colour words.
 * - `verts`: Total vertices evaluated.
 * - `first`/`last`: First and last RGBA colour values encountered in the frame.
 * A frame rendering smooth gradients reports `changes ~ verts`. A flat-shaded frame reports 0.
 */
uint32_t eegs_colour_changes(void);
uint32_t eegs_colour_verts(void);
uint32_t eegs_colour_first(void);
uint32_t eegs_colour_last(void);

/* Shading Mode Verification Counters:
 * - `eegs_prim_iip()`: Count of declared PRIMs requesting Gouraud shading (IIP bit 3).
 * - `eegs_prim_tme()`: Count of declared PRIMs requesting texture mapping (TME bit 4).
 * - `eegs_prim_decl()`: Total PRIM declarations processed.
 */
uint32_t eegs_prim_iip(void);
uint32_t eegs_prim_tme(void);
uint32_t eegs_prim_decl(void);

/* Large Geometry Diagnostics:
 * Distinguishes large background backdrop quads from small UI elements by area:
 * - `wide`: Triangles exceeding the wide threshold (32,768 doubled pixels).
 * - `wideflat`: Large triangles where all three vertex colours are identical.
 * - `big_*`: Area, corner colours, and smooth flag of the single largest triangle in the frame.
 */
uint32_t eegs_wide_tri(void);
uint32_t eegs_wide_flat(void);
uint32_t eegs_big_area(void);
uint32_t eegs_big_c0(void);
uint32_t eegs_big_c1(void);
uint32_t eegs_big_c2(void);
uint32_t eegs_big_smooth(void);

/* Returns 1 if any primitive in the current frame requested depth testing. */
int eegs_depth_test_active(void);

/* Set the window origin and clip scissor for subsequent primitives in the stream. */
void eegs_set_window_origin(int32_t win_x, int32_t win_y,
                            int32_t clip_x0, int32_t clip_y0,
                            int32_t clip_x1, int32_t clip_y1);

#endif /* BTRON_GL_BACKEND_EEGS_H */
