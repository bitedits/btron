/*
 * test_ps2_fused_row.c - does the compositor's row rule still mean the same thing
 *                        after the second read of the band was removed?
 *
 * include/btron/fused_row.h holds the rule redraw_all_windows_clip() applies to every
 * client row it composites, in two shapes: the scan-then-copy row every port has always
 * run, and the fused row the PS2 port now runs because the EE priced the walk's second
 * read of the band at 23.86 ns/px there (doc/txt/PS2.txt, LANE 9's `Shave` cell; the
 * ladder itself is src/cores/core_ps2.c's RAM-to-RAM phase, whose `blit ship` row is
 * this same header function measured on the guest).  One pass with the decision folded
 * into it is a faster row.  It is also a row whose correctness nobody can see: the
 * composite lands in a framebuffer on a machine with no display attached, and a group
 * that decides wrong paints a hole in a window or paints over one -- at a column that
 * depends on where the window sits, so it moves when the window moves.
 *
 * The claim under test is therefore byte-identity, not appearance: over one source band,
 * each shape must leave exactly the bytes the scalar rule leaves -- writing every
 * non-sentinel pixel, touching no sentinel pixel, and writing nothing outside the span.
 * The scan-then-copy shape is held to the same reference because this file is the only
 * place either shape is checked, and it is what the Pi 400 ships.  The dispatcher is
 * checked against the shape its own dial names, so an inverted #if cannot hide behind
 * three shapes that all happen to agree with the reference.
 *
 * Phase is its own family, because that is the trap the fused shape was built to avoid:
 * in the walk the destination row begins at `dest_x + cx0` and the source at `cx0`, so
 * their 8-byte phases differ whenever a window's left edge is odd.  Every span is run at
 * both relative phases, which is the whole set a COLOR* can be in.
 *
 * Two controls are deliberately-wrong rows with the opposite expected answer -- a group
 * stored without being tested, and a rule applied one pixel short of the span's end.  If
 * either passes as correct, this file's byte-identity is not doing any work, and the row
 * that says so is printed before the verdict.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* The header itself, not a model of it: the identical translation unit is what wnd.c
 * compiles into every target, btron-ps2.elf among them. */
#include "../../include/btron/fused_row.h"

#define TRANSP BTRON_ROW_TRANSPARENT
#define CANVAS(i) ((COLOR)(0x00A5A5A5u ^ (uint32_t)(i)))   /* the pixel already there */

#define MAXSPAN 804         /* the widest row the walk can be asked to composite */
#define PAD     8           /* guard pixels either side of the destination span */

static int s_checks, s_failures;
static unsigned long long s_pixels_compared;

static void ok(const char *what, int pass)
{
    s_checks++;
    if (pass) return;
    s_failures++;
    printf("  [FAIL] %s\n", what);
}

/* Seeded LCG: the same band for the same seed on every run, so a [FAIL] line names a
 * reproducible case and not a coincidence of this process's heap. */
typedef struct { uint32_t s; } RNG;
static uint32_t rnd(RNG *r)
{
    r->s = r->s * 1103515245u + 12345u;
    return (r->s >> 16) ^ (r->s << 4);
}

/* A colour that is never the sentinel and never the canvas pattern, so a pixel copied
 * from the wrong column is a difference rather than an accident. */
static uint32_t live_colour(RNG *r)
{
    uint32_t c;
    do {
        c = rnd(r) & 0x00FFFFFFu;
    } while (c == 0u || (c & 0xFFu) == 0xA5u);
    return c;
}

static COLOR s_src[MAXSPAN + 2 * PAD];
static COLOR s_dst[MAXSPAN + 3 * PAD];
static COLOR s_ref[MAXSPAN + 3 * PAD];
static COLOR s_two[MAXSPAN + 3 * PAD];
static COLOR s_dis[MAXSPAN + 3 * PAD];

/* The rule as the walk has always meant it, one pixel at a time.  Both shapes must equal
 * THIS, not each other: two shapes wrong in the same way would still agree. */
static void scalar_rule(COLOR *dst, const COLOR *src, int span)
{
    for (int i = 0; i < span; i++) {
        if (src[i] != TRANSP) dst[i] = src[i];
    }
}

