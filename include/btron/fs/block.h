/*
 * B-System BTRON3 Filesystem — block.h
 * Portable block device abstraction (BlkDev) and backend declarations.
 *
 * Per FS.md §11.  All FS code touches the volume only through BlkDev
 * read/write by logical block address (LBA).  The block_size is always
 * BTRON_BLOCK_SIZE (1024) for BTRON volumes.
 *
 * Error codes: 0 = success, negative = error (compatible with ER).
 */

#ifndef _BTRON_FS_BLOCK_H_
#define _BTRON_FS_BLOCK_H_

#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * BlkDev — portable block device handle.
 * All on-disk access goes through this interface.
 *
 *   read (ctx, lba, buf, count)  — read `count` blocks starting at `lba`
 *   write(ctx, lba, buf, count)  — write `count` blocks starting at `lba`
 *                                  NULL write means read-only (ROM memdisk)
 *   block_size  — bytes per block (1024 for BTRON logical blocks)
 *   nblocks     — total number of logical blocks on this device
 *   ctx         — opaque per-backend state pointer
 */
typedef struct BlkDev {
    int  (*read )(struct BlkDev *dev, BLK lba, void       *buf, UW count);
    int  (*write)(struct BlkDev *dev, BLK lba, const void *buf, UW count);
    UW   block_size;   /* always 1024 for BTRON                    */
    UW   nblocks;      /* total blocks on device                   */
    void *ctx;         /* backend private data                     */
} BlkDev;

/* ── Backend constructors ──────────────────────────────────────── */

/*
 * blk_mem_create — memdisk backend.
 *   buf      : raw memory holding the volume image (caller owns lifetime)
 *   nbytes   : byte size of the buffer (must be a multiple of 1024)
 *   readonly : non-zero → write callback is NULL (ROM image)
 * Returns a heap-allocated BlkDev, or NULL on error.
 * The caller frees with blk_destroy().
 */
BlkDev *blk_mem_create(void *buf, UW nbytes, int readonly);

/*
 * blk_file_create — POSIX file backend.
 *   path       : file path for the raw volume image
 *   create_new : if non-zero, create (truncate) the file to nblocks * 1024 bytes
 *   nblocks    : used only when create_new != 0; ignored on open of existing file
 * Returns a heap-allocated BlkDev, or NULL on error.
 * The caller frees with blk_destroy().
 */
BlkDev *blk_file_create(const char *path, int create_new, UW nblocks);

/*
 * blk_destroy — release a BlkDev and its backend resources.
 * Does NOT free the memory buffer for blk_mem; caller is responsible.
 */
void blk_destroy(BlkDev *dev);

/*
 * blk_file_close — close the FILE* and free a file-backed BlkDev.
 * Use instead of blk_destroy() for BlkDevs created by blk_file_create().
 */
void blk_file_close(BlkDev *dev);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_BLOCK_H_ */
