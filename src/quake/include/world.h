/*
 * src/quake/include/world.h — World & BSP Map Architecture Interface
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_WORLD_H
#define QUAKE_WORLD_H

#include "quakedef.h"
#include "bspfile.h"

#define MAX_MAP_TEXTURES 256

typedef struct {
    char   name[64];
    vec3_t mins;
    vec3_t maxs;
    vec3_t spawn_origin;
    float  spawn_angle;

    int numplanes;
    dplane_t *planes;

    int numvertexes;
    dvertex_t *vertexes;

    int numnodes;
    dnode_t *nodes;

    int numtexinfo;
    texinfo_t *texinfo;

    int numfaces;
    dface_t *faces;

    int numclipnodes;
    dclipnode_t *clipnodes;

    int numleafs;
    dleaf_t *leafs;

    int nummarksurfaces;
    unsigned short *marksurfaces;

    int numsurfedges;
    int *surfedges;

    int numedges;
    dedge_t *edges;

    int nummodels;
    dmodel_t *models;

    int vislen;
    byte *visdata;

    int lightlen;
    byte *lightdata;

    int entlen;
    char *entities;

    int  numtextures;
    char texture_names[MAX_MAP_TEXTURES][16];

    int is_loaded;
    int hunk_mark;
} bsp_world_t;

extern bsp_world_t g_world;

/* Map lifecycle */
int  World_LoadMap(const char *mapname);
void World_UnloadMap(void);

/* Spatial queries & PVS */
int   World_PointInLeaf(const vec3_t point);
byte *World_LeafPVS(int leafnum, byte *decompressed_buffer, int max_len);

/* Collision hulls */
int   World_HullPointContents(int hullnum, int nodenum, const vec3_t point);

/* Server integration — call after World_LoadMap */
void  SV_SpawnServer(const char *mapname);
void  SV_ServerFrame(float dt);

#endif /* QUAKE_WORLD_H */
