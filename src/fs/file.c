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

#include <btron/libc_shim.h>
#if BTRON_HOSTED
#include <time.h>
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
/*
 * A B-right/V Real Body header keeps its fixed fields in the first 0x100 bytes
 * (measured: 0x6C..0x93 is the name and 0x94..0xFF hold further per-body data),
 * and the record rows live above that.  No body on the golden volume has a row
 * below 0x1100, so everything from 0x100 up is the row area.
 */
#define BVR_RIDX_AREA_START 0x100

/*
 * How many 16-byte index rows a header block can really hold.  The in-core
 * array is sized for the largest measured case (a B-right/V 8 KiB header,
 * FS.md 7.4), which is far more than a cleanroom 1 KiB block stores, so every
 * read and write of the row area is clamped by the block, not by the array.
 */
static UW hdr_row_capacity(UW bsize, int brightv)
{
    UW head = brightv ? BVR_RIDX_AREA_START : HDR_RIDX_OFFSET;
    if (bsize < head + BTRON_REC_IDX_SIZE) return 0;
    UW room = (bsize - head) / BTRON_REC_IDX_SIZE;
    return room < REC_IDX_LEVEL0_MAX ? room : REC_IDX_LEVEL0_MAX;
}

/* ── Timestamp helper ───────────────────────────────────────────── */
static UW now_ts(void) {
#if BTRON_HOSTED
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
static inline UW rd_u24_le(const unsigned char *p) {
    return (UW)p[0] | ((UW)p[1] << 8) | ((UW)p[2] << 16);
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

/* B-right/V Real Body header magic. */
#define BV_IS_HDR(b) (memcmp((b), "norT", 4) == 0 || memcmp((b), "Tron", 4) == 0)

/*
 * Resolve a B-right/V body's header block from the address its FID table entry
 * names, and leave that block's bytes in `buf`.
 *
 * The FID table names the body's first *data* block, so the header is one block
 * earlier: measured on the golden volume, the magic is at ptr-1 for 4457 of the
 * 4457 live FIDs and at ptr for none of them (FS.md 3.2, 7.4 consequence 4).
 * Preferring ptr instead -- the order this code first used -- makes a header-only
 * body (its successor's header happens to be at ptr) invisible and its successor
 * reachable under two FIDs with two names, which is how one body came to be
 * listed twice as same-sized siblings; and it decodes FID 0 as SBOOT rather than
 * the volume's root drawer, dumping that drawer's 52 children at the top level.
 *
 * Falling back to ptr keeps bodies written before this rule readable.  Returns 0
 * and the header block in *hdr_out on success, -1 when neither block is one.
 */
static int bv_hdr_block(Volume *v, BLK ptr, unsigned char *buf, BLK *hdr_out)
{
    if (ptr > 0 && vol_read_blk(v, ptr - 1, buf) == 0 && BV_IS_HDR(buf)) {
        *hdr_out = ptr - 1;
        return 0;
    }
    if (vol_read_blk(v, ptr, buf) == 0 && BV_IS_HDR(buf)) {
        *hdr_out = ptr;
        return 0;
    }
    return -1;
}

/*
 * The Real Body's name: up to 20 T-code units at +0x6C, zero-terminated, ending
 * at +0x94.  Measured over all 4457 golden-volume bodies the longest names are
 * exactly 20 units (FS.md 5.1), so decoding only 16 truncates 163 of them and the
 * listing then offers a path segment no lookup can resolve -- opn_dir() on
 * "English/Limitations on Use" fails because the drawer's own row says
 * "Limitations on U".  Both directions go through these two helpers.
 */
#define BV_NAME_UNITS 20

static void bv_name_from_hdr(const unsigned char *hdrbuf, char *out, int out_max)
{
    UH tc[BV_NAME_UNITS];
    for (int k = 0; k < BV_NAME_UNITS; k++) tc[k] = rd_u16_le(hdrbuf + 0x6C + k * 2);
    btr_tcode_to_utf8(tc, BV_NAME_UNITS, out, out_max);
}

static void bv_name_to_hdr(unsigned char *hdrbuf, const char *name)
{
    UH tc[BV_NAME_UNITS];
    btr_utf8_to_tcode(name, tc, BV_NAME_UNITS);
    for (int k = 0; k < BV_NAME_UNITS; k++) wr_u16_le(hdrbuf + 0x6C + k * 2, tc[k]);
}

static int read_header_block(Volume *v, BLK blk, OpenFile *of)
{
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return -1;
    if (vol_read_blk(v, blk, buf) != 0) {
        free(buf);
        return -1;
    }

    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) {
        BLK hdr = 0;
        int found_hdr_m1 = 0;
        int blk_is_elf = (memcmp(buf, "\x7f\x45\x4c\x46", 4) == 0);
        if (bv_hdr_block(v, blk, buf, &hdr) == 0) {
            found_hdr_m1 = (hdr == blk - 1);
            of->hdr_blk  = hdr;
            of->data_blk = hdr + 1;
        } else {
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
            of->hdr.did = 0;
            of->hdr.pdid = 0;
            of->is_stream = 1;
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
        /* B-right/V Real Body Header */
        of->hdr.flags      = rd_u16_le(buf + 4);
        of->hdr.atype      = 0;
        of->hdr.ctime      = rd_u32_le(buf + 0x60);
        of->hdr.mtime      = rd_u32_le(buf + 0x64);
        of->hdr.atime      = rd_u32_le(buf + 0x68);
        /*
         * B-right/V encodes no drawer id / parent drawer id in the Real Body
         * header: +0x64 and +0x68 are the modify and access timestamps above,
         * so they must not be mirrored into did/pdid.  Hierarchy comes from
         * RT_LINK records alone (FS.md 5.1, 7.4).
         */
        of->hdr.did        = 0;
        of->hdr.pdid       = 0;
        of->is_stream      = 0;   /* set below, from the decoded rows */
        of->hdr.owner      = 0;
        of->hdr.group      = 0;
        of->hdr.nlnk       = 1;
        of->hdr.idxlv      = 0;
        of->hdr.total_size = rd_u32_le(buf + 0x48);
        UW nblk_4c = rd_u32_le(buf + 0x4C);   /* blocks claimed by this body */

        bv_name_from_hdr(buf, (char *)of->hdr.name, (int)sizeof(of->hdr.name));

        /*
         * B-right/V record index: 16-byte rows in the tail area of the header
         * block, ascending address == ascending record number.  A continuation
         * row (byte0 != 0) sits directly after the row it extends and is not a
         * record of its own, so the header's +0x50 count leaves it out (FS.md
         * 7.1, 7.4; LINX P2 says the same for read()/lseek()).
         *
         * The row fields, measured against golden B-right/V bodies:
         *   +0  byte0 kind, byte1 packed record type (0x80 = link)
         *   +2  UH subtype / attribute
         *   +4  UW record body position: UH block offset, UH byte offset
         *   +8  UW payload size
         *   +12 UB block count of this record's body
         *   +13 UB[3] block address (allocation hint; not used to seek)
         *
         * The area is an array with holes: a row freed by a deleted record
         * leaves its slot zeroed, so the rows are not one contiguous run at the
         * end of the block.  Measured over all 4457 bodies of the golden
         * volume, taking every non-empty row-shaped slot from BVR_RIDX_AREA_START
         * upward recovers exactly the +0x50 count for 4456 of them (the
         * exception is the two-level index body, FS.md 7.3), while stopping at
         * the first hole drops 27 rows across 6 bodies -- records that never
         * open and drawer children that never list.  BTRON.SYS's "English" body
         * is one: it keeps 4 child links and a 2084-byte record below the hole.
         */
        unsigned int found[REC_IDX_LEVEL0_MAX];
        int nfound = 0;
        for (unsigned int off = BVR_RIDX_AREA_START;
             off + BTRON_REC_IDX_SIZE <= bsize && nfound < REC_IDX_LEVEL0_MAX;
             off += BTRON_REC_IDX_SIZE) {
            const unsigned char *rp = buf + off;
            if (rp[0] == 0 && rp[1] == 0) continue;   /* unused slot or hole */
            found[nfound++] = off;
        }

        unsigned int n = 0;
        BLK hint0 = 0;                     /* block named by the first entry */
        for (int j = 0; j < nfound && n < REC_IDX_LEVEL0_MAX; j++) {
            const unsigned char *rp = buf + found[j];
            if (rp[0] != 0) continue;                 /* continuation entry */
            UW pos = rd_u32_le(rp + 4);
            BLK hint = (BLK)rd_u24_le(rp + 13);
            of->ridx[n].kind   = rd_u16_le(rp + 0);
            of->ridx[n].type   = rd_u16_le(rp + 2);
            /*
             * A link row has no payload of its own: +8 carries the target's
             * location data, not a size.  The engine's link convention is
             * size 0 with offset == target FID.
             */
            of->ridx[n].size   = (of->ridx[n].kind == 0x8000)
                               ? 0 : rd_u32_le(rp + 8);
            of->ridx[n].flags  = (UW)rp[12];
            /*
             * Link records carry the target FID in the low half of the position
             * word, exactly as a data record carries its block index there (the
             * high half is the record's other location half).  Reading the
             * whole word as an FID loses every link whose high half is non-zero:
             * measured on the golden volume, 1695 of 1695 such rows name a live
             * FID in the low half, and masking raises resolved links from 7224
             * to 8871 of 8919 rows (FS.md 7.4).
             */
            of->ridx[n].offset = (of->ridx[n].kind == 0x8000)
                               ? (pos & 0xFFFFu)
                               : (pos & 0xFFFFu) * bsize + (pos >> 16);
            if (hint0 == 0 && of->ridx[n].kind != 0x8000) hint0 = hint;
            n++;
        }
        /*
         * +0x50 is the header's own count of rows, continuation rows excluded,
         * and it agrees with the scan for every body but the two-level-indexed
         * one; use it to bound the count if the area ever holds more row-shaped
         * slots than the body declares.
         */
        UW decl50 = rd_u32_le(buf + 0x50);
        if (decl50 > 0 && decl50 <= REC_IDX_LEVEL0_MAX && n > decl50) n = (unsigned int)decl50;

        of->hdr.nrec = n;
        of->nrec     = n;
        of->data_used = of->hdr.total_size;
        (void)nblk_4c;

        /*
         * Data extent: the block the FID table names, which is the header's
         * successor for every body on a Cho-Kanji volume (FS.md 3.2).  A body
         * whose header was found at the named block itself is allocated away
         * from it (measured: BTRON.SYS at 111, Drawing Pad at 112 -- in both
         * cases the header's successor is another body's Real Header); for those
         * the block named by the index entry is the extent.
         */
        of->data_blk = 0;
        if (found_hdr_m1) {
            of->data_blk = blk;
            if (blk_is_elf) of->hdr.flags |= 0x0001;
        } else if (of->hdr.total_size > 0) {
            of->data_blk = blk + 1;
            if (hint0 && hint0 != of->data_blk) {
                unsigned char *probe = (unsigned char *)malloc(bsize);
                int successor_is_header = 0;
                if (probe && vol_read_blk(v, blk + 1, probe) == 0 &&
                    (memcmp(probe, "norT", 4) == 0 || memcmp(probe, "Tron", 4) == 0))
                    successor_is_header = 1;
                free(probe);
                if (successor_is_header) of->data_blk = hint0;
            }
        }

        /*
         * Record positions from the index are only meaningful when the entry
         * names the extent we settled on.  Otherwise the bodies concatenate
         * their record payloads in record order (FS.md 8), and the entry's
         * block/byte fields are stale allocation data.
         */
        if (n > 0 && hint0 != 0 && hint0 != of->data_blk) {
            UW cum = 0;
            for (unsigned int i = 0; i < n; i++) {
                if (of->ridx[i].kind == 0x8000) continue;   /* keeps target FID */
                of->ridx[i].offset = cum;
                cum += of->ridx[i].size;
            }
        }

        /*
         * A direct stream is a body whose whole payload is one record of the
         * stream record type 0x1F and that carries no link rows: 1925 of the
         * golden volume's 4457 bodies.  The proxy this used -- "the header was
         * found one block before the FID table entry" -- is true of every body on
         * a Cho-Kanji volume (FS.md 3.2), so it called 2934 of them streams, left
         * no body to read as a multi-record document, and because clu's `fs -r`
         * skips a body it thinks is a stream it stopped dumping record indexes
         * entirely.
         */
        {
            unsigned int ndata = 0, nstream = 0, nlink = 0;
            for (unsigned int i = 0; i < n; i++) {
                if (of->ridx[i].kind == 0x8000) nlink++;
                else { ndata++; if (of->ridx[i].kind == 0x9F00) nstream++; }
            }
            of->is_stream = (nlink == 0 && ndata == 1 && nstream == 1);
        }

        if (n == 0 && of->hdr.total_size > 0) {
            /* Index not in this block (indirect/2-level body): stream it whole. */
            of->hdr.nrec  = 1;
            of->nrec      = 1;
            of->ridx[0].kind   = 0;
            of->ridx[0].type   = 0;
            of->ridx[0].size   = of->hdr.total_size;
            of->ridx[0].offset = 0;
            of->ridx[0].flags  = 1;
            of->is_stream = 1;
        }
        of->hdr.data_blk = of->data_blk;
        free(buf);
        return 0;
    }

    /* Standard cleanroom volume */
    of->is_stream = 0;
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
    of->hdr.did        = rd_u32_be(p + 100);
    of->hdr.pdid       = rd_u32_be(p + 104);

    /*
     * Cap nrec defensively (NASA Rule 5), and to the rows that actually fit
     * between the 192-byte header and the end of this block: the in-core array
     * is larger than a cleanroom header, so a corrupt count must not read past
     * the block buffer.
     */
    UW cr_room = hdr_row_capacity(bsize, 0);
    if (of->hdr.nrec > cr_room) {
        of->hdr.nrec = cr_room;
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

    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) {
        if (vol_read_blk(v, blk, buf) != 0) {
            memset(buf, 0, bsize);
        }
        if (memcmp(buf, "norT", 4) != 0) {
            memcpy(buf, "Tron", 4);
        }
        wr_u16_le(buf + 4, of->hdr.flags);
        wr_u32_le(buf + 0x48, of->hdr.total_size);
        /* +0x4C is the number of blocks the body claims for record payloads. */
        wr_u32_le(buf + 0x4C, (of->hdr.total_size + bsize - 1) / bsize);
        wr_u32_le(buf + 0x64, of->hdr.mtime);

        bv_name_to_hdr(buf, (const char *)of->hdr.name);

        unsigned int crm = hdr_row_capacity(bsize, 1);
        unsigned int cnt = (of->nrec < crm) ? of->nrec : crm;
        UW nlink = 0;
        for (unsigned int i = 0; i < cnt; i++)
            if (of->ridx[i].kind == 0x8000) nlink++;
        /* +0x44 counts link rows, +0x50 counts every row but continuations. */
        wr_u32_le(buf + 0x44, nlink);
        wr_u32_le(buf + 0x50, cnt);

        /*
         * Clear the row area first: read_header_block() walks the whole area
         * and tolerates the zero holes a golden volume's deleted records leave
         * behind, so a row left over from a longer version of this file would
         * otherwise be read back as a record.
         */
        memset(buf + BVR_RIDX_AREA_START, 0, bsize - BVR_RIDX_AREA_START);
        for (unsigned int i = 0; i < cnt; i++) {
            /*
             * Ascending address == ascending record number, with the newest row
             * in the last slot of the block; that is how Cho-Kanji packs the
             * area, and the reverse of it silently reorders a file's records
             * across a write/read round trip.
             */
            unsigned char *rp = buf + bsize - (cnt - i) * BTRON_REC_IDX_SIZE;
            UW pos;
            if (of->ridx[i].kind == 0x8000) {
                pos = of->ridx[i].offset;            /* the target FID, low half */
            } else {
                /* read_header_block() decodes +4 as UH block, UH byte offset. */
                pos = of->ridx[i].offset / bsize
                    + ((of->ridx[i].offset % bsize) << 16);
            }
            /*
             * +0 is two bytes, not a word: byte0 is 0 and byte1 is the packed
             * record type with bit7 marking the row in use -- 0x80 for a link
             * (RT_LINK), 0x81 for a TAD data row (RT_TADDATA).  Rows the engine
             * creates carry kind 0 and keep their RT in `type`, so that byte has
             * to be packed here; writing the raw 16-bit kind leaves both bytes
             * zero, which read_header_block() reads back as an unused slot, and a
             * created file then comes home with no records at all.
             */
            rp[0] = (unsigned char)(of->ridx[i].kind & 0xFF);
            rp[1] = (unsigned char)(of->ridx[i].kind >> 8);
            if (rp[1] == 0) {
                rp[1] = (unsigned char)((of->ridx[i].type & 0x1F) | 0x80);
            }
            wr_u16_le(rp + 2, of->ridx[i].type);
            wr_u32_le(rp + 4, pos);
            wr_u32_le(rp + 8, of->ridx[i].size);
            rp[12] = (unsigned char)(of->ridx[i].flags & 0xFF);
            BLK rblk = of->data_blk;
            rp[13] = (unsigned char)(rblk & 0xFF);
            rp[14] = (unsigned char)((rblk >> 8) & 0xFF);
            rp[15] = (unsigned char)((rblk >> 16) & 0xFF);
        }
        int ret = vol_write_blk(v, blk, buf);
        free(buf);
        if (ret == 0) fil_hier_invalidate(v);
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
    wr_u32_be(p + 100, of->hdr.did);
    wr_u32_be(p + 104, of->hdr.pdid);

    /* Encode RecordIndex entries */
    UW crm = hdr_row_capacity(bsize, 0);
    unsigned int cnt = (of->nrec < crm) ? of->nrec : crm;
    for (unsigned int i = 0; i < cnt; i++) {
        unsigned char *rp = buf + HDR_RIDX_OFFSET + i * BTRON_REC_IDX_SIZE;
        wr_u16_be(rp +  0, of->ridx[i].kind);
        wr_u16_be(rp +  2, of->ridx[i].type);
        wr_u32_be(rp +  4, of->ridx[i].size);
        wr_u32_be(rp +  8, of->ridx[i].offset);
        wr_u32_be(rp + 12, of->ridx[i].flags);
    }
    /*
     * Every row the volume holds has just been rewritten, so any derived
     * parentage for this volume is now stale (FS.md 7.4 consequence 8).
     */
    int ret = vol_write_blk(v, blk, buf);
    free(buf);
    if (ret == 0) fil_hier_invalidate(v);
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

    int is_bv = (vol_fs_type(v) == FS_TYPE_BRIGHTV);

    for (FID i = 0; i < nfmax; i++) {
        if (vol_fid_refcount(v, i) == 0 && i != FID_ROOT) continue;
        if (!is_bv && vol_hash_get(v, i) != target_hash) continue;
        /* Hash match or B-right/V scan: do name compare */
        BLK hblk = vol_fid_get_blk(v, i);
        if (hblk == 0 || hblk == FID_INVALID) continue;

        char stored[64];
        if (is_bv) {
            BLK hdr = 0;
            if (bv_hdr_block(v, hblk, buf, &hdr) != 0) continue;
            bv_name_from_hdr(buf, stored, (int)sizeof(stored));
        } else {
            if (vol_read_blk(v, hblk, buf) != 0) continue;
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

static const char *strip_volume_prefix(Volume *v, const char *path)
{
    (void)v;
    if (!path) return "";
    const char *name = path;
    if (name[0] != '/') return name;

    const char *suffix = NULL;
    Volume *mv = vol_find_by_prefix(path, &suffix);
    if (mv && suffix) {
        name = suffix;
        while (*name == '/') name++;
        return name;
    }

    /* Fallback: skip volume label up to first slash */
    name++;
    const char *sl = name;
    while (*sl && *sl != '/') sl++;
    if (*sl == '/') {
        name = sl + 1;
        while (*name == '/') name++;
    } else {
        name = "";
    }
    return name;
}

static Volume *resolve_volume_from_path(const char *path)
{
    if (!path || !path[0]) return NULL;

    /* Relative path: resolve volume of current working directory */
    if (path[0] != '/') {
        return resolve_volume_from_path(g_cwd_path);
    }

    /* Path is "/" or empty: return volume of g_cwd_path, or default volume */
    if (path[1] == '\0') {
        Volume *cv = resolve_volume_from_path(g_cwd_path);
        if (cv) return cv;
        return g_sys_vol ? g_sys_vol : (vol_mounted_count() > 0 ? vol_get_mounted(0) : NULL);
    }

    Volume *mv = vol_find_by_prefix(path, NULL);
    if (mv) return mv;

    /* Extract volume prefix: e.g. from "/STORAGE_DEV/file.txt", prefix is "STORAGE_DEV" */
    char vname[64];
    const char *p = path + 1;
    size_t i = 0;
    while (*p && *p != '/' && *p != '#' && i < sizeof(vname) - 1) {
        vname[i++] = *p++;
    }
    vname[i] = '\0';

    Volume *v = vol_find_by_name(vname);
    if (v) return v;

    /* Fallback checks for legacy aliases */
    if (fs_strcasecmp(vname, "B-right") == 0 && g_chokanji_vol) return g_chokanji_vol;

    return NULL;
}

/* ── Path resolution ─────────────────────────────────────────────── */
/*
 * A BTRON path names a Virtual Body inside a container: a drawer is a Real
 * Body whose records are RT_LINKs, and each link carries the child's name and
 * target FID. So a path is resolved by walking those links from FID_ROOT.
 *
 * Volumes written before that model was implemented (and B-right/V Real Bodies,
 * which the FID table addresses directly) are reachable by bare name, so every
 * step falls back to the volume's flat name index rather than failing.
 */

/* Does this Real Body hold link records, i.e. is it a container? */
static int fid_is_container(Volume *v, FID fid)
{
    ID fd = opn_fil_fid(v, fid, F_READ);
    if (fd < 0) return 0;
    OpenFile *of = &g_open_files[(int)fd];
    int is_dir = 0;
    for (unsigned int i = 0; i < of->nrec; i++) {
        if (fil_rec_is_link(fd, (W)i)) { is_dir = 1; break; }
    }
    cls_fil(fd);
    return is_dir;
}

/* Look up one path segment among a container's link records. */
static FID container_lookup(Volume *v, FID dir_fid, const char *seg)
{
    ID fd = opn_fil_fid(v, dir_fid, F_READ);
    if (fd < 0) return FID_INVALID;
    OpenFile *of = &g_open_files[(int)fd];
    FID found = FID_INVALID;
    for (unsigned int i = 0; i < of->nrec && found == FID_INVALID; i++) {
        if (!fil_rec_is_link(fd, (W)i)) continue;
        FID cfid = FID_INVALID;
        char cname[64] = "";
        if (fil_get_rec_link_info(fd, (W)i, &cfid, cname, sizeof(cname), NULL) != 0) continue;
        if (cfid != FID_INVALID && fs_strcasecmp(cname, seg) == 0) found = cfid;
    }
    cls_fil(fd);
    return found;
}

/*
 * Resolve `name` (the volume-relative remainder of a path) to a FID.
 *
 * Each segment may carry a "#<fid>" anchor, which addresses that Real Body
 * directly and outranks the segment's text.  Listings hand the anchor out
 * (DIR_ENTRY.robj_id, which src/clu/vfs.c copies into VfsEntry.fid), so a path
 * assembled from a listing re-opens exactly the body that listing named.
 *
 * Names alone cannot promise that: a volume may hold several bodies with the
 * same name, and the flat name index returns the first of them.  On Cho-Kanji's
 * top level four drawers are called "src" and two "pcat", and "locale" names a
 * non-container too, so a name-only path onto any of them silently opened the
 * wrong body -- which is what sc's panes showed as the same folder repeating.
 * An anchor whose FID is no longer alive falls back to the segment's name, so a
 * remembered path still works after the body it pointed at was deleted.
 */
static FID resolve_obj_fid(Volume *v, const char *name)
{
    if (!v) return FID_INVALID;
    if (name[0] == '\0' || strcmp(name, ".") == 0) return FID_ROOT;

    FID cur = FID_ROOT;
    const char *p = name;
    while (*p) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        char seg[64];
        size_t n = 0;
        while (*p && *p != '/' && *p != '#' && n < sizeof(seg) - 1) seg[n++] = *p++;
        seg[n] = '\0';

        FID next = FID_INVALID;
        if (*p == '#') {
            const char *np = p + 1;
            if (*np >= '0' && *np <= '9') {
                FID fid = 0;
                while (*np >= '0' && *np <= '9') fid = fid * 10 + (FID)(*np++ - '0');
                if (vol_fid_refcount(v, fid) || fid == FID_ROOT) next = fid;
                p = np;
            }
            while (*p && *p != '/') p++;   /* rest of this segment: already used or dead */
        }
        if (next == FID_INVALID) {
            next = container_lookup(v, cur, seg);
            if (next == FID_INVALID) next = find_fid_by_name(v, seg);
            if (next == FID_INVALID) return FID_INVALID;
        }
        cur = next;
    }
    if (cur != FID_ROOT) return cur;

    /* Nothing matched segment by segment: try the whole string as one name. */
    return find_fid_by_name(v, name);
}

static FID resolve_path_fid(const char *path)
{
    Volume *v = resolve_volume_from_path(path);
    if (!v) return FID_INVALID;
    return resolve_obj_fid(v, strip_volume_prefix(v, path));
}

/* ── opn_fil ─────────────────────────────────────────────────────── */
ID opn_fil(const char *path, UW mode)
{
    Volume *v = resolve_volume_from_path(path);
    if (!v) return (ID)-1;

    FID fid = resolve_obj_fid(v, strip_volume_prefix(v, path));
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
    Volume *v = resolve_volume_from_path(path);
    if (!v || !path) return (ID)-1;

    const char *name = strip_volume_prefix(v, path);

    if (find_fid_by_name(v, name) != FID_INVALID)
        return (ID)-1; /* already exists */

    FID fid = vol_fid_alloc(v);
    if (fid == FID_INVALID) return (ID)-1;

    /*
     * On a Cho-Kanji volume a body's header and its first data block are
     * adjacent and the FID table names the *data* block -- that is how all 4457
     * bodies of the golden volume are laid out (FS.md 3.2), and bv_hdr_block()
     * resolves every one of them as ptr-1.  Registering the header block instead
     * would make a created body readable only under a second convention.
     * A clean-room volume keeps the §3.2 header-block pointer.
     */
    int bv = (vol_fs_type(v) == FS_TYPE_BRIGHTV);
    BLK hblk, dblk = 0;
    if (bv) {
        if (vol_alloc_block_pair(v, &hblk, &dblk) != 0) {
            vol_fid_free(v, fid);
            return (ID)-1;
        }
    } else {
        hblk = vol_alloc_block(v);
        if (hblk == FID_INVALID) {
            vol_fid_free(v, fid);
            return (ID)-1;
        }
    }

    /* Find a free open-file slot */
    int slot = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        vol_free_block(v, hblk);
        if (dblk) vol_free_block(v, dblk);
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
    of->data_blk = dblk;
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
    of->hdr.data_blk   = dblk;
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
    vol_fid_set(v, fid, bv ? dblk : hblk, 1);
    vol_hash_set(v, fid, vol_name_hash(name));
    vol_mark_dirty(v);
    fil_hier_invalidate(v);   /* the table gained a body after the header write */

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
    Volume *v = resolve_volume_from_path(path);
    if (!v || !path) return (ER)-1;

    const char *name = strip_volume_prefix(v, path);

    FID fid = resolve_obj_fid(v, name);
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
    fil_hier_invalidate(v);   /* the FID and its blocks are gone from the tables */
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

/* ── Cached hierarchy ─────────────────────────────────────────────── */
/*
 * FS.md 3.2 "Root namespace", 7.4 consequence 8.
 *
 * A B-right/V Real Body header encodes no parent, so the only parentage in the
 * volume is other bodies' RT_LINK rows.  FID 0 is the root drawer -- its header
 * is the body named after the volume label and its rows are the top level -- so
 * the root list is simply the FIDs whose naming drawer is FID 0.
 *
 * Reading that per listing means re-reading every one of the 4457 bodies again,
 * and doing it separately in two consumers is how clu's tree and sc's pane came
 * to disagree. So the engine resolves it once per volume and keeps it until a
 * mutation could have changed it.
 *
 * Built for B-right/V volumes only; a cleanroom volume's FID 0 drawer is read
 * through the same container path and needs no cache.
 */
typedef struct {
    Volume         *vol;
    UW              seq;         /* vol_mount_seq() this snapshot describes */
    UW              nfmax;
    UW              nblk;
    BLK            *hdr;         /* [nfmax] header block, 0 = not a live body */
    FID            *parent;      /* [nfmax] drawer that reaches it, FID_INVALID = unreachable */
    unsigned char  *is_dir;      /* [nfmax] carries at least one link row */
    UW             *out_start;   /* [nfmax+1] row-offset of a body's first child */
    FID            *out_dst;     /* [out_start[nfmax]] every link-row target, by source */
    FID            *root;        /* [nroot] the root drawer's children, in row order */
    UW              nroot;
} FilHier;

/* Volumes browsed at once are counted, and a listing touches one or two. */
#define FIL_HIER_SLOTS 4

/*
 * A body that declares more rows than its header area holds keeps the remainder
 * in level-1 index blocks (FS.md 7.3).  Measured on fid 139 `index`, the only
 * such body of the golden volume's 4457: its header's in-use slots are at
 * +0x6C0/+0x6D0, each holding two 8-byte (UW row count, UW block) entries, and
 * the block word carries the same one-block bias as the FID table, so the rows
 * live at `block - 1`.  The four entries declare 20 + 402 + 510 + 510 = 1442
 * rows, which is exactly its +0x50, and 1440 of them are RT_LINK rows naming the
 * 573 bodies no reached drawer names -- that index is the only entrance to the
 * B-Book subtree, so a hierarchy that ignores it silently loses a whole tree.
 *
 * A slot the level-0 scan skips for a non-zero byte 0 is the only place an entry
 * is read from, which keeps small data rows (whose size word could otherwise
 * look like a count) out of it.
 */
#define BVR_IDX_ENTRIES_PER_SLOT 2
#define BVR_RIDX_CONT_MAX        16   /* continuation slots inspected per body */
#define NELMS(a)                 (sizeof(a) / sizeof((a)[0]))

/* Interleaved (source, target) row pairs, grown as rows are found. */
typedef struct {
    FID      *edges;
    UW        cap;
    UW        n;
    Volume   *v;
    UW        nfmax;
    UW       *last_src;    /* [nfmax] source that last claimed this target */
    int       failed;
} EdgeVec;

/*
 * One RT_LINK row -> one edge, but never two for the same drawer: Cho-Kanji
 * repeats a link once per record position it occupies (measured on fid 139,
 * whose 1440 index rows name 573 bodies), and a listing that replays the
 * repetitions shows the same folder several times over.  A source's rows are all
 * added together, so remembering the last source per target is enough.
 */
static void edgevec_add(EdgeVec *ev, FID src, const unsigned char *rp)
{
    FID tf = (FID)(rd_u32_le(rp + 4) & 0xFFFFu);
    if (tf == src || tf >= ev->nfmax) return;
    if (vol_fid_refcount(ev->v, tf) == 0 && tf != FID_ROOT) return;
    if (ev->last_src[tf] == (UW)src + 1) return;
    ev->last_src[tf] = (UW)src + 1;
    if (ev->n == ev->cap) {
        /* No realloc on a freestanding target -- the kernel heap is
         * Imalloc/Icalloc/Ifree (libc_shim.h), so grow by copy. */
        UW newcap = ev->cap * 2;
        FID *grown = (FID *)malloc((size_t)newcap * 2 * sizeof(FID));
        if (!grown) { ev->failed = 1; return; }
        memcpy(grown, ev->edges, (size_t)ev->n * 2 * sizeof(FID));
        free(ev->edges);
        ev->edges = grown;
        ev->cap   = newcap;
    }
    ev->edges[ev->n * 2]     = src;
    ev->edges[ev->n * 2 + 1] = tf;
    ev->n++;
}

static int bv_index_blocks(const unsigned char *hdrbuf, UW bsize, BLK nblk,
                           unsigned int off, BLK *blks, unsigned int *counts, int max)
{
    int n = 0;
    for (int k = 0; k < BVR_IDX_ENTRIES_PER_SLOT && off + BTRON_REC_IDX_SIZE <= bsize; k++) {
        const unsigned char *ep = hdrbuf + off + k * 8;
        unsigned int cnt = rd_u32_le(ep);
        BLK b = (BLK)rd_u32_le(ep + 4);
        if (cnt == 0 || cnt > bsize / BTRON_REC_IDX_SIZE) continue;
        if (b == 0 || b > nblk) continue;
        if (n < max) { blks[n] = b - 1; counts[n] = cnt; n++; }
    }
    return n;
}

static FilHier s_hier[FIL_HIER_SLOTS];

static void hier_release(FilHier *h)
{
    free(h->hdr);
    free(h->parent);
    free(h->is_dir);
    free(h->out_start);
    free(h->out_dst);
    free(h->root);
    memset(h, 0, sizeof(*h));
}

void fil_hier_invalidate(Volume *v)
{
    for (int i = 0; i < FIL_HIER_SLOTS; i++) {
        if (!s_hier[i].vol) continue;
        if (!v || s_hier[i].vol == v) hier_release(&s_hier[i]);
    }
}

/*
 * One pass over the live FIDs collects every RT_LINK row as a drawer -> target
 * edge; a breadth-first walk from FID 0 then gives each body the parent that
 * reaches it from the root.
 *
 * Choosing the parent root-first rather than as "the lowest FID whose row names
 * it" is what makes the tree navigable.  Cho-Kanji lets a link name any body,
 * including one that names its own drawer, and measured on the golden volume
 * first-wins parentage left 12 bodies as their own ancestor and 561 more below
 * such a cycle -- folders sc could not climb out of.  A spanning tree grown from
 * the root is acyclic by construction; the rows it does not use stay on disk as
 * the additional links they are.
 */
static FilHier *hier_build(Volume *v)
{
    UW nfmax = vol_nfmax(v);
    UW bsize = vol_block_size(v);
    UW nblk  = vol_total_blocks(v);
    if (!v || nfmax == 0 || nblk == 0) return (FilHier *)0;

    int slot = -1;
    for (int i = 0; i < FIL_HIER_SLOTS; i++) {
        if (!s_hier[i].vol) { slot = i; break; }
    }
    if (slot < 0) {
        /* All slots busy: reuse the one for a volume that is no longer mounted. */
        for (int i = 0; i < FIL_HIER_SLOTS; i++) {
            if (s_hier[i].seq != vol_mount_seq(s_hier[i].vol)) { slot = i; break; }
        }
    }
    if (slot < 0) slot = 0;
    FilHier *h = &s_hier[slot];
    hier_release(h);

    h->vol  = v;
    h->seq  = vol_mount_seq(v);
    h->nfmax = nfmax;
    h->nblk  = nblk;
    h->hdr         = (BLK *)calloc(nfmax, sizeof(BLK));
    h->parent      = (FID *)calloc(nfmax, sizeof(FID));
    h->is_dir      = (unsigned char *)calloc(nfmax, 1);
    h->out_start   = (UW *)calloc(nfmax + 1, sizeof(UW));
    h->root        = (FID *)calloc(nfmax, sizeof(FID));
    unsigned char *seen = (unsigned char *)calloc(nfmax, 1);
    FID *queue     = (FID *)malloc(nfmax * sizeof(FID));
    unsigned char *buf = (unsigned char *)malloc(bsize);
    unsigned char *ibuf = (unsigned char *)malloc(bsize);
    if (!h->hdr || !h->parent || !h->is_dir || !h->out_start || !h->root ||
        !seen || !queue || !buf || !ibuf) {
        free(seen); free(queue); free(buf); free(ibuf);
        hier_release(h);
        return (FilHier *)0;
    }
    for (UW f = 0; f < nfmax; f++) h->parent[f] = FID_INVALID;

    /* Interleaved (source, target) pairs, grown as rows are found. */
    EdgeVec ev;
    ev.edges = (FID *)malloc(8192 * 2 * sizeof(FID));
    ev.cap   = 8192;
    ev.n     = 0;
    ev.v     = v;
    ev.nfmax = nfmax;
    ev.last_src = (UW *)calloc(nfmax, sizeof(UW));
    ev.failed = 0;
    if (!ev.edges || !ev.last_src) {
        free(ev.last_src);
        free(ev.edges); free(seen); free(queue); free(buf); free(ibuf);
        hier_release(h);
        return (FilHier *)0;
    }

    for (UW f = 0; f < nfmax; f++) {
        FID fid = (FID)f;
        if (vol_fid_refcount(v, fid) == 0 && fid != FID_ROOT) continue;
        BLK b = vol_fid_get_blk(v, fid);
        if (b == 0 || b >= nblk) continue;

        BLK hdr_blk = 0;
        if (bv_hdr_block(v, b, buf, &hdr_blk) != 0) continue;   /* not a Real Body */
        h->hdr[fid] = hdr_blk;

        /*
         * Link rows only: byte0 == 0 keeps continuation rows out (FS.md 7.4), and
         * byte1 == 0x80 is RT_LINK with the in-use bit. The target FID is the low
         * half of +4 -- reading the whole word names nothing (consequence 1).
         *
         * A body whose +0x50 exceeds the rows its header area holds keeps the rest
         * in level-1 index blocks; those are followed below, or the whole subtree
         * hanging off that index is lost (FS.md 7.3).
         */
        unsigned int decl50 = rd_u32_le(buf + 0x50);
        unsigned int lvl0_used = 0;
        UW cont_off[BVR_RIDX_CONT_MAX];
        int ncont = 0;

        for (UW off = BVR_RIDX_AREA_START; off + BTRON_REC_IDX_SIZE <= bsize; off += BTRON_REC_IDX_SIZE) {
            const unsigned char *rp = buf + off;
            if (rp[0] == 0 && rp[1] == 0) continue;
            lvl0_used++;
            if (rp[0] != 0) {
                if (ncont < BVR_RIDX_CONT_MAX) cont_off[ncont++] = off;
                continue;
            }
            if (rp[1] != 0x80) continue;
            edgevec_add(&ev, fid, rp);
        }

        if (decl50 > lvl0_used && ncont > 0) {
            BLK iblk[BVR_RIDX_CONT_MAX * BVR_IDX_ENTRIES_PER_SLOT];
            unsigned int icnt[NELMS(iblk)];
            int ni = 0;
            for (int c = 0; c < ncont && ni < (int)NELMS(iblk); c++)
                ni += bv_index_blocks(buf, bsize, nblk, cont_off[c],
                                      iblk + ni, icnt + ni, (int)NELMS(iblk) - ni);
            for (int c = 0; c < ni; c++) {
                if (iblk[c] >= nblk || vol_read_blk(v, iblk[c], ibuf) != 0) continue;
                if (memcmp(ibuf, "norT", 4) == 0 || memcmp(ibuf, "Tron", 4) == 0) continue;
                for (unsigned int r = 0; r < icnt[c]; r++) {
                    const unsigned char *rp = ibuf + r * BTRON_REC_IDX_SIZE;
                    if (rp[0] != 0 || rp[1] != 0x80) continue;
                    edgevec_add(&ev, fid, rp);
                }
            }
        }
    }
    free(buf);
    free(ibuf);

    FID *edges = ev.edges;
    UW   nedge = ev.n;
    free(ev.last_src);
    if (ev.failed) {
        free(edges); free(seen); free(queue);
        hier_release(h);
        return (FilHier *)0;
    }

    /* Adjacency by source, so the walk costs rows rather than FIDs x rows. */
    for (UW i = 0; i < nedge; i++) h->out_start[edges[i * 2]]++;
    UW acc = 0;
    for (UW f = 0; f < nfmax; f++) { UW c = h->out_start[f]; h->out_start[f] = acc; acc += c; }
    h->out_start[nfmax] = acc;
    h->out_dst = (FID *)malloc((nedge ? nedge : 1) * sizeof(FID));
    UW *cur = (UW *)malloc(nfmax * sizeof(UW));
    if (!h->out_dst || !cur) {
        free(cur); free(edges); free(seen); free(queue);
        hier_release(h);
        return (FilHier *)0;
    }
    memcpy(cur, h->out_start, nfmax * sizeof(UW));
    for (UW i = 0; i < nedge; i++) h->out_dst[cur[edges[i * 2]]++] = edges[i * 2 + 1];
    free(cur);
    free(edges);

    for (UW f = 0; f < nfmax; f++)
        if (h->out_start[f] < h->out_start[f + 1]) h->is_dir[f] = 1;

    UW qh = 0, qt = 0;
    seen[FID_ROOT] = 1;
    queue[qt++] = FID_ROOT;
    while (qh < qt) {
        FID f = queue[qh++];
        for (UW k = h->out_start[f]; k < h->out_start[f + 1]; k++) {
            FID c = h->out_dst[k];
            if (c >= nfmax || seen[c]) continue;
            seen[c] = 1;
            h->parent[c] = f;
            queue[qt++] = c;
        }
    }
    free(seen);
    free(queue);

    /* Root children are the FID 0 drawer's own rows, in the order it carries them
     * (FS.md 3.2); a row naming a body the drawer already named lists once. */
    for (UW k = h->out_start[FID_ROOT]; k < h->out_start[FID_ROOT + 1]; k++) {
        FID c = h->out_dst[k];
        if (c >= nfmax || h->parent[c] != FID_ROOT) continue;
        int dup = 0;
        for (UW i = 0; i < h->nroot; i++) if (h->root[i] == c) { dup = 1; break; }
        if (!dup) h->root[h->nroot++] = c;
    }
    return h;
}

/* The current snapshot for v, built if absent or stale. */
static const FilHier *hier_get(Volume *v)
{
    if (!v || vol_fs_type(v) != FS_TYPE_BRIGHTV) return (FilHier *)0;
    UW seq = vol_mount_seq(v);
    for (int i = 0; i < FIL_HIER_SLOTS; i++) {
        if (s_hier[i].vol == v && s_hier[i].seq == seq) return &s_hier[i];
    }
    return hier_build(v);
}

int fil_hier_nroot(Volume *v)
{
    const FilHier *h = hier_get(v);
    return h ? (int)h->nroot : -1;
}

FID fil_hier_root_fid(Volume *v, int i)
{
    const FilHier *h = hier_get(v);
    if (!h || i < 0 || (UW)i >= h->nroot) return FID_INVALID;
    return h->root[i];
}

BLK fil_hier_hdr_blk(Volume *v, FID fid)
{
    const FilHier *h = hier_get(v);
    if (!h || fid >= h->nfmax) return 0;
    return h->hdr[fid];
}

int fil_hier_is_dir(Volume *v, FID fid)
{
    const FilHier *h = hier_get(v);
    if (!h || fid >= h->nfmax) return 0;
    return h->is_dir[fid];
}

FID fil_hier_parent(Volume *v, FID fid)
{
    const FilHier *h = hier_get(v);
    if (!h || fid >= h->nfmax) return FID_INVALID;
    return h->parent[fid];
}

int fil_hier_is_root(Volume *v, FID fid)
{
    const FilHier *h = hier_get(v);
    if (!h) return 0;
    for (UW i = 0; i < h->nroot; i++) {
        if (h->root[i] == fid) return 1;
    }
    return 0;
}

/*
 * The children a drawer's link rows name, in row order, already free of the
 * per-position repetitions (hier_build's edge vector dedupes them).  This is the
 * complete child list: it includes rows that live in a level-1 index block,
 * which a body's in-core record index cannot hold (FS.md 7.3).
 */
int fil_hier_nchild(Volume *v, FID fid)
{
    const FilHier *h = hier_get(v);
    if (!h || fid >= h->nfmax) return -1;
    return (int)(h->out_start[fid + 1] - h->out_start[fid]);
}

FID fil_hier_child(Volume *v, FID fid, int i)
{
    const FilHier *h = hier_get(v);
    if (!h || fid >= h->nfmax) return FID_INVALID;
    UW k = h->out_start[fid];
    if (i < 0 || k + (UW)i >= h->out_start[fid + 1]) return FID_INVALID;
    return h->out_dst[k + i];
}

/* ── opn_dir / rd_dir / cls_dir ──────────────────────────────────── */
/*
 * BTRON's directory model: a directory is a container Real Body whose records
 * are RT_LINK Virtual Bodies.  On a cleanroom volume rd_dir() replays those
 * records; on a B-right/V volume the drawer's names and links live in the rows,
 * so rd_dir() hands out the hierarchy snapshot's child list -- the same list
 * clu's fs views print, complete through a level-1 index (FS.md 7.3), and the
 * only one that proves parentage.  The volume-wide FID-table scan below survives
 * only for a cleanroom volume whose FID 0 holds no links.
 */
typedef struct {
    UW next_fid;      /* root-list cursor, or FID cursor in scan mode   */
    UW next_rec;      /* child cursor: snapshot index or record number  */
    UW nrec;          /* number of children to hand out                 */
    BOOL scoped;      /* TRUE = walk dir_fid's children                 */
    BOOL from_hier;   /* TRUE = children come from the hierarchy snapshot */
    FID dir_fid;      /* container this directory was opened for       */
    Volume *vol;
} DirState;
static DirState g_dirs[16];
static int      g_dir_used[16];

ID opn_dir(const char *path)
{
    Volume *v = resolve_volume_from_path(path);
    if (!v) return (ID)-1;

    FID dir_fid = resolve_path_fid(path);
    if (dir_fid == FID_INVALID) return (ID)-1;

    /*
     * A B-right/V drawer's complete child list is the hierarchy snapshot's: it
     * holds every link row, including the ones filed in a level-1 index block
     * that no in-core record index can reach (FS.md 7.3), and it folds the
     * repetitions of one link into a single entry.  Names are read the same way
     * the record path reads them -- from the target Real Body's header -- so the
     * two listings cannot drift apart.
     */
    int nchild = (vol_fs_type(v) == FS_TYPE_BRIGHTV) ? fil_hier_nchild(v, dir_fid) : -1;
    if (nchild > 0) {
        for (int i = 0; i < 16; i++) {
            if (g_dir_used[i]) continue;
            g_dir_used[i] = 1;
            memset(&g_dirs[i], 0, sizeof(g_dirs[i]));
            g_dirs[i].nrec      = (UW)nchild;
            g_dirs[i].scoped    = 1;
            g_dirs[i].from_hier = 1;
            g_dirs[i].dir_fid   = dir_fid;
            g_dirs[i].vol       = v;
            return (ID)(0x1000 + i);
        }
        return (ID)-1;
    }

    ID fd = opn_fil_fid(v, dir_fid, F_READ);
    if (fd < 0) return (ID)-1;
    OpenFile *of = &g_open_files[(int)fd];
    UW nrec = of->nrec;
    int scoped = 0;
    for (unsigned int i = 0; i < nrec; i++) {
        if (fil_rec_is_link(fd, (W)i)) { scoped = 1; break; }
    }
    cls_fil(fd);
    if (!scoped && dir_fid != FID_ROOT) return (ID)-1; /* not a directory */

    for (int i = 0; i < 16; i++) {
        if (!g_dir_used[i]) {
            g_dir_used[i] = 1;
            memset(&g_dirs[i], 0, sizeof(g_dirs[i]));
            g_dirs[i].nrec     = nrec;
            g_dirs[i].scoped   = scoped;
            g_dirs[i].dir_fid  = dir_fid;
            g_dirs[i].vol      = v;
            return (ID)(0x1000 + i);
        }
    }
    return (ID)-1;
}

/* Fill size/attr for one entry from its Real Body header. */
static void dir_entry_stat(Volume *v, DIR_ENTRY *entry)
{
    FID fid = (FID)entry->robj_id;
    ID cfd = opn_fil_fid(v, fid, F_READ);
    if (cfd < 0) { entry->attr = 0; entry->size = 0; return; }
    OpenFile *co = &g_open_files[(int)cfd];
    entry->size = co->hdr.total_size;
    entry->attr = co->hdr.flags;
    int is_dir;
    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) {
        is_dir = fil_hier_is_dir(v, fid);   /* the snapshot is the drawer test */
    } else {
        is_dir = 0;
        for (unsigned int k = 0; k < co->nrec; k++) {
            if (fil_rec_is_link(cfd, (W)k)) { is_dir = 1; break; }
        }
    }
    cls_fil(cfd);
    if (is_dir) entry->attr |= OBJ_DIRECTORY;
}

/* The name a body's own Real Body header carries; 0 if it has none. */
static int body_name(Volume *v, FID fid, char *out, size_t out_max)
{
    ID fd = opn_fil_fid(v, fid, F_READ);
    if (fd < 0 || out_max == 0) { if (out_max) out[0] = '\0'; return 0; }
    size_t n = strlen((const char *)g_open_files[(int)fd].hdr.name);
    if (n >= out_max) n = out_max - 1;
    memcpy(out, g_open_files[(int)fd].hdr.name, n);
    out[n] = '\0';
    cls_fil(fd);
    return n != 0;
}

ER rd_dir(ID dir_id, DIR_ENTRY *entry)
{
    int slot = (int)(dir_id - 0x1000);
    if (slot < 0 || slot >= 16 || !g_dir_used[slot]) return (ER)-1;
    Volume *v = g_dirs[slot].vol ? g_dirs[slot].vol : g_sys_vol;
    if (!v) return (ER)-1;

    /* ── Scoped: hand out the container's children ───────────────── */
    if (g_dirs[slot].scoped) {
        FID dir_fid = g_dirs[slot].dir_fid;

        if (g_dirs[slot].from_hier) {
            while (g_dirs[slot].next_rec < g_dirs[slot].nrec) {
                UW i = g_dirs[slot].next_rec++;
                FID cfid = fil_hier_child(v, dir_fid, (int)i);
                if (cfid == FID_INVALID) continue;

                char cname[64];
                if (!body_name(v, cfid, cname, sizeof(cname))) continue;

                memset(entry, 0, sizeof(*entry));
                entry->robj_id = (ID)cfid;
                size_t nlen = strlen(cname);
                if (nlen >= sizeof(entry->name)) nlen = sizeof(entry->name) - 1;
                memcpy(entry->name, cname, nlen);
                entry->name[nlen] = '\0';
                dir_entry_stat(v, entry);
                return (ER)0;
            }
            return (ER)1; /* E_EOF / end of directory */
        }

        while (g_dirs[slot].next_rec < g_dirs[slot].nrec) {
            UW i = g_dirs[slot].next_rec++;
            ID fd = opn_fil_fid(v, dir_fid, F_READ);
            if (fd < 0) return (ER)1;
            if (!fil_rec_is_link(fd, (W)i)) { cls_fil(fd); continue; }

            FID cfid = FID_INVALID;
            char cname[64] = "";
            ER er = fil_get_rec_link_info(fd, (W)i, &cfid, cname, sizeof(cname), NULL);
            cls_fil(fd);
            if (er != 0 || cfid == FID_INVALID) continue;

            memset(entry, 0, sizeof(*entry));
            entry->robj_id = (ID)cfid;
            size_t nlen = strlen(cname);
            if (nlen == 0) {
                /* Undecodable link name: show the Real Body's own name. */
                if (body_name(v, cfid, cname, sizeof(cname)))
                    nlen = strlen(cname);
            }
            if (nlen >= sizeof(entry->name)) nlen = sizeof(entry->name) - 1;
            memcpy(entry->name, cname, nlen);
            entry->name[nlen] = '\0';
            if (!entry->name[0]) continue;
            dir_entry_stat(v, entry);
            return (ER)0;
        }
        return (ER)1; /* E_EOF / end of directory */
    }

    /*
     * ── Root of a B-right/V volume ─────────────────────────────────
     * FID 0 is the volume's root drawer: its header is the body named after the
     * volume and its link rows are the top level (FS.md 3.2).  opn_dir() opens it
     * as a container, so the scoped replay above is the whole answer and there is
     * nothing to enumerate here.  Falling through to the volume-wide FID dump
     * below is what used to print 307 entries with no parentage -- every body in
     * the namespace, alias FIDs included.
     */
    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) return (ER)1;

    /* ── Volume-wide scan: a cleanroom root with no link records ───── */
    UW nfmax = vol_nfmax(v);
    UW bsize = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) return (ER)-1;

    while (g_dirs[slot].next_fid < nfmax) {
        FID fid = g_dirs[slot].next_fid++;
        if (vol_fid_refcount(v, fid) == 0 && fid != FID_ROOT) continue;
        BLK hblk = vol_fid_get_blk(v, fid);
        if (hblk == 0 || hblk == FID_INVALID) continue;

        if (vol_read_blk(v, hblk, buf) != 0) continue;

        entry->robj_id = (ID)fid;
        entry->attr = ((UW)buf[0] << 8) | buf[1]; /* flags BE */
        entry->size = ((UW)buf[28] << 24) | ((UW)buf[29] << 16) |
                      ((UW)buf[30] << 8)  | buf[31];
        char nm[41];
        memcpy(nm, buf + 32, 40);
        nm[40] = '\0';
        int ni = 0;
        while (nm[ni] && ni < 63) { entry->name[ni] = nm[ni]; ni++; }
        entry->name[ni] = '\0';
        /* The root Real Body is the drawer itself, not an entry of it. */
        if (fid == FID_ROOT && vol_name(v) && strcmp(entry->name, vol_name(v)) == 0)
            continue;
        if (fid_is_container(v, fid)) entry->attr |= OBJ_DIRECTORY;
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

/* ── fil_is_stream ───────────────────────────────────────────────── */
int fil_is_stream(ID fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return 0;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used) return 0;
    return (int)of->is_stream;
}

/*
 * Is this RecordIndex entry a link (Virtual Body) entry?
 *
 * FS.md 7.1 defines the entry kind by its first two bytes: a link index entry
 * is byte0==0, byte1==0x80, a normal index entry is byte0==0 with byte1 holding
 * the packed record type. Read little-endian (B-right/V) that is kind==0x8000
 * for links versus 0x8100/0x8a00/0x8f00... for TAD data records; big-endian,
 * kind==0x0080. Reading a Cho-Kanji type byte as `type` therefore yields 0 for
 * ordinary records, so `type == RT_LINK` alone must not decide anything on that
 * format — it would present every data file as a drawer full of dead links.
 */
static int ridx_is_link_entry(const Volume *v, const RecordIndex *ri)
{
    if (ri->size == 0 && ri->kind == 0 && ri->type == 0) return 0;
    if (vol_is_le(v)) return ri->kind == 0x8000;
    if (ri->kind == 0x0080) return 1;
    return ri->type == RT_LINK && ri->size >= 16;
}

/* Does this FID name a Real Body that exists in this volume? */
static int fid_exists(Volume *v, FID fid)
{
    if (fid == FID_INVALID) return 0;
    if (fid == FID_ROOT) return 1;
    return fid < vol_nfmax(v) && vol_fid_refcount(v, fid) != 0;
}

/* ── fil_rec_is_link ─────────────────────────────────────────────── */
int fil_rec_is_link(ID fd, W rec_idx)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES) return 0;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used || rec_idx < 0 || (UW)rec_idx >= of->nrec) return 0;
    Volume *v = of_vol(of);
    if (!v) return 0;

    const RecordIndex *ri = &of->ridx[rec_idx];
    if (!ridx_is_link_entry(v, ri)) return 0;
    if (!vol_is_le(v)) return 1;

    /*
     * B-right/V carries the target FID in the entry's offset field with no
     * payload of its own. Requiring that FID to exist rejects the misparsed
     * entries whose field is really a block address or file content.
     */
    return ri->size == 0 && fid_exists(v, (FID)ri->offset);
}

/* ── fil_get_rec_link_info ───────────────────────────────────────── */
ER fil_get_rec_link_info(ID fd, W rec_idx, FID *out_fid, char *out_name, size_t name_max, UH attrs[5])
{
    if (out_fid) *out_fid = FID_INVALID;
    if (out_name && name_max > 0) out_name[0] = '\0';
    if (attrs) memset(attrs, 0, 5 * sizeof(UH));

    if (fd < 0 || fd >= MAX_OPEN_FILES) return (ER)-1;
    OpenFile *of = &g_open_files[(int)fd];
    if (!of->used || rec_idx < 0 || (UW)rec_idx >= of->nrec) return (ER)-1;

    Volume *v = of_vol(of);
    if (!v) return (ER)-1;

    RecordIndex *ri = &of->ridx[rec_idx];
    if (!fil_rec_is_link(fd, rec_idx)) return (ER)-1;

    FID link_fid = FID_INVALID;
    char link_name[64] = "";

    if (ri->size >= 16) {
        ID rec = opn_rec(fd, rec_idx, 0x0001);
        if (rec >= 0) {
            unsigned char pbuf[16];
            W got = 0;
            rd_rec(rec, pbuf, 16, &got);
            if (got >= 16) {
                if (vol_is_le(v)) {
                    link_fid = (FID)((unsigned int)pbuf[0] | ((unsigned int)pbuf[1] << 8) |
                                     ((unsigned int)pbuf[2] << 16) | ((unsigned int)pbuf[3] << 24));
                    if (attrs) {
                        for (int a = 0; a < 5; a++)
                            attrs[a] = (UH)(pbuf[4 + a * 2] | (pbuf[4 + a * 2 + 1] << 8));
                    }
                    unsigned short nlen = (unsigned short)(pbuf[14] | (pbuf[15] << 8));
                    if (nlen > 0 && nlen < sizeof(link_name)) {
                        W got2 = 0;
                        rd_rec(rec, link_name, (W)nlen, &got2);
                        link_name[got2] = '\0';
                    }
                } else {
                    link_fid = ((FID)pbuf[0] << 24) | ((FID)pbuf[1] << 16) |
                               ((FID)pbuf[2] << 8)  | (FID)pbuf[3];
                    if (attrs) {
                        for (int a = 0; a < 5; a++)
                            attrs[a] = ((UH)pbuf[4 + a * 2] << 8) | pbuf[4 + a * 2 + 1];
                    }
                    unsigned short nlen = ((unsigned short)pbuf[14] << 8) | pbuf[15];
                    if (nlen > 0 && nlen < sizeof(link_name)) {
                        W got2 = 0;
                        rd_rec(rec, link_name, (W)nlen, &got2);
                        link_name[got2] = '\0';
                    }
                }
            }
            cls_rec(rec);
        }
    }

    /*
     * B-right/V keeps the target FID in the entry itself. Masking a field that
     * is not an FID into range only manufactures links that lead nowhere.
     */
    if (link_fid == FID_INVALID && ri->offset > 0 && ri->offset < vol_nfmax(v))
        link_fid = (FID)ri->offset;

    if (link_fid == FID_INVALID || !fid_exists(v, link_fid)) return (ER)-1;

    /* Fallback: if link_name is empty, query target Real Body's header name */
    if (link_name[0] == '\0' && link_fid != (FID)0) {
        ID target_fd = opn_fil_fid(v, link_fid, 0x0001);
        if (target_fd >= 0) {
            strncpy(link_name, (const char *)g_open_files[(int)target_fd].hdr.name, sizeof(link_name) - 1);
            link_name[sizeof(link_name) - 1] = '\0';
            cls_fil(target_fd);
        }
    }

    if (out_fid) *out_fid = link_fid;
    if (out_name && name_max > 0) {
        strncpy(out_name, link_name[0] ? link_name : "(link)", name_max - 1);
        out_name[name_max - 1] = '\0';
    }
    return (ER)0;
}
