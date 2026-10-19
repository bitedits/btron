/*
 * ps2_paint_worker.c — the price of a window's paint, and whether this frame can afford it
 *
 * The law this file implements is stated in ps2_paint_worker.h; what is here is the arithmetic and
 * the two things the law has to be careful about.
 *
 * First: the clock.  Every age is a difference of the CALLER's µs counter, taken as uint32_t and
 * subtracted, so it is correct across the CP0 Count wrap the port already folds everywhere else
 * (btron_render_perf_us()) -- and, like that fold, it is only trusted while the interval is under one
 * wrap.  The longest interval asked about here is `starve_us`, two hundred thousand µs, which is a
 * tenth of a wrap; a job that has been left for longer than a wrap reads as fresh, admits itself, and
 * the worst that happens is a frame painted that did not need painting.  That direction of failure is
 * the safe one, and it is the reason the age rule is an override rather than a veto.
 *
 * Second: no division and no float.  The EWMA is (3*old + new)/4, written as a shift, because this
 * link has no libgcc and a 64-bit divide is a call to __udivdi3 that nothing answers (the same reason
 * ps2_task_ms() keeps its remainder in µs).  The rounding term is the +2, so a price does not drift
 * downward when the samples are one µs apart.
 *
 * Zero proprietary Sony SDK code: nothing here touches hardware.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_paint_worker.h"

#include <stddef.h>

#define PS2_PW_STARVE_DEFAULT 200000u   /* µs a window may go unrepainted: 12 frames at 16.667 ms */

typedef struct {
    ps2_pw_job_t job;
    int          live;
} ps2_pw_slot_t;

static ps2_pw_slot_t  s_pw[PS2_PW_MAX];
static ps2_pw_stats_t s_st;
static uint32_t       s_slice_left_us;    /* what this pass may still spend */
static uint32_t       s_starve_us = PS2_PW_STARVE_DEFAULT;
static ps2_pw_stats_t s_taken;            /* the window ps2_pw_stats_take() last reported */

static ps2_pw_job_t *ps2_pw_slot(const void *wnd)
{
    uint32_t i;

    if (!wnd) return NULL;
    for (i = 0u; i < PS2_PW_MAX; i++)
        if (s_pw[i].live && s_pw[i].job.wnd == wnd) return &s_pw[i].job;
    return NULL;
}

static ps2_pw_job_t *ps2_pw_slot_new(const void *wnd, int is_layer)
{
    uint32_t i;

    for (i = 0u; i < PS2_PW_MAX; i++) {
        if (s_pw[i].live) continue;
        s_pw[i].job.wnd          = wnd;
        s_pw[i].job.est_us       = 0u;
        s_pw[i].job.last_us      = 0u;
        s_pw[i].job.paints       = 0u;
        s_pw[i].job.us_total     = 0u;
        s_pw[i].job.defers       = 0u;
        s_pw[i].job.forceds      = 0u;
        s_pw[i].job.last_done_us = 0u;
        s_pw[i].job.layer        = is_layer ? 1u : 0u;
        s_pw[i].job.seen         = 0u;
        s_pw[i].live             = 1;
        s_st.jobs++;
        return &s_pw[i].job;
    }
    return NULL;
}

void ps2_pw_reset(uint32_t starve_us)
{
    const ps2_pw_stats_t zero = { 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u };
    uint32_t i;

    for (i = 0u; i < PS2_PW_MAX; i++) s_pw[i].live = 0;
    s_st = zero;
    s_taken = zero;
    s_slice_left_us = 0u;
    s_starve_us = starve_us ? starve_us : PS2_PW_STARVE_DEFAULT;
}

void ps2_pw_pass_begin(uint32_t slice_us, uint32_t now_us)
{
    uint32_t i;

    (void)now_us;                        /* the slice is the only thing set here */
    s_slice_left_us = slice_us;
    s_st.slice_us = slice_us;
    s_st.slice_left_us = slice_us;
    for (i = 0u; i < PS2_PW_MAX; i++)
        if (s_pw[i].live) s_pw[i].job.seen = 0u;
}

