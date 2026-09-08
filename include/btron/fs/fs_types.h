#ifndef _BTRON_FS_TYPES_H_
#define _BTRON_FS_TYPES_H_

#include <btron/types.h>

/* Common constants */
#define BTRON_BLOCK_SIZE     1024
#define BTRON_FILE_HDR_SIZE   192
#define BTRON_REC_IDX_SIZE     16

/* Volume magic / format */
#define VOL_MAGIC_BE         0x42FE      /* classic big-endian */
#define VOL_MAGIC_LE         0x52FE      /* little-endian (quasi) */
#define FS_TYPE_STD          0x6400
#define FS_TYPE_EXT          0x6401

#endif /* _BTRON_FS_TYPES_H_ */
