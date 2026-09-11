/*
 * B-System BTRON3 Filesystem — blk_part.c
 * MBR partition scanner & block device partition slicer for B-System.
 *
 * Scans MBR for partition type 0x13 (Personal Media BTRON / B-right/V),
 * accounts for the 8-sector IPL/bootloader area, and wraps the underlying
 * device into a partition BlkDev with the appropriate filesystem block size.
 */

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1

#include <btron/fs/block.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    BlkDev *parent;
    UW      start_lba; /* start in parent block units (sectors) */
    UW      sectors_per_block;
} PartCtx;

static int part_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (lba + count > dev->nblocks) return -1;
    BLK parent_lba = ctx->start_lba + lba * ctx->sectors_per_block;
    UW  parent_count = count * ctx->sectors_per_block;
    return ctx->parent->read(ctx->parent, parent_lba, buf, parent_count);
}

static int part_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    PartCtx *ctx = (PartCtx *)dev->ctx;
    if (!ctx->parent->write) return -1;
    if (lba + count > dev->nblocks) return -1;
    BLK parent_lba = ctx->start_lba + lba * ctx->sectors_per_block;
    UW  parent_count = count * ctx->sectors_per_block;
    return ctx->parent->write(ctx->parent, parent_lba, buf, parent_count);
}

BlkDev *blk_partition_create(BlkDev *parent, UW start_lba, UW nblocks, UW block_size)
{
    if (!parent || block_size < parent->block_size || (block_size % parent->block_size) != 0)
        return NULL;

    PartCtx *ctx = (PartCtx *)malloc(sizeof(PartCtx));
    BlkDev  *dev = (BlkDev  *)malloc(sizeof(BlkDev));
    if (!ctx || !dev) {
        free(ctx); free(dev);
        return NULL;
    }

    ctx->parent            = parent;
    ctx->start_lba         = start_lba;
    ctx->sectors_per_block = block_size / parent->block_size;

    dev->read       = part_read;
    dev->write      = parent->write ? part_write : NULL;
    dev->block_size = block_size;
    dev->nblocks    = nblocks;
    dev->ctx        = ctx;

    return dev;
}

BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size)
{
    if (!dev || dev->block_size != 512) return NULL;

    unsigned char sec0[512];
    if (dev->read(dev, 0, sec0, 1) != 0) return NULL;

    /* Verify MBR signature 0x55 0xAA */
    if (sec0[510] != 0x55 || sec0[511] != 0xAA) return NULL;

    /* Scan 4 partition entries at offset 446 (0x1BE) */
    for (int i = 0; i < 4; i++) {
        const unsigned char *ent = sec0 + 446 + i * 16;
        uint8_t  part_type = ent[4];
        uint32_t start_lba = (uint32_t)ent[8] |
                            ((uint32_t)ent[9]  <<  8) |
                            ((uint32_t)ent[10] << 16) |
                            ((uint32_t)ent[11] << 24);
        uint32_t total_sec = (uint32_t)ent[12] |
                            ((uint32_t)ent[13] <<  8) |
                            ((uint32_t)ent[14] << 16) |
                            ((uint32_t)ent[15] << 24);

        if (part_type == 0x13 && total_sec > 8) { /* 0x13 = BTRON partition */
            /*
             * In B-right/V 4.0 hard disks, Partition 1 begins with an 8-sector
             * (4096-byte) IPL boot area. The filesystem volume header starts
             * at partition relative sector 8 (absolute LBA start_lba + 8).
             */
            uint32_t fs_start_lba = start_lba + 8;
            uint32_t fs_sectors   = total_sec - 8;
            if (fs_block_size == 0) fs_block_size = 8192;
            uint32_t spb = fs_block_size / 512;
            uint32_t nblocks = fs_sectors / spb;

            return blk_partition_create(dev, fs_start_lba, nblocks, fs_block_size);
        }
    }

    return NULL;
}

#else
BlkDev *blk_partition_create(BlkDev *parent, UW start_lba, UW nblocks, UW block_size) {
    (void)parent; (void)start_lba; (void)nblocks; (void)block_size; return NULL;
}
BlkDev *blk_mbr_find_btron_partition(BlkDev *dev, UW fs_block_size) {
    (void)dev; (void)fs_block_size; return NULL;
}
#endif /* __STDC_HOSTED__ */
