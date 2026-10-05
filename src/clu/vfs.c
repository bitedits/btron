/*
 * B-System BTRON3 — vfs.c
 * Virtual File System implementation for CLU applications (sc, tv).
 * Direct cleanroom binding to B-System 2-level Record-Stream File System.
 * Completely self-contained within B-System; NEVER portals to host OS.
 */
#include "vfs.h"
#include <btron/file.h>
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_types.h>

#if CLU_HOSTED
extern BlkDev *blk_file_create(const char *path, int create_new, UW nblocks);
#endif

extern Volume *g_sys_vol;

int vfs_init(void)
{
#if CLU_HOSTED
    if (vol_mounted_count() == 0) {
        BlkDev *dev = blk_file_create("btron_sys.vol", 0, 0);
        if (dev != NULL) {
            Volume *v = vol_mount(dev);
            if (v != NULL && g_sys_vol == NULL) {
                g_sys_vol = v;
            }
        }
        BlkDev *adev = blk_file_create("btron_anders.vol", 0, 0);
        if (adev != NULL) {
            (void)vol_mount(adev);
        }
    }
#endif
    return (vol_mounted_count() > 0 || g_sys_vol != NULL) ? 0 : -1;
}

void vfs_normalize_path(const char *in_path, char *out_path, size_t out_max)
{
    if (out_path == NULL || out_max < 2) return;
    if (in_path == NULL || in_path[0] == '\0') {
        out_path[0] = '/';
        out_path[1] = '\0';
        return;
    }

    char tmp[VFS_MAX_PATH];
    size_t in_len = strlen(in_path);
    if (in_len >= sizeof(tmp)) in_len = sizeof(tmp) - 1;
    memcpy(tmp, in_path, in_len);
    tmp[in_len] = '\0';

    char *segments[32];
    int seg_count = 0;
    char *p = tmp;

    while (*p != '\0' && seg_count < 32) {
        while (*p == '/') p++;
        if (*p == '\0') break;
        char *seg_start = p;
        while (*p != '\0' && *p != '/') p++;
        if (*p != '\0') {
            *p = '\0';
            p++;
        }
        if (strcmp(seg_start, ".") == 0) {
            continue;
        } else if (strcmp(seg_start, "..") == 0) {
            if (seg_count > 0) seg_count--;
        } else {
            segments[seg_count++] = seg_start;
        }
    }

    size_t out_len = 0;
    out_path[out_len++] = '/';
    out_path[out_len] = '\0';

    for (int i = 0; i < seg_count; i++) {
        size_t slen = strlen(segments[i]);
        if (out_len + slen + 2 < out_max) {
            if (out_len > 1) {
                out_path[out_len++] = '/';
            }
            memcpy(out_path + out_len, segments[i], slen);
            out_len += slen;
            out_path[out_len] = '\0';
        }
    }
}

int vfs_list_dir(const char *path, VfsEntry *entries, int max_entries)
{
    (void)vfs_init();
    if (entries == NULL || max_entries <= 0) return 0;

    char norm_path[VFS_MAX_PATH];
    vfs_normalize_path(path, norm_path, sizeof(norm_path));
    int count = 0;

    /* ── Root directory "/": list all mounted BTRON volumes ─────────── */
    if (strcmp(norm_path, "/") == 0) {
        int vcount = vol_mounted_count();
        for (int i = 0; i < vcount && count < max_entries; i++) {
            Volume *v = vol_get_mounted(i);
            if (v == NULL) continue;
            const char *vname = vol_name(v);
            if (vname == NULL || vname[0] == '\0') continue;

            VfsEntry *e = &entries[count++];
            memset(e, 0, sizeof(*e));
            size_t nlen = strlen(vname);
            if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
            memcpy(e->name, vname, nlen);
            e->name[nlen] = '\0';
            e->size = (uint32_t)(vol_total_blocks(v) * vol_block_size(v));
            e->mtime = 0;
            e->mode = 0777;
            e->is_dir = 1;
            e->is_link = 0;
        }
        return count;
    }

    /* ── Any directory: the volume root (/SYS) or a drawer inside it ── */
    ID dir = opn_dir(norm_path);
    if (dir < 0) return 0;

    DIR_ENTRY de;
    int guard = 0;
    while (rd_dir(dir, &de) == 0 && count < max_entries && guard++ < 2048) {
        if (de.name[0] == '\0') continue;
        if (strcmp(de.name, ".") == 0 || strcmp(de.name, "..") == 0) continue;

        VfsEntry *e = &entries[count++];
        memset(e, 0, sizeof(*e));
        size_t nlen = strlen(de.name);
        if (nlen >= sizeof(e->name)) nlen = sizeof(e->name) - 1;
        memcpy(e->name, de.name, nlen);
        e->name[nlen] = '\0';
        e->size = de.size;
        e->mtime = 0;
        e->is_dir = (de.attr & OBJ_DIRECTORY) ? 1 : 0;
        e->is_link = 0;
        e->mode = (de.attr & OBJ_EXEC) ? 0755 : 0644;
    }
    (void)cls_dir(dir);
    return count;
}

