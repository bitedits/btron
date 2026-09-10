/*
 * B-System BTRON3 Filesystem — fs_types.h
 * Common constants, primitive typedefs, and the FS_LINK struct.
 * All values are per FS.md §15 (constants summary).
 *
 * Multi-byte on-disk fields are big-endian (VOL_MAGIC_BE = 0x42FE).
 * UTF-8 name encoding used throughout (TC encoding deferred).
 */

#ifndef _BTRON_FS_TYPES_H_
#define _BTRON_FS_TYPES_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Block / size constants ────────────────────────────────────── */
#define BTRON_BLOCK_SIZE        1024   /* logical block size (bytes)        */
#define BTRON_SYS_HDR_SIZE       128   /* system header size (bytes)        */
#define BTRON_FILE_HDR_SIZE      192   /* FileHeader size (bytes)           */
#define BTRON_REC_IDX_SIZE        16   /* RecordIndex entry size (bytes)    */
#define BTRON_FRAG_ENT_SIZE        6   /* on-disk FragEntry size (bytes)    */
#define BTRON_FID_ENT_SIZE         4   /* FID table entry size (bytes)      */
#define BTRON_NAME_HASH_SIZE       4   /* short-name hash entry (bytes)     */
#define BTRON_MAX_NAME_BYTES      40   /* max UTF-8 file name length        */
#define BTRON_FRAG_TABLE_MAX      32   /* max fragment entries per file     */
#define REC_IDX_LEVEL0_MAX        40   /* max RecordIndex entries at level 0 */

/* ── Volume magic / format IDs ─────────────────────────────────── */
#define VOL_MAGIC_BE            0x42FE  /* classic big-endian (standard)    */
#define VOL_MAGIC_LE            0x52FE  /* little-endian quasi variant      */
#define FS_TYPE_STD             0x6400  /* standard FS type                 */
#define FS_TYPE_EXT             0x6401  /* extended FS type                 */

/* ── Special FID values ─────────────────────────────────────────── */
#define FID_ROOT                     0  /* root file (always FID 0)        */
#define FID_INVALID          0xFFFFFFFFu /* sentinel for "no FID"           */

/* ── File header flags (TTTT xxxx BAPO xRWE) ───────────────────── */
#define FTYPE_SHIFT             12      /* bits[15:12] = file type         */
#define FTYPE_LINK               0      /* 0 = link-file                   */
#define FTYPE_NORMAL             1      /* 1 = normal file                 */
#define FFLG_PERM             0x0020    /* P: delete-protect               */
#define FFLG_WPROTECT         0x0010    /* O: write-protect                */
#define FFLG_ATTR_B           0x0200    /* B: app attribute                */
#define FFLG_ATTR_A           0x0100    /* A: app attribute                */
#define FFLG_READ             0x0004    /* R: owner read                   */
#define FFLG_WRITE            0x0002    /* W: owner write                  */
#define FFLG_EXEC             0x0001    /* E: owner execute                */

/* ── Real-Body / OBJ flags (stored in FileHeader.flags high bits) ─ */
#define OBJ_EXEC              0x8000    /* executable Real Body            */
#define OBJ_DEV               0x2000    /* device Real Body                */
#define OBJ_EJECT             0x1000    /* removable device                */

/* ── Primitive typedefs ─────────────────────────────────────────── */
typedef UW  FID;   /* File ID — index into FID table (0-based)  */
typedef UW  BLK;   /* Logical block address (0-based)           */

/*
 * FS_LINK — target descriptor used by cre_lnk / del_lnk.
 * Named FS_LINK to avoid collision with proc.h's process LINK.
 * The attr[5] array maps to [ATR1..ATR5] columns in CLU.md fs -l.
 */
typedef struct {
    FID  target_fid;   /* FID of referenced Real Body     */
    UH   attr[5];      /* ATR1..ATR5 link attributes      */
} FS_LINK;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_TYPES_H_ */
