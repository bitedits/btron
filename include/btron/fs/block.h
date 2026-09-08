#ifndef _BTRON_FS_BLOCK_H_
#define _BTRON_FS_BLOCK_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Fragment / Extent descriptor (conceptual) */
typedef struct {
    UW   start_block;    /* first logical block */
    UW   count;          /* number of consecutive blocks */
} Fragment;

/* A Real Body’s data is a sequence of Fragments */
typedef struct {
    UW         nfrag;
    Fragment   frag[1];  /* variable length */
} FragmentTable;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_BLOCK_H_ */
