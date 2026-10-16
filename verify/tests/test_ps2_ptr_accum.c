/*
 * test_ps2_ptr_accum.c -- does the PS2 pointer's own arithmetic accumulate error?
 *
 * The complaint this suite exists to answer is that the cursor's error looks
 * cumulative: the further the hand goes, the worse the position reads.  That
 * symptom has exactly one arithmetic cause -- a distance that is spent short of
 * what the counts asked for, and short by a quantity that grows with the number
 * of reports -- and it has several innocent causes that merely feel the same: a
 * subpixel remainder held in one place and not another, a gain that is not the
 * parity ratio (so the offset grows with distance from the last wall, with no
 * loss anywhere), a screen border eating travel, a cap deferring it.
 *
 * So the suite does not assert "smooth" or "right".  It asserts the one property
 * that decides between those families:
 *
 *     for every gain the dial offers, the shortfall of the shipped chain, measured
 *     in 1/256ths of a pixel, is bounded by |carry| <= 255 no matter how many
 *     reports the stroke is made of -- and is exactly zero at the end of any
 *     out-and-back stroke.
 *
 * That is a bound independent of stroke length, which is the definition of "not
 * accumulating".  It is checked on the real dp_ptr_scale(), by #including the
 * translation unit core_ps2.c links, and the equality is exact rather than a
 * tolerance: sum(pixels) * 256 + carry == multiplier * sum(counts).
 *
 * Two WRONG twins of the function are compiled here as positive controls, because
 * a suite that cannot show an accumulating error is not an instrument: one throws
 * the remainder away per report, one throws it away whenever a report brings zero
 * on that axis (which is what a hand that moves one axis at a time does constantly).
 * Both are shown to lose distance in proportion to the length of the stroke -- the
 * symptom as described -- and both are shown NOT to be what ships.  Neither is in
 * any image; the file that would carry them does not contain them.
 *
 * Compiled with -DBTRON_PS2_TARGET, which is what selects the dial's table; the
 * suite reads btron_ptr_gain_fp[], so a build without the flag fails at the link
 * rather than certifying the wrong shape.
 *
 * What this suite CANNOT reach, and says so at the end instead of passing it:
 * ps2_move_mouse()'s border clamp and everything the host does to the counts above
 * the guest.  The first lives in a translation unit that needs the PS2 SDK; the
 * second is not arithmetic at all.  Both are measured, not reasoned about, by the
 * [PSTAT] `border:` row and a `ptgain` sweep on a booted image.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The code under test itself, not a copy of its arithmetic: this is the identical
 * translation unit that links into btron-ps2.elf. */
#include "../../src/graphics/dp_accel.c"

static int s_checks, s_failures;

static void eq(const char *what, long long got, long long want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-46s got=%lld want=%lld\n", what, got, want);
}

static void le(const char *what, long long got, long long bound)
{
    s_checks++;
    if (got <= bound) return;
    s_failures++;
    printf("  [FAIL] %-46s got=%lld bound=%lld\n", what, got, bound);
}

/* The other side of the same statement, used only on the controls: a control that
 * is not actually broken is worse than no control, because it certifies nothing. */
static void ge(const char *what, long long got, long long floor)
{
    s_checks++;
    if (got >= floor) return;
    s_failures++;
    printf("  [FAIL] %-46s got=%lld floor=%lld\n", what, got, floor);
}

/* ── The shipped chain, one axis ─────────────────────────────────────
 *
 * `n` reports of `per` counts each, through dp_ptr_scale() with one carry that is
 * never reset between them -- which is how the bus feeds it: ps2_usb_on_mouse()
 * calls it per report and ps2_ptr_service() spends the sum once per paint.
 * Returns the whole pixels paid out, and leaves the carry for the caller to read. */
static long long drive(int32_t per, int n, int32_t mult, int32_t *carry)
{
    long long px = 0;
    int i;

    *carry = 0;
    for (i = 0; i < n; i++)
        px += dp_ptr_scale(per, mult, carry);
    return px;
}

/* The same, out and back: n reports each way, so the counts sum to zero and the
 * exact distance is therefore zero as well. */
