/*
 * B-System BTRON3 Filesystem — file.c
 * Implementation of the public file.h API against a mounted Volume.
 *
 * Design:
 *   - Index level 0 only (max 40 records per file; E_LIMIT otherwise).
 *   - The first 192 bytes of a file's header block are the FileHeader.
 *   - Immediately after the FileHeader, RecordIndex entries are packed:
 *       bytes [192 .. 192 + 40*16 - 1] = up to 40 × 16-byte RecordIndex.
 *   - Data blocks are allocated contiguously from vol_alloc_block().
 *     A simple linear extent: all data for a record is written into
 *     successive blocks from the file's data_blk pool.
 *   - Fragment table is updated conceptually (bad-block handling deferred).
 *
 * Encoding: file names are UTF-8, stored in FileHeader.name[40].
 *
 * Error codes follow the btron/error.h convention (0 = OK, negative = error).
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
   extern int   tkl_memcmp(const void *, const void *, size_t);
   extern int   tkl_strcmp(const char *, const char *);
   extern int   tkl_strncmp(const char *, const char *, size_t);
   extern size_t tkl_strlen(const char *);
   extern char  *tkl_strncpy(char *, const char *, size_t);
#  define malloc   Imalloc
#  define calloc   Icalloc
#  define free     Ifree
#  define memcpy   tkl_memcpy
#  define memset   tkl_memset
#  define memcmp   tkl_memcmp
#  define strcmp   tkl_strcmp
#  define strncmp  tkl_strncmp
#  define strlen   tkl_strlen
#  define strncpy  tkl_strncpy
#endif

#include <btron/fs/block.h>
#include <btron/fs/fs_types.h>
#include <btron/fs/vol_api.h>
#include <btron/file.h>
#include <btron/error.h>
#include <btron/tad.h>
#include <btron/fs/fs_internal.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>

/* ── Global open-file / open-record tables ──────────────────────── */
OpenFile g_open_files[MAX_OPEN_FILES];
OpenRec  g_open_recs [MAX_OPEN_RECS ];

/* ── Block layout constants ─────────────────────────────────────── */
#define HDR_RIDX_OFFSET  192   /* RecordIndex array starts at byte 192 in hdr block */
#define HDR_RIDX_MAXBYTES (REC_IDX_LEVEL0_MAX * BTRON_REC_IDX_SIZE)

/* ── Timestamp helper ───────────────────────────────────────────── */
static UW now_ts(void) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    return (UW)(time(NULL) - 946684800UL);
#else
    return 0;
#endif
}

/* ── Endian-independent byte helpers ─────────────────────────────── */
static inline UH rd_u16_be(const unsigned char *p) {
    return (UH)(((UH)p[0] << 8) | (UH)p[1]);
}
static inline UW rd_u32_be(const unsigned char *p) {
    return ((UW)p[0] << 24) | ((UW)p[1] << 16) | ((UW)p[2] << 8) | (UW)p[3];
}
static inline UH rd_u16_le(const unsigned char *p) {
    return (UH)((UH)p[0] | ((UH)p[1] << 8));
}
static inline UW rd_u32_le(const unsigned char *p) {
    return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16) | ((UW)p[3] << 24);
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
static inline void wr_u16_le(unsigned char *p, UH val) {
    p[0] = (unsigned char)(val & 0xFF);
    p[1] = (unsigned char)((val >> 8) & 0xFF);
}
static inline void wr_u32_le(unsigned char *p, UW val) {
    p[0] = (unsigned char)(val & 0xFF);
    p[1] = (unsigned char)((val >> 8) & 0xFF);
    p[2] = (unsigned char)((val >> 16) & 0xFF);
    p[3] = (unsigned char)((val >> 24) & 0xFF);
}

