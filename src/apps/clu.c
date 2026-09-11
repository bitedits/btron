/*
 * B-System BTRON3 — clu.c
 * Cho-Kanji-compatible CLU filesystem commands, wired as gterm builtins.
 *
 * All commands operate on g_sys_vol (the /SYS mount).
 * Output shapes match CLU.md exactly (doc/md/CLU.md).
 *
 * Compile with: -Iinclude
 * No SDL dependency; pure C99.
 */

#include <stddef.h>
#include <stdint.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  include <stdio.h>
#  include <stdlib.h>
#  include <string.h>
#  include <time.h>
#  include <ctype.h>
#else
   extern void *Imalloc(size_t);
   extern void *Icalloc(size_t, size_t);
   extern void  Ifree(void *);
   extern int snprintf(char *, size_t, const char *, ...);
   extern void *tkl_memset(void *, int, size_t);
   extern void *tkl_memcpy(void *, const void *, size_t);
   extern int   tkl_memcmp(const void *, const void *, size_t);
   extern int   tkl_strcmp(const char *, const char *);
   extern int   tkl_strncmp(const char *, const char *, size_t);
   extern size_t tkl_strlen(const char *);
   extern char  *tkl_strncpy(char *, const char *, size_t);
#  define malloc   Imalloc
#  define calloc   Icalloc
#  define free     Ifree
#  define memset   tkl_memset
#  define memcpy   tkl_memcpy
#  define memcmp   tkl_memcmp
#  define strcmp   tkl_strcmp
#  define strncmp  tkl_strncmp
#  define strlen   tkl_strlen
#  define strncpy  tkl_strncpy
#  define isspace(c) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r')
#  define isdigit(c) ((c)>='0'&&(c)<='9')
#endif

#include "clu.h"
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>
#include <btron/file.h>
#include <btron/tad.h>
#include <btron/dp.h>   /* COLOR_* constants */

static char *fs_strrchr(const char *s, int c) {
    if (!s) return NULL;
    const char *last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char *)s;
    return (char *)last;
}

static inline uint16_t clu_rd_u16_le(const unsigned char *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t clu_rd_u32_le(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ── Arg parsing helpers ─────────────────────────────────────────── */
/* Skip leading whitespace */
static const char *skip_ws(const char *p) {
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}
/* Check if args starts with flag (e.g. "-l") */
static int has_flag(const char *args, const char *flag) {
    const char *p = args;
    while (*p) {
        p = skip_ws(p);
        if (*p == '-') {
            const char *f = flag + 1; /* skip '-' in flag */
            const char *a = p + 1;
            while (*f && *a == *f) { f++; a++; }
            if (!*f) return 1;
        }
        while (*p && !isspace((unsigned char)*p)) p++;
    }
    return 0;
}
/* Extract all non-flag tokens into a list, returns count */
static int get_targets(const char *args, char targets[][80], int maxt) {
    const char *p = skip_ws(args);
    int count = 0;
    while (*p && count < maxt) {
        p = skip_ws(p);
        if (!*p) break;
        if (*p == '-') { while (*p && !isspace((unsigned char)*p)) p++; continue; }
        if (*p == '"') {
            p++;
            int i = 0;
            while (*p && *p != '"' && i < 79) targets[count][i++] = *p++;
            targets[count++][i] = '\0';
            if (*p == '"') p++;
        } else {
            int i = 0;
            while (*p && !isspace((unsigned char)*p) && i < 79) targets[count][i++] = *p++;
            targets[count++][i] = '\0';
        }
    }
    return count;
}

/* Extract first non-flag argument (filename) */
static void get_target(const char *args, char *out, int maxlen) {
    char targets[1][80];
    int n = get_targets(args, targets, 1);
    if (n > 0) {
        size_t len = strlen(targets[0]);
        if (len >= (size_t)maxlen) len = (size_t)maxlen - 1;
        memcpy(out, targets[0], len);
        out[len] = '\0';
    } else {
        out[0] = '\0';
    }
}

/* ── Timestamp formatter ─────────────────────────────────────────── */
static void fmt_ts(UW ts, char *buf, int bufsz) {
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
    time_t t = (time_t)ts + 946684800L; /* BTRON epoch → Unix epoch */
    struct tm *tm_info = localtime(&t);
    if (tm_info)
        snprintf(buf, (size_t)bufsz, "%04d-%02d-%02d %02d:%02d",
                 tm_info->tm_year + 1900, tm_info->tm_mon + 1,
                 tm_info->tm_mday, tm_info->tm_hour, tm_info->tm_min);
    else
        snprintf(buf, (size_t)bufsz, "----/--/-- --:--");
#else
    (void)ts;
    snprintf(buf, (size_t)bufsz, "2026-09-10 00:00");
#endif
}

/* ── clu_cd ──────────────────────────────────────────────────────── */
void clu_cd(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));

    if (!target[0]) {
        /* No argument: show current working directory */
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s]", g_cwd_path);
        out(msg, COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "/SYS") == 0 || strcmp(target, "/") == 0) {
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/SYS");
        out("[/SYS]", COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "/ANDERS") == 0 || strcmp(target, "ANDERS") == 0) {
        if (!g_anders_vol) {
            out("cd: '/ANDERS': volume not mounted", COLOR_RED, ud);
            return;
        }
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/ANDERS");
        out("[/ANDERS]", COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "/CHOKANJI") == 0 || strcmp(target, "CHOKANJI") == 0 ||
        strcmp(target, "/B-right") == 0 || strcmp(target, "B-right") == 0) {
        if (!g_chokanji_vol) {
            out("cd: '/CHOKANJI': volume not mounted", COLOR_RED, ud);
            return;
        }
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/CHOKANJI");
        out("[/CHOKANJI]", COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "..") == 0) {
        char *last_slash = fs_strrchr(g_cwd_path, '/');
        if (last_slash && last_slash != g_cwd_path) {
            *last_slash = '\0';
        } else {
            snprintf(g_cwd_path, sizeof(g_cwd_path), "/SYS");
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s]", g_cwd_path);
        out(msg, COLOR_GREEN, ud);
        return;
    }

    /* Verify target exists on active volume */
    ID fd = opn_fil(target, 0x0001 /* F_READ */);
    if (fd < 0) {
        char err[128];
        snprintf(err, sizeof(err), "cd: '%s': no such directory", target);
        out(err, COLOR_RED, ud);
        return;
    }
    cls_fil(fd);

    if (target[0] == '/') {
        snprintf(g_cwd_path, sizeof(g_cwd_path), "%s", target);
    } else {
        size_t cur_len = strlen(g_cwd_path);
        if (cur_len + strlen(target) + 2 < sizeof(g_cwd_path)) {
            snprintf(g_cwd_path + cur_len, sizeof(g_cwd_path) - cur_len, "/%s", target);
        }
    }
    char msg[128];
    snprintf(msg, sizeof(msg), "[%s]", g_cwd_path);
    out(msg, COLOR_GREEN, ud);
}

/* ── File kind probing (for ls/fs coloring) ─────────────────────── */
/*
 * clu_probe_kind — inspects the first block of a Real Body by FID.
 * Returns:
 *   2  = ELF executable  (\x7fELF magic at start, or after 192-byte BTRON hdr)
 *   1  = BTRON executable (OBJ_EXEC flag set in FileHeader.flags)
 *   0  = regular file / directory container
 */
static int clu_probe_kind(Volume *v, unsigned int fid) {
    if (!v || fid == FID_INVALID) return 0;
    BLK blk = vol_fid_get_blk(v, (FID)fid);
    if (blk == 0 || blk == FID_INVALID) return 0;
    UW bsz = vol_block_size(v);
    unsigned char *buf = (unsigned char *)malloc(bsz);
    if (!buf) return 0;
    int kind = 0;
    if (vol_read_blk(v, blk, buf) == 0) {
        /* ELF magic: 0x7F 'E' 'L' 'F' at offset 0 (raw ELF image) */
        if (buf[0] == 0x7F && buf[1] == 0x45 && buf[2] == 0x4C && buf[3] == 0x46) {
            kind = 2;
        /* ELF embedded after 192-byte BTRON FileHeader */
        } else if (bsz > 196 &&
                   buf[0xC0] == 0x7F && buf[0xC1] == 0x45 &&
                   buf[0xC2] == 0x4C && buf[0xC3] == 0x46) {
            kind = 2;
        } else {
            /* OBJ_EXEC flag in FileHeader flags word */
            unsigned short fhflags;
            int is_bv = vol_is_brightv(v);
            if (is_bv)
                fhflags = (unsigned short)(buf[4] | ((unsigned short)buf[5] << 8)); /* LE */
            else
                fhflags = (unsigned short)(((unsigned short)buf[0] << 8) | buf[1]); /* BE */
            if (fhflags & OBJ_EXEC)
                kind = 1;
        }
    }
    free(buf);
    return kind;
}

/* Pick output color from probe result:
 *   kind==2  → ELF     → bright green  (like Unix ls --color exec)
 *   kind==1  → BTRON x → bright green
 *   is_link  → symlink → cyan          (like Unix ls --color symlink)
 *   else     → regular → light gray
 */
static UW clu_kind_color(int kind, int is_link) {
    if (kind >= 1) return COLOR_GREEN;  /* ELF or OBJ_EXEC executable */
    if (is_link)   return COLOR_CYAN;   /* RT_LINK virtual body        */
    return COLOR_LTGRAY;
}