int vfs_read_file(const char *path, char *buf, size_t max_bytes, size_t *out_bytes)
{
    (void)vfs_init();
    if (path == NULL || buf == NULL || max_bytes == 0) return -1;
    if (out_bytes != NULL) *out_bytes = 0;

    ID fd = opn_fil(path, F_READ);
    if (fd < 0) return -1;

    size_t total_read = 0;
    for (int r = 0; r < 64 && total_read < max_bytes; r++) {
        ID rec = opn_rec(fd, (W)r, F_READ);
        if (rec < 0) break;

        W got = 0;
        W want = (W)(max_bytes - total_read);
        ER err = rd_rec(rec, buf + total_read, want, &got);
        (void)cls_rec(rec);

        if (err != 0 || got <= 0) break;
        total_read += (size_t)got;
    }
    (void)cls_fil(fd);

    if (total_read < max_bytes) {
        buf[total_read] = '\0';
    } else {
        buf[max_bytes - 1] = '\0';
    }
    if (out_bytes != NULL) *out_bytes = total_read;
    return 0;
}

int vfs_write_file(const char *path, const char *buf, size_t bytes)
{
    (void)vfs_init();
    if (path == NULL) return -1;

    (void)del_fil(path);

    ID fd = cre_fil(path, F_WRITE);
    if (fd < 0) return -1;

    if (bytes > 0 && buf != NULL) {
        (void)ins_rec(fd, 0, buf, (W)bytes);
    } else {
        (void)ins_rec(fd, 0, "", 0);
    }
    (void)cls_fil(fd);
    return 0;
}

int vfs_exists(const char *path)
{
    (void)vfs_init();
    if (path == NULL || path[0] == '\0') return 0;
    if (strcmp(path, "/") == 0) return 1;

    char norm_path[VFS_MAX_PATH];
    vfs_normalize_path(path, norm_path, sizeof(norm_path));

    /* Check if it is a mounted volume, e.g. /SYS or /ANDERS */
    if (norm_path[0] == '/' && strchr(norm_path + 1, '/') == NULL) {
        int mcount = vol_mounted_count();
        for (int i = 0; i < mcount; i++) {
            Volume *v = vol_get_mounted((UW)i);
            if (v != NULL) {
                const char *lbl = vol_name(v);
                if (lbl != NULL && strcmp(norm_path + 1, lbl) == 0) return 1;
            }
        }
    }

    /* Check if regular file / record exists */
    ID fd = opn_fil(norm_path, F_READ);
    if (fd >= 0) {
        (void)cls_fil(fd);
        return 1;
    }
    return 0;
}


int vfs_delete(const char *path)
{
    (void)vfs_init();
    if (path == NULL) return -1;
    return (del_fil(path) == 0) ? 0 : -1;
}

int vfs_copy(const char *src, const char *dst)
{
    (void)vfs_init();
    if (src == NULL || dst == NULL) return -1;

    char chunk[4096];
    size_t got = 0;
    if (vfs_read_file(src, chunk, sizeof(chunk), &got) != 0) return -1;
    return vfs_write_file(dst, chunk, got);
}
