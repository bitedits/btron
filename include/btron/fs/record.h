/*
 * B-System BTRON3 Filesystem — record.h
 * On-disk Record Index (16 bytes) and Fragment Entry (6 bytes).
 *
 * RecordIndex layout per FS.md §7:
 *   kind   — entry classification:
 *              kind != 0 → continuation (connection) index entry
 *              kind == 0, type != 0 → normal index entry
 *              kind == 0, type == 0x80xx → link index entry
 *              kind == 0, type == 0 → unused slot
 *   type   — RT_* record type packed as per FS.md §7.2 note
 *              ("100T TTTT" style; for clean-room we store RT_* in low 5 bits)
 *   size   — payload size in bytes
 *   offset — byte offset from start of concatenated Data Block stream
 *   flags  — reserved (continuation entries store block# of next fragment here)
 *
 * FragEntry (6 bytes, big-endian on disk) per FS.md §6:
 *   blk[3]  — 24-bit start block address (big-endian)
 *   len[3]  — 24-bit free-fragment byte length (big-endian)
 *
 * LinkRecord (variable, follows RecordIndex for RT_LINK):
 *   target_fid — FID of referenced Real Body
 *   attr[5]    — ATR1..ATR5 link attributes (16-bit each)
 *   name_len   — byte length of UTF-8 name that follows this struct
 *   name[]     — UTF-8 bytes (not NUL-terminated on disk; name_len gives length)
 */

#ifndef _BTRON_FS_RECORD_H_
#define _BTRON_FS_RECORD_H_

#include <btron/fs/fs_types.h>
#include <btron/tad.h>   /* RT_* record type constants */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Record Index (16 bytes on disk) ────────────────────────────── */
typedef struct __attribute__((packed)) {
    UH   kind;     /* +0  2 B  entry kind: 0=normal/link/unused, ≠0=continuation */
    UH   type;     /* +2  2 B  RT_* in low 5 bits; subtype in high bits           */
    UW   size;     /* +4  4 B  payload size in bytes                              */
    UW   offset;   /* +8  4 B  byte offset into Data Block stream                 */
    UW   flags;    /* +12 4 B  reserved (cont: block# of next frag)               */
                   /* total: 2+2+4+4+4 = 16 bytes                                 */
} RecordIndex;

#ifndef __cplusplus
_Static_assert(sizeof(RecordIndex) == 16,
    "RecordIndex must be exactly 16 bytes (BTRON REC_IDX_SIZE)");
#endif

/* RecordIndex.kind sentinel for link index entries */
#define RIDX_KIND_LINK_TYPE  0x80   /* byte1 == 0x80 when kind==0 means link entry */

/* ── On-disk Fragment Entry (6 bytes, big-endian) ───────────────── */
/*
 * Stored in the FileHeader fragment table area (up to 32 entries of 6 bytes
 * each = 192 bytes — note this overlaps with the _pad[] in FileHeader; the
 * fragment table is decoded by the FS engine from the header block, not from
 * the C struct directly).
 *
 * We use a byte array so that big-endian packing is explicit and portable.
 */
typedef struct __attribute__((packed)) {
    UB   blk[3];   /* 24-bit start block address, big-endian */
    UB   len[3];   /* 24-bit free fragment byte length, big-endian */
} FragEntry;

#ifndef __cplusplus
_Static_assert(sizeof(FragEntry) == 6,
    "FragEntry must be exactly 6 bytes (BTRON FRAG_ENT_SIZE)");
#endif

/* Helpers to read/write big-endian 24-bit values from FragEntry fields */
static inline UW frag_get_blk(const FragEntry *fe) {
    return ((UW)fe->blk[0] << 16) | ((UW)fe->blk[1] << 8) | (UW)fe->blk[2];
}
static inline UW frag_get_len(const FragEntry *fe) {
    return ((UW)fe->len[0] << 16) | ((UW)fe->len[1] << 8) | (UW)fe->len[2];
}
static inline void frag_set_blk(FragEntry *fe, UW v) {
    fe->blk[0] = (UB)((v >> 16) & 0xFF);
    fe->blk[1] = (UB)((v >>  8) & 0xFF);
    fe->blk[2] = (UB)( v        & 0xFF);
}
static inline void frag_set_len(FragEntry *fe, UW v) {
    fe->len[0] = (UB)((v >> 16) & 0xFF);
    fe->len[1] = (UB)((v >>  8) & 0xFF);
    fe->len[2] = (UB)( v        & 0xFF);
}

/* ── Link record payload (after RecordIndex, in data blocks) ─────── */
/*
 * RT_LINK record payload stored in the data block stream.
 * name_len bytes of UTF-8 name follow immediately after this struct on disk.
 */
typedef struct __attribute__((packed)) {
    UW   target_fid;   /* FID of referenced Real Body             */
    UH   attr[5];      /* ATR1..ATR5 link attributes (16-bit each) */
    UH   name_len;     /* byte length of UTF-8 name that follows  */
} LinkRecord;

#ifndef __cplusplus
_Static_assert(sizeof(LinkRecord) == 16,
    "LinkRecord fixed header must be 16 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_RECORD_H_ */