/* ── Read FileHeader + RecordIndex from a header block ──────────── */
static int read_header_block(Volume *v, BLK blk, OpenFile *of)
{
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return -1;
    if (vol_read_blk(v, blk, buf) != 0) {
        free(buf);
        return -1;
    }

    if (vol_is_brightv(v)) {
        if (memcmp(buf, "Tron", 4) != 0 && memcmp(buf, "norT", 4) != 0) {
            /* On Cho-Kanji volumes, the Real Body Header is at blk - 1 for files with data */
            int found_hdr_m1 = 0;
            if (blk > 0) {
                unsigned char *hbuf = (unsigned char *)malloc(bsize);
                if (hbuf) {
                    if (vol_read_blk(v, blk - 1, hbuf) == 0 &&
                        (memcmp(hbuf, "Tron", 4) == 0 || memcmp(hbuf, "norT", 4) == 0)) {
                        free(buf);
                        buf = hbuf;
                        of->hdr_blk = blk - 1;
                        of->data_blk = blk;
                        found_hdr_m1 = 1;
                    } else {
                        free(hbuf);
                    }
                }
            }
            if (!found_hdr_m1) {
                /* Direct ELF binary or raw file stream in Cho-Kanji without header */
                int is_elf = (memcmp(buf, "\x7f\x45\x4c\x46", 4) == 0);
                of->hdr.flags = FILE_HDR_FLAGS_NORMAL;
                if (is_elf) of->hdr.flags |= 0x0001; /* OBJ_EXEC */
                of->hdr.atype = 0;
                of->hdr.ctime = 0;
                of->hdr.mtime = 0;
                of->hdr.atime = 0;
                of->hdr.owner = 0;
                of->hdr.group = 0;
                of->hdr.nlnk  = 1;
                of->hdr.idxlv = 0;
                of->hdr.total_size = bsize;
                of->hdr.nrec = 1;
                of->hdr.data_blk = blk;
                of->nrec = 1;
                of->data_used = bsize;
                of->data_blk = blk;
                of->ridx[0].kind = is_elf ? 0x9F00 : 0x0000;
                of->ridx[0].type = 0;
                of->ridx[0].offset = 0;
                of->ridx[0].size = bsize;
                of->ridx[0].flags = 1;
                const char *pfx = is_elf ? "ELF_" : "BODY_";
                char *d = (char *)of->hdr.name;
                while (*pfx) *d++ = *pfx++;
                UW num = (UW)of->fid;
                char tmp[16]; int ti = 0;
                if (num == 0) tmp[ti++] = '0';
                else { while (num > 0) { tmp[ti++] = (char)('0' + (num % 10)); num /= 10; } }
                while (ti > 0 && d < (char *)of->hdr.name + sizeof(of->hdr.name) - 1)
                    *d++ = tmp[--ti];
                *d = '\0';
                free(buf);
                return 0;
            }
        }
        /* B-right/V Real Body Header */
        of->hdr.flags      = rd_u16_le(buf + 4);
        of->hdr.atype      = 0;
        of->hdr.ctime      = rd_u32_le(buf + 0x60);
        of->hdr.mtime      = rd_u32_le(buf + 0x64);
        of->hdr.atime      = rd_u32_le(buf + 0x68);
        of->hdr.owner      = 0;
        of->hdr.group      = 0;
        of->hdr.nlnk       = 1;
        of->hdr.idxlv      = 0;
        of->hdr.total_size = rd_u32_le(buf + 0x48);
        UW nrec_4c         = rd_u32_le(buf + 0x4C);
        UW nrec_44         = rd_u32_le(buf + 0x44);
        of->hdr.nrec       = (of->hdr.total_size == 0 && nrec_44 > nrec_4c) ? nrec_44 : nrec_4c;

        UH tc[20];
        for (int k = 0; k < 16; k++) {
            tc[k] = rd_u16_le(buf + 0x6C + k * 2);
        }
        tc[16] = 0;
        btr_tcode_to_utf8(tc, 16, (char *)of->hdr.name, sizeof(of->hdr.name));

        if (of->hdr.nrec > REC_IDX_LEVEL0_MAX) {
            of->hdr.nrec = REC_IDX_LEVEL0_MAX;
        }
        of->nrec      = of->hdr.nrec;
        of->data_used = of->hdr.total_size;
        of->data_blk  = blk;

        for (unsigned int i = 0; i < of->nrec; i++) {
            unsigned char *rp = buf + bsize - (i + 1) * 16;
            of->ridx[i].kind   = rd_u16_le(rp + 0);
            of->ridx[i].type   = rd_u16_le(rp + 2);
            of->ridx[i].offset = rd_u32_le(rp + 4);
            of->ridx[i].size   = rd_u32_le(rp + 8);
            uint8_t nblocks    = rp[12];
            of->ridx[i].flags  = (UW)nblocks;
        }
        of->hdr.data_blk = of->data_blk;
        free(buf);
        return 0;
    }

    /* Standard cleanroom volume */
    unsigned char *p = buf;
    of->hdr.flags      = rd_u16_be(p +  0);
    of->hdr.atype      = rd_u16_be(p +  2);
    of->hdr.ctime      = rd_u32_be(p +  4);
    of->hdr.mtime      = rd_u32_be(p +  8);
    of->hdr.atime      = rd_u32_be(p + 12);
    of->hdr.owner      = rd_u16_be(p + 16);
    of->hdr.group      = rd_u16_be(p + 18);
    of->hdr.nlnk       = rd_u16_be(p + 20);
    of->hdr.idxlv      = rd_u16_be(p + 22);
    of->hdr.nrec       = rd_u32_be(p + 24);
    of->hdr.total_size = rd_u32_be(p + 28);
    memcpy(of->hdr.name, p + 32, 40);
    of->hdr.data_blk   = rd_u32_be(p + 72);

    /* Cap nrec defensively (NASA Rule 5) */
    if (of->hdr.nrec > REC_IDX_LEVEL0_MAX) {
        of->hdr.nrec = REC_IDX_LEVEL0_MAX;
    }
    of->nrec      = of->hdr.nrec;
    of->data_blk  = of->hdr.data_blk;
    of->data_used = of->hdr.total_size;

    /* Decode RecordIndex entries */
    for (unsigned int i = 0; i < of->nrec; i++) {
        unsigned char *rp = buf + HDR_RIDX_OFFSET + i * BTRON_REC_IDX_SIZE;
        of->ridx[i].kind   = rd_u16_be(rp +  0);
        of->ridx[i].type   = rd_u16_be(rp +  2);
        of->ridx[i].size   = rd_u32_be(rp +  4);
        of->ridx[i].offset = rd_u32_be(rp +  8);
        of->ridx[i].flags  = rd_u32_be(rp + 12);
    }
    free(buf);
    return 0;
}

