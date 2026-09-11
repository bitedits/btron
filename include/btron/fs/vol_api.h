/*
 * B-System BTRON3 Filesystem — vol_api.h
 * Public Volume API: mount/format/sync + block backend constructors.
 *
 * Consumers:
 *   - src/cores/core_posix.c   (boot mount)
 *   - src/apps/clu_fs.c        (CLU commands)
 *   - src/tools/mkbtronfs.c    (host tool)
 *   - tests/test_fs.c          (tests)
 *
 * The opaque Volume struct is defined in src/fs/vol.c.
 * All CLU operations use g_sys_vol (the /SYS mount).
 */

#ifndef _BTRON_FS_VOL_API_H_
#define _BTRON_FS_VOL_API_H_

#include <btron/fs/block.h>
#include <btron/fs/fs_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration of opaque Volume handle */
typedef struct Volume Volume;

/* ── Global /SYS volume ────────────────────────────────────────── */
/*
 * g_sys_vol is set at boot by core_posix / core_boot after mounting
 * btron_sys.vol (or a freshly formatted RAM disk).
 * All CLU builtins in clu_fs.c operate on g_sys_vol.
 */
extern Volume *g_sys_vol;
extern Volume *g_anders_vol;
extern Volume *g_chokanji_vol;

/*
 * g_cwd_path — current working path (UTF-8), updated by clu_cd.
 * Format: "/SYS" or "/SYS/SomeDirectory".  Used for the gterm prompt.
 */
extern char g_cwd_path[128];

/* ── Volume lifecycle ──────────────────────────────────────────── */

/*
 * vol_format — write an empty BTRON volume to `dev`.
 *   nfmax  : maximum number of files (FIDs); determines FID table size.
 *   nlb    : total logical blocks (device must be at least nlb blocks).
 *   name   : UTF-8 volume name (also used as root file name, FID 0).
 * Returns 0 on success, negative on error.
 * After vol_format the device holds a valid, clean volume.
 */
int vol_format(BlkDev *dev, UW nfmax, UW nlb, const char *name);

/*
 * vol_mount — open a previously formatted volume for use.
 *   dev    : block device holding the volume image.
 * Validates the magic number, loads FID table + bitmaps into RAM,
 * sets dirty=1 in the header (marks it as mounted), and returns an
 * opaque Volume handle.  Returns NULL if the magic is wrong.
 */
Volume *vol_mount(BlkDev *dev);

/*
 * vol_umount — flush and close a volume.
 * Calls vol_sync(), sets dirty=0, frees in-RAM structures.
 * After this call `v` is invalid.
 */
void vol_umount(Volume *v);

/*
 * vol_sync — flush all dirty in-RAM state to the block device.
 * Writes back: FID table blocks, bitmap blocks, header block.
 * Safe to call multiple times.
 */
void vol_sync(Volume *v);

/* ── Volume statistics ─────────────────────────────────────────── */

/*
 * vol_free_blocks — return current free-block count.
 */
UW vol_free_blocks(const Volume *v);

/*
 * vol_total_blocks — return total block count (nlb).
 */
UW vol_total_blocks(const Volume *v);

/*
 * vol_nfmax — return max FID count.
 */
UW vol_nfmax(const Volume *v);

/*
 * vol_name — return pointer to the UTF-8 volume name (NUL-terminated).
 */
const char *vol_name(const Volume *v);

/*
 * vol_block_size — return logical block size in bytes (e.g. 1024 or 8192).
 */
UW vol_block_size(const Volume *v);

/*
 * vol_is_brightv — returns 1 if volume is B-right/V format, 0 otherwise.
 */
int vol_is_brightv(const Volume *v);

/* TRON Code <-> UTF-8 transcoding */
void btr_tcode_to_utf8(const UH *tc, int max_tcs, char *utf8, int max_bytes);
void btr_utf8_to_tcode(const char *utf8, UH *tc, int max_tcs);

/* ── Internal helpers (used by file.c; not for application code) ── */
BLK  vol_alloc_block(Volume *v);
void vol_free_block(Volume *v, BLK blk);
FID  vol_fid_alloc(Volume *v);
void vol_fid_free(Volume *v, FID fid);
BLK  vol_fid_get_blk(const Volume *v, FID fid);
void vol_fid_set(Volume *v, FID fid, BLK blk, UB refcount);
UB   vol_fid_refcount(const Volume *v, FID fid);
void vol_fid_ref_inc(Volume *v, FID fid);
void vol_fid_ref_dec(Volume *v, FID fid);
UW   vol_name_hash(const char *name);
void vol_hash_set(Volume *v, FID fid, UW hash);
UW   vol_hash_get(const Volume *v, FID fid);
int  vol_read_blk(Volume *v, BLK lba, void *buf);
int  vol_write_blk(Volume *v, BLK lba, const void *buf);
void vol_mark_dirty(Volume *v);

/* ── File-level helpers exposed for host tools ─────────────────────── */
/*
 * fil_set_rec_type — set the RecordIndex.type for a given open fd + record.
 * Used by mkbtronfs to mark RT_TADDATA after ins_rec().
 */
void fil_set_rec_type(ID fd, W rec_idx, UH type);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_FS_VOL_API_H_ */