/* ── clu_ls ──────────────────────────────────────────────────────── */
void clu_ls(const char *args, ShellOutputFn out, void *ud)
{
    Volume *v = (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol :
                ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) ? g_chokanji_vol : g_sys_vol;
    if (!v) { out("ls: no volume mounted", COLOR_RED, ud); return; }

    int flag_l = has_flag(args, "-l");
    int flag_t = has_flag(args, "-t");

    if (flag_l) {
        out("ATYPE ATR NREC NREF SIZE  MTIME            NAME", COLOR_CYAN, ud);
    } else if (flag_t) {
        out("CTIME              ATIME              MTIME              NAME", COLOR_CYAN, ud);
    }

    char target[80];
    get_target(args, target, sizeof(target));
    const char *dir_path = target[0] ? target : g_cwd_path;

    /* First attempt: inspect container file links */
    ID fd = opn_fil(dir_path, 0x0001);
    if (fd >= 0) {
        OpenFile *of = &g_open_files[(int)fd];
        Volume *ofv = of_vol(of);
        int is_root = (of->fid == FID_ROOT);
        int link_count = 0;
        for (unsigned int i = 0; i < of->nrec; i++) {
            RecordIndex *ri = &of->ridx[i];
            if (ri->type == RT_LINK && ri->size >= 16) {
                unsigned char pbuf[80] = {0};
                ID rec = opn_rec(fd, (W)i, 0x0001);
                if (rec >= 0) {
                    W got = 0;
                    rd_rec(rec, pbuf, 16, &got);
                    FID link_fid = 0;
                    unsigned short nlen = 0;
                    if (vol_is_brightv(ofv)) {
                        link_fid = (FID)((unsigned int)pbuf[0] | ((unsigned int)pbuf[1] << 8) |
                                         ((unsigned int)pbuf[2] << 16) | ((unsigned int)pbuf[3] << 24));
                        nlen = (unsigned short)(pbuf[14] | (pbuf[15] << 8));
                    } else {
                        link_fid = ((unsigned int)pbuf[0]<<24)|((unsigned int)pbuf[1]<<16)|
                                   ((unsigned int)pbuf[2]<<8)|(unsigned int)pbuf[3];
                        nlen = ((unsigned short)pbuf[14]<<8)|pbuf[15];
                    }
                    char link_name[48] = "";
                    if (nlen > 0 && nlen < 40) {
                        W got2 = 0;
                        rd_rec(rec, link_name, (W)nlen, &got2);
                        link_name[got2] = '\0';
                    }
                    cls_rec(rec);

                    if (vol_is_brightv(ofv) && link_name[0] == '\0') {
                        BLK lb = vol_fid_get_blk(ofv, link_fid);
                        if (lb > 0 && lb != FID_INVALID) {
                            unsigned char *lbuf = (unsigned char *)malloc(vol_block_size(ofv));
                            if (lbuf) {
                                if (vol_read_blk(ofv, lb, lbuf) == 0 &&
                                    (memcmp(lbuf, "Tron", 4) == 0 || memcmp(lbuf, "norT", 4) == 0)) {
                                    UH tc[20];
                                    for (int k = 0; k < 16; k++) tc[k] = clu_rd_u16_le(lbuf + 0x6C + k * 2);
                                    tc[16] = 0;
                                    btr_tcode_to_utf8(tc, 16, link_name, sizeof(link_name));
                                }
                                free(lbuf);
                            }
                        }
                    }
                    if (link_name[0] == '\0') {
                        snprintf(link_name, sizeof(link_name), "FID%u", (unsigned)link_fid);
                    }

                    link_count++;
                    /* Probe target for color: ELF/exec=green, link=cyan */
                    int lk = clu_probe_kind(ofv, (unsigned int)link_fid);
                    UW lk_color = clu_kind_color(lk, 1 /* is_link */);
                    if (flag_l) {
                        BLK hblk = vol_fid_get_blk(ofv, link_fid);
                        UW of_bsize = vol_block_size(ofv);
                        unsigned char *lhbuf = (unsigned char *)calloc(1, of_bsize);
                        if (lhbuf) {
                            if (hblk != FID_INVALID && hblk != 0) vol_read_blk(ofv, hblk, lhbuf);
                            unsigned short atype = 0, tsz = 0;
                            unsigned int mtime = 0;
                            if (vol_is_brightv(ofv)) {
                                atype = 0;
                                mtime = clu_rd_u32_le(lhbuf + 0x64);
                                tsz   = clu_rd_u32_le(lhbuf + 0x48);
                            } else {
                                atype = ((unsigned short)lhbuf[2] << 8) | lhbuf[3];
                                mtime = ((unsigned int)lhbuf[8]<<24)|((unsigned int)lhbuf[9]<<16)|
                                        ((unsigned int)lhbuf[10]<<8)|(unsigned int)lhbuf[11];
                                tsz   = ((unsigned int)lhbuf[28]<<24)|((unsigned int)lhbuf[29]<<16)|
                                        ((unsigned int)lhbuf[30]<<8)|(unsigned int)lhbuf[31];
                            }
                            char mt[24]; fmt_ts(mtime, mt, sizeof(mt));
                            char line[256];
                            snprintf(line, sizeof(line), "%04X  ---  1    1    %-5u %s %s",
                                     atype, tsz, mt, link_name);
                            out(line, lk_color, ud);
                            free(lhbuf);
                        }
                    } else if (flag_t) {
                        BLK hblk = vol_fid_get_blk(ofv, link_fid);
                        UW of_bsize = vol_block_size(ofv);
                        unsigned char *lhbuf = (unsigned char *)calloc(1, of_bsize);
                        if (lhbuf) {
                            if (hblk != FID_INVALID && hblk != 0) vol_read_blk(ofv, hblk, lhbuf);
                            unsigned int ctime = 0, atime = 0, mtime = 0;
                            if (vol_is_brightv(ofv)) {
                                ctime = clu_rd_u32_le(lhbuf + 0x60);
                                mtime = clu_rd_u32_le(lhbuf + 0x64);
                                atime = clu_rd_u32_le(lhbuf + 0x68);
                            } else {
                                ctime = ((unsigned int)lhbuf[4]<<24)|((unsigned int)lhbuf[5]<<16)|
                                        ((unsigned int)lhbuf[6]<<8)|(unsigned int)lhbuf[7];
                                mtime = ((unsigned int)lhbuf[8]<<24)|((unsigned int)lhbuf[9]<<16)|
                                        ((unsigned int)lhbuf[10]<<8)|(unsigned int)lhbuf[11];
                                atime = ((unsigned int)lhbuf[12]<<24)|((unsigned int)lhbuf[13]<<16)|
                                        ((unsigned int)lhbuf[14]<<8)|(unsigned int)lhbuf[15];
                            }
                            char ct[24], at[24], mt[24];
                            fmt_ts(ctime, ct, sizeof(ct));
                            fmt_ts(atime, at, sizeof(at));
                            fmt_ts(mtime, mt, sizeof(mt));
                            char line[256];
                            snprintf(line, sizeof(line), "%-18s %-18s %-18s %s",
                                     ct, at, mt, link_name);
                            out(line, lk_color, ud);
                            free(lhbuf);
                        }
                    } else {
                        out(link_name, lk_color, ud);
                    }
                }
            }
        }
        cls_fil(fd);
        /* Subdirectory/drawer containers terminate here (empty directory shows 0 entries, never root!) */
        if (!is_root || link_count > 0) return;
    } else {
        int is_root_path = (strcmp(dir_path, "/CHOKANJI") == 0 || strcmp(dir_path, "/CHOKANJI/") == 0 ||
                            strcmp(dir_path, "/SYS") == 0 || strcmp(dir_path, "/SYS/") == 0 ||
                            strcmp(dir_path, "/ANDERS") == 0 || strcmp(dir_path, "/ANDERS/") == 0 ||
                            strcmp(dir_path, "/") == 0 || strcmp(dir_path, ".") == 0 || dir_path[0] == '\0');
        if (!is_root_path) {
            char err[128];
            snprintf(err, sizeof(err), "ls: '%s': no such directory", dir_path);
            out(err, COLOR_RED, ud);
            return;
        }
    }

    /* Fallback: flat directory scan for volume root containers */
    ID dir = opn_dir(dir_path);
    if (dir < 0) { out("ls: opn_dir failed", COLOR_RED, ud); return; }

    UW bsize = vol_block_size(v);
    unsigned char *hbuf = (flag_l || flag_t) ? (unsigned char *)malloc(bsize) : NULL;
    int is_bv = vol_is_brightv(v);

    DIR_ENTRY entry;
    while (rd_dir(dir, &entry) == 0) {
        if (!entry.name[0]) continue;

        /* Determine color from block data when available */
        FID fid = (FID)entry.robj_id;

        if (!flag_l && !flag_t) {
            /* Simple name-only listing: probe kind for color */
            int ek = clu_probe_kind(v, (unsigned int)fid);
            out(entry.name, clu_kind_color(ek, 0), ud);
            continue;
        }

        /* Read full FileHeader for extra info */
        BLK hblk = vol_fid_get_blk(v, fid);
        if (hbuf && hblk != 0 && hblk != FID_INVALID) {
            if (vol_read_blk(v, hblk, hbuf) != 0) continue;
        } else {
            continue;
        }

        unsigned short flags, atype, nlnk;
        unsigned int ctime, mtime, atime, nrec, tsz;

        if (is_bv) {
            flags = clu_rd_u16_le(hbuf + 4);
            atype = 0;
            ctime = clu_rd_u32_le(hbuf + 0x60);
            mtime = clu_rd_u32_le(hbuf + 0x64);
            atime = clu_rd_u32_le(hbuf + 0x68);
            nlnk  = 1;
            nrec  = clu_rd_u32_le(hbuf + 0x4C);
            tsz   = clu_rd_u32_le(hbuf + 0x48);
        } else {
            /* Decode FileHeader fields (big-endian) */
            flags = ((unsigned short)hbuf[0] << 8) | hbuf[1];
            atype = ((unsigned short)hbuf[2] << 8) | hbuf[3];
            ctime = ((unsigned int)hbuf[4]<<24)|((unsigned int)hbuf[5]<<16)|
                    ((unsigned int)hbuf[6]<<8)|(unsigned int)hbuf[7];
            mtime = ((unsigned int)hbuf[8]<<24)|((unsigned int)hbuf[9]<<16)|
                    ((unsigned int)hbuf[10]<<8)|(unsigned int)hbuf[11];
            atime = ((unsigned int)hbuf[12]<<24)|((unsigned int)hbuf[13]<<16)|
                    ((unsigned int)hbuf[14]<<8)|(unsigned int)hbuf[15];
            nlnk  = ((unsigned short)hbuf[20]<<8)|hbuf[21];
            nrec  = ((unsigned int)hbuf[24]<<24)|((unsigned int)hbuf[25]<<16)|
                    ((unsigned int)hbuf[26]<<8)|(unsigned int)hbuf[27];
            tsz   = ((unsigned int)hbuf[28]<<24)|((unsigned int)hbuf[29]<<16)|
                    ((unsigned int)hbuf[30]<<8)|(unsigned int)hbuf[31];
        }

        /* Color: ELF magic or OBJ_EXEC flag → green; otherwise gray */
        int is_exec_flag = (flags & OBJ_EXEC) ? 1 : 0;
        int is_elf_blk   = (hbuf[0] == 0x7F && hbuf[1] == 0x45 &&
                            hbuf[2] == 0x4C && hbuf[3] == 0x46) ? 1 : 0;
        UW entry_color = (is_exec_flag || is_elf_blk) ? COLOR_GREEN : COLOR_LTGRAY;

        char line[256];
        if (flag_l) {
            char mt[24]; fmt_ts(mtime, mt, sizeof(mt));
            char atr[4] = "---";
            if (flags & 0x0020) atr[0] = 'P';
            if (flags & 0x0010) atr[1] = 'O';
            snprintf(line, sizeof(line),
                     "%04X  %s %-4u %-4u %-5u %s %s",
                     atype, atr, nrec, nlnk, tsz, mt, entry.name);
        } else if (flag_t) {
            char ct[24], at[24], mt[24];
            fmt_ts(ctime, ct, sizeof(ct));
            fmt_ts(atime, at, sizeof(at));
            fmt_ts(mtime, mt, sizeof(mt));
            snprintf(line, sizeof(line), "%-18s %-18s %-18s %s",
                     ct, at, mt, entry.name);
        }
        out(line, entry_color, ud);
    }
    if (hbuf) free(hbuf);
    cls_dir(dir);
}

