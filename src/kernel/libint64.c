/*
 * libint64.c - the 64-bit integer compiler-runtime calls, one home for every lane
 *
 * clang emits `__udivdi3`, `__umoddi3` and `__divdi3` wherever a source divides a
 * 64-bit count on a machine whose register file cannot do it.  Two lanes here are
 * built that way and neither links a libc:
 *
 *   PS2   --target=mipsel-unknown-elf -march=mips2 -mabi=32   (O32: no 64-bit
 *            divide, rem or conversion instruction exists)
 *   UEFI, PC-98 --target=i686-none-elf, ld -m elf_i386        (32-bit x86: `div`
 *            is 32-bit; a 64-bit quotient needs the same helper)
 *
 * The DAW divides 64-bit counts constantly -- the transport converts samples to
 * nanoseconds and back (src/media/gst_rt.c), the arrange ruler maps pixels to bars
 * (orchestra_arrange.c), the SMF loader walks a delta-time stream
 * (orchestra_smf.c) -- so these three names are what stops `make uefi`,
 * `make pc98` and `make ps2` at the link.
 *
 * Two homes were possible.  The PS2 lane's own precedent for a count that does not
 * fit the machine is to narrow the arithmetic (see the `no 64-bit divisor`
 * comments in core_ps2.c, ps2_irq.c and ps2_ptr_stroke.h), and that is right for a
 * cycle counter whose whole range is 32 bits by construction.  It is not right
 * here: ns-since-start over a frame rate is a 64-bit quantity by definition, and
 * rewriting each of those sites to a 32-bit approximation would change what the
 * transport measures -- a silent behavior edit in files the bench times.  So the
 * runtime answers it instead.
 *
 * This used to live at the end of src/drivers/ps2/ps2_builtins.c.  It moved here
 * because the i386 lanes need the identical three functions: one hand-written
 * truth, linked by every lane that asks for it, instead of a second copy that can
 * drift.  It is in COMMON_NO_SDL_SRCS, so it reaches uefi, pc98, ps2, m68k and the
 * two bare-metal ARM images; the hosted lanes (posix, qemu) have a real libc and
 * native 64-bit arithmetic and never see this file.
 *
 * The loop below keeps the running remainder in two 32-bit registers and does every
 * compare and subtract on that pair, because an implementation written with
 * `uint64_t / uint64_t` would recurse into the symbol it defines.  Shifts and the
 * reassembly are free: clang turns them into register moves, not calls.
 *
 * Correctness is checked against the host's own `/` and `%` over the whole edge
 * corpus plus a randomized sweep: `make test-ps2-softfloat`, which #includes this
 * source so the host cannot quietly resolve the names from compiler-rt and grade
 * its own code instead of ours.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>

/* ── The 64-bit integer libcalls ──────────────────────────────────────────── */

static uint64_t u64_divmod(uint64_t n, uint64_t d, uint64_t *rem)
{
    const uint32_t nh = (uint32_t)(n >> 32), nl = (uint32_t)n;
    const uint32_t dh = (uint32_t)(d >> 32), dl = (uint32_t)d;
    uint32_t qh = 0u, ql = 0u;
    uint32_t rh = 0u, rl = 0u;
    int i;

    if (d == 0) {
        /* Division by zero is undefined in C and no caller in the tree can
         * reach it (a zero frame rate is refused upstream).  Returning zero
         * with the dividend as the remainder is a choice, not a law: what
         * matters is that the answer is obviously wrong rather than plausible,
         * so a mistake reads as a stalled transport instead of 2^64-1. */
        if (rem) *rem = n;
        return 0;
    }
    if (nh == 0u && dh == 0u) {
        /* Both operands fit 32 bits -- a frame count over a sample rate -- and
         * the machine's own 32-bit divide answers that, so the 64-iteration
         * loop is only paid for the divides that genuinely need 64 bits. */
        if (rem) *rem = (uint64_t)(nl % dl);
        return (uint64_t)(nl / dl);
    }
    for (i = 0; i < 64; i++) {
        const uint32_t bit = (i < 32) ? ((nh >> (uint32_t)(31 - i)) & 1u)
                                      : ((nl >> (uint32_t)(63 - i)) & 1u);
        rh = (rh << 1) | (rl >> 31);
        rl = (rl << 1) | bit;
        if (rh > dh || (rh == dh && rl >= dl)) {
            const uint32_t borrow = (rl < dl) ? 1u : 0u;
            rl -= dl;
            rh = rh - dh - borrow;
            if (i < 32) qh = (uint32_t)((qh << 1) | 1u);
            else        ql = (uint32_t)((ql << 1) | 1u);
        } else {
            if (i < 32) qh <<= 1;
            else        ql <<= 1;
        }
    }
    if (rem) *rem = ((uint64_t)rh << 32) | (uint64_t)rl;
    return ((uint64_t)qh << 32) | (uint64_t)ql;
}

uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    return u64_divmod(a, b, 0);
}

uint64_t __umoddi3(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    (void)u64_divmod(a, b, &r);
    return r;
}

/* Signed quotient: magnitude-divide, then apply the sign of the operands.  The
 * magnitudes come from unsigned subtraction so INT64_MIN, whose absolute value
 * is not representable, is handled without the undefined negation.  C truncates
 * toward zero, which is what the unsigned loop does; only the sign is added
 * here. */
int64_t __divdi3(int64_t a, int64_t b)
{
    const uint32_t neg = ((a < 0) ^ (b < 0)) ? 1u : 0u;
    const uint64_t ma = (a < 0) ? (uint64_t)0u - (uint64_t)a : (uint64_t)a;
    const uint64_t mb = (b < 0) ? (uint64_t)0u - (uint64_t)b : (uint64_t)b;
    const uint64_t q = u64_divmod(ma, mb, 0);
    return neg ? (int64_t)((uint64_t)0u - q) : (int64_t)q;
}
