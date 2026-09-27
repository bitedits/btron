/*
 * src/quake/include/texture.h — Quake Texture & Lightmap Management
 *
 * Manages the 256-color Quake palette, miptex BSP surface textures,
 * and gfx.wad HUD lump textures as OpenGL ES 1.1 texture objects.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_TEXTURE_H
#define QUAKE_TEXTURE_H

#include "quakedef.h"

/* ── Maximum OpenGL textures allocated at once ──────────────────── */
#define MAX_GL_TEXTURES  512

/* ── Standard Quake palette: 256 RGB triplets ────────────────────── */
typedef struct {
    uint8_t r, g, b;
} quake_pal_t;

extern quake_pal_t g_quake_palette[256];

/* ── In-memory texture descriptor ────────────────────────────────── */
typedef struct {
    unsigned int tex_id;          /* OpenGL texture object ID       */
    char         name[16];        /* Miptex name (lowercase)         */
    int          width;
    int          height;
    int          is_uploaded;
} gl_tex_t;

extern gl_tex_t  g_gl_textures[MAX_GL_TEXTURES];
extern int       g_num_gl_textures;

/* ── Lifecycle ────────────────────────────────────────────────────── */
void TEX_Init(void);
void TEX_LoadPalette(const char *pak_path);

/* ── Miptex loading from BSP LUMP_TEXTURES ───────────────────────── */
int  TEX_LoadBSPTextures(const byte *tex_lump, int lump_len);
int  TEX_FindTexture(const char *name);  /* returns index or -1 */

#endif /* QUAKE_TEXTURE_H */
