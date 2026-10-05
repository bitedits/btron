/*
 * B-System BTRON3 — vfs.h
 * Virtual File System abstraction for CLU applications (sc, tv).
 *
 * Direct cleanroom binding to B-System 2-level Record-Stream File System:
 *   - Mounts and navigates B-System volumes (/SYS, /ANDERS, etc.)
 *   - Lists directories via opn_dir / rd_dir
 *   - Reads / writes Real Body records via opn_fil / rd_rec / wr_rec / ins_rec
 *   - Completely self-contained within B-System; NEVER portals to host OS.
 *
 * NASA JPL Rule 2 & 3 compliant: static structures, bounded loops, no dynamic
 * heap allocations during file operations.
 */
#ifndef CLU_VFS_H
#define CLU_VFS_H

#include "cluc.h"
#include <stdint.h>
#include <stddef.h>

#define VFS_MAX_PATH  1024
#define VFS_MAX_NAME  128
#define VFS_MAX_FILES 256

/* No Real Body identity behind this entry (a volume shown at "/", a host file). */
#define VFS_NO_FID    0xFFFFFFFFu

/*
 * name is what the user reads; fid is the Real Body that listing actually
 * named.  They must never be conflated: a volume can hold several bodies with
 * the same name, so a path built from names alone opens the first of them no
 * matter which one the cursor was on.  Callers append "#<fid>" to a path
 * segment to re-open this very entry (see vfs_child_path), and keep displaying
 * name.
 */
typedef struct {
    char     name[VFS_MAX_NAME];
    uint32_t fid;       /* VFS_NO_FID when the entry has no Real Body identity */
    uint32_t size;
    uint32_t mtime;
    uint16_t mode;      /* 0755 for exec, 0644 for regular, 0777 for dir */
    uint8_t  is_dir;
    uint8_t  is_link;
} VfsEntry;

/* Initialize VFS and ensure default volumes (/SYS) are mounted */
int  vfs_init(void);

/* List directory into entries array (clamped to max_entries, returns count) */
int  vfs_list_dir(const char *path, VfsEntry *entries, int max_entries);

/* Read file into buffer (returns 0 on success, <0 on error) */
int  vfs_read_file(const char *path, char *buf, size_t max_bytes, size_t *out_bytes);

/* Write file from buffer (returns 0 on success, <0 on error) */
int  vfs_write_file(const char *path, const char *buf, size_t bytes);

/* Check if path exists (returns 1 if exists, 0 otherwise) */
int  vfs_exists(const char *path);

/* Delete file */
int  vfs_delete(const char *path);

/* Copy file */
int  vfs_copy(const char *src, const char *dst);

/* Normalize path (resolves .., collapses extra slashes, clamps to VFS_MAX_PATH) */
void vfs_normalize_path(const char *in_path, char *out_path, size_t out_max);

/*
 * Append one listing entry to a directory path, carrying its Real Body anchor:
 * "<dir>/<name>#<fid>" when the entry has an identity, "<dir>/<name>" when it
 * does not.  This is the only supported way to build a child path from a
 * listing; joining names by hand loses which body the entry named.
 */
void vfs_child_path(char *out, size_t out_max, const char *dir_path,
                    const char *name, uint32_t fid);

/* Path without the "#<fid>" anchors, for showing to the user. */
void vfs_display_path(const char *path, char *out, size_t out_max);

/* 1 when path names a volume's own root (e.g. "/SYS", "/B-right/V"), which is
 * the top of that pane: there is nothing above it but the volume list "/". */
int  vfs_is_volume_root(const char *path);

#endif /* CLU_VFS_H */
