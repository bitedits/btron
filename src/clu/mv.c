/*
 * B-System BTRON3 — src/clu/mv.c
 * CLU File Move & Rename Utility (mv, ren)
 *
 * Core Portability Notes:
 *   - MIPS R5900 (Sony PS2 Emotion Engine, O32 ABI):
 *       Standard 32-bit registers, no alignment hazards.
 *   - ARM32 / ARM64 (Raspberry Pi BCM2835/BCM2837):
 *       Zero dynamic allocation; synchronous volume metadata update.
 *   - x86 / x86_64: Clean compile across GCC and Clang.
 *   - M68k (Motorola 68000):
 *       Minimal stack usage; constant 128-byte message buffer.
 *
 * NASA/JPL "Power of Ten" Compliance:
 *   - Single fixed-step operation (mov_fil) followed by sync.
 *   - Return code validation.
 *   - Fixed bounds on argument extraction.
 */

#include "cluc.h"

void clu_cmd_mv(const char *args, ShellOutputFn out, void *ud)
{
    char targets[2][80];
    int n = clu_get_targets(args, targets, 2);
    if (n < 2) { out("mv: usage: mv <old> <new>", COLOR_RED, ud); return; }
    ER err = mov_fil(targets[0], targets[1]);
    if (err != 0) {
        out("mv: rename/move failed", COLOR_RED, ud); return;
    }
    char msg[128]; snprintf(msg, sizeof(msg), "Renamed '%s' -> '%s'", targets[0], targets[1]);
    out(msg, COLOR_GREEN, ud);
    if (g_sys_vol) vol_sync(g_sys_vol);
}
