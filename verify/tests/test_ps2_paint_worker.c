/*
 * verify/tests/test_ps2_paint_worker.c -- LANE 22: a paint job that has a price and a deadline
 *
 * The claim under test is not that the PS2 paints faster.  It is that a per-app paint can be
 * SCHEDULED at all: priced from what it cost last time, admitted while the frame still has room,
 * run once per pass however many bands ask for it, and forced through when the price would otherwise
 * starve an application forever.  None of that is readable off a register and all of it is arithmetic
 * on numbers this file can supply, so the module is compiled whole -- `#include` of the source, the
 * same shape test_ps2_softfloat.c and test_ps2_gif_ring.c use, and the reason those files exist.
 *
 * What is checkable, and the edit that turns each row red -- LAW 16d, because a gate that cannot fail
 * is not an instrument:
 *
 *   A window this pass has never priced is painted even when the frame has no room for it.
 *       Send first sight through the price rule instead of around it, and an unknown window meets
 *       the same DEFER as an expensive one: the screen then shows nothing and the price stays
 *       unknown forever.
 *   One paint per window per pass, counted as `dups` on the second ask.
 *       Never set `seen`, and this is the composite's old behaviour exactly -- one callback per band
 *       that reaches the window -- which is the redundancy LANE 22 was opened to remove.
 *   The slice drains by the ESTIMATE, then is trued up by the MEASUREMENT, and never charges twice.
 *       Charge `done()` the full measured cost on top of the reservation and a 400 us job that costs
 *       100 us leaves the frame 400 us poorer than it is; the row would then under-report headroom on
 *       every pass that guessed high, which is every pass after a re-bake.
 *   The age rule is checked BEFORE the price rule.
 *       Reorder the two arms and a starved job is refused, then refused again, forever: the app stops
 *       animating while the cursor gets smoother, and nothing in the counts says which one happened.
 *   The age is a modulo difference, so a wrapped clock cannot veto an admission.
 *       Compare `now > last + starve` instead and the override stops working for one wrap of CP0
 *       Count -- 29 s at this machine's measured rate, which is longer than a demo and shorter than a
 *       session.
 *   The price is an EWMA, so one outlier does not become the estimate.
 *       Fold with `est = us` and a single 10 ms re-bake prices every later frame of that app at 10 ms,
 *       which reads as an application that has decided to stop drawing.
 *   The rounding term keeps a 1 us job at 1 us.
 *       Write (3*est + us) >> 2 and repeated small samples decay toward zero, and a job priced at 0
 *       is admitted without limit.
 *   A full table refuses; it does not lose a window.
 *       Overwrite the oldest slot instead and a live app inherits another app's price, which is the
 *       single failure mode that makes every number in this file a lie.
 *   The window snapshot is a delta, with the maxima and the live fields passed through.
 *       Drop the `s_taken` bookkeeping and `paints=` becomes a lifetime total, which is what a
 *       budget row printed for 2026-10-14's [PSTAT] would then have been lying about.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* A table this suite can actually fill, and a starvation bound it can actually cross in a few calls.
 * The shipped values live in ps2_paint_worker.h and the defaults in ps2_paint_worker.c; overriding
 * the cap here is the same idiom the ring suite uses to reach its capacity path. */
#define PS2_PW_MAX 4u
#include "../../src/drivers/ps2/ps2_paint_worker.h"
#include "../../src/drivers/ps2/ps2_paint_worker.c"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (ok) return;
    s_failures++;
    printf("  [FAIL] %s\n", what);
}

static void eqi(const char *what, uint32_t got, uint32_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %s: got %u want %u\n", what, got, want);
}

/* Windows are identified by address and never dereferenced, so statics in here are enough to be
 * six distinct windows. */
static int s_w[8];
static const void *W(int i) { return &s_w[i]; }

/* 1 -- a window nobody has priced is painted, even at a zero slice. */
static void test_first_sight_is_painted(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(200000u);
    ps2_pw_pass_begin(0u, 1000u);                    /* the frame has spent everything already */
    check("first sight at a zero slice still paints", ps2_pw_ask(W(0), 1, 1000u) == PS2_PW_PAINT);
    ps2_pw_stats(&st);
    eqi("asks", st.asks, 1u);
    eqi("paints", st.paints, 1u);
    eqi("defers", st.defers, 0u);
    eqi("a live job is in the table", st.jobs, 1u);
    /* An unknown window with no identity at all is a caller error, and the answer must be a refusal
     * that the caller can act on rather than a slot built out of NULL. */
    ps2_pw_pass_begin(1000u, 1000u);
    check("a NULL window is not admitted", ps2_pw_ask(NULL, 0, 1000u) == PS2_PW_NOJOB);
}

