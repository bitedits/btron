/*
 * ps2_ptr_stroke.h -- splitting a hand's motion into strokes, and pricing the two
 *                     speeds of the same slide against each other.
 *
 * Why this file exists rather than five more lines in core_ps2.c
 *
 * The question this port has to answer before it can ship a pointer gain is not
 * "how fast should the cursor move" but "which layer already multiplied the
 * distance": if the wire carries host cursor pixels, one guest constant is correct
 * and sufficient; if something above the wire (macOS's own cursor curve) has
 * already scaled counts by speed, then no constant is right, because the constant
 * would have to change with how hard the hand is moving.  The law is stated in
 * doc/md/PS2.md, "Which layer owns the pointer", and the test is the slow-slide /
 * fast-slide pair it reads as a ratio.
 *
 * Taking that test used to need typing in the middle of it: `ptrstat` is a Stage 1
 * prompt command, a session that goes straight to the desktop reaches it only by
 * Ctrl+\, and the protocol was "slide slowly, call the command, slide fast, call it
 * again, then read which window was which".  A hand that cannot put the cursor
 * anywhere is being asked to type, which is the wrong order -- and worse, the
 * windows reset under the caller, so the sample that decides the answer is the one
 * most likely to be discarded by the keystroke that reads it.
 *
 * So the segmentation lives here instead, and it needs no keystroke at all: the
 * hand performs the same long slide twice at two speeds, every report is filed into
 * the stroke it belongs to, the slowest and fastest qualifying strokes are kept as
 * the pair, and the ratio between them is printed as one row with its own spread
 * check.  The spread check is what makes the row honest rather than suggestive: two
 * slides at nearly the same speed are not the experiment, and saying so is better
 * than printing 1.0x and having it read as a verdict.
 *
 * What it deliberately does NOT do
 *
 * It does not measure the gain.  Counts per guest pixel is a constant by
 * construction here -- the guest's own pixels are the integral of the same counts
 * times the shipped multiplier -- so a row that "proved" proportionality from guest
 * pixels would be proving arithmetic.  The only fixed distance available to this
 * port is the one a human supplies on purpose, which is why `ptgain <host px>`
 * still exists and still owns that half of the question.
 *
 * It also cannot see the acceleration directly; it sees its consequence, which is
 * that the second slide carried more counts than the first for the same physical
 * travel.  That is why a qualifying stroke has to be long (PS2_STROKE_MIN): the
 * shorter the slide, the more a hand's overshoot moves the distance under the test.
 *
 * Units and limits
 *
 * `frame` is the OHCI HcFmNumber, which this port measures at 1 ms per frame (see
 * the bus row of the same table), so a frame delta is a millisecond delta and no
 * CP0 Count is involved -- which matters because a stroke can outlast the counter's
 * 29.1 s wrap while it cannot outlast the 16-bit frame counter it is timed with.
 * The deltas are therefore masked, the way `[PSTAT]` masks its own window.
 *
 * Every product here is 32-bit and every division is by a 32-bit value: this image
 * is built -msoft-float with no __udivdi3 in the link, so a 64-bit divide would be
 * a call into a routine the port does not have.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_PTR_STROKE_H
#define PS2_PTR_STROKE_H

#include <stdint.h>

/* Stillness that ends a stroke, in frames.  A pause shorter than this is the flat
 * part of one slide -- a hand's path is not a straight line, and a pointer that
 * briefly stops registering in the middle of a sweep is still sweeping.  Longer than
 * this and the next motion is a new intention, so it must be a new sample, or the
 * two slides the test compares would be merged into one and the pair lost. */
#define PS2_STROKE_GAP_MS     100u

/* Counts a stroke has to carry to be used as a sample.  At the shipped emulator
 * gain this is roughly a third of the 800 px canvas walked in one go; below it a
 * stroke is a nudge, and a nudge's distance is not the same physical slide twice. */
#define PS2_STROKE_MIN        800u