/* ── Write FileHeader + RecordIndex back to the header block ─────── */
static int write_header_block(Volume *v, BLK blk, const OpenFile *of)
{
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return -1;

    if (vol_is_brightv(v)) {
        if (vol_read_blk(v, blk, buf) != 0) {
            memset(buf, 0, bsize);
        }
        if (memcmp(buf, "norT", 4) != 0) {
            memcpy(buf, "Tron", 4);
        }
        wr_u16_le(buf + 4, of->hdr.flags);
        wr_u32_le(buf + 0x48, of->hdr.total_size);
        wr_u32_le(buf + 0x4C, of->nrec);
        wr_u32_le(buf + 0x64, of->hdr.mtime);

        UH tc[20];
        btr_utf8_to_tcode((const char *)of->hdr.name, tc, 16);
        for (int k = 0; k < 16; k++) {
            wr_u16_le(buf + 0x6C + k * 2, tc[k]);
        }

        unsigned int cnt = (of->nrec < REC_IDX_LEVEL0_MAX) ? of->nrec : REC_IDX_LEVEL0_MAX;
        for (unsigned int i = 0; i < cnt; i++) {
            unsigned char *rp = buf + bsize - (i + 1) * 16;
            wr_u16_le(rp + 0, of->ridx[i].kind);
            wr_u16_le(rp + 2, of->ridx[i].type);
            wr_u32_le(rp + 4, of->ridx[i].offset);
            wr_u32_le(rp + 8, of->ridx[i].size);
            rp[12] = (unsigned char)(of->ridx[i].flags & 0xFF);
            BLK rblk = of->data_blk;
            rp[13] = (unsigned char)(rblk & 0xFF);
            rp[14] = (unsigned char)((rblk >> 8) & 0xFF);
            rp[15] = (unsigned char)((rblk >> 16) & 0xFF);
        }
        int ret = vol_write_blk(v, blk, buf);
        free(buf);
        return ret;
    }

    /* Standard cleanroom volume */
    memset(buf, 0, bsize);

    unsigned char *p = buf;
    wr_u16_be(p +  0, of->hdr.flags);
    wr_u16_be(p +  2, of->hdr.atype);
    wr_u32_be(p +  4, of->hdr.ctime);
    wr_u32_be(p +  8, of->hdr.mtime);
    wr_u32_be(p + 12, of->hdr.atime);
    wr_u16_be(p + 16, of->hdr.owner);
    wr_u16_be(p + 18, of->hdr.group);
    wr_u16_be(p + 20, of->hdr.nlnk);
    wr_u16_be(p + 22, of->hdr.idxlv);
    wr_u32_be(p + 24, of->nrec);
    wr_u32_be(p + 28, of->hdr.total_size);
    memcpy(p + 32, of->hdr.name, 40);
    wr_u32_be(p + 72, of->data_blk);

    /* Encode RecordIndex entries */
    unsigned int cnt = (of->nrec < REC_IDX_LEVEL0_MAX) ? of->nrec : REC_IDX_LEVEL0_MAX;
    for (unsigned int i = 0; i < cnt; i++) {
        unsigned char *rp = buf + HDR_RIDX_OFFSET + i * BTRON_REC_IDX_SIZE;
        wr_u16_be(rp +  0, of->ridx[i].kind);
        wr_u16_be(rp +  2, of->ridx[i].type);
        wr_u32_be(rp +  4, of->ridx[i].size);
        wr_u32_be(rp +  8, of->ridx[i].offset);
        wr_u32_be(rp + 12, of->ridx[i].flags);
    }
    int ret = vol_write_blk(v, blk, buf);
    free(buf);
    return ret;
}