/* ── clu_fs_cmd helpers ─────────────────────────────────────────── */

typedef struct {
    char name[48];
    unsigned int fid;
} CluFsPendingLink;

static void clu_fs_dump_records(Volume *v, ID fd, FID parent_fid, const char *parent_name, int depth,
                                int flag_l, int flag_r, unsigned char *visited, UW nfmax,
                                ShellOutputFn out, void *ud)
{
    if (fd < 0) return;
    OpenFile *of = &g_open_files[(int)fd];
    if (of->fid < nfmax) {
        visited[of->fid] = 1;
    }

    int indent = (depth > 0) ? (depth * 2) : 0;
    if (indent > 16) indent = 16;

    int is_bv_root = (vol_is_brightv(v) && of->fid == FID_ROOT);

    if (!is_bv_root && of->nrec > 0) {
        for (unsigned int i = 0; i < of->nrec; i++) {
            RecordIndex *ri = &of->ridx[i];
            if (vol_is_brightv(v) && ri->size == 0 && ri->type == 0) continue;
            char line[256];
            if (ri->type == RT_LINK) {
                char link_name[48] = "(link)";
                unsigned int link_fid = 0;
                unsigned short attrs[5] = {0,0,0,0,0};
                if (ri->size >= 16) {
                    unsigned char pbuf[80] = {0};
                    ID rec = opn_rec(fd, (W)i, 0x0001);
                    if (rec >= 0) {
                        W got = 0;
                        rd_rec(rec, pbuf, 16, &got);
                        if (vol_is_brightv(v)) {
                            link_fid = (unsigned int)pbuf[0] | ((unsigned int)pbuf[1] << 8) |
                                       ((unsigned int)pbuf[2] << 16) | ((unsigned int)pbuf[3] << 24);
                            for (int a = 0; a < 5; a++)
                                attrs[a] = (unsigned short)(pbuf[4+a*2] | (pbuf[4+a*2+1] << 8));
                            unsigned short nlen = (unsigned short)(pbuf[14] | (pbuf[15] << 8));
                            if (nlen > 0 && nlen < 40) {
                                W got2 = 0;
                                rd_rec(rec, link_name, (W)nlen, &got2);
                                link_name[got2] = '\0';
                            }
                        } else {
                            link_fid   = ((unsigned int)pbuf[0]<<24)|((unsigned int)pbuf[1]<<16)|
                                         ((unsigned int)pbuf[2]<<8)|(unsigned int)pbuf[3];
                            for (int a = 0; a < 5; a++)
                                attrs[a] = ((unsigned short)pbuf[4+a*2]<<8)|pbuf[4+a*2+1];
                            unsigned short nlen = ((unsigned short)pbuf[14]<<8)|pbuf[15];
                            if (nlen > 0 && nlen < 40) {
                                W got2 = 0;
                                rd_rec(rec, link_name, (W)nlen, &got2);
                                link_name[got2] = '\0';
                            }
                        }
                        cls_rec(rec);
                    }
                }
                if (vol_is_brightv(v) && link_fid == 0 && ri->offset > 0) {
                    link_fid = ri->offset;
                }
                if (vol_is_brightv(v) && (link_name[0] == '\0' || strcmp(link_name, "(link)") == 0)) {
                    if (link_fid > 0 && link_fid != FID_INVALID && link_fid < nfmax) {
                        BLK lb = vol_fid_get_blk(v, (FID)link_fid);
                        if (lb > 0 && lb != FID_INVALID) {
                            unsigned char *lbuf = (unsigned char *)malloc(vol_block_size(v));
                            if (lbuf) {
                                if (vol_read_blk(v, lb, lbuf) == 0) {
                                    if (memcmp(lbuf, "Tron", 4) == 0 || memcmp(lbuf, "norT", 4) == 0) {
                                        UH tc[20];
                                        for (int k = 0; k < 16; k++) tc[k] = clu_rd_u16_le(lbuf + 0x6C + k * 2);
                                        tc[16] = 0;
                                        btr_tcode_to_utf8(tc, 16, link_name, sizeof(link_name));
                                    } else if (memcmp(lbuf, "\x7f\x45\x4c\x46", 4) == 0) {
                                        snprintf(link_name, sizeof(link_name), "ELF_%u", link_fid);
                                    }
                                }
                                free(lbuf);
                            }
                        }
                    }
                }
                int lkind = clu_probe_kind(v, link_fid);
                UW lcolor  = clu_kind_color(lkind, lkind < 1 /* is_link if not exec */);
                const char *tag = (lkind >= 2) ? " [ELF]" : (lkind == 1 ? " [EXE]" : "");
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  0 %04X  : %-5u %-6u [%04X %04X %04X %04X %04X] : %*s%s%s",
                             i, (unsigned)ri->flags & 0xFFFF,
                             link_fid, (unsigned)of->fid,
                             attrs[0], attrs[1], attrs[2], attrs[3], attrs[4],
                             indent, "", link_name, tag);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  0    %04X  : %-5u %-6u : %*s%s%s",
                             i, (unsigned)ri->flags & 0xFFFF,
                             link_fid, (unsigned)of->fid,
                             indent, "", link_name, tag);
                }
                out(line, lcolor, ud);

                /* Recurse depth-first into child Virtual Object Real Body if -r is requested */
                if (flag_r && depth < 16 && link_fid != 0 && link_fid != of->fid && link_fid < nfmax && !visited[link_fid]) {
                    ID child_fd = opn_fil_fid(v, (FID)link_fid, 0x0001);
                    if (child_fd < 0) {
                        char child_path[128];
                        const char *vprefix = (v == g_chokanji_vol) ? "/CHOKANJI/" :
                                              (v == g_anders_vol) ? "/ANDERS/" : "/SYS/";
                        snprintf(child_path, sizeof(child_path), "%s%s", vprefix, link_name);
                        child_fd = opn_fil(child_path, 0x0001);
                    }
                    if (child_fd >= 0) {
                        clu_fs_dump_records(v, child_fd, of->fid, link_name, depth + 1, flag_l, flag_r, visited, nfmax, out, ud);
                        cls_fil(child_fd);
                    }
                }
            } else {
                if (flag_l) {
                    if (depth > 0) {
                        snprintf(line, sizeof(line),
                                 "%u:  %-4u %04X  : -     %-6u (data record)              : %*s[%s]",
                                 i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                                 (unsigned)parent_fid,
                                 indent, "", parent_name);
                    } else {
                        snprintf(line, sizeof(line),
                                 "%u:  %-4u %04X  : -     %-6u (data record)",
                                 i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                                 (unsigned)parent_fid);
                    }
                } else {
                    if (depth > 0) {
                        snprintf(line, sizeof(line),
                                 "%u:  %-4u %04X  : -     %-6u : %-12u : %*s[%s]",
                                 i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                                 (unsigned)parent_fid,
                                 ri->size, indent, "", parent_name);
                    } else {
                        snprintf(line, sizeof(line),
                                 "%u:  %-4u %04X  : -     %-6u : %-12u",
                                 i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                                 (unsigned)parent_fid,
                                 ri->size);
                    }
                }
                out(line, COLOR_LTGRAY, ud);
            }
        }
    } else if (of->fid == FID_ROOT || is_bv_root) {
        unsigned int rec_idx = 0;
        /* For Cho-Kanji volume root, first dump any active data records belonging to FID 0 itself */
        if (is_bv_root && of->nrec > 0) {
            for (unsigned int i = 0; i < of->nrec; i++) {
                RecordIndex *ri = &of->ridx[i];
                if (ri->size == 0 && ri->type == 0) continue;
                char line[256];
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  %-4u %04X  : -     %-6u (data record)",
                             rec_idx++, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                             (unsigned)of->fid);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  %-4u %04X  : -     %-6u : %-12u",
                             rec_idx++, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                             (unsigned)of->fid, ri->size);
                }
                out(line, COLOR_LTGRAY, ud);
            }
        }

        /* Directory enumeration for root container */
        const char *vdir = (v == g_chokanji_vol) ? "/CHOKANJI" :
                           (v == g_anders_vol) ? "/ANDERS" : "/SYS";
        ID dir = opn_dir(vdir);
        if (dir >= 0) {
            DIR_ENTRY entry;
            unsigned int idx = rec_idx;
            while (rd_dir(dir, &entry) == 0) {
                if (!entry.name[0]) continue;
                FID efid = (FID)entry.robj_id;
                if (efid == FID_ROOT && !is_bv_root) continue;
                char line[256];
                int ekind = clu_probe_kind(v, (unsigned int)efid);
                UW ecolor  = clu_kind_color(ekind, ekind < 1 /* is_link if not exec */);
                const char *etag = (ekind >= 2) ? " [ELF]" : (ekind == 1 ? " [EXE]" : "");
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  0 0000  : %-5u %-6u [0000 0000 0000 0000 0000] : %*s%s%s",
                             idx++, (unsigned)efid, (unsigned)of->fid, indent, "", entry.name, etag);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  0    %04X  : %-5u %-6u : %*s%s%s",
                             idx++, 0, (unsigned)efid, (unsigned)of->fid, indent, "", entry.name, etag);
                }
                out(line, ecolor, ud);

                /* Recurse depth-first into child Real Bodies if -r is requested */
                if (flag_r && depth < 16 && efid != 0 && efid < nfmax && !visited[efid]) {
                    ID child_fd = opn_fil_fid(v, efid, 0x0001);
                    if (child_fd < 0) {
                        char child_path[128];
                        const char *vprefix = (v == g_chokanji_vol) ? "/CHOKANJI/" :
                                              (v == g_anders_vol) ? "/ANDERS/" : "/SYS/";
                        snprintf(child_path, sizeof(child_path), "%s%s", vprefix, entry.name);
                        child_fd = opn_fil(child_path, 0x0001);
                    }
                    if (child_fd >= 0) {
                        clu_fs_dump_records(v, child_fd, of->fid, entry.name, depth + 1, flag_l, flag_r, visited, nfmax, out, ud);
                        cls_fil(child_fd);
                    }
                }
            }
            cls_dir(dir);
            if (idx == 0) out("(0 records)", COLOR_LTGRAY, ud);
        }
    } else {
        out("(0 records)", COLOR_LTGRAY, ud);
    }
}

/* ── clu_fs_cmd ──────────────────────────────────────────────────── */

