/*
 * B-System BTRON3 Filesystem — vol.c
 * Volume format, mount, umount, sync, and low-level block/FID helpers.
 *
 * On-disk layout (per FS.md §3):
 *   Block 0          : VolumeHeader (first 128 bytes) + FID table start
 *   Block 0..sfidt-1 : FID table  (4 bytes per FID)
 *   Block sfidt..sfidt+sfnmt-1 : short-name hash table (4 bytes per FID)
 *   Block sfidt+sfnmt..+nbmp-1 : used-block bitmap (1 bit per block)
 *   Block ..+nbmp               : bad-block bitmap
 *   Block data_start+           : file data region (Real Bodies)
 *
 * In RAM, the Volume struct caches:
 *   - the VolumeHeader
 *   - the full FID table  (fid_tbl[nfmax])
 *   - the full used-block bitmap (ubmp[nbmp_bytes])
 *   - the full short-name hash table (htbl[nfmax])
 *
 * Big-endian fields are written/read with explicit byte swapping.
 * The host always works in native endian and converts at the block boundary.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  include <stdlib.h>
#  include <string.h>
#  include <time.h>
#else
   extern void *Imalloc(size_t);
   extern void *Icalloc(size_t, size_t);
   extern void  Ifree(void *);
   extern void *tkl_memcpy(void *, const void *, size_t);
   extern void *tkl_memset(void *, int, size_t);
   extern int   tkl_strcmp(const char *, const char *);
   extern int   tkl_strncmp(const char *, const char *, size_t);
   extern size_t tkl_strlen(const char *);
   extern char *tkl_strncpy(char *, const char *, size_t);
#  define malloc   Imalloc
#  define calloc   Icalloc
#  define free     Ifree
#  define memcpy   tkl_memcpy
#  define memset   tkl_memset
#  define strcmp   tkl_strcmp
#  define strncmp  tkl_strncmp
#  define strlen   tkl_strlen
#  define strncpy  tkl_strncpy
#endif

#include <btron/fs/vol_api.h>
#include <btron/fs/volume.h>

/* ── Endian-independent big-endian byte helpers ─────────────────── */
static inline UH rd_u16_be(const unsigned char *p) {
    return (UH)(((UH)p[0] << 8) | (UH)p[1]);
}
static inline UW rd_u32_be(const unsigned char *p) {
    return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | (UW)p[3];
}
static inline void wr_u16_be(unsigned char *p, UH val) {
    p[0] = (unsigned char)((val >> 8) & 0xFF);
    p[1] = (unsigned char)(val & 0xFF);
}
static inline void wr_u32_be(unsigned char *p, UW val) {
    p[0] = (unsigned char)((val >> 24) & 0xFF);
    p[1] = (unsigned char)((val >> 16) & 0xFF);
    p[2] = (unsigned char)((val >> 8) & 0xFF);
    p[3] = (unsigned char)(val & 0xFF);
}

/* ── Volume struct ──────────────────────────────────────────────── */
struct Volume {
    BlkDev      *dev;
    VolumeHeader hdr;           /* cached, host-endian shadow */
    UW          *fid_tbl;       /* [nfmax] raw 4-byte entries (native endian) */
    UW          *htbl;          /* [nfmax] short-name hashes (native endian)  */
    unsigned char *ubmp;        /* used-block bitmap (1 bit per block)       */
    UW           ubmp_bytes;    /* byte size of ubmp                          */
    int          dirty;
};

/* ── Globals ────────────────────────────────────────────────────── */
Volume *g_sys_vol = (Volume *)0;
char    g_cwd_path[128] = "/SYS";

/* ── Internal block I/O ─────────────────────────────────────────── */
int vol_read_blk(Volume *v, BLK lba, void *buf)
{
    return v->dev->read(v->dev, lba, buf, 1);
}

int vol_write_blk(Volume *v, BLK lba, const void *buf)
{
    if (!v->dev->write) return -2; /* read-only */
    return v->dev->write(v->dev, lba, buf, 1);
}

void vol_mark_dirty(Volume *v) { if (v) v->dirty = 1; }