static int fs_strcasecmp(const char *s1, const char *s2)
{
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

/* ── Lookup file by name using hash table then full compare ──────── */
static FID find_fid_by_name(Volume *v, const char *name)
{
    if (!v || !name) return FID_INVALID;
    UW target_hash = vol_name_hash(name);
    UW nfmax = vol_nfmax(v);
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return FID_INVALID;

    int is_bv = vol_is_brightv(v);

    for (FID i = 0; i < nfmax; i++) {
        if (vol_fid_refcount(v, i) == 0 && i != FID_ROOT) continue;
        if (!is_bv && vol_hash_get(v, i) != target_hash) continue;
        /* Hash match or B-right/V scan: do name compare */
        BLK hblk = vol_fid_get_blk(v, i);
        if (hblk == 0 || hblk == FID_INVALID) continue;
        if (vol_read_blk(v, hblk, buf) != 0) continue;

        char stored[64];
        if (is_bv) {
            if (memcmp(buf, "Tron", 4) != 0 && memcmp(buf, "norT", 4) != 0)
                continue;
            UH tc[20];
            for (int k = 0; k < 16; k++) {
                tc[k] = rd_u16_le(buf + 0x6C + k * 2);
            }
            tc[16] = 0;
            btr_tcode_to_utf8(tc, 16, stored, sizeof(stored));
        } else {
            memcpy(stored, buf + 32, 40);
            stored[40] = '\0';
        }

        if (strcmp(stored, name) == 0 || fs_strcasecmp(stored, name) == 0) {
            free(buf);
            return i;
        }
    }
    free(buf);
    return FID_INVALID;
}

/* ── opn_fil ─────────────────────────────────────────────────────── */
ID opn_fil(const char *path, UW mode)
{
    if (!path) return (ID)-1;

    Volume *v = NULL;
    if (path[0] == '/') {
        if (strncmp(path, "/ANDERS", 7) == 0 && (path[7] == '/' || path[7] == '\0')) v = g_anders_vol;
        else if (((strncmp(path, "/CHOKANJI", 9) == 0 && (path[9] == '/' || path[9] == '\0')) ||
                  (strncmp(path, "/B-right", 8) == 0 && (path[8] == '/' || path[8] == '\0'))) && g_chokanji_vol) v = g_chokanji_vol;
        else if (strncmp(path, "/SYS", 4) == 0 && (path[4] == '/' || path[4] == '\0')) v = g_sys_vol;
        else v = g_sys_vol;
    } else {
        /* Relative path: use active directory volume */
        if (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) v = g_anders_vol;
        else if ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) v = g_chokanji_vol;
        else v = g_sys_vol;
    }
    if (!v) v = g_sys_vol;
    if (!v) return (ID)-1;

    /* Strip leading "/" or volume prefix for flat namespace lookup */
    const char *name = path;
    if (name[0] == '/') {
        while (*name == '/') name++;
        /* Skip volume label if present */
        const char *sl = name;
        while (*sl && *sl != '/') sl++;
        if (*sl == '/') name = sl + 1;
        else name = ""; /* Path was just volume name (e.g. "/CHOKANJI") -> target is root */
    }

    FID fid = FID_INVALID;
    if (name[0] == '\0' || strcmp(name, ".") == 0) {
        fid = FID_ROOT;
    } else {
        fid = find_fid_by_name(v, name);
        /* If relative search on SYS fails, check ANDERS (but never do unconstrained Cho-Kanji scan) */
        if (fid == FID_INVALID && v == g_sys_vol && g_anders_vol && path[0] != '/') {
            fid = find_fid_by_name(g_anders_vol, name);
            if (fid != FID_INVALID) v = g_anders_vol;
        }
    }
    if (fid == FID_INVALID) return (ID)-1;
    return opn_fil_fid(v, fid, mode);
}

/* ── opn_fil_fid ─────────────────────────────────────────────────── */
ID opn_fil_fid(Volume *v, FID fid, UW mode)
{
    if (!v || fid == FID_INVALID) return (ID)-1;
    BLK hblk = vol_fid_get_blk(v, fid);
    if (hblk == 0 || hblk == FID_INVALID) return (ID)-1;

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].used) {
            OpenFile *of = &g_open_files[i];
            memset(of, 0, sizeof(*of));
            of->vol      = v;
            of->fid      = fid;
            of->hdr_blk  = hblk;
            of->mode     = mode;
            of->used     = 1;
            if (read_header_block(v, of->hdr_blk, of) != 0) {
                of->used = 0;
                return (ID)-1;
            }
            return (ID)i;
        }
    }
    return (ID)-1; /* E_LIMIT: no open slots */
}

/* ── cre_fil ─────────────────────────────────────────────────────── */
ID cre_fil(const char *path, UW mode)
{
    Volume *v = g_sys_vol;
    if (path && strncmp(path, "/ANDERS", 7) == 0 && g_anders_vol) {
        v = g_anders_vol;
    } else if (path && (strncmp(path, "/CHOKANJI", 9) == 0 || strncmp(path, "/B-right", 8) == 0) && g_chokanji_vol) {
        v = g_chokanji_vol;
    }
    if (!v || !path) return (ID)-1;

    const char *name = path;
    if (name[0] == '/') {
        while (*name == '/') name++;
        const char *sl = name;
        while (*sl && *sl != '/') sl++;
        if (*sl == '/') name = sl + 1;
    }

    if (find_fid_by_name(v, name) != FID_INVALID)
        return (ID)-1; /* already exists */

    FID fid = vol_fid_alloc(v);
    if (fid == FID_INVALID) return (ID)-1;

    BLK hblk = vol_alloc_block(v);
    if (hblk == FID_INVALID) return (ID)-1;

    /* Find a free open-file slot */
    int slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        vol_free_block(v, hblk);
        return (ID)-1;
    }

    OpenFile *of = &g_open_files[slot];
    memset(of, 0, sizeof(*of));
    of->vol      = v;
    of->fid      = fid;
    of->hdr_blk  = hblk;
    of->mode     = mode;
    of->used     = 1;
    of->dirty    = 1;
    of->data_blk = 0;
    of->data_used= 0;

    UW ts = now_ts();
    of->hdr.flags      = FILE_HDR_FLAGS_NORMAL;
    of->hdr.atype      = 0x0001;
    of->hdr.ctime      = ts;
    of->hdr.mtime      = ts;
    of->hdr.atime      = ts;
    of->hdr.nlnk       = 1;
    of->hdr.idxlv      = 0;
    of->hdr.nrec       = 0;
    of->hdr.total_size = 0;
    of->hdr.data_blk   = 0;
    {
        unsigned int i = 0;
        const unsigned char *nm = (const unsigned char *)name;
        while (nm[i] && i < 39) { of->hdr.name[i] = nm[i]; i++; }
        of->hdr.name[i] = 0;
    }
    of->nrec = 0;

    /* Write initial header block */
    write_header_block(v, hblk, of);

    /* Register in FID table */
    vol_fid_set(v, fid, hblk, 1);
    vol_hash_set(v, fid, vol_name_hash(name));
    vol_mark_dirty(v);

    return (ID)slot;
}

