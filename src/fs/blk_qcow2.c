/*
 * B-System BTRON3 Filesystem — blk_qcow2.c
 * Portable QEMU QCOW2 (v2/v3) block device backend for B-System.
 *
 * Provides sector-level (512-byte) read and write access to QCOW2 images
 * without external dependencies. Supports standard uncompressed clusters
 * as used by authentic B-right/V 4.0 images.
 */

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1

#include <btron/fs/block.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define QCOW_MAGIC 0x514649fbU /* "QFI\xfb" */

typedef struct {
    FILE     *fp;
    uint32_t  version;
    uint32_t  cluster_bits;
    uint32_t  cluster_size;
    uint32_t  l2_bits;
    uint32_t  l2_entries;
    uint64_t  disk_size;
    uint32_t  l1_size;
    uint64_t  l1_table_offset;
    uint64_t *l1_table;
    int       read_only;

    /* Simple 1-entry L2 table cache */
    uint64_t  cached_l2_offset;
    uint64_t *cached_l2_table;
} Qcow2Ctx;

static inline uint32_t be32(uint32_t x) {
    return ((x << 24) & 0xFF000000U) |
           ((x <<  8) & 0x00FF0000U) |
           ((x >>  8) & 0x0000FF00U) |
           ((x >> 24) & 0x000000FFU);
}

static inline uint64_t be64(uint64_t x) {
    return ((uint64_t)be32((uint32_t)(x & 0xFFFFFFFFU)) << 32) |
           (uint64_t)be32((uint32_t)(x >> 32));
}

static int qcow2_load_l2(Qcow2Ctx *ctx, uint64_t l2_offset)
{
    if (ctx->cached_l2_offset == l2_offset && ctx->cached_l2_table)
        return 0;

    if (!ctx->cached_l2_table) {
        ctx->cached_l2_table = (uint64_t *)malloc(ctx->l2_entries * sizeof(uint64_t));
        if (!ctx->cached_l2_table) return -1;
    }

    if (fseek(ctx->fp, (long)l2_offset, SEEK_SET) != 0)
        return -1;

    size_t read_bytes = ctx->l2_entries * sizeof(uint64_t);
    if (fread(ctx->cached_l2_table, 1, read_bytes, ctx->fp) != read_bytes)
        return -1;

    for (uint32_t i = 0; i < ctx->l2_entries; i++) {
        ctx->cached_l2_table[i] = be64(ctx->cached_l2_table[i]);
    }

    ctx->cached_l2_offset = l2_offset;
    return 0;
}

static int qcow2_read_sector(Qcow2Ctx *ctx, uint64_t sec, void *buf)
{
    uint64_t byte_off = sec * 512ULL;
    if (byte_off >= ctx->disk_size) {
        memset(buf, 0, 512);
        return 0;
    }

    uint64_t cluster_idx = byte_off >> ctx->cluster_bits;
    uint32_t cluster_off = (uint32_t)(byte_off & (ctx->cluster_size - 1));
    uint32_t l1_idx      = (uint32_t)(cluster_idx >> ctx->l2_bits);
    uint32_t l2_idx      = (uint32_t)(cluster_idx & (ctx->l2_entries - 1));

    if (l1_idx >= ctx->l1_size) {
        memset(buf, 0, 512);
        return 0;
    }

    uint64_t l2_offset = ctx->l1_table[l1_idx] & ~0x8000000000000000ULL;
    if (l2_offset == 0) {
        memset(buf, 0, 512);
        return 0;
    }

    if (qcow2_load_l2(ctx, l2_offset) != 0) {
        memset(buf, 0, 512);
        return -1;
    }

    uint64_t entry = ctx->cached_l2_table[l2_idx];
    uint64_t cluster_data = entry & 0x00FFFFFFFFFFFFFFULL & ~3ULL;
    if (cluster_data == 0) {
        memset(buf, 0, 512);
        return 0;
    }

    if (fseek(ctx->fp, (long)(cluster_data + cluster_off), SEEK_SET) != 0)
        return -1;

    if (fread(buf, 1, 512, ctx->fp) != 512)
        return -1;

    return 0;
}