typedef struct {
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

static void clu_fs_node_add_child(CluFsNode *nodes, FID parent, FID child, UW nfmax)
{
    if (parent == FID_INVALID || child == FID_INVALID || parent == child) return;
    if (parent >= nfmax || child >= nfmax) return;
    if (nodes[child].parent_fid != FID_INVALID) return; /* Already linked to a parent */

    /* Cycle prevention: ensure parent is not a descendant of child */
    UW hop = 0;
    for (FID anc = parent; anc != FID_INVALID && hop < nfmax; anc = nodes[anc].parent_fid, hop++) {
        if (anc == child) return;
    }

    /* Check if already in parent's child list */
    hop = 0;
    for (FID c = nodes[parent].first_child; c != FID_INVALID && hop < nfmax; c = nodes[c].next_sibling, hop++) {
        if (c == child) return;
    }

    nodes[child].parent_fid = parent;
    nodes[child].next_sibling = FID_INVALID;

    /* Insert in ascending FID order for a deterministic, normalized tree hierarchy */
    if (nodes[parent].first_child == FID_INVALID || child < nodes[parent].first_child) {
        nodes[child].next_sibling = nodes[parent].first_child;
        nodes[parent].first_child = child;
        if (nodes[parent].last_child == FID_INVALID) {
            nodes[parent].last_child = child;
        }
    } else {
        FID prev = nodes[parent].first_child;
        hop = 0;
        while (prev != FID_INVALID && nodes[prev].next_sibling != FID_INVALID &&
               nodes[prev].next_sibling < child && hop < nfmax) {
            prev = nodes[prev].next_sibling;
            hop++;
        }
        nodes[child].next_sibling = nodes[prev].next_sibling;
        nodes[prev].next_sibling = child;
        if (nodes[child].next_sibling == FID_INVALID) {
            nodes[parent].last_child = child;
        }
    }
}

static void clu_fs_print_node_line(const CluFsNode *nodes, FID fid, int indent_spaces,
                                   int flag_l, int is_tree, ShellOutputFn out, void *ud)
{
    if (!nodes[fid].blk) return;
    int kind = nodes[fid].is_dir ? 3 : (nodes[fid].is_elf ? 2 : (nodes[fid].is_stream ? 4 : 0));
    const char *tag = (kind == 3) ? "[DIR]" :
                      (kind == 2) ? "[ELF]" :
                      (kind == 4) ? "[STR]" : "[TAD]";
    UW color = clu_kind_color(kind, (kind == 3));

    char name_buf[64];
    if (is_tree && nodes[fid].is_dir && nodes[fid].name[0]) {
        size_t len = strlen(nodes[fid].name);
        if (len > 0 && nodes[fid].name[len - 1] != '/') {
            snprintf(name_buf, sizeof(name_buf), "%s/", nodes[fid].name);
        } else {
            snprintf(name_buf, sizeof(name_buf), "%s", nodes[fid].name);
        }
    } else {
        snprintf(name_buf, sizeof(name_buf), "%s", nodes[fid].name);
    }

    char line[256];
    if (flag_l) {
        snprintf(line, sizeof(line),
                 "%-5u %-6u %-4u %-5s %04X  : %08X %08X : %-10u %*s%s",
                 (unsigned)fid, (unsigned)nodes[fid].blk, (unsigned)nodes[fid].refc,
                 tag, (unsigned)nodes[fid].flags,
                 nodes[fid].did, nodes[fid].pdid,
                 nodes[fid].sz, indent_spaces, "", name_buf);
    } else {
        snprintf(line, sizeof(line),
                 "%-5u %-6u %-5s %-10u %*s%s",
                 (unsigned)fid, (unsigned)nodes[fid].blk, tag, nodes[fid].sz,
                 indent_spaces, "", name_buf);
    }
    out(line, color, ud);
}

static void clu_fs_print_node_tree(CluFsNode *nodes, FID fid, int depth, int flag_l,
                                   UW nfmax, ShellOutputFn out, void *ud)
{
    if (depth > 20 || fid >= nfmax || nodes[fid].blk == 0) return;
    nodes[fid].visited = 1;

    int indent = depth * 2;
    if (indent > 40) indent = 40;

    clu_fs_print_node_line(nodes, fid, indent, flag_l, 1, out, ud);

    UW hop = 0;
    for (FID c = nodes[fid].first_child; c != FID_INVALID && hop < nfmax; c = nodes[c].next_sibling, hop++) {
        if (c < nfmax && !nodes[c].visited) {
            clu_fs_print_node_tree(nodes, c, depth + 1, flag_l, nfmax, out, ud);
        }
    }
}

/* ── clu_open_target ─────────────────────────────────────────────── */
static ID clu_open_target(const char *target, UW mode, Volume *default_vol,
                          Volume **out_vol, FID *out_fid, int *out_is_fid)
{
    if (out_vol) *out_vol = NULL;
    if (out_fid) *out_fid = FID_INVALID;
    if (out_is_fid) *out_is_fid = 0;

    if (!target || !target[0]) return -1;

    Volume *v = default_vol ? default_vol :
                (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol :
                ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) ? g_chokanji_vol : g_sys_vol;

    ID fd = -1;
    int is_fid = 0;
    FID fid_val = FID_INVALID;
    const char *num_str = NULL;
    Volume *target_vol = v;

    if (strncmp(target, "/CHOKANJI#", 10) == 0) {
        target_vol = g_chokanji_vol;
        num_str = target + 10;
        is_fid = 1;
    } else if (strncmp(target, "/ANDERS#", 8) == 0) {
        target_vol = g_anders_vol;
        num_str = target + 8;
        is_fid = 1;
    } else if (strncmp(target, "/SYS#", 5) == 0) {
        target_vol = g_sys_vol;
        num_str = target + 5;
        is_fid = 1;
    } else if (strncmp(target, "/CHOKANJI/", 10) == 0 || strcmp(target, "/CHOKANJI") == 0 ||
               strncmp(target, "/B-right/", 9) == 0 || strcmp(target, "/B-right") == 0) {
        target_vol = g_chokanji_vol;
    } else if (strncmp(target, "/ANDERS/", 8) == 0 || strcmp(target, "/ANDERS") == 0) {
        target_vol = g_anders_vol;
    } else if (strncmp(target, "/SYS/", 5) == 0 || strcmp(target, "/SYS") == 0) {
        target_vol = g_sys_vol;
    } else if (target[0] == '/') {
        if (target[1] == '\0') {
            target_vol = v;
        } else {
            target_vol = NULL;
        }
    }

    if (!is_fid) {
        int all_digits = 1;
        for (int i = 0; target[i]; i++) {
            if (!isdigit((unsigned char)target[i])) { all_digits = 0; break; }
        }
        if (target[0] == '#') {
            num_str = target + 1;
            while (*num_str == ' ') num_str++;
            is_fid = 1;
        } else if ((target[0] == 'f' || target[0] == 'F') &&
                   (target[1] == 'i' || target[1] == 'I') &&
                   (target[2] == 'd' || target[2] == 'D') &&
                   target[3] == ':') {
            num_str = target + 4;
            while (*num_str == ' ') num_str++;
            is_fid = 1;
        } else if (all_digits && target[0]) {
            num_str = target;
            is_fid = 1;
        }
    }

    if (is_fid && num_str && *num_str) {
        unsigned long val = 0;
        const char *np = num_str;
        while (*np >= '0' && *np <= '9') {
            val = val * 10 + (unsigned long)(*np - '0');
            np++;
        }
        fid_val = (FID)val;
        if (out_fid) *out_fid = fid_val;
        if (out_is_fid) *out_is_fid = 1;
        if (out_vol) *out_vol = target_vol;

        if (!target_vol) return -1;
        return opn_fil_fid(target_vol, fid_val, mode);
    }

    if (out_vol) *out_vol = target_vol;
    if (!target_vol) return -1;

    /* Absolute / volume path */
    fd = opn_fil(target, mode);
    if (fd >= 0) return fd;

    /* Relative path: look only in current working volume */
    if (target[0] != '/') {
        char full[128];
        snprintf(full, sizeof(full), "%s/%s", g_cwd_path, target);
        fd = opn_fil(full, mode);
    }
    return fd;
}

static void clu_report_target_err(const char *cmd, const char *target, Volume *target_vol,
                                  FID fid_val, int is_fid, ShellOutputFn out, void *ud)
{
    char err[128];
    const char *vol_name = (target_vol == g_chokanji_vol && g_chokanji_vol) ? "/CHOKANJI" :
                           (target_vol == g_anders_vol && g_anders_vol) ? "/ANDERS" :
                           (target_vol == g_sys_vol && g_sys_vol) ? "/SYS" :
                           (!target_vol) ? "unmounted volume" : g_cwd_path;
    if (!target_vol) {
        if (target && target[0] == '/') {
            char vbuf[32];
            int vi = 0;
            const char *p = target;
            vbuf[vi++] = *p++;
            while (*p && *p != '/' && *p != '#' && vi < (int)sizeof(vbuf) - 1) {
                vbuf[vi++] = *p++;
            }
            vbuf[vi] = '\0';
            if (vi > 1)
                snprintf(err, sizeof(err), "%s: volume '%s' is not mounted", cmd, vbuf);
            else
                snprintf(err, sizeof(err), "%s: volume is not mounted", cmd);
        } else {
            snprintf(err, sizeof(err), "%s: volume is not mounted", cmd);
        }
    } else if (is_fid) {
        snprintf(err, sizeof(err), "%s: FID %u not found on %s", cmd, (unsigned)fid_val, vol_name);
    } else {
        snprintf(err, sizeof(err), "%s: '%s': not found on %s", cmd, target, vol_name);
    }
    out(err, COLOR_RED, ud);
}

void clu_fs_cmd(const char *args, ShellOutputFn out, void *ud)
{
    int flag_l = has_flag(args, "-l");
    int flag_r = has_flag(args, "-r") || has_flag(args, "-R");
    int flag_a = has_flag(args, "-a") || has_flag(args, "--all");
    int flag_g = has_flag(args, "-g") || has_flag(args, "--group");
    int flag_t = has_flag(args, "-t") || has_flag(args, "--tree");
    char target[80];
    get_target(args, target, sizeof(target));

    Volume *v = (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol :
                ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) ? g_chokanji_vol : g_sys_vol;
    if (target[0]) {
        if (strncmp(target, "/ANDERS", 7) == 0 && g_anders_vol) v = g_anders_vol;
        else if ((strncmp(target, "/CHOKANJI", 9) == 0 || strncmp(target, "/B-right", 8) == 0) && g_chokanji_vol) v = g_chokanji_vol;
        else if (strncmp(target, "/SYS", 4) == 0 && g_sys_vol) v = g_sys_vol;
    }
    if (!v) { out("fs: no volume mounted", COLOR_RED, ud); return; }

    if (flag_a || flag_g || flag_t) {
        UW nfmax = vol_nfmax(v);
        if (nfmax < 256) nfmax = 256;
        CluFsNode *nodes = (CluFsNode *)calloc(nfmax, sizeof(CluFsNode));
        if (!nodes) { out("fs: memory allocation failed", COLOR_RED, ud); return; }

        for (FID f = 0; f < nfmax; f++) {
            nodes[f].parent_fid = FID_INVALID;
            nodes[f].first_child = FID_INVALID;
            nodes[f].last_child = FID_INVALID;
            nodes[f].next_sibling = FID_INVALID;
        }

        UW bsize = vol_block_size(v);
        unsigned char *buf = (unsigned char *)malloc(bsize);
        unsigned char *hbuf = (unsigned char *)malloc(bsize);
        if (!buf || !hbuf) {
            if (buf) free(buf);
            if (hbuf) free(hbuf);
            free(nodes);
            return;
        }

        unsigned int count = 0;
        for (FID fid = 0; fid < nfmax; fid++) {
            UB refc = vol_fid_refcount(v, fid);
            BLK blk = vol_fid_get_blk(v, fid);
            if (refc == 0 && fid != FID_ROOT) continue;
            if (blk == 0 || blk == FID_INVALID) continue;

            if (vol_read_blk(v, blk, buf) != 0) continue;
            count++;

            nodes[fid].blk = blk;
            nodes[fid].refc = refc;

            if (vol_is_brightv(v)) {
                int has_hdr = 0;
                if (memcmp(buf, "Tron", 4) == 0 || memcmp(buf, "norT", 4) == 0) {
                    memcpy(hbuf, buf, bsize);
                    nodes[fid].hdr_blk = blk;
                    has_hdr = 1;
                    nodes[fid].is_stream = 0;
                } else if (blk > 0 && vol_read_blk(v, blk - 1, hbuf) == 0 &&
                           (memcmp(hbuf, "Tron", 4) == 0 || memcmp(hbuf, "norT", 4) == 0)) {
                    nodes[fid].hdr_blk = blk - 1;
                    has_hdr = 1;
                    nodes[fid].is_stream = 1;
                }

                if (has_hdr) {
                    nodes[fid].flags = clu_rd_u16_le(hbuf + 4);
                    nodes[fid].sz = clu_rd_u32_le(hbuf + 0x48);
                    UW cnt_44 = clu_rd_u32_le(hbuf + 0x44);
                    UW cnt_4c = clu_rd_u32_le(hbuf + 0x4c);
                    UW cnt_50 = clu_rd_u32_le(hbuf + 0x50);
                    nodes[fid].did = clu_rd_u32_le(hbuf + 0x64);
                    nodes[fid].pdid = clu_rd_u32_le(hbuf + 0x68);

                    UH tc[20];
                    for (int k = 0; k < 16; k++) tc[k] = clu_rd_u16_le(hbuf + 0x6C + k * 2);
                    tc[16] = 0;
                    char raw_name[48] = "";
                    btr_tcode_to_utf8(tc, 16, raw_name, sizeof(raw_name));

                    if (nodes[fid].is_stream) {
                        snprintf(nodes[fid].name, sizeof(nodes[fid].name), "[*] %s", raw_name[0] ? raw_name : "stream");
                    } else {
                        snprintf(nodes[fid].name, sizeof(nodes[fid].name), "%s", raw_name[0] ? raw_name : "-");
                    }

                    if (memcmp(buf, "\x7f\x45\x4c\x46", 4) == 0 || (nodes[fid].flags & 0x0001)) {
                        nodes[fid].is_elf = 1;
                    }

                    if (!nodes[fid].is_stream) {
                        UW max_scan = cnt_44;
                        if (cnt_4c > max_scan) max_scan = cnt_4c;
                        if (cnt_50 > max_scan) max_scan = cnt_50;
                        if (max_scan > 1024) max_scan = 1024;

                        for (UW k = 1; k <= max_scan; k++) {
                            int off = (int)bsize - (int)k * 16;
                            if (off < 0) break;
                            uint16_t kind = clu_rd_u16_le(hbuf + off);
                            if (kind == 0x8000) {
                                nodes[fid].is_dir = 1;
                                uint32_t raw_cfid = clu_rd_u32_le(hbuf + off + 4);
                                FID cfid = (raw_cfid < nfmax) ? (FID)raw_cfid : (FID)(raw_cfid & 0xFFFF);
                                if (cfid < nfmax && cfid != fid) {
                                    clu_fs_node_add_child(nodes, fid, cfid, nfmax);
                                }
                            }
                        }
                    }
                } else {
                    nodes[fid].is_stream = 1;
                    nodes[fid].sz = bsize;
                    if (memcmp(buf, "\x7f\x45\x4c\x46", 4) == 0) {
                        nodes[fid].is_elf = 1;
                        snprintf(nodes[fid].name, sizeof(nodes[fid].name), "[*] ELF_%u", (unsigned)fid);
                    } else {
                        snprintf(nodes[fid].name, sizeof(nodes[fid].name), "[*] stream_%u", (unsigned)fid);
                    }
                }
            } else {
                /* Cleanroom BTRON volume */
                nodes[fid].hdr_blk = blk;
                nodes[fid].flags = ((UH)buf[0] << 8) | buf[1];
                nodes[fid].sz = ((UW)buf[28] << 24) | ((UW)buf[29] << 16) | ((UW)buf[30] << 8) | buf[31];
                memcpy(nodes[fid].name, buf + 32, 40);
                nodes[fid].name[40] = '\0';
                if (nodes[fid].flags & 0x0001) nodes[fid].is_elf = 1;
                if (nodes[fid].flags & 0x1000) nodes[fid].is_dir = 1;
            }
        }

        /* Pass 2: Connect parent-child linkages */
        if (vol_is_brightv(v)) {
            for (FID f = 0; f < nfmax; f++) {
                if (nodes[f].blk == 0 || f == FID_ROOT) continue;
                if (nodes[f].parent_fid == FID_INVALID && nodes[f].pdid != 0) {
                    for (FID p = 0; p < nfmax; p++) {
                        if (nodes[p].blk == 0 || p == f) continue;
                        if (nodes[p].did == nodes[f].pdid) {
                            clu_fs_node_add_child(nodes, p, f, nfmax);
                            break;
                        }
                    }
                }
            }
        } else {
            const char *vdir = (v == g_anders_vol) ? "/ANDERS" : "/SYS";
            ID dir = opn_dir(vdir);
            if (dir >= 0) {
                DIR_ENTRY entry;
                while (rd_dir(dir, &entry) == 0) {
                    if (!entry.name[0]) continue;
                    FID efid = (FID)entry.robj_id;
                    if (efid < nfmax && efid != FID_ROOT) {
                        clu_fs_node_add_child(nodes, FID_ROOT, efid, nfmax);
                    }
                }
                cls_dir(dir);
            }
        }

        /* Check if a specific target FID or container was requested */
        FID start_fid = FID_INVALID;
        if (target[0]) {
            int all_digits = 1;
            const char *np = (target[0] == '#') ? target + 1 : target;
            while (*np == ' ') np++;
            for (int i = 0; np[i]; i++) {
                if (!isdigit((unsigned char)np[i])) { all_digits = 0; break; }
            }
            if (all_digits && *np) {
                start_fid = (FID)strtoul(np, NULL, 10);
            } else {
                for (FID f = 0; f < nfmax; f++) {
                    if (nodes[f].blk && strcmp(nodes[f].name, target) == 0) {
                        start_fid = f;
                        break;
                    }
                }
            }
        }

        if (flag_t) {
            char title[128];
            snprintf(title, sizeof(title), "=== Real Bodies on %s (Tree Structure) ===", vol_name(v));
            out(title, COLOR_CYAN, ud);

            if (flag_l)
                out("FID   BLK    REF  TYPE  STYPE : DID      PDID     : SIZE     NAME", COLOR_CYAN, ud);
            else
                out("FID   BLK    TYPE  SIZE       NAME", COLOR_CYAN, ud);

            if (start_fid != FID_INVALID && start_fid < nfmax && nodes[start_fid].blk) {
                clu_fs_print_node_tree(nodes, start_fid, 0, flag_l, nfmax, out, ud);
            } else {
                if (nodes[FID_ROOT].blk) {
                    clu_fs_print_node_tree(nodes, FID_ROOT, 0, flag_l, nfmax, out, ud);
                }
                for (FID f = 0; f < nfmax; f++) {
                    if (nodes[f].blk && !nodes[f].visited && nodes[f].parent_fid == FID_INVALID) {
                        clu_fs_print_node_tree(nodes, f, 0, flag_l, nfmax, out, ud);
                    }
                }
                for (FID f = 0; f < nfmax; f++) {
                    if (nodes[f].blk && !nodes[f].visited) {
                        clu_fs_print_node_tree(nodes, f, 0, flag_l, nfmax, out, ud);
                    }
                }
            }
        } else if (flag_g) {
            char title[128];
            snprintf(title, sizeof(title), "=== Real Bodies on %s (Grouped by [DIR]) ===", vol_name(v));
            out(title, COLOR_CYAN, ud);

            if (flag_l)
                out("FID   BLK    REF  TYPE  STYPE : DID      PDID     : SIZE     NAME", COLOR_CYAN, ud);
            else
                out("FID   BLK    TYPE  SIZE       NAME", COLOR_CYAN, ud);

            if (start_fid != FID_INVALID && start_fid < nfmax && nodes[start_fid].blk) {
                /* Single container group */
                clu_fs_print_node_line(nodes, start_fid, 0, flag_l, 0, out, ud);
                UW hop = 0;
                for (FID c = nodes[start_fid].first_child; c != FID_INVALID && hop < nfmax; c = nodes[c].next_sibling, hop++) {
                    if (c < nfmax && nodes[c].blk) {
                        clu_fs_print_node_line(nodes, c, 2, flag_l, 0, out, ud);
                    }
                }
            } else {
                /* Volume-wide FID table grouped by parent container in FID order */
                for (FID p = 0; p < nfmax; p++) {
                    if (nodes[p].blk == 0) continue;
                    if (nodes[p].first_child != FID_INVALID) {
                        clu_fs_print_node_line(nodes, p, 0, flag_l, 0, out, ud);
                        UW hop = 0;
                        for (FID c = nodes[p].first_child; c != FID_INVALID && hop < nfmax; c = nodes[c].next_sibling, hop++) {
                            if (c < nfmax && nodes[c].blk) {
                                clu_fs_print_node_line(nodes, c, 2, flag_l, 0, out, ud);
                            }
                        }
                    }
                }
                /* Unparented standalone bodies that have no parent and no children */
                for (FID f = 0; f < nfmax; f++) {
                    if (nodes[f].blk && nodes[f].parent_fid == FID_INVALID && nodes[f].first_child == FID_INVALID) {
                        clu_fs_print_node_line(nodes, f, 0, flag_l, 0, out, ud);
                    }
                }
            }
        } else {
            char title[128];
            snprintf(title, sizeof(title), "=== Real Bodies on %s (FID table 0..%u) ===",
                     vol_name(v), (unsigned)nfmax - 1);
            out(title, COLOR_CYAN, ud);

            if (flag_l)
                out("FID   BLK    REF  TYPE  STYPE : DID      PDID     : SIZE     PARENT       NAME", COLOR_CYAN, ud);
            else
                out("FID   BLK    TYPE  SIZE       PARENT       NAME", COLOR_CYAN, ud);

            for (FID fid = 0; fid < nfmax; fid++) {
                if (nodes[fid].blk == 0) continue;

                char parent_str[48];
                if (nodes[fid].parent_fid != FID_INVALID && nodes[fid].parent_fid < nfmax) {
                    FID pf = nodes[fid].parent_fid;
                    const char *pname = nodes[pf].name;
                    if (strncmp(pname, "[*] ", 4) == 0) pname += 4;
                    if (pname[0] && strcmp(pname, "-") != 0) {
                        snprintf(parent_str, sizeof(parent_str), "%u(%s)", (unsigned)pf, pname);
                    } else {
                        snprintf(parent_str, sizeof(parent_str), "%u", (unsigned)pf);
                    }
                } else {
                    snprintf(parent_str, sizeof(parent_str), "-");
                }

                int kind = nodes[fid].is_dir ? 3 : (nodes[fid].is_elf ? 2 : (nodes[fid].is_stream ? 4 : 0));
                const char *tag = (kind == 3) ? "[DIR]" :
                                  (kind == 2) ? "[ELF]" :
                                  (kind == 4) ? "[STR]" : "[TAD]";
                UW color = clu_kind_color(kind, (kind == 3));

                char line[256];
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%-5u %-6u %-4u %-5s %04X  : %08X %08X : %-10u %-12s %s",
                             (unsigned)fid, (unsigned)nodes[fid].blk, (unsigned)nodes[fid].refc,
                             tag, (unsigned)nodes[fid].flags,
                             nodes[fid].did, nodes[fid].pdid,
                             nodes[fid].sz, parent_str, nodes[fid].name);
                } else {
                    snprintf(line, sizeof(line),
                             "%-5u %-6u %-5s %-10u %-12s %s",
                             (unsigned)fid, (unsigned)nodes[fid].blk, tag, nodes[fid].sz,
                             parent_str, nodes[fid].name);
                }
                out(line, color, ud);

                if (flag_r && (kind == 3 || kind == 0)) {
                    ID cfd = opn_fil_fid(v, fid, 0x0001);
                    if (cfd >= 0) {
                        OpenFile *cof = &g_open_files[(int)cfd];
                        if (cof->nrec > 0 && !(cof->nrec == 1 && cof->ridx[0].size == bsize && cof->ridx[0].type == 0)) {
                            unsigned char *vrec = (unsigned char *)calloc(nfmax, 1);
                            if (vrec) {
                                clu_fs_dump_records(v, cfd, fid, nodes[fid].name[0] ? nodes[fid].name : "body", 1, flag_l, 0, vrec, nfmax, out, ud);
                                free(vrec);
                            }
                        }
                        cls_fil(cfd);
                    }
                }
            }
        }

        free(buf);
        free(hbuf);
        free(nodes);
        char summary[80];
        snprintf(summary, sizeof(summary), "(%u real bodies total)", count);
        out(summary, COLOR_LTGRAY, ud);
        return;
    }

    const char *path = target[0] ? target : g_cwd_path;
    Volume *target_vol = NULL;
    FID fid_val = FID_INVALID;
    int is_fid = 0;
    ID fd = clu_open_target(path, 0x0001, v, &target_vol, &fid_val, &is_fid);

    if (fd < 0) {
        clu_report_target_err("fs", path, target_vol, fid_val, is_fid, out, ud);
        return;
    }

    OpenFile *of = &g_open_files[(int)fd];
    v = of_vol(of);

    if (flag_l)
        out("NO: 0 STYPE : FID   PARENT [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME", COLOR_CYAN, ud);
    else
        out("NO: TYPE STYPE : FID   PARENT : SIZE / NAME", COLOR_CYAN, ud);

    UW nfmax = vol_nfmax(v);
    if (nfmax < 256) nfmax = 256;
    unsigned char *visited = (unsigned char *)calloc(nfmax, 1);
    if (!visited) { cls_fil(fd); return; }

    clu_fs_dump_records(v, fd, (FID)0, path, 0, flag_l, flag_r, visited, nfmax, out, ud);
    free(visited);
    cls_fil(fd);
}

