/*
 * src/gl/grid_map.h — Viewport map extraction for retained mesh batching
 *
 * Architecture & Design:
 * Retained meshes (such as animated 2D/3D wave ribbons and UI grids) submit thousands
 * of vertices per frame that share identical transformation matrices, lighting states,
 * and viewport mappings.
 *
 * Rather than subjecting every vertex to the general TinyGL pipeline (matrix loads,
 * homogenous divide, clipping, and float-to-int viewport scaling in immediate mode),
 * this module extracts the active model-view-projection and viewport constants into
 * a lightweight snapshot (`tgl_grid_map_t` / `tgl_tex_map_t`).
 *
 * ABI Boundary & Float Isolation:
 * Under the MIPS/PS2 ABI, floating-point operations are partitioned cleanly:
 * `tgl_grid_map()` and `tgl_grid_map_points()` reside in `src/gl/tinygl/vertex_stage.c`
 * compiled with `-mhard-float`. The downstream consumer (`src/gl/backend_eegs.c`)
 * is compiled with `-msoft-float` and consumes pre-projected integer device pixels
 * (`tgl_grid_pt_t` / `tgl_tex_pt_t`), ensuring no floats cross translation unit boundaries.
 *
 * Transformation Algebra:
 * For planar 2D/affine geometry (where Z=0 and W=1):
 *     pc.X = x * mx + xc
 *     pc.Y = y * my + yc
 * Screen coordinates map directly to:
 *     screen_x = trans_x + (pc.X * winv) * scale_x
 *     screen_y = trans_y + (pc.Y * winv) * scale_y
 *
 * Fallback & Integrity:
 * If the OpenGL context is not strictly affine/orthogonal (e.g. general 3D perspective
 * coupling X to Y, non-constant W, or dynamic vertex lighting), `tgl_grid_map()` refuses
 * fast-path extraction with a specific refusal code (`TGL_GRID_REFUSE_*`), prompting
 * the renderer to gracefully fall back to the standard immediate-mode vertex pipeline.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_GRID_MAP_H
#define BTRON_GL_GRID_MAP_H

#include <stdint.h>

/* Refusal reasons returned by tgl_grid_map() when context cannot be batched */
#define TGL_GRID_REFUSE_NONE  0u
#define TGL_GRID_REFUSE_ARG   1u    /* Null parameter pointer */
#define TGL_GRID_REFUSE_STALE 2u    /* Model-projection product is dirty and uncomputed */
#define TGL_GRID_REFUSE_LIGHT 3u    /* Lighting enabled: vertex colors require lighting stage */
#define TGL_GRID_REFUSE_TEX   4u    /* Texture 2D enabled in untextured map request */
#define TGL_GRID_REFUSE_W     5u    /* Homogeneous W coordinate depends on vertex position */
#define TGL_GRID_REFUSE_SKEW  6u    /* Non-diagonal projection couples X and Y axes */
#define TGL_GRID_REFUSE_RECT  7u    /* Viewport bounds rejection (handled by GS hardware scissoring) */
#define TGL_GRID_REFUSE_UNSET 8u    /* Map was never captured (uninitialized struct) */
#define TGL_GRID_REFUSE_RANGE 9u    /* Mapped point exceeds 12-bit GS integer coordinate bounds (-2048..+2047) */
#define TGL_GRID_REFUSE_NOTEX 10u   /* Textured map requested without valid bound and uploaded texture */

/* Snapshot of affine transformation, viewport scaling, and raster state */
typedef struct {
    float    mx, xc;
    float    my, yc;
    float    winv;
    float    scale_x, trans_x;
    float    scale_y, trans_y;
    uint32_t a_byte;
    uint32_t smooth;
    uint32_t blended;
    uint32_t brighten;    /* Blending law: 1 for GL_DST_COLOR/GL_ONE additive brighten, 0 for standard src-over */
    int32_t  vp_x0, vp_x1, vp_y0, vp_y1;
    uint32_t refuse;
    uint32_t ok;
} tgl_grid_map_t;

/* Single mapped vertex point in device integer pixel coordinates */
typedef struct {
    int32_t  x_px, y_px;
    uint32_t rgba;
} tgl_grid_pt_t;

/* Snapshot of textured transformation state including UV texture coordinate scaling */
typedef struct {
    tgl_grid_map_t g;
    float          sscale, soff;
    float          tscale, toff;
    const void    *pixmap;
} tgl_tex_map_t;

/* Single mapped textured vertex point with ST coordinates and 32-bit RGBA color */
typedef struct {
    int32_t  x_px, y_px;
    float    s, t;
    uint32_t rgba;
} tgl_tex_pt_t;

/*
 * Extract affine transformation and viewport state from the active TinyGL context.
 * Returns 0 and populates `out` on success.
 * Returns -1 and sets `out->refuse` if state is incompatible with batch mapping.
 */
int tgl_grid_map(tgl_grid_map_t *out);

/*
 * Batch-transforms `n` mesh points using pre-computed viewport mapping constants.
 * `xs`, `ys`, and `bri` provide vertex positions and greyscale intensity values.
 * Writes output integer device coordinates and packed colors into `out`.
 */
void tgl_grid_map_points(const tgl_grid_map_t *m, const float *xs, const float *ys,
                         const float *bri, int n, tgl_grid_pt_t *out);

/*
 * Extract affine transformation, viewport state, and texture mapper constants.
 * Requires GL_TEXTURE_2D to be enabled with an uploaded texture pixmap.
 * Returns 0 on success, or -1 with refusal code in `out->g.refuse`.
 */
int tgl_grid_map_tex(tgl_tex_map_t *out);

/*
 * Batch-transforms `n` textured vertices using pre-computed viewport and UV constants.
 * Takes positions (`xs`, `ys`), UV coordinates (`us`, `vs`), and interleaved RGBA (`cols`).
 * Writes device coordinates, ST texture coordinates, and packed RGBA into `out`.
 */
void tgl_grid_map_tex_points(const tgl_tex_map_t *m, const float *xs, const float *ys,
                             const float *us, const float *vs, const float *cols, int n,
                             tgl_tex_pt_t *out);

#endif /* BTRON_GL_GRID_MAP_H */
