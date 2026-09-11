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

/* ── Endian helpers ─────────────────────────────────────────────── */
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

static inline UH rd_u16(int is_le, const unsigned char *p) {
    if (is_le) return (UH)((UH)p[0] | ((UH)p[1] << 8));
    return rd_u16_be(p);
}
static inline UW rd_u32(int is_le, const unsigned char *p) {
    if (is_le) return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
    return rd_u32_be(p);
}
static inline void wr_u16(int is_le, unsigned char *p, UH val) {
    if (is_le) {
        p[0] = (unsigned char)(val & 0xFF);
        p[1] = (unsigned char)((val >> 8) & 0xFF);
    } else {
        wr_u16_be(p, val);
    }
}
static inline void wr_u32(int is_le, unsigned char *p, UW val) {
    if (is_le) {
        p[0] = (unsigned char)(val & 0xFF);
        p[1] = (unsigned char)((val >> 8) & 0xFF);
        p[2] = (unsigned char)((val >> 16) & 0xFF);
        p[3] = (unsigned char)((val >> 24) & 0xFF);
    } else {
        wr_u32_be(p, val);
    }
}

/* ── TRON Code <-> UTF-8 conversion helpers for B-right/V ──────── */
void btr_tcode_to_utf8(const UH *tc, int max_tcs, char *utf8, int max_bytes)
{
    if (!tc || !utf8 || max_bytes <= 0) return;
    int di = 0;
    for (int i = 0; i < max_tcs && tc[i] != 0 && di < max_bytes - 4; i++) {
        UH c = tc[i];
        if (c >= 0x2341 && c <= 0x235A) {
            utf8[di++] = (char)('A' + (c - 0x2341));
        } else if (c >= 0x2361 && c <= 0x237A) {
            utf8[di++] = (char)('a' + (c - 0x2361));
        } else if (c >= 0x2330 && c <= 0x2339) {
            utf8[di++] = (char)('0' + (c - 0x2330));
        } else if (c == 0x2121) {
            utf8[di++] = ' ';
        } else if (c == 0x215D) {
            utf8[di++] = '-';
        } else if (c == 0x213F) {
            utf8[di++] = '/';
        } else if (c == 0x212E) {
            utf8[di++] = '.';
        } else if (c == 0x2132 || c == 0x2170) {
            utf8[di++] = '_';
        } else if (c >= 0x20 && c <= 0x7E) {
            utf8[di++] = (char)c;
        } else {
            /* Decode Plane 1 / Multi-byte T-Code (Kana & CJK) */
            UW high = (c >> 8) & 0xFF;
            UW low  = c & 0xFF;
            UW cp = 0;
            if (high == 0x24 && low >= 0x21 && low <= 0x73) {
                cp = 0x3040 + (low - 0x20); /* Hiragana */
            } else if (high == 0x25 && low >= 0x21 && low <= 0x76) {
                cp = 0x30A0 + (low - 0x20); /* Katakana */
            } else if (high >= 0x30 && high <= 0x85) {
                UW offset = ((high - 0x30) << 8) | low;
                cp = 0x4E00 + offset;
                if (cp > 0x9FFF) cp = 0;
            }

            if (cp > 0 && cp <= 0x7FF) {
                utf8[di++] = (char)(0xC0 | ((cp >> 6) & 0x1F));
                utf8[di++] = (char)(0x80 | (cp & 0x3F));
            } else if (cp > 0x7FF && cp <= 0xFFFF) {
                utf8[di++] = (char)(0xE0 | ((cp >> 12) & 0x0F));
                utf8[di++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[di++] = (char)(0x80 | (cp & 0x3F));
            }
        }
    }
    utf8[di] = '\0';
}

void btr_utf8_to_tcode(const char *utf8, UH *tc, int max_tcs)
{
    if (!utf8 || !tc || max_tcs <= 0) return;
    int ti = 0;
    while (*utf8 && ti < max_tcs - 1) {
        unsigned char c = (unsigned char)*utf8++;
        if (c >= 'A' && c <= 'Z') {
            tc[ti++] = 0x2341 + (c - 'A');
        } else if (c >= 'a' && c <= 'z') {
            tc[ti++] = 0x2361 + (c - 'a');
        } else if (c >= '0' && c <= '9') {
            tc[ti++] = 0x2330 + (c - '0');
        } else if (c == ' ') {
            tc[ti++] = 0x2121;
        } else if (c == '-') {
            tc[ti++] = 0x215D;
        } else if (c == '/') {
            tc[ti++] = 0x213F;
        } else if (c == '.') {
            tc[ti++] = 0x212E;
        } else if (c == '_') {
            tc[ti++] = 0x2132;
        } else {
            tc[ti++] = c;
        }
    }
    while (ti < max_tcs) tc[ti++] = 0;
}

/* ── Volume struct ──────────────────────────────────────────────── */
struct Volume {
    BlkDev        *dev;
    VolumeHeader   hdr;           /* cached, host-endian shadow */
    UW            *fid_tbl;       /* [nfmax] raw 4-byte entries (native endian) */
    UW            *htbl;          /* [nfmax] short-name hashes (native endian)  */
    unsigned char *ubmp;          /* used-block bitmap (1 bit per block)       */
    UW             ubmp_bytes;    /* byte size of ubmp                          */
    int            dirty;
    int            is_le;         /* 1 = little-endian, 0 = big-endian          */
    int            is_brightv;    /* 1 = B-right/V 4.02 filesystem              */
    UW             block_size;    /* logical block size (e.g. 1024 or 8192)     */
    UW             bmp_start;     /* start block of bitmap                      */
    UW             fid_start;     /* start block of FID table                   */
    UW             htbl_start;    /* start block of short-name hash table       */
};

/* ── Globals ────────────────────────────────────────────────────── */
Volume *g_sys_vol = (Volume *)0;
Volume *g_anders_vol = (Volume *)0;
Volume *g_chokanji_vol = (Volume *)0;
char    g_cwd_path[128] = "/SYS";

UW vol_block_size(const Volume *v)
{
    return (v && v->block_size > 0) ? v->block_size : BTRON_BLOCK_SIZE;
}

int vol_is_brightv(const Volume *v)
{
    return v ? v->is_brightv : 0;
}


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
    UW bsize = vol_block_size(v);
    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return;
    UW entries_per_blk = bsize / 4;
    UW fid_blk_start   = v->fid_start;
    UW sfidt = v->hdr.sfidt;
    for (UW b = 0; b < sfidt; b++) {
        UW base = b * entries_per_blk;
        memset(blk_buf, 0, bsize);
        for (UW i = 0; i < entries_per_blk && base + i < v->hdr.nfmax; i++) {
            if (v->is_brightv) {
                BLK blk = (v->fid_tbl[base + i] >> 8) & 0x00FFFFFFu;
                UB rc   = (UB)(v->fid_tbl[base + i] & 0xFF);
                blk_buf[i * 4 + 0] = (UB)(blk & 0xFF);
                blk_buf[i * 4 + 1] = (UB)((blk >> 8) & 0xFF);
                blk_buf[i * 4 + 2] = (UB)((blk >> 16) & 0xFF);
                blk_buf[i * 4 + 3] = rc;
            } else {
                wr_u32_be(&blk_buf[i * 4], v->fid_tbl[base + i]);
            }
        }
        vol_write_blk(v, fid_blk_start + b, blk_buf);
    }
    free(blk_buf);
}

