/*
 * src/quake/include/bspfile.h — Quake BSP File Structure Definitions
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_BSPFILE_H
#define QUAKE_BSPFILE_H

#include "quakedef.h"

#define BSPVERSION 29

typedef struct {
    int fileofs;
    int filelen;
} lump_t;

#define LUMP_ENTITIES   0
#define LUMP_PLANES     1
#define LUMP_TEXTURES   2
#define LUMP_VERTEXES   3
#define LUMP_VISIBILITY 4
#define LUMP_NODES      5
#define LUMP_TEXINFO    6
#define LUMP_FACES      7
#define LUMP_LIGHTING   8
#define LUMP_CLIPNODES  9
#define LUMP_LEAFS     10
#define LUMP_MARKSURFACES 11
#define LUMP_EDGES     12
#define LUMP_SURFEDGES 13
#define LUMP_MODELS    14
#define HEADER_LUMPS   15

typedef struct {
    int    version;
    lump_t lumps[HEADER_LUMPS];
} dheader_t;

typedef struct {
    float point[3];
} dvertex_t;

typedef struct {
    float normal[3];
    float dist;
    int   type;
} dplane_t;

typedef struct {
    int   planenum;
    short children[2];
    short mins[3];
    short maxs[3];
    unsigned short firstface;
    unsigned short numfaces;
} dnode_t;

typedef struct {
    int   contents;
    int   visofs;
    short mins[3];
    short maxs[3];
    unsigned short firstmarksurface;
    unsigned short nummarksurfaces;
    byte  ambient_level[4];
} dleaf_t;

typedef struct {
    unsigned short v[2];
} dedge_t;

typedef struct {
    short planenum;
    short side;
    int   firstedge;
    short numedges;
    short texinfo;
    byte  styles[4];
    int   lightofs;
} dface_t;

typedef struct {
    float vecs[2][4];
    int   miptex;
    int   flags;
} texinfo_t;

typedef struct {
    int   planenum;
    short children[2];
} dclipnode_t;

typedef struct {
    float mins[3], maxs[3];
    float origin[3];
    int   headnode[4];
    int   visleafs;
    int   firstface;
    int   numfaces;
} dmodel_t;

typedef struct {
    char     name[16];
    unsigned width;
    unsigned height;
    unsigned offsets[4];
} dmiptex_t;

#define CONTENTS_EMPTY        -1
#define CONTENTS_SOLID        -2
#define CONTENTS_WATER        -3
#define CONTENTS_SLIME        -4
#define CONTENTS_LAVA         -5
#define CONTENTS_SKY          -6
#define CONTENTS_ORIGIN       -7
#define CONTENTS_CLIP         -8
#define CONTENTS_CURRENT_0    -9
#define CONTENTS_CURRENT_90   -10
#define CONTENTS_CURRENT_180  -11
#define CONTENTS_CURRENT_270  -12
#define CONTENTS_CURRENT_UP   -13
#define CONTENTS_CURRENT_DOWN -14

#define MAX_MAP_HULLS         4

#endif /* QUAKE_BSPFILE_H */
