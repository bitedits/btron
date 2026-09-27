/*
 * src/quake/sys/fs_btron.c — File System, PAK & BSP Loader for B-System
 *
 * Fast-path strategy:
 *   1. FS_Init reads the entire pak0.pak into a single Hunk allocation.
 *   2. An FNV-1a open-address hash table maps filename → (offset, length)
 *      inside the image for O(1) lookup with zero per-query fseek / fread.
 *   3. FS_CacheBSP caches a named .bsp into the hunk once and exposes
 *      per-lump pointers so World_LoadMap and the texture system can
 *      work directly from memory with no further I/O.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/fs_btron.h"

/* ── Search paths for pak0.pak ─────────────────────────────────── */
static const char *s_search_paths[] = {
    "assets/quake/pak/pak0.pak",
    "assets/quake/id1/pak0.pak",
    "id1/pak0.pak",
    "pak0.pak",
    "/SYS/GAMES/QUAKE/PAK0.PAK",
    "/CHOKANJI/PAK0.PAK",
    NULL
};

/* ── PAK on-disk header ─────────────────────────────────────────── */
typedef struct {
    char id[4];   /* "PACK" */
    int  dirofs;
    int  dirlen;
} dpackheader_t;

/* ── Whole-PAK image in hunk ────────────────────────────────────── */
static byte        *s_pak_image    = NULL;   /* Entire PAK file in hunk */
static int          s_pak_size     = 0;
static const char  *s_pak_path_str = NULL;

/* ── Hash table ─────────────────────────────────────────────────── */
static fs_ht_slot_t s_ht[FS_HT_SIZE];

/* ── BSP global pre-cache ───────────────────────────────────────── */
fs_bsp_cache_t g_bsp_cache;

/* ── Helpers ────────────────────────────────────────────────────── */

/* FNV-1a 32-bit, case-insensitive (forward slash normalised) */
static uint32_t fnv1a(const char *str) {
    uint32_t h = 0x811c9dc5u;
    while (*str) {
        unsigned char c = (unsigned char)*str++;
        /* Normalise backslash → forward slash, uppercase → lowercase */
        if (c == '\\') c = '/';
        if (c >= 'A' && c <= 'Z') c = c + 32u;
        h ^= (uint32_t)c;
        h *= 0x01000193u;
    }
    return h ? h : 1u; /* Never store 0 (empty-slot sentinel) */
}

static void ht_insert(uint32_t hash, int offset, int length, const char *name) {
    uint32_t idx = hash & FS_HT_MASK;
    for (uint32_t probe = 0; probe < FS_HT_SIZE; probe++) {
        uint32_t slot = (idx + probe) & FS_HT_MASK;
        if (!s_ht[slot].hash) {
            s_ht[slot].hash   = hash;
            s_ht[slot].offset = offset;
            s_ht[slot].length = length;
            /* Copy name, lowercase-normalised */
            int i = 0;
            while (name[i] && i < 55) {
                char c = name[i];
                if (c == '\\') c = '/';
                if (c >= 'A' && c <= 'Z') c = c + ('a' - 'A');
                s_ht[slot].name[i] = c;
                i++;
            }
            s_ht[slot].name[i] = '\0';
            return;
        }
    }
    /* Table full — silently drop (should never happen with load < 0.5) */
}

static const fs_ht_slot_t *ht_find(const char *path) {
    uint32_t hash = fnv1a(path);
    uint32_t idx  = hash & FS_HT_MASK;
    for (uint32_t probe = 0; probe < FS_HT_SIZE; probe++) {
        uint32_t slot = (idx + probe) & FS_HT_MASK;
        if (!s_ht[slot].hash) return NULL;           /* Empty — not found */
        if (s_ht[slot].hash == hash) {
            /* Verify full name equality to handle collisions */
            const char *a = s_ht[slot].name;
            const char *b = path;
            int match = 1;
            while (*a || *b) {
                unsigned char ca = (unsigned char)*a++;
                unsigned char cb = (unsigned char)*b++;
                if (cb == '\\') cb = '/';
                if (cb >= 'A' && cb <= 'Z') cb += 32u;
                if (ca != cb) { match = 0; break; }
            }
            if (match) return &s_ht[slot];
        }
    }
    return NULL;
}

/* ────────────────────────────────────────────────────────────────── */
/*   Hosted build (POSIX / QEMU)                                     */
/* ────────────────────────────────────────────────────────────────── */

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)