/* ── cls_fil ─────────────────────────────────────────────────────── */
ER cls_fil(ID fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ER)-1;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used) return (ER)-1;

    if (of->dirty) {
        of->hdr.nrec       = of->nrec;
        of->hdr.mtime      = now_ts();
        of->hdr.total_size = of->data_used;
        of->hdr.data_blk   = of->data_blk;
        write_header_block(of_vol(of), of->hdr_blk, of);
    }
    memset(of, 0, sizeof(*of));
    return (ER)0;
}

/* ── del_fil ─────────────────────────────────────────────────────── */
ER del_fil(const char *path)
{
    Volume *v = g_sys_vol;
    if (path && strncmp(path, "/ANDERS", 7) == 0 && g_anders_vol) {
        v = g_anders_vol;
    } else if (path && (strncmp(path, "/CHOKANJI", 9) == 0 || strncmp(path, "/B-right", 8) == 0) && g_chokanji_vol) {
        v = g_chokanji_vol;
    }
    if (!v || !path) return (ER)-1;

    const char *name = path;
    if (name[0] == '/') {
        while (*name == '/') name++;
        const char *sl = name;
        while (*sl && *sl != '/') sl++;
        if (*sl == '/') name = sl + 1;
    }

    FID fid = find_fid_by_name(v, name);
    if (fid == FID_INVALID || fid == FID_ROOT) return (ER)-1;

    /* Open to read header and record index */
    ID fd = opn_fil(path, F_READ);
    if (fd < 0) return (ER)-1;

    OpenFile *of = &g_open_files[(int)fd];
    UW bsize = vol_block_size(v);

    /* Free data blocks if allocated */
    if (of->data_blk != 0 && of->data_used > 0) {
        UW used_blks = (of->data_used + bsize - 1) / bsize;
        for (UW b = 0; b < used_blks; b++) {
            vol_free_block(v, of->data_blk + b);
        }
    }

    /* Free header block */
    vol_free_block(v, of->hdr_blk);

    /* Release FID */
    vol_fid_free(v, fid);

    cls_fil(fd);
    vol_mark_dirty(v);
    return (ER)0;
}

/* ── ins_rec ─────────────────────────────────────────────────────── */
ER ins_rec(ID fd, W rec_idx, const void *buf, W sz)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ER)-1;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used) return (ER)-1;
    if (of->nrec >= REC_IDX_LEVEL0_MAX) return (ER)-5; /* E_LIMIT */

    Volume *v = of_vol(of);
    if (!v) return (ER)-1;

    UW payload = (sz > 0) ? (UW)sz : 0;
    UW data_offset = of->data_used;
    UW bsize = vol_block_size(v);

    /* Write payload across blocks */
    if (payload > 0 && buf) {
        const unsigned char *src = (const unsigned char *)buf;
        UW remaining = payload;
        UW cur_offset = data_offset;
        unsigned char *blk_buf = (unsigned char *)malloc(bsize);
        if (!blk_buf) return (ER)-1;

        while (remaining > 0) {
            UW blk_idx = cur_offset / bsize;
            UW blk_off = cur_offset % bsize;

            /* Ensure data_blk is allocated */
            if (of->data_blk == 0) {
                BLK db = vol_alloc_block(v);
                if (db == FID_INVALID) { free(blk_buf); return (ER)-1; }
                of->data_blk = db;
                of->hdr.data_blk = db;
            } else if (blk_idx > 0 && blk_off == 0) {
                /* Allocate next block in contiguous extent */
                BLK nb = vol_alloc_block(v);
                if (nb == FID_INVALID) { free(blk_buf); return (ER)-1; }
            }

            BLK cur_blk = of->data_blk + blk_idx;
            if (blk_off > 0 || remaining < bsize) {
                if (vol_read_blk(v, cur_blk, blk_buf) != 0)
                    memset(blk_buf, 0, bsize);
            } else {
                memset(blk_buf, 0, bsize);
            }

            UW space = bsize - blk_off;
            UW chunk = (remaining < space) ? remaining : space;
            memcpy(blk_buf + blk_off, src, chunk);
            vol_write_blk(v, cur_blk, blk_buf);

            src        += chunk;
            remaining  -= chunk;
            cur_offset += chunk;
        }
        free(blk_buf);
    }

    /* Insert RecordIndex entry at rec_idx (shift later entries) */
    unsigned int insert_at = (rec_idx < 0 || (UW)rec_idx > of->nrec)
                           ? (unsigned int)of->nrec
                           : (unsigned int)rec_idx;
    /* Shift entries above insert_at */
    for (unsigned int i = (unsigned int)of->nrec; i > insert_at; i--)
        of->ridx[i] = of->ridx[i-1];

    of->ridx[insert_at].kind   = 0;
    of->ridx[insert_at].type   = (UH)RT_TADDATA;
    of->ridx[insert_at].size   = payload;
    of->ridx[insert_at].offset = data_offset;
    of->ridx[insert_at].flags  = 0;

    of->nrec++;
    of->hdr.nrec       = of->nrec;
    of->data_used     += payload;
    of->hdr.total_size = of->data_used;
    of->dirty          = 1;

    write_header_block(v, of->hdr_blk, of);
    return (ER)0;
}