int ps2_pw_ask(const void *wnd, int is_layer, uint32_t now_us)
{
    ps2_pw_job_t *j;
    uint32_t age;

    s_st.asks++;
    /* A window with no identity is a caller error, and the answer it gets must be the one the caller
     * already handles -- a refusal it paints around -- rather than a slot nobody can find again. */
    if (!wnd) { s_st.no_jobs++; return PS2_PW_NOJOB; }
    j = ps2_pw_slot(wnd);
    if (!j) {
        j = ps2_pw_slot_new(wnd, is_layer);
        if (!j) { s_st.no_jobs++; return PS2_PW_NOJOB; }
        /* A window's first sight has no price.  Painting it is not a policy concession: the
         * alternative is a frame that shows a window nobody has ever drawn, and the price stays
         * unknown forever. */
        j->seen = 1u;
        s_st.paints++;
        return PS2_PW_PAINT;
    }
    if (j->seen) { s_st.dups++; return PS2_PW_SEEN; }

    /* The age rule before the price rule, so a starved job is never refused twice in a row and
     * `forceds` counts the override rather than hiding it. */
    age = j->paints ? (uint32_t)(now_us - j->last_done_us) : 0u;
    if (j->paints && age >= s_starve_us) {
        j->seen = 1u;
        j->forceds++;
        s_st.forceds++;
        s_st.paints++;
        return PS2_PW_PAINT;
    }

    if (j->est_us <= s_slice_left_us) {
        s_slice_left_us -= j->est_us;
        j->seen = 1u;
        s_st.paints++;
        return PS2_PW_PAINT;
    }

    j->defers++;
    s_st.defers++;
    return PS2_PW_DEFER;
}

void ps2_pw_done(const void *wnd, uint32_t us, uint32_t now_us)
{
    ps2_pw_job_t *j = ps2_pw_slot(wnd);
    uint32_t reserved;

    if (!j) return;
    /* The slice was drawn down by the ESTIMATE at admission, because that is what the decision was
     * made on.  Here the estimate is trued up against what the paint actually cost, so the number
     * the row prints as `left=` is the frame's real remaining headroom and not a double charge:
     * overspend is subtracted, underspend handed back, and a first-sight job (reserved 0) pays its
     * whole measured cost, which is the price of being the window nobody had measured yet. */
    reserved = j->est_us;
    j->last_us = us;
    j->us_total += us;
    j->paints++;
    j->last_done_us = now_us;
    /* The first sample IS the estimate; after that, three parts memory and one part measurement.
     * An unweighted mean of the same history would let a single 300 ms re-bake hide forty ordinary
     * frames, which is the opposite of what a WCET estimate is for. */
    j->est_us = j->est_us ? ((3u * j->est_us + us + 2u) >> 2) : us;
    s_st.paint_us += us;
    if (us > s_st.longest_us) s_st.longest_us = us;
    if (us >= reserved) {
        const uint32_t over = us - reserved;
        s_slice_left_us = over < s_slice_left_us ? s_slice_left_us - over : 0u;
    } else {
        s_slice_left_us += reserved - us;
    }
    s_st.slice_left_us = s_slice_left_us;
}

void ps2_pw_unmark(const void *wnd)
{
    ps2_pw_job_t *j = ps2_pw_slot(wnd);

    if (!j) return;
    j->seen = 0u;
}

void ps2_pw_credit(uint32_t avoided)
{
    s_st.credit += avoided;
}

uint32_t ps2_pw_starve_us(void)
{
    return s_starve_us;
}

const ps2_pw_job_t *ps2_pw_find(const void *wnd)
{
    return ps2_pw_slot(wnd);
}

int ps2_pw_job_at(uint32_t i, ps2_pw_job_t *out)
{
    uint32_t seen = 0u;
    uint32_t k;

    if (!out) return 0;
    for (k = 0u; k < PS2_PW_MAX; k++) {
        if (!s_pw[k].live) continue;
        if (seen == i) { *out = s_pw[k].job; return 1; }
        seen++;
    }
    return 0;
}

void ps2_pw_stats(ps2_pw_stats_t *out)
{
    if (out) *out = s_st;
}

/* Differenced against what was last handed out, so a caller that asks every hundredth pass gets
 * "what did these hundred passes do" rather than a number that only ever grows.  `slice_us`,
 * `slice_left_us` and `jobs` describe this instant and are passed through, because a budget's
 * headroom means nothing as a delta. */
void ps2_pw_stats_take(ps2_pw_stats_t *out)
{
    ps2_pw_stats_t now = s_st;
    ps2_pw_stats_t d;

    if (!out) return;
    d.asks          = now.asks          - s_taken.asks;
    d.paints        = now.paints        - s_taken.paints;
    d.defers        = now.defers        - s_taken.defers;
    d.forceds       = now.forceds       - s_taken.forceds;
    d.dups          = now.dups          - s_taken.dups;
    d.no_jobs       = now.no_jobs       - s_taken.no_jobs;
    d.paint_us      = now.paint_us      - s_taken.paint_us;
    d.credit        = now.credit        - s_taken.credit;
    d.longest_us    = now.longest_us;          /* a maximum: reported, not differenced */
    d.slice_us      = now.slice_us;
    d.slice_left_us = now.slice_left_us;
    d.jobs          = now.jobs;
    s_taken         = now;
    s_st.longest_us = 0u;                       /* one profiling window's worst paint */
    *out = d;
}
