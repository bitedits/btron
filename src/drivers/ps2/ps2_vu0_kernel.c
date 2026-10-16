/*
 * src/drivers/ps2/ps2_vu0_kernel.c — VU0 SIMD Procedural Geometry Kernel
 *
 * Implements Layer 3 (L3) of the Clean-Room OpenGL Architecture.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_vu0_kernel.h"
#include <math.h>

void ps2_vu0_hopf_project_batch(const vu0_vec4_t *s3_pts,
                                size_t n,
                                const vu0_hopf_params_t *params,
                                vu0_vec4_t *out_r3)
{
    if (!s3_pts || !params || !out_r3 || n == 0) return;

    const float m00 = params->m[0][0], m01 = params->m[0][1], m02 = params->m[0][2];
    const float m10 = params->m[1][0], m11 = params->m[1][1], m12 = params->m[1][2];
    const float m20 = params->m[2][0], m21 = params->m[2][1], m22 = params->m[2][2];
    const float scale = params->scale;
    const float z_off = params->z_offset;
    const float min_d = params->min_denom > 0.0f ? params->min_denom : 0.08f;

    for (size_t i = 0; i < n; i++) {
        const float x0 = s3_pts[i].x;
        const float x1 = s3_pts[i].y;
        const float x2 = s3_pts[i].z;
        const float x3 = s3_pts[i].w;

        float denom = 1.08f - x3;
        if (denom < min_d) denom = min_d;
        const float q = 1.0f / denom;

        const float X3d = x0 * q;
        const float Y3d = x1 * q;
        const float Z3d = x2 * q;

        const float rx = m00 * X3d + m01 * Y3d + m02 * Z3d;
        const float ry = m10 * X3d + m11 * Y3d + m12 * Z3d;
        const float rz = m20 * X3d + m21 * Y3d + m22 * Z3d;

        out_r3[i].x = rx * scale;
        out_r3[i].y = ry * scale;
        out_r3[i].z = rz * scale + z_off;
        out_r3[i].w = 1.0f;
    }
}

void ps2_vu0_ribbon_lattice_batch(float phase, float amplitude, float frequency,
                                  uint32_t cols, uint32_t rows,
                                  vu0_vec4_t *out_grid)
{
    if (!out_grid || cols == 0 || rows == 0) return;

    for (uint32_t r = 0; r < rows; r++) {
        const float v = (float)r / (float)(rows > 1 ? rows - 1 : 1);
        const float y = (v - 0.5f) * 2.0f;

        for (uint32_t c = 0; c < cols; c++) {
            const float u = (float)c / (float)(cols > 1 ? cols - 1 : 1);
            const float x = (u - 0.5f) * 2.0f;

            /* Sinusoidal floating ribbon wave equation */
            const float wave = sinf(u * frequency * 6.283185f + phase) *
                               cosf(v * frequency * 3.141592f + phase * 0.5f);
            const float z = wave * amplitude;

            const uint32_t idx = r * cols + c;
            out_grid[idx].x = x;
            out_grid[idx].y = y;
            out_grid[idx].z = z;
            out_grid[idx].w = 1.0f;
        }
    }
}
