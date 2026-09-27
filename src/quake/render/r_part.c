/*
 * src/quake/render/r_part.c — Quake Particle System
 *
 * Implements Quake's iconic particle effects as a static 2048-slot BSS pool:
 *   - Blood, explosion, fire, ember, rocket trail, lava splash, teleport ring
 *   - Per-frame Euler integration with Quake gravity (800 u/s²)
 *   - Camera-facing billboard quads emitted via GL_QUADS
 *
 * All particle allocations come from the circular ring buffer — never hunk.
 * The pool is reset at map load; particle state is never saved.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/r_part.h"
#include "../include/quakedef.h"
#include "../include/mathlib.h"
#include "../include/server.h"
#include "../../gl/gl_dispatch.h"

/* ── Globals ─────────────────────────────────────────────────────────── */
particle_t g_particles[MAX_PARTICLES];
int        g_num_particles = 0;

/* Ring-buffer allocator — never fails, overwrites oldest */
static int s_part_head = 0;

static particle_t *alloc_particle(void) {
    particle_t *p = &g_particles[s_part_head % MAX_PARTICLES];
    s_part_head++;
    if (g_num_particles < MAX_PARTICLES) g_num_particles++;
    return p;
}

/* ── Colour ramp tables (Quake palette indices → RGB) ────────────────── */
/* Fire ramp: bright yellow → orange → dark red */
static const float s_ramp_fire[8][3] = {
    {1.00f, 0.93f, 0.20f}, {1.00f, 0.75f, 0.10f}, {1.00f, 0.55f, 0.05f},
    {0.90f, 0.35f, 0.02f}, {0.75f, 0.20f, 0.01f}, {0.55f, 0.10f, 0.00f},
    {0.35f, 0.04f, 0.00f}, {0.15f, 0.01f, 0.00f},
};
/* Explosion ramp: white → yellow → orange → red → brown */
static const float s_ramp_explode[8][3] = {
    {1.00f, 1.00f, 0.80f}, {1.00f, 0.92f, 0.50f}, {1.00f, 0.78f, 0.20f},
    {0.95f, 0.60f, 0.05f}, {0.80f, 0.40f, 0.02f}, {0.60f, 0.22f, 0.01f},
    {0.35f, 0.08f, 0.00f}, {0.10f, 0.01f, 0.00f},
};
/* Blood: dark red → brown */
static const float s_ramp_blood[4][3] = {
    {0.60f, 0.04f, 0.04f}, {0.45f, 0.03f, 0.03f},
    {0.30f, 0.02f, 0.02f}, {0.15f, 0.01f, 0.01f},
};
/* Teleport: electric blue-white */
static const float s_ramp_tele[4][3] = {
    {0.80f, 0.90f, 1.00f}, {0.50f, 0.70f, 1.00f},
    {0.20f, 0.50f, 1.00f}, {0.05f, 0.20f, 0.80f},
};

static float s_part_time = 0.0f;

/* Simple LCG for deterministic pseudo-random positions */
static unsigned s_prng = 0xC0FFEE17u;
static float prand(void) {
    s_prng = s_prng * 1664525u + 1013904223u;
    return (float)(s_prng >> 8) * (1.0f / (float)0xFFFFFF) * 2.0f - 1.0f;
}
static float prand01(void) { return (prand() + 1.0f) * 0.5f; }

/* ── Emitters ─────────────────────────────────────────────────────────── */

void P_ExplosionParticles(const float *org) {
    for (int i = 0; i < 512; i++) {
        particle_t *p = alloc_particle();
        int ri = i & 7;
        p->type  = (i & 1) ? PT_EXPLODE : PT_EXPLODE2;
        p->org[0] = org[0] + prand() * 16.0f;
        p->org[1] = org[1] + prand() * 16.0f;
        p->org[2] = org[2] + prand() * 16.0f;
        p->vel[0] = prand() * 256.0f;
        p->vel[1] = prand() * 256.0f;
        p->vel[2] = prand() * 256.0f + 80.0f;
        p->ramp   = (float)ri;
        p->die    = s_part_time + 0.5f * prand01();
        p->size   = 2.5f;
        p->alpha  = 1.0f;
        VectorCopy(s_ramp_explode[ri], p->color);
    }
}