/* ── del_rec ─────────────────────────────────────────────────────── */
ER del_rec(ID fd, W rec_idx)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ER)-1;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used || rec_idx < 0 || (UW)rec_idx >= of->nrec) return (ER)-1;

    /* Shift RecordIndex entries */
    for (unsigned int i = (unsigned int)rec_idx; i + 1 < of->nrec; i++)
        of->ridx[i] = of->ridx[i+1];
    memset(&of->ridx[of->nrec - 1], 0, sizeof(RecordIndex));
    of->nrec--;
    of->hdr.nrec = of->nrec;
    of->dirty    = 1;

    write_header_block(of_vol(of), of->hdr_blk, of);
    return (ER)0;
}

/* ── rd_rec ──────────────────────────────────────────────────────── */
ER rd_rec(ID rec_id, VP buf, W sz, W *read_sz)
{
    if (rec_id < 0 || rec_id >= MAX_OPEN_RECS) return (ER)-1;
    OpenRec *or_ = &g_open_recs[(int)rec_id];
    if (!or_->used) return (ER)-1;

    OpenFile *of = &g_open_files[(int)or_->fd];
    Volume   *v  = of_vol(of);
    if (!v) return (ER)-1;

    UW want = (sz > 0) ? (UW)sz : 0;
    UW avail = (or_->size > or_->pos) ? (or_->size - or_->pos) : 0;
    if (want > avail) want = avail;
    if (want == 0) { if (read_sz) *read_sz = 0; return (ER)0; }

    unsigned char *dst = (unsigned char *)buf;
    UW stream_off = or_->data_offset + or_->pos;
    UW remaining = want;
    UW bsize = vol_block_size(v);

    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return (ER)-1;

    while (remaining > 0) {
        BLK cur_blk = of->data_blk + (stream_off / bsize);
        UW  blk_off = stream_off % bsize;

        if (vol_read_blk(v, cur_blk, blk_buf) != 0) break;
        UW avail_in_blk = bsize - blk_off;
        UW chunk = (remaining < avail_in_blk) ? remaining : avail_in_blk;
        memcpy(dst, blk_buf + blk_off, chunk);
        dst        += chunk;
        remaining  -= chunk;
        stream_off += chunk;
    }
    free(blk_buf);
    UW got = want - remaining;
    or_->pos += got;
    if (read_sz) *read_sz = (W)got;
    return (ER)0;
}

/* ── wr_rec ──────────────────────────────────────────────────────── */
ER wr_rec(ID rec_id, const void *buf, W sz, W *wrote_sz)
{
    if (rec_id < 0 || rec_id >= MAX_OPEN_RECS) return (ER)-1;
    OpenRec *or_ = &g_open_recs[(int)rec_id];
    if (!or_->used) return (ER)-1;

    OpenFile *of = &g_open_files[(int)or_->fd];
    Volume   *v  = of_vol(of);
    if (!v) return (ER)-1;

    UW want = (sz > 0) ? (UW)sz : 0;
    if (want == 0) { if (wrote_sz) *wrote_sz = 0; return (ER)0; }

    const unsigned char *src = (const unsigned char *)buf;
    UW stream_off = or_->data_offset + or_->pos;
    UW remaining = want;
    UW bsize = vol_block_size(v);

    unsigned char *blk_buf = (unsigned char *)malloc(bsize);
    if (!blk_buf) return (ER)-1;

    while (remaining > 0) {
        BLK cur_blk = of->data_blk + (stream_off / bsize);
        UW  blk_off = stream_off % bsize;

        if (blk_off > 0 || remaining < bsize) {
            if (vol_read_blk(v, cur_blk, blk_buf) != 0)
                memset(blk_buf, 0, bsize);
        } else {
            memset(blk_buf, 0, bsize);
        }
        UW avail_in_blk = bsize - blk_off;
        UW chunk = (remaining < avail_in_blk) ? remaining : avail_in_blk;
        memcpy(blk_buf + blk_off, src, chunk);
        vol_write_blk(v, cur_blk, blk_buf);
        src        += chunk;
        remaining  -= chunk;
        stream_off += chunk;
    }
    free(blk_buf);
    UW got = want - remaining;
    or_->pos += got;
    if (or_->pos > or_->size) {
        or_->size = or_->pos;
        of->ridx[or_->rec_idx].size = or_->size;
        of->hdr.total_size += got;
        of->dirty = 1;
    }
    if (wrote_sz) *wrote_sz = (W)got;
    return (ER)0;
}

