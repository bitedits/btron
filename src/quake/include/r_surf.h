/*
 * src/quake/include/r_surf.h — Animated Surface Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_R_SURF_H
#define QUAKE_R_SURF_H

#include "quakedef.h"
#include "bspfile.h"

/* Advance the time accumulator by dt seconds (call once per frame) */
void R_UpdateSurfTime(float dt);

/* Draw a turbulent (water/slime/lava) polygon with UV warping */
void R_DrawTurbSurface(const dface_t *face, const texinfo_t *ti,
                       int tex_w, int tex_h, float light);

/* Draw a scrolling sky surface (dual-layer additive cloud planes) */
void R_DrawSkySurface(const dface_t *face, const texinfo_t *ti);

#endif /* QUAKE_R_SURF_H */
