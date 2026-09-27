/*
 * src/quake/include/render.h — 3D Renderer Interface for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_RENDER_H
#define QUAKE_RENDER_H

#include "quakedef.h"

typedef struct {
    vec3_t vieworg;
    vec3_t viewangles;
    float  fov_x, fov_y;
} refdef_t;

extern refdef_t r_refdef;

void R_Init(int width, int height);
void R_Resize(int width, int height);  /* Viewport-only update, no engine reinit */
void R_RenderView(void);
void R_BeginFrame(void);
void R_EndFrame(void);

#endif /* QUAKE_RENDER_H */
