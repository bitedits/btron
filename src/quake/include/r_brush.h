/*
 * src/quake/include/r_brush.h — World Brush & Face Geometry Rasterizer
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_R_BRUSH_H
#define QUAKE_R_BRUSH_H

#include "quakedef.h"
#include "world.h"

void R_InitBrushRenderer(void);
void R_DrawWorld(void);

#endif /* QUAKE_R_BRUSH_H */