static void flush_hash_table(Volume *v)
{
    UW bsize = vol_block_size(v);
    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return;
    UW entries_per_blk = bsize / 4;
    UW htbl_start = v->htbl_start;
    for (UW b = 0; b < v->hdr.sfnmt; b++) {
        UW base = b * entries_per_blk;
        memset(blk_buf, 0, bsize);
        for (UW i = 0; i < entries_per_blk && base + i < v->hdr.nfmax; i++) {
            wr_u32(v->is_le, &blk_buf[i * 4], v->htbl[base + i]);
        }
        vol_write_blk(v, htbl_start + b, blk_buf);
    }
    free(blk_buf);
}

static void flush_bitmap(Volume *v)
{
    UW bsize = vol_block_size(v);
    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return;

    if (v->is_brightv) {
        /* Block 0: bytes 128..bsize-1 are bitmap */
        vol_read_blk(v, 0, blk_buf);
        UW b0_avail = bsize - 128;
        UW chunk = (v->ubmp_bytes < b0_avail) ? v->ubmp_bytes : b0_avail;
        memcpy(blk_buf + 128, v->ubmp, chunk);
        vol_write_blk(v, 0, blk_buf);

        /* Blocks 1 .. nbmp-1 */
        for (UW b = 1; b < v->hdr.nbmp; b++) {
            UW base_byte = b0_avail + (b - 1) * bsize;
            memset(blk_buf, 0, bsize);
            if (base_byte < v->ubmp_bytes) {
                UW rem = v->ubmp_bytes - base_byte;
                UW n = (rem < bsize) ? rem : bsize;
                memcpy(blk_buf, v->ubmp + base_byte, n);
            }
            vol_write_blk(v, b, blk_buf);
        }
    } else {
        UW bmp_start = v->bmp_start;
        UW nbmp = v->hdr.nbmp;
        for (UW b = 0; b < nbmp; b++) {
            UW base_byte = b * bsize;
            memset(blk_buf, 0, bsize);
            for (UW i = 0; i < bsize && base_byte + i < v->ubmp_bytes; i++)
                blk_buf[i] = v->ubmp[base_byte + i];
            vol_write_blk(v, bmp_start + b, blk_buf);
        }
        /* Bad-block bitmap: all zeros (no bad blocks in clean volume) */
        memset(blk_buf, 0, bsize);
        for (UW b = 0; b < nbmp; b++)
            vol_write_blk(v, bmp_start + nbmp + b, blk_buf);
    }
    free(blk_buf);
}

