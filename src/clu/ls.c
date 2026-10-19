/*
 * B-System BTRON3 — src/clu/ls.c
 * CLU Directory & Real Body Listing (ls)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Naturally aligned structures; no misaligned word reads when probing
 *       magic headers or formatting output buffers.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Standard 32/64-bit integer operations; color coding uses native 32-bit
 *       UW RGBA/ARGB representations compatible with all display controllers.
 *   - x86 / x86_64: Hosted POSIX & freestanding environment.
 *   - M68k (Motorola 68000):
 *       Big-endian byte order correctly handled in magic checking (ELF magic
 *       is byte-sequenced \x7fELF).
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Bounded loop iterations over directory and drawer entries.
 *   - Explicit buffer size constraints for formatting.
 *   - Zero runtime dynamic heap allocation outside controlled graph reconstruction.
 */

#include "cluc.h"

/* ── File kind probing (for ls/fs coloring) ─────────────────────── */

int clu_probe_kind(Volume *v, unsigned int fid)
{
    if (!v || fid == FID_INVALID) return 0;
    ID fd = opn_fil_fid(v, (FID)fid, 0x0001);
    if (fd < 0) return 0;
    OpenFile *of = &g_open_files[(int)fd];
    int kind = 0;
    if (of->hdr.flags & 0x0001) {
        kind = 2; /* Executable / ELF */
    } else if (of->hdr.flags & OBJ_EXEC) {
        kind = 1; /* BTRON Executable */
    } else {
        ID rec = opn_rec(fd, 0, 0x0001);
        if (rec >= 0) {
            unsigned char m[4];
            W got = 0;
            rd_rec(rec, m, 4, &got);
            cls_rec(rec);
            if (got >= 4 && memcmp(m, "\x7f\x45\x4c\x46", 4) == 0) {
                kind = 2;
            }
        }
    }
    cls_fil(fd);
    return kind;
}

UW clu_kind_color(int kind, int is_link)
{
    if (kind >= 1) return COLOR_GREEN;  /* ELF or OBJ_EXEC executable */
    if (is_link)   return COLOR_CYAN;   /* RT_LINK virtual body        */
    return COLOR_LTGRAY;
}

/* ── Listing Output Helpers ──────────────────────────────────────── */

static void clu_ls_emit_entry(Volume *v, FID fid, const char *name,
                              int flag_l, int flag_t, ShellOutputFn out, void *ud)
{
    if (!name || !name[0]) return;
    {
        const char *np = name;
        while (*np == ' ') np++;
        if (!*np) return;
    }

    if (!flag_l && !flag_t) {
        int ek = clu_probe_kind(v, (unsigned int)fid);
        out(name, clu_kind_color(ek, 0), ud);
        return;
    }

    ID lfd = opn_fil_fid(v, fid, 0x0001);
    if (lfd < 0) return;
    OpenFile *lof = &g_open_files[(int)lfd];

    unsigned short flags = lof->hdr.flags;
    unsigned short atype = lof->hdr.atype;
    unsigned int ctime = lof->hdr.ctime;
    unsigned int mtime = lof->hdr.mtime;
    unsigned int atime = lof->hdr.atime;
    unsigned int nrec  = lof->nrec;
    unsigned int tsz   = lof->hdr.total_size;
    int is_exec = (flags & (0x0001 | OBJ_EXEC)) ? 1 : 0;
    cls_fil(lfd);

    UW entry_color = is_exec ? COLOR_GREEN : COLOR_LTGRAY;
    char line[256];
    if (flag_l && flag_t) {
        char ct[24], mt[24];
        clu_fmt_ts(ctime, ct, sizeof(ct));
        clu_fmt_ts(mtime, mt, sizeof(mt));
        char atr[4] = "---";
        if (flags & 0x0020) atr[0] = 'P';
        if (flags & 0x0010) atr[1] = 'O';
        snprintf(line, sizeof(line),
                 "%04X  %s %-4u 1    %-5u %-18s %-18s %s",
                 atype, atr, nrec, tsz, ct, mt, name);
        out(line, entry_color, ud);
    } else if (flag_l) {
        char mt[24]; clu_fmt_ts(mtime, mt, sizeof(mt));
        char atr[4] = "---";
        if (flags & 0x0020) atr[0] = 'P';
        if (flags & 0x0010) atr[1] = 'O';
        snprintf(line, sizeof(line),
                 "%04X  %s %-4u 1    %-5u %s %s",
                 atype, atr, nrec, tsz, mt, name);
        out(line, entry_color, ud);
    } else {
        char ct[24], at[24], mt[24];
        clu_fmt_ts(ctime, ct, sizeof(ct));
        clu_fmt_ts(atime, at, sizeof(at));
        clu_fmt_ts(mtime, mt, sizeof(mt));
        snprintf(line, sizeof(line), "%-18s %-18s %-18s %s",
                 ct, at, mt, name);
        out(line, entry_color, ud);
    }
}

/* ── clu_cmd_ls ──────────────────────────────────────────────────── */