void P_BloodSplash(const float *org, int count) {
    for (int i = 0; i < count; i++) {
        particle_t *p = alloc_particle();
        p->type  = PT_BLOB;
        p->org[0] = org[0] + prand() * 8.0f;
        p->org[1] = org[1] + prand() * 8.0f;
        p->org[2] = org[2] + prand() * 8.0f;
        p->vel[0] = prand() * 80.0f;
        p->vel[1] = prand() * 80.0f;
        p->vel[2] = prand() * 80.0f + 40.0f;
        p->ramp   = 0;
        p->die    = s_part_time + 0.6f + prand01() * 0.4f;
        p->size   = 2.0f;
        p->alpha  = 1.0f;
        VectorCopy(s_ramp_blood[0], p->color);
    }
}

void P_RocketTrail(const float *start, const float *end, int type) {
    float len = sqrtf(
        (end[0]-start[0])*(end[0]-start[0]) +
        (end[1]-start[1])*(end[1]-start[1]) +
        (end[2]-start[2])*(end[2]-start[2]));
    if (len < 1.0f) return;
    float step = 3.0f;
    int n = (int)(len / step);
    float dx = (end[0]-start[0]) / (float)n;
    float dy = (end[1]-start[1]) / (float)n;
    float dz = (end[2]-start[2]) / (float)n;

    for (int i = 0; i < n; i++) {
        particle_t *p = alloc_particle();
        p->org[0] = start[0] + dx*i + prand()*2.0f;
        p->org[1] = start[1] + dy*i + prand()*2.0f;
        p->org[2] = start[2] + dz*i + prand()*2.0f;
        p->vel[0] = prand() * 6.0f;
        p->vel[1] = prand() * 6.0f;
        p->vel[2] = prand() * 6.0f;
        p->size   = 1.5f;
        p->alpha  = 0.85f;
        p->die    = s_part_time + 0.3f;
        if (type == 0) {
            /* Fire trail */
            p->type = PT_FIRE;
            int ri = (int)(prand01() * 7.0f) & 7;
            p->ramp = (float)ri;
            VectorCopy(s_ramp_fire[ri], p->color);
        } else {
            /* Grenade smoke */
            p->type = PT_SMOKE;
            p->color[0] = 0.38f; p->color[1] = 0.35f; p->color[2] = 0.32f;
        }
    }
}

void P_TeleportSplash(const float *org) {
    for (int i = -16; i < 16; i++) {
        for (int j = -16; j < 16; j++) {
            particle_t *p = alloc_particle();
            p->type  = PT_TELEPORT;
            p->org[0] = org[0] + (float)i;
            p->org[1] = org[1] + (float)j;
            p->org[2] = org[2] + (prand01() * 48.0f);
            p->vel[0] = (float)i * 8.0f;
            p->vel[1] = (float)j * 8.0f;
            p->vel[2] = 256.0f;
            int ri = (int)(prand01() * 3.0f) & 3;
            p->ramp  = (float)ri;
            p->die   = s_part_time + 0.3f;
            p->size  = 1.5f;
            p->alpha = 1.0f;
            VectorCopy(s_ramp_tele[ri], p->color);
        }
    }
}

void P_LavaSplash(const float *org) {
    for (int i = -16; i < 16; i++) {
        for (int j = -16; j < 16; j++) {
            particle_t *p = alloc_particle();
            p->type  = PT_SLOWGRAV;
            p->org[0] = org[0] + (float)i * 8.0f + prand()*4.0f;
            p->org[1] = org[1] + (float)j * 8.0f + prand()*4.0f;
            p->org[2] = org[2] + prand() * 64.0f;
            p->vel[0] = (float)i * prand01() * 16.0f;
            p->vel[1] = (float)j * prand01() * 16.0f;
            p->vel[2] = 80.0f + prand()*40.0f;
            p->ramp  = (float)((int)(prand01()*7.0f) & 7);
            p->die   = s_part_time + 2.0f + prand01();
            p->size  = 2.0f;
            p->alpha = 1.0f;
            int ri = (int)p->ramp & 7;
            p->color[0] = s_ramp_fire[ri][0];
            p->color[1] = s_ramp_fire[ri][1];
            p->color[2] = s_ramp_fire[ri][2];
        }
    }
}

void P_RunParticleEffect(const float *org, const float *dir,
                         int color, int count) {
    (void)dir; (void)color;
    for (int i = 0; i < count; i++) {
        particle_t *p = alloc_particle();
        p->type  = PT_SLOWGRAV;
        p->org[0] = org[0] + prand()*8.0f;
        p->org[1] = org[1] + prand()*8.0f;
        p->org[2] = org[2] + prand()*8.0f;
        p->vel[0] = prand()*30.0f;
        p->vel[1] = prand()*30.0f;
        p->vel[2] = prand()*30.0f;
        p->size  = 1.5f;
        p->alpha = 1.0f;
        p->die   = s_part_time + 0.5f;
        p->color[0] = 0.7f; p->color[1] = 0.7f; p->color[2] = 0.7f;
    }
}

