/*
 * src/quake/include/r_alias.h — MDL Alias Model Renderer Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_R_ALIAS_H
#define QUAKE_R_ALIAS_H

#include "quakedef.h"

/* ── MDL on-disk header (idpoly format) ──────────────────────────────── */
#define IDPOLYHEADER  0x4F504449  /* "IDPO" */
#define ALIAS_VERSION 6

typedef struct {
    float v[3];   /* Dequantized position */
    float n[3];   /* Normal from lookup table */
} mdl_vert_t;

/* On-disk MDL vertex: exactly 4 bytes — do NOT add onseam prefix */
typedef struct {
    byte v[3];         /* Quantized XYZ */
    byte normalidx;    /* Index into r_avertexnormals[162] */
} dtrivert_t;

/* On-disk texture-coordinate vertex: 3 ints = 12 bytes */
typedef struct {
    int onseam;   /* Non-zero if vertex is on seam between front/back skins */
    int s;        /* U coordinate (pixel, not normalised) */
    int t;        /* V coordinate (pixel, not normalised) */
} dstvert_t;

typedef struct {
    int  facesfront;
    int  vertices[3];  /* Indices into frame vertex array */
} dtriangle_t;

typedef struct {
    byte min_v[3];
    byte max_v[3];
    byte name[16];
    /* followed by numverts dtrivert_t */
} daliasframe_t;

typedef struct {
    int  ident;           /* IDPOLYHEADER */
    int  version;         /* ALIAS_VERSION */
    float scale[3];
    float scale_origin[3];
    float boundingradius;
    float eyeposition[3];
    int  numskins;
    int  skinwidth, skinheight;
    int  numverts;
    int  numtris;
    int  numframes;
    int  synctype;
    int  flags;
    float size;
} mdl_header_t;

/* ── In-memory model ─────────────────────────────────────────────────── */
#define MAX_MDL_VERTS    2048
#define MAX_MDL_TRIS     4096
#define MAX_MDL_FRAMES   256
#define MAX_MDL_SKINS    4
#define MAX_MDL_MODELS   64

typedef struct {
    char     name[64];
    int      is_loaded;

    int      numverts;
    int      numtris;
    int      numframes;

    float    scale[3];
    float    origin[3];

    /* Frame vertex data (all frames, numverts × numframes) */
    mdl_vert_t *frames[MAX_MDL_FRAMES];

    /* Triangle index array */
    int      tris[MAX_MDL_TRIS][3];
    int      tri_facesfront[MAX_MDL_TRIS];

    /* Precomputed normalised UV for each triangle corner [tri][vert][u/v] */
    float    tri_st[MAX_MDL_TRIS][3][2];

    /* Skin GL texture IDs */
    unsigned int skin_tex[MAX_MDL_SKINS];
    int      skinwidth, skinheight;

    /* Hunk mark for cleanup */
    int      hunk_mark;
} alias_model_t;

extern alias_model_t g_alias_models[MAX_MDL_MODELS];
extern int           g_num_alias_models;

/* ── Quake normal lookup table (162 precalculated normals) ──────────── */
extern const float r_avertexnormals[162][3];

/* ── API ─────────────────────────────────────────────────────────────── */
int   R_LoadAliasModel(const char *path);
void  R_DrawAliasModel(int model_idx, int frame, const float *origin,
                       const float *angles, float light);

#endif /* QUAKE_R_ALIAS_H */