/* ── Name hash (FS.md §3.3) ─────────────────────────────────────── */
UW vol_name_hash(const char *name)
{
    UW words[10];
    unsigned int i;
    memset(words, 0, sizeof(words));
    {
        const unsigned char *p = (const unsigned char *)name;
        unsigned int bi = 0;
        while (*p && bi < 40) {
            unsigned int wi = bi / 4;
            unsigned int shift = 24 - (bi % 4) * 8;
            if (wi < 10)
                words[wi] |= ((UW)*p) << shift;
            p++;
            bi++;
        }
    }
    UW hash = 0;
    for (i = 0; i < 10; i++) {
        hash ^= words[i];
        /* rotate right 1 bit */
        hash = (hash >> 1) | (hash << 31);
    }
    return hash;
}

/* ── Block allocation ───────────────────────────────────────────── */
BLK vol_alloc_block(Volume *v)
{
    UW data_start = v->hdr.data_start;
    UW nlb        = v->hdr.nlb;
    for (UW b = data_start; b < nlb; b++) {
        unsigned int byte = b >> 3;
        unsigned int bit  = b & 7;
        if (byte < v->ubmp_bytes && !(v->ubmp[byte] & (1u << bit))) {
            v->ubmp[byte] |= (unsigned char)(1u << bit);
            if (v->hdr.free_blocks > 0)
                v->hdr.free_blocks--;
            v->dirty = 1;
            return b;
        }
    }
    return FID_INVALID; /* no free block */
}

void vol_free_block(Volume *v, BLK blk)
{
    if (!v || blk == FID_INVALID || blk >= v->hdr.nlb) return;
    unsigned int byte = blk >> 3;
    unsigned int bit  = blk & 7;
    if (byte < v->ubmp_bytes) {
        if (v->ubmp[byte] & (1u << bit)) {
            v->ubmp[byte] &= (unsigned char)~(1u << bit);
            v->hdr.free_blocks++;
            v->dirty = 1;
        }
    }
}

/* ── FID table helpers ──────────────────────────────────────────── */
/*
 * FID table entry (4 bytes):
 *   bits[31:8]  = 24-bit start block address of FileHeader block
 *   bits[ 7:0]  = reference count (0 = unused FID)
 */
FID vol_fid_alloc(Volume *v)
{
    UW nfmax = v->hdr.nfmax;
    for (UW i = 1; i < nfmax; i++) {   /* FID 0 reserved for root */
        if (v->fid_tbl[i] == 0)
            return (FID)i;
    }
    return FID_INVALID;
}

void vol_fid_free(Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return;
    v->fid_tbl[fid] = 0;
    v->htbl[fid]    = 0;
    v->dirty = 1;
}

BLK vol_fid_get_blk(const Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return (BLK)FID_INVALID;
    return (v->fid_tbl[fid] >> 8) & 0x00FFFFFFu;
}

void vol_fid_set(Volume *v, FID fid, BLK blk, UB refcount)
{
    if (fid >= v->hdr.nfmax) return;
    v->fid_tbl[fid] = ((blk & 0x00FFFFFFu) << 8) | (UW)refcount;
    v->dirty = 1;
}

UB vol_fid_refcount(const Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return 0;
    return (UB)(v->fid_tbl[fid] & 0xFF);
}

void vol_fid_ref_inc(Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return;
    UB rc = vol_fid_refcount(v, fid);
    if (rc < 255) vol_fid_set(v, fid, vol_fid_get_blk(v, fid), (UB)(rc + 1));
}

void vol_fid_ref_dec(Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return;
    UB rc = vol_fid_refcount(v, fid);
    if (rc > 0) vol_fid_set(v, fid, vol_fid_get_blk(v, fid), (UB)(rc - 1));
}

/* ── Hash table helpers ─────────────────────────────────────────── */
void vol_hash_set(Volume *v, FID fid, UW hash)
{
    if (fid >= v->hdr.nfmax) return;
    v->htbl[fid] = hash;
    v->dirty = 1;
}

UW vol_hash_get(const Volume *v, FID fid)
{
    if (fid >= v->hdr.nfmax) return 0;
    return v->htbl[fid];
}

/* ── Statistics ─────────────────────────────────────────────────── */
UW vol_free_blocks (const Volume *v) { return v ? v->hdr.free_blocks : 0; }
UW vol_total_blocks(const Volume *v) { return v ? v->hdr.nlb         : 0; }
UW vol_nfmax       (const Volume *v) { return v ? v->hdr.nfmax       : 0; }
const char *vol_name(const Volume *v)
{
    if (!v) return "";
    return (const char *)v->hdr.vol_name;
}

