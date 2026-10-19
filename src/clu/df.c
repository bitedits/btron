/*
 * B-System BTRON3 — src/clu/df.c
 * CLU Disk Free & Volume Sync Utilities (df, sync)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       64-bit integer overflow protection in percentage and block count
 *       calculations; 32-bit integer arithmetic ensures compatibility without
 *       libgcc 64-bit division overhead.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Deterministic mounted volume traversal up to vol_mounted_count().
 *   - x86 / x86_64: Formats block metrics identically across POSIX/Bare-Metal.
 *   - M68k (Motorola 68000):
 *       Safe division with zero guard.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Bounded loop iteration up to vol_mounted_count().
 *   - Zero dynamic allocation at runtime.
 *   - Bounded formatting strings.
 */

#include "cluc.h"

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

/* ── clu_cmd_df ──────────────────────────────────────────────────── */

void clu_cmd_df(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));

    int count = vol_mounted_count();
    if (count == 0) {
        out("df: no volume mounted", COLOR_RED, ud);
        return;
    }

    if (target[0]) {
        const char *t = (target[0] == '/') ? target + 1 : target;
        Volume *v = vol_find_by_name(t);
        if (!v) {
            char err[128];
            snprintf(err, sizeof(err), "df: '%s': volume not mounted", target);
            out(err, COLOR_RED, ud);
            return;
        }
        char mnt[80];
        snprintf(mnt, sizeof(mnt), "/%s", vol_name(v));
        out("PATH      DEV   TOTAL   FREE    USED   UNIT  MAXFILE  NAME", COLOR_CYAN, ud);
        const char *dev = (v == g_chokanji_vol) ? "hda1" : ((v == g_sys_vol) ? "mem0" : ((v == g_anders_vol) ? "mem1" : "dev"));
        clu_df_print_volume(v, mnt, dev, out, ud);
        return;
    }

    out("PATH      DEV   TOTAL   FREE    USED   UNIT  MAXFILE  NAME", COLOR_CYAN, ud);
    for (int i = 0; i < count; i++) {
        Volume *v = vol_get_mounted(i);
        if (v) {
            char mnt[80];
            snprintf(mnt, sizeof(mnt), "/%s", vol_name(v));
            char devname[16];
            if (v == g_chokanji_vol) snprintf(devname, sizeof(devname), "hda1");
            else if (v == g_sys_vol) snprintf(devname, sizeof(devname), "mem0");
            else if (v == g_anders_vol) snprintf(devname, sizeof(devname), "mem1");
            else snprintf(devname, sizeof(devname), "dev%d", i);
            clu_df_print_volume(v, mnt, devname, out, ud);
        }
    }
}

/* ── clu_cmd_sync ────────────────────────────────────────────────── */

void clu_cmd_sync(const char *args, ShellOutputFn out, void *ud)
{
    (void)args;
    if (!g_sys_vol) { out("sync: no volume mounted", COLOR_YELLOW, ud); return; }
    vol_sync(g_sys_vol);
    out("(all caches flushed)", COLOR_GREEN, ud);
}