static int qcow2_write_sector(Qcow2Ctx *ctx, uint64_t sec, const void *buf)
{
    if (ctx->read_only) return -1;
    uint64_t byte_off = sec * 512ULL;
    if (byte_off >= ctx->disk_size) return -1;

    uint64_t cluster_idx = byte_off >> ctx->cluster_bits;
    uint32_t cluster_off = (uint32_t)(byte_off & (ctx->cluster_size - 1));
    uint32_t l1_idx      = (uint32_t)(cluster_idx >> ctx->l2_bits);
    uint32_t l2_idx      = (uint32_t)(cluster_idx & (ctx->l2_entries - 1));

    if (l1_idx >= ctx->l1_size) return -1;

    uint64_t l2_offset = ctx->l1_table[l1_idx] & ~0x8000000000000000ULL;
    if (l2_offset == 0) {
        /* Allocate new L2 table at end of file */
        if (fseek(ctx->fp, 0, SEEK_END) != 0) return -1;
        long fsz = ftell(ctx->fp);
        fsz = (fsz + (long)ctx->cluster_size - 1) & ~((long)ctx->cluster_size - 1);
        l2_offset = (uint64_t)fsz;

        /* Zero new L2 table */
        uint64_t *zero_l2 = (uint64_t *)calloc(ctx->l2_entries, sizeof(uint64_t));
        if (!zero_l2) return -1;
        if (fseek(ctx->fp, (long)l2_offset, SEEK_SET) != 0 ||
            fwrite(zero_l2, sizeof(uint64_t), ctx->l2_entries, ctx->fp) != ctx->l2_entries) {
            free(zero_l2);
            return -1;
        }
        free(zero_l2);

        /* Update L1 table in memory and on disk */
        ctx->l1_table[l1_idx] = l2_offset | 0x8000000000000000ULL;
        uint64_t l1_disk = be64(ctx->l1_table[l1_idx]);
        if (fseek(ctx->fp, (long)(ctx->l1_table_offset + l1_idx * 8), SEEK_SET) != 0 ||
            fwrite(&l1_disk, 1, 8, ctx->fp) != 8) {
            return -1;
        }
        fflush(ctx->fp);
    }

    if (qcow2_load_l2(ctx, l2_offset) != 0) return -1;

    uint64_t entry = ctx->cached_l2_table[l2_idx];
    uint64_t cluster_data = entry & 0x00FFFFFFFFFFFFFFULL & ~3ULL;

    if (cluster_data == 0) {
        /* Allocate a new cluster at end of file */
        if (fseek(ctx->fp, 0, SEEK_END) != 0) return -1;
        long fsz = ftell(ctx->fp);
        fsz = (fsz + (long)ctx->cluster_size - 1) & ~((long)ctx->cluster_size - 1);
        cluster_data = (uint64_t)fsz;

        /* Zero new cluster */
        void *zbuf = calloc(1, ctx->cluster_size);
        if (!zbuf) return -1;
        if (fseek(ctx->fp, (long)cluster_data, SEEK_SET) != 0 ||
            fwrite(zbuf, 1, ctx->cluster_size, ctx->fp) != ctx->cluster_size) {
            free(zbuf);
            return -1;
        }
        free(zbuf);

        /* Update L2 table entry in memory and on disk */
        entry = cluster_data | 0x8000000000000000ULL;
        ctx->cached_l2_table[l2_idx] = entry;
        uint64_t entry_disk = be64(entry);
        if (fseek(ctx->fp, (long)(l2_offset + l2_idx * 8), SEEK_SET) != 0 ||
            fwrite(&entry_disk, 1, 8, ctx->fp) != 8) {
            return -1;
        }
        fflush(ctx->fp);
    }

    /* Write 512-byte sector */
    if (fseek(ctx->fp, (long)(cluster_data + cluster_off), SEEK_SET) != 0)
        return -1;
    if (fwrite(buf, 1, 512, ctx->fp) != 512)
        return -1;
    fflush(ctx->fp);

    return 0;
}

static int qcow2_read(BlkDev *dev, BLK lba, void *buf, UW count)
{
    Qcow2Ctx *ctx = (Qcow2Ctx *)dev->ctx;
    unsigned char *dst = (unsigned char *)buf;
    for (UW i = 0; i < count; i++) {
        if (qcow2_read_sector(ctx, (uint64_t)lba + i, dst + i * 512) != 0)
            return -1;
    }
    return 0;
}

