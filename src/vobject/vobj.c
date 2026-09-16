/*
 * B-System (BTRON 3.20) Real Body / Virtual Body Hyper-Data Model Engine: vobj.c
 * Cleanroom implementation with persistent storage backing.
 */

#include <btron/vobj.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
extern void* Imalloc(size_t sz);
extern void Ifree(void *ptr);
extern void* Icalloc(size_t nmemb, size_t sz);
#define malloc Imalloc
#define free Ifree
#define calloc Icalloc
#define strncpy tkl_strncpy
#define memset tkl_memset
#define memcpy tkl_memcpy
#define strcmp tkl_strcmp
#define strncat tkl_strncat
#define snprintf tkl_snprintf
#define strlen tkl_strlen
static inline char* local_strchr(const char *s, int c) {
    while (s && *s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    return (c == 0 && s) ? (char*)s : (void*)0;
}
#define strchr local_strchr
static inline int rand(void) { return 42; }
#endif

#define MAX_ROBJS 2048

static ROBJ g_robj_table[MAX_ROBJS];
static ID g_next_robj_id = 100;
static char g_storage_root[256] = "btron_store";

/* Helper to deduce VOBJ_TYPE from filename / path */
static inline __attribute__((unused)) VOBJ_TYPE deduce_type_from_name(const char *name) {
    if (!name) return VOBJ_TYPE_TEXT;
    size_t len = strlen(name);
    if (len >= 4 && (strcmp(name + len - 4, ".png") == 0 || strcmp(name + len - 4, ".PNG") == 0 ||
                     strcmp(name + len - 4, ".gif") == 0 || strcmp(name + len - 4, ".GIF") == 0)) {
        return VOBJ_TYPE_DRAW;
    }
    return VOBJ_TYPE_TEXT;
}

static void register_file_as_robj(const char *path, const char *name, VOBJ_TYPE type) {
    if (!path) return;
    /* Check if already registered */
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id != 0 && strcmp(g_robj_table[i].path, path) == 0) {
            return;
        }
    }
    /* Find free slot */
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id == 0) {
            g_robj_table[i].robj_id = g_next_robj_id++;
            g_robj_table[i].type = type;
            strncpy(g_robj_table[i].name, name ? name : "Object", sizeof(g_robj_table[i].name) - 1);
            strncpy(g_robj_table[i].path, path, sizeof(g_robj_table[i].path) - 1);
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
            struct stat st;
            if (stat(path, &st) == 0) {
                g_robj_table[i].size = (UW)st.st_size;
            } else {
                g_robj_table[i].size = 0;
            }
#else
            g_robj_table[i].size = 0;
#endif
            return;
        }
    }
}

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
static void scan_dir_and_register(const char *dir_path, VOBJ_TYPE def_type) {
    DIR *d = opendir(dir_path);
    if (!d) return;

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        char sub_path[512];
        snprintf(sub_path, sizeof(sub_path), "%s/%s", dir_path, de->d_name);
        struct stat st;
        if (stat(sub_path, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_dir_and_register(sub_path, def_type);
        } else if (S_ISREG(st.st_mode)) {
            VOBJ_TYPE t = deduce_type_from_name(de->d_name);
            register_file_as_robj(sub_path, de->d_name, (t == VOBJ_TYPE_DRAW) ? t : def_type);
        }
    }
    closedir(d);
}
#endif

ER init_vobj_sys(const char *storage_root) {
    if (storage_root && storage_root[0]) {
        strncpy(g_storage_root, storage_root, sizeof(g_storage_root) - 1);
    }
    memset(g_robj_table, 0, sizeof(g_robj_table));
    g_next_robj_id = 100;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    /* Ensure storage root directory exists */
    struct stat st;
    if (stat(g_storage_root, &st) != 0) {
#if defined(_WIN32)
        mkdir(g_storage_root);
#else
        mkdir(g_storage_root, 0755);
#endif
    }

    /* 1. Register persistent user Real Bodies in storage root */
    scan_dir_and_register(g_storage_root, VOBJ_TYPE_TEXT);

    /* 2. Register standard TAD system books and documents */
    scan_dir_and_register("tad_bin", VOBJ_TYPE_TEXT);

    /* 3. Register documentation markdown files */
    scan_dir_and_register("doc/md", VOBJ_TYPE_TEXT);

    /* 4. Register sample texts */
    scan_dir_and_register("assets/texts", VOBJ_TYPE_TEXT);

    /* 5. Register graphic icons and artwork Real Bodies */
    scan_dir_and_register("assets/icons", VOBJ_TYPE_DRAW);
    scan_dir_and_register("assets/pixart", VOBJ_TYPE_DRAW);
#endif

    return E_OK;
}

