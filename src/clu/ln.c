/*
 * B-System BTRON3 — src/clu/ln.c
 * CLU Hypermedia Link & Virtual-Object Link Utility (ln)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Naturally aligned FS_LINK struct creation; zero unaligned memory
 *       accesses when passing struct pointers across the VFS layer.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Clean 32-bit FID resolution.
 *   - x86 / x86_64: Works seamlessly across hosted and bare-metal environments.
 *   - M68k (Motorola 68000):
 *       Explicit memset initialization to clear structure padding bytes.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Zero dynamic allocation.
 *   - Strict error checking on target open and link creation.
 *   - Safe, bounded string formatting.
 */

#include "cluc.h"

void clu_cmd_ln(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = clu_get_targets(args, targets, 2);
    if (n < 2) { out("ln: usage: ln <src> <linkname>", COLOR_RED, ud); return; }

    ID src_fd = opn_fil(targets[0], 0x0001);
    if (src_fd < 0) {
        char err[128]; snprintf(err, sizeof(err), "ln: '%s': not found", targets[0]);
        out(err, COLOR_RED, ud); return;
    }
    FID src_fid = g_open_files[(int)src_fd].fid;
    cls_fil(src_fd);

    FS_LINK lnk;
    memset(&lnk, 0, sizeof(lnk));
    lnk.target_fid = src_fid;

    ER err = cre_lnk(targets[1], &lnk);
    if (err != 0) { out("ln: cre_lnk failed", COLOR_RED, ud); return; }
    char msg[128]; snprintf(msg, sizeof(msg), "Link '%s' -> '%s'", targets[1], targets[0]);
    out(msg, COLOR_GREEN, ud);
}
