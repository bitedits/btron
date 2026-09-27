/*
 * src/quake/include/fs_btron.h — File System, PAK & BSP Loader for B-System
 *
 * v2 — Fast-path PAK loading:
 *  • Entire PAK archive is read once into the hunk at FS_Init().
 *  • FNV-1a hash table maps path → [offset, length] in O(1).
 *  • BSP pre-cache keeps the .bsp bytes in the hunk across frames.
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_FS_BTRON_H
#define QUAKE_FS_BTRON_H

#include "quakedef.h"
#include "bspfile.h"

/* ── PAK directory entry (on-disk format, 64 bytes) ─────────────── */
typedef struct {
    char name[56];
    int  filepos;
    int  filelen;
} packfile_t;

/* ── Legacy pack_t kept for API compatibility ────────────────────── */
typedef struct {
    char        filename[MAX_OSPATH];
    FILE       *handle;
    int         numfiles;
    packfile_t *files;
} pack_t;

/* ── Hash-table slot (open-address, power-of-2 size) ─────────────── */
#define FS_HT_BITS   13               /* 8192 slots, load factor ≤ 0.5 */
#define FS_HT_SIZE   (1 << FS_HT_BITS)
#define FS_HT_MASK   (FS_HT_SIZE - 1)

typedef struct {
    uint32_t hash;      /* FNV-1a of lowercased path; 0 = empty */
    int      offset;    /* Byte offset inside s_pak_image[]     */
    int      length;    /* Byte count                           */
    char     name[56];  /* Stored for hash-collision resolution */
} fs_ht_slot_t;

/* ── BSP lump pre-cache (populated by FS_CacheBSP) ──────────────── */
typedef struct {
    char         path[64];           /* Map path currently cached       */
    const byte  *data;               /* Pointer into hunk               */
    int          length;             /* Total BSP byte count            */
    const byte  *lumps[HEADER_LUMPS];/* Per-lump base pointers          */
    int          lump_lens[HEADER_LUMPS];
    int          is_ready;
} fs_bsp_cache_t;

extern fs_bsp_cache_t g_bsp_cache;

/* ── Public API ──────────────────────────────────────────────────── */
int   FS_Init(void);
byte *FS_LoadFile(const char *path, int *out_len);
void  FS_FreeFile(void *buffer);

/* Warm the BSP hunk cache.  Returns 1 on success, 0 on failure.    */
int   FS_CacheBSP(const char *bsp_path);

/* Return a pointer directly into the PAK image (zero-copy). */
const byte *FS_FindInPak(const char *path, int *out_len);

#endif /* QUAKE_FS_BTRON_H */