ROBJ* cre_robj(const char *name, VOBJ_TYPE type) {
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id == 0) {
            g_robj_table[i].robj_id = g_next_robj_id++;
            g_robj_table[i].type = type;
            strncpy(g_robj_table[i].name, name ? name : "Untitled Object", sizeof(g_robj_table[i].name) - 1);

            /* Store new user Real Bodies in storage root */
            char clean_name[64];
            strncpy(clean_name, name ? name : "vobj", sizeof(clean_name) - 1);
            clean_name[sizeof(clean_name) - 1] = '\0';
            /* If no extension, add .tad */
            if (strchr(clean_name, '.') == NULL) {
                strncat(clean_name, ".tad", sizeof(clean_name) - strlen(clean_name) - 1);
            }
            snprintf(g_robj_table[i].path, sizeof(g_robj_table[i].path), "%.128s/%.64s", g_storage_root, clean_name);
            g_robj_table[i].size = 0;
            return &g_robj_table[i];
        }
    }
    return NULL;
}

ROBJ* opn_robj(ID robj_id) {
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id == robj_id) {
            return &g_robj_table[i];
        }
    }
    return NULL;
}

ER cls_robj(ROBJ *robj) {
    (void)robj;
    return E_OK;
}

ROBJ* find_robj_by_path(const char *path) {
    if (!path) return NULL;
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id != 0 && strcmp(g_robj_table[i].path, path) == 0) {
            return &g_robj_table[i];
        }
    }
    return NULL;
}

ROBJ* get_or_create_robj_for_file(const char *path, const char *name, VOBJ_TYPE type) {
    if (!path) return NULL;
    ROBJ *existing = find_robj_by_path(path);
    if (existing) return existing;

    register_file_as_robj(path, name, type);
    return find_robj_by_path(path);
}

int get_robj_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_ROBJS; i++) {
        if (g_robj_table[i].robj_id != 0) count++;
    }
    return count;
}

ROBJ* get_robj_by_index(int idx) {
    if (idx < 0 || idx >= MAX_ROBJS) return NULL;
    if (g_robj_table[idx].robj_id != 0) return &g_robj_table[idx];
    return NULL;
}

VOBJ_LINK* cre_vobj_link(ID target_robj_id, const char *label, H x, H y) {
    VOBJ_LINK *link = (VOBJ_LINK*)calloc(1, sizeof(VOBJ_LINK));
    if (!link) return NULL;

    link->vobj_id = rand() % 10000 + 1;
    link->target_robj = target_robj_id;
    strncpy(link->label, label ? label : "Virtual Link", sizeof(link->label) - 1);
    link->pos.x = x;
    link->pos.y = y;

    return link;
}

ER rd_vobj_data(ROBJ *robj, void *buf, UW len, UW *read_bytes) {
    if (!robj || !buf || len == 0) return E_PAR;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    FILE *fp = fopen(robj->path, "rb");
    if (!fp) {
        /* Fallback: if not found, return empty */
        if (read_bytes) *read_bytes = 0;
        return E_NOEXS;
    }
    size_t nr = fread(buf, 1, len, fp);
    fclose(fp);
    if (read_bytes) *read_bytes = (UW)nr;
    return E_OK;
#else
    if (read_bytes) *read_bytes = 0;
    return E_OK;
#endif
}

ER wr_vobj_data(ROBJ *robj, const void *buf, UW len) {
    if (!robj || !buf) return E_PAR;

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    FILE *fp = fopen(robj->path, "wb");
    if (!fp) return ER_IO;

    size_t nw = fwrite(buf, 1, len, fp);
    fclose(fp);
    if (nw != len) return ER_IO;

    robj->size = len;
    return E_OK;
#else
    robj->size = len;
    return E_OK;
#endif
}