void clu_cmd_ls(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));
    const char *dir_path = target[0] ? target : g_cwd_path;

    Volume *v = clu_resolve_target_vol(dir_path, NULL);
    if (!v) { out("ls: no volume mounted", COLOR_RED, ud); return; }

    int flag_l = clu_has_flag(args, "-l");
    int flag_t = clu_has_flag(args, "-t");

    if (flag_l && flag_t) {
        out("ATYPE ATR NREC NREF SIZE  CTIME              MTIME              NAME", COLOR_CYAN, ud);
    } else if (flag_l) {
        out("ATYPE ATR NREC NREF SIZE  MTIME            NAME", COLOR_CYAN, ud);
    } else if (flag_t) {
        out("CTIME              ATIME              MTIME              NAME", COLOR_CYAN, ud);
    }

    ID fd = opn_fil(dir_path, 0x0001);
    if (fd >= 0) {
        OpenFile *of = &g_open_files[(int)fd];
        Volume *ofv = of_vol(of);
        int is_root = (of->fid == FID_ROOT);
        int link_count = 0;
        for (unsigned int i = 0; i < of->nrec; i++) {
            if (!fil_rec_is_link(fd, (W)i)) continue;
            FID link_fid = FID_INVALID;
            char link_name[64] = "";
            if (fil_get_rec_link_info(fd, (W)i, &link_fid, link_name, sizeof(link_name), NULL) != 0)
                continue;

            link_count++;
            int lk = clu_probe_kind(ofv, (unsigned int)link_fid);
            UW lk_color = clu_kind_color(lk, 1 /* is_link */);

            if (flag_l || flag_t) {
                ID lfd = (link_fid != FID_INVALID) ? opn_fil_fid(ofv, link_fid, 0x0001) : -1;
                unsigned short atype = 0;
                unsigned int ctime = 0, atime = 0, mtime = 0, tsz = 0;
                if (lfd >= 0) {
                    OpenFile *lof = &g_open_files[(int)lfd];
                    atype = lof->hdr.atype;
                    ctime = lof->hdr.ctime;
                    mtime = lof->hdr.mtime;
                    atime = lof->hdr.atime;
                    tsz   = lof->hdr.total_size;
                    cls_fil(lfd);
                }
                if (flag_l && flag_t) {
                    char ct[24], mt[24];
                    clu_fmt_ts(ctime, ct, sizeof(ct));
                    clu_fmt_ts(mtime, mt, sizeof(mt));
                    char line[256];
                    snprintf(line, sizeof(line), "%04X  ---  1    1    %-5u %-18s %-18s %s",
                             atype, tsz, ct, mt, link_name);
                    out(line, lk_color, ud);
                } else if (flag_l) {
                    char mt[24]; clu_fmt_ts(mtime, mt, sizeof(mt));
                    char line[256];
                    snprintf(line, sizeof(line), "%04X  ---  1    1    %-5u %s %s",
                             atype, tsz, mt, link_name);
                    out(line, lk_color, ud);
                } else {
                    char ct[24], at[24], mt[24];
                    clu_fmt_ts(ctime, ct, sizeof(ct));
                    clu_fmt_ts(atime, at, sizeof(at));
                    clu_fmt_ts(mtime, mt, sizeof(mt));
                    char line[256];
                    snprintf(line, sizeof(line), "%-18s %-18s %-18s %s",
                             ct, at, mt, link_name);
                    out(line, lk_color, ud);
                }
            } else {
                out(link_name, lk_color, ud);
            }
        }
        cls_fil(fd);
        if (!is_root || link_count > 0) return;
    } else {
        int is_root_path = clu_is_root_path(v, dir_path);
        if (!is_root_path) {
            char err[128];
            snprintf(err, sizeof(err), "ls: '%s': no such directory", dir_path);
            out(err, COLOR_RED, ud);
            return;
        }
    }

    if (vol_fs_type(v) == FS_TYPE_BRIGHTV) {
        UW nfmax = 0;
        unsigned int nlive = 0;
        CluFsNode *nodes = clu_fs_build_graph(v, &nfmax, &nlive);
        if (!nodes) { out("ls: out of memory", COLOR_RED, ud); return; }
        int n = 0;
        for (FID c = nodes[FID_ROOT].first_child; c != FID_INVALID; c = nodes[c].next_sibling) {
            clu_ls_emit_entry(v, c, nodes[c].name, flag_l, flag_t, out, ud);
            n++;
        }
        free(nodes);
        if (n == 0) out("(no drawer at the root of this volume)", COLOR_LTGRAY, ud);
        return;
    }

    ID dir = opn_dir(dir_path);
    if (dir < 0) { out("ls: opn_dir failed", COLOR_RED, ud); return; }

    DIR_ENTRY entry;
    while (rd_dir(dir, &entry) == 0) {
        if (!entry.name[0]) continue;
        clu_ls_emit_entry(v, (FID)entry.robj_id, entry.name, flag_l, flag_t, out, ud);
    }
    cls_dir(dir);
}
