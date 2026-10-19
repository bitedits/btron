/*
 * B-System BTRON3 — src/clu/st.c
 * CLU File Stat, Attributes & Timestamp Utilities (stat, info, finfo, chmod, touch, chtime)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Naturally aligned structure accesses when inspecting OpenFile and
 *       RecordIndex metadata. BTRON T-Code decoding uses byte-swapped 16-bit
 *       shorts with strict alignment protection.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Pure integer arithmetic; timestamp calculation wraps cleanly around
 *       BTRON epoch (Jan 1, 2000).
 *   - x86 / x86_64: Identical output representation in hosted & bare-metal modes.
 *   - M68k (Motorola 68000):
 *       No unaligned 16-bit or 32-bit accesses; bitwise attribute flags
 *       guaranteed portable across endiannesses.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Bounded loop iterations over records (up to of->nrec).
 *   - Fixed-size local buffers on stack; bounded snprintf operations.
 *   - Zero dynamic runtime heap allocation.
 */

#include "cluc.h"
#if BTRON_HOSTED
#  include <time.h>
#endif

/* ── clu_stat_internal ───────────────────────────────────────────── */

static void clu_stat_internal(const char *cmd_name, const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));

    if (!target[0]) {
        char err[128];
        snprintf(err, sizeof(err), "%s: missing file argument (usage: %s <file|FID>)", cmd_name, cmd_name);
        out(err, COLOR_RED, ud);
        return;
    }

    Volume *v = clu_get_cwd_vol();

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
    const char *vname = vol_name(v);
    const char *vol_name_str = (vname && vname[0]) ? vname :
                               ((v == g_chokanji_vol) ? "CHOKANJI" : (v == g_anders_vol ? "ANDERS" : "SYS"));
    const char *vol_desc = vol_description(v);

    snprintf(line, sizeof(line), "  File: %s", of->hdr.name[0] ? (const char *)of->hdr.name : "(unnamed)");
    out(line, COLOR_WHITE, ud);

    snprintf(line, sizeof(line), "   FID: %-5u (0x%04X)      Volume: /%s [%s]",
             (unsigned)of->fid, (unsigned)of->fid, vol_name_str, vol_desc);
    out(line, COLOR_CYAN, ud);

    snprintf(line, sizeof(line), "Blocks: Header=%u  Data=%u",
             (unsigned)of->hdr_blk, (unsigned)of->data_blk);
    out(line, COLOR_LTGRAY, ud);

    snprintf(line, sizeof(line), "  Size: %-10u bytes     Used: %-10u   Records: %u",
             (unsigned)of->hdr.total_size, (unsigned)of->data_used, (unsigned)of->nrec);
    out(line, COLOR_LTGRAY, ud);

    int kind = clu_probe_kind(v, (unsigned int)of->fid);
    const char *kind_str = (kind == 2) ? "ELF 32-bit Executable (i386)" :
                           (kind == 1 || (of->hdr.flags & 0x8000) || of->ridx[0].kind == 0x9F00) ? "Executable Binary (OBJ_EXEC / Program)" :
                           (of->nrec > 0 && of->ridx[0].kind == 0x8000) ? "Directory Drawer / Container" :
                           (of->nrec > 0 && of->ridx[0].type == 1) ? "TAD Document / Text" : "Data Stream";
    snprintf(line, sizeof(line), "Format: %s", kind_str);
    out(line, clu_kind_color(kind, 0), ud);

    char flags_desc[64];
    int fpos = 0;
    if (of->hdr.flags & 0x8000) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "EXEC ");
    if (of->hdr.flags & 0x0001) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "WPROTECT ");
    if (of->hdr.flags & 0x0002) fpos += snprintf(flags_desc + fpos, sizeof(flags_desc) - fpos, "DPROTECT ");
    if (fpos == 0) snprintf(flags_desc, sizeof(flags_desc), "NORMAL");

    snprintf(line, sizeof(line), "Access: ATYPE=%04X  Flags=%04X (%s)  Nlnk=%u",
             (unsigned)of->hdr.atype, (unsigned)of->hdr.flags, flags_desc, (unsigned)of->hdr.nlnk);
    out(line, COLOR_LTGRAY, ud);

    char mt[24], ct[24], at[24];
    clu_fmt_ts(of->hdr.mtime, mt, sizeof(mt));
    clu_fmt_ts(of->hdr.ctime, ct, sizeof(ct));
    clu_fmt_ts(of->hdr.atime, at, sizeof(at));
    snprintf(line, sizeof(line), "Modify: %s (0x%08X)", mt, (unsigned)of->hdr.mtime);
    out(line, COLOR_LTGRAY, ud);
    snprintf(line, sizeof(line), "Access: %s (0x%08X)", at, (unsigned)of->hdr.atime);
    out(line, COLOR_LTGRAY, ud);
    snprintf(line, sizeof(line), "Create: %s (0x%08X)", ct, (unsigned)of->hdr.ctime);
    out(line, COLOR_LTGRAY, ud);

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

            if (fil_rec_is_link(fd, i)) {
                FID lfid = FID_INVALID;
                char lname[48] = "";
                if (fil_get_rec_link_info(fd, i, &lfid, lname, sizeof(lname), NULL) == 0 && lfid != FID_INVALID) {
                    if (lname[0]) {
                        snprintf(line, sizeof(line), "       -> Link Target: FID %u (\"%s\")", (unsigned)lfid, lname);
                    } else {
                        snprintf(line, sizeof(line), "       -> Link Target: FID %u", (unsigned)lfid);
                    }
                    out(line, COLOR_CYAN, ud);
                }
            } else if (of->ridx[i].size > 0 && of->ridx[i].size <= 512) {
                ID rec = opn_rec(fd, (W)i, 0x0001);
                if (rec >= 0) {
                    unsigned char pbuf[128];
                    W got = 0;
                    rd_rec(rec, pbuf, sizeof(pbuf), &got);
                    cls_rec(rec);
                    if (got >= 4) {
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
                        }
                    }
                }
            }
        }
    } else {
        out("Records: (0 records)", COLOR_LTGRAY, ud);
    }

    cls_fil(fd);
}