static void flush_header(Volume *v)
{
    UW bsize = vol_block_size(v);
    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return;

    if (v->is_brightv) {
        vol_read_blk(v, 0, blk_buf); /* preserve bitmap in 128..bsize */
        wr_u16(1, blk_buf +  0, v->hdr.magic);
        wr_u16(1, blk_buf +  2, v->hdr.fs_type);
        wr_u16(1, blk_buf +  4, v->hdr.nbmp);
        wr_u16(1, blk_buf +  6, v->hdr.sfidt);
        wr_u16(1, blk_buf +  8, v->hdr.sfnmt);
        wr_u16(1, blk_buf + 0x18, (UH)(v->block_size & 0xFFFF));
        wr_u32(1, blk_buf + 0x20, v->hdr.nlb);
        wr_u32(1, blk_buf + 0x24, v->hdr.free_blocks);
        UH tc_name[20];
        btr_utf8_to_tcode((const char *)v->hdr.vol_name, tc_name, 20);
        for (int i = 0; i < 20; i++) {
            wr_u16(1, blk_buf + 0x30 + i * 2, tc_name[i]);
        }
        vol_write_blk(v, 0, blk_buf);
    } else {
        memset(blk_buf, 0, bsize);
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
    free(blk_buf);
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
    v.bmp_start        = 1 + sfidt + sfnmt;
    v.fid_start        = 1;
    v.htbl_start       = 1 + sfidt;
    v.block_size       = BTRON_BLOCK_SIZE;
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

    UW initial_bsize = dev->block_size ? dev->block_size : BTRON_BLOCK_SIZE;
    if (initial_bsize < BTRON_BLOCK_SIZE) initial_bsize = BTRON_BLOCK_SIZE;

    unsigned char *blk_buf = (unsigned char *)malloc(initial_bsize);
    if (!blk_buf) return (Volume *)0;

    if (dev->read(dev, 0, blk_buf, 1) != 0) {
        free(blk_buf);
        return (Volume *)0;
    }

    /* Read and validate magic */
    UH m_be = rd_u16_be(blk_buf + 0);
    UH m_le = rd_u16(1, blk_buf + 0);
    int is_le = 0;
    if (m_be == VOL_MAGIC_BE) {
        is_le = 0;
    } else if (m_le == VOL_MAGIC_LE || m_le == VOL_MAGIC_BE) {
        is_le = 1;
    } else {
        free(blk_buf);
        return (Volume *)0;
    }

    UH magic = rd_u16(is_le, blk_buf + 0);
    UH fs_type = rd_u16(is_le, blk_buf + 2);
    int is_brightv = (fs_type == FS_TYPE_BRIGHTV);

    Volume *v = (Volume *)calloc(1, sizeof(Volume));
    if (!v) {
        free(blk_buf);
        return (Volume *)0;
    }

    v->dev        = dev;
    v->is_le      = is_le;
    v->is_brightv = is_brightv;

    if (is_brightv) {
        v->block_size       = rd_u16(1, blk_buf + 0x18);
        if (v->block_size == 0) v->block_size = 8192;
        v->hdr.magic        = magic;
        v->hdr.fs_type      = fs_type;
        v->hdr.nbmp         = rd_u16(1, blk_buf + 0x04);
        v->hdr.sfidt        = rd_u16(1, blk_buf + 0x06);
        v->hdr.sfnmt        = rd_u16(1, blk_buf + 0x08);
        v->hdr.nlb          = rd_u32(1, blk_buf + 0x20);
        v->hdr.free_blocks  = rd_u32(1, blk_buf + 0x24);
        v->hdr.nfmax        = (UW)v->hdr.sfidt * (v->block_size / 4);
        v->hdr.access_level = 0;
        v->hdr.dirty        = 1;
        v->bmp_start        = 0;
        v->fid_start        = v->hdr.nbmp;
        v->htbl_start       = v->fid_start + v->hdr.sfidt;
        v->hdr.data_start   = v->htbl_start + v->hdr.sfnmt;

        /* Decode fs_name at 0x30 */
        UH tc[20];
        for (int k = 0; k < 20; k++) {
            tc[k] = rd_u16(1, blk_buf + 0x30 + k * 2);
        }
        btr_tcode_to_utf8(tc, 20, (char *)v->hdr.vol_name, sizeof(v->hdr.vol_name));
        if (v->hdr.vol_name[0] == '\0') {
            strncpy((char *)v->hdr.vol_name, "B-right/V", sizeof(v->hdr.vol_name) - 1);
            v->hdr.vol_name[sizeof(v->hdr.vol_name) - 1] = '\0';
        }
    } else {
        v->block_size       = dev->block_size ? dev->block_size : BTRON_BLOCK_SIZE;
        v->hdr.magic        = magic;
        v->hdr.fs_type      = fs_type;
        v->hdr.nfmax        = rd_u32(is_le, blk_buf +  4);
        v->hdr.nlb          = rd_u32(is_le, blk_buf +  8);
        v->hdr.sfidt        = rd_u16(is_le, blk_buf + 12);
        v->hdr.sfnmt        = rd_u16(is_le, blk_buf + 14);
        v->hdr.nbmp         = rd_u16(is_le, blk_buf + 16);
        v->hdr.access_level = blk_buf[18];
        v->hdr.dirty        = 1;
        v->hdr.free_blocks  = rd_u32(is_le, blk_buf + 20);
        v->hdr.data_start   = rd_u32(is_le, blk_buf + 24);
        memcpy(v->hdr.vol_name, blk_buf + 28, 40);
        v->bmp_start        = 1 + v->hdr.sfidt + v->hdr.sfnmt;
        v->fid_start        = 1;
        v->htbl_start       = 1 + v->hdr.sfidt;
    }

    /* Resize blk_buf if needed */
    UW bsize = vol_block_size(v);
    if (bsize != initial_bsize) {
        free(blk_buf);
        blk_buf = (unsigned char *)malloc(bsize);
        if (!blk_buf) {
            free(v);
            return (Volume *)0;
        }
    }

    UW nfmax = v->hdr.nfmax;
    UW sfidt = v->hdr.sfidt;
    UW sfnmt = v->hdr.sfnmt;
    UW nbmp  = v->hdr.nbmp;

    /* Allocate RAM caches */
    v->fid_tbl    = (UW *)calloc(nfmax, sizeof(UW));
    v->htbl       = (UW *)calloc(nfmax, sizeof(UW));
    v->ubmp_bytes = (v->hdr.nlb + 7) / 8;
    v->ubmp       = (unsigned char *)calloc(v->ubmp_bytes, 1);
    if (!v->fid_tbl || !v->htbl || !v->ubmp) {
        free(v->fid_tbl); free(v->htbl); free(v->ubmp); free(v); free(blk_buf);
        return (Volume *)0;
    }

    /* Load FID table */
    {
        UW entries_per_blk = bsize / 4;
        for (UW b = 0; b < sfidt; b++) {
            if (dev->read(dev, v->fid_start + b, blk_buf, 1) != 0) break;
            UW base = b * entries_per_blk;
            for (UW i = 0; i < entries_per_blk && base + i < nfmax; i++) {
                if (is_brightv) {
                    UB b0 = blk_buf[i * 4 + 0];
                    UB b1 = blk_buf[i * 4 + 1];
                    UB b2 = blk_buf[i * 4 + 2];
                    UB rc = blk_buf[i * 4 + 3];
                    BLK fblk = (BLK)(b0 | (b1 << 8) | (b2 << 16));
                    v->fid_tbl[base + i] = ((fblk & 0x00FFFFFFu) << 8) | (UW)rc;
                } else {
                    v->fid_tbl[base + i] = rd_u32_be(&blk_buf[i * 4]);
                }
            }
        }
    }

    /* Load hash table */
    {
        UW entries_per_blk = bsize / 4;
        for (UW b = 0; b < sfnmt; b++) {
            if (dev->read(dev, v->htbl_start + b, blk_buf, 1) != 0) break;
            UW base = b * entries_per_blk;
            for (UW i = 0; i < entries_per_blk && base + i < nfmax; i++) {
                v->htbl[base + i] = rd_u32(is_le, &blk_buf[i * 4]);
            }
        }
    }

    /* Load used-block bitmap */
    {
        if (is_brightv) {
            dev->read(dev, 0, blk_buf, 1);
            UW b0_avail = bsize - 128;
            UW chunk = (v->ubmp_bytes < b0_avail) ? v->ubmp_bytes : b0_avail;
            memcpy(v->ubmp, blk_buf + 128, chunk);

            for (UW b = 1; b < nbmp; b++) {
                if (dev->read(dev, b, blk_buf, 1) != 0) break;
                UW base_byte = b0_avail + (b - 1) * bsize;
                if (base_byte < v->ubmp_bytes) {
                    UW rem = v->ubmp_bytes - base_byte;
                    UW n = (rem < bsize) ? rem : bsize;
                    memcpy(v->ubmp + base_byte, blk_buf, n);
                }
            }
        } else {
            for (UW b = 0; b < nbmp; b++) {
                if (dev->read(dev, v->bmp_start + b, blk_buf, 1) != 0) break;
                UW base_byte = b * bsize;
                for (UW i = 0; i < bsize && base_byte + i < v->ubmp_bytes; i++)
                    v->ubmp[base_byte + i] = blk_buf[i];
            }
        }
    }

    /* Mark dirty=1 in header on disk */
    flush_header(v);

    v->dirty = 0;
    free(blk_buf);
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