/* The two samples must differ in speed by at least this much, in percent, or the
 * row refuses to call itself a verdict: 200 = the fast slide must be at least twice
 * the slow slide's speed. */
#define PS2_STROKE_SPREAD_PCT 200u

/* Where a stroke's counts are capped, so a pathological window cannot make
 * ps2_stroke_verdict()'s multiply overflow.  No hand reaches it. */
#define PS2_STROKE_SATURATE   40000000u

/* Verdicts.  The row prints them in the order the reading rules ask for, not in
 * this order, and the one that matters most for a first session is last: a pair
 * cannot be taken from a wire that carried nothing, and telling the hand that it
 * should slide harder is the wrong advice for that case. */
#define PS2_STROKE_NONE       0   /* fewer than two samples -- nothing to compare */
#define PS2_STROKE_SPREAD     1   /* two samples, no speed spread -- not the experiment */
#define PS2_STROKE_WIRE       2   /* same counts for the same slide: the wire carries distance */
#define PS2_STROKE_ACCEL      3   /* the fast slide carried more: a multiplier above the wire */
#define PS2_STROKE_SHRINK     4   /* the fast slide carried fewer: something divides, and that is its own news */
#define PS2_STROKE_SILENT     5   /* no report ever reached the reader -- the wire, not the hand */

typedef struct {
    /* The stroke currently under the hand. */
    uint32_t open;
    uint32_t start_f, last_f;
    uint32_t counts;
    /* The two samples the verdict reads, and how many strokes qualified. */
    uint32_t samples;
    uint32_t slow_counts, slow_ms;
    uint32_t fast_counts, fast_ms;
    /* The longest stroke the hand actually gave, qualifying or not.  A refusal that
     * says only "800 counts are needed" is advice the reader cannot check: with this
     * field the row says whether the slide was too short (move further) or the wire
     * was silent (nothing arrived at all), which are different faults and different
     * fixes. */
    uint32_t max_counts;
    /* Every report the splitter was fed, moving or not.  `reports == 0` is the one
     * reading no window can give, because a window that resets cannot tell "the hand
     * did not slide" from "the bus delivered nothing" -- and only the second one
     * means the port, rather than the person, is what to look at. */
    uint32_t reports;
} ps2_stroke_t;

/* HcFmNumber is 16-bit; a stroke that happens to straddle its wrap is a normal
 * stroke and must not read as a 65-second pause. */
static inline uint32_t ps2_stroke_delta(uint32_t from, uint32_t to)
{
    return (to - from) & 0xFFFFu;
}

/* Counts per millisecond in 1000ths, split so no product exceeds 32 bits: `whole`
 * is bounded by the 127-count byte and `rem * 1000` by 999 * 1000. */
static inline uint32_t ps2_stroke_speed1000(uint32_t counts, uint32_t ms)
{
    const uint32_t whole = counts / ms;
    const uint32_t rem = counts % ms;

    if (!ms) return 0u;
    return whole * 1000u + (rem * 1000u) / ms;
}

/* File a finished stroke into the pair: keep the slowest and the fastest by speed,
 * because those two are the experiment no matter how many strokes the session
 * contains, and the extremes are the widest spread the hand actually gave. */
static inline void ps2_stroke_record(ps2_stroke_t *s, uint32_t counts, uint32_t ms)
{
    const uint32_t spd = ps2_stroke_speed1000(counts, ms);
    const uint32_t slow_spd = s->samples ? ps2_stroke_speed1000(s->slow_counts, s->slow_ms) : 0u;
    const uint32_t fast_spd = s->samples ? ps2_stroke_speed1000(s->fast_counts, s->fast_ms) : 0u;

    if (counts < PS2_STROKE_MIN || !ms) return;

    if (!s->samples) {
        s->slow_counts = s->fast_counts = counts;
        s->slow_ms = s->fast_ms = ms;
        s->samples = 1u;
        return;
    }
    s->samples++;
    if (spd <= slow_spd) {
        s->slow_counts = counts;
        s->slow_ms = ms;
    }
    if (spd >= fast_spd) {
        s->fast_counts = counts;
        s->fast_ms = ms;
    }
}

