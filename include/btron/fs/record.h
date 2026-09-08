#ifndef _BTRON_FS_RECORD_H_
#define _BTRON_FS_RECORD_H_

#include <btron/fs/fs_types.h>
#include <btron/tad.h>          /* RT_* */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Record Index entry – 16 bytes on disk
 */
typedef struct {
    UH   kind;           /* 0 = normal, special values for link/continuation */
    UH   type;           /* record type (RT_*) + subtype bits */
    UW   size;           /* payload size in bytes */
    UW   offset;         /* byte offset into Data Blocks */
    /* remaining bytes of the 16-byte slot are flags / reserved */
} RecordIndex;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_RECORD_H_ */