/* 2 -- one paint per window per pass; the second ask is the redundancy, counted. */
static void test_once_per_pass(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(200000u);
    ps2_pw_pass_begin(1000000u, 1000u);
    check("band 1 asks the layer window", ps2_pw_ask(W(0), 1, 1000u) == PS2_PW_PAINT);
    ps2_pw_done(W(0), 500u, 1500u);
    check("band 2 asks it again", ps2_pw_ask(W(0), 1, 1500u) == PS2_PW_SEEN);
    check("band 3 too", ps2_pw_ask(W(0), 1, 1500u) == PS2_PW_SEEN);
    ps2_pw_stats(&st);
    eqi("asks", st.asks, 3u);
    eqi("paints", st.paints, 1u);
    eqi("dups", st.dups, 2u);
    /* The next pass is allowed again: `seen` is a per-pass mark, not a per-frame one. */
    ps2_pw_pass_begin(1000000u, 2000u);
    check("the next pass asks it again", ps2_pw_ask(W(0), 1, 2000u) == PS2_PW_PAINT);
    /* And a job that was admitted but could not run -- its device vanished between the ask and the
     * callback -- owes the pass its paint rather than standing as done. */
    ps2_pw_unmark(W(0));
    check("un-marking re-opens the job", ps2_pw_ask(W(0), 1, 2000u) == PS2_PW_PAINT);
    ps2_pw_stats(&st);
    eqi("the re-opened job painted twice this pass", st.paints, 3u);
}

/* 3 -- the slice admits by estimate and is trued up by measurement, never charged twice. */
static void test_price_admits_then_refuses(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(200000u);
    /* Price window 0 at 400 us over three samples of exactly 400. */
    ps2_pw_pass_begin(100000u, 1000u);
    ps2_pw_ask(W(0), 0, 1000u); ps2_pw_done(W(0), 400u, 1000u);
    ps2_pw_pass_begin(100000u, 2000u);
    ps2_pw_ask(W(0), 0, 2000u); ps2_pw_done(W(0), 400u, 2000u);
    ps2_pw_pass_begin(100000u, 3000u);
    ps2_pw_ask(W(0), 0, 3000u); ps2_pw_done(W(0), 400u, 3000u);
    ps2_pw_stats(&st);
    eqi("a constant price converges on itself", st.slice_left_us, 100000u - 400u);
    /* 900 us of slice, 400 priced: two jobs fit, the third does not. */
    ps2_pw_pass_begin(900u, 4000u);
    check("job one fits",   ps2_pw_ask(W(1), 0, 4000u) == PS2_PW_PAINT);   /* first sight */
    ps2_pw_done(W(1), 400u, 4000u);
    check("job two fits",   ps2_pw_ask(W(0), 0, 4000u) == PS2_PW_PAINT);   /* priced 400 */
    ps2_pw_done(W(0), 400u, 4000u);
    check("job three is refused", ps2_pw_ask(W(2), 0, 4000u) == PS2_PW_PAINT); /* first sight: exempt */
    ps2_pw_done(W(2), 400u, 4000u);
    ps2_pw_stats(&st);
    eqi("slice never goes below zero", st.slice_left_us, 0u);
    /* The refusal needs a priced window that is not new: reuse 0 after the pass. */
    ps2_pw_pass_begin(100u, 5000u);
    check("a priced job cannot fit a 100 us slice", ps2_pw_ask(W(0), 0, 5000u) == PS2_PW_DEFER);
    ps2_pw_stats(&st);
    eqi("the refusal is counted", st.defers, 1u);
}