/* One report's horizontal magnitude.  `ax` is |dx| as the decoder saw it, before
 * this port's own gain -- the quantity the layer question is actually about. */
static inline void ps2_stroke_feed(ps2_stroke_t *s, uint32_t frame, uint32_t ax)
{
    s->reports++;
    if (!ax) return;                       /* a still report is silence, not a gap */

    if (!s->open || ps2_stroke_delta(s->last_f, frame) > PS2_STROKE_GAP_MS) {
        if (s->open)
            ps2_stroke_record(s, s->counts, ps2_stroke_delta(s->start_f, s->last_f) + 1u);
        s->open = 1u;
        s->start_f = frame;
        s->counts = ax;
    } else if (s->counts < PS2_STROKE_SATURATE) {
        s->counts += ax;
    }
    if (s->counts > s->max_counts) s->max_counts = s->counts;
    s->last_f = frame;
}

/* Close whatever the hand left under way, so a session that ends with the slide
 * that decides the question still prints the answer.  The stroke is timed to its
 * last report, not to the moment of printing: a slide that stopped three seconds
 * before the row was asked for took the time it took, and a pause that happened
 * after it is not part of the hand's travel. */
static inline void ps2_stroke_flush(ps2_stroke_t *s)
{
    if (!s->open) return;
    ps2_stroke_record(s, s->counts, ps2_stroke_delta(s->start_f, s->last_f) + 1u);
    s->open = 0u;
    s->counts = 0u;
}

static inline void ps2_stroke_reset(ps2_stroke_t *s)
{
    s->open = 0u;
    s->start_f = s->last_f = 0u;
    s->counts = 0u;
    s->samples = 0u;
    s->slow_counts = s->fast_counts = 0u;
    s->slow_ms = s->fast_ms = 0u;
    s->max_counts = 0u;
    s->reports = 0u;
}

/* fast_counts / slow_counts, in 100ths, plus the verdict that reading earns.  The
 * spread test comes first and it is the whole point of returning a state rather than
 * a number: a row of "1.00x" produced by two identical-speed slides looks exactly
 * like the answer and is not one. */
static inline int ps2_stroke_verdict(const ps2_stroke_t *s, uint32_t *ratio100)
{
    const uint32_t slow_spd = ps2_stroke_speed1000(s->slow_counts, s->slow_ms ? s->slow_ms : 1u);
    const uint32_t fast_spd = ps2_stroke_speed1000(s->fast_counts, s->fast_ms ? s->fast_ms : 1u);

    if (ratio100) *ratio100 = 0u;
    /* Before every other reading: a wire that delivered nothing is NOT a hand that
     * failed to slide, and the advice the row gives for those two is opposite.  It
     * takes one report -- a still one counts -- to leave this state, so a session
     * whose mouse never moved is told the truth about the bus. */
    if (!s->reports) return PS2_STROKE_SILENT;
    if (s->samples < 2u || !s->slow_counts || !s->fast_counts) return PS2_STROKE_NONE;
    /* Both products stay inside 32 bits: the speeds are counts per ms, bounded by a
     * 127-count byte, and the spread constant is small. */
    if (fast_spd * 100u < slow_spd * PS2_STROKE_SPREAD_PCT) return PS2_STROKE_SPREAD;

    if (ratio100)
        *ratio100 = s->fast_counts > PS2_STROKE_SATURATE / 100u
                        ? PS2_STROKE_SATURATE / 100u
                        : s->fast_counts * 100u / s->slow_counts;
    if (!ratio100) return PS2_STROKE_NONE;

    if (*ratio100 >= 125u) return PS2_STROKE_ACCEL;
    if (*ratio100 <= 80u)  return PS2_STROKE_SHRINK;
    return PS2_STROKE_WIRE;
}

#endif /* PS2_PTR_STROKE_H */