/* ── opn_rec / cls_rec / pos_rec / trn_rec ───────────────────────── */
ID opn_rec(ID fd, W rec_idx, UW mode)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ID)-1;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used || rec_idx < 0 || (UW)rec_idx >= of->nrec) return (ID)-1;

    for (int i = 0; i < MAX_OPEN_RECS; i++) {
        if (!g_open_recs[i].used) {
            OpenRec *or_ = &g_open_recs[i];
            memset(or_, 0, sizeof(*or_));
            or_->used        = 1;
            or_->fd          = fd;
            or_->rec_idx     = (W)rec_idx;
            or_->mode        = mode;
            or_->pos         = 0;
            or_->size        = of->ridx[rec_idx].size;
            or_->data_offset = of->ridx[rec_idx].offset;
            return (ID)i;
        }
    }
    return (ID)-1;
}

ER cls_rec(ID rec_id)
{
    if (rec_id < 0 || rec_id >= MAX_OPEN_RECS) return (ER)-1;
    memset(&g_open_recs[(int)rec_id], 0, sizeof(OpenRec));
    return (ER)0;
}

ER pos_rec(ID rec_id, W offset, W origin)
{
    if (rec_id < 0 || rec_id >= MAX_OPEN_RECS) return (ER)-1;
    OpenRec *or_ = &g_open_recs[(int)rec_id];
    if (!or_->used) return (ER)-1;
    switch (origin) {
        case REC_POS_SET: or_->pos = (UW)offset; break;
        case REC_POS_CUR: or_->pos = (UW)((W)or_->pos + offset); break;
        case REC_POS_END: or_->pos = (UW)((W)or_->size + offset); break;
        default: return (ER)-1;
    }
    return (ER)0;
}

ER trn_rec(ID rec_id, W sz)
{
    if (rec_id < 0 || rec_id >= MAX_OPEN_RECS) return (ER)-1;
    OpenRec *or_ = &g_open_recs[(int)rec_id];
    if (!or_->used) return (ER)-1;
    or_->size = (sz >= 0) ? (UW)sz : 0;
    return (ER)0;
}

/* ── opn_dir / rd_dir / cls_dir ──────────────────────────────────── */
typedef struct {
    UW next_fid;
    Volume *vol;
} DirState;
static DirState g_dirs[16];
static int      g_dir_used[16];

ID opn_dir(const char *path)
{
    Volume *v = g_sys_vol;
    if (path && strncmp(path, "/ANDERS", 7) == 0 && g_anders_vol) {
        v = g_anders_vol;
    } else if (path && (strncmp(path, "/CHOKANJI", 9) == 0 || strncmp(path, "/B-right", 8) == 0) && g_chokanji_vol) {
        v = g_chokanji_vol;
    }
    for (int i = 0; i < 16; i++) {
        if (!g_dir_used[i]) {
            g_dir_used[i] = 1;
            g_dirs[i].next_fid = 0;
            g_dirs[i].vol = v;
            return (ID)(0x1000 + i);
        }
    }
    return (ID)-1;
}

ER rd_dir(ID dir_id, DIR_ENTRY *entry)
{
    int slot = (int)(dir_id - 0x1000);
    if (slot < 0 || slot >= 16 || !g_dir_used[slot]) return (ER)-1;
    Volume *v = g_dirs[slot].vol ? g_dirs[slot].vol : g_sys_vol;
    if (!v) return (ER)-1;

    UW nfmax = vol_nfmax(v);
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return (ER)-1;
    int is_bv = vol_is_brightv(v);

    while (g_dirs[slot].next_fid < nfmax) {
        FID fid = g_dirs[slot].next_fid++;
        if (vol_fid_refcount(v, fid) == 0 && fid != FID_ROOT) continue;
        BLK hblk = vol_fid_get_blk(v, fid);
        if (hblk == 0 || hblk == FID_INVALID) continue;

        if (vol_read_blk(v, hblk, buf) != 0) continue;

        entry->robj_id = (ID)fid;
        if (is_bv) {
            if (memcmp(buf, "Tron", 4) != 0 && memcmp(buf, "norT", 4) != 0)
                continue;
            entry->attr = rd_u16_le(buf + 4);
            entry->size = rd_u32_le(buf + 0x48);
            UH tc[20];
            for (int k = 0; k < 16; k++) {
                tc[k] = rd_u16_le(buf + 0x6C + k * 2);
            }
            tc[16] = 0;
            char nm[64];
            btr_tcode_to_utf8(tc, 16, nm, sizeof(nm));
            if (!nm[0]) continue;
            int ni = 0;
            while (nm[ni] && ni < 63) { entry->name[ni] = nm[ni]; ni++; }
            entry->name[ni] = '\0';
        } else {
            entry->attr = ((UW)buf[0] << 8) | buf[1]; /* flags BE */
            entry->size = ((UW)buf[28] << 24) | ((UW)buf[29] << 16) |
                          ((UW)buf[30] << 8)  | buf[31];
            char nm[41];
            memcpy(nm, buf + 32, 40);
            nm[40] = '\0';
            int ni = 0;
            while (nm[ni] && ni < 63) { entry->name[ni] = nm[ni]; ni++; }
            entry->name[ni] = '\0';
        }
        free(buf);
        return (ER)0;
    }
    free(buf);
    return (ER)1; /* E_EOF / end of directory */
}