static int qcow2_write(BlkDev *dev, BLK lba, const void *buf, UW count)
{
    Qcow2Ctx *ctx = (Qcow2Ctx *)dev->ctx;
    const unsigned char *src = (const unsigned char *)buf;
    for (UW i = 0; i < count; i++) {
        if (qcow2_write_sector(ctx, (uint64_t)lba + i, src + i * 512) != 0)
            return -1;
    }
    return 0;
}

BlkDev *blk_qcow2_create(const char *path, int read_only)
{
    if (!path) return NULL;

    FILE *fp = fopen(path, read_only ? "rb" : "r+b");
    if (!fp && !read_only) {
        /* Fall back to read-only if file is not writable */
        fp = fopen(path, "rb");
        read_only = 1;
    }
    if (!fp) return NULL;

    unsigned char hdr[104];
    if (fread(hdr, 1, sizeof(hdr), fp) < 72) {
        fclose(fp);
        return NULL;
    }

    uint32_t magic = be32(*(uint32_t *)(hdr + 0));
    if (magic != QCOW_MAGIC) {
        fclose(fp);
        return NULL;
    }

    uint32_t version         = be32(*(uint32_t *)(hdr + 4));
    uint32_t cluster_bits    = be32(*(uint32_t *)(hdr + 20));
    uint64_t disk_size       = be64(*(uint64_t *)(hdr + 24));
    uint32_t l1_size         = be32(*(uint32_t *)(hdr + 36));
    uint64_t l1_table_offset = be64(*(uint64_t *)(hdr + 40));

    if (cluster_bits < 9 || cluster_bits > 24 || l1_size == 0) {
        fclose(fp);
        return NULL;
    }

    uint32_t cluster_size = 1U << cluster_bits;
    uint32_t l2_bits      = cluster_bits - 3;
    uint32_t l2_entries   = 1U << l2_bits;

    uint64_t *l1_table = (uint64_t *)malloc(l1_size * sizeof(uint64_t));
    if (!l1_table) {
        fclose(fp);
        return NULL;
    }

    if (fseek(fp, (long)l1_table_offset, SEEK_SET) != 0 ||
        fread(l1_table, sizeof(uint64_t), l1_size, fp) != l1_size) {
        free(l1_table);
        fclose(fp);
        return NULL;
    }

    for (uint32_t i = 0; i < l1_size; i++) {
        l1_table[i] = be64(l1_table[i]);
    }

    Qcow2Ctx *ctx = (Qcow2Ctx *)calloc(1, sizeof(Qcow2Ctx));
    BlkDev   *dev = (BlkDev   *)calloc(1, sizeof(BlkDev));
    if (!ctx || !dev) {
        free(l1_table); free(ctx); free(dev); fclose(fp);
        return NULL;
    }

    ctx->fp              = fp;
    ctx->version         = version;
    ctx->cluster_bits    = cluster_bits;
    ctx->cluster_size    = cluster_size;
    ctx->l2_bits         = l2_bits;
    ctx->l2_entries      = l2_entries;
    ctx->disk_size       = disk_size;
    ctx->l1_size         = l1_size;
    ctx->l1_table_offset = l1_table_offset;
    ctx->l1_table        = l1_table;
    ctx->read_only       = read_only;

    dev->read       = qcow2_read;
    dev->write      = read_only ? NULL : qcow2_write;
    dev->block_size = 512; /* sector level */
    dev->nblocks    = (UW)(disk_size / 512ULL);
    dev->ctx        = ctx;

    return dev;
}

void blk_qcow2_close(BlkDev *dev)
{
    if (!dev) return;
    Qcow2Ctx *ctx = (Qcow2Ctx *)dev->ctx;
    if (ctx) {
        if (ctx->fp) fclose(ctx->fp);
        if (ctx->l1_table) free(ctx->l1_table);
        if (ctx->cached_l2_table) free(ctx->cached_l2_table);
        free(ctx);
    }
    free(dev);
}

#else
BlkDev *blk_qcow2_create(const char *path, int read_only) { (void)path; (void)read_only; return NULL; }
void    blk_qcow2_close(BlkDev *dev) { (void)dev; }
#endif /* __STDC_HOSTED__ */
