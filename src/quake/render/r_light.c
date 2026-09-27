/*
 * src/quake/render/r_light.c — BSP Lightmap Sampling & Dynamic Lights
 *
 * Quake lightmaps are monochrome 8-bit texels stored in LUMP_LIGHTING.
 * Each face references a block at dface_t.lightofs sized by the face's
 * texinfo-projected extents.  We sample the four-corner average and
 * return a [0,1] float that modulates the surface colour in r_brush.c.
 *
 * Dynamic lights (muzzle flash, explosions) are handled by computing a
 * 1/r² falloff from each registered dl_t and accumulating into the face
 * ambient term.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/r_light.h"
#include "../include/world.h"
#include "../include/mathlib.h"

/* ── Dynamic light registry ─────────────────────────────────────────── */
dlight_t    g_dlights[MAX_DLIGHTS];
int         g_num_dlights = 0;

static float s_light_styles[MAX_LIGHTSTYLES];  /* [0,1] per style */
static float s_anim_time = 0.0f;

/* ── Lightstyle string animation table (Quake standard) ────────────── */
/* Style 0 = "m" (full bright), style 1 = "mmnmmommommnonmmonqnmmo" etc. */
static const char *s_style_strings[MAX_LIGHTSTYLES] = {
    "m",                         /* 0: normal */
    "mmnmmommommnonmmonqnmmo",   /* 1: flicker A */
    "abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcba", /* 2: slow pulse */
    "mmmmmaaaaammmmmaaaaaabcdefgabcdefg",  /* 3: candle A */
    "mamamamamama",              /* 4: fast strobe */
    "jklmnopqrstuvwxyzyxwvutsrqponmlkj",  /* 5: gentle pulse */
    "nmonqnmomnmomomno",         /* 6: flicker B */
    "mmmaaaabcdefgmmmmaaaammmaamm", /* 7: candle B */
    "mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa", /* 8: candle C */
    "aaaaaaaazzzzzzzz",          /* 9: slow strobe */
    "mmamammmmammamamaaamammma",  /* 10: fluorescent */
    "abcdefghijklmnopqrrqponmlkjihgfedcba", /* 11: slow wave */
};

void R_AnimateLights(float dt) {
    s_anim_time += dt;

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        const char *s = (i < 12 && s_style_strings[i]) ? s_style_strings[i] : "m";
        int len = 0;
        while (s[len]) len++;
        if (len == 0) { s_light_styles[i] = 1.0f; continue; }

        int frame = (int)(s_anim_time * 10.0f) % len;
        /* 'a'=0, 'm'=1.0, 'z'≈1.92 — normalise to [0,1] */
        float val = (float)(s[frame] - 'a') / 12.0f;
        if (val > 1.0f) val = 1.0f;
        s_light_styles[i] = val;
    }
}

/* ── Face lightmap extent calculation ───────────────────────────────── */
static void face_extents(const dface_t *face, int *out_w, int *out_h) {
    if (!face || face->texinfo < 0 || face->texinfo >= g_world.numtexinfo) {
        *out_w = *out_h = 16; return;
    }
    const texinfo_t *ti = &g_world.texinfo[face->texinfo];

    float smin =  1e9f, smax = -1e9f;
    float tmin =  1e9f, tmax = -1e9f;

    for (int e = 0; e < face->numedges; e++) {
        int se = face->firstedge + e;
        if (se >= g_world.numsurfedges) break;
        int ev = g_world.surfedges[se];
        int vi = (ev >= 0) ? g_world.edges[ev].v[0]
                           : g_world.edges[-ev].v[1];
        if (vi >= g_world.numvertexes) continue;
        const float *p = g_world.vertexes[vi].point;

        float s = p[0]*ti->vecs[0][0] + p[1]*ti->vecs[0][1]
                + p[2]*ti->vecs[0][2] + ti->vecs[0][3];
        float t = p[0]*ti->vecs[1][0] + p[1]*ti->vecs[1][1]
                + p[2]*ti->vecs[1][2] + ti->vecs[1][3];
        if (s < smin) smin = s; if (s > smax) smax = s;
        if (t < tmin) tmin = t; if (t > tmax) tmax = t;
    }

    /* Lightmap block is in 16-texel units */
    *out_w = ((int)(smax / 16.0f) - (int)(smin / 16.0f) + 1);
    *out_h = ((int)(tmax / 16.0f) - (int)(tmin / 16.0f) + 1);
    if (*out_w < 1) *out_w = 1;
    if (*out_h < 1) *out_h = 1;
}

/* ── Main lightmap sample for a face ────────────────────────────────── */
float R_LightForFace(const dface_t *face, const float *pos) {
    float ambient = 0.18f;  /* Minimum ambient so no surface is pitch-black */

    /* Sum static lightmap samples across up to 4 light styles */
    if (face && face->lightofs >= 0 && g_world.lightdata && g_world.lightlen > 0) {
        int lw, lh;
        face_extents(face, &lw, &lh);
        int block_size = lw * lh;

        for (int s = 0; s < 4; s++) {
            int style = face->styles[s];
            if (style == 255) break;

            int lofs = face->lightofs + s * block_size;
            if (lofs + block_size > g_world.lightlen) break;

            const byte *lm = g_world.lightdata + lofs;

            /* Sample centre texel of the lightmap block */
            int cx = lw / 2, cy = lh / 2;
            byte raw = lm[cy * lw + cx];
            float scale = (style < MAX_LIGHTSTYLES) ? s_light_styles[style] : 1.0f;
            ambient += ((float)raw / 255.0f) * scale * 0.6f;
        }
    }

    /* Accumulate dynamic lights */
    if (pos) {
        for (int i = 0; i < g_num_dlights; i++) {
            const dlight_t *dl = &g_dlights[i];
            if (dl->radius <= 0.0f) continue;
            float dx = pos[0] - dl->origin[0];
            float dy = pos[1] - dl->origin[1];
            float dz = pos[2] - dl->origin[2];
            float dist = sqrtf(dx*dx + dy*dy + dz*dz);
            if (dist < dl->radius) {
                float att = 1.0f - dist / dl->radius;
                ambient += att * dl->intensity;
            }
        }
    }

    if (ambient < 0.0f) ambient = 0.0f;
    if (ambient > 1.0f) ambient = 1.0f;
    return ambient;
}

/* ── Register a dynamic light (muzzle flash, explosion) ─────────────── */
void R_AddDynamicLight(const float *origin, float radius, float intensity) {
    if (g_num_dlights >= MAX_DLIGHTS) g_num_dlights = 0; /* ring wrap */
    dlight_t *dl = &g_dlights[g_num_dlights++];
    dl->origin[0] = origin[0];
    dl->origin[1] = origin[1];
    dl->origin[2] = origin[2];
    dl->radius    = radius;
    dl->intensity = intensity;
    dl->die       = s_anim_time + 0.1f;
}

/* ── Expire old dynamic lights each frame ───────────────────────────── */
void R_DecayLights(void) {
    int out = 0;
    for (int i = 0; i < g_num_dlights; i++) {
        if (g_dlights[i].die > s_anim_time) {
            g_dlights[out++] = g_dlights[i];
        }
    }
    g_num_dlights = out;
}