static long long drive_there_and_back(int32_t per, int n, int32_t mult, int32_t *carry)
{
    long long px = 0;
    int i;

    *carry = 0;
    for (i = 0; i < n; i++) px += dp_ptr_scale(per, mult, carry);
    for (i = 0; i < n; i++) px += dp_ptr_scale(-per, mult, carry);
    return px;
}

/* How many characters a label actually is, without pulling a string.h into a
 * suite whose job is one function's arithmetic. */
static long long nchars(const char *s)
{
    long long n = 0;

    while (s[n] && n < 64) n++;
    return n;
}

/* The quantity the whole suite is about, in 1/256ths of a pixel: what the counts
 * were worth minus what the cursor was given.  Zero would mean the arithmetic is
 * exact on this stroke; anything else is distance the pointer did not get, and the
 * question is always whether it grows with n. */
static long long shortfall_fp(long long px, long long counts, int32_t mult, int32_t carry)
{
    long long s = (long long)mult * counts - px * 256 - carry;
    return s < 0 ? -s : s;
}

/* ── The two wrong twins, controls only ────────────────────────────── */

/* (a) No memory: pay whole pixels per report, throw the fraction away.  This is
 * what "the remainder is dropped" looks like, and it is the shape of the reported
 * symptom: the loss is per report, so a long slide arrives shorter by a distance
 * proportional to the slide. */
static int32_t ctrl_drop_remainder(int32_t raw, int32_t mult, int32_t *carry)
{
    int32_t total = raw * mult;               /* the carry is never added back */
    int32_t px = total / 256;
    *carry = 0;
    return px;
}

/* (b) Memory that decays whenever the axis is quiet.  A hand rarely moves both
 * axes at once, so the quiet axis reports zero constantly, and a variant that
 * clears the remainder there spends a stroke at half the rate it asked for. */
static int32_t ctrl_decay_on_zero(int32_t raw, int32_t mult, int32_t *carry)
{
    int32_t total, px;

    if (raw == 0) { *carry = 0; return 0; }
    total = *carry + raw * mult;
    px = total / 256;
    *carry = total % 256;
    return px;
}

static long long drive_with(int32_t (*fn)(int32_t, int32_t, int32_t *),
                            int32_t per, int n, int32_t mult, int32_t *carry)
{
    long long px = 0;
    int i;

    *carry = 0;
    for (i = 0; i < n; i++)
        px += fn(per, mult, carry);
    return px;
}

/* ── The claims ───────────────────────────────────────────────────── */

/* 1. The bound is independent of stroke length, at every stop the dial offers. */
static void the_shortfall_does_not_grow(void)
{
    int s;

    for (s = 1; s < BTRON_PTR_GAIN_STEPS; s++) {
        const int32_t mult = btron_ptr_gain_fp[s];
        int32_t c1, c2;
        long long px1 = drive(7, 4096, mult, &c1);
        long long px2 = drive(7, 8192, mult, &c2);
        char what[64];

        snprintf(what, sizeof(what), "shortfall fp, stop %d, 4096 reports", s);
        le(what, shortfall_fp(px1, 4096LL * 7, mult, c1), 255);
        snprintf(what, sizeof(what), "shortfall fp, stop %d, 8192 reports", s);
        le(what, shortfall_fp(px2, 8192LL * 7, mult, c2), 255);
        /* Not a tolerance -- the identity the bound comes from.  Holds exactly
         * because dp_ptr_scale() writes back the remainder it did not pay. */
        snprintf(what, sizeof(what), "exact identity, stop %d", s);
        eq(what, (long long)mult * 8192LL * 7, px2 * 256 + c2);
        /* And a doubling stroke does pay out double the pixels, which is what
         * "proportional, not accumulating" looks like from the other side. */
        snprintf(what, sizeof(what), "twice the counts, stop %d", s);
        le(what, px2 - 2 * px1, 1);
        le(what, 2 * px1 - px2, 1);
    }
}

