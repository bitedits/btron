/*
 * tronMSX — the BMSX real-body record.  B-MSX Rev 1.05, §4.1.
 * Fixed 64-byte little-endian header followed by `size` payload bytes.
 */

#ifndef _BTRON_EMULATOR_MSX_BODY_H_
#define _BTRON_EMULATOR_MSX_BODY_H_

#include <stdint.h>

typedef struct {
    char     magic[4];    /* 0  'B','M','S','X'                          */
    uint16_t version;     /* 4  == 1                                     */
    uint16_t kind;        /* 6  1=rom 2=com 3=bload 4=cas                 */
    uint16_t machine;     /* 8  1=MSX 2=MSX2 3=MSX2+                      */
    uint16_t flags;       /* 10 bit0 pause at first frame, bit1 kb-hook   */
    uint32_t entry;       /* 12 0x4000 for kind=1                         */
    uint32_t size;        /* 16 payload bytes                             */
    char     bios_key[16];/* 20 settings key, empty = default C-BIOS      */
    char     name[28];    /* 36 UTF-8, NUL-terminated                     */
} b_msx_body;

typedef char msx_body_size_must_be_64[ (sizeof(b_msx_body) == 64) ? 1 : -1 ];

#define BMSX_KIND_ROM   1u
#define BMSX_KIND_COM   2u
#define BMSX_KIND_BLOAD 3u
#define BMSX_KIND_CAS   4u

#define BMSX_MACHINE_MSX   1u
#define BMSX_MACHINE_MSX2  2u
#define BMSX_MACHINE_MSX2P 3u

#define BMSX_FLAG_PAUSE     0x0001u
#define BMSX_FLAG_KB_HOOK   0x0002u

/* Read the header off the front of a buffer; host-endian neutral (§4.1). */
static inline void b_msx_body_load(b_msx_body *b, const uint8_t *h)
{
    uint32_t i;
    for (i = 0; i < 64; i++) ((uint8_t *)b)[i] = h[i];
}

static inline uint16_t b_msx_rd16(const uint8_t *p)
{ return (uint16_t)(p[0] | (p[1] << 8)); }

static inline uint32_t b_msx_rd32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

#endif /* _BTRON_EMULATOR_MSX_BODY_H_ */