/* ── Entry Points ────────────────────────────────────────────────── */

void clu_cmd_stat(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("stat", args, out, ud);
}

void clu_cmd_info(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("info", args, out, ud);
}

void clu_cmd_finfo(const char *args, ShellOutputFn out, void *ud)
{
    clu_stat_internal("finfo", args, out, ud);
}

void clu_cmd_chmod(const char *args, ShellOutputFn out, void *ud)
{
    const char *p = args;
    p = clu_skip_ws(p);
    int aflag = 0;
    if (strncmp(p, "-a", 2) == 0) { p += 2; aflag = (*p++) - '0'; }
    p = clu_skip_ws(p);
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

void clu_cmd_touch(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));
    if (!target[0]) { out("touch: missing path", COLOR_RED, ud); return; }

    ID fd = opn_fil(target, 0x0002);
    if (fd < 0) {
        fd = cre_fil(target, 0x0002);
        if (fd < 0) { out("touch: cannot create file", COLOR_RED, ud); return; }
        cls_fil(fd);
        char msg[128]; snprintf(msg, sizeof(msg), "Created '%s'", target);
        out(msg, COLOR_GREEN, ud); return;
    }
    OpenFile *of = &g_open_files[(int)fd];
    UW ts = (UW)(
#if BTRON_HOSTED
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

void clu_cmd_chtime(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = clu_get_targets(args, targets, 2);
    if (n < 1) { out("chtime: usage: chtime [-m] [time] <path>", COLOR_RED, ud); return; }
    const char *tpath = (n >= 2) ? targets[1] : targets[0];

    ID fd = opn_fil(tpath, 0x0002);
    if (fd < 0) { out("chtime: file not found", COLOR_RED, ud); return; }
    OpenFile *of = &g_open_files[(int)fd];
    UW ts = (UW)(
#if BTRON_HOSTED
        time(NULL) - 946684800L
#else
        0
#endif
    );
    of->hdr.atime = ts;
    of->hdr.mtime = ts;
    of->dirty = 1;
    cls_fil(fd);
    if (g_sys_vol) vol_mark_dirty(g_sys_vol);
    char msg[128];
    snprintf(msg, sizeof(msg), "Updated timestamps for '%s'", tpath);
    out(msg, COLOR_GREEN, ud);
}
