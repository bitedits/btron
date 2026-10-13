/*
 * test_ps2_ptr_stroke.c - does the PS2 port's stroke splitter actually tell the
 *                         two speeds of one slide apart, and can it be wrong?
 *
 * src/cores/ps2_ptr_stroke.h decides the question that ships the pointer gain:
 * whether the wire carries host distance (one guest constant is right) or somebody
 * above it has already multiplied by speed (no constant is right for both hands).
 * It is reached only from core_ps2.c, in a session whose screen the developer
 * cannot see, so a silent mis-segmentation would print a confident ratio and be
 * read as an answer.
 *
 * The instrument therefore has to be shown capable of turning red.  The first two
 * cases are a matched pair built from the same synthetic stream: counts that
 * proportion to *distance* must read 1.00x, and counts that additionally
 * proportion to *speed* -- i.e. the very thing being looked for -- must read as
 * acceleration and not as a coincidence.  Both are fed through the same
 * ps2_stroke_feed() the bus report handler calls, so nothing here is a model of
 * the code under test rather than the code itself.
 *
 * The remaining cases are the ways a splitter goes wrong in practice and that a
 * live session would blame on the mouse instead: a pause that is not a new stroke,
 * a nudge counted as a slide, two strokes that never differed in speed pretending
 * to be the experiment, the 16-bit frame counter wrapping inside a stroke, and the
 * stroke that is still under the hand when the row is printed.
 *
 * The last family is the one a real session fell into: a row that says "slide
 * further" to a wire that delivered nothing is wrong advice, so the splitter counts
 * every report it is fed, moving or not, and keeps the longest stroke it saw.  Those
 * two readings are what let the row tell an idle hand from a silent bus, and they are
 * checked as their own case below.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The header itself, not a copy of its arithmetic: the identical translation unit
 * is what core_ps2.c compiles into btron-ps2.elf. */
#include "../../src/cores/ps2_ptr_stroke.h"

static int s_checks, s_failures;

static void eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-42s got=%llu want=%llu\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

static void eq32(const char *what, uint32_t got, uint32_t want)
{
    eq(what, (uint64_t)got, (uint64_t)want);
}

static void eqi(const char *what, long got, long want)
{
    eq(what, (uint64_t)(long)got, (uint64_t)(long)want);
}

/* n reports, `per` counts each, one OHCI frame apart -- which is how the emulator
 * publishes them (motion is sampled once per emulated EE frame). */
static void feed_stream(ps2_stroke_t *s, uint32_t start_frame, uint32_t n, uint32_t per)
{
    uint32_t i;

    for (i = 0; i < n; i++)
        ps2_stroke_feed(s, (start_frame + i) & 0xFFFFu, per);
}

/* The pair, run to completion, with the verdict and its ratio. */
static int finish(ps2_stroke_t *s, uint32_t *ratio100)
{
    ps2_stroke_flush(s);
    return ps2_stroke_verdict(s, ratio100);
}

/* ── The two cases the row is for: matched pair, opposite answers ─────── */

static void wire_carries_distance(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    /* One deliberate slide of the Mac's display width, done slowly: 1500 counts,
     * one per frame, 1500 ms under the hand. */
    feed_stream(&s, 100u, 1500u, 1u);
    /* The same physical slide, quickly: the same 1500 counts (60 reports x 25),
     * 60 frames.  The 3389-frame
     * pause before it is what closes the first stroke -- no extra call needed, and
     * none wanted, because a report that opens the next sample must not also put a
     * count into it. */
    feed_stream(&s, 5000u, 60u, 25u);

    eqi("verdict: distance on the wire", finish(&s, &ratio), PS2_STROKE_WIRE);
    eq32("ratio of the two counts", ratio, 100u);
    eq32("qualifying samples", s.samples, 2u);
    eq32("slow sample kept", s.slow_counts, 1500u);
    eq32("fast sample kept", s.fast_counts, 1500u);
    /* The pair really did differ in speed, which is what makes the 1.00x mean
     * something rather than merely be arithmetic. */
    eq32("the two samples differ in speed", s.fast_ms * 2u < s.slow_ms, 1u);
}

