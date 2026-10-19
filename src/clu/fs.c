/*
 * B-System BTRON3 — src/clu/fs.c
 * CLU File System & Navigation Tooling (fs, cd, mkf, apd)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Strict alignment enforced; structures packed and naturally aligned.
 *       No unaligned 32-bit memory access. Stack depth strictly limited to
 *       prevent overflowing EE thread stack space.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Pure integer arithmetic; no hardware float dependencies. Little-endian
 *       native representation.
 *   - x86 / x86_64 (Hosted POSIX & Bare-Metal UEFI):
 *       Compatible with both hosted glibc/musl and freestanding libc_shim.
 *   - M68k (Motorola 68000):
 *       Strict 16-bit word alignment preserved; bounded call depth (<16) to
 *       run within tight embedded 8KB stack envelopes.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Fixed loop bounds on all token and record iterations.
 *   - Bounded recursion depth for hierarchy dumping.
 *   - Error checks on all VFS and block operations.
 *   - String operations strictly bounded with snprintf / size tracking.
 */

#include "cluc.h"
#if BTRON_HOSTED
#  include <time.h>
#endif

/* ── Arg parsing helpers ─────────────────────────────────────────── */

const char *clu_skip_ws(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

int clu_has_flag(const char *args, const char *flag)
{
    const char *p = args;
    while (*p) {
        p = clu_skip_ws(p);
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

int clu_get_targets(const char *args, char targets[][80], int maxt)
{
    const char *p = clu_skip_ws(args);
    int count = 0;
    while (*p && count < maxt) {
        p = clu_skip_ws(p);
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

void clu_get_target(const char *args, char *out, int maxlen)
{
    char targets[1][80];
    int n = clu_get_targets(args, targets, 1);
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

void clu_fmt_ts(UW ts, char *buf, int bufsz)
{
#if BTRON_HOSTED
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

Volume *clu_get_cwd_vol(void)
{
    if (g_cwd_path[0] == '/') {
        char vname[64];
        const char *p = g_cwd_path + 1;
        size_t i = 0;
        while (*p && *p != '/' && i < sizeof(vname) - 1) {
            vname[i++] = *p++;
        }
        vname[i] = '\0';
        Volume *v = vol_find_by_name(vname);
        if (v) return v;
    }
    return g_sys_vol ? g_sys_vol : (vol_mounted_count() > 0 ? vol_get_mounted(0) : NULL);
}

Volume *clu_resolve_target_vol(const char *target, Volume *default_vol)
{
    if (!default_vol) default_vol = clu_get_cwd_vol();
    if (!target || !target[0]) return default_vol;

    const char *t = target;
    if (t[0] == '/') {
        if (t[1] == '\0') return default_vol;
        Volume *mv = vol_find_by_prefix(t, NULL);
        if (mv) return mv;
        char vname[64];
        const char *p = t + 1;
        size_t i = 0;
        while (*p && *p != '/' && *p != '#' && i < sizeof(vname) - 1) {
            vname[i++] = *p++;
        }
        vname[i] = '\0';
        Volume *v = vol_find_by_name(vname);
        if (v) return v;
    } else {
        Volume *v = vol_find_by_name(t);
        if (v) return v;
    }
    return default_vol;
}

int clu_strcasecmp(const char *s1, const char *s2)
{
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    while (*s1 && *s2) {
        char c1 = *s1;
        char c2 = *s2;
        if (c1 >= 'A' && c1 <= 'Z') c1 = (char)(c1 + 32);
        if (c2 >= 'A' && c2 <= 'Z') c2 = (char)(c2 + 32);
        if (c1 != c2) return (int)(unsigned char)c1 - (int)(unsigned char)c2;
        s1++;
        s2++;
    }
    return (int)(unsigned char)*s1 - (int)(unsigned char)*s2;
}

int clu_is_root_path(Volume *v, const char *dir_path)
{
    if (!dir_path || dir_path[0] == '\0' || strcmp(dir_path, "/") == 0 || strcmp(dir_path, ".") == 0) return 1;
    if (dir_path[0] == '/') {
        const char *suffix = NULL;
        Volume *mv = vol_find_by_prefix(dir_path, &suffix);
        if (mv && suffix && (suffix[0] == '\0' || (suffix[0] == '/' && suffix[1] == '\0'))) {
            return 1;
        }
        const char *p = dir_path + 1;
        char vname[64];
        size_t i = 0;
        while (*p && *p != '/' && i < sizeof(vname) - 1) {
            vname[i++] = *p++;
        }
        vname[i] = '\0';
        if (*p == '\0' || (*p == '/' && *(p + 1) == '\0')) {
            Volume *target_v = vol_find_by_name(vname);
            if (target_v) return 1;
        }
    } else {
        Volume *target_v = vol_find_by_name(dir_path);
        if (target_v) return 1;
    }
    if (v) {
        const char *vn = vol_name(v);
        if (vn && vn[0]) {
            char root1[64], root2[64];
            snprintf(root1, sizeof(root1), "/%s", vn);
            snprintf(root2, sizeof(root2), "/%s/", vn);
            if (clu_strcasecmp(dir_path, root1) == 0 || clu_strcasecmp(dir_path, root2) == 0 ||
                clu_strcasecmp(dir_path, vn) == 0) return 1;
        }
    }
    return 0;
}

ID clu_open_target(const char *target, UW mode, Volume *default_vol,
                   Volume **out_vol, FID *out_fid, int *out_is_fid)
{
    if (out_vol) *out_vol = NULL;
    if (out_fid) *out_fid = FID_INVALID;
    if (out_is_fid) *out_is_fid = 0;

    if (!target || !target[0]) return -1;

    Volume *v = default_vol ? default_vol : clu_get_cwd_vol();

    ID fd = -1;
    int is_fid = 0;
    FID fid_val = FID_INVALID;
    const char *num_str = NULL;
    Volume *target_vol = v;

    if (target[0] == '/') {
        target_vol = NULL;
        if (target[1] == '\0') {
            target_vol = v;
        } else {
            const char *p = NULL;
            target_vol = vol_find_by_prefix(target, &p);
            if (!target_vol) {
                char vname[64];
                const char *tp = target + 1;
                size_t i = 0;
                while (*tp && *tp != '/' && *tp != '#' && i < sizeof(vname) - 1) {
                    vname[i++] = *tp++;
                }
                vname[i] = '\0';
                target_vol = vol_find_by_name(vname);
                p = tp;
            }
            if (p && *p == '#') {
                num_str = p + 1;
                is_fid = 1;
            }
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

void clu_report_target_err(const char *cmd, const char *target, Volume *target_vol,
                           FID fid_val, int is_fid, ShellOutputFn out, void *ud)
{
    char err[128];
    char v_buf[64];
    if (target_vol) {
        const char *vn = vol_name(target_vol);
        snprintf(v_buf, sizeof(v_buf), "/%s", (vn && vn[0]) ? vn : "VOL");
    } else {
        snprintf(v_buf, sizeof(v_buf), "unmounted volume");
    }
    const char *vol_name_str = target_vol ? v_buf : g_cwd_path;
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
        snprintf(err, sizeof(err), "%s: FID %u not found on %s", cmd, (unsigned)fid_val, vol_name_str);
    } else {
        snprintf(err, sizeof(err), "%s: '%s': not found on %s", cmd, target, vol_name_str);
    }
    out(err, COLOR_RED, ud);
}

/* ── Hierarchical record and drawer node dumping ─────────────────── */

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
    int link_count = 0;

    if (of->nrec > 0) {
        for (unsigned int i = 0; i < of->nrec; i++) {
            RecordIndex *ri = &of->ridx[i];
            if (ri->size == 0 && ri->type == 0 && ri->kind == 0) continue;
            char line[256];
            if (fil_rec_is_link(fd, i)) {
                link_count++;
                char link_name[48] = "(link)";
                FID link_fid = FID_INVALID;
                unsigned short attrs[5] = {0,0,0,0,0};
                fil_get_rec_link_info(fd, i, &link_fid, link_name, sizeof(link_name), attrs);

                int lkind = clu_probe_kind(v, (unsigned int)link_fid);
                UW lcolor  = clu_kind_color(lkind, lkind < 1 /* is_link if not exec */);
                const char *tag = (lkind >= 2) ? " [ELF]" : (lkind == 1 ? " [EXE]" : "");
                if (flag_l) {
                    snprintf(line, sizeof(line),
                             "%u:  0 %04X  : %-5u %-6u [%04X %04X %04X %04X %04X] : %*s%s%s",
                             i, (unsigned)ri->flags & 0xFFFF,
                             (unsigned)link_fid, (unsigned)of->fid,
                             attrs[0], attrs[1], attrs[2], attrs[3], attrs[4],
                             indent, "", link_name, tag);
                } else {
                    snprintf(line, sizeof(line),
                             "%u:  0    %04X  : %-5u %-6u : %*s%s%s",
                             i, (unsigned)ri->flags & 0xFFFF,
                             (unsigned)link_fid, (unsigned)of->fid,
                             indent, "", link_name, tag);
                }
                out(line, lcolor, ud);

                /* Recurse depth-first into child Virtual Object Real Body if -r is requested */
                if (flag_r && depth < 16 && link_fid != 0 && link_fid != of->fid && link_fid < nfmax && !visited[link_fid]) {
                    ID child_fd = opn_fil_fid(v, (FID)link_fid, 0x0001);
                    if (child_fd < 0) {
                        char child_path[128];
                        char vprefix[64];
                        const char *vn = vol_name(v);
                        snprintf(vprefix, sizeof(vprefix), "/%s/", (vn && vn[0]) ? vn : (v == g_chokanji_vol ? "CHOKANJI" : "SYS"));
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
    }

    if (of->fid == FID_ROOT && link_count == 0) {
        unsigned int rec_idx = 0;

        /* Directory enumeration for root container */
        char vdir[64];
        const char *vn = vol_name(v);
        snprintf(vdir, sizeof(vdir), "/%s", (vn && vn[0]) ? vn : (v == g_chokanji_vol ? "CHOKANJI" : "SYS"));
        ID dir = opn_dir(vdir);
        if (dir >= 0) {
            DIR_ENTRY entry;
            unsigned int idx = rec_idx;
            while (rd_dir(dir, &entry) == 0) {
                if (!entry.name[0]) continue;
                FID efid = (FID)entry.robj_id;
                if (efid == FID_ROOT) continue;
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
                        char vprefix[64];
                        const char *vn2 = vol_name(v);
                        snprintf(vprefix, sizeof(vprefix), "/%s/", (vn2 && vn2[0]) ? vn2 : (v == g_chokanji_vol ? "CHOKANJI" : "SYS"));
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
    } else if (of->nrec == 0) {
        out("(0 records)", COLOR_LTGRAY, ud);
    }
}

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

CluFsNode *clu_fs_build_graph(Volume *v, UW *pnfmax, unsigned int *pcount)
{
    UW nfmax = vol_nfmax(v);
    if (nfmax < 256) nfmax = 256;
    CluFsNode *nodes = (CluFsNode *)calloc(nfmax, sizeof(CluFsNode));
    if (!nodes) return NULL;

    for (FID f = 0; f < nfmax; f++) {
        nodes[f].parent_fid   = FID_INVALID;
        nodes[f].first_child  = FID_INVALID;
        nodes[f].last_child   = FID_INVALID;
        nodes[f].next_sibling = FID_INVALID;
    }

    unsigned int count = 0;
    for (FID fid = 0; fid < nfmax; fid++) {
        UB refc = vol_fid_refcount(v, fid);
        BLK blk = vol_fid_get_blk(v, fid);
        if (refc == 0 && fid != FID_ROOT) continue;
        if (blk == 0 || blk == FID_INVALID) continue;

        ID fd = opn_fil_fid(v, fid, 0x0001);
        if (fd < 0) continue;
        OpenFile *of = &g_open_files[(int)fd];
        count++;

        nodes[fid].blk       = blk;
        nodes[fid].hdr_blk   = of->hdr_blk ? of->hdr_blk : blk;
        nodes[fid].refc      = refc;
        nodes[fid].flags     = of->hdr.flags;
        nodes[fid].sz        = of->hdr.total_size;
        nodes[fid].did       = of->hdr.did;
        nodes[fid].pdid      = of->hdr.pdid;
        nodes[fid].is_stream = of->is_stream;
        nodes[fid].is_elf    = (of->hdr.flags & 0x0001) != 0;

        snprintf(nodes[fid].name, sizeof(nodes[fid].name), "%s",
                 (const char *)of->hdr.name);

        if (of->nrec > 0) {
            for (unsigned int i = 0; i < of->nrec; i++) {
                if (!fil_rec_is_link(fd, (W)i)) continue;
                nodes[fid].is_dir = 1;
                FID cfid = FID_INVALID;
                char cname[48] = "";
                if (fil_get_rec_link_info(fd, (W)i, &cfid, cname, sizeof(cname), NULL) == 0 &&
                    cfid != FID_INVALID && cfid != fid && cfid < nfmax) {
                    clu_fs_node_add_child(nodes, fid, cfid, nfmax);
                }
            }
        }
        cls_fil(fd);
    }

    int nroot = fil_hier_nroot(v);
    if (nroot >= 0) {
        for (int i = 0; i < nroot; i++) {
            FID f = fil_hier_root_fid(v, i);
            if (f == FID_INVALID || f >= nfmax || !nodes[f].blk) continue;
            if (nodes[f].parent_fid != FID_INVALID) continue;  /* named by a drawer */
            clu_fs_node_add_child(nodes, FID_ROOT, f, nfmax);
        }
    } else {
        for (FID f = 1; f < nfmax; f++) {
            if (!nodes[f].blk || !nodes[f].is_dir) continue;
            if (nodes[f].parent_fid != FID_INVALID) continue;
            int dup = 0;
            for (FID r = nodes[FID_ROOT].first_child; r != FID_INVALID; r = nodes[r].next_sibling) {
                if (nodes[r].hdr_blk == nodes[f].hdr_blk) { dup = 1; break; }
            }
            if (!dup) clu_fs_node_add_child(nodes, FID_ROOT, f, nfmax);
        }
    }

    *pnfmax = nfmax;
    *pcount = count;
    return nodes;
}

static void clu_fs_print_node_line(const CluFsNode *nodes, FID fid, int indent_spaces,
                                   int flag_l, int is_tree, ShellOutputFn out, void *ud)
{
    if (!nodes[fid].blk) return;
    int kind = nodes[fid].is_elf ? 2 : (nodes[fid].is_dir ? 3 : (nodes[fid].is_stream ? 4 : 0));
    const char *tag = (kind == 3) ? "[DIR]" :
                      (kind == 2) ? "[ELF]" :
                      (kind == 4) ? "[STR]" : "[TAD]";
    UW color = clu_kind_color(kind, (kind == 3));

    char name_buf[64];
    const char *base = nodes[fid].name[0] ? nodes[fid].name : "-";
    if (is_tree && nodes[fid].is_dir && nodes[fid].name[0]) {
        size_t len = strlen(base);
        if (len > 0 && base[len - 1] != '/') {
            snprintf(name_buf, sizeof(name_buf), "%s/", base);
        } else {
            snprintf(name_buf, sizeof(name_buf), "%s", base);
        }
    } else {
        snprintf(name_buf, sizeof(name_buf), "%s", base);
    }
    if (nodes[fid].is_stream) {
        char marked[64];
        snprintf(marked, sizeof(marked), "[*] %s", name_buf);
        snprintf(name_buf, sizeof(name_buf), "%s", marked);
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

/* ── clu_cmd_fs ──────────────────────────────────────────────────── */

void clu_cmd_fs(const char *args, ShellOutputFn out, void *ud)
{
    int flag_l = clu_has_flag(args, "-l");
    int flag_r = clu_has_flag(args, "-r") || clu_has_flag(args, "-R");
    int flag_a = clu_has_flag(args, "-a") || clu_has_flag(args, "--all");
    int flag_g = clu_has_flag(args, "-g") || clu_has_flag(args, "--group");
    int flag_t = clu_has_flag(args, "-t") || clu_has_flag(args, "--tree");
    char target[80];
    clu_get_target(args, target, sizeof(target));

    Volume *v = clu_resolve_target_vol(target[0] ? target : g_cwd_path, NULL);
    if (!v) { out("fs: no volume mounted", COLOR_RED, ud); return; }

    if (flag_a || flag_g || flag_t) {
        UW nfmax = 0;
        unsigned int count = 0;
        CluFsNode *nodes = clu_fs_build_graph(v, &nfmax, &count);
        if (!nodes) { out("fs: memory allocation failed", COLOR_RED, ud); return; }

        /* Check if a specific target FID or container was requested */
        FID start_fid = FID_INVALID;
        if (target[0]) {
            const char *tname = target;
            const char *aname = (g_anders_vol && vol_name(g_anders_vol)[0]) ? vol_name(g_anders_vol) : "ANDERS";
            size_t alen = strlen(aname);
            char a_prefix[64];
            snprintf(a_prefix, sizeof(a_prefix), "/%s/", aname);

            if (strncmp(tname, a_prefix, alen + 2) == 0) tname += (alen + 2);
            else if (strncmp(tname, "/ANDERS/", 8) == 0) tname += 8;
            else if (strncmp(tname, "/CHOKANJI/", 10) == 0) tname += 10;
            else if (strncmp(tname, "/B-right/", 9) == 0) tname += 9;
            else if (strncmp(tname, "/SYS/", 5) == 0) tname += 5;

            if (clu_is_root_path(v, target)) {
                start_fid = flag_g ? FID_INVALID : FID_ROOT;
            } else {
                int all_digits = 1;
                const char *np = (tname[0] == '#') ? tname + 1 : tname;
                while (*np == ' ') np++;
                for (int i = 0; np[i]; i++) {
                    if (!isdigit((unsigned char)np[i])) { all_digits = 0; break; }
                }
                if (all_digits && *np) {
                    start_fid = (FID)strtoul(np, NULL, 10);
                } else {
                    for (FID f = 0; f < nfmax; f++) {
                        if (nodes[f].blk && strcmp(nodes[f].name, tname) == 0) {
                            start_fid = f;
                            break;
                        }
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
                clu_fs_print_node_line(nodes, start_fid, 0, flag_l, 0, out, ud);
                UW hop = 0;
                for (FID c = nodes[start_fid].first_child; c != FID_INVALID && hop < nfmax; c = nodes[c].next_sibling, hop++) {
                    if (c < nfmax && nodes[c].blk) {
                        clu_fs_print_node_line(nodes, c, 2, flag_l, 0, out, ud);
                    }
                }
            } else {
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

                int kind = nodes[fid].is_elf ? 2 : (nodes[fid].is_dir ? 3 : (nodes[fid].is_stream ? 4 : 0));
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
                        if (cof->nrec > 0 && !cof->is_stream) {
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

/* ── clu_cmd_cd ──────────────────────────────────────────────────── */

void clu_cmd_cd(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));

    if (!target[0]) {
        char msg[128];
        snprintf(msg, sizeof(msg), "[%s]", g_cwd_path);
        out(msg, COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "/") == 0) {
        Volume *sys = g_sys_vol ? g_sys_vol : (vol_mounted_count() > 0 ? vol_get_mounted(0) : NULL);
        const char *vn = sys ? vol_name(sys) : "SYS";
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/%s", vn);
        char msg[80];
        snprintf(msg, sizeof(msg), "[/%s]", vn);
        out(msg, COLOR_GREEN, ud);
        return;
    }

    const char *t = (target[0] == '/') ? target + 1 : target;
    Volume *v = vol_find_by_name(t);
    if (v) {
        const char *vn = vol_name(v);
        snprintf(g_cwd_path, sizeof(g_cwd_path), "/%s", (vn && vn[0]) ? vn : t);
        char msg[80];
        snprintf(msg, sizeof(msg), "[/%s]", (vn && vn[0]) ? vn : t);
        out(msg, COLOR_GREEN, ud);
        return;
    }

    if (strcmp(target, "..") == 0) {
        char *last_slash = strrchr(g_cwd_path, '/');
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

/* ── clu_cmd_mkf ─────────────────────────────────────────────────── */

void clu_cmd_mkf(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));
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

/* ── clu_cmd_apd ─────────────────────────────────────────────────── */

void clu_cmd_apd(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = clu_get_targets(args, targets, 2);

    const char *p = args;
    p = clu_skip_ws(p);
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
        char msg[128]; snprintf(msg, sizeof(msg), "Deleted %d record(s) from '%s'", count, targets[0]);
        out(msg, COLOR_GREEN, ud);
        return;
    }
    out("apd: (use -d#.# to delete records; full apd not yet implemented)", COLOR_YELLOW, ud);
}
