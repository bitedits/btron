/*
 * src/quake/core/world.c — BSP v29 World Geometry Loader & Spatial Engine
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/world.h"
#include "../include/fs_btron.h"

/* A PVS row is (visleafs+7)/8 bytes; e1m1 measures 144, and the largest maps
 * in pak0 stay well under this ceiling. */
#define WORLD_MAX_PVS_BYTES 4096

bsp_world_t g_world;

void World_UnloadMap(void) {
    if (g_world.is_loaded) {
        if (g_world.hunk_mark >= 0) {
            Hunk_FreeToLowMark(g_world.hunk_mark);
        }
        memset(&g_world, 0, sizeof(g_world));
    }
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));
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

    World_UnloadMap();
    int mark = Hunk_LowMark();

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

int World_VisLeafCount(void) {
    if (!g_world.is_loaded || g_world.nummodels <= 0) return 0;
    return g_world.models[0].visleafs;
}

int World_VisRowBytes(void) {
    int n = World_VisLeafCount();
    return n > 0 ? (n + 7) / 8 : 0;
}

/* qbsp numbers the visible leafs as the contiguous prefix 1..visleafs of the
 * leaf array, and a PVS row addresses those numbers, not leaf numbers:
 * the bit for leaf N is bit (N-1) of the row.  Solid/sky leafs (visofs == -1)
 * have no bit at all. */
int World_LeafVisBit(int leafnum) {
    if (!g_world.is_loaded) return -1;
    if (leafnum <= 0 || leafnum > World_VisLeafCount()) return -1;
    if (g_world.leafs[leafnum].visofs < 0) return -1;
    return leafnum - 1;
}

byte *World_LeafPVS(int leafnum, byte *decompressed_buffer, int max_len) {
    if (!decompressed_buffer || max_len <= 0) return NULL;

    int row_bytes = World_VisRowBytes();
    if (row_bytes > max_len) row_bytes = max_len;
    if (row_bytes > WORLD_MAX_PVS_BYTES) row_bytes = WORLD_MAX_PVS_BYTES;

    int visofs = (World_LeafVisBit(leafnum) >= 0) ? g_world.leafs[leafnum].visofs : -1;
    if (visofs < 0 || visofs >= g_world.vislen || row_bytes <= 0) {
        /* The camera is not in a leaf qbsp gave a row to — either outside the
         * world or embedded in solid geometry.  There is no visibility data to
         * consult, so mark every leaf with a row visible: over-drawing is
         * survivable, holes where a wall should be are not.  Faces belonging to
         * the row-less leafs are still skipped by callers through
         * World_LeafVisBit(), so this does not draw solid/sky shells. */
        memset(decompressed_buffer, 0xFF, row_bytes);
        if (max_len > row_bytes)
            memset(decompressed_buffer + row_bytes, 0, max_len - row_bytes);
        return decompressed_buffer;
    }

    const byte *in  = g_world.visdata + visofs;
    const byte *end = g_world.visdata + g_world.vislen;
    memset(decompressed_buffer, 0, row_bytes);

    /* Run-length form: a literal byte, or 0x00 followed by a count of zeros. */
    int out_idx = 0;
    while (out_idx < row_bytes && in < end) {
        if (*in) {
            decompressed_buffer[out_idx++] = *in++;
        } else {
            in++;
            if (in >= end) break;
            int count = *in++;
            while (count-- > 0 && out_idx < row_bytes)
                decompressed_buffer[out_idx++] = 0;
        }
    }

    if (max_len > row_bytes)
        memset(decompressed_buffer + row_bytes, 0, max_len - row_bytes);

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
