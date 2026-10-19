/*
 * ps2_paint_worker.h — who paints this frame, and how much of it they are allowed to take
 *
 * LANE 22 of the plan of record (task #165, "Offload painting to per-app worker tasks").  It exists
 * because of one fact about the compositor that the band work made visible: `ps2_paint_region()` is
 * called once per band, and every one of those calls brackets its own composite, which re-runs the
 * paint callback of EVERY stale window that band touches.  A pass that presents two bands over the
 * XMB therefore asks the XMB to draw its frame twice, inside the presentation plane's run, at a price
 * nobody on the machine knows.  The `comp=` column of [XMBT] measured the consequence: 55 ms of every
 * XMB frame was repainting windows that were already valid.
 *
 * What "offload" can and cannot mean here, stated plainly because the wrong reading of it would be a
 * lie.  The Emotion Engine has ONE main core; `sta_tsk()` runs a body on the caller's stack and
 * `slp_tsk()` is a no-op (see the task layer in src/cores/core_ps2.c), so there is no thread to move
 * work onto and no parallelism to claim.  Three things ARE real, and this file is the policy for all
 * three:
 *
 *   - ONCE PER PASS.  A window's callback runs at most one time per pass, however many bands reach
 *     it.  The saving is countable, and `credit()` is what counts it.
 *   - A PRICE PER WINDOW.  Each job carries the exponentially-weighted mean of what its last paints
 *     actually cost, measured by the caller and folded back here.  Without a price there is nothing to
 *     schedule against; with one, "is there room in this frame" is a question that can be answered.
 *   - A BUDGET PER PASS.  A job is admitted only while the sum of the estimates still fits the
 *     frame's remaining slice, which is the port's own `ui_period_us` minus what the pass has already
 *     spent and minus what its present is expected to cost.  That is the WCET rule of
 *     include/btron/async_rt.h applied to the producer rather than to the plane.
 *
 * The fourth term is the one that makes this safe to turn on: a job that is refused too often is
 * admitted anyway.  `starve_us` is how long a window may be left unrepainted before price stops
 * mattering, and the admission that overrode the price is counted as `forced` rather than hidden --
 * a policy that silently starves the XMB would read as a smooth desktop and a frozen application.
 *
 * This file decides and counts.  It calls nothing, reads no register, and knows only a window's
 * address as an identity, which is what lets verify/tests/test_ps2_paint_worker.c drive the whole
 * law -- fits, refusals, starvation overrides, the per-pass once, and the price model -- on a machine
 * with no PS2 in it.  The caller (src/cores/core_ps2.c) owns the clocks, the paint callbacks, the
 * eegs brackets and the composite bypass; the seam is `ask()` returning an answer, never a side
 * effect.
 *
 * Nothing here is compiled into the shipped image unless the port asks for it: the core's use of this
 * file is guarded by BTRON_PS2_PAINT_WORKER (Makefile `PS2_PW=1`, default 0), and the module itself
 * is a leaf -- an unreferenced object costs .text nobody reads.
 *
 * What this lane DID have to change outside the PS2 port is one predicate in the shared compositor
 * (src/window/wnd.c `wnd_owes_callback()`), so the claim "the shared path is untouched" is not the
 * true one.  The true claim is that it is a no-op everywhere else: the freeze is raised only by the
 * PS2 paint pass, so on every other target the predicate reduces, term by term, to the `!img_valid`
 * answer it has always given -- and the Pi 400, posix, uefi and pc98 all still build.  Without that
 * predicate this policy measured as doing nothing at all (see below), which is why it is here.
 *
 * MEASURED, 2026-10-18, `BENCH_APP=1 PS2_PW=1 BENCH_LAUNCH_FRAMES=40 scripts/ps2_bench.sh`:
 * the column moves.  A pass that refused the app reads `comp paint=0` against `comp paint=23,848`
 * on one that admitted it, and its whole composite costs 30.2-32.0 ms against 51.1 ms; pass period
 * falls from ~75 ms to 46.8 ms, so the pointer's clock, which is the pass and not the application,
 * runs ~1.6x faster.  Steady state: `paint=2 defer=8` per 10 passes, 25 paints over 132 passes.
 *
 * The honest half of that row is `forced=24` of the 25 paints.  The XMB's own price settles at
 * `est=50,744` us -- ~48.5 ms measured inside the composite brackets, of which 23.8 ms is the
 * callback column -- against a slice of 15,600 us, so the frame it wants is 3.1x the frame it is
 * allowed and the price rule never admits it.  Every real admission is the starvation override
 * (`PS2_PW_STARVE_DEFAULT` = 200,000 us) deciding, which sets the app at ~4-5 Hz and makes the pass
 * fast.  That is the whole of what a single-core EE can do here: the dial trades the application's
 * frame rate for the desktop's and the pointer's, it does not create capacity.  The first paint of a
 * session still overshoots (pass 1: slice 16,636, spent 51,129, `left=0`) because an unpriced job is
 * admitted on sight -- the slice is a schedule, not a cap.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_PAINT_WORKER_H
#define PS2_PAINT_WORKER_H

#include <stdint.h>

/* Windows the table can hold a price for.  Sized from the pool that opens them: PS2_MAX_TASKS is 4
 * apps, and the desktop's own paintable windows that reach a pass' damage are the panel and at most
 * a couple of app windows, so 8 is "every window this machine can have up, plus slack".  A job that
 * does not fit is refused and counted (`no_slot`), which is the honest answer -- a full table means
 * the compositor paints that window as it always did, not that a frame is lost.  Overridable, in the
 * shape ps2_gif_ring.h sets for the same reason: the host gate needs to reach capacity to prove the
 * refusal path exists at all. */
