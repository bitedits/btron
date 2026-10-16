/*
 * src/drivers/ps2/ps2_vu0_kernel.h — VU0 SIMD Procedural Geometry Kernel
 *
 * Implements Layer 3 (L3) of the Clean-Room OpenGL Architecture.
 * Offloads procedural geometric generation:
 *   1. Lil Cu 64: 4D Hopf Fibration S³ → ℝ³ stereographic projection & 3D rotation.
 *   2. XMB: Dynamic floating ribbon lattice synthesis.
 *
 * Designed to execute on Emotion Engine Coprocessor 2 (COP2 / VU0 Macro Mode)
 * with direct RDRAM access via lqc2/sqc2.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_PS2_VU0_KERNEL_H
#define BTRON_PS2_VU0_KERNEL_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float x, y, z, w;
} __attribute__((aligned(16))) vu0_vec4_t;

typedef struct {
    float m[3][3];       /* 3x3 Yaw-Pitch-Roll rotation matrix */
    float scale;         /* Uniform post-rotation scale */
    float z_offset;      /* Translation offset in Z */
    float min_denom;     /* Denominator clamp epsilon (default: 0.08f) */
} vu0_hopf_params_t;

/*
 * Evaluates the stereographic projection S³ → ℝ³ and 3D Euler rotation
 * for a batch of 4D hyperspherical coordinates.
 *
 * Input:
 *   s3_pts:  Array of N 4D points on S³ (x0, x1, x2, x3)
 *   params:  Projection and rotation parameters
 * Output:
 *   out_r3:  Array of N 3D vertices (X, Y, Z, 1.0f)
 */
void ps2_vu0_hopf_project_batch(const vu0_vec4_t *s3_pts,
                                size_t n,
                                const vu0_hopf_params_t *params,
                                vu0_vec4_t *out_r3);

/*
 * Evaluates a ribbon lattice segment for the XMB desktop wave.
 *
 * Input:
 *   phase, amplitude, frequency: wave dynamics parameters
 *   cols, rows: lattice resolution
 * Output:
 *   out_grid: Output grid vertices (X, Y, Z, 1.0f)
 */
void ps2_vu0_ribbon_lattice_batch(float phase, float amplitude, float frequency,
                                  uint32_t cols, uint32_t rows,
                                  vu0_vec4_t *out_grid);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_PS2_VU0_KERNEL_H */