/* ── clu_stat ────────────────────────────────────────────────────── */
static void clu_stat_internal(const char *cmd_name, const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));

    if (!target[0]) {
        char err[128];
        snprintf(err, sizeof(err), "%s: missing file argument (usage: %s <file|FID>)", cmd_name, cmd_name);
        out(err, COLOR_RED, ud);
        return;
    }

    Volume *v = (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol :
                ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) ? g_chokanji_vol : g_sys_vol;

    Volume *target_vol = NULL;
    FID fid_val = FID_INVALID;
    int is_fid = 0;
    ID fd = clu_open_target(target, 0x0001, v, &target_vol, &fid_val, &is_fid);
    if (fd < 0) {
        clu_report_target_err(cmd_name, target, target_vol, fid_val, is_fid, out, ud);
        return;
    }

    OpenFile *of = &g_open_files[(int)fd];
    v = of_vol(of);

    char line[160];
    const char *vol_name = (v == g_chokanji_vol) ? "CHOKANJI" :
                           (v == g_anders_vol) ? "ANDERS" : "SYS";
    const char *vol_desc = vol_is_brightv(v) ? "B-right/V 4.02 (Cho-Kanji)" : "Cleanroom BTRON3";

    snprintf(line, sizeof(line), "  File: %s", of->hdr.name[0] ? (const char *)of->hdr.name : "(unnamed)");
    out(line, COLOR_WHITE, ud);

    snprintf(line, sizeof(line), "   FID: %-5u (0x%04X)      Volume: /%s [%s]",
             (unsigned)of->fid, (unsigned)of->fid, vol_name, vol_desc);
    out(line, COLOR_CYAN, ud);

    snprintf(line, sizeof(line), "Blocks: Header=%u  Data=%u",
             (unsigned)of->hdr_blk, (unsigned)of->data_blk);
    out(line, COLOR_LTGRAY, ud);

    snprintf(line, sizeof(line), "  Size: %-10u bytes     Used: %-10u   Records: %u",
             (unsigned)of->hdr.total_size, (unsigned)of->data_used, (unsigned)of->nrec);
    out(line, COLOR_LTGRAY, ud);

    /* Format / Kind */
    int kind = clu_probe_kind(v, (unsigned int)of->fid);
    const char *kind_str = (kind == 2) ? "ELF 32-bit Executable (i386)" :
                           (kind == 1 || (of->hdr.flags & 0x8000) || of->ridx[0].kind == 0x9F00) ? "Executable Binary (OBJ_EXEC / Program)" :
                           (of->nrec > 0 && of->ridx[0].kind == 0x8000) ? "Directory Drawer / Container" :
                           (of->nrec > 0 && of->ridx[0].type == 1) ? "TAD Document / Text" : "Data Stream";
    snprintf(line, sizeof(line), "Format: %s", kind_str);
    out(line, clu_kind_color(kind, 0), ud);

    /* Access & Flags */
    char flags_desc[64];
    int fpos = 0;
    if (of->hdr.flags & 0x8000) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "EXEC ");
    if (of->hdr.flags & 0x0001) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "WPROTECT ");
    if (of->hdr.flags & 0x0002) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "DPROTECT ");
    if (fpos == 0) snprintf(flags_desc, sizeof(flags_desc), "NORMAL");

    snprintf(line, sizeof(line), "Access: ATYPE=%04X  Flags=%04X (%s)  Nlnk=%u",
             (unsigned)of->hdr.atype, (unsigned)of->hdr.flags, flags_desc, (unsigned)of->hdr.nlnk);
    out(line, COLOR_LTGRAY, ud);

    /* Timestamps */
    char mt[24], ct[24], at[24];
    fmt_ts(of->hdr.mtime, mt, sizeof(mt));
    fmt_ts(of->hdr.ctime, ct, sizeof(ct));
    fmt_ts(of->hdr.atime, at, sizeof(at));
    snprintf(line, sizeof(line), "Modify: %s (0x%08X)", mt, (unsigned)of->hdr.mtime);
    out(line, COLOR_LTGRAY, ud);
    snprintf(line, sizeof(line), "Access: %s (0x%08X)", at, (unsigned)of->hdr.atime);
    out(line, COLOR_LTGRAY, ud);
    snprintf(line, sizeof(line), "Create: %s (0x%08X)", ct, (unsigned)of->hdr.ctime);
    out(line, COLOR_LTGRAY, ud);

    /* Records Header */
    if (of->nrec > 0) {
        out("Records:", COLOR_CYAN, ud);
        out("  REC  KIND    TYPE    OFFSET      SIZE        FLAGS", COLOR_LTGRAY, ud);
        out("  ---  ------  ------  ----------  ----------  -----", COLOR_LTGRAY, ud);
        for (unsigned int i = 0; i < of->nrec; i++) {
            snprintf(line, sizeof(line), "  [%2u] 0x%04X  0x%04X  %-10u  %-10u  %u",
                     i, (unsigned)of->ridx[i].kind, (unsigned)of->ridx[i].type,
                     (unsigned)of->ridx[i].offset, (unsigned)of->ridx[i].size,
                     (unsigned)of->ridx[i].flags);
            out(line, COLOR_WHITE, ud);

            /* Inspect record content if available */
            if (of->ridx[i].size > 0 && of->ridx[i].size <= 512) {
                ID rec = opn_rec(fd, (W)i, 0x0001);
                if (rec >= 0) {
                    unsigned char pbuf[128];
                    W got = 0;
                    rd_rec(rec, pbuf, sizeof(pbuf), &got);
                    cls_rec(rec);
                    if (got >= 4) {
                        /* Check for TRON coded link or text */
                        if (pbuf[1] == 0x23 || pbuf[3] == 0x23) {
                            UH tc[64];
                            int tclen = (int)got / 2;
                            if (tclen > 60) tclen = 60;
                            for (int k = 0; k < tclen; k++) tc[k] = (UH)(pbuf[k*2] | (pbuf[k*2+1] << 8));
                            tc[tclen] = 0;
                            char utf8[128];
                            btr_tcode_to_utf8(tc, tclen, utf8, sizeof(utf8));
                            if (utf8[0]) {
                                snprintf(line, sizeof(line), "       -> Link/Text: \"%s\"", utf8);
                                out(line, COLOR_CYAN, ud);
                            }
                        } else if ((of->ridx[i].type == 0 || of->ridx[i].type == RT_LINK) && !vol_is_brightv(v)) {
                            FID lfid = (FID)((pbuf[0]<<24)|(pbuf[1]<<16)|(pbuf[2]<<8)|pbuf[3]);
                            snprintf(line, sizeof(line), "       -> Target FID: %u", (unsigned)lfid);
                            out(line, COLOR_CYAN, ud);
                        }
                    }
                }
            } else if (vol_is_brightv(v) && of->ridx[i].kind == 0x8000) {
                UW nfmax = vol_nfmax(v);
                uint32_t raw_cfid = of->ridx[i].offset;
                FID cfid = (raw_cfid < nfmax) ? (FID)raw_cfid : (FID)(raw_cfid & 0xFFFF);
                if (cfid < nfmax) {
                    ID cfd = opn_fil_fid(v, cfid, 0x0001);
                    if (cfd >= 0) {
                        snprintf(line, sizeof(line), "       -> Child FID: %u (\"%s\")", (unsigned)cfid, g_open_files[(int)cfd].hdr.name);
                        cls_fil(cfd);
                    } else {
                        snprintf(line, sizeof(line), "       -> Child FID: %u", (unsigned)cfid);
                    }
                    out(line, COLOR_CYAN, ud);
                }
            }
        }
    } else {
        out("Records: (0 records)", COLOR_LTGRAY, ud);
    }

    cls_fil(fd);
}

