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

void ps2_vu0_hopf_fiber_full_batch(float cos_eta, float sin_eta, float beta, float psi,
                                   int num_pts, const vu0_hopf_params_t *params,
                                   vu0_vec4_t *out_r3)
{
    if (!params || !out_r3 || num_pts <= 0) return;

    const float m00 = params->m[0][0], m01 = params->m[0][1], m02 = params->m[0][2];
    const float m10 = params->m[1][0], m11 = params->m[1][1], m12 = params->m[1][2];
    const float m20 = params->m[2][0], m21 = params->m[2][1], m22 = params->m[2][2];
    const float scale = params->scale;
    const float z_off = params->z_offset;
    const float min_d = params->min_denom > 0.0f ? params->min_denom : 0.08f;
    const float beta_half = beta * 0.5f;
    const float psi_scaled = psi * 0.8f;
    const float inv_pts = (2.0f * (float)M_PI) / (float)num_pts;

    const float c1 = beta_half + psi;
    const float c2 = -beta_half + psi_scaled;
    const float cos_c1 = cosf(c1);
    const float sin_c1 = sinf(c1);
    const float cos_c2 = cosf(c2);
    const float sin_c2 = sinf(c2);

    static float s_cached_cos_xi[65];
    static float s_cached_sin_xi[65];
    static int s_cached_pts = 0;

    if (s_cached_pts != num_pts) {
        for (int p = 0; p <= num_pts && p <= 64; p++) {
            const float xi = (float)p * inv_pts;
            s_cached_cos_xi[p] = cosf(xi);
            s_cached_sin_xi[p] = sinf(xi);
        }
        s_cached_pts = num_pts;
    }

    for (int p = 0; p <= num_pts; p++) {
        const float cos_xi = s_cached_cos_xi[p];
        const float sin_xi = s_cached_sin_xi[p];

        const float cos_a1 = cos_xi * cos_c1 - sin_xi * sin_c1;
        const float sin_a1 = sin_xi * cos_c1 + cos_xi * sin_c1;
        const float cos_a2 = cos_xi * cos_c2 - sin_xi * sin_c2;
        const float sin_a2 = sin_xi * cos_c2 + cos_xi * sin_c2;

        const float x0 = cos_eta * cos_a1;
        const float x1 = cos_eta * sin_a1;
        const float x2 = sin_eta * cos_a2;
        const float x3 = sin_eta * sin_a2;

        float denom = 1.08f - x3;
        if (denom < min_d) denom = min_d;
        const float q = 1.0f / denom;

        const float X3d = x0 * q;
        const float Y3d = x1 * q;
        const float Z3d = x2 * q;

        const float rx = m00 * X3d + m01 * Y3d + m02 * Z3d;
        const float ry = m10 * X3d + m11 * Y3d + m12 * Z3d;
        const float rz = m20 * X3d + m21 * Y3d + m22 * Z3d;

        out_r3[p].x = rx * scale;
        out_r3[p].y = ry * scale;
        out_r3[p].z = rz * scale + z_off;
        out_r3[p].w = 1.0f;
    }
}

void ps2_vu0_damp_batch(float *current, const float *target, float lambda, float dt, size_t count)
{
    if (!current || !target || count == 0) return;
    const float factor = expf(-lambda * dt);
    for (size_t i = 0; i < count; i++) {
        current[i] = target[i] + (current[i] - target[i]) * factor;
    }
}

void ps2_vu0_damp_angle_batch(float *current, const float *target, float lambda, float dt, size_t count)
{
    if (!current || !target || count == 0) return;
    const float factor = expf(-lambda * dt);
    const float two_pi = (float)(2.0 * M_PI);
    for (size_t i = 0; i < count; i++) {
        float diff = target[i] - current[i];
        while (diff < -(float)M_PI) diff += two_pi;
        while (diff >  (float)M_PI) diff -= two_pi;
        current[i] = current[i] + diff * (1.0f - factor);
    }
}

void ps2_vu0_circle_ring_batch(float r, size_t num_segments, vu0_vec4_t *out_pts)
{
    if (!out_pts || num_segments == 0) return;
    const float inv_seg = (2.0f * (float)M_PI) / (float)num_segments;
    for (size_t a = 0; a <= num_segments; a++) {
        const float ang = (float)a * inv_seg;
        out_pts[a].x = cosf(ang) * r;
        out_pts[a].y = sinf(ang) * r;
        out_pts[a].z = 0.0f;
        out_pts[a].w = 1.0f;
    }
}

