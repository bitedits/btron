/*
 * B-System BTRON3 — cluc.h
 * Shared declarations for the CLU terminal applications and POSIX-compatible commands.
 *
 * Rules the whole CLU terminal stack follows (NASA/JPL "Power of Ten"):
 *   - no unbounded recursion, no goto, no setjmp
 *   - every loop has a fixed upper bound
 *   - no dynamic allocation at runtime: all storage is static and sized by macros
 *   - every return value that can fail is checked
 *   - all string output goes through bounded snprintf/memcpy
 *
 * Core Portability:
 *   - MIPS R5900 (PS2 Emotion Engine, O32 ABI)
 *   - ARM32 / ARM64 (BCM2835 / BCM2837, Raspberry Pi)
 *   - x86 / x86_64 (POSIX hosted & bare-metal)
 *   - M68k (Motorola 68000)
 */
#ifndef CLUC_H
#define CLUC_H

#include <btron/libc_shim.h>
#include <btron/types.h>
#include <btron/core.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>
#include <btron/file.h>
#include <btron/tad.h>
#include <btron/dp.h>
#include <btron/wnd.h>

#ifdef __cplusplus
extern "C" {
#endif

/* CLU code tests hostedness through this name; it is the shim's flag. */
#define CLU_HOSTED BTRON_HOSTED

/* Clamp helper used by layout code. */
#define CLU_CLAMP(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))

/* ── Node structure for BTRON / B-right/V drawer graphs ─────────── */
typedef struct CluFsNode {
    FID parent_fid;
    FID first_child;
    FID next_sibling;
    FID last_child;
    BLK blk;
    BLK hdr_blk;
    UW sz;
    uint32_t did;
    uint32_t pdid;
    UH flags;
    UB refc;
    UB is_dir;
    UB is_elf;
    UB is_stream;
    UB visited;
    char name[48];
} CluFsNode;

/* ── Shared Argument & Resolution Helpers (src/clu/fs.c) ────────── */
const char *clu_skip_ws(const char *p);
int clu_has_flag(const char *args, const char *flag);
int clu_get_targets(const char *args, char targets[][80], int maxt);
void clu_get_target(const char *args, char *out, int maxlen);
void clu_fmt_ts(UW ts, char *buf, int bufsz);
Volume *clu_get_cwd_vol(void);
Volume *clu_resolve_target_vol(const char *target, Volume *default_vol);
int clu_strcasecmp(const char *s1, const char *s2);
int clu_is_root_path(Volume *v, const char *dir_path);
ID clu_open_target(const char *target, UW mode, Volume *default_vol,
                   Volume **out_vol, FID *out_fid, int *out_is_fid);
void clu_report_target_err(const char *cmd, const char *target, Volume *target_vol,
                           FID fid_val, int is_fid, ShellOutputFn out, void *ud);
int clu_probe_kind(Volume *v, unsigned int fid);
UW clu_kind_color(int kind, int is_link);
CluFsNode *clu_fs_build_graph(Volume *v, UW *pnfmax, unsigned int *pcount);

/* ── CLU Command Implementation Entry Points ────────────────────── */
void clu_cmd_ls   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_cp   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_rm   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_empf (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_mv   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_ln   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_df   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_sync (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_fs   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_cd   (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_mkf  (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_apd  (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_stat (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_info (const char *args, ShellOutputFn out, void *ud);
void clu_cmd_finfo(const char *args, ShellOutputFn out, void *ud);
void clu_cmd_chmod(const char *args, ShellOutputFn out, void *ud);
void clu_cmd_touch(const char *args, ShellOutputFn out, void *ud);
void clu_cmd_chtime(const char *args, ShellOutputFn out, void *ud);
void clu_cmd_tp   (const char *args, ShellOutputFn out, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* CLUC_H */
