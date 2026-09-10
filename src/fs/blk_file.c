/*
 * B-System BTRON3 Filesystem — blk_file.c
 * POSIX file-backed block device backend.
 *
 * Wraps a flat binary file as a block device.  Used by mkbtronfs and
 * the POSIX hosted build (btron_sys.vol).
 *
 * When create_new != 0, the file is created/truncated and zero-filled
 * to nblocks * BTRON_BLOCK_SIZE bytes.  Otherwise the file is opened
 * for reading+writing and nblocks is derived from the file size.
 *
 * Only available in hosted (POSIX) builds.
 */

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1

#include <btron/fs/block.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Private context ───────────────────────────────────────────── */
typedef struct {
    FILE *fp;
} FileCtx;

/* ── Backend callbacks ─────────────────────────────────────────── */
static int file_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    FileCtx *ctx = (FileCtx *)dev->ctx;
    long off = (long)lba * (long)dev->block_size;
    if (fseek(ctx->fp, off, SEEK_SET) != 0)
        return -1;
    size_t got = fread(buf, dev->block_size, (size_t)count, ctx->fp);
    return (got == (size_t)count) ? 0 : -1;
}

static int file_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    FileCtx *ctx = (FileCtx *)dev->ctx;
    long off = (long)lba * (long)dev->block_size;
    if (fseek(ctx->fp, off, SEEK_SET) != 0)
        return -1;
    size_t written = fwrite(buf, dev->block_size, (size_t)count, ctx->fp);
    if (written != (size_t)count)
        return -1;
    fflush(ctx->fp);
    return 0;
}

static void file_close_ctx(FileCtx *ctx)
{
    if (ctx && ctx->fp) {
        fclose(ctx->fp);
        ctx->fp = NULL;
    }
}

/* ── Constructor / destructor ──────────────────────────────────── */
BlkDev *blk_file_create(const char *path, int create_new, UW nblocks)
{
    if (!path) return NULL;

    FILE *fp = NULL;
    UW derived_nblocks = nblocks;

    if (create_new) {
        /* Create / truncate and zero-fill */
        fp = fopen(path, "w+b");
        if (!fp) return NULL;
        if (nblocks == 0) nblocks = 1024;   /* default 1 MiB */
        unsigned long total = (unsigned long)nblocks * BTRON_BLOCK_SIZE;
        /* Seek to last byte and write a zero to set the file size */
        if (fseek(fp, (long)(total - 1), SEEK_SET) != 0 ||
            fputc(0, fp) == EOF) {
            fclose(fp);
            return NULL;
        }
        /* Zero-fill (fseek+fputc already extends; rewind to fill) */
        rewind(fp);
        {
            unsigned char zbuf[BTRON_BLOCK_SIZE];
            memset(zbuf, 0, sizeof(zbuf));
            for (UW i = 0; i < nblocks; i++) {
                if (fwrite(zbuf, 1, BTRON_BLOCK_SIZE, fp) != BTRON_BLOCK_SIZE) {
                    fclose(fp);
                    return NULL;
                }
            }
        }
        fflush(fp);
        rewind(fp);
        derived_nblocks = nblocks;
    } else {
        /* Open existing file */
        fp = fopen(path, "r+b");
        if (!fp) return NULL;
        /* Derive nblocks from file size */
        if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
        long fsize = ftell(fp);
        if (fsize <= 0) { fclose(fp); return NULL; }
        derived_nblocks = (UW)((unsigned long)fsize / BTRON_BLOCK_SIZE);
        rewind(fp);
    }

    BlkDev  *dev = (BlkDev  *)malloc(sizeof(BlkDev));
    FileCtx *ctx = (FileCtx *)malloc(sizeof(FileCtx));
    if (!dev || !ctx) {
        free(dev); free(ctx); fclose(fp);
        return NULL;
    }

    ctx->fp         = fp;
    dev->read       = file_read;
    dev->write      = file_write;
    dev->block_size = BTRON_BLOCK_SIZE;
    dev->nblocks    = derived_nblocks;
    dev->ctx        = ctx;

    return dev;
}

/* Override the generic blk_destroy defined in blk_mem.c for file backend */
/* (blk_destroy is declared in block.h and defined once in blk_mem.c;
 *  the file backend's ctx is freed there.  We just close the FILE* here
 *  by storing it in the ctx which blk_mem.c's blk_destroy free()s.) */
/* Actually blk_destroy in blk_mem.c calls free(dev->ctx) — the FileCtx
 * must be freed.  We close fp before free by hooking the destroy path.
 * Simplest: provide our own destructor that clients can call explicitly,
 * or just use blk_file_close() as an alias. */
void blk_file_close(BlkDev *dev)
{
    if (!dev) return;
    FileCtx *ctx = (FileCtx *)dev->ctx;
    if (ctx) {
        file_close_ctx(ctx);
        free(ctx);
    }
    free(dev);
}

#else
/* Bare-metal builds don't need the file backend */
void blk_file_close(void *dev) { (void)dev; }
#endif /* __STDC_HOSTED__ */