/* ── Flush helper: write FID table back to disk ─────────────────── */
static void flush_fid_table(Volume *v)
{
    unsigned char blk_buf[BTRON_BLOCK_SIZE];
    UW entries_per_blk = BTRON_BLOCK_SIZE / 4;
    UW fid_blk_start   = 1; /* block 0 is the header; FID table starts at 1 */
    UW sfidt = v->hdr.sfidt;
    for (UW b = 0; b < sfidt; b++) {
        UW base = b * entries_per_blk;
        memset(blk_buf, 0, sizeof(blk_buf));
        for (UW i = 0; i < entries_per_blk && base + i < v->hdr.nfmax; i++) {
            wr_u32_be(&blk_buf[i * 4], v->fid_tbl[base + i]);
        }
        vol_write_blk(v, fid_blk_start + b, blk_buf);
    }
}

static void flush_hash_table(Volume *v)
{
    unsigned char blk_buf[BTRON_BLOCK_SIZE];
    UW entries_per_blk = BTRON_BLOCK_SIZE / 4;
    UW htbl_start = 1 + v->hdr.sfidt;
    for (UW b = 0; b < v->hdr.sfnmt; b++) {
        UW base = b * entries_per_blk;
        memset(blk_buf, 0, sizeof(blk_buf));
        for (UW i = 0; i < entries_per_blk && base + i < v->hdr.nfmax; i++) {
            wr_u32_be(&blk_buf[i * 4], v->htbl[base + i]);
        }
        vol_write_blk(v, htbl_start + b, blk_buf);
    }
}

static void flush_bitmap(Volume *v)
{
    unsigned char blk_buf[BTRON_BLOCK_SIZE];
    UW bmp_start = 1 + v->hdr.sfidt + v->hdr.sfnmt;
    UW nbmp = v->hdr.nbmp;
    for (UW b = 0; b < nbmp; b++) {
        UW base_byte = b * BTRON_BLOCK_SIZE;
        memset(blk_buf, 0, sizeof(blk_buf));
        for (UW i = 0; i < BTRON_BLOCK_SIZE && base_byte + i < v->ubmp_bytes; i++)
            blk_buf[i] = v->ubmp[base_byte + i];
        vol_write_blk(v, bmp_start + b, blk_buf);
    }
    /* Bad-block bitmap: all zeros (no bad blocks in clean volume) */
    memset(blk_buf, 0, sizeof(blk_buf));
    for (UW b = 0; b < nbmp; b++)
        vol_write_blk(v, bmp_start + nbmp + b, blk_buf);
}

static void flush_header(Volume *v)
{
    unsigned char blk_buf[BTRON_BLOCK_SIZE];
    memset(blk_buf, 0, sizeof(blk_buf));

    wr_u16_be(blk_buf +  0, v->hdr.magic);
    wr_u16_be(blk_buf +  2, v->hdr.fs_type);
    wr_u32_be(blk_buf +  4, v->hdr.nfmax);
    wr_u32_be(blk_buf +  8, v->hdr.nlb);
    wr_u16_be(blk_buf + 12, v->hdr.sfidt);
    wr_u16_be(blk_buf + 14, v->hdr.sfnmt);
    wr_u16_be(blk_buf + 16, v->hdr.nbmp);
    blk_buf[18] = v->hdr.access_level;
    blk_buf[19] = v->hdr.dirty;
    wr_u32_be(blk_buf + 20, v->hdr.free_blocks);
    wr_u32_be(blk_buf + 24, v->hdr.data_start);
    memcpy(blk_buf + 28, v->hdr.vol_name, 40);

    vol_write_blk(v, 0, blk_buf);
}

/* ── vol_sync ───────────────────────────────────────────────────── */
void vol_sync(Volume *v)
{
    if (!v || !v->dirty) return;
    flush_fid_table(v);
    flush_hash_table(v);
    flush_bitmap(v);
    flush_header(v);
    v->dirty = 0;
}

