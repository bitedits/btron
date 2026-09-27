/*
 * src/quake/core/world.c — BSP v29 World Geometry Loader & Spatial Engine
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/world.h"
#include "../include/fs_btron.h"

bsp_world_t g_world;

void World_UnloadMap(void) {
    if (g_world.is_loaded) {
        if (g_world.hunk_mark >= 0) {
            Hunk_FreeToLowMark(g_world.hunk_mark);
        }
        memset(&g_world, 0, sizeof(g_world));
    }
}

static void parse_spawn_entity(const char *ents) {
    if (!ents) return;

    /* Look for info_player_start */
    const char *p = ents;
    while (*p) {
        const char *entity_start = q_strstr(p, "info_player_start");
        if (!entity_start) break;

        /* Find opening brace before and closing brace after */
        const char *open_b = entity_start;
        while (open_b > ents && *open_b != '{') open_b--;
        const char *close_b = q_strchr(entity_start, '}');
        if (!close_b) break;

        /* Search within { ... } for "origin" and "angle" */
        const char *orig_tag = q_strstr(open_b, "\"origin\"");
        if (orig_tag && orig_tag < close_b) {
            const char *val = orig_tag + 8;
            while (*val && (*val == ' ' || *val == '\t' || *val == '\"')) val++;
            float ox = 0.0f, oy = 0.0f, oz = 0.0f;
            ox = q_atof(val);
            while (*val && *val != ' ') val++;
            while (*val == ' ') val++;
            oy = q_atof(val);
            while (*val && *val != ' ') val++;
            while (*val == ' ') val++;
            oz = q_atof(val);
            VectorSet(g_world.spawn_origin, ox, oy, oz);
        }

        const char *ang_tag = q_strstr(open_b, "\"angle\"");
        if (ang_tag && ang_tag < close_b) {
            const char *val = ang_tag + 7;
            while (*val && (*val == ' ' || *val == '\t' || *val == '\"')) val++;
            g_world.spawn_angle = q_atof(val);
        }

        return; /* Successfully extracted primary player start */
    }

    /* Fallback default */
    VectorSet(g_world.spawn_origin, 0.0f, 0.0f, 0.0f);
    g_world.spawn_angle = 0.0f;
}

int World_LoadMap(const char *mapname) {
    if (!mapname) return 0;

    int mark = Hunk_LowMark();
    World_UnloadMap();

    /*
     * Fast-path: FS_CacheBSP slurps the entire .bsp into the hunk once
     * (zero-copy from the PAK image) and exposes per-lump base pointers.
     * We then cast those pointers directly — no memcpy, no extra fread.
     */
    if (!FS_CacheBSP(mapname)) {
        Con_Printf("World_LoadMap: Could not load '%s'\n", mapname);
        Hunk_FreeToLowMark(mark);
        return 0;
    }

    const fs_bsp_cache_t *bc = &g_bsp_cache;

    g_world.hunk_mark = mark;
    strncpy(g_world.name, mapname, sizeof(g_world.name) - 1);
    g_world.name[sizeof(g_world.name) - 1] = '\0';

    /* ── Macro: cast lump pointer to typed array, zero-copy ────── */
#define BIND_LUMP(field, count_field, lump_id, T) do { \
    g_world.count_field = bc->lump_lens[lump_id] / (int)sizeof(T); \
    g_world.field = (T *)bc->lumps[lump_id]; \
} while (0)

    BIND_LUMP(planes,        numplanes,        LUMP_PLANES,        dplane_t);
    BIND_LUMP(vertexes,      numvertexes,      LUMP_VERTEXES,      dvertex_t);
    BIND_LUMP(nodes,         numnodes,         LUMP_NODES,         dnode_t);
    BIND_LUMP(texinfo,       numtexinfo,       LUMP_TEXINFO,       texinfo_t);
    BIND_LUMP(faces,         numfaces,         LUMP_FACES,         dface_t);
    BIND_LUMP(clipnodes,     numclipnodes,     LUMP_CLIPNODES,     dclipnode_t);
    BIND_LUMP(leafs,         numleafs,         LUMP_LEAFS,         dleaf_t);
    BIND_LUMP(marksurfaces,  nummarksurfaces,  LUMP_MARKSURFACES,  unsigned short);
    BIND_LUMP(surfedges,     numsurfedges,     LUMP_SURFEDGES,     int);
    BIND_LUMP(edges,         numedges,         LUMP_EDGES,         dedge_t);
    BIND_LUMP(models,        nummodels,        LUMP_MODELS,        dmodel_t);