/* 2. An out-and-back stroke ends where it began, with no remainder left over. */
static void a_round_trip_nets_to_zero(void)
{
    static const int32_t amps[] = { 1, 3, 4, 7, 40, 127 };
    int s, a;

    for (s = 1; s < BTRON_PTR_GAIN_STEPS; s++) {
        for (a = 0; a < (int)(sizeof(amps) / sizeof(amps[0])); a++) {
            int32_t c;
            long long px = drive_there_and_back(amps[a], 2000,
                                                btron_ptr_gain_fp[s], &c);
            char what[64];

            snprintf(what, sizeof(what), "round trip +/-%d at stop %d (px)",
                     (int)amps[a], s);
            eq(what, px, 0);
            snprintf(what, sizeof(what), "round trip +/-%d at stop %d (carry)",
                     (int)amps[a], s);
            eq(what, c, 0);
        }
    }
}

/* 3. A quiet report is not a refund.  This is the raw==0 branch, whose whole
 * purpose is the diagonal case, and the twin that decays there is shown losing
 * half of a stroke that the shipped code spends in full. */
static void zero_reports_keep_the_remainder(void)
{
    const int32_t mult = 96;                  /* the shipped emu profile's gain */
    int32_t dense_c = 0, slow_c = 0;
    long long dense = 0, slow = 0;
    int i;

    for (i = 0; i < 2000; i++) dense += dp_ptr_scale(1, mult, &dense_c);
    for (i = 0; i < 2000; i++) {
        slow += dp_ptr_scale(1, mult, &slow_c);
        slow += dp_ptr_scale(0, mult, &slow_c);
    }
    eq("interleaved zeros pay the same pixels", slow, dense);
    eq("and end on the same remainder", slow_c, dense_c);
    /* The exactness of that particular stroke: 2000 counts at 0.375 px each is
     * 750 px with nothing left over, so a person nudging one axis at a time is
     * not, on this code, losing the nudge. */
    eq("2000 single counts at 96/256 are 750 px", dense, 750);
    eq("and no remainder to lose", dense_c, 0);

    /* The control, on the same two streams, to prove the check above is about
     * something that can go wrong.  Driven with the interleaved stream, because
     * the decay only spends distance on a report that brings zero -- and a hand
     * that moves one axis at a time brings zero on the other axis constantly. */
    slow = 0; slow_c = 0;
    for (i = 0; i < 2000; i++) {
        slow += ctrl_decay_on_zero(1, mult, &slow_c);
        slow += ctrl_decay_on_zero(0, mult, &slow_c);
    }
    eq("control: a decaying carry pays nothing for the same 2000 counts", slow, 0);
}

/* 4. The accumulating shape, priced, so the bound in case 1 means something: the
 * same stroke through the remainder-dropping twin loses distance in proportion to
 * its length. */
static void dropping_the_remainder_is_the_symptom(void)
{
    const int32_t mult = 96;
    int32_t c1, c2;
    long long px1 = drive_with(ctrl_drop_remainder, 7, 4096, mult, &c1);
    long long px2 = drive_with(ctrl_drop_remainder, 7, 8192, mult, &c2);
    long long e1 = shortfall_fp(px1, 4096LL * 7, mult, c1);
    long long e2 = shortfall_fp(px2, 8192LL * 7, mult, c2);

    /* 0.375 px per report: the error of a hand that slid twice as far is twice as
     * big, which is precisely "cumulative". */
    ge("control: the dropper does lose distance, 4096 reports", e1, 256);
    le("control: its error doubles with the stroke", e2 - 2 * e1, 1);
    ge("control: twice the stroke, twice the error", e2, 512);
    /* And the shipped chain on the same two strokes, for the contrast the suite
     * is actually making. */
    le("shipped: error at 8192 reports stays under a pixel",
       shortfall_fp(drive(7, 8192, mult, &c2), 8192LL * 7, mult, c2), 255);
}

/* 5. LAW 13d, stated as arithmetic: what a stroke is worth cannot depend on how
 * the reports were sliced into.  The same distance, delivered at twice the counts
 * per report and half the reports, pays the same pixels. */
