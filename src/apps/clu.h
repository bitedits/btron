/*
 * B-System BTRON3 — clu.h
 * Cho-Kanji-compatible CLU filesystem command helpers.
 *
 * Each function is dispatched from gterm's shell_execute_cmd switch.
 * All commands operate on g_sys_vol (the mounted /SYS volume).
 *
 * args  : the raw argument string after the command name (may be "")
 * out   : callback to append a line to the terminal output
 * ud    : user_data pointer forwarded to out
 *
 * Output shapes match CLU.md exactly (see doc/md/CLU.md §Navigation).
 */

#ifndef _CLU_H_
#define _CLU_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Shell output callback type (matches gterm's ShellOutputFn) */
typedef void (*ShellOutputFn)(const char *line, COLOR col, void *ud);

/* ── Volume navigation ──────────────────────────────────────────── */
void clu_cd   (const char *args, ShellOutputFn out, void *ud);

/* ── Listing ────────────────────────────────────────────────────── */
void clu_ls   (const char *args, ShellOutputFn out, void *ud);

/* ── Record introspection ───────────────────────────────────────── */
void clu_fs_cmd(const char *args, ShellOutputFn out, void *ud);

/* ── Content display ────────────────────────────────────────────── */
void clu_tp   (const char *args, ShellOutputFn out, void *ud);

/* ── File record operations ─────────────────────────────────────── */
void clu_mkf  (const char *args, ShellOutputFn out, void *ud);
void clu_cp   (const char *args, ShellOutputFn out, void *ud);
void clu_ln   (const char *args, ShellOutputFn out, void *ud);
void clu_rm   (const char *args, ShellOutputFn out, void *ud);
void clu_ren  (const char *args, ShellOutputFn out, void *ud);
void clu_empf (const char *args, ShellOutputFn out, void *ud);
void clu_apd  (const char *args, ShellOutputFn out, void *ud);

/* ── Attributes & time ──────────────────────────────────────────── */
void clu_chmod  (const char *args, ShellOutputFn out, void *ud);
void clu_touch  (const char *args, ShellOutputFn out, void *ud);
void clu_chtime (const char *args, ShellOutputFn out, void *ud);

/* ── Volume / system ────────────────────────────────────────────── */
void clu_df      (const char *args, ShellOutputFn out, void *ud);
void clu_sync_cmd(const char *args, ShellOutputFn out, void *ud);

#ifdef __cplusplus
}
#endif

#endif /* _CLU_FS_H_ */
