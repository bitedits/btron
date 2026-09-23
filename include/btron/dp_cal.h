#ifndef BTRON_DP_CAL_H
#define BTRON_DP_CAL_H

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Pointer loopback calibration.
 *
 * The question this answers is not "is the mouse too fast" -- that one has a
 * hand in it and only the operator can say.  It is the prior question, which is
 * whether this port turns N counts into a predictable number of pixels at all:
 * whether an axis leaks into the other, whether counts spent against a border
 * come back when the pointer is pulled away from it, whether the residual a
 * sub-1 scale holds back is paid out symmetrically, and what the gain actually
 * is at a small movement versus a large one.  Those are answerable without a
 * person, by feeding the port's own report seam counts whose totals are known
 * and measuring what the pointer did with them.
 *
 * What is deliberately NOT here: the source of the counts.  On an emulator the
 * numbers arriving over USB have already been accelerated and coalesced by the
 * host before the first byte existed, and no guest-side measurement can see
 * through that -- this tool characterises the port, and the live rows say what
 * the host is doing to it.  A port whose table is flat is a port that can be
 * calibrated; one whose table wanders has a bug in it, whatever the hand says.
 */
typedef struct {
    /* Feed one pointer report into the seam where the port would put a real
     * one: after whatever decodes the wire, before the position gets drawn. */
    void (*send)(int dx, int dy);
    void (*get_pos)(int *x, int *y);
    /* Put the cursor somewhere.  Required, because every measurement here has
     * to start from a known place to mean anything. */
    void (*set_pos)(int x, int y);
    int   width, height;
    /* One row of results.  Five arguments and no varargs on purpose: this runs
     * on a target whose compiler lays a variadic save area out wider than the
     * va_arg side walks it, so the formatting stays with the port that knows
     * its own ABI. */
    void (*row)(const char *tag, long v0, long v1, long v2, long v3);
} dp_cal_port_t;

#define DP_CAL_COUNTS 64L    /* counts per gain sample, held constant across sizes */

/* Runs every check and prints the table.  Returns the number that failed:
 * the gain table is data either way, and only the four consistency checks
 * carry a verdict. */
int dp_cal_run(const dp_cal_port_t *port);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_DP_CAL_H */
