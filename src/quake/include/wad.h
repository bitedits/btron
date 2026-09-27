/*
 * src/quake/include/wad.h — WAD Lump Loader for Quake
 *
 * Cleanroom C99 implementation for B-System.
 * Copyright 2026 Synrc Research Center. MIT License / GPL-2.0.
 */

#ifndef QUAKE_WAD_H
#define QUAKE_WAD_H

#include "quakedef.h"

#define CMP_NONE 0
#define CMP_LZSS 1

#define TYP_NONE  0
#define TYP_LABEL 1
#define TYP_LUMPY 64
#define TYP_PALETTE 64
#define TYP_QTEX  65
#define TYP_QPIC  66
#define TYP_SOUND 67
#define TYP_MIPTEX 68

typedef struct {
    int  width;
    int  height;
    byte data[4];
} qpic_t;

typedef struct {
    int  filepos;
    int  disksize;
    int  size;
    char type;
    char compression;
    char pad1, pad2;
    char name[16];
} lumpinfo_t;

void  W_LoadWadFile(const char *filename);
byte *W_GetLumpName(const char *name);
qpic_t *Draw_PicFromWad(const char *name);

#endif /* QUAKE_WAD_H */