static void the_value_is_not_a_function_of_speed(void)
{
    int s;

    for (s = 1; s < BTRON_PTR_GAIN_STEPS; s++) {
        const int32_t mult = btron_ptr_gain_fp[s];
        int32_t c1, c2;
        long long slow = drive(1, 2048, mult, &c1);    /* 2048 counts, 1 per report */
        long long fast = drive(2, 1024, mult, &c2);    /* 2048 counts, 2 per report */

        char what[64];
        snprintf(what, sizeof(what), "same counts, half the reports, stop %d", s);
        le(what, fast - slow, 1);
        snprintf(what, sizeof(what), "and not one pixel short of it, stop %d", s);
        le(what, slow - fast, 1);
    }
}

/* 6. The shipped cap makes the limiter the identity, so there is no debt in the
 * image for a stroke to chase.  This is the other way distance goes missing, and
 * the reason PS2_PTR_MAX_STEP is 0. */
static void the_shipped_cap_defers_nothing(void)
{
    int32_t defer = 0;
    int i;
    static const int32_t wants[] = { -512, -300, -8, -1, 0, 1, 8, 300, 512 };

    for (i = 0; i < (int)(sizeof(wants) / sizeof(wants[0])); i++) {
        char what[64];
        snprintf(what, sizeof(what), "max_step 0 returns the whole %d", (int)wants[i]);
        eq(what, dp_ptr_limit(wants[i], 0, &defer), wants[i]);
    }
    eq("and leaves no debt behind", defer, 0);

    /* The control: with a cap on, a flick beyond the debt bound is abandoned, and
     * the cursor is then short by exactly the difference.  Measured here so that
     * "the cap is off" is a priced statement, not a preference. */
    defer = 0;
    eq("a cap of 8 spends 8", dp_ptr_limit(512, 8, &defer), 8);
    eq("and banks at most 2 steps of it", defer, 16);
    eq("the next pass is owed 8, not the whole stroke", dp_ptr_limit(0, 8, &defer), 8);
}

/* 7. The dial is one table read by everybody: every stop indexes back to itself,
 * so a panel radio, a launcher row and `sens` cannot name one gain three ways. */
static void the_dial_has_one_table(void)
{
    int s;

    eq("auto reads as auto", btron_ptr_gain_index(0), 0);
    eq("a negative gain reads as auto", btron_ptr_gain_index(-96), 0);
    for (s = 1; s < BTRON_PTR_GAIN_STEPS; s++) {
        char what[64];
        snprintf(what, sizeof(what), "stop %d indexes back to itself", s);
        eq(what, btron_ptr_gain_index(btron_ptr_gain_fp[s]), s);
        /* The label is painted into the launcher's value column, whose budget is
         * roughly 38 glyphs at 800 px; 31 is the buffer, so check the shorter one. */
        snprintf(what, sizeof(what), "stop %d's label fits the column", s);
        le(what, nchars(btron_ptr_gain_label(s)), 24);
    }
    eq("an index past the table says so", btron_ptr_gain_label(BTRON_PTR_GAIN_STEPS)[0], 'g');
    eq("and reads as a bare word", btron_ptr_gain_label(-1)[0], 'g');
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 pointer accumulation: the shipped chain's error is bounded\n");
    printf(" per stroke, not per report -- and the twins that are not\n");
    printf("==========================================================\n");

    the_shortfall_does_not_grow();
    a_round_trip_nets_to_zero();
    zero_reports_keep_the_remainder();
    dropping_the_remainder_is_the_symptom();
    the_value_is_not_a_function_of_speed();
    the_shipped_cap_defers_nothing();
    the_dial_has_one_table();

    /* Named, not certified.  Both are real ways for the cursor to end up short of
     * the hand, and neither is arithmetic this suite can see: the first is a
     * clamp in ps2_move_mouse(), in a translation unit that needs the PS2 SDK to
     * compile; the second is above the guest entirely.  Their instruments are the
     * [PSTAT] `border:` row and a `ptgain` sweep on a booted image. */
    printf("  SKIP   border clamp (ps2_move_mouse)   -- not host-linkable; "
           "measured by [PSTAT] border:\n");
    printf("  SKIP   host gain above the guest       -- not this stack; "
           "measured by `ptgain <host px>`\n");

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - no in-guest mechanism accumulates distance\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a pointer stroke is being paid short\n");
    return 1;
}
