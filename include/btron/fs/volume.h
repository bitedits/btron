/*
 * B-System BTRON3 Filesystem — volume.h
 * On-disk Volume Header: exactly 128 bytes, big-endian fields.
 *
 * Layout per FS.md §3.1.  The struct is __attribute__((packed)) to
 * guarantee no compiler padding.  _Static_assert enforces the size.
 *
 * Field naming follows FS.md §3.1 table:
 *   magic       — 0x42FE (standard BE) or 0x52FE (quasi LE)
 *   fs_type     — 0x6400 (standard) or 0x6401 (extended)
 *   nfmax       — max file count (number of FIDs)
 *   nlb         — total logical blocks on volume
 *   sfidt       — FID-table size in logical blocks
 *   sfnmt       — short-name-table size in logical blocks
 *   nbmp        — each bitmap's size in logical blocks (two bitmaps: used + bad)
 *   access_level— 0 = none, 1 = partial, 2 = full
 *   dirty       — 0 = clean / unmounted, 1 = mounted / dirty
 *   free_blocks — live free-block count (updated on alloc/free)
 *   data_start  — block address of first file-data-region block
 *   vol_name    — UTF-8 volume name (= root file name); zero-padded to 40 bytes
 *   _pad        — reserved; always zero; pads struct to exactly 128 bytes
 */

#ifndef _BTRON_FS_VOLUME_H_
#define _BTRON_FS_VOLUME_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    UH   magic;                  /* +0   2 B  VOL_MAGIC_BE / VOL_MAGIC_LE      */
    UH   fs_type;                /* +2   2 B  FS_TYPE_STD  / FS_TYPE_EXT       */
    UW   nfmax;                  /* +4   4 B  max FIDs                          */
    UW   nlb;                    /* +8   4 B  total logical blocks              */
    UH   sfidt;                  /* +12  2 B  FID-table blocks                  */
    UH   sfnmt;                  /* +14  2 B  short-name-table blocks           */
    UH   nbmp;                   /* +16  2 B  bitmap blocks (per bitmap)        */
    UB   access_level;           /* +18  1 B  0/1/2                             */
    UB   dirty;                  /* +19  1 B  0=clean, 1=dirty                  */
    UW   free_blocks;            /* +20  4 B  live free-block count             */
    UW   data_start;             /* +24  4 B  block# of file data region start  */
    UB   vol_name[40];           /* +28 40 B  UTF-8 volume/root name            */
    UB   _pad[60];               /* +68 60 B  reserved, zero                    */
                                 /* total: 2+2+4+4+2+2+2+1+1+4+4+40+60 = 128 B */
} VolumeHeader;

#ifndef __cplusplus
_Static_assert(sizeof(VolumeHeader) == 128,
    "VolumeHeader must be exactly 128 bytes (BTRON SYS_HDR_SIZE)");
#endif

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_VOLUME_H_ */