void clu_stat(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("stat", args, out, ud);
}

void clu_info(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("info", args, out, ud);
}

void clu_finfo(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("finfo", args, out, ud);
}

/* ── clu_tp ──────────────────────────────────────────────────────── */
void clu_tp(const char *args, ShellOutputFn out, void *ud)
{
    int flag_x = has_flag(args, "-x");
    int flag_a = has_flag(args, "-a");
    char target[80];
    get_target(args, target, sizeof(target));

    if (!target[0]) { out("tp: missing file argument", COLOR_RED, ud); return; }

    Volume *v = (strncmp(g_cwd_path, "/ANDERS", 7) == 0 && g_anders_vol) ? g_anders_vol :
                ((strncmp(g_cwd_path, "/CHOKANJI", 9) == 0 || strncmp(g_cwd_path, "/B-right", 8) == 0) && g_chokanji_vol) ? g_chokanji_vol : g_sys_vol;

    Volume *target_vol = NULL;
    FID fid_val = FID_INVALID;
    int is_fid = 0;
    ID fd = clu_open_target(target, 0x0001, v, &target_vol, &fid_val, &is_fid);
    if (fd < 0) {
        clu_report_target_err("tp", target, target_vol, fid_val, is_fid, out, ud);
        return;
    }

    OpenFile *of = &g_open_files[(int)fd];
    v = of_vol(of);

    unsigned char payload[1024 * 4];
    W got = 0;
    int found_rec = 0;

    for (unsigned int i = 0; i < of->nrec; i++) {
        if (!vol_is_brightv(v) && of->ridx[i].type == RT_LINK) continue;
        if (vol_is_brightv(v) && (of->ridx[i].kind == 0x8000 || of->ridx[i].type == 0x0080) && of->ridx[i].size == 0) continue;
        if (of->ridx[i].size == 0) continue;

        ID rec = opn_rec(fd, (W)i, 0x0001);
        if (rec < 0) continue;

        UW to_read = of->ridx[i].size;
        if (to_read > sizeof(payload)) to_read = sizeof(payload);
        rd_rec(rec, payload, (W)to_read, &got);
        cls_rec(rec);
        found_rec = 1;
        break;
    }

    if (!found_rec) {
        BLK dblk = of->data_blk ? of->data_blk : of->hdr_blk;
        if (dblk > 0 && dblk != FID_INVALID) {
            unsigned char *bbuf = (unsigned char *)malloc(vol_block_size(v));
            if (bbuf) {
                if (vol_read_blk(v, dblk, bbuf) == 0) {
                    UW sz = of->hdr.total_size > 0 ? of->hdr.total_size : vol_block_size(v);
                    if (sz > sizeof(payload)) sz = sizeof(payload);
                    memcpy(payload, bbuf, sz);
                    got = (W)sz;
                }
                free(bbuf);
            }
        }
    }

    if (got == 0) {
        out("(0 bytes)", COLOR_LTGRAY, ud);
        cls_fil(fd);
        return;
    }

    if (flag_x || flag_a) {
        /* Hex / ASCII dump */
        char hexline[80];
        for (int off = 0; off < (int)got; off += 16) {
            int chunk = ((int)got - off < 16) ? (int)got - off : 16;
            if (flag_x) {
                int pos = snprintf(hexline, sizeof(hexline), "%04X:", off);
                for (int j = 0; j < chunk; j++)
                    pos += snprintf(hexline + pos, sizeof(hexline) - (size_t)pos,
                                    " %02X", payload[off + j]);
            } else {
                int pos = 0;
                for (int j = 0; j < chunk; j++) {
                    unsigned char c = payload[off + j];
                    hexline[pos++] = (c >= 32 && c < 127) ? (char)c : '.';
                }
                hexline[pos] = '\0';
            }
            out(hexline, COLOR_LTGRAY, ud);
        }
    } else if (got >= 4 && memcmp(payload, "\x7f\x45\x4c\x46", 4) == 0) {
        /* Plain view of ELF Binary: print header summary & hex preview */
        char banner[128];
        snprintf(banner, sizeof(banner), "[ELF 32-bit LSB Executable (i386)] (Size: %u bytes)",
                 of->hdr.total_size > 0 ? of->hdr.total_size : (UW)got);
        out(banner, COLOR_GREEN, ud);
        for (int off = 0; off < 64 && off < (int)got; off += 16) {
            char hexline[80];
            int chunk = ((int)got - off < 16) ? (int)got - off : 16;
            int pos = snprintf(hexline, sizeof(hexline), "%04X:", off);
            for (int j = 0; j < chunk; j++)
                pos += snprintf(hexline + pos, sizeof(hexline) - (size_t)pos, " %02X", payload[off + j]);
            out(hexline, COLOR_LTGRAY, ud);
        }
    } else if (got >= 4 && (payload[1] == 0x23 || payload[3] == 0x23)) {
        /* TRON-coded ASCII string (e.g. cpp include link sys/errno.h) */
        UH tc[64];
        int tclen = (int)got / 2;
        if (tclen > 60) tclen = 60;
        for (int k = 0; k < tclen; k++) tc[k] = (UH)(payload[k*2] | (payload[k*2+1] << 8));
        tc[tclen] = 0;
        char utf8[128];
        btr_tcode_to_utf8(tc, tclen, utf8, sizeof(utf8));
        if (utf8[0]) {
            out(utf8, COLOR_CYAN, ud);
        }
    } else {
        /* Plain text: print printable bytes, parse TAD TS_TEXT segments */
        int off = 0;
        char textbuf[512];
        int tblen = 0;
        while (off < (int)got) {
            if (payload[off] == 0xFF && off + 3 < (int)got) {
                if (tblen > 0) { textbuf[tblen] = '\0'; out(textbuf, COLOR_LTGRAY, ud); tblen = 0; }
                unsigned char seg_id = payload[off + 1];
                int seg_len = ((int)payload[off+2] << 8) | payload[off+3];
                if (seg_id == 0xE1 /* TS_TEXT */) {
                    int start = off + 4;
                    int end   = start + seg_len;
                    if (end > (int)got) end = (int)got;
                    for (int j = start; j < end; j++) {
                        unsigned char c = payload[j];
                        if (c >= 32 && c < 127 && tblen < 510)
                            textbuf[tblen++] = (char)c;
                        else if ((c == '\n' || tblen > 200)) {
                            textbuf[tblen] = '\0';
                            out(textbuf, COLOR_LTGRAY, ud);
                            tblen = 0;
                        }
                    }
                }
                off += 4 + seg_len;
            } else {
                unsigned char c = payload[off];
                if (c >= 32 && c < 127 && tblen < 510) textbuf[tblen++] = (char)c;
                else if (c == '\n' || tblen > 200) {
                    textbuf[tblen] = '\0'; out(textbuf, COLOR_LTGRAY, ud); tblen = 0;
                }
                off++;
            }
        }
        if (tblen > 0) { textbuf[tblen] = '\0'; out(textbuf, COLOR_LTGRAY, ud); }
    }
    cls_fil(fd);
}

