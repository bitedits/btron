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
    FileHeader  hdr;            /* cached 192-byte FileHeader               */
    RecordIndex ridx[REC_IDX_LEVEL0_MAX]; /* level-0 index (up to 40)      */
    UW          nrec;           /* mirrors hdr.nrec                         */
    UW          data_blk;       /* first data block allocated to this file  */
    UW          data_used;      /* bytes used in data region so far         */
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
    (void)of;
    /* For now all files are on g_sys_vol; multi-volume support deferred */
    extern Volume *g_sys_vol;
    return g_sys_vol;
}

#endif /* _FS_INTERNAL_H_ */
