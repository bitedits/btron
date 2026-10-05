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
 * (measured: 0x6C..0x7F is the name, and 0xB0/0xC0 hold further per-body data),
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
        int found_hdr_m1 = 0;
        int blk_is_elf = (memcmp(buf, "\x7f\x45\x4c\x46", 4) == 0);
        if (memcmp(buf, "Tron", 4) != 0 && memcmp(buf, "norT", 4) != 0) {
            /* On Cho-Kanji volumes, the Real Body Header is at blk - 1 for files with data */
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
        of->is_stream      = found_hdr_m1 ? 1 : 0;
        of->hdr.owner      = 0;
        of->hdr.group      = 0;
        of->hdr.nlnk       = 1;
        of->hdr.idxlv      = 0;
        of->hdr.total_size = rd_u32_le(buf + 0x48);
        UW nblk_4c = rd_u32_le(buf + 0x4C);   /* blocks claimed by this body */

        UH tc[20];
        for (int k = 0; k < 16; k++) {
            tc[k] = rd_u16_le(buf + 0x6C + k * 2);
        }
        tc[16] = 0;
        btr_tcode_to_utf8(tc, 16, (char *)of->hdr.name, sizeof(of->hdr.name));

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
         * Data extent: normally the blocks immediately after the Real Body
         * header, which is what the FID table names for a body opened through
         * blk-1.  Some bodies are allocated away from their header (measured:
         * BTRON.SYS at 111, Drawing Pad at 112 -- in both cases the header's
         * successor is another body's Real Header); for those the block named
         * by the index entry is the extent.
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

        UH tc[20];
        btr_utf8_to_tcode((const char *)of->hdr.name, tc, 16);
        for (int k = 0; k < 16; k++) {
            wr_u16_le(buf + 0x6C + k * 2, tc[k]);
        }

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
        if (vol_read_blk(v, hblk, buf) != 0) continue;

        char stored[64];
        if (is_bv) {
            if (memcmp(buf, "Tron", 4) != 0 && memcmp(buf, "norT", 4) != 0) {
                int found_hdr = 0;
                if (hblk > 0) {
                    if (vol_read_blk(v, hblk - 1, buf) == 0 &&
                        (memcmp(buf, "Tron", 4) == 0 || memcmp(buf, "norT", 4) == 0)) {
                        found_hdr = 1;
                    }
                }
                if (!found_hdr) continue;
            }
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
 * A B-right/V Real Body header encodes no parent, so the only provable parentage
 * in the volume is other bodies' RT_LINK rows -- and a row that names FID T
 * reaches every FID sharing T's header block, because those are the same body
 * (hard links). Judging edges by FID alone put 42 drawers at the root of the
 * golden volume including etc, LC_TIME, Mail Manager, Makefile and makerules,
 * each of which a real drawer already names; judging by header block leaves 38
 * FIDs on 37 bodies, which is that volume's genuine top level.
 *
 * Deriving that per listing means re-reading every one of the 4457 bodies again,
 * and doing it separately in two consumers is how clu's tree and sc's pane came
 * to disagree. So the engine derives it once per volume and keeps it until a
 * mutation could have changed it.
 *
 * Built for B-right/V volumes only; a cleanroom volume has a real root drawer at
 * FID 0 and needs no derivation.
 */
typedef struct {
    Volume         *vol;
    UW              seq;         /* vol_mount_seq() this snapshot describes */
    UW              nfmax;
    UW              nblk;
    BLK            *hdr;         /* [nfmax] header block, 0 = not a live body */
    FID            *parent;      /* [nfmax] drawer naming it, FID_INVALID = none */
    unsigned char  *is_dir;      /* [nfmax] carries at least one link row */
    unsigned char  *blk_claimed; /* [nblk] block named by a row, then root-listed */
    FID            *root;        /* [nroot] drawers no link row reaches */
    UW              nroot;
} FilHier;

/* Volumes browsed at once are counted, and a listing touches one or two. */
#define FIL_HIER_SLOTS 4
static FilHier s_hier[FIL_HIER_SLOTS];

static void hier_release(FilHier *h)
{
    free(h->hdr);
    free(h->parent);
    free(h->is_dir);
    free(h->blk_claimed);
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
 * One pass over the live FIDs: read each body's header block, take its link rows,
 * and remember the target.  Then propagate every edge across the FIDs that share
 * the target's header block, and the drawers left unclaimed are the root list.
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
    h->blk_claimed = (unsigned char *)calloc(nblk, 1);
    h->root        = (FID *)calloc(nfmax, sizeof(FID));
    if (!h->hdr || !h->parent || !h->is_dir || !h->blk_claimed || !h->root) {
        hier_release(h);
        return (FilHier *)0;
    }
    for (UW f = 0; f < nfmax; f++) h->parent[f] = FID_INVALID;

    unsigned char *buf = (unsigned char *)malloc(bsize);
    if (!buf) { hier_release(h); return (FilHier *)0; }

    for (UW f = 0; f < nfmax; f++) {
        FID fid = (FID)f;
        if (vol_fid_refcount(v, fid) == 0 && fid != FID_ROOT) continue;
        BLK b = vol_fid_get_blk(v, fid);
        if (b == 0 || b >= nblk) continue;
        if (vol_read_blk(v, b, buf) != 0) continue;

        BLK hdr_blk;
        if (memcmp(buf, "Tron", 4) == 0 || memcmp(buf, "norT", 4) == 0) {
            hdr_blk = b;
        } else if (b > 0 && vol_read_blk(v, b - 1, buf) == 0 &&
                   (memcmp(buf, "Tron", 4) == 0 || memcmp(buf, "norT", 4) == 0)) {
            hdr_blk = b - 1;   /* FS.md 7.4 consequence 4 */
        } else {
            continue;           /* not a Real Body header at all */
        }
        h->hdr[fid] = hdr_blk;

        /*
         * Link rows only: byte0 == 0 keeps continuation rows out (FS.md 7.4), and
         * byte1 == 0x80 is RT_LINK with the in-use bit. The target FID is the low
         * half of +4 -- reading the whole word names nothing (consequence 1).
         */
        for (UW off = BVR_RIDX_AREA_START; off + BTRON_REC_IDX_SIZE <= bsize; off += BTRON_REC_IDX_SIZE) {
            const unsigned char *rp = buf + off;
            if (rp[0] != 0 || rp[1] != 0x80) continue;
            FID tf = (FID)(rd_u32_le(rp + 4) & 0xFFFFu);
            if (tf == fid || tf >= nfmax) continue;
            if (vol_fid_refcount(v, tf) == 0 && tf != FID_ROOT) continue;
            h->is_dir[fid] = 1;
            if (h->parent[tf] == FID_INVALID) h->parent[tf] = fid;
        }
    }
    free(buf);

    /*
     * An edge reaches the target's body, not just the target's FID: a row that
     * names FID T also reaches every FID sharing T's header block.  Claiming the
     * block is what turns 42 apparent root drawers into 38, because etc, LC_TIME,
     * Mail Manager, Makefile and makerules each have an alias FID that a real
     * drawer does name.
     */
    for (UW f = 0; f < nfmax; f++) {
        if (h->parent[f] == FID_INVALID || h->hdr[f] == 0 || h->hdr[f] >= nblk) continue;
        h->blk_claimed[h->hdr[f]] = 1;
    }

    /* Root children: drawers on a block no row claimed, one entry per block. */
    for (UW f = 1; f < nfmax; f++) {
        if (!h->is_dir[f] || h->hdr[f] == 0 || h->hdr[f] >= nblk) continue;
        if (h->blk_claimed[h->hdr[f]]) continue;
        h->blk_claimed[h->hdr[f]] = 1;      /* dedups alias FIDs of the same body */
        h->root[h->nroot++] = (FID)f;
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

/* ── opn_dir / rd_dir / cls_dir ──────────────────────────────────── */
/*
 * BTRON's directory model: a directory is a container Real Body whose records
 * are RT_LINK Virtual Bodies, so opn_dir() resolves the path to that container
 * and rd_dir() replays its link records.
 *
 * A B-right/V volume has no root drawer (FS.md 3.2), so its root has no records
 * to replay and rd_dir() answers from the hierarchy snapshot instead -- the same
 * list clu's fs views print, and the only one that proves parentage. The
 * volume-wide FID-table scan below survives only for a cleanroom volume whose
 * FID 0 holds no links.
 */
typedef struct {
    UW next_fid;      /* root-list cursor, or FID cursor in scan mode   */
    UW next_rec;      /* record cursor for the scoped (link) mode      */
    UW nrec;          /* record count of the scoped container          */
    BOOL scoped;      /* TRUE = walk dir_fid's link records            */
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
            g_dirs[i].next_fid = 0;
            g_dirs[i].next_rec = 0;
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
    int is_dir = 0;
    for (unsigned int k = 0; k < co->nrec; k++) {
        if (fil_rec_is_link(cfd, (W)k)) { is_dir = 1; break; }
    }
    cls_fil(cfd);
    if (is_dir) entry->attr |= OBJ_DIRECTORY;
}

ER rd_dir(ID dir_id, DIR_ENTRY *entry)
{
    int slot = (int)(dir_id - 0x1000);
    if (slot < 0 || slot >= 16 || !g_dir_used[slot]) return (ER)-1;
    Volume *v = g_dirs[slot].vol ? g_dirs[slot].vol : g_sys_vol;
    if (!v) return (ER)-1;

    /* ── Scoped: replay the container's link records ─────────────── */
    if (g_dirs[slot].scoped) {
        FID dir_fid = g_dirs[slot].dir_fid;
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
                ID cfd2 = opn_fil_fid(v, cfid, F_READ);
                if (cfd2 >= 0) {
                    char hn[41];
                    memcpy(hn, g_open_files[(int)cfd2].hdr.name, 40);
                    hn[40] = '\0';
                    snprintf(cname, sizeof(cname), "%s", hn);
                    cls_fil(cfd2);
                    nlen = strlen(cname);
                }
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
     * ── Root of a B-right/V volume: the cached drawer list ────────────
     * This namespace has no root container to replay (FS.md 3.2), so the answer
     * is the snapshot's root list: the drawers no link row reaches, deduped by
     * header block.  The volume-wide FID dump below is what `sc` used to show
     * here -- 307 entries that proved no parentage and printed every alias FID
     * of a body, which is how etc, Mail Manager, Makefile and LC_TIME appeared
     * at the root, twice each.
     */
    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) {
        int nroot = fil_hier_nroot(v);
        if (nroot < 0) return (ER)-1;
        while ((int)g_dirs[slot].next_fid < nroot) {
            FID fid = fil_hier_root_fid(v, (int)g_dirs[slot].next_fid++);
            ID cfd = opn_fil_fid(v, fid, F_READ);
            if (cfd < 0) continue;
            OpenFile *co = &g_open_files[(int)cfd];
            memset(entry, 0, sizeof(*entry));
            entry->robj_id = (ID)fid;
            entry->attr    = co->hdr.flags;
            entry->size    = co->hdr.total_size;
            size_t nlen = strlen((const char *)co->hdr.name);
            if (nlen >= sizeof(entry->name)) nlen = sizeof(entry->name) - 1;
            memcpy(entry->name, co->hdr.name, nlen);
            entry->name[nlen] = '\0';
            cls_fil(cfd);
            if (!entry->name[0]) continue;
            entry->attr |= OBJ_DIRECTORY;    /* a root child is a drawer by rule */
            return (ER)0;
        }
        return (ER)1; /* E_EOF / end of directory */
    }

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
