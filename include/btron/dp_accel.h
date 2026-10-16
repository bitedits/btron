/*
 * B-TRON Specification Compatible Header: dp_accel.h
 * Pointer motion: a relative mouse report turned into a distance on screen.
 *
 * Shared because every port with an HID pointer faces the same arithmetic, and
 * because the part that must not differ between ports is the arithmetic itself:
 * a remainder dropped on one report is a pixel that never comes back.  What
 * deliberately stays per-port is the multiplier and everything done with the
 * result -- the Pi 400 has a hardware cursor plane it can move without
 * repainting, the PS2 has to redraw, and a real serial mouse reports at a
 * different rate from an emulated one.
 */
#ifndef _BTRON_DP_ACCEL_H_
#define _BTRON_DP_ACCEL_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One axis of a report, scaled and integrated.
 *
 * mult_fp is 8.8 fixed point, so 256 hands the device's counts back unchanged,
 * 512 doubles them and 128 halves them -- values below 256 are the point of this
 * function, because a source that reports the host pointer's already-accelerated
 * movement needs to be slowed down and every accelerator here only speeds up.
 *
 * carry is the subpixel remainder left over from the last report, in 1/256ths of
 * a pixel; pass NULL to integrate without memory.  It is bounded at +/-255, so a
 * report that brings zero on this axis leaves it alone rather than halving it: a
 * paused axis cannot bank up into a jump it was not owed, but a hand that moves
 * one axis at a time is forever reporting zero on the other, and decaying there
 * is how a scale below 256 ends up never paying out a pixel at all.
 *
 * Returns whole pixels, truncated symmetrically towards zero and clamped to
 * +/-512, with the leftover written back through carry. */
int32_t dp_ptr_scale(int32_t raw, int32_t mult_fp, int32_t *carry);

/* dp_ptr_scale with the Archimedes / RISC OS CMOS &C2 MouseStep curve in front
 * of it: step 1..4 selects 1.5x, 2.0x, 2.5x, 3.0x, each gaining a further 0.5x
 * once the move is more than four counts, and a move of two counts or less is
 * multiplied by 1.5 first so that fine positioning survives a coarse step.
 * Anything outside 1..4 is treated as step 4, as the curve's own default was.
 *
 * Declared for every target, defined for every target except BTRON_PS2_TARGET,
 * whose pointer law (doc/txt/PS2.txt LAW 13d) allows no speed-dependent gain at
 * all: a PS2 caller compiles against this header and fails at the link, which is
 * the enforcement that file keeps deliberately. */
int32_t dp_ptr_riscos(int32_t raw, int step, int32_t *carry);

/* Cap how far the cursor may move for one call, and hand the rest of it to the
 * next call.
 *
 * The unit is whatever the caller calls "one go", and it should be the thing the
 * eye sees: a frame, not a report.  A device that reports at 1 kHz can hand over
 * eight reports between two paints, so capping each of them bounds nothing a
 * person could notice -- accumulate the reports and spend them here once per
 * paint.  `want` is the distance arrived since the last call, and must not
 * already contain the previous debt, which this function adds itself.
 *
 * This is the difference between a pointer that is slow and a pointer that is
 * short.  Dropping the excess loses travel, so a stroke that started at one edge
 * of the screen ends up in the middle of it and the hand has to repeat itself --
 * the same class of fault as losing reports anywhere else along this path.
 * Deferring it keeps the whole stroke and merely spends it over more frames, so
 * the cursor cannot be made to jump but still arrives where the hand meant.
 *
 * The debt is bounded at DP_PTR_LIMIT_DEBT steps on purpose: a flick far above
 * the cap is then given up on rather than chased for a second afterwards, which
 * is the difference between a slow pointer and one that keeps crawling after the
 * hand has stopped moving.  max_step <= 0 means no cap and leaves the debt
 * untouched, and defer may be NULL, which also drops rather than defers. */
#define DP_PTR_LIMIT_DEBT 2
int32_t dp_ptr_limit(int32_t want, int32_t max_step, int32_t *defer);

/* ── The gain dial (doc/txt/PS2.txt LAW 13e) ────────────────────────────
 *
 * One multiplier, one home, several hands.  `g_ptr_gain_fp` is that
 * multiplier -- 8.8 fixed point, the unit dp_ptr_scale() takes, 0 meaning
 * "whatever the live source profile's own default is" -- and the hands that can
 * turn it (the Stage-1 `sens` prompt and the gain chords, the Settings > Input
 * panel's gain block, the launcher's row in the pointer band) all write this one
 * variable rather than keeping a copy of their own.  It is defined in
 * src/settings/input.c, next to the panel that draws the dial.
 *
 * btron_ptr_gain_fp[] is the dial's stops and the only list of them, and
 * btron_ptr_gain_label() prints a stop from that list, so the panel's radios and
 * the launcher's row cannot name the same gain two different ways -- what the
 * numbers are called is derived from what they are, not retyped per control.
 * Index 0 is the auto choice; a hand that wants a fixed gain picks 1..6.
 *
 * Defined for the PS2 target only, and declared for every target, which is the
 * mirror image of dp_ptr_riscos() above: a port whose pointer has no dial of its
 * own fails at the LINK if it tries to read one, and the Pi 400 -- whose gain is
 * the curve's `step`, not this constant -- keeps the bytes it has. */
#define BTRON_PTR_GAIN_STEPS 7
extern const int32_t btron_ptr_gain_fp[BTRON_PTR_GAIN_STEPS];
extern int32_t g_ptr_gain_fp;

/* Which stop a gain sits nearest, for drawing the dial from the live value:
 * `sens 38` and a hand that then opens the panel should agree on one row.
 * Any value at or below the auto step, and any negative, reads as index 0. */
int btron_ptr_gain_index(int32_t fp);

/* What stop `index` is called, one line, built from the table on first use and
 * kept: "Auto (source gain)", or "0.38 px per count" -- the decimal rather than
 * the /256 form, rounded to nearest, because this line has to fit the launcher's
 * value column as well as the panel's radio.  The /256 number is the core's row
 * to print, not a control's.  Out-of-range indexes return a bare "gain" rather
 * than reading past the table. */
const char *btron_ptr_gain_label(int index);

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DP_ACCEL_H_ */
