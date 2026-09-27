/*
 * src/quake/include/draw.h — 2D Drawing Header for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_DRAW_H
#define QUAKE_DRAW_H

#include "quakedef.h"
#include "wad.h"

void Draw_Init(void);
void Draw_Character(int x, int y, int num);
void Draw_String(int x, int y, const char *str);
void Draw_Pic(int x, int y, qpic_t *pic);
void Draw_Fill(int x, int y, int w, int h, int color);
void Draw_FadeScreen(void);

#endif /* QUAKE_DRAW_H */
