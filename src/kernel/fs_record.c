/*
 * B-TRON 2-Level Record Stream File System: fs_record.c
 * Cleanroom implementation of Sakamura BTRON File System.
 */

#include <btron/file.h>
#include <btron/types.h>
#include <btron/error.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#define MAX_FS_FILES      16
#define MAX_RECS_PER_FILE 8
#define MAX_RECORD_SIZE   4096
#define MAX_OPEN_RECS     16
#define MAX_DIR_ENTRIES   8

typedef struct {
    UB   data[MAX_RECORD_SIZE];
    int  size;
    BOOL in_use;
} BTRON_REC;

typedef struct {
    char      path[64];
    UW        mode;
    BOOL      open;
    BOOL      exists;
    BTRON_REC records[MAX_RECS_PER_FILE];
    int       num_records;
} BTRON_FILE;

typedef struct {
    ID   fd;
    W    rec_idx;
    UW   mode;
    W    offset;
    BOOL open;
} BTRON_OPEN_REC;

static BTRON_FILE g_files[MAX_FS_FILES];
static BTRON_OPEN_REC g_open_recs[MAX_OPEN_RECS];
static pthread_mutex_t g_fs_lock = PTHREAD_MUTEX_INITIALIZER;
static BOOL g_fs_init = FALSE;

static void fs_init_once(void) {
    if (!g_fs_init) {
        memset(g_files, 0, sizeof(g_files));
        memset(g_open_recs, 0, sizeof(g_open_recs));

        /* Default root system files */
        g_files[0].exists = TRUE;
        strncpy(g_files[0].path, "/sys/startup.tad", sizeof(g_files[0].path) - 1);
        g_files[0].num_records = 1;
        g_files[0].records[0].in_use = TRUE;
        const char *def = "TRON_AUTORUN";
        memcpy(g_files[0].records[0].data, def, strlen(def));
        g_files[0].records[0].size = strlen(def);

        g_fs_init = TRUE;
    }
}

ID opn_fil(const char *path, UW mode) {
    if (!path) return ER_PAR;

    pthread_mutex_lock(&g_fs_lock);
    fs_init_once();

    for (int i = 0; i < MAX_FS_FILES; i++) {
        if (g_files[i].exists && strcmp(g_files[i].path, path) == 0) {
            g_files[i].open = TRUE;
            g_files[i].mode = mode;
            pthread_mutex_unlock(&g_fs_lock);
            return (ID)(i + 1);
        }
    }

    /* If CREATE mode specified, create it */
    if (mode & F_CREATE) {
        for (int i = 0; i < MAX_FS_FILES; i++) {
            if (!g_files[i].exists) {
                g_files[i].exists = TRUE;
                g_files[i].open = TRUE;
                g_files[i].mode = mode;
                g_files[i].num_records = 0;
                strncpy(g_files[i].path, path, sizeof(g_files[i].path) - 1);
                pthread_mutex_unlock(&g_fs_lock);
                return (ID)(i + 1);
            }
        }
        pthread_mutex_unlock(&g_fs_lock);
        return ER_NOSPC;
    }

    pthread_mutex_unlock(&g_fs_lock);
    return ER_NOEXS;
}

ER cls_fil(ID fd) {
    if (fd <= 0 || fd > MAX_FS_FILES) return ER_FD;

    pthread_mutex_lock(&g_fs_lock);
    fs_init_once();

    int idx = fd - 1;
    if (!g_files[idx].exists || !g_files[idx].open) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_FD;
    }

    g_files[idx].open = FALSE;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ID cre_fil(const char *path, UW mode) {
    return opn_fil(path, mode | F_CREATE);
}