/* ── Per-frame physics integration ──────────────────────────────────── */
#define PART_GRAVITY  800.0f

void P_UpdateParticles(float dt) {
    s_part_time += dt;
    int out = 0;
    for (int i = 0; i < g_num_particles; i++) {
        particle_t *p = &g_particles[i];
        if (p->die <= s_part_time) continue;  /* expired */

        /* Velocity integration */
        p->org[0] += p->vel[0] * dt;
        p->org[1] += p->vel[1] * dt;
        p->org[2] += p->vel[2] * dt;

        switch (p->type) {
        case PT_GRAV:
            p->vel[2] -= PART_GRAVITY * dt;
            break;
        case PT_SLOWGRAV:
            p->vel[2] -= PART_GRAVITY * 0.05f * dt;
            break;
        case PT_FIRE: {
            p->ramp += dt * 5.0f;
            int ri = (int)p->ramp & 7;
            VectorCopy(s_ramp_fire[ri], p->color);
            p->vel[2] += 20.0f * dt;  /* embers rise */
            p->size   *= (1.0f - dt * 1.5f);
            if (p->size < 0.3f) { p->die = 0; continue; }
            break; }
        case PT_EXPLODE:
        case PT_EXPLODE2: {
            p->ramp += dt * 15.0f;
            int ri = (int)p->ramp & 7;
            VectorCopy(s_ramp_explode[ri], p->color);
            p->vel[2] -= PART_GRAVITY * 0.3f * dt;
            p->alpha  -= dt * 2.5f;
            if (p->alpha < 0.0f) { p->die = 0; continue; }
            break; }
        case PT_BLOB:
        case PT_BLOB2: {
            p->vel[2] -= PART_GRAVITY * dt;
            int ri = (int)(p->ramp) & 3;
            VectorCopy(s_ramp_blood[ri], p->color);
            p->ramp += dt * 4.0f;
            break; }
        case PT_TELEPORT: {
            p->vel[2] -= PART_GRAVITY * 0.15f * dt;
            int ri = (int)(p->ramp) & 3;
            VectorCopy(s_ramp_tele[ri], p->color);
            p->alpha -= dt * 3.0f;
            if (p->alpha < 0.0f) { p->die = 0; continue; }
            break; }
        case PT_SMOKE:
            p->vel[2] += 15.0f * dt;
            p->alpha  -= dt * 0.8f;
            p->size   *= (1.0f + dt * 0.5f);
            if (p->alpha < 0.05f) { p->die = 0; continue; }
            break;
        default: break;
        }

        /* Compact live particles */
        if (i != out) g_particles[out] = *p;
        out++;
    }
    g_num_particles = out;
}

/* ── Billboard particle renderer ─────────────────────────────────────── */
void R_DrawParticles(const float *vieworg, const float *viewright,
                     const float *viewup) {
    if (!g_gl || g_num_particles == 0) return;
    (void)vieworg;

    /* Draw as camera-aligned quads */
    glDisable(GL_DEPTH_TEST);   /* Particles always on top of geometry */
    glDisable(GL_TEXTURE_2D);   /* Pure colour billboard */
    glBegin(GL_QUADS);

    for (int i = 0; i < g_num_particles; i++) {
        const particle_t *p = &g_particles[i];
        float s = p->size;
        float a = p->alpha > 1.0f ? 1.0f : (p->alpha < 0.0f ? 0.0f : p->alpha);
        glColor3f(p->color[0] * a, p->color[1] * a, p->color[2] * a);

        /* Four corners: centre ± (right ± up) * size */
        float c[3] = { p->org[0], p->org[1], p->org[2] };

        /* top-left  */  glVertex3f(c[0]+(viewup[0]-viewright[0])*s, c[1]+(viewup[1]-viewright[1])*s, c[2]+(viewup[2]-viewright[2])*s);
        /* top-right */  glVertex3f(c[0]+(viewup[0]+viewright[0])*s, c[1]+(viewup[1]+viewright[1])*s, c[2]+(viewup[2]+viewright[2])*s);
        /* bot-right */  glVertex3f(c[0]-(viewup[0]-viewright[0])*s, c[1]-(viewup[1]-viewright[1])*s, c[2]-(viewup[2]-viewright[2])*s);
        /* bot-left  */  glVertex3f(c[0]-(viewup[0]+viewright[0])*s, c[1]-(viewup[1]+viewright[1])*s, c[2]-(viewup[2]+viewright[2])*s);
    }

    glEnd();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
}
