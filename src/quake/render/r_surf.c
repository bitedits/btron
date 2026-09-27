/*
 * src/quake/render/r_surf.c — Turbulent Surface Warping (Water/Slime/Lava)
 *
 * Quake uses sine-wave vertex displacement for animated fluid surfaces
 * (texture names prefixed with '*').  This module provides:
 *   - R_DrawTurbSurface: emits a warped polygon using texcoord perturbation
 *   - R_DrawSkySurface: renders scrolling dual-layer sky dome
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/r_surf.h"
#include "../include/world.h"
#include "../include/texture.h"
#include "../../gl/gl_dispatch.h"

/* ── Turb sine table (256 entries, Quake standard) ─────────────────── */
#define TURB_SIZE   256
#define TURB_MASK   (TURB_SIZE - 1)
#define TURB_SCALE  8.0f   /* amplitude in texture units */
#define TURB_SPEED  0.5f   /* Hz */

static float s_turb_sin[TURB_SIZE];
static int   s_turb_init = 0;
static float s_surf_time = 0.0f;

static void init_turb_table(void) {
    if (s_turb_init) return;
    for (int i = 0; i < TURB_SIZE; i++) {
        /* Use integer approximation: sin(2π·i/256) */
        float angle = (float)i * (6.28318f / (float)TURB_SIZE);
        /* freestanding sin approximation: Taylor series 5-term */
        float x = angle;
        while (x > 3.14159f)  x -= 6.28318f;
        while (x < -3.14159f) x += 6.28318f;
        float x2 = x*x;
        s_turb_sin[i] = x * (1.0f - x2*(0.16667f - x2*(0.00833f - x2*0.000198f)));
    }
    s_turb_init = 1;
}

void R_UpdateSurfTime(float dt) {
    init_turb_table();
    s_surf_time += dt;
}

/* ── Turbulent polygon emitter ──────────────────────────────────────── */
void R_DrawTurbSurface(const dface_t *face, const texinfo_t *ti,
                       int tex_w, int tex_h, float light) {
    if (!g_gl || !face) return;

    /* phase offset in table units */
    int phase = (int)(s_surf_time * TURB_SPEED * (float)TURB_SIZE) & TURB_MASK;

    glColor3f(light, light * 0.88f, light * 0.72f); /* warm water tint */
    glBegin(GL_POLYGON);

    for (int e = 0; e < face->numedges; e++) {
        int se = face->firstedge + e;
        if (se >= g_world.numsurfedges) break;
        int ev = g_world.surfedges[se];
        int vi = (ev >= 0) ? g_world.edges[ev].v[0]
                           : g_world.edges[-ev].v[1];
        if (vi >= g_world.numvertexes) continue;
        const float *p = g_world.vertexes[vi].point;

        /* Base UV from texinfo */
        float s = p[0]*ti->vecs[0][0] + p[1]*ti->vecs[0][1]
                + p[2]*ti->vecs[0][2] + ti->vecs[0][3];
        float t = p[0]*ti->vecs[1][0] + p[1]*ti->vecs[1][1]
                + p[2]*ti->vecs[1][2] + ti->vecs[1][3];

        /* Sine perturbation */
        int si = ((int)(t * 0.125f) + phase) & TURB_MASK;
        int ti_idx = ((int)(s * 0.125f) + phase) & TURB_MASK;
        s += s_turb_sin[si]  * TURB_SCALE;
        t += s_turb_sin[ti_idx] * TURB_SCALE;

        if (tex_w > 0) s /= (float)tex_w;
        if (tex_h > 0) t /= (float)tex_h;

        glTexCoord2f(s, t);
        glVertex3f(p[0], p[1], p[2]);
    }
    glEnd();
}

/* ── Sky surface: dual-layer scrolling clouds ───────────────────────── */
#define SKY_LAYERS 2
static const float s_sky_speeds[SKY_LAYERS] = { 0.015f, 0.008f };
static const float s_sky_alphas[SKY_LAYERS] = { 1.00f,  0.55f  };
static const float s_sky_colors[SKY_LAYERS][3] = {
    { 0.22f, 0.30f, 0.48f },  /* deep blue sky */
    { 0.55f, 0.55f, 0.58f },  /* cloud white-grey */
};

void R_DrawSkySurface(const dface_t *face, const texinfo_t *ti) {
    if (!g_gl || !face) return;

    for (int layer = 0; layer < SKY_LAYERS; layer++) {
        float scroll = s_surf_time * s_sky_speeds[layer];
        float r = s_sky_colors[layer][0];
        float g = s_sky_colors[layer][1];
        float b = s_sky_colors[layer][2];
        glColor3f(r * s_sky_alphas[layer],
                  g * s_sky_alphas[layer],
                  b * s_sky_alphas[layer]);

        glBegin(GL_POLYGON);
        for (int e = 0; e < face->numedges; e++) {
            int se = face->firstedge + e;
            if (se >= g_world.numsurfedges) break;
            int ev = g_world.surfedges[se];
            int vi = (ev >= 0) ? g_world.edges[ev].v[0]
                               : g_world.edges[-ev].v[1];
            if (vi >= g_world.numvertexes) continue;
            const float *p = g_world.vertexes[vi].point;

            float s = 0.0f, t = 0.0f;
            if (ti) {
                s = p[0]*ti->vecs[0][0] + p[1]*ti->vecs[0][1]
                  + p[2]*ti->vecs[0][2] + ti->vecs[0][3];
                t = p[0]*ti->vecs[1][0] + p[1]*ti->vecs[1][1]
                  + p[2]*ti->vecs[1][2] + ti->vecs[1][3];
                s = s / 128.0f + scroll;
                t = t / 128.0f + scroll * 0.5f;
            }
            glTexCoord2f(s, t);
            glVertex3f(p[0], p[1], p[2]);
        }
        glEnd();
    }
}