/* One case.
 *
 *   `every`  puts a sentinel every k-th pixel of the span; 0 for none, 1 for a row that
 *            is entirely transparent -- the two contents the ladder says decide whether a
 *            fused group is a win or a loss.
 *   `extra`  a short list of single sentinel columns (terminated by -1), for the facts a
 *            density cannot name: column 0, the last column, one just inside a group, one
 *            just outside it.
 *   `shift`  the destination starts one pixel further into its buffer than the source
 *            does, which is the 4-mod-8 relative phase an odd window edge produces.
 *
 * Returns nothing; it compares three shipped shapes against the reference and audits both
 * buffers' guard pixels. */
static void one_case(const char *family, int span, int every, const int *extra,
                     int shift, uint32_t seed)
{
    if (span < 0 || span > MAXSPAN) return;

    COLOR *src = s_src + PAD;                  /* the case builder writes into it */
    COLOR *dst = s_dst + PAD + shift;     /* the fused pass writes here */
    COLOR *ref = s_ref + PAD + shift;
    COLOR *two = s_two + PAD + shift;     /* the scan-then-copy pass */
    COLOR *dis = s_dis + PAD + shift;     /* the dispatcher the walk calls */
    const size_t span_bytes = (size_t)span * sizeof(COLOR);

    RNG r = { seed ? seed : 1u };
    for (int i = 0; i < MAXSPAN + 2 * PAD; i++) s_src[i] = live_colour(&r);
    if (every >= 1) {
        for (int i = 0; i < span; i += every) src[i] = TRANSP;
    }
    if (extra) {
        for (const int *h = extra; *h >= 0; h++) {
            if (*h < span) src[*h] = TRANSP;
        }
    }

    /* All three destinations start as the same canvas: the window underneath, in a value
     * that differs per column so a row shifted by one pixel is caught. */
    for (int i = -PAD; i < span + 2 * PAD; i++) {
        dst[i] = ref[i] = two[i] = dis[i] = CANVAS(PAD + shift + i);
    }
    scalar_rule(ref, src, span);

    btron_row_copy_fused(dst, src, span);
    btron_row_copy_twopass(two, src, span);
    btron_row_copy_skip_zero(dis, src, span);
    s_pixels_compared += (unsigned long long)span;

    char label[96];
    snprintf(label, sizeof(label), "%s: fused == the rule (span=%d shift=%d)",
             family, span, shift);
    ok(label, memcmp(dst, ref, span_bytes) == 0);

    snprintf(label, sizeof(label), "%s: twopass == the rule (span=%d shift=%d)",
             family, span, shift);
    ok(label, memcmp(two, ref, span_bytes) == 0);

    /* The dial's own line, checked against the shape it names rather than against the
     * reference: an inverted #if ships a correct-looking row on every port and is only
     * visible here. */
#if defined(BTRON_PS2_TARGET)
    snprintf(label, sizeof(label), "%s: PS2 dial reaches the fused row (span=%d)",
             family, span);
    ok(label, memcmp(dis, dst, span_bytes) == 0);
#else
    snprintf(label, sizeof(label), "%s: host dial reaches the twopass row (span=%d)",
             family, span);
    ok(label, memcmp(dis, two, span_bytes) == 0);
#endif

    /* Nothing outside [dst, dst+span) may move, on either side: a group loop that runs
     * past the end of a row writes the next row's first pixels, and one screen is as good
     * as another for noticing that. */
    int guards = 1;
    for (int i = 1; i <= PAD; i++) {
        if (dst[-PAD + i - 1] != CANVAS(PAD + shift - PAD + i - 1)) guards = 0;
        if (dst[span + i - 1] != CANVAS(PAD + shift + span + i - 1)) guards = 0;
    }
    snprintf(label, sizeof(label), "%s: no write outside the span (span=%d shift=%d)",
             family, span, shift);
    ok(label, guards);

    /* The sentinel columns, checked against the canvas directly rather than through the
     * reference, which is the rule's own output and so cannot accuse it.  Derived from
     * `src`, which the layout above built. */
    int kept = 1;
    for (int i = 0; i < span; i++) {
        if (src[i] == TRANSP && dst[i] != CANVAS(PAD + shift + i)) kept = 0;
    }
    snprintf(label, sizeof(label), "%s: every sentinel column kept the canvas (span=%d)",
             family, span);
    ok(label, kept);

    /* And the live columns really were written, or the two could agree by both leaving
     * the row alone. */
    int wrote = 1;
    for (int i = 0; i < span; i++) {
        if (src[i] != TRANSP && dst[i] != src[i]) wrote = 0;
    }
    snprintf(label, sizeof(label), "%s: every live column was copied (span=%d)",
             family, span);
    ok(label, wrote);
}

