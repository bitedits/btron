/*
 * B-System BTRON3 — clu_fs.c
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
   extern void  Ifree(void *);
   extern int snprintf(char *, size_t, const char *, ...);
   extern void *tkl_memset(void *, int, size_t);
   extern void *tkl_memcpy(void *, const void *, size_t);
   extern int   tkl_strcmp(const char *, const char *);
   extern int   tkl_strncmp(const char *, const char *, size_t);
   extern size_t tkl_strlen(const char *);
   extern char  *tkl_strncpy(char *, const char *, size_t);
#  define malloc   Imalloc
#  define free     Ifree
#  define memset   tkl_memset
#  define memcpy   tkl_memcpy
#  define strcmp   tkl_strcmp
#  define strncmp  tkl_strncmp
#  define strlen   tkl_strlen
#  define strncpy  tkl_strncpy
#  define isspace(c) ((c)==' '||(c)=='\t'||(c)=='\n'||(c)=='\r')
#endif

#include "clu_fs.h"
#include <btron/fs/vol_api.h>
#include <btron/fs/fs_internal.h>
#include <btron/fs/header.h>
#include <btron/fs/record.h>
#include <btron/file.h>
#include <btron/tad.h>
#include <btron/dp.h>   /* COLOR_* constants */

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

    if (strcmp(target, "/SYS") == 0 || strcmp(target, "/") == 0 || strcmp(target, "..") == 0) {
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/SYS");
        out("[/SYS]", COLOR_GREEN, ud);
        return;
    }

    /* Verify the file exists on the volume */
    ID fd = opn_fil(target, 0x0001 /* F_READ */);
    if (fd < 0) {
        char err[128];
        snprintf(err, sizeof(err), "cd: '%s': no such file", target);
        out(err, COLOR_RED, ud);
        return;
    }
    cls_fil(fd);
    snprintf(g_cwd_path, sizeof(g_cwd_path), "/SYS/%s", target);
    char msg[128];
    snprintf(msg, sizeof(msg), "[%s]", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_ls ──────────────────────────────────────────────────────── */
void clu_ls(const char *args, ShellOutputFn out, void *ud)
{
    Volume *v = g_sys_vol;
    if (!v) { out("ls: no volume mounted", COLOR_RED, ud); return; }

    int flag_l = has_flag(args, "-l");
    int flag_t = has_flag(args, "-t");

    if (flag_l) {
        out("ATYPE ATR NREC NREF SIZE  MTIME            NAME", COLOR_CYAN, ud);
    } else if (flag_t) {
        out("CTIME              ATIME              MTIME              NAME", COLOR_CYAN, ud);
    }

    ID dir = opn_dir("/SYS");
    if (dir < 0) { out("ls: opn_dir failed", COLOR_RED, ud); return; }

    DIR_ENTRY entry;
    while (rd_dir(dir, &entry) == 0) {
        if (!entry.name[0]) continue;

        /* Read full FileHeader for extra info */
        FID fid = (FID)entry.robj_id;
        BLK hblk = vol_fid_get_blk(v, fid);
        unsigned char hbuf[BTRON_BLOCK_SIZE];
        if (vol_read_blk(v, hblk, hbuf) != 0) continue;

        /* Decode FileHeader fields (big-endian) */
        unsigned short flags = ((unsigned short)hbuf[0] << 8) | hbuf[1];
        unsigned short atype = ((unsigned short)hbuf[2] << 8) | hbuf[3];
        unsigned int ctime = ((unsigned int)hbuf[4]<<24)|((unsigned int)hbuf[5]<<16)|
                             ((unsigned int)hbuf[6]<<8)|(unsigned int)hbuf[7];
        unsigned int mtime = ((unsigned int)hbuf[8]<<24)|((unsigned int)hbuf[9]<<16)|
                             ((unsigned int)hbuf[10]<<8)|(unsigned int)hbuf[11];
        unsigned int atime = ((unsigned int)hbuf[12]<<24)|((unsigned int)hbuf[13]<<16)|
                             ((unsigned int)hbuf[14]<<8)|(unsigned int)hbuf[15];
        unsigned short nlnk = ((unsigned short)hbuf[20]<<8)|hbuf[21];
        unsigned int nrec  = ((unsigned int)hbuf[24]<<24)|((unsigned int)hbuf[25]<<16)|
                             ((unsigned int)hbuf[26]<<8)|(unsigned int)hbuf[27];
        unsigned int tsz   = ((unsigned int)hbuf[28]<<24)|((unsigned int)hbuf[29]<<16)|
                             ((unsigned int)hbuf[30]<<8)|(unsigned int)hbuf[31];

        char line[256];
        if (flag_l) {
            char mt[24]; fmt_ts(mtime, mt, sizeof(mt));
            /* ATR: P=delete-protect, O=write-protect, else '-' */
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
        } else {
            snprintf(line, sizeof(line), "%s", entry.name);
        }
        out(line, COLOR_LTGRAY, ud);
    }
    cls_dir(dir);
}

/* ── clu_fs_cmd ──────────────────────────────────────────────────── */
void clu_fs_cmd(const char *args, ShellOutputFn out, void *ud)
{
    int flag_l = has_flag(args, "-l");
    char target[80];
    get_target(args, target, sizeof(target));

    Volume *v = g_sys_vol;
    if (!v) { out("fs: no volume mounted", COLOR_RED, ud); return; }

    const char *path = target[0] ? target : g_cwd_path;
    ID fd = opn_fil(path, 0x0001);
    if (fd < 0 && (!target[0] || strcmp(target, "SYS") == 0 || strcmp(target, "/SYS") == 0)) {
        fd = opn_fil("SYS", 0x0001);
    }

    if (fd < 0) {
        char err[128];
        snprintf(err, sizeof(err), "fs: '%s': not found", path);
        out(err, COLOR_RED, ud);
        return;
    }

    OpenFile *of = &g_open_files[(int)fd];

    if (flag_l)
        out("NO: 0 STYPE : FID [ATR1 ATR2 ATR3 ATR4 ATR5] : NAME", COLOR_CYAN, ud);
    else
        out("NO: TYPE STYPE : SIZE / NAME", COLOR_CYAN, ud);

    if (of->nrec > 0) {
        for (unsigned int i = 0; i < of->nrec; i++) {
            RecordIndex *ri = &of->ridx[i];
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
                        cls_rec(rec);
                    }
                }
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  0 %04X  : %-3u [%04X %04X %04X %04X %04X] : %s",
                             i, (unsigned)ri->flags & 0xFFFF,
                             link_fid,
                             attrs[0], attrs[1], attrs[2], attrs[3], attrs[4],
                             link_name);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  0    %04X  : %s",
                             i, (unsigned)ri->flags & 0xFFFF, link_name);
                }
            } else {
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  %-4u %04X  :     (data record)",
                             i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  %-4u %04X  : %-12u",
                             i, (unsigned)ri->type, (unsigned)ri->flags & 0xFFFF,
                             ri->size);
                }
            }
            out(line, COLOR_LTGRAY, ud);
        }
    } else if (of->fid == FID_ROOT) {
        /* Fallback: directory enumeration for root container if 0 records */
        ID dir = opn_dir("/SYS");
        if (dir >= 0) {
            DIR_ENTRY entry;
            unsigned int idx = 0;
            while (rd_dir(dir, &entry) == 0) {
                if (!entry.name[0]) continue;
                FID efid = (FID)entry.robj_id;
                if (efid == FID_ROOT) continue;
                char line[256];
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  0 0000  : %-3u [0000 0000 0000 0000 0000] : %s",
                             idx++, (unsigned)efid, entry.name);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  0    %04X  : %s",
                             idx++, 0, entry.name);
                }
                out(line, COLOR_LTGRAY, ud);
            }
            cls_dir(dir);
            if (idx == 0) out("(0 records)", COLOR_LTGRAY, ud);
        }
    } else {
        out("(0 records)", COLOR_LTGRAY, ud);
    }
    cls_fil(fd);
}