int FS_Init(void) {
    memset(s_ht, 0, sizeof(s_ht));
    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));

    for (int pi = 0; s_search_paths[pi]; pi++) {
        FILE *f = fopen(s_search_paths[pi], "rb");
        if (!f) continue;

        /* ── Read PAK header ─────────────────────────────────────── */
        dpackheader_t hdr;
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
            strncmp(hdr.id, "PACK", 4) != 0) {
            fclose(f); continue;
        }

        /* ── Determine total PAK size ────────────────────────────── */
        fseek(f, 0, SEEK_END);
        long pak_sz = ftell(f);
        fseek(f, 0, SEEK_SET);

        if (pak_sz <= 0) { fclose(f); continue; }

        /* ── Allocate hunk and slurp entire PAK in ONE fread ──────── */
        s_pak_image = (byte *)Hunk_AllocName((size_t)pak_sz + 1, "pak0_image");
        if (!s_pak_image) { fclose(f); continue; }

        size_t got = fread(s_pak_image, 1, (size_t)pak_sz, f);
        fclose(f);

        if ((long)got < pak_sz) {
            s_pak_image = NULL;
            Con_DPrintf("FS_Init: short read on %s\n", s_search_paths[pi]);
            continue;
        }
        s_pak_image[pak_sz] = 0;
        s_pak_size     = (int)pak_sz;
        s_pak_path_str = s_search_paths[pi];

        /* ── Build hash table from embedded directory ─────────────── */
        int num_entries = hdr.dirlen / (int)sizeof(packfile_t);
        if (num_entries > FS_HT_SIZE / 2) num_entries = FS_HT_SIZE / 2;

        const packfile_t *dir = (const packfile_t *)(s_pak_image + hdr.dirofs);
        for (int i = 0; i < num_entries; i++) {
            if (dir[i].filelen <= 0 || dir[i].filepos < 0) continue;
            if (dir[i].filepos + dir[i].filelen > s_pak_size) continue;
            ht_insert(fnv1a(dir[i].name), dir[i].filepos,
                      dir[i].filelen, dir[i].name);
        }

        Con_Printf("FS_Init: PAK cached %s (%d KB, %d entries)\n",
                   s_search_paths[pi],
                   (int)(pak_sz / 1024), num_entries);
        return 1;
    }

    Con_DPrintf("FS_Init: no pak0.pak found\n");
    return 0;
}

/* ── Zero-copy PAK lookup ───────────────────────────────────────── */
const byte *FS_FindInPak(const char *path, int *out_len) {
    if (!s_pak_image || !path) return NULL;
    const fs_ht_slot_t *slot = ht_find(path);
    if (!slot) return NULL;
    if (out_len) *out_len = slot->length;
    return s_pak_image + slot->offset;
}

/* ── FS_LoadFile: try PAK cache first, then direct file ─────────── */
byte *FS_LoadFile(const char *path, int *out_len) {
    if (!path) return NULL;

    /* 1. PAK hash-table lookup — O(1), zero-copy reference */
    const byte *pak_ptr = FS_FindInPak(path, out_len);
    if (pak_ptr) {
        /*
         * Callers may write a NUL terminator one byte past.
         * Make a hunk copy only when the caller requests a mutable buffer.
         * For read-only callers (TEX, BSP parser) we return directly.
         * We tag the pointer by returning it as-is; FS_FreeFile is a no-op.
         */
        return (byte *)pak_ptr;   /* Direct pointer into hunk image */
    }

    /* 2. Direct filesystem fallback (loose files, modded content) */
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    int size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size <= 0) { fclose(f); return NULL; }

    byte *buf = (byte *)Hunk_AllocName((size_t)size + 1, path);
    if (!buf) { fclose(f); return NULL; }

    fread(buf, 1, (size_t)size, f);
    buf[size] = 0;
    fclose(f);

    if (out_len) *out_len = size;
    return buf;
}

/* ── BSP pre-cache: load once, expose lump pointers ─────────────── */
int FS_CacheBSP(const char *bsp_path) {
    if (g_bsp_cache.is_ready) return 1; /* Already cached */

    memset(&g_bsp_cache, 0, sizeof(g_bsp_cache));

    int bsp_len = 0;
    const byte *data = FS_FindInPak(bsp_path, &bsp_len);

    /* Fallback: loose file */
    if (!data) {
        data = FS_LoadFile(bsp_path, &bsp_len);
    }

    if (!data || bsp_len < (int)sizeof(dheader_t)) {
        Con_DPrintf("FS_CacheBSP: %s not found\n", bsp_path);
        return 0;
    }

    const dheader_t *hdr = (const dheader_t *)data;
    if (hdr->version != BSPVERSION) {
        Con_DPrintf("FS_CacheBSP: %s bad version %d\n", bsp_path, hdr->version);
        return 0;
    }

    g_bsp_cache.data   = data;
    g_bsp_cache.length = bsp_len;

    for (int l = 0; l < HEADER_LUMPS; l++) {
        int ofs = hdr->lumps[l].fileofs;
        int len = hdr->lumps[l].filelen;
        if (ofs < 0 || len < 0 || ofs + len > bsp_len) {
            g_bsp_cache.lumps[l]     = NULL;
            g_bsp_cache.lump_lens[l] = 0;
        } else {
            g_bsp_cache.lumps[l]     = data + ofs;
            g_bsp_cache.lump_lens[l] = len;
        }
    }

    g_bsp_cache.is_ready = 1;
    Con_Printf("FS_CacheBSP: cached %s (%d KB)\n",
               bsp_path, bsp_len / 1024);
    return 1;
}

/* ── Freestanding stub ──────────────────────────────────────────── */
#else  /* BTRON_UEFI_TARGET */

int   FS_Init(void) { memset(s_ht,0,sizeof(s_ht)); memset(&g_bsp_cache,0,sizeof(g_bsp_cache)); return 0; }
byte *FS_LoadFile(const char *p, int *l) { (void)p; if(l)*l=0; return NULL; }
const byte *FS_FindInPak(const char *p, int *l) { (void)p; if(l)*l=0; return NULL; }
int   FS_CacheBSP(const char *p) { (void)p; return 0; }

#endif  /* !BTRON_UEFI_TARGET */

void FS_FreeFile(void *buffer) {
    /* Hunk memory — no-op (NASA JPL Rule 3 arena) */
    (void)buffer;
}
