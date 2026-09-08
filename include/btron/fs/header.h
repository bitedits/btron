#ifndef _BTRON_FS_HEADER_H_
#define _BTRON_FS_HEADER_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * File Header – exactly 192 bytes on disk (big-endian classic)
 * Only the most important fields are shown; the rest is padding/reserved.
 */
typedef struct {
    UH   flags;          /* TTTT xxxx BAPO xRWE */
    UH   atype;          /* application type */
    /* timestamps, owner, group, link count, index level, size … */
    UB   reserved[192 - 4];
} FileHeader;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_HEADER_H_ */
