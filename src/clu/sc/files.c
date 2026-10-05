/*
 * B-System BTRON3 — files.c
 * Sokhatsky Commander file listing and sorting via B-System VFS.
 * Completely self-contained within B-System (no host OS calls).
 */
#define SC_INTERNAL 1
#include "sokhatsky.h"

#if CLU_HOSTED
#include <stdlib.h>
#else
extern void qsort(void *base, size_t nel, size_t width, int (*compar)(const void *, const void *));
#endif

int compare_files(const void *a, const void *b)
{
    const File *fa = (const File *)a;
    const File *fb = (const File *)b;

    /* ".." always comes first */
    if (strcmp(fa->name, "..") == 0) return -1;
    if (strcmp(fb->name, "..") == 0) return 1;

    /* Directories before regular files */
    if (fa->is_dir != fb->is_dir) {
        return fb->is_dir - fa->is_dir;
    }

    if (active_panel != NULL && active_panel->sort_type == 1) {
        /* Sort by size descending */
        if (fb->size != fa->size) {
            return (fb->size > fa->size) ? 1 : -1;
        }
    } else if (active_panel != NULL && active_panel->sort_type == 2) {
        /* Sort by date descending */
        if (fb->mtime != fa->mtime) {
            return (fb->mtime > fa->mtime) ? 1 : -1;
        }
    }
    return strcmp(fa->name, fb->name);
}

void load_files(Panel *panel)
{
    if (panel == NULL) return;
    panel->file_count = 0;

    static VfsEntry vfs_entries[MAX_FILES];
    int vcount = vfs_list_dir(panel->path, vfs_entries, MAX_FILES - 1);

    /* Add ".." for all directories except root "/" */
    if (strcmp(panel->path, "/") != 0) {
        File *up = &panel->files[panel->file_count++];
        memset(up, 0, sizeof(*up));
        strcpy(up->name, "..");
        up->fid = VFS_NO_FID;      /* FID 0 is a real body; this row is not one */
        up->is_dir = 1;
        up->mode = 0777;
    }

    for (int i = 0; i < vcount && panel->file_count < MAX_FILES; i++) {
        VfsEntry *ve = &vfs_entries[i];
        if (ve->name[0] == '\0') continue;

        File *f = &panel->files[panel->file_count++];
        memset(f, 0, sizeof(*f));
        size_t nlen = strlen(ve->name);
        if (nlen >= sizeof(f->name)) nlen = sizeof(f->name) - 1;
        memcpy(f->name, ve->name, nlen);
        f->name[nlen] = '\0';
        f->fid = ve->fid;
        f->size = ve->size;
        f->mtime = ve->mtime;
        f->is_dir = ve->is_dir;
        f->is_link = ve->is_link;
        f->mode = ve->mode;
    }

    if (panel->file_count > 1) {
        qsort(panel->files, (size_t)panel->file_count, sizeof(File), compare_files);
    }

    if (panel->cursor >= panel->file_count) {
        panel->cursor = panel->file_count > 0 ? panel->file_count - 1 : 0;
    }
    if (panel->cursor < 0) {
        panel->cursor = 0;
    }
}