#undef BIND_LUMP

    if (g_world.nummodels > 0 && g_world.models) {
        VectorCopy(g_world.models[0].mins, g_world.mins);
        VectorCopy(g_world.models[0].maxs, g_world.maxs);
    }

    /* Visibility — direct pointer into PAK image */
    g_world.visdata = (byte *)bc->lumps[LUMP_VISIBILITY];
    g_world.vislen  = bc->lump_lens[LUMP_VISIBILITY];

    /* Lighting — direct pointer */
    g_world.lightdata = (byte *)bc->lumps[LUMP_LIGHTING];
    g_world.lightlen  = bc->lump_lens[LUMP_LIGHTING];

    /* Entities — need NUL terminator, make a hunk copy */
    int ent_len = bc->lump_lens[LUMP_ENTITIES];
    if (ent_len > 0 && bc->lumps[LUMP_ENTITIES]) {
        g_world.entities = (char *)Hunk_Alloc((size_t)ent_len + 1);
        if (g_world.entities) {
            memcpy(g_world.entities, bc->lumps[LUMP_ENTITIES], (size_t)ent_len);
            g_world.entities[ent_len] = '\0';
            parse_spawn_entity(g_world.entities);
        }
    }
    g_world.entlen = ent_len;

    /* Textures — scan names from LUMP_TEXTURES zero-copy */
    const byte *tdata   = bc->lumps[LUMP_TEXTURES];
    int         tlen    = bc->lump_lens[LUMP_TEXTURES];
    if (tdata && tlen > (int)sizeof(int)) {
        int num_tex = *(const int *)tdata;
        const int *offsets = (const int *)(tdata + 4);
        if (num_tex > MAX_MAP_TEXTURES) num_tex = MAX_MAP_TEXTURES;
        g_world.numtextures = num_tex;
        for (int t = 0; t < num_tex; t++) {
            if (offsets[t] >= 0 && offsets[t] + 16 < tlen) {
                const dmiptex_t *mt = (const dmiptex_t *)(tdata + offsets[t]);
                strncpy(g_world.texture_names[t], mt->name, 16);
                g_world.texture_names[t][15] = '\0';
            }
        }
    }

    g_world.is_loaded = 1;

    Con_Printf("BSP World Loaded '%s':\n", mapname);
    Con_Printf("  Geometry: %d faces, %d verts, %d nodes, %d leafs, %d models\n",
               g_world.numfaces, g_world.numvertexes, g_world.numnodes,
               g_world.numleafs, g_world.nummodels);
    Con_Printf("  Spawn   : (%.1f, %.1f, %.1f) angle: %.1f\n",
               g_world.spawn_origin[0], g_world.spawn_origin[1],
               g_world.spawn_origin[2], g_world.spawn_angle);

    return 1;
}

int World_PointInLeaf(const vec3_t point) {
    if (!g_world.is_loaded || g_world.numnodes <= 0) return 0;

    int nodenum = 0;
    while (nodenum >= 0) {
        if (nodenum >= g_world.numnodes) return 0;
        const dnode_t *node = &g_world.nodes[nodenum];
        const dplane_t *plane = &g_world.planes[node->planenum];
        float dist = DotProduct(point, plane->normal) - plane->dist;
        nodenum = (dist >= 0.0f) ? node->children[0] : node->children[1];
    }

    int leafnum = -1 - nodenum;
    if (leafnum < 0 || leafnum >= g_world.numleafs) return 0;
    return leafnum;
}

byte *World_LeafPVS(int leafnum, byte *decompressed_buffer, int max_len) {
    if (!decompressed_buffer || max_len <= 0) return NULL;

    if (!g_world.is_loaded || leafnum <= 0 || leafnum >= g_world.numleafs || !g_world.visdata) {
        memset(decompressed_buffer, 0xFF, max_len);
        return decompressed_buffer;
    }

    int visofs = g_world.leafs[leafnum].visofs;
    if (visofs < 0 || visofs >= g_world.vislen) {
        memset(decompressed_buffer, 0xFF, max_len);
        return decompressed_buffer;
    }

    const byte *in = g_world.visdata + visofs;
    int row_bytes = (g_world.numleafs + 7) / 8;
    if (row_bytes > max_len) row_bytes = max_len;

    int out_idx = 0;
    while (out_idx < row_bytes) {
        if (*in) {
            decompressed_buffer[out_idx++] = *in++;
        } else {
            in++;
            int count = *in++;
            while (count-- > 0 && out_idx < row_bytes) {
                decompressed_buffer[out_idx++] = 0;
            }
        }
    }

    if (out_idx < max_len) {
        memset(decompressed_buffer + out_idx, 0, max_len - out_idx);
    }
    return decompressed_buffer;
}

int World_HullPointContents(int hullnum, int nodenum, const vec3_t point) {
    if (!g_world.is_loaded) return CONTENTS_EMPTY;

    if (hullnum == 0) {
        int leaf = World_PointInLeaf(point);
        return g_world.leafs[leaf].contents;
    }

    while (nodenum >= 0) {
        if (nodenum >= g_world.numclipnodes) return CONTENTS_EMPTY;
        const dclipnode_t *node = &g_world.clipnodes[nodenum];
        const dplane_t *plane = &g_world.planes[node->planenum];
        float dist = DotProduct(point, plane->normal) - plane->dist;
        nodenum = (dist >= 0.0f) ? node->children[0] : node->children[1];
    }

    return nodenum;
}
