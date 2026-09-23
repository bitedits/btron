/* See include/btron/dp_cal.h for what this measures and what it deliberately
 * cannot see.  Everything here is integer and knows no clock: the counts going
 * in and the pixels coming out are the only two quantities that have to be
 * trustworthy, which is what makes the same file usable on a port with no timer
 * and on an emulator whose clock is not this one's. */

#include <btron/dp_cal.h>

/* One-count reports are what a slow drag produces and the case a residual
 * carry exists for; sixteen-count reports are what a flick produces and the
 * case an accelerator curve exists for.  The count total is held at
 * DP_CAL_COUNTS for every row so a number that changes between rows is a
 * changed gain rather than a changed sample size. */
static const int s_amps[] = { 1, 4, 16 };
#define DP_CAL_NAMPS ((int)(sizeof(s_amps) / sizeof(s_amps[0])))

/* Where a sweep starts, kept far enough from the border it travels away from
 * that the largest gain in the tree still has room for DP_CAL_COUNTS counts. */
static int s_start_x, s_start_y;

static void park(const dp_cal_port_t *p, int x, int y, int *rx, int *ry)
{
    p->set_pos(x, y);
    p->get_pos(rx, ry);   /* the port's answer, not the one asked for */
}

/* One amplitude of the transfer function on one axis.  The other axis is read
 * in the same row because an axis that leaks into its neighbour is the failure
 * this exists to catch, and it can only be seen as motion on the axis that was
 * not asked for. */
static int gain_sweep(const dp_cal_port_t *p, int amp, int vertical)
{
    const int n = (int)(DP_CAL_COUNTS / amp);
    int x0, y0, x1, y1;

    park(p, vertical ? s_start_x : 0, vertical ? 0 : s_start_y, &x0, &y0);
    for (int i = 0; i < n; i++) {
        if (vertical) p->send(0, amp);
        else          p->send(amp, 0);
    }
    p->get_pos(&x1, &y1);

    const long moved = vertical ? (long)(y1 - y0) : (long)(x1 - x0);
    const long stray = vertical ? (long)(x1 - x0) : (long)(y1 - y0);

    p->row(vertical ? "GAINY" : "GAINX", amp, moved, stray, DP_CAL_COUNTS);
    return stray != 0;
}

/* Push until the border stops the pointer, then pull back the same number of
 * reports.  Losing counts against a border is not a fault -- a relative pointer
 * has nowhere to put them -- but losing them on the way back is, and that is
 * what a pointer that will not leave an edge looks like.  The row says how far
 * it got pushed out, how many counts that took, where it came back to, and by
 * how much it fell short of the edge it started from. */
static int edge_sweep(const dp_cal_port_t *p, int vertical)
{
    const int amp   = 8;
    const int limit = (p->width + p->height) / amp + 16;
    int x, y;

    park(p, vertical ? s_start_x : 0, vertical ? 0 : s_start_y, &x, &y);
    int last = vertical ? y : x;
    int out  = 0;
    while (out < limit) {
        if (vertical) p->send(0, amp);
        else          p->send(amp, 0);
        out++;
        p->get_pos(&x, &y);
        const int now = vertical ? y : x;
        if (now == last) break;
        last = now;
    }

    for (int i = 0; i < out; i++) {
        if (vertical) p->send(0, -amp);
        else          p->send(-amp, 0);
    }
    p->get_pos(&x, &y);
    const long back  = vertical ? y : x;
    const long wall  = last;
    const long counts = (long)out * amp;

    p->row(vertical ? "EDGEY" : "EDGEX", wall, counts, back, wall - back);
    return back != 0;
}

/* Equal and opposite reports, so the counts sum to nothing.  A scale that
 * quantizes one direction more generously than the other walks the pointer away
 * from where it started on a hand that never moved, and the amount it walks is
 * the size of that asymmetry. */
static int drift_sweep(const dp_cal_port_t *p)
{
    int x0, y0, x1, y1;

    park(p, s_start_x, s_start_y, &x0, &y0);
    for (int i = 0; i < DP_CAL_COUNTS; i++) {
        p->send(1, 1);
        p->send(-1, -1);
    }
    p->get_pos(&x1, &y1);

    const long dx = x1 - x0, dy = y1 - y0;
    p->row("DRIFT", dx, dy, 0, 0);
    /* One pixel either way is the last count still being paid out of the
     * residual, which is what the residual is for; more is the imbalance. */
    return (dx > 1 || dx < -1 || dy > 1 || dy < -1);
}

/* Both axes asked for the same number of counts, so an axis with its own gain,
 * its own clamp, or its own sign convention cannot hide. */
static int diagonal_sweep(const dp_cal_port_t *p)
{
    const int amp = s_amps[DP_CAL_NAMPS - 1];
    const int n   = (int)(DP_CAL_COUNTS / amp);
    int x0, y0, x1, y1;

    park(p, 0, 0, &x0, &y0);
    for (int i = 0; i < n; i++) p->send(amp, amp);
    p->get_pos(&x1, &y1);

    const long dx = x1 - x0, dy = y1 - y0;
    p->row("DIAG", dx, dy, dx - dy, 0);
    return dx != dy;
}

int dp_cal_run(const dp_cal_port_t *p)
{
    if (!p || !p->send || !p->get_pos || !p->set_pos || !p->row) return -1;
    if (p->width < 64 || p->height < 64) return -1;

    int fails = 0;
    /* Far enough from the bottom and right that even a doubled gain spends
     * DP_CAL_COUNTS counts before the border rather than against it. */
    s_start_x = p->width  / 4;
    s_start_y = p->height / 4;

    for (int i = 0; i < DP_CAL_NAMPS; i++) fails += gain_sweep(p, s_amps[i], 0);
    for (int i = 0; i < DP_CAL_NAMPS; i++) fails += gain_sweep(p, s_amps[i], 1);
    fails += edge_sweep(p, 0);
    fails += edge_sweep(p, 1);
    fails += drift_sweep(p);
    fails += diagonal_sweep(p);

    p->row("CAL", fails, DP_CAL_COUNTS, p->width, p->height);
    return fails;
}