static void wire_carries_speed(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    feed_stream(&s, 100u, 1500u, 1u);
    /* The positive control: the same slide, and the counts now come multiplied by
     * how fast the hand went -- 2.5x of them.  A splitter or a verdict that could
     * not tell this from the row above is not an instrument. */
    feed_stream(&s, 5000u, 60u, 63u);

    eqi("verdict: acceleration above the wire", finish(&s, &ratio), PS2_STROKE_ACCEL);
    eq32("ratio reads the multiplier", ratio, 63u * 60u * 100u / 1500u);
    eq32("slow sample unchanged by the fast one", s.slow_counts, 1500u);
}

/* ── The ways a splitter goes wrong, each refused by name ─────────────── */

static void no_spread_is_not_a_verdict(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 12345u;

    /* Two slides of the same length, done at almost the same speed: the counts
     * agree, and printing 1.00x here would read exactly like the answer. */
    feed_stream(&s, 0u, 400u, 3u);
    feed_stream(&s, 2000u, 300u, 4u);

    eqi("two near-identical speeds refuse", finish(&s, &ratio), PS2_STROKE_SPREAD);
    eq32("and print no ratio", ratio, 0u);
    eq32("both strokes still counted", s.samples, 2u);
}

static void short_stroke_is_not_a_sample(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    feed_stream(&s, 0u, 60u, 5u);           /* 300 counts: a nudge */
    feed_stream(&s, 2000u, 1500u, 1u);      /* 1500 counts: a slide */

    finish(&s, &ratio);
    eq32("nudge not recorded as a sample", s.samples, 1u);
    eq32("the slide is the sample", s.slow_counts, 1500u);
    eqi("one sample gives no verdict", ps2_stroke_verdict(&s, &ratio), PS2_STROKE_NONE);
    eq32("and no ratio", ratio, 0u);
}

static void a_pause_splits_but_a_flattening_does_not(void)
{
    ps2_stroke_t long_gap = { 0 }, flat = { 0 };
    uint32_t ratio = 0u;

    /* 1200 counts, then a hand that stops for half a second, then 1200 more: two
     * intentions, and the test needs them kept apart. */
    feed_stream(&long_gap, 0u, 400u, 3u);
    feed_stream(&long_gap, 900u, 400u, 3u);
    finish(&long_gap, &ratio);
    eq32("a 500 ms pause splits the stroke", long_gap.samples, 2u);

    /* The same counts with no pause: one slide whose hand merely levelled off, and
     * splitting it would turn one sample into two near-identical ones and lose the
     * spread the verdict reads. */
    feed_stream(&flat, 0u, 800u, 3u);
    finish(&flat, &ratio);
    eq32("a continuous sweep stays one stroke", flat.samples, 1u);
    eq32("and holds all its counts", flat.slow_counts, 2400u);
}

static void still_reports_are_silence(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t i, ratio = 0u;

    /* A device that is not moving still answers IN tokens with 00 00 00, and those
     * must not close a stroke -- they arrive for the whole time a pointer rests. */
    for (i = 0; i < 1200u; i++) {
        ps2_stroke_feed(&s, i & 0xFFFFu, (i % 3u) ? 0u : 2u);
    }
    finish(&s, &ratio);
    eq32("zero reports do not fragment a stroke", s.samples, 1u);
    eq32("every nonzero report counted", s.slow_counts, 800u);
    /* The quiet ones are counted too, and that is what the silent-wire reading hangs
     * on: a still IN token proves the bus is talking even though no stroke exists. */
    eq32("and every report, still or not, was seen", s.reports, 1200u);
}