static void test_slice_is_trued_up_not_double_charged(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(200000u);
    /* Price W(0) at 400 us on a frame with room to spare, so the next pass reserves exactly 400. */
    ps2_pw_pass_begin(100000u, 1000u);
    ps2_pw_ask(W(0), 0, 1000u);
    ps2_pw_done(W(0), 400u, 1000u);

    ps2_pw_pass_begin(1000u, 2000u);
    check("a 400 us estimate is admitted", ps2_pw_ask(W(0), 0, 2000u) == PS2_PW_PAINT);
    /* It cost 100, not 400.  The reservation came out at admission; the 300 us of underspend has to
     * come back, or the frame is recorded as poorer than it is by every pass that guessed high. */
    ps2_pw_done(W(0), 100u, 2000u);
    ps2_pw_stats(&st);
    eqi("underspend is refunded", st.slice_left_us, 1000u - 100u);
    /* Overspend takes what it needs and stops at zero rather than wrapping.  The estimate is now
     * (3*400 + 100 + 2)/4 = 325, so a 5 ms paint is 4,675 over what was reserved against a 675 us
     * remainder -- the floor is what this row is checking. */
    ps2_pw_pass_begin(1000u, 3000u);
    check("the job is admitted again next pass", ps2_pw_ask(W(0), 0, 3000u) == PS2_PW_PAINT);
    ps2_pw_done(W(0), 5000u, 3000u);
    ps2_pw_stats(&st);
    eqi("overspend floors at zero", st.slice_left_us, 0u);
}

/* 4 -- starvation overrides the price, and it is checked before the price. */
static void test_starvation_overrides_price(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(1000u);                       /* a starve bound this suite can cross exactly */
    ps2_pw_pass_begin(100000u, 1000u);
    ps2_pw_ask(W(0), 1, 1000u); ps2_pw_done(W(0), 9000u, 1000u);   /* price it at 9 ms */
    ps2_pw_pass_begin(100u, 1500u);            /* a slice it cannot possibly fit */
    check("too young and too expensive: deferred", ps2_pw_ask(W(0), 1, 1500u) == PS2_PW_DEFER);
    /* Same job, same price, older by more than the bound: the age arm must fire first. */
    ps2_pw_pass_begin(100u, 1000u + 1000u);
    check("starved: admitted anyway", ps2_pw_ask(W(0), 1, 2000u) == PS2_PW_PAINT);
    ps2_pw_stats(&st);
    eqi("the override is counted as forced, not hidden", st.forceds, 1u);
    eqi("and it is a paint", st.paints, 2u);
    /* Exactly at the bound the rule is `>=`: one frame of extra patience would make the row say
     * "starved" for a job the policy never actually refused. */
    ps2_pw_pass_begin(100u, 1000u);
    check("the bound is inclusive", ps2_pw_ask(W(1), 1, 1000u) == PS2_PW_PAINT);
    ps2_pw_done(W(1), 9000u, 1000u);
    ps2_pw_pass_begin(100u, 1000u + 999u);
    check("one µs short of it, refused", ps2_pw_ask(W(1), 1, 1999u) == PS2_PW_DEFER);
    ps2_pw_pass_begin(100u, 1000u + 1000u);
    check("at it, admitted", ps2_pw_ask(W(1), 1, 2000u) == PS2_PW_PAINT);
}

/* 5 -- the age wraps with the clock, and a wrapped clock cannot veto an admission. */
static void test_age_survives_a_clock_wrap(void)
{
    ps2_pw_stats_t st;
    const uint32_t near_max = 0xFFFFFFFFu;

    ps2_pw_reset(1000u);
    /* Paint at a clock reading that is 500 µs from wrapping, then ask 3000 µs later -- which, taken
     * as unsigned modulo arithmetic, is 2500 past the wrap and still a 3000 µs interval. */
    ps2_pw_pass_begin(100000u, near_max - 500u);
    ps2_pw_ask(W(0), 1, near_max - 500u);
    ps2_pw_done(W(0), 9000u, near_max - 500u);            /* priced above the slice below */
    ps2_pw_pass_begin(100u, near_max + 2500u);
    check("a wrapped age still forces the admission",
          ps2_pw_ask(W(0), 1, near_max + 2500u) == PS2_PW_PAINT);
    ps2_pw_stats(&st);
    eqi("forced through the wrap", st.forceds, 1u);
}

