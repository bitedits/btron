/*
 * B-System BTRON3 Filesystem — fs_internal.h
 * Internal FS engine state: open-file and open-record tables.
 * Not for application code — used only by src/fs/file.c.
 */

#ifndef _FS_INTERNAL_H_
#define _FS_INTERNAL_H_

#include <btron/file.h>
#include <btron/fs/fs_types.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>
#include <btron/fs/vol_api.h>

#define MAX_OPEN_FILES   64
#define MAX_OPEN_RECS    128

/*
 * OpenFile — one slot per opn_fil / cre_fil call.
 */
typedef struct {
    BOOL        used;
    FID         fid;
    BLK         hdr_blk;       /* logical block address of FileHeader block */
    UW          mode;
    BOOL        dirty;          /* FileHeader or RecordIndex modified?      */
    UB          is_stream;      /* Real Body is a direct stream / secondary */
    FileHeader  hdr;            /* cached 192-byte FileHeader               */
    RecordIndex ridx[REC_IDX_LEVEL0_MAX]; /* level-0 index, capacity see FS.md 7.4 */
    UW          nrec;           /* mirrors hdr.nrec                         */
    UW          data_blk;       /* first data block allocated to this file  */
    UW          data_used;      /* bytes used in data region so far         */
    Volume     *vol;            /* backing volume instance                  */
} OpenFile;

/*
 * OpenRec — one slot per opn_rec call.
 */
typedef struct {
    BOOL        used;
    ID          fd;             /* parent OpenFile slot index               */
    W           rec_idx;        /* record index within the file             */
    UW          mode;
    UW          pos;            /* current read/write position in record    */
    UW          size;           /* payload size of this record              */
    UW          data_offset;    /* byte offset in data block stream         */
} OpenRec;

/* Global tables (defined in file.c) */
extern OpenFile g_open_files[MAX_OPEN_FILES];
extern OpenRec  g_open_recs [MAX_OPEN_RECS ];

/* Helper: return a pointer to the Volume that backs a given OpenFile */
static inline Volume *of_vol(const OpenFile *of) {
    if (of && of->vol) return of->vol;
    extern Volume *g_sys_vol;
    extern Volume *g_anders_vol;
    return g_sys_vol ? g_sys_vol : g_anders_vol;
}

/* ── Cached hierarchy ─────────────────────────────────────────────── */
/*
 * Directory structure for a volume, derived once and shared: FS.md 3.2 "Root
 * namespace" and 7.4 consequence 8.  A B-right/V body header encodes no parent,
 * so parentage comes from other bodies' RT_LINK rows, and an edge counts for
 * every FID that shares the target's header block (hard links).  Both rd_dir()
 * and clu's fs views read structure from here so they cannot disagree.
 *
 * A snapshot is rebuilt when the volume's mount sequence changes or after
 * fil_hier_invalidate(), which every engine call that can add or remove a row
 * makes.  Each accessor ensures the snapshot; callers hold nothing.
 */

/* Drop v's snapshot; NULL drops every snapshot. */
void fil_hier_invalidate(Volume *v);

/* Number of root children (drawers no link row reaches). -1 on failure. */
int fil_hier_nroot(Volume *v);

/* The i'th root child, or FID_INVALID if i is out of range. */
FID fil_hier_root_fid(Volume *v, int i);

/* Header block of a live body, 0 if the FID is not one. */
BLK fil_hier_hdr_blk(Volume *v, FID fid);

/* 1 if the body carries at least one link row (it is a drawer). */
int fil_hier_is_dir(Volume *v, FID fid);

/* Drawer whose link row names this FID, FID_INVALID if none. */
FID fil_hier_parent(Volume *v, FID fid);

/* 1 if this FID is on the root child list. */
int fil_hier_is_root(Volume *v, FID fid);

#endif /* _FS_INTERNAL_H_ */