/* ── clu_tp ──────────────────────────────────────────────────────── */
void clu_tp(const char *args, ShellOutputFn out, void *ud)
{
    int flag_x = has_flag(args, "-x");
    int flag_a = has_flag(args, "-a");
    char target[80];
    get_target(args, target, sizeof(target));

    if (!target[0]) { out("tp: missing file argument", COLOR_RED, ud); return; }

    ID fd = opn_fil(target, 0x0001);
    if (fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "tp: '%s': not found", target);
        out(err, COLOR_RED, ud); return;
    }

    OpenFile *of = &g_open_files[(int)fd];
    /* Find first RT_TADDATA or any data record */
    for (unsigned int i = 0; i < of->nrec; i++) {
        if (of->ridx[i].type == RT_LINK) continue;
        if (of->ridx[i].size == 0) continue;

        ID rec = opn_rec(fd, (W)i, 0x0001);
        if (rec < 0) continue;

        unsigned char payload[1024 * 4];
        W got = 0;
        UW to_read = of->ridx[i].size;
        if (to_read > sizeof(payload)) to_read = sizeof(payload);
        rd_rec(rec, payload, (W)to_read, &got);
        cls_rec(rec);

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
        break; /* show only first data record by default */
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

/* ── clu_df ──────────────────────────────────────────────────────── */
void clu_df(const char *args, ShellOutputFn out, void *ud)
{
    (void)args;
    Volume *v = g_sys_vol;
    if (!v) { out("df: no volume mounted", COLOR_RED, ud); return; }

    out("PATH  DEV   TOTAL   FREE    USED  UNIT  MAXFILE  NAME", COLOR_CYAN, ud);

    UW total = vol_total_blocks(v);
    UW free_blks = vol_free_blocks(v);
    UW used  = total - free_blks;
    UW pct   = (total > 0) ? (used * 100 / total) : 0;
    char line[128];
    snprintf(line, sizeof(line),
             "%-5s %-5s %-7uK %-7uK %2u%%  %-5u %-8u %s",
             "/SYS", "mem0",
             total,   /* already in blocks = KiB since BLOCK_SIZE=1024 */
             free_blks,
             pct,
             BTRON_BLOCK_SIZE,
             vol_nfmax(v),
             vol_name(v));
    out(line, COLOR_LTGRAY, ud);
}

/* ── clu_sync_cmd ────────────────────────────────────────────────── */
void clu_sync_cmd(const char *args, ShellOutputFn out, void *ud)
{
    (void)args;
    if (!g_sys_vol) { out("sync: no volume mounted", COLOR_YELLOW, ud); return; }
    vol_sync(g_sys_vol);
    out("(all caches flushed)", COLOR_GREEN, ud);
}