#ifndef PS2_PW_MAX
#define PS2_PW_MAX 8u
#endif

/* What `ps2_pw_ask()` answers.  PAINT means "run this window's callback now, and tell me what it
 * cost"; DEFER means "this pass cannot afford it, and it is still owed"; SEEN means it already
 * painted this pass, which is the redundancy this file exists to stop. */
#define PS2_PW_PAINT 1
#define PS2_PW_DEFER 0
#define PS2_PW_SEEN  2
#define PS2_PW_NOJOB (-1)     /* the table is full: the caller paints as before */

typedef struct {
    const void *wnd;         /* identity only; never dereferenced here */
    uint32_t est_us;         /* the price: EWMA of this job's measured paints, 0 = never painted */
    uint32_t last_us;        /* the most recent single paint, as measured */
    uint32_t paints;         /* completed paints */
    uint32_t us_total;       /* µs spent inside its callbacks */
    uint32_t defers;         /* refusals this job got */
    uint32_t forceds;        /* admissions that overrode a too-expensive price */
    uint32_t last_done_us;   /* the caller's clock at its last completion, for the age rule */
    unsigned layer : 1;      /* its picture is a GS stream, not a pixmap */
    unsigned seen  : 1;      /* it painted already in the pass now running */
} ps2_pw_job_t;

typedef struct {
    uint32_t asks;           /* candidate windows put in front of the policy */
    uint32_t paints;         /* PS2_PW_PAINT answers, forced ones included */
    uint32_t defers;         /* PS2_PW_DEFER answers */
    uint32_t forceds;        /* paints admitted because the job had waited past starve_us */
    uint32_t dups;           /* PS2_PW_SEEN: the second ask for a window this pass */
    uint32_t no_jobs;        /* the table was full */
    uint32_t paint_us;       /* µs the admitted paints cost, summed */
    uint32_t longest_us;     /* the worst single paint seen -- a pass' tail is this number */
    uint32_t credit;         /* composite re-records this policy made unnecessary */
    uint32_t slice_us;       /* the slice the last pass began with: what the frame allowed */
    uint32_t slice_left_us;  /* what remained when the pass ended -- the headroom, if any */
    uint32_t jobs;           /* live table entries */
} ps2_pw_stats_t;

/* Zero the table and the counters, and set the starvation bound.  Called at desktop entry, because a
 * price learned from a previous session's window is a price about a freed object. */
void ps2_pw_reset(uint32_t starve_us);

/* The one frame's worth of arithmetic that has to be re-set every pass: how much of the frame is
 * still available for painting, and what the clock says now.  `now_us` is the caller's monotonic µs
 * clock and is only ever used in differences, so it may wrap. */
void ps2_pw_pass_begin(uint32_t slice_us, uint32_t now_us);

/* The policy's whole question, in one call: price this window (creating its job on first sight, with
 * `is_layer` said once and remembered), and answer PAINT, DEFER, SEEN or NOJOB.
 *
 * The order of the four reasons is the law, and it is why a first sight is always painted: a job with
 * no price is a window nobody has ever measured, and refusing it would show nothing at all. */
int ps2_pw_ask(const void *wnd, int is_layer, uint32_t now_us);

/* A paint of `us` microseconds finished, at `now_us`.  Folds the price -- the first sample becomes
 * the estimate, every later one is (3*old + new)/4 -- and clears nothing: the job stays SEEN so a
 * second ask in the same pass is refused. */
void ps2_pw_done(const void *wnd, uint32_t us, uint32_t now_us);

/* The caller chose not to paint a job it had been told to paint (its device was gone, its rect moved
 * out of the band between ask and run).  Un-marks it, so the window is owed again rather than left
 * standing as "already painted" for the rest of the pass. */
void ps2_pw_unmark(const void *wnd);

/* How many composite re-records this pass made unnecessary, for `credit`.  The caller knows how many
 * bands reached the window; the policy knows nothing about bands. */
void ps2_pw_credit(uint32_t avoided);

/* The starvation bound currently in force, so the row can name the number an admission overrode
 * rather than leave the reader to know it. */
uint32_t ps2_pw_starve_us(void);

/* Table reads, for the rows.  `ps2_pw_job_at()` copies entry `i` out and returns 0 when `i` is past
 * the live jobs, so the core can format a name against a pointer it owns. */
const ps2_pw_job_t *ps2_pw_find(const void *wnd);
int ps2_pw_job_at(uint32_t i, ps2_pw_job_t *out);

void ps2_pw_stats(ps2_pw_stats_t *out);
void ps2_pw_stats_take(ps2_pw_stats_t *out);   /* one profiling window, maxima included */

#endif /* PS2_PAINT_WORKER_H */