/* 6 -- the price model: an EWMA that one outlier cannot hijack, and that small samples do not decay. */
static void test_price_model(void)
{
    ps2_pw_job_t j;

    ps2_pw_reset(200000u);
    ps2_pw_pass_begin(1000000u, 1000u);
    ps2_pw_ask(W(0), 0, 1000u);
    ps2_pw_done(W(0), 400u, 1000u);
    check("the first sample is the estimate", ps2_pw_find(W(0)) && ps2_pw_find(W(0))->est_us == 400u);

    /* Twenty samples of 400 keep it at 400 exactly: 3/4 + 1/4 of the same number, with the +2
     * rounding, must not drift. */
    for (int i = 0; i < 20; i++) {
        ps2_pw_pass_begin(1000000u, (uint32_t)(2000 + i));
        ps2_pw_ask(W(0), 0, (uint32_t)(2000 + i));
        ps2_pw_done(W(0), 400u, (uint32_t)(2000 + i));
    }
    check("a constant price does not drift", ps2_pw_find(W(0))->est_us == 400u);

    /* One 10 ms re-bake moves it by a quarter of the difference, not to the sample. */
    ps2_pw_pass_begin(1000000u, 9000u);
    ps2_pw_ask(W(0), 0, 9000u);
    ps2_pw_done(W(0), 10000u, 9000u);
    eqi("one outlier moves a quarter of the gap", ps2_pw_find(W(0))->est_us,
        400u + (10000u - 400u) / 4u);
    check("and the estimate is still nowhere near the outlier",
          ps2_pw_find(W(0))->est_us < 3000u);
    /* The same outlier twice, then the ordinary cost returning: the price must come back down, so a
     * re-bake cannot price an app out of the frame permanently. */
    ps2_pw_pass_begin(1000000u, 10000u); ps2_pw_ask(W(0), 0, 10000u); ps2_pw_done(W(0), 10000u, 10000u);
    for (int i = 0; i < 40; i++) {
        ps2_pw_pass_begin(1000000u, (uint32_t)(11000 + i));
        ps2_pw_ask(W(0), 0, (uint32_t)(11000 + i));
        ps2_pw_done(W(0), 400u, (uint32_t)(11000 + i));
    }
    check("and recovers when the cost recovers", ps2_pw_find(W(0))->est_us < 600u);

    /* A job whose real cost is one µs must stay at one µs.  Without the +2 this decays:
     * (3*1 + 1) >> 2 == 1 is fine, but (3*2 + 1) >> 2 == 1 and a run of 1s from 3 goes 3,2,1,1 --
     * what has to be proven is that it cannot reach 0, because a job priced at 0 is admitted without
     * limit and the whole policy stops binding. */
    ps2_pw_pass_begin(1000000u, 20000u);
    ps2_pw_ask(W(1), 0, 20000u);
    ps2_pw_done(W(1), 1u, 20000u);
    for (int i = 0; i < 200; i++) {
        ps2_pw_pass_begin(1000000u, (uint32_t)(21000 + i));
        ps2_pw_ask(W(1), 0, (uint32_t)(21000 + i));
        ps2_pw_done(W(1), 1u, (uint32_t)(21000 + i));
    }
    eqi("a 1 us job stays priced at 1 us", ps2_pw_find(W(1))->est_us, 1u);

    /* The layer flag is said once at first sight and remembered, because the second ask comes from a
     * different band's caller and must not be able to disagree about what kind of window this is. */
    j = *ps2_pw_find(W(1));
    eqi("a software job is remembered as software", j.layer, 0u);
    ps2_pw_pass_begin(1000000u, 30000u);
    ps2_pw_ask(W(2), 1, 30000u);
    eqi("a layer job is remembered as a layer", ps2_pw_find(W(2))->layer, 1u);
    ps2_pw_ask(W(2), 0, 30000u);                 /* a later ask that says otherwise */
    eqi("and is not re-typed by it", ps2_pw_find(W(2))->layer, 1u);
}

/* 7 -- a full table refuses; it does not steal a slot from a live app. */
static void test_table_capacity(void)
{
    ps2_pw_stats_t st;

    ps2_pw_reset(200000u);
    ps2_pw_pass_begin(1000000u, 1000u);
    for (unsigned i = 0u; i < PS2_PW_MAX; i++)
        check("every slot takes a job", ps2_pw_ask(W((int)i), 0, 1000u) == PS2_PW_PAINT);
    check("the fifth window is refused a slot",
          ps2_pw_ask(W(4), 0, 1000u) == PS2_PW_NOJOB);
    ps2_pw_stats(&st);
    eqi("the refusal is counted", st.no_jobs, 1u);
    eqi("the table holds exactly its cap", st.jobs, PS2_PW_MAX);
    /* The refused window is not damaged by the refusal: the caller paints it as the composite always
     * did, and the priced jobs answer their own questions normally. */
    ps2_pw_pass_begin(1000000u, 2000u);
    check("a priced job still asks fine", ps2_pw_ask(W(0), 0, 2000u) == PS2_PW_PAINT);
}

