/*
 * geo3d.h — MSX Geo3D Geometry Coprocessor (V9968 Cartridge extension).
 *
 * Clean-room C99 implementation of the Alexander Moncks / HRA! Geo3D coprocessor.
 * Handles 3x3 fixed-point matrix transforms (Q2.14), perspective projection,
 * edge clipping, face shading and direct line/polygon rendering into VRAM.
 */

#ifndef _BTRON_EMULATOR_MSX_GEO3D_H_
#define _BTRON_EMULATOR_MSX_GEO3D_H_

#include <stdint.h>
#include "v9938.h"

#define GEO3D_MAX_VERTICES  256u
#define GEO3D_MAX_EDGES     256u
#define GEO3D_MAX_FACES     256u

typedef struct {
    int16_t x, y, z;
} geo3d_vertex_t;

typedef struct {
    uint8_t v0, v1;
} geo3d_edge_t;

typedef struct {
    uint8_t i0, i1, i2, i3;
    int16_t nx, ny, nz;
    uint8_t base_col;
} geo3d_face_t;

typedef struct {
    int16_t sx, sy, z;
    uint8_t flags;
    uint8_t valid;
} geo3d_proj_t;

typedef struct {
    uint8_t         reg_idx;
    uint8_t         regs[128];

    /* Model RAM */
    geo3d_vertex_t  vram[GEO3D_MAX_VERTICES];
    geo3d_edge_t    edges[GEO3D_MAX_EDGES];
    geo3d_face_t    faces[GEO3D_MAX_FACES];

    /* Streaming buffers */
    uint8_t         v_buf[6];
    uint8_t         v_buf_len;
    uint8_t         e_buf[2];
    uint8_t         e_buf_len;
    uint8_t         f_buf[11];
    uint8_t         f_buf_len;

    /* Projected cache for RUN phase */
    geo3d_proj_t    proj[GEO3D_MAX_VERTICES];

    /* Counters */
    uint16_t        cnt_skipped;
    uint16_t        cnt_drawn;
    uint16_t        cnt_culled;

    /* Attached VDP */
    v9938_t        *vdp;
} geo3d_t;

void    geo3d_init(geo3d_t *g);
void    geo3d_reset(geo3d_t *g);
void    geo3d_attach_vdp(geo3d_t *g, v9938_t *v);

uint8_t geo3d_read(geo3d_t *g, uint8_t port);
void    geo3d_write(geo3d_t *g, uint8_t port, uint8_t val);

#endif /* _BTRON_EMULATOR_MSX_GEO3D_H_ */
