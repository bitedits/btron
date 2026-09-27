/*
 * src/quake/core/wad.c — WAD Lump Loader for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#include "../include/wad.h"
#include <string.h>

#define MAX_LUMPS 256

static lumpinfo_t s_lumps[MAX_LUMPS];
static int        s_num_lumps = 0;
static byte      *s_wad_data = NULL;

void W_LoadWadFile(const char *filename) {
    (void)filename;
    s_num_lumps = 0;
}

byte *W_GetLumpName(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < s_num_lumps; i++) {
        if (q_strcasecmp(s_lumps[i].name, name) == 0) {
            return s_wad_data + s_lumps[i].filepos;
        }
    }
    return NULL;
}

qpic_t *Draw_PicFromWad(const char *name) {
    byte *data = W_GetLumpName(name);
    return (qpic_t *)data;
}