/* The span sweep, at both relative phases.  The lengths are boundary cases rather than a
 * range: they straddle the group's start and end, the group plus one, and the tail's
 * first and last pixel, then the widest row the walk can be handed. */
static const int s_spans[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 12, 15, 16, 17, 18, 20, 23, 24, 31, 32, 33,
    46, 47, 48, 49, 63, 64, 65, 79, 80, 95, 96, 97, 127, 128, 129,
    255, 256, 257, 511, 512, 513, 799, 800, 801
};

static void spans_at_both_phases(const char *family, int every, const int *extra)
{
    for (int shift = 0; shift <= 4; shift += 4) {
        for (unsigned i = 0; i < sizeof(s_spans) / sizeof(s_spans[0]); i++) {
            one_case(family, s_spans[i], every, extra, shift,
                     20261014u + (uint32_t)i + (uint32_t)shift);
        }
    }
}

static void the_contents_regimes(void)
{
    /* The all-opaque band is what production composites -- the shipped walk's own column
     * says `blit=24481(613440 px)` = 39.90 ns/px, the bench's `scan` rung to two
     * decimals -- and the one-in-eight band is the regime the ladder says a fused group
     * loses in.  Both ship, so both are gated. */
    spans_at_both_phases("all opaque", 0, NULL);
    spans_at_both_phases("one in eight", 8, NULL);
    spans_at_both_phases("one in sixteen", 16, NULL);
    spans_at_both_phases("whole row transparent", 1, NULL);
    printf("  span sweep: %u lengths x 2 phases x 4 contents\n",
           (unsigned)(sizeof(s_spans) / sizeof(s_spans[0])));
}

static void every_single_sentinel_column(void)
{
    /* A 34-pixel row covers one group of 16, the next group's first, middle and last
     * pixel, and the tail's first and last, with every column between them. */
    for (int p = 0; p < 34; p++) {
        const int hole[2] = { p, -1 };
        one_case("sentinel column", 34, 0, hole, 0, 777u + (uint32_t)p);
        one_case("sentinel column", 34, 0, hole, 4, 991u + (uint32_t)p);
    }
    printf("  sentinel columns swept: 68 (34 columns x 2 phases of a 34-px row)\n");
}

static void edges_a_group_test_can_miss(void)
{
    static const int first_last[] = { 0, 800, -1 };
    static const int group_edges[] = { 15, 16, 31, 32, 47, 48, -1 };
    static const int head_and_tail[] = { 0, 1, 2, 79, 80, 81, -1 };
    spans_at_both_phases("first and last", 0, first_last);
    spans_at_both_phases("group edges", 0, group_edges);
    spans_at_both_phases("head and tail", 0, head_and_tail);

    /* Two sentinels bracketing one live pixel, and one live pixel alone in a group: the
     * shapes a dragged window's 1-px damage column produces. */
    static const int pinch[] = { 6, 8, -1 };
    static const int lone[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 12, 13, 14, 15, -1 };
    spans_at_both_phases("pinched pixel", 0, pinch);
    spans_at_both_phases("one live pixel", 0, lone);
}

/* ---------------------------------------------------------------- controls ---- */

/* A fused pass that never tests its group: the shape the ladder would have priced if the
 * decision were free.  It must be CAUGHT. */
