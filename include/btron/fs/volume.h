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

/*
 * BrightVVolumeHeader — on-disk 128-byte superblock for B-right/V 4.02 (x86 LE)
 * Layout matches FS_STATE from B-right/V <btron/file.h> and real 2001 hda.qcow2.
 */
typedef struct __attribute__((packed)) {
    UH   magic;                  /* +0x00  2 B  0x52FE (VOL_MAGIC_LE)           */
    UH   fs_type;                /* +0x02  2 B  0x6402 (FS_TYPE_BRIGHTV)        */
    UH   nbmp;                   /* +0x04  2 B  usage bitmap block count (41)   */
    UH   sfidt;                  /* +0x06  2 B  FID table block count (32)      */
    UH   sfnmt;                  /* +0x08  2 B  short-name table blocks (32)    */
    UB   rsv1[14];               /* +0x0A 14 B  reserved / partition metadata   */
    UH   fs_bsize;               /* +0x18  2 B  logical block size (8192)       */
    UH   fs_nfile;               /* +0x1A  2 B  max files / FIDs (65535)        */
    H    fs_lang;                /* +0x1C  2 B  language (0x0021 = F_JPN)       */
    H    fs_level;               /* +0x1E  2 B  access management level (0)     */
    UW   fs_nblk;                /* +0x20  4 B  total logical blocks (1310298)  */
    UW   fs_nfree;               /* +0x24  4 B  free logical blocks (1244040)   */
    UW   fs_mtime;               /* +0x28  4 B  system mtime                    */
    UW   fs_ctime;               /* +0x2C  4 B  system ctime                    */
    UH   fs_name[20];            /* +0x30 40 B  volume name (16-bit T-Code)     */
    UH   fs_locat[20];           /* +0x58 40 B  device locat (16-bit T-Code)    */
                                 /* total: 2+2+2+2+2+14+2+2+2+2+4+4+4+4+40+40 = 128 */
} BrightVVolumeHeader;

#ifndef __cplusplus
_Static_assert(sizeof(VolumeHeader) == 128,
    "VolumeHeader must be exactly 128 bytes (BTRON SYS_HDR_SIZE)");
_Static_assert(sizeof(BrightVVolumeHeader) == 128,
    "BrightVVolumeHeader must be exactly 128 bytes");
#endif


#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_VOLUME_H_ */
