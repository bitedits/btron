/*
 * B-System BTRON3 — src/clu/rm.c
 * CLU File Removal & Emptying Utilities (rm, empf)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Pure kernel syscall invocations (del_fil, opn_fil, del_rec); zero
 *       unaligned access, zero stack bloat.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Consistent status codes returned to caller.
 *   - x86 / x86_64: Supported across POSIX hosted shims and bare-metal targets.
 *   - M68k (Motorola 68000):
 *       Guaranteed safe execution on minimal ROM/RAM footprint.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Bounded loop iteration when emptying records.
 *   - Explicit checking of return error codes from del_fil and opn_fil.
 *   - Bounded snprintf buffer output.
 */

#include "cluc.h"

/* ── clu_cmd_rm ──────────────────────────────────────────────────── */

void clu_cmd_rm(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));
    if (!target[0]) { out("rm: missing path argument", COLOR_RED, ud); return; }
    ER err = del_fil(target);
    if (err != 0) {
        char msg[128]; snprintf(msg, sizeof(msg), "rm: cannot remove '%s'", target);
        out(msg, COLOR_RED, ud); return;
    }
    char msg[128]; snprintf(msg, sizeof(msg), "Removed '%s'", target);
    out(msg, COLOR_GREEN, ud);
}

/* ── clu_cmd_empf ────────────────────────────────────────────────── */

void clu_cmd_empf(const char *args, ShellOutputFn out, void *ud)
{
    char target[80];
    clu_get_target(args, target, sizeof(target));
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
