/*
 * src/quake/include/r_light.h — Dynamic Lights & Lightmap Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_R_LIGHT_H
#define QUAKE_R_LIGHT_H

#include "quakedef.h"
#include "bspfile.h"

#define MAX_DLIGHTS  32

typedef struct {
    float origin[3];
    float radius;
    float intensity;
    float die;       /* Time at which this light expires */
} dlight_t;

extern dlight_t g_dlights[MAX_DLIGHTS];
extern int      g_num_dlights;

/* Call once per frame before rendering */
void  R_AnimateLights(float dt);
void  R_DecayLights(void);

/* Query lightmap brightness for a BSP face at world position */
float R_LightForFace(const dface_t *face, const float *pos);

/* Register a transient dynamic light */
void  R_AddDynamicLight(const float *origin, float radius, float intensity);

#endif /* QUAKE_R_LIGHT_H */