static void a_wrapped_frame_counter_is_not_a_pause(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    /* HcFmNumber is 16-bit: 0xFFF8 -> 0x0008 is 16 ms, not 65 kB of idle.  Unmasked
     * this would close the stroke and, worse, time it as 65 seconds, which the slow
     * sample would then keep as the slowest stroke of the session. */
    eq32("delta across the wrap", ps2_stroke_delta(0xFFF8u, 0x0008u), 16u);
    feed_stream(&s, 0xF000u, 1000u, 2u);       /* 2000 counts, 0xF000..0xF3E7 */
    feed_stream(&s, 0xFF00u, 500u, 5u);        /* a second stroke, straddling 0x0000 */
    finish(&s, &ratio);
    eq32("the wrap did not split the second stroke", s.samples, 2u);
    eq32("slow sample is the first", s.slow_counts, 2000u);
    eq32("fast sample is the second", s.fast_counts, 2500u);
    /* 500 reports one frame apart are 500 frame numbers, and the span that crosses
     * 0x0000 reads the same as the span that does not.  Were the boundary test a
     * plain `frame > last_f` instead of a masked delta, 0x00F3 would compare as
     * older than 0xFF00 and this sample would be two strokes: one of 256 reports
     * and one of 244, each under the 800-count bar, and the session would print
     * "no samples" after a person had slid the mouse twice. */
    eq32("and it was timed across the wrap in ms", s.fast_ms, 500u);
}

/* Two strokes whose boundary is the counter's own wrap. */
static void a_wrap_between_strokes_still_splits(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    /* A ends at 0xFFFB and B starts at 0x0100 -- 325 ms of stillness, which is a
     * pause, but a pause whose second half has a smaller frame number than its
     * first.  A boundary test written as `frame > last_f + gap` merges them into
     * one 3100-count stroke, and the session then prints "no samples" for a hand
     * that did the experiment correctly twice. */
    feed_stream(&s, 0xFE00u, 500u, 3u);        /* 1500 counts in 500 ms */
    feed_stream(&s, 0x0100u, 200u, 8u);         /* 1600 counts in 200 ms */

    eqi("the wrap did not merge the two slides", finish(&s, &ratio), PS2_STROKE_WIRE);
    eq32("samples across the wrap", s.samples, 2u);
    eq32("slow is the first", s.slow_counts, 1500u);
    eq32("fast is the second", s.fast_counts, 1600u);
    eq32("and the second was timed from its own start", s.fast_ms, 200u);
    eq32("ratio reads the two counts", ratio, 106u);
}

static void an_open_stroke_is_flushed(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    feed_stream(&s, 0u, 1500u, 1u);            /* the hand is still moving at print time */
    eq32("nothing recorded before the flush", s.samples, 0u);
    eqi("verdict: distance on the wire", finish(&s, &ratio), PS2_STROKE_NONE);
    eq32("recorded after it", s.samples, 1u);
    eq32("with the counts it held", s.slow_counts, 1500u);
    eq32("and the frames it took", s.slow_ms, 1500u);
    /* Flushing twice must not bill the stroke a second time. */
    ps2_stroke_flush(&s);
    eq32("a second flush changes nothing", s.samples, 1u);
}

static void a_shrink_reads_as_its_own_news(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    /* Counts that get *smaller* with speed cannot come from an accelerator, and
     * reading them as 1.00x would be the same false plateau in the other
     * direction -- the row names it as its own verdict instead. */
    feed_stream(&s, 0u, 1500u, 1u);            /* 1500 counts, 1 per frame */
    feed_stream(&s, 5000u, 60u, 15u);          /* 900 counts, 15 per frame */
    eqi("verdict: the fast slide carried less", finish(&s, &ratio), PS2_STROKE_SHRINK);
    eq32("ratio below one", ratio, 60u);
}