/* ── clu_mkf ─────────────────────────────────────────────────────── */
void clu_mkf(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));
    if (!target[0]) { out("mkf: missing path argument", COLOR_RED, ud); return; }
    ID fd = cre_fil(target, 0x0002);
    if (fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "mkf: cannot create '%s'", target);
        out(err, COLOR_RED, ud); return;
    }
    cls_fil(fd);
    char msg[128]; snprintf(msg, sizeof(msg), "Created '%s'", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_rm ──────────────────────────────────────────────────────── */
void clu_rm(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));
    if (!target[0]) { out("rm: missing path argument", COLOR_RED, ud); return; }
    ER err = del_fil(target);
    if (err != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "rm: cannot remove '%s'", target);
        out(msg, COLOR_RED, ud); return;
    }
    char msg[128]; snprintf(msg, sizeof(msg), "Removed '%s'", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_ren ─────────────────────────────────────────────────────── */
void clu_ren(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = get_targets(args, targets, 2);
    if (n < 2) { out("ren: usage: ren <old> <new>", COLOR_RED, ud); return; }
    ER err = mov_fil(targets[0], targets[1]);
    if (err != 0) {
        out("ren: rename failed", COLOR_RED, ud); return;
    }
    char msg[128]; snprintf(msg, sizeof(msg), "Renamed '%s' -> '%s'", targets[0], targets[1]);
    out(msg, COLOR_GREEN, ud);
    vol_sync(g_sys_vol);
}

/* ── clu_cp ──────────────────────────────────────────────────────── */
void clu_cp(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = get_targets(args, targets, 2);
    if (n < 2) { out("cp: usage: cp <src> <dst>", COLOR_RED, ud); return; }

    ID src_fd = opn_fil(targets[0], 0x0001);
    if (src_fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "cp: '%s': not found", targets[0]);
        out(err, COLOR_RED, ud); return;
    }
    OpenFile *src_of = &g_open_files[(int)src_fd];
    unsigned int nrec = src_of->nrec;

    ID dst_fd = cre_fil(targets[1], 0x0002);
    if (dst_fd < 0) {
        cls_fil(src_fd);
        char err[128]; snprintf(err, sizeof(err), "cp: cannot create '%s'", targets[1]);
        out(err, COLOR_RED, ud); return;
    }

    /* Copy all records */
    for (unsigned int i = 0; i < nrec; i++) {
        UW sz = src_of->ridx[i].size;
        if (sz == 0) { ins_rec(dst_fd, (W)i, NULL, 0); continue; }
        unsigned char *pbuf = (unsigned char *)malloc(sz);
        if (!pbuf) break;
        ID rec = opn_rec(src_fd, (W)i, 0x0001);
        if (rec >= 0) {
            W got = 0;
            rd_rec(rec, pbuf, (W)sz, &got);
            cls_rec(rec);
            ins_rec(dst_fd, (W)i, pbuf, (W)got);
        }
        free(pbuf);
    }
    cls_fil(dst_fd);
    cls_fil(src_fd);
    char msg[128]; snprintf(msg, sizeof(msg), "Copied '%s' -> '%s'", targets[0], targets[1]);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_ln ──────────────────────────────────────────────────────── */
void clu_ln(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = get_targets(args, targets, 2);
    if (n < 2) { out("ln: usage: ln <src> <linkname>", COLOR_RED, ud); return; }

    ID src_fd = opn_fil(targets[0], 0x0001);
    if (src_fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "ln: '%s': not found", targets[0]);
        out(err, COLOR_RED, ud); return;
    }
    FID src_fid = g_open_files[(int)src_fd].fid;
    cls_fil(src_fd);

    FS_LINK lnk; memset(&lnk, 0, sizeof(lnk));
    lnk.target_fid = src_fid;

    ER err = cre_lnk(targets[1], &lnk);
    if (err != 0) { out("ln: cre_lnk failed", COLOR_RED, ud); return; }
    char msg[128]; snprintf(msg, sizeof(msg), "Link '%s' -> '%s'", targets[1], targets[0]);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_empf ────────────────────────────────────────────────────── */
void clu_empf(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));
    if (!target[0]) { out("empf: missing argument", COLOR_RED, ud); return; }

    ID fd = opn_fil(target, 0x0002);
    if (fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "empf: '%s': not found", target);
        out(err, COLOR_RED, ud); return;
    }
    OpenFile *of = &g_open_files[(int)fd];
    while (of->nrec > 0) {
        del_rec(fd, 0);
    }
    cls_fil(fd);
    char msg[128]; snprintf(msg, sizeof(msg), "Emptied '%s'", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_apd ─────────────────────────────────────────────────────── */
void clu_apd(const char *args, ShellOutputFn out, void *ud)
{
    /* Minimal: -d#.# delete records, else append from path2 to path1 */
    char targets[2][80];
    int n = get_targets(args, targets, 2);

    /* Check for delete flag: -d<start>.<count> */
    const char *p = args;
    p = skip_ws(p);
    if (strncmp(p, "-d", 2) == 0) {
        p += 2;
        int start = 0, count = 1;
        while (*p >= '0' && *p <= '9') start = start * 10 + (*p++ - '0');
        if (*p == '.') { p++; count = 0; while (*p >= '0' && *p <= '9') count = count * 10 + (*p++ - '0'); }
        if (n < 1) { out("apd -d: missing path", COLOR_RED, ud); return; }
        ID fd = opn_fil(targets[0], 0x0002);
        if (fd < 0) { out("apd: cannot open file", COLOR_RED, ud); return; }
        for (int i = 0; i < count; i++) del_rec(fd, (W)start);
        cls_fil(fd);
        char msg[80]; snprintf(msg, sizeof(msg), "Deleted %d record(s) from '%s'", count, targets[0]);
        out(msg, COLOR_GREEN, ud);
        return;
    }
    out("apd: (use -d#.# to delete records; full apd not yet implemented)", COLOR_YELLOW, ud);
}

/* ── clu_chmod ───────────────────────────────────────────────────── */
void clu_chmod(const char *args, ShellOutputFn out, void *ud)
{
    /* -a1 set write-protect, -a2 clear write-protect, -a3 set del-protect, -a4 clear */
    const char *p = args;
    p = skip_ws(p);
    int aflag = 0;
    if (strncmp(p, "-a", 2) == 0) { p += 2; aflag = (*p++) - '0'; }
    p = skip_ws(p);
    char target[80] = {0};
    int i = 0;
    if (*p == '"') { p++; while (*p && *p != '"' && i < 79) target[i++] = *p++; }
    else { while (*p && !isspace((unsigned char)*p) && i < 79) target[i++] = *p++; }
    target[i] = '\0';

    if (!target[0]) { out("chmod: missing path", COLOR_RED, ud); return; }

    Volume *v = g_sys_vol;
    ID fd = opn_fil(target, 0x0002);
    if (fd < 0) { out("chmod: file not found", COLOR_RED, ud); return; }
    OpenFile *of = &g_open_files[(int)fd];
    switch (aflag) {
        case 1: of->hdr.flags |=  0x0010; break; /* set write-protect O */
        case 2: of->hdr.flags &= ~0x0010; break;
        case 3: of->hdr.flags |=  0x0020; break; /* set delete-protect P */
        case 4: of->hdr.flags &= ~0x0020; break;
    }
    of->dirty = 1;
    cls_fil(fd);
    vol_mark_dirty(v);
    out("Attribute changed", COLOR_GREEN, ud);
}

/* ── clu_touch ───────────────────────────────────────────────────── */
void clu_touch(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));
    if (!target[0]) { out("touch: missing path", COLOR_RED, ud); return; }

    ID fd = opn_fil(target, 0x0002);
    if (fd < 0) {
        /* Create empty file */
        fd = cre_fil(target, 0x0002);
        if (fd < 0) { out("touch: cannot create file", COLOR_RED, ud); return; }
        cls_fil(fd);
        char msg[128]; snprintf(msg, sizeof(msg), "Created '%s'", target);
        out(msg, COLOR_GREEN, ud); return;
    }
    OpenFile *of = &g_open_files[(int)fd];
    UW ts = (UW)(
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        time(NULL) - 946684800L
#else
        0
#endif
    );
    of->hdr.atime = ts;
    of->hdr.mtime = ts;
    of->dirty = 1;
    cls_fil(fd);
    char msg[128]; snprintf(msg, sizeof(msg), "Touched '%s'", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_chtime ──────────────────────────────────────────────────── */
void clu_chtime(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = get_targets(args, targets, 2);
    if (n < 1) { out("chtime: usage: chtime [-m] [time] <path>", COLOR_RED, ud); return; }
    const char *tpath = (n >= 2) ? targets[1] : targets[0];

    ID fd = opn_fil(tpath, 0x0002);
    if (fd < 0) { out("chtime: file not found", COLOR_RED, ud); return; }
    OpenFile *of = &g_open_files[(int)fd];
    UW ts = (UW)(
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
        time(NULL) - 946684800L
#else
        0
#endif
    );
    of->hdr.atime = ts;
    of->hdr.mtime = ts;
    of->dirty = 1;
    cls_fil(fd);
    vol_mark_dirty(g_sys_vol);
    char msg[128];
    snprintf(msg, sizeof(msg), "Updated timestamps for '%s'", tpath);
    out(msg, COLOR_GREEN, ud);
}

static void clu_df_print_volume(Volume *v, const char *mount_path, const char *dev_name, ShellOutputFn out, void *ud)
{
    if (!v) return;
    UW total = vol_total_blocks(v);
    UW free_blks = vol_free_blocks(v);
    UW used  = total - free_blks;
    UW pct   = (total > 0) ? (used * 100 / total) : 0;
    char tot_s[16], free_s[16], pct_s[16];
    snprintf(tot_s, sizeof(tot_s), "%uK", total);
    snprintf(free_s, sizeof(free_s), "%uK", free_blks);
    snprintf(pct_s, sizeof(pct_s), "%u%%", pct);
    char line[128];
    snprintf(line, sizeof(line),
             "%-9s %-5s %-7s %-7s %-6s %-5u %-8u %s",
             mount_path, dev_name,
             tot_s,
             free_s,
             pct_s,
             vol_block_size(v),
             vol_nfmax(v),
             vol_name(v));
    out(line, COLOR_LTGRAY, ud);
}

/* ── clu_df ──────────────────────────────────────────────────────── */
void clu_df(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    get_target(args, target, sizeof(target));

    if (!g_sys_vol && !g_anders_vol && !g_chokanji_vol) {
        out("df: no volume mounted", COLOR_RED, ud);
        return;
    }

    int show_sys = 1;
    int show_anders = (g_anders_vol && g_anders_vol != g_sys_vol);
    int show_chokanji = (g_chokanji_vol != NULL);

    if (target[0]) {
        if (strcmp(target, "/SYS") == 0 || strcmp(target, "SYS") == 0) {
            show_sys = 1;
            show_anders = 0;
            show_chokanji = 0;
        } else if (strcmp(target, "/ANDERS") == 0 || strcmp(target, "ANDERS") == 0) {
            show_sys = 0;
            show_anders = (g_anders_vol && g_anders_vol != g_sys_vol);
            show_chokanji = 0;
            if (!show_anders) {
                out("df: '/ANDERS': volume not mounted", COLOR_RED, ud);
                return;
            }
        } else if (strcmp(target, "/CHOKANJI") == 0 || strcmp(target, "CHOKANJI") == 0 ||
                   strcmp(target, "/B-right") == 0 || strcmp(target, "B-right") == 0) {
            show_sys = 0;
            show_anders = 0;
            show_chokanji = (g_chokanji_vol != NULL);
            if (!show_chokanji) {
                out("df: '/CHOKANJI': volume not mounted", COLOR_RED, ud);
                return;
            }
        } else {
            char err[128];
            snprintf(err, sizeof(err), "df: '%s': no such volume", target);
            out(err, COLOR_RED, ud);
            return;
        }
    }

    out("PATH      DEV   TOTAL   FREE    USED   UNIT  MAXFILE  NAME", COLOR_CYAN, ud);

    if (show_sys && g_sys_vol) {
        clu_df_print_volume(g_sys_vol, "/SYS", "mem0", out, ud);
    }
    if (show_anders && g_anders_vol) {
        clu_df_print_volume(g_anders_vol, "/ANDERS", "mem1", out, ud);
    }
    if (show_chokanji && g_chokanji_vol) {
        clu_df_print_volume(g_chokanji_vol, "/CHOKANJI", "hda1", out, ud);
    }
}

/* ── clu_sync_cmd ────────────────────────────────────────────────── */
void clu_sync_cmd(const char *args, ShellOutputFn out, void *ud)
{
    (void)args;
    if (!g_sys_vol) { out("sync: no volume mounted", COLOR_YELLOW, ud); return; }
    vol_sync(g_sys_vol);
    out("(all caches flushed)", COLOR_GREEN, ud);
}
