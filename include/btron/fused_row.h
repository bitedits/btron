/*
 * B-System compositor row rule: fused_row.h
 *
 * One row of a client pixmap onto the screen canvas, writing every pixel that is not
 * the transparent sentinel and leaving the sentinel pixels exactly as the canvas has
 * them.  This is the rule `redraw_all_windows_clip()`'s walk has always applied; what
 * lives here is the two shapes it can be computed in, and the dial that picks one.
 *
 * WHY THERE ARE TWO SHAPES AT ALL
 *
 * The walk has asked the question per pixel and then copied the row: read the band to
 * DECIDE whether it may copy it, then read the SAME band again to copy it.  Two passes
 * over bytes that were already in registers on the first one.  The bench priced that
 * shape against a fused one on the EE (src/cores/core_ps2.c, the RAM-to-RAM phase, a
 * 400x16 rect -- 6,400 px per rep, all rows `chk=ok`; the run is 2026-10-14 and its last
 * row is the shipped code itself, called through btron_row_copy_skip_zero()):
 *
 *   decide-then-copy, per pixel        39.84 ns/px   (what shipped before this file)
 *   decide-then-copy, 64-bit decide    36.58         (the shave `blit wide` measured)
 *   fused,  2 px per decision          33.33
 *   fused,  4 px, 32-bit words         25.52
 *   fused,  8 px, 32-bit words         22.51
 *   fused, 16 px, 32-bit words         21.09
 *   fused, as this file writes it      20.95         <- btron_row_copy_fused()
 *   fused,  8 px, bit-test decision    33.28         (rejected, see below)
 *   the copy alone, no decision        15.96         (btron_row_blit's own floor)
 *
 * So a row pays 4.99 ns/px for the decision and nothing at all for the second read,
 * against the 23.88 the two-pass shape pays for the pair.  The fused pass sits 5 ns/px
 * above a pure copy, which is the ceiling this shape could reach; the walk used to sit 24
 * above it.  Read off the shipped compositor's own column rather than the bench's rect,
 * the same row moved from `blit=24481(613440 px)` to `blit=12665` on the identical pixel
 * count -- 39.90 to 20.65 ns/px, 1.93x -- with `bg=` and `frame=` unmoved in the same
 * pass, which is the control that says the blit column is what changed.
 *
 * WHAT THE ROWS DO NOT SAY
 *
 * The 64-bit pair forms (the per-pixel `f2`, and the 4/8-px `f4`/`f8` that load two
 * pixels per access) were faster at reading but not at deciding: `f4` measured 26.39
 * against `f4w`'s 25.52, so nothing is paid for staying in 32-bit words.  Staying in
 * them is not a style choice either -- in the walk the destination row starts at
 * `dest_x + cx0` and the source row at `cx0`, so the two differ in phase by the
 * window's own left edge, and a pair access is only the same shape in both buffers
 * while that edge is even.  A word-based group has no phase to respect, so one body is
 * correct at every damage rect and every window origin.
 *
 * The bit-test decision -- `(w - 1) & ~w & 0x80000000` per pixel ORed together, instead
 * of sixteen compares -- was measured and rejected: 33.28 clean and 40.57 when the
 * content is dirty, i.e. the WORST row in the sweep, worse than the two-pass shape it
 * was trying to replace.  Arithmetic does not substitute for the compare here.
 *
 * TWO CONTENTS DECIDE, NOT ONE
 *
 * The rows above are an all-opaque band, which is what production composites: before this
 * file, the shipped walk's own column read `blit=24481(613440 px)` = 39.90 ns/px, the
 * bench's `scan` rung to two decimals, so instrument and product agreed.  A band with one
 * pixel in eight set to the sentinel is the other regime, and it reorders the ladder --
 * the two-pass walk gets CHEAPER on dirty content (30.38, because it stops scanning at
 * the first sentinel and never pays for the copy) while a fused group of 16 always fails
 * its decision and takes the per-pixel fixup.  Even there the fused pass wins, at 22.40,
 * and 16 px is the group that wins BOTH regimes; the smaller groups land at 25.52 (8 px)
 * and 29.34 (4 px) under the same dirty band.  That is the reason the fixup over a
 * failed group is per-pixel and works from the values already loaded: it is the path a
 * transparent window takes on every row, not an error path.
 *
 * WHY ONLY ONE PORT TAKES IT
 *
 * `btron_row_copy_twopass()` is the shipped row verbatim, including its copy through
 * btron_row_blit(), whose 64-byte NEON loop is what makes the Pi 400's composite fast.
 * A fused pass would put a per-pixel decision inside that vector loop and take the
 * benefit away.  So the dial is the target macro, not a shared judgement: the PS2 port
 * gets the fused row because the EE has no vector copy to lose, and every other port
 * keeps the code it has always run.
 *
 * The gate is verify/tests/test_ps2_fused_row.c: both shapes against the scalar rule,
 * byte-identical canvas over random content at every boundary a row can have.  It exists
 * because the bench's own checksum cannot see this -- it samples the destination every
 * 64th pixel, and the sentinel columns never fall on a sample.
 */