ER cls_dir(ID dir_id)
{
    int slot = (int)(dir_id - 0x1000);
    if (slot < 0 || slot >= 16) return (ER)-1;
    g_dir_used[slot] = 0;
    return (ER)0;
}

/* ── cre_lnk / del_lnk ──────────────────────────────────────────── */
ER cre_lnk(const char *link_path, const FS_LINK *target)
{
    if (!link_path || !target) return (ER)-1;

    /* Build RT_LINK payload: LinkRecord (fixed 16B) + UTF-8 name */
    const char *link_name = link_path;
    if (link_name[0] == '/') {
        while (*link_name == '/') link_name++;
        const char *sl = link_name;
        while (*sl && *sl != '/') sl++;
        if (*sl == '/') link_name = sl + 1;
    }

    /* Encode payload: [target_fid 4B][attr[5] 10B][name_len 2B][name...] */
    unsigned char payload[16 + 40];
    memset(payload, 0, sizeof(payload));
    payload[0] = (unsigned char)(target->target_fid >> 24);
    payload[1] = (unsigned char)(target->target_fid >> 16);
    payload[2] = (unsigned char)(target->target_fid >>  8);
    payload[3] = (unsigned char)(target->target_fid      );
    for (int a = 0; a < 5; a++) {
        payload[4 + a*2]   = (unsigned char)(target->attr[a] >> 8);
        payload[4 + a*2+1] = (unsigned char)(target->attr[a]     );
    }
    unsigned int nlen = 0;
    while (link_name[nlen] && nlen < 39) nlen++;
    payload[14] = (unsigned char)(nlen >> 8);
    payload[15] = (unsigned char)(nlen     );
    memcpy(payload + 16, link_name, nlen);

    /* Create the Real Body as a link-file */
    ID fd = cre_fil(link_path, F_WRITE | F_CREATE);
    if (fd < 0) return (ER)-1;

    OpenFile *of = &g_open_files[(int)fd];
    /* Set link-file type in flags */
    of->hdr.flags = FILE_HDR_FLAGS_LINK;
    of->dirty     = 1;

    /* Insert one RT_LINK record */
    ER err = ins_rec(fd, 0, payload, (W)(16 + nlen));
    if (err == 0) {
        of->ridx[0].type = (UH)RT_LINK;
        write_header_block(of_vol(of), of->hdr_blk, of);
    }
    cls_fil(fd);
    return err;
}

ER del_lnk(const char *link_path)
{
    return del_fil(link_path);
}

/* ── ref_vol ─────────────────────────────────────────────────────── */
ER ref_vol(ID vol_id, VOL_INFO *info)
{
    Volume *v = g_sys_vol;
    if (vol_id == 1 && g_anders_vol) v = g_anders_vol;
    else if (vol_id == 2 && g_chokanji_vol) v = g_chokanji_vol;
    if (!v || !info) return (ER)-1;
    info->vol_id       = vol_id;
    info->total_blocks = vol_total_blocks(v);
    info->free_blocks  = vol_free_blocks(v);
    info->block_size   = vol_block_size(v);
    {
        const char *nm = vol_name(v);
        unsigned int i = 0;
        while (nm[i] && i < 31) { info->vol_name[i] = nm[i]; i++; }
        info->vol_name[i] = '\0';
    }
    return (ER)0;
}

/* ── mov_fil / chg_fil (stubs) ───────────────────────────────────── */
ER chg_fil(ID fd, UW mode)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ER)-1;
    g_open_files[(int)fd].mode = mode;
    return (ER)0;
}

ER mov_fil(const char *src_path, const char *dst_path)
{
    if (!src_path || !dst_path) return (ER)-1;
    Volume *v = g_sys_vol;
    if (!v) return (ER)-1;

    const char *src = src_path;
    if (src[0] == '/') { while (*src == '/') src++; const char *sl = src; while (*sl && *sl != '/') sl++; if (*sl == '/') src = sl + 1; }
    const char *dst = dst_path;
    if (dst[0] == '/') { while (*dst == '/') dst++; const char *sl = dst; while (*sl && *sl != '/') sl++; if (*sl == '/') dst = sl + 1; }

    FID fid = find_fid_by_name(v, src);
    if (fid == FID_INVALID) return (ER)-1;

    /* Update name in FileHeader */
    BLK hblk = vol_fid_get_blk(v, fid);
    unsigned char buf[BTRON_BLOCK_SIZE];
    if (vol_read_blk(v, hblk, buf) != 0) return (ER)-1;
    memset(buf + 32, 0, 40);
    unsigned int i = 0;
    const unsigned char *nm = (const unsigned char *)dst;
    while (nm[i] && i < 39) { buf[32 + i] = nm[i]; i++; }
    vol_write_blk(v, hblk, buf);

    /* Update hash table */
    vol_hash_set(v, fid, vol_name_hash(dst));
    vol_mark_dirty(v);
    return (ER)0;
}

/* ── fil_set_rec_type ────────────────────────────────────────────── */
void fil_set_rec_type(ID fd, W rec_idx, UH type)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used || (UW)rec_idx >= of->nrec) return;
    of->ridx[rec_idx].type = type;
    of->dirty = 1;
}