ER del_fil(const char *path) {
    if (!path) return ER_PAR;

    pthread_mutex_lock(&g_fs_lock);
    fs_init_once();

    for (int i = 0; i < MAX_FS_FILES; i++) {
        if (g_files[i].exists && strcmp(g_files[i].path, path) == 0) {
            g_files[i].exists = FALSE;
            g_files[i].open = FALSE;
            pthread_mutex_unlock(&g_fs_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_fs_lock);
    return ER_NOEXS;
}

ER chg_fil(ID fd, UW mode) {
    if (fd <= 0 || fd > MAX_FS_FILES) return ER_FD;
    pthread_mutex_lock(&g_fs_lock);
    g_files[fd - 1].mode = mode;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER mov_fil(const char *src_path, const char *dst_path) {
    if (!src_path || !dst_path) return ER_PAR;

    pthread_mutex_lock(&g_fs_lock);
    fs_init_once();

    for (int i = 0; i < MAX_FS_FILES; i++) {
        if (g_files[i].exists && strcmp(g_files[i].path, src_path) == 0) {
            strncpy(g_files[i].path, dst_path, sizeof(g_files[i].path) - 1);
            pthread_mutex_unlock(&g_fs_lock);
            return E_OK;
        }
    }

    pthread_mutex_unlock(&g_fs_lock);
    return ER_NOEXS;
}

ID opn_rec(ID fd, W rec_idx, UW mode) {
    if (fd <= 0 || fd > MAX_FS_FILES) return ER_FD;
    if (rec_idx < 0 || rec_idx >= MAX_RECS_PER_FILE) return ER_PAR;

    pthread_mutex_lock(&g_fs_lock);
    fs_init_once();

    BTRON_FILE *f = &g_files[fd - 1];
    if (!f->exists) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_FD;
    }

    /* Auto-create record slot if writing */
    if (!f->records[rec_idx].in_use) {
        f->records[rec_idx].in_use = TRUE;
        f->records[rec_idx].size = 0;
        if (rec_idx >= f->num_records) f->num_records = rec_idx + 1;
    }

    for (int i = 0; i < MAX_OPEN_RECS; i++) {
        if (!g_open_recs[i].open) {
            g_open_recs[i].fd      = fd;
            g_open_recs[i].rec_idx = rec_idx;
            g_open_recs[i].mode    = mode;
            g_open_recs[i].offset  = 0;
            g_open_recs[i].open    = TRUE;
            pthread_mutex_unlock(&g_fs_lock);
            return (ID)(i + 1);
        }
    }

    pthread_mutex_unlock(&g_fs_lock);
    return ER_NOSPC;
}

ER cls_rec(ID rec_id) {
    if (rec_id <= 0 || rec_id > MAX_OPEN_RECS) return ER_ID;
    pthread_mutex_lock(&g_fs_lock);
    g_open_recs[rec_id - 1].open = FALSE;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER rd_rec(ID rec_id, VP buf, W sz, W *read_sz) {
    if (!buf || sz < 0) return ER_PAR;
    if (rec_id <= 0 || rec_id > MAX_OPEN_RECS) return ER_ID;

    pthread_mutex_lock(&g_fs_lock);
    BTRON_OPEN_REC *orec = &g_open_recs[rec_id - 1];
    if (!orec->open) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_ID;
    }

    BTRON_FILE *f = &g_files[orec->fd - 1];
    BTRON_REC *r  = &f->records[orec->rec_idx];

    int avail = r->size - orec->offset;
    int to_read = (sz < avail) ? sz : avail;
    if (to_read > 0) {
        memcpy(buf, r->data + orec->offset, to_read);
        orec->offset += to_read;
    } else {
        to_read = 0;
    }

    if (read_sz) *read_sz = to_read;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER wr_rec(ID rec_id, const void *buf, W sz, W *wrote_sz) {
    if (!buf || sz < 0) return ER_PAR;
    if (rec_id <= 0 || rec_id > MAX_OPEN_RECS) return ER_ID;

    pthread_mutex_lock(&g_fs_lock);
    BTRON_OPEN_REC *orec = &g_open_recs[rec_id - 1];
    if (!orec->open) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_ID;
    }

    BTRON_FILE *f = &g_files[orec->fd - 1];
    BTRON_REC *r  = &f->records[orec->rec_idx];

    int space = MAX_RECORD_SIZE - orec->offset;
    int to_write = (sz < space) ? sz : space;
    if (to_write > 0) {
        memcpy(r->data + orec->offset, buf, to_write);
        orec->offset += to_write;
        if (orec->offset > r->size) r->size = orec->offset;
    }

    if (wrote_sz) *wrote_sz = to_write;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER ins_rec(ID fd, W rec_idx, const void *buf, W sz) {
    ID rid = opn_rec(fd, rec_idx, F_WRITE);
    if (rid <= 0) return (ER)rid;
    W wrote = 0;
    ER er = wr_rec(rid, buf, sz, &wrote);
    cls_rec(rid);
    return er;
}

ER del_rec(ID fd, W rec_idx) {
    if (fd <= 0 || fd > MAX_FS_FILES) return ER_FD;
    if (rec_idx < 0 || rec_idx >= MAX_RECS_PER_FILE) return ER_PAR;

    pthread_mutex_lock(&g_fs_lock);
    g_files[fd - 1].records[rec_idx].in_use = FALSE;
    g_files[fd - 1].records[rec_idx].size = 0;
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER pos_rec(ID rec_id, W offset, W origin) {
    if (rec_id <= 0 || rec_id > MAX_OPEN_RECS) return ER_ID;

    pthread_mutex_lock(&g_fs_lock);
    BTRON_OPEN_REC *orec = &g_open_recs[rec_id - 1];
    if (!orec->open) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_ID;
    }

    BTRON_REC *r = &g_files[orec->fd - 1].records[orec->rec_idx];

    if (origin == REC_POS_SET) {
        orec->offset = offset;
    } else if (origin == REC_POS_CUR) {
        orec->offset += offset;
    } else if (origin == REC_POS_END) {
        orec->offset = r->size + offset;
    }

    if (orec->offset < 0) orec->offset = 0;
    if (orec->offset > r->size) orec->offset = r->size;

    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ER trn_rec(ID rec_id, W sz) {
    if (rec_id <= 0 || rec_id > MAX_OPEN_RECS) return ER_ID;

    pthread_mutex_lock(&g_fs_lock);
    BTRON_OPEN_REC *orec = &g_open_recs[rec_id - 1];
    if (!orec->open) {
        pthread_mutex_unlock(&g_fs_lock);
        return ER_ID;
    }

    BTRON_REC *r = &g_files[orec->fd - 1].records[orec->rec_idx];
    if (sz >= 0 && sz <= MAX_RECORD_SIZE) {
        r->size = sz;
    }
    pthread_mutex_unlock(&g_fs_lock);
    return E_OK;
}

ID opn_dir(const char *path) {
    (void)path;
    return (ID)1;
}

ER rd_dir(ID dir_id, DIR_ENTRY *entry) {
    (void)dir_id;
    if (!entry) return ER_PAR;
    strncpy(entry->name, "sample.tad", sizeof(entry->name) - 1);
    entry->attr = 0;
    entry->size = 1024;
    entry->robj_id = 100;
    return E_OK;
}

ER cls_dir(ID dir_id) {
    (void)dir_id;
    return E_OK;
}

ER cre_lnk(const char *link_path, const FS_LINK *target) {
    (void)link_path;
    (void)target;
    return E_OK;
}

ER del_lnk(const char *link_path) {
    (void)link_path;
    return E_OK;
}

ER ref_vol(ID vol_id, VOL_INFO *info) {
    if (!info) return ER_PAR;
    info->vol_id = vol_id > 0 ? vol_id : 1;
    strncpy(info->vol_name, "BTRON_SYSTEM", sizeof(info->vol_name) - 1);
    info->total_blocks = 131072;
    info->free_blocks  = 120000;
    info->block_size   = 4096;
    return E_OK;
}
