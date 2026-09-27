/*
 * src/quake/include/r_part.h — Particle System Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_R_PART_H
#define QUAKE_R_PART_H

#include "quakedef.h"

#define MAX_PARTICLES  2048

typedef enum {
    PT_STATIC   = 0,
    PT_GRAV     = 1,   /* gravity affected */
    PT_SLOWGRAV = 2,   /* gentle float down */
    PT_FIRE     = 3,   /* fire / embers: shrink + rise */
    PT_EXPLODE  = 4,   /* explosion: fast spread */
    PT_EXPLODE2 = 5,   /* secondary explosion debris */
    PT_BLOB     = 6,   /* blood blob */
    PT_BLOB2    = 7,   /* blood trail */
    PT_TELEPORT = 8,   /* teleport sparkle */
    PT_SMOKE    = 9,   /* slow rising smoke */
} ptype_t;

typedef struct particle_s {
    float    org[3];
    float    vel[3];
    float    color[3];   /* RGB [0,1] */
    float    alpha;
    float    size;
    float    die;        /* world time at which particle expires */
    float    ramp;       /* colour ramp index for fire/smoke */
    ptype_t  type;
} particle_t;

extern particle_t g_particles[MAX_PARTICLES];
extern int        g_num_particles;

/* Emitters */
void P_RunParticleEffect(const float *org, const float *dir,
                         int color, int count);
void P_RocketTrail(const float *start, const float *end, int type);
void P_LavaSplash(const float *org);
void P_TeleportSplash(const float *org);
void P_BloodSplash(const float *org, int count);
void P_ExplosionParticles(const float *org);

/* Per-frame: integrate + draw */
void P_UpdateParticles(float dt);
void R_DrawParticles(const float *vieworg, const float *viewright,
                     const float *viewup);

#endif /* QUAKE_R_PART_H */
