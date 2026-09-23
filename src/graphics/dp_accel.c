/*
 * dp_accel.c -- pointer motion shared by every bare-metal port.
 *
 * A relative mouse report is a count, not a distance: what one count means on
 * screen depends on how the source produced it and on how wide the desktop is.
 * So this file owns only the arithmetic that is the same wherever it happens --
 * multiply, integrate, hand back whole pixels and remember the rest -- and each
 * port supplies its own multiplier and decides what a move is worth: the Pi 400
 * moves a hardware cursor plane for free, the PS2 has to repaint the desktop.
 *
 * The leftover is the whole reason this is shared rather than inlined at each
 * call.  Scaling by anything under 1.0 truncates a one-count report to zero, so
 * a pointer that cannot be moved gently is not a slow pointer, it is a broken
 * one; carrying the remainder in 1/256ths of a pixel is what lets a sub-1 scale
 * still answer a single count, eventually.
 *
 * Deliberately not here: the Haiku continuous curve, which is float arithmetic
 * and borrows an fsqrt instruction.  It stays in the port that offers it, and a
 * target with no floating point has to be given an integer curve of its own
 * rather than have this one weakened to suit both.
 */

#include <btron/dp_accel.h>

/* Past this a report is not a hand moving a mouse; it is a burst, a stuck
 * device, or garbage off a bus, and none of them belong the pointer across the
 * desktop in one frame. */
#define DP_PTR_MAX_COUNTS 512
#define DP_PTR_MAX_PIXELS 512

int32_t dp_ptr_scale(int32_t raw, int32_t mult_fp, int32_t *carry)
{
    if (raw == 0) {
        /* Halving rather than clearing: a remainder that survives one idle
         * report is still owed, but a long pause should not bank it up. */
        if (carry) *carry = *carry / 2;
        return 0;
    }

    if (raw >  DP_PTR_MAX_COUNTS) raw =  DP_PTR_MAX_COUNTS;
    if (raw < -DP_PTR_MAX_COUNTS) raw = -DP_PTR_MAX_COUNTS;

    int32_t total = (carry ? *carry : 0) + raw * mult_fp;

    /* C truncates towards zero and % keeps the dividend's sign, so a 0.4-pixel
     * move is held as +102 or -102 and neither direction pays out early.  A
     * floor would turn -0.4 into -1 with a positive remainder left behind, which
     * reads as the pointer drifting west while the hand is still. */
    int32_t pixels = total / 256;

    if (carry) {
        int32_t rest = total % 256;
        if (rest >  255) rest =  255;
        if (rest < -255) rest = -255;
        *carry = rest;
    }

    if (pixels >  DP_PTR_MAX_PIXELS) pixels =  DP_PTR_MAX_PIXELS;
    if (pixels < -DP_PTR_MAX_PIXELS) pixels = -DP_PTR_MAX_PIXELS;
    return pixels;
}

int32_t dp_ptr_riscos(int32_t raw, int step, int32_t *carry)
{
    int32_t mag = (raw < 0) ? -raw : raw;
    int32_t mult_fp;

    if (raw == 0) return dp_ptr_scale(0, 0, carry);

    /* Fine positioning first, so the coarse end of the curve does not destroy
     * the small moves that are used to land on a pixel boundary. */
    if (mag <= 2) mag = (mag * 3) / 2;

    if (step < 1 || step > 3) step = 4;
    mult_fp = 256 + step * 128 + (mag > 4 ? 128 : 0);

    return dp_ptr_scale((raw < 0) ? -mag : mag, mult_fp, carry);
}

int32_t dp_ptr_limit(int32_t want, int32_t max_step, int32_t *defer)
{
    int32_t v, out, debt, bound;

    if (max_step <= 0 || !defer) return want;

    v = want + *defer;
    if (v > max_step) out = max_step;
    else if (v < -max_step) out = -max_step;
    else out = v;

    /* Clamped on the way in as well as checked on the way out: without this a
     * hand that flicks 512 counts leaves the cursor walking for a quarter of a
     * second after the stroke is over, which is the exact thing that makes a
     * pointer feel like it is being dragged through something. */
    debt = v - out;
    bound = max_step * DP_PTR_LIMIT_DEBT;
    if (debt >  bound) debt =  bound;
    if (debt < -bound) debt = -bound;
    *defer = debt;
    return out;
}
