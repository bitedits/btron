/*
 * B-System BTRON3 Filesystem — blk_mem.c
 * memdisk block device backend.
 *
 * Operates on a caller-supplied byte buffer.  For read-only (ROM)
 * memdisks the write pointer is NULL and writes return an error.
 *
 * Usage:
 *   uint8_t buf[1024 * 1024];
 *   BlkDev *dev = blk_mem_create(buf, sizeof(buf), 0 / * writable * /);
 *   vol_format(dev, 256, 1024, "SYS");
 *   ...
 *   blk_destroy(dev);
 */

#include <btron/fs/block.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  include <stdlib.h>
#  include <string.h>
#else
   /* Bare-metal: rely on kernel malloc/memcpy aliases */
   extern void *Imalloc(size_t sz);
   extern void  Ifree(void *ptr);
   extern void *tkl_memcpy(void *d, const void *s, size_t n);
#  define malloc  Imalloc
#  define free    Ifree
#  define memcpy  tkl_memcpy
#endif

/* ── Private context ───────────────────────────────────────────── */
typedef struct {
    unsigned char *buf;
    unsigned int   nbytes;
    int            readonly;
} MemCtx;

/* ── Backend callbacks ─────────────────────────────────────────── */
static int mem_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    MemCtx *ctx = (MemCtx *)dev->ctx;
    unsigned int off = lba * dev->block_size;
    unsigned int len = count * dev->block_size;
    if (off + len > ctx->nbytes)
        return -1;
    memcpy(buf, ctx->buf + off, len);
    return 0;
}

static int mem_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    MemCtx *ctx = (MemCtx *)dev->ctx;
    if (ctx->readonly)
        return -2;  /* read-only */
    unsigned int off = lba * dev->block_size;
    unsigned int len = count * dev->block_size;
    if (off + len > ctx->nbytes)
        return -1;
    memcpy(ctx->buf + off, buf, len);
    return 0;
}

/* ── Constructor / destructor ──────────────────────────────────── */
BlkDev *blk_mem_create(void *buf, UW nbytes, int readonly)
{
    if (!buf || nbytes < BTRON_BLOCK_SIZE)
        return (BlkDev *)0;

    BlkDev *dev = (BlkDev *)malloc(sizeof(BlkDev));
    if (!dev) return (BlkDev *)0;

    MemCtx *ctx = (MemCtx *)malloc(sizeof(MemCtx));
    if (!ctx) { free(dev); return (BlkDev *)0; }

    ctx->buf      = (unsigned char *)buf;
    ctx->nbytes   = nbytes;
    ctx->readonly = readonly;

    dev->read       = mem_read;
    dev->write      = readonly ? (int(*)(BlkDev*, BLK, const void*, UW))0
                                : mem_write;
    dev->block_size = BTRON_BLOCK_SIZE;
    dev->nblocks    = nbytes / BTRON_BLOCK_SIZE;
    dev->ctx        = ctx;

    return dev;
}

void blk_destroy(BlkDev *dev)
{
    if (!dev) return;
    if (dev->ctx) free(dev->ctx);
    free(dev);
}