/* ── vol_format ─────────────────────────────────────────────────── */
int vol_format(BlkDev *dev, UW nfmax, UW nlb, const char *name)
{
    if (!dev || nfmax < 2 || nlb < 8) return -1;

    /* Compute system area sizes */
    UW entries_per_blk = BTRON_BLOCK_SIZE / 4;
    UW sfidt = (nfmax + entries_per_blk - 1) / entries_per_blk;
    UW sfnmt = sfidt;   /* same count, one hash per FID */
    UW ubmp_bits   = nlb;
    UW nbmp = (ubmp_bits + BTRON_BLOCK_SIZE * 8 - 1) / (BTRON_BLOCK_SIZE * 8);
    UW data_start = 1 + sfidt + sfnmt + nbmp + nbmp; /* hdr + FID + hash + 2 bmps */

    if (data_start >= nlb) return -2; /* volume too small */

    /* Build a Volume struct to use flush helpers */
    Volume v;
    memset(&v, 0, sizeof(v));
    v.dev = dev;

    v.hdr.magic        = VOL_MAGIC_BE;
    v.hdr.fs_type      = FS_TYPE_STD;
    v.hdr.nfmax        = nfmax;
    v.hdr.nlb          = nlb;
    v.hdr.sfidt        = (UH)sfidt;
    v.hdr.sfnmt        = (UH)sfnmt;
    v.hdr.nbmp         = (UH)nbmp;
    v.hdr.access_level = 0;
    v.hdr.dirty        = 0;
    v.hdr.data_start   = data_start;
    {
        const unsigned char *nm = (const unsigned char *)name;
        unsigned int i = 0;
        while (nm[i] && i < 39) { v.hdr.vol_name[i] = nm[i]; i++; }
        v.hdr.vol_name[i] = 0;
    }

    /* Allocate temp RAM for FID table, hash table, bitmap */
    v.fid_tbl   = (UW *)calloc(nfmax, sizeof(UW));
    v.htbl      = (UW *)calloc(nfmax, sizeof(UW));
    v.ubmp_bytes = (nlb + 7) / 8;
    v.ubmp      = (unsigned char *)calloc(v.ubmp_bytes, 1);
    if (!v.fid_tbl || !v.htbl || !v.ubmp) {
        free(v.fid_tbl); free(v.htbl); free(v.ubmp);
        return -3;
    }

    /* Mark system area blocks as used */
    for (UW b = 0; b < data_start; b++) {
        unsigned int byte = b >> 3;
        unsigned int bit  = b & 7;
        if (byte < v.ubmp_bytes)
            v.ubmp[byte] |= (unsigned char)(1u << bit);
    }
    v.hdr.free_blocks = nlb - data_start;

    /* Create FID 0 = root (a normal file) */
    BLK root_blk = vol_alloc_block(&v);
    if (root_blk == FID_INVALID) {
        free(v.fid_tbl); free(v.htbl); free(v.ubmp);
        return -4;
    }
    vol_fid_set(&v, FID_ROOT, root_blk, 1);
    vol_hash_set(&v, FID_ROOT, vol_name_hash(name));

    /* Write root FileHeader block */
    {
        unsigned char blk_buf[BTRON_BLOCK_SIZE];
        memset(blk_buf, 0, sizeof(blk_buf));
        UH flags = (UH)((FTYPE_NORMAL << 12) | FFLG_READ | FFLG_WRITE);
        wr_u16_be(blk_buf +  0, flags);
        wr_u16_be(blk_buf +  2, 0);     /* atype */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        UW ts = (UW)(time(NULL) - 946684800UL); /* secs since 2000-01-01 */
#else
        UW ts = 0;
#endif
        wr_u32_be(blk_buf +  4, ts);    /* ctime */
        wr_u32_be(blk_buf +  8, ts);    /* mtime */
        wr_u32_be(blk_buf + 12, ts);    /* atime */
        wr_u16_be(blk_buf + 16, 0);     /* owner */
        wr_u16_be(blk_buf + 18, 0);     /* group */
        wr_u16_be(blk_buf + 20, 1);     /* nlnk */
        wr_u16_be(blk_buf + 22, 0);     /* idxlv */
        wr_u32_be(blk_buf + 24, 0);     /* nrec */
        wr_u32_be(blk_buf + 28, 0);     /* total_size */
        /* name[40] at +32 */
        {
            const unsigned char *nm = (const unsigned char *)name;
            unsigned int i = 0;
            while (nm[i] && i < 39) { blk_buf[32 + i] = nm[i]; i++; }
        }
        wr_u32_be(blk_buf + 72, 0);     /* data_blk */
        dev->write(dev, root_blk, blk_buf, 1);
    }

    /* Flush everything */
    v.dirty = 1;
    flush_fid_table(&v);
    flush_hash_table(&v);
    flush_bitmap(&v);
    v.hdr.dirty = 0;
    flush_header(&v);

    free(v.fid_tbl);
    free(v.htbl);
    free(v.ubmp);
    return 0;
}

