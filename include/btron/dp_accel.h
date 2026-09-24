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
 * Anything outside 1..4 is treated as step 4, as the curve's own default was. */
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

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_DP_ACCEL_H_ */
