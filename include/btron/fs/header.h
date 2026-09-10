/*
 * B-System BTRON3 Filesystem — header.h
 * On-disk File Header (Real Body header): exactly 192 bytes, big-endian.
 *
 * Layout per FS.md §5.
 *
 * flags word bits (TTTT xxxx BAPO xRWE):
 *   bits[15:12] TTTT — file type: 0=link-file, 1=normal file, 2+=reserved
 *   bits[11: 8] xxxx — reserved (0)
 *   bit [7]     B    — app attribute B
 *   bit [6]     A    — app attribute A
 *   bit [5]     P    — permanent (delete-protect)
 *   bit [4]     O    — write-protect
 *   bit [3]     x    — reserved
 *   bit [2]     R    — owner read
 *   bit [1]     W    — owner write
 *   bit [0]     E    — owner execute
 *
 * Timestamps: seconds since 2000-01-01 00:00:00 UTC (BTRON STIME epoch).
 *
 * name[40]: UTF-8 file name, zero-padded.  Full name per FS.md §3.3.
 *
 * The struct is __attribute__((packed)) to suppress any alignment padding.
 * Field sizes: 2+2+4+4+4+2+2+2+2+4+4+40 = 72 bytes; _pad = 120 bytes.
 * Total = 192 bytes.
 */

#ifndef _BTRON_FS_HEADER_H_
#define _BTRON_FS_HEADER_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct __attribute__((packed)) {
    UH   flags;          /* +0    2 B  TTTT xxxx BAPO xRWE          */
    UH   atype;          /* +2    2 B  application type (ATYPE)     */
    UW   ctime;          /* +4    4 B  creation time (STIME epoch)  */
    UW   mtime;          /* +8    4 B  modification time            */
    UW   atime;          /* +12   4 B  access time                  */
    UH   owner;          /* +16   2 B  owner ID                     */
    UH   group;          /* +18   2 B  group ID                     */
    UH   nlnk;           /* +20   2 B  hard-link count              */
    UH   idxlv;          /* +22   2 B  index level: 0/1/2           */
    UW   nrec;           /* +24   4 B  number of records            */
    UW   total_size;     /* +28   4 B  total payload bytes          */
    UB   name[40];       /* +32  40 B  full UTF-8 file name         */
    UW   data_blk;       /* +72   4 B  first data block (extent)    */
    UB   _pad[116];      /* +76 116 B  reserved, zero               */
                         /* total: 72 + 4 + 116 = 192 bytes         */
} FileHeader;

#ifndef __cplusplus
_Static_assert(sizeof(FileHeader) == 192,
    "FileHeader must be exactly 192 bytes (BTRON FILE_HDR_SIZE)");
#endif

/* Convenience macro: extract file type from flags */
#define FILE_HDR_TYPE(hdr)   (((hdr).flags >> 12) & 0xF)

/* Build a flags word for a normal file with default RW access */
#define FILE_HDR_FLAGS_NORMAL  ((UH)((FTYPE_NORMAL << 12) | FFLG_READ | FFLG_WRITE))

/* Build a flags word for a link-file */
#define FILE_HDR_FLAGS_LINK    ((UH)(FTYPE_LINK << 12))

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_HEADER_H_ */
