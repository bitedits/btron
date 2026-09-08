#ifndef _BTRON_FS_VOLUME_H_
#define _BTRON_FS_VOLUME_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Volume Header (first part of System Block) – conceptual */
typedef struct {
    UH   magic;          /* 0x42FE or 0x52FE */
    UH   fs_type;        /* 0x6400 / 0x6401 */
    UW   total_blocks;
    UW   free_blocks;
    UW   fid_table_off;
    UW   bitmap_off;
    UW   data_start;
    /* … more fields as needed … */
} VolumeHeader;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_VOLUME_H_ */