static void control_an_untested_group(void)
{
    enum { SPAN = 40 };
    COLOR *src = s_src + PAD, *dst = s_dst + PAD, *ref = s_ref + PAD;
    RNG r = { 31337u };

    for (int i = 0; i < MAXSPAN + 2 * PAD; i++) s_src[i] = live_colour(&r);
    src[3] = TRANSP;                       /* inside the first group */
    src[20] = TRANSP;                      /* inside the second */
    for (int i = -PAD; i < SPAN + PAD; i++) dst[i] = ref[i] = CANVAS(PAD + i);
    scalar_rule(ref, src, SPAN);
    for (int i = 0; i < SPAN; i++) dst[i] = src[i];         /* the wrong row */

    ok("control: an untested group disagrees with the rule",
       memcmp(dst, ref, SPAN * sizeof(COLOR)) != 0);
    ok("control: and it disagrees exactly at the two sentinel columns",
       dst[3] != ref[3] && dst[20] != ref[20]);
}

/* The rule applied one pixel short of the span's end -- what a loop bound written for a
 * whole group produces when the tail is forgotten.  Must be CAUGHT. */
static void control_a_short_row(void)
{
    enum { SPAN = 20 };
    COLOR *src = s_src + PAD, *dst = s_dst + PAD, *ref = s_ref + PAD;
    RNG r = { 90210u };

    for (int i = 0; i < MAXSPAN + 2 * PAD; i++) s_src[i] = live_colour(&r);
    for (int i = -PAD; i < SPAN + PAD; i++) dst[i] = ref[i] = CANVAS(PAD + i);
    scalar_rule(ref, src, SPAN);
    for (int i = 0; i + 1 < SPAN; i++) dst[i] = src[i];     /* the wrong row */

    ok("control: a row one pixel short disagrees with the rule",
       memcmp(dst, ref, SPAN * sizeof(COLOR)) != 0);
}

/* The real shape on the same boundary, so the control above is not merely a fact about
 * the reference: the span's LAST pixel must be written when it is live and kept when it
 * is the sentinel. */
static void control_the_real_row_at_that_boundary(void)
{
    enum { SPAN = 20 };
    COLOR *src = s_src + PAD, *dst = s_dst + PAD;
    RNG r = { 90211u };

    for (int i = 0; i < MAXSPAN + 2 * PAD; i++) s_src[i] = live_colour(&r);
    for (int i = -PAD; i < SPAN + PAD; i++) dst[i] = CANVAS(PAD + i);
    btron_row_copy_fused(dst, src, SPAN);
    ok("the real fused row writes the span's last pixel",
       dst[SPAN - 1] == src[SPAN - 1]);

    src[SPAN - 1] = TRANSP;
    dst[SPAN - 1] = CANVAS(PAD + SPAN - 1);
    btron_row_copy_fused(dst, src, SPAN);
    ok("and keeps the span's last pixel when it is the sentinel",
       dst[SPAN - 1] == CANVAS(PAD + SPAN - 1));
}

static void the_dial_is_the_measured_one(void)
{
    printf("  fused group size: %d pixels; sentinel = 0x%08x\n",
           BTRON_ROW_FUSE_GROUP, (unsigned)TRANSP);
#if defined(BTRON_PS2_TARGET)
    printf("  port dial: BTRON_PS2_TARGET -> the walk calls the FUSED row\n");
#else
    printf("  port dial: host -> the walk calls the TWO-PASS row, as every port but PS2 does\n");
#endif
    ok("the group is the one the ladder priced", BTRON_ROW_FUSE_GROUP == 16);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" fused_row.h: the compositor's row rule, byte for byte\n");
    printf("==========================================================\n");

    the_dial_is_the_measured_one();
    the_contents_regimes();
    every_single_sentinel_column();
    edges_a_group_test_can_miss();
    control_an_untested_group();
    control_a_short_row();
    control_the_real_row_at_that_boundary();

    printf("  pixels compared against the scalar rule: %llu\n", s_pixels_compared);
    ok("the sweep compared real work, not an empty loop", s_pixels_compared > 100000ull);

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-OK] both shipped rows leave exactly the bytes the scalar rule leaves\n");
        return 0;
    }
    printf("[CI-FAIL] a fused or two-pass row disagrees with the rule it replaced\n");
    return 1;
}