static void reset_clears_everything(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    feed_stream(&s, 0u, 1500u, 1u);
    feed_stream(&s, 5000u, 60u, 25u);
    finish(&s, &ratio);
    eq("samples before reset", s.samples, 2u);
    ps2_stroke_reset(&s);
    eq32("samples after reset", s.samples, 0u);
    eq32("open stroke after reset", s.open, 0u);
    eq32("counts after reset", s.counts, 0u);
    eq32("report count after reset", s.reports, 0u);
    eq32("longest stroke after reset", s.max_counts, 0u);
    /* Not NONE: a reset session has seen nothing, and the row that prints on top of
     * it has to say so, or `ptgain`-style resets would quietly re-aim the advice at
     * the person instead of at the port. */
    eqi("verdict after reset", ps2_stroke_verdict(&s, &ratio), PS2_STROKE_SILENT);
}

/* The fault a live session actually hit: the row refused with "slide further" while
 * the pointer handler had received no report at all since boot.  The two readings
 * below are what separate those cases, and they are the reason the splitter counts
 * reports rather than only strokes. */
static void a_silent_wire_is_not_an_idle_hand(void)
{
    ps2_stroke_t s = { 0 };
    uint32_t ratio = 0u;

    eqi("no report at all reads as a silent wire", finish(&s, &ratio), PS2_STROKE_SILENT);
    eq32("and prints no ratio", ratio, 0u);
    eq32("nothing was counted", s.reports, 0u);

    /* One still report is enough to leave that state: the device answered, so the
     * bus is alive and what is missing is motion.  An idle hand must therefore read
     * as the ordinary refusal, never as silence. */
    ps2_stroke_feed(&s, 0u, 0u);
    eqi("one still report ends the silence", ps2_stroke_verdict(&s, &ratio), PS2_STROKE_NONE);
    eq32("and is itself counted", s.reports, 1u);
    eq32("without opening a stroke", s.open, 0u);

    /* Two nudges, each under the sample bar: the distance is the fault here, and
     * `max_counts` is the number the reader can check against their own hand --
     * 300 against the 800 a sample needs, which is advice, not a shrug. */
    feed_stream(&s, 100u, 60u, 5u);        /* 300 counts in 60 ms */
    feed_stream(&s, 2000u, 40u, 5u);       /* 200 counts in 40 ms */
    finish(&s, &ratio);
    eq32("neither nudge qualified as a sample", s.samples, 0u);
    eq32("the longest stroke is still recorded", s.max_counts, 300u);
    eqi("and the refusal is the ordinary one", ps2_stroke_verdict(&s, &ratio), PS2_STROKE_NONE);
    eq32("with every report counted", s.reports, 101u);
}

static void counts_saturate_instead_of_wrapping(void)
{
    ps2_stroke_t s = { 0 };

    /* The guard that keeps ps2_stroke_verdict()'s multiply inside 32 bits. */
    s.open = 1u;
    s.start_f = 0u;
    s.last_f = 0u;
    s.counts = PS2_STROKE_SATURATE;
    ps2_stroke_feed(&s, 1u, 5000u);
    eq32("a pathological stream cannot overflow", s.counts, PS2_STROKE_SATURATE);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 pointer strokes: segmentation, the two-speed pair, and\n");
    printf(" the controls that say the verdict row can go red\n");
    printf("==========================================================\n");

    wire_carries_distance();
    wire_carries_speed();
    no_spread_is_not_a_verdict();
    short_stroke_is_not_a_sample();
    a_pause_splits_but_a_flattening_does_not();
    still_reports_are_silence();
    a_wrapped_frame_counter_is_not_a_pause();
    a_wrap_between_strokes_still_splits();
    an_open_stroke_is_flushed();
    a_shrink_reads_as_its_own_news();
    a_silent_wire_is_not_an_idle_hand();
    reset_clears_everything();
    counts_saturate_instead_of_wrapping();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the pair is told apart, and the controls fire\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a stroke boundary or a verdict is not what the bus deserves\n");
    return 1;
}
