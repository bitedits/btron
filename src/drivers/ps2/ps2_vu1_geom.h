/*
 * src/drivers/ps2/ps2_vu1_geom.h — VU1 Geometry & Lit-Vertex Streaming Pipeline
 *
 * Clean-room hardware contract for L2a (transform/clip/perspective) and L2b (lit-vertex)
 * executing on Vector Unit 1 (VU1).
 *
 * Implements the streaming double-buffered microcode interface driven via VIF1 DMA.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_PS2_VU1_GEOM_H
#define BTRON_PS2_VU1_GEOM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exported microcode boundaries from ps2_vu1_geom.vsm */
extern const uint32_t VU1GeomPipe_CodeStart[];
extern const uint32_t VU1GeomPipe_CodeEnd[];

/* Uniform locations in VU1 MicroMem (0..5 quadwords) */
#define VU1_MEM_MVP_ROW0    0u
#define VU1_MEM_MVP_ROW1    1u
#define VU1_MEM_MVP_ROW2    2u
#define VU1_MEM_MVP_ROW3    3u
#define VU1_MEM_LIGHT_DIR   4u
#define VU1_MEM_LIGHT_COL   5u

/* Header of each double-buffered packet uploaded via VIF1 UNPACK */
typedef struct {
    float    scale_x;
    float    scale_y;
    float    scale_z;
    float    vert_count;    /* Cast to float for scale vector, read as int by ilw.w */
    uint64_t giftag_lo;     /* GIFTAG with NLOOP=vert_count, FLG=REGLIST */
    uint64_t giftag_hi;     /* REGS=0x5210 (PRIM, RGBAQ, ST, XYZ2) */
} __attribute__((aligned(16))) vu1_packet_header_t;

/*
 * Uploads the VU1GeomPipe microcode to VU1 instruction memory via VIF1 MPG.
 * Returns 0 on success, negative on error.
 */
int ps2_vu1_geom_init(void);

/*
 * Sets active uniforms (MVP matrix, directional light vector and diffuse/ambient colors).
 */
void ps2_vu1_geom_set_mvp(const float mvp[16]);
void ps2_vu1_geom_set_light(const float dir[3], float ambient, const float rgb[3], int enabled);

const float* ps2_vu1_geom_get_mvp(void);
const float* ps2_vu1_geom_get_light_dir(void);
const float* ps2_vu1_geom_get_light_col(void);
int          ps2_vu1_geom_is_inited(void);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_PS2_VU1_GEOM_H */