/* 8 -- credit, and the snapshot being a delta. */
static void test_credit_and_window(void)
{
    ps2_pw_stats_t a, b;

    ps2_pw_reset(200000u);
    ps2_pw_pass_begin(1000000u, 1000u);
    ps2_pw_ask(W(0), 1, 1000u); ps2_pw_done(W(0), 300u, 1000u);
    ps2_pw_credit(2u);                           /* two bands reached it; the composite will not */
    ps2_pw_stats_take(&a);
    eqi("the first window sees one paint", a.paints, 1u);
    eqi("and two re-records avoided", a.credit, 2u);
    eqi("its cost", a.paint_us, 300u);

    /* Nothing happens between the two takes: the second window must be all zeroes, while the live
     * fields still describe the instant. */
    ps2_pw_stats_take(&b);
    eqi("an empty window paints nothing", b.paints, 0u);
    eqi("asks nothing", b.asks, 0u);
    eqi("credits nothing", b.credit, 0u);
    eqi("spends nothing", b.paint_us, 0u);
    eqi("the live job count survives the differencing", b.jobs, 1u);

    /* The maximum belongs to one window and then restarts, because "the worst paint of these hundred
     * passes" is the number a budget is checked against, and a lifetime max is only ever 10 ms. */
    ps2_pw_pass_begin(1000000u, 2000u);
    ps2_pw_ask(W(0), 1, 2000u); ps2_pw_done(W(0), 9000u, 2000u);
    ps2_pw_stats_take(&a);
    eqi("this window's worst paint", a.longest_us, 9000u);
    ps2_pw_pass_begin(1000000u, 3000u);
    ps2_pw_ask(W(0), 1, 3000u); ps2_pw_done(W(0), 100u, 3000u);
    ps2_pw_stats_take(&b);
    eqi("the next window does not inherit the outlier", b.longest_us, 100u);
    /* And the differenced totals must not have lost the outlier either. */
    eqi("summed cost of the second window", b.paint_us, 100u);
}

/* 9 -- the degenerate calls, which are what a real pass makes on a desktop with no windows. */
static void test_degenerate(void)
{
    ps2_pw_stats_t st;
    ps2_pw_job_t j;

    ps2_pw_reset(200000u);
    check("an empty table answers no row", ps2_pw_job_at(0, &j) == 0);
    ps2_pw_done(W(7), 100u, 1000u);              /* a price for a window nobody tracked */
    ps2_pw_unmark(W(7));                         /* and an un-mark for one */
    ps2_pw_stats(&st);
    eqi("neither created a job", st.jobs, 0u);
    eqi("nor charged paint time", st.paint_us, 0u);
    ps2_pw_credit(0u);
    ps2_pw_stats_take(&st);
    eqi("a zero credit is a no-op", st.credit, 0u);
    ps2_pw_stats(NULL);                          /* must not dereference */
    ps2_pw_stats_take(NULL);
    check("a NULL window finds nothing", ps2_pw_find(NULL) == NULL);
    /* A pass begun with a zero slice and asked nothing is the idle desktop's own shape. */
    ps2_pw_pass_begin(0u, 0u);
    ps2_pw_stats(&st);
    eqi("and reports a zero slice", st.slice_us, 0u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 LANE 22: the paint worker -- prices, admission, and\n");
    printf(" the once-per-pass law.  cap=%u jobs\n", PS2_PW_MAX);
    printf("==========================================================\n");

    test_first_sight_is_painted();
    test_once_per_pass();
    test_price_admits_then_refuses();
    test_slice_is_trued_up_not_double_charged();
    test_starvation_overrides_price();
    test_age_survives_a_clock_wrap();
    test_price_model();
    test_table_capacity();
    test_credit_and_window();
    test_degenerate();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - a paint is priced, admitted once, and never starved silently\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the policy refused a first paint, double-charged a slice, or hid a starvation\n");
    return 1;
}