#ifndef _BTRON_FUSED_ROW_H_
#define _BTRON_FUSED_ROW_H_

#include <btron/types.h>
#include <btron/fast_blit.h>

/* The value a client pixmap writes for "draw nothing here".  wnd.c has spelled it
 * literally since the walk existed; naming it here is what lets the gate sweep it. */
#ifndef BTRON_ROW_TRANSPARENT
#define BTRON_ROW_TRANSPARENT 0x00000000u
#endif

/* How many pixels one decision covers in the fused pass.  16 is the measured winner in
 * both content regimes; the gate asserts the group boundary cases at this size, so a
 * change here has to be a measurement and not an edit. */
#ifndef BTRON_ROW_FUSE_GROUP
#define BTRON_ROW_FUSE_GROUP 16
#endif

/* One pass: read each group once, decide once, then either store the group or fix it up
 * from the values already in hand.  No second read of the band.  The loads are 32-bit so
 * src and dst need not share any phase, and the tail is the scalar rule, so a row of any
 * length is covered. */
static inline void btron_row_copy_fused(COLOR *dst, const COLOR *src, int span)
{
    int i = 0;

    for (; i + (BTRON_ROW_FUSE_GROUP - 1) < span; i += BTRON_ROW_FUSE_GROUP) {
        COLOR w[BTRON_ROW_FUSE_GROUP];
        int dirty = 0;

        for (int k = 0; k < BTRON_ROW_FUSE_GROUP; k++) w[k] = src[i + k];
        for (int k = 0; k < BTRON_ROW_FUSE_GROUP; k++) {
            dirty |= (w[k] == BTRON_ROW_TRANSPARENT);
        }
        if (!dirty) {
            for (int k = 0; k < BTRON_ROW_FUSE_GROUP; k++) dst[i + k] = w[k];
        } else {
            for (int k = 0; k < BTRON_ROW_FUSE_GROUP; k++) {
                if (w[k] != BTRON_ROW_TRANSPARENT) dst[i + k] = w[k];
            }
        }
    }
    for (; i < span; i++) {
        if (src[i] != BTRON_ROW_TRANSPARENT) dst[i] = src[i];
    }
}

/* The shipped row, unchanged in substance: scan until the first sentinel, and take the
 * wide copy only when the whole span survived the scan.  Kept in this header so the gate
 * can hold it to the same reference as the fused pass, and so the non-PS2 ports keep one
 * copy of the rule rather than a parallel model of it. */
static inline void btron_row_copy_twopass(COLOR *dst, const COLOR *src, int span)
{
    int opaque = 1;

    for (int cx = 0; cx < span; cx++) {
        if (src[cx] == BTRON_ROW_TRANSPARENT) {
            opaque = 0;
            break;
        }
    }
    if (opaque) {
        btron_row_blit(dst, src, (size_t)span * sizeof(COLOR));
    } else {
        for (int cx = 0; cx < span; cx++) {
            if (src[cx] != BTRON_ROW_TRANSPARENT) dst[cx] = src[cx];
        }
    }
}

/* The rule as one call, with the port's measured shape behind it. */
static inline void btron_row_copy_skip_zero(COLOR *dst, const COLOR *src, int span)
{
#if defined(BTRON_PS2_TARGET)
    btron_row_copy_fused(dst, src, span);
#else
    btron_row_copy_twopass(dst, src, span);
#endif
}

#endif /* _BTRON_FUSED_ROW_H_ */