/* ── vol_mount ──────────────────────────────────────────────────── */
Volume *vol_mount(BlkDev *dev)
{
    if (!dev) return (Volume *)0;

    unsigned char blk_buf[BTRON_BLOCK_SIZE];
    if (dev->read(dev, 0, blk_buf, 1) != 0)
        return (Volume *)0;

    /* Read and validate header */
    UH magic = rd_u16_be(blk_buf + 0);
    if (magic != VOL_MAGIC_BE && magic != VOL_MAGIC_LE)
        return (Volume *)0;

    Volume *v = (Volume *)calloc(1, sizeof(Volume));
    if (!v) return (Volume *)0;

    v->dev = dev;
    /* Load header into native-endian shadow */
    v->hdr.magic        = magic;
    v->hdr.fs_type      = rd_u16_be(blk_buf +  2);
    v->hdr.nfmax        = rd_u32_be(blk_buf +  4);
    v->hdr.nlb          = rd_u32_be(blk_buf +  8);
    v->hdr.sfidt        = rd_u16_be(blk_buf + 12);
    v->hdr.sfnmt        = rd_u16_be(blk_buf + 14);
    v->hdr.nbmp         = rd_u16_be(blk_buf + 16);
    v->hdr.access_level = blk_buf[18];
    v->hdr.dirty        = 1; /* mark as mounted */
    v->hdr.free_blocks  = rd_u32_be(blk_buf + 20);
    v->hdr.data_start   = rd_u32_be(blk_buf + 24);
    memcpy(v->hdr.vol_name, blk_buf + 28, 40);

    UW nfmax = v->hdr.nfmax;
    UW sfidt = v->hdr.sfidt;
    UW sfnmt = v->hdr.sfnmt;
    UW nbmp  = v->hdr.nbmp;

    /* Allocate RAM caches */
    v->fid_tbl   = (UW *)calloc(nfmax, sizeof(UW));
    v->htbl      = (UW *)calloc(nfmax, sizeof(UW));
    v->ubmp_bytes = (v->hdr.nlb + 7) / 8;
    v->ubmp      = (unsigned char *)calloc(v->ubmp_bytes, 1);
    if (!v->fid_tbl || !v->htbl || !v->ubmp) {
        free(v->fid_tbl); free(v->htbl); free(v->ubmp); free(v);
        return (Volume *)0;
    }

    /* Load FID table */
    {
        UW entries_per_blk = BTRON_BLOCK_SIZE / 4;
        for (UW b = 0; b < sfidt; b++) {
            if (dev->read(dev, 1 + b, blk_buf, 1) != 0) break;
            UW base = b * entries_per_blk;
            for (UW i = 0; i < entries_per_blk && base + i < nfmax; i++) {
                v->fid_tbl[base + i] = rd_u32_be(&blk_buf[i * 4]);
            }
        }
    }

    /* Load hash table */
    {
        UW entries_per_blk = BTRON_BLOCK_SIZE / 4;
        UW htbl_start = 1 + sfidt;
        for (UW b = 0; b < sfnmt; b++) {
            if (dev->read(dev, htbl_start + b, blk_buf, 1) != 0) break;
            UW base = b * entries_per_blk;
            for (UW i = 0; i < entries_per_blk && base + i < nfmax; i++) {
                v->htbl[base + i] = rd_u32_be(&blk_buf[i * 4]);
            }
        }
    }

    /* Load used-block bitmap */
    {
        UW bmp_start = 1 + sfidt + sfnmt;
        for (UW b = 0; b < nbmp; b++) {
            if (dev->read(dev, bmp_start + b, blk_buf, 1) != 0) break;
            UW base_byte = b * BTRON_BLOCK_SIZE;
            for (UW i = 0; i < BTRON_BLOCK_SIZE && base_byte + i < v->ubmp_bytes; i++)
                v->ubmp[base_byte + i] = blk_buf[i];
        }
    }

    /* Mark dirty=1 in header on disk */
    flush_header(v);

    v->dirty = 0; /* we just synced the dirty flag; rest is clean */
    return v;
}

/* ── vol_umount ─────────────────────────────────────────────────── */
void vol_umount(Volume *v)
{
    if (!v) return;
    v->dirty = 1;
    vol_sync(v);
    v->hdr.dirty = 0;
    flush_header(v);
    free(v->fid_tbl);
    free(v->htbl);
    free(v->ubmp);
    free(v);
}
