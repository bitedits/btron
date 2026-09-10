#ifndef _BTRON_FS_HEADER_H_
#define _BTRON_FS_HEADER_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * File Header – 192 bytes on disk.
 * Field order is logical; exact classic offsets can be refined later.
 * Multi-byte fields are big-endian on classic volumes.
 */

typedef struct {
    UH   flags;          /* TTTT xxxx BAPO xRWE */
    UH   atype;          /* application type */
    UW   ctime;          /* creation  (STIME-style) */
    UW   mtime;          /* modification */
    UW   atime;          /* access */
    UH   owner;
    UH   group;
    UH   nlnk;           /* link count */
    UH   idxlv;          /* index level: 0=direct, 1/2=indirect */
    UW   nrec;           /* number of records */
    UW   total_size;     /* total payload bytes */
    UB   reserved[192 - 32];
} FileHeader;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_HEADER_H_ */
