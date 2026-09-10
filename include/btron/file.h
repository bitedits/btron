/*
 * B-TRON Specification Compatible Header: file.h
 * BTRON 3.20 2-Level Record-Stream File System Engine.
 */

#ifndef _BTRON_FILE_H_
#define _BTRON_FILE_H_

#include <btron/basic.h>
#include <btron/tad.h>
#include <btron/types.h>
#include <btron/error.h>
#include <btron/proc.h>
#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── File Access Modes ─────────────────────────────────────────── */
#define F_READ       0x0001U
#define F_WRITE      0x0002U
#define F_APPEND     0x0004U
#define F_CREATE     0x0008U
#define F_EXCL       0x0010U

/* ── Record Positioning Origins ────────────────────────────────── */
#define REC_POS_SET  0   /* From beginning of record */
#define REC_POS_CUR  1   /* From current record offset */
#define REC_POS_END  2   /* From end of record */

/* ── Directory Entry Structure ─────────────────────────────────── */
typedef struct {
    char name[64];
    UW   attr;
    UW   size;
    ID   robj_id;
} DIR_ENTRY;

/* ── Volume Information Structure ──────────────────────────────── */
typedef struct {
    ID   vol_id;
    char vol_name[32];
    UW   total_blocks;
    UW   free_blocks;
    UW   block_size;
} VOL_INFO;

/* ── File Level APIs ───────────────────────────────────────────── */
ID opn_fil(const char *path, UW mode);
ER cls_fil(ID fd);
ID cre_fil(const char *path, UW mode);
ER del_fil(const char *path);
ER chg_fil(ID fd, UW mode);
ER mov_fil(const char *src_path, const char *dst_path);

/* ── Record Level APIs ─────────────────────────────────────────── */
ID opn_rec(ID fd, W rec_idx, UW mode);
ER cls_rec(ID rec_id);
ER rd_rec(ID rec_id, VP buf, W sz, W *read_sz);
ER wr_rec(ID rec_id, const void *buf, W sz, W *wrote_sz);
ER ins_rec(ID fd, W rec_idx, const void *buf, W sz);
ER del_rec(ID fd, W rec_idx);
ER pos_rec(ID rec_id, W offset, W origin);
ER trn_rec(ID rec_id, W sz);

/* ── Directory & Link APIs ─────────────────────────────────────── */
ID opn_dir(const char *path);
ER rd_dir(ID dir_id, DIR_ENTRY *entry);
ER cls_dir(ID dir_id);
ER cre_lnk(const char *link_path, const FS_LINK *target);
ER del_lnk(const char *link_path);
ER ref_vol(ID vol_id, VOL_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FILE_H_ */
