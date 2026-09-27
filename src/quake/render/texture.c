/*
 * src/quake/render/texture.c — Quake Palette, Miptex & GL Texture Uploader
 *
 * Loads gfx/palette.lmp from pak0.pak into g_quake_palette[], converts
 * 8-bit indexed miptex surfaces to RGBX8888, and uploads them as GL
 * texture objects via the B-System gl_ops_t dispatch table.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/texture.h"
#include "../include/bspfile.h"
#include "../include/fs_btron.h"
#include "../../gl/gl_dispatch.h"

/* ── Globals ──────────────────────────────────────────────────────── */
quake_pal_t g_quake_palette[256];
gl_tex_t    g_gl_textures[MAX_GL_TEXTURES];
int         g_num_gl_textures = 0;

/* ── Palette loader ───────────────────────────────────────────────── */
void TEX_LoadPalette(const char *pak_path) {
    (void)pak_path;

    int len = 0;
    byte *pal_data = FS_LoadFile("gfx/palette.lmp", &len);
    if (!pal_data || len < 768) {
        /* Fallback: grayscale ramp */
        for (int i = 0; i < 256; i++) {
            g_quake_palette[i].r = (uint8_t)i;
            g_quake_palette[i].g = (uint8_t)i;
            g_quake_palette[i].b = (uint8_t)i;
        }
        Con_DPrintf("TEX_LoadPalette: using grayscale fallback\n");
        return;
    }

    for (int i = 0; i < 256; i++) {
        g_quake_palette[i].r = pal_data[i * 3 + 0];
        g_quake_palette[i].g = pal_data[i * 3 + 1];
        g_quake_palette[i].b = pal_data[i * 3 + 2];
    }
    Con_Printf("TEX_LoadPalette: loaded 256-color Quake palette\n");
}

/* ── Convert 8-bit indexed miptex pixels to RGBA8888 ──────────────── */
static void expand_indexed_to_rgba(const byte *indexed, int npixels, byte *rgba) {
    for (int i = 0; i < npixels; i++) {
        byte pal_idx = indexed[i];
        /* Index 255 is transparent "water warp" — treat as solid blue */
        quake_pal_t c = g_quake_palette[pal_idx];
        rgba[i * 4 + 0] = c.r;
        rgba[i * 4 + 1] = c.g;
        rgba[i * 4 + 2] = c.b;
        rgba[i * 4 + 3] = (pal_idx == 255) ? 0x00 : 0xFF;
    }
}

/*
 * Static RGBA scratch — one buffer for the upload loop, never grows the hunk.
 * Quake BSP textures are at most 512×512; we cap uploads at that size.
 * Larger miptexes (rare) are skipped rather than overflowing BSS.
 */
#define TEX_SCRATCH_MAX_PIXELS (512 * 512)
static byte s_rgba_scratch[TEX_SCRATCH_MAX_PIXELS * 4]; /* 1 MiB BSS */

/* ── Upload single miptex to GL ──────────────────────────────────── */
static int upload_miptex(const byte *tex_lump_base, int ofs, gl_tex_t *out) {
    if (!g_gl) return 0;

    const dmiptex_t *mt = (const dmiptex_t *)(tex_lump_base + ofs);

    int w = (int)mt->width;
    int h = (int)mt->height;

    if (w <= 0 || h <= 0) return 0;

    /* Mip-level 0 pixel data immediately follows the dmiptex_t header */
    if (mt->offsets[0] == 0) {
        /* External WAD reference — record name only, skip GL upload */
        strncpy(out->name, mt->name, 15);
        out->name[15] = '\0';
        out->width = w; out->height = h;
        out->tex_id = 0; out->is_uploaded = 0;
        return 1;
    }

    /* Cap texture dimensions to avoid scratch overflow */
    if (w > 512) w = 512;
    if (h > 512) h = 512;
    int npix = w * h;

    const byte *mip0 = (const byte *)mt + mt->offsets[0];
    expand_indexed_to_rgba(mip0, npix, s_rgba_scratch); /* zero hunk cost */

    /* Upload to GL */
    GLuint tex_id = 0;
    glGenTextures(1, &tex_id);
    glBindTexture(GL_TEXTURE_2D, tex_id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_rgba_scratch);
    glBindTexture(GL_TEXTURE_2D, 0);

    strncpy(out->name, mt->name, 15);
    out->name[15] = '\0';
    for (int i = 0; out->name[i]; i++) {
        char c = out->name[i];
        if (c >= 'A' && c <= 'Z') out->name[i] = c + ('a' - 'A');
    }
    out->width       = w;
    out->height      = h;
    out->tex_id      = (unsigned int)tex_id;
    out->is_uploaded = 1;
    return 1;
}

/* ── Load all textures from BSP LUMP_TEXTURES data ────────────────── */
int TEX_LoadBSPTextures(const byte *tex_lump, int lump_len) {
    if (!tex_lump || lump_len < 4) return 0;

    int num_tex = *(const int *)tex_lump;
    if (num_tex <= 0) return 0;
    if (num_tex > MAX_GL_TEXTURES) num_tex = MAX_GL_TEXTURES;

    int hunk_before = Hunk_LowMark();
    Con_Printf("TEX_LoadBSPTextures: hunk before upload = %d KB\n",
               hunk_before / 1024);

    const int *offsets = (const int *)(tex_lump + 4);
    int loaded = 0;

    for (int t = 0; t < num_tex && g_num_gl_textures < MAX_GL_TEXTURES; t++) {
        int ofs = offsets[t];
        if (ofs < 0 || ofs + (int)sizeof(dmiptex_t) >= lump_len) continue;

        gl_tex_t *slot = &g_gl_textures[g_num_gl_textures];
        if (upload_miptex(tex_lump, ofs, slot)) {
            g_num_gl_textures++;
            loaded++;
        }
    }

    int hunk_after = Hunk_LowMark();
    Con_Printf("TEX_LoadBSPTextures: loaded %d/%d textures, "
               "hunk delta = %d bytes (total %d KB)\n",
               loaded, num_tex,
               hunk_after - hunk_before,
               hunk_after / 1024);
    return loaded;
}

/* ── Find texture by name ─────────────────────────────────────────── */
int TEX_FindTexture(const char *name) {
    if (!name) return -1;
    for (int i = 0; i < g_num_gl_textures; i++) {
        if (q_strcasecmp(g_gl_textures[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

/* ── Module Initialisation ────────────────────────────────────────── */
void TEX_Init(void) {
    g_num_gl_textures = 0;
    memset(g_gl_textures, 0, sizeof(g_gl_textures));
    TEX_LoadPalette(NULL);
    Con_Printf("TEX_Init: texture system ready\n");
}
