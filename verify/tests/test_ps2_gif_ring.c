/*
 * verify/tests/test_ps2_gif_ring.c -- LANE 10: a producer that is not stopped by its consumer
 *
 * doc/txt/PS2.txt, "The lanes", row 10, and VIOLATION 2.  The claim under test is not that the GIF
 * is fast; it is the S_FULL shape from doc/txt/ASYNC.txt §0.1 -- bounded capacity, a counted
 * refusal, a completion check at the next yield -- and a claim about what a producer DOES NOT do
 * cannot be read off a register.  So the ring is compiled here (all of it: it is bookkeeping and
 * three hooks, which is the whole reason it is separable at all) and the hooks are a simulated
 * channel 2 this file decides when to free.
 *
 * What is checkable, and the edit that turns each row red -- LAW 16d, because a gate that cannot
 * fail is not an instrument (every one of these was run, and the failure counts are in
 * doc/md/PS2-GS-LOG.md beside the date):
 *
 *   post() returns while the channel is still busy, having started exactly one transfer.
 *       Remove the `if (ps2_gif_hw_busy()) break;` guard in drain() and channel 2 gets two starts
 *       at once -- which is the row, not a comment about it.
 *   At capacity post() refuses and counts.
 *       Change `s_gif_st.refused++` to a spin and the refusal rows go red twice over: the count is
 *       zero and the harness hangs, which is what the old sender did.  The capacity is on
 *       *outstanding* transfers -- queued plus the one already on the bus -- and counting s_gif_n alone
 *       is the same defect in a smaller form: the ring would then own nine buffers against a cap of
 *       eight, and the buffer-lifetime law is about buffers, not about array slots.
 *   Transfers reach the channel in posted order, including across a wrap of the index.
 *       Swap `s_gif_head` for `s_gif_tail` when taking the next descriptor and the order test sees the
 *       newest packet first.
 *   reap() leaves nothing queued and nothing in flight, and says how long that took.
 *       Make reap() a single drain() -- the shape the lane would have been implemented in by
 *       someone who read "non-blocking" as the whole requirement -- and the pending row goes red
 *       while the wait rows go to zero.
 *   The window snapshot reports "since the last take" and the live fields survive it.
 *       Drop the clears and `posted=` becomes the machine's lifetime total, which is what the
 *       [GSTS] gif: row would then be lying about.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* A ring this suite can actually fill.  The shipped capacity is derived from the stage's group
 * table and is asserted there (src/gl/eegs_present.c's compile-time bound); here it is eight, so
 * the capacity path and the index wrap are reachable in a handful of posts.  Eight, not seven: the
 * ring's whole-descriptor state must be distinguishable from its empty state, and this is the
 * shape that says so. */
#define PS2_GIF_RING_CAP 8u
#include "../../src/drivers/ps2/ps2_gif_ring.h"
#include "../../src/drivers/ps2/ps2_gif_ring.c"

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
    printf("  [FAIL] %-56s got=%u want=%u\n", what, (unsigned)got, (unsigned)want);
}

/* ── The simulated channel 2 ────────────────────────────────────────────────────────────
 * A transfer takes `s_xfer_us` microseconds and pays for them out of ps2_gif_hw_busy(), the only way
 * the ring is allowed to learn that a transfer finished.  Ticking `s_now` inside busy() is what makes
 * the simulation honest in both directions: a reap() against a slow consumer terminates eventually
 * and reports a real number of microseconds, and a post() against a busy one returns without polling
 * it to death -- which is the row that would hang, and did, when this model had no countdown at all.
 * `s_xfer_us` is therefore a per-test choice of how fast the GIF is: 0 for a consumer that keeps up
 * (the common pass), and a value above the number of polls the test makes for a consumer that does
 * not.  sim_complete() is the forced case -- an interrupt arrived, STR is clear now.
 *
 * Every start is recorded, pointer and length, so order and count are read off the bus rather than
 * inferred from a counter the code under test wrote. */
static uint32_t s_free;             /* 1 while a transfer is in flight: STR */
static uint32_t s_left;             /* µs of this transfer the channel has not yet paid for */
static uint32_t s_polls;            /* busy() calls this transfer has answered, for the budget */
static uint32_t s_xfer_us;          /* how slow the simulated GIF is */
static uint32_t s_now;              /* ps2_gif_hw_us()'s notion of now */
static uint32_t s_deadlocked;       /* transfers the poll budget had to force off the bus */
static const void *s_seen[512];
static uint32_t s_seen_qwc[512];
static uint32_t s_seen_n;

/* A transfer that never finishes is a bug in the code under test, and this file has a long-running
 * proof of that: the first cut of this sim freed the channel only when the test said so, and
 * `make test-ps2-gif-ring` sat in reap()'s wait loop until it was killed.  So the sim refuses to be
 * the thing that hides a deadlock -- after this many polls it forces the transfer off the bus and
 * counts the event, and the last row of main() requires the count to be zero.  A hanging gate is not
 * a gate; a red one is. */
#define SIM_POLL_BUDGET 100000u

int ps2_gif_hw_busy(void)
{
    if (!s_free) return 0;

    /* The budget is what a test cannot see: a consumer set slower than SIM_POLL_BUDGET microseconds
     * would keep reap() polling, which is the failure this file exists to be able to report. */
    if (s_left != 0u && s_polls < SIM_POLL_BUDGET) {
        s_left--;
        s_polls++;
        s_now += 1u;                 /* the wait costs time, because a real one does */
        return 1;
    }
    if (s_left != 0u) s_deadlocked++;
    s_free = 0u;
    s_left = 0u;
    return 0;
}

void ps2_gif_hw_start(const void *packet, uint32_t qwc)
{
    if (s_seen_n < 512u) {
        s_seen[s_seen_n] = packet;
        s_seen_qwc[s_seen_n] = qwc;
        s_seen_n++;
    }
    s_free = 1u;
    s_left = s_xfer_us;
    s_polls = 0u;
}

uint32_t ps2_gif_hw_us(void) { return s_now; }

static void sim_complete(void) { s_free = 0u; }

/* The consumer's speed, and the only state a test has to set for the capacity rows to mean
 * something: the ring refuses at PS2_GIF_RING_CAP *outstanding* transfers, so a GIF that never
 * finishes is what turns a full queue from a hang into a counted refusal. */
static void sim_consumer(uint32_t us_per_transfer) { s_xfer_us = us_per_transfer; }

static void sim_tick(uint32_t us) { s_now += us; }

static void sim_reset(void)
{
    s_free = 0u;
    s_left = 0u;
    s_polls = 0u;
    s_xfer_us = 0u;
    s_now = 1000u;
    s_seen_n = 0u;
    memset(s_seen, 0, sizeof(s_seen));
    /* s_deadlocked is deliberately not cleared: it is this file's own invariant over the whole
     * run, and a test that resets it would reset the evidence that nothing hung. */
}

/* The ring is a singleton, so a suite run needs it put back the way the image would find it at
 * boot: no descriptors, nothing in flight, every counter zero.  Reaching the statics directly is
 * the point of compiling the unit in rather than linking it. */
static void ring_reset(void)
{
    sim_reset();
    memset(s_gif_ring, 0, sizeof(s_gif_ring));
    s_gif_head = s_gif_tail = s_gif_n = 0u;
    s_gif_in_flight = 0u;
    memset(&s_gif_st, 0, sizeof(s_gif_st));
}

/* Packets are identified by address, and the addresses are of eight statics: the ring hands the
 * channel the pointer it was given, and nothing else, so a stable object per packet is all the
 * identity a check needs. */
static uint64_t s_pkt[8][2] __attribute__((aligned(16)));

/* ── 1. the producer is not stopped by the consumer ────────────────────────────────────── */

static void test_post_never_waits(void)
{
    uint32_t i;
    int rc = 0;

    ring_reset();
    sim_consumer(1000u);            /* a GIF that is still on its first quadword all test long */

    /* Nine posts against a capacity of eight, with the channel never freed.  The first takes the
     * channel; the rest are accepted into the queue; the ninth is refused -- the cap is on what the
     * ring *owns*, one transfer on the bus included, because that is the quantity the buffer-lifetime
     * law is about.  None of that can be true unless every post returned: a sender that spun on STR
     * would not be here nine deep, and would not have a refusal count at all. */
    for (i = 0u; i < 9u; i++) rc = ps2_gif_post(s_pkt[i % 8u], 4u + i);

    check("the ninth post found the ring at capacity and came back instead of spinning", rc != 0);
    eqi("the channel was started exactly once, not once per post", s_seen_n, 1u);
    eqi("seven descriptors are the backlog", s_gif_n, 7u);
    eqi("the full ring was a counted refusal, not a silent loss", s_gif_st.refused, 1u);
    eqi("and eight posts were accepted", s_gif_st.posted, 8u);

    {
        ps2_gif_stats_t st;
        ps2_gif_stats(&st);
        eqi("the row's `queued=` is the backlog", st.queued, 7u);
        eqi("the row's `inflight=` is the one transfer the hardware takes", st.in_flight, 1u);
        eqi("the row's `cap=` names the bound the refusal hit", st.cap, PS2_GIF_RING_CAP);
        /* The quantity `cap=` bounds, and the one a producer waits on, are the same number: queued
         * plus in flight, never more than the cap. */
        eqi("and pending() is exactly at the bound", ps2_gif_pending(), PS2_GIF_RING_CAP);
        eqi("and nothing was reaped, so nothing blocked", st.wait_us, 0u);
    }

    /* The lengths travel with the pointers, or a drained transfer would be a quarter of the
     * packet it stands for and the GIF would read the next descriptor's words as pixels. */
    eqi("the transfer on the bus is the first packet's", s_seen_qwc[0], 4u);
    check("and it is the first packet, not the last", s_seen[0] == &s_pkt[0][0]);
}

/* ── 2. order, which is the whole reason one ring serves every producer ────────────────── */

static void test_order_across_the_wrap(void)
{
    uint32_t round, i;

    ring_reset();
    sim_consumer(100u);             /* slow enough that only the test's sim_complete() frees it */

    /* Three rounds of fill-and-drain against a capacity of eight moves both cursors past the end
     * of the array, which is where a mask index goes wrong and goes wrong silently: the first
     * frame looks right, and the picture corrupts on the pass where `tail` wraps.  Each round
     * posts a distinguishable pattern and the bus is required to show the same pattern. */
    for (round = 0u; round < 3u; round++) {
        for (i = 0u; i < 8u; i++) {
            if (ps2_gif_post(s_pkt[i], 1u + i) != 0) {
                check("a frame of exactly the capacity fits", 0);
                break;
            }
            /* Free the channel between posts, so every post this round makes is also a start:
             * that is the fast-consumer case, and the order law has to hold there too. */
            sim_complete();
            sim_tick(1u);
        }
        sim_complete();
        (void)ps2_gif_reap();
    }

    eqi("twenty-four posts put twenty-four transfers on the bus", s_seen_n, 24u);
    for (i = 1u; i < 8u; i++) {
        char label[64];
        /* Packet 0 of each round is posted first and started first; compare the positions inside
         * each round rather than the absolute index, because what is under test is the sequence. */
        snprintf(label, sizeof(label), "round keeps packet %u after %u", i, i - 1u);
        check(label, s_seen[i] == &s_pkt[i][0] && s_seen[8u + i] == &s_pkt[i][0]);
    }
    eqi("with the lengths that went with them", s_seen_qwc[7], 8u);
    check("and the ring is empty afterwards, so a wrap cannot strand a descriptor",
          ps2_gif_pending() == 0u);
}

/* ── 3. the completion check at the next yield ─────────────────────────────────────────── */

static void test_reap_drains_and_counts(void)
{
    uint32_t i, waited;
    ps2_gif_stats_t st;

    ring_reset();
    sim_consumer(100u);

    /* Post a backlog while the channel stays busy: five accepted, one on the bus, four queued. */
    for (i = 0u; i < 5u; i++) (void)ps2_gif_post(s_pkt[i], 2u);
    eqi("one transfer started while four waited", s_seen_n, 1u);
    eqi("and the four are all the ring is holding", ps2_gif_pending(), 5u);

    /* The yield.  A reap() that only drained once would leave four descriptors behind and call
     * itself done, which is the corruption the ring's buffer-lifetime law exists to prevent --
     * so this row is the one that says reap() means "empty", not "tried". */
    sim_tick(50u);
    sim_complete();
    waited = ps2_gif_reap();

    eqi("the reap ran the whole backlog out", s_seen_n, 5u);
    eqi("and left nothing behind", ps2_gif_pending(), 0u);
    check("the wait is reported to the caller", waited > 0u);
    ps2_gif_stats(&st);
    eqi("and counted once", st.waits, 1u);
    check("with a nonzero wait_us", st.wait_us > 0u);
    eqi("wait_max_us is this reap's wait", st.wait_max_us, waited);
    /* `hold` is how long a descriptor sat, measured from its own post to the moment the channel
     * took it: the fifth was queued for the whole of the simulated wait, so the worst hold has to
     * be at least the 50 us the test advanced the clock by. */
    check("hold_max_us saw the queue's own delay", st.hold_max_us >= 50u);

    /* A reap with nothing to do must not invent a wait: this is the common case -- one pass's
     * stream is long gone by the next begin() -- and a row that always printed a number would
     * make the lane look like it had not been taken. */
    ring_reset();
    waited = ps2_gif_reap();
    eqi("an empty reap waits nothing", waited, 0u);
    ps2_gif_stats(&st);
    eqi("and is not counted as a wait", st.waits, 0u);
    eqi("touching no bus at all", s_seen_n, 0u);
}

/* ── 4. the instrument covers the blocking half too ───────────────────────────────────── */

static void test_sync_note(void)
{
    ps2_gif_stats_t st;

    ring_reset();

    ps2_gif_note_sync(10u);
    ps2_gif_note_sync(70u);
    ps2_gif_note_sync(3u);
    ps2_gif_stats(&st);
    eqi("three blocking sends counted", st.sync_sends, 3u);
    eqi("their microseconds add up", st.sync_us, 83u);
    eqi("and the worst is the worst", st.sync_max_us, 70u);
}

/* ── 5. one profiling window, the way [GSTS] reads it ─────────────────────────────────── */

static void test_stats_window(void)
{
    ps2_gif_stats_t a, b;
    uint32_t i;

    ring_reset();
    sim_consumer(0u);               /* a GIF that keeps up: this is what a normal pass looks like */
    for (i = 0u; i < 4u; i++) (void)ps2_gif_post(s_pkt[i], 1u);
    ps2_gif_note_sync(9u);
    sim_complete();
    (void)ps2_gif_reap();

    ps2_gif_stats_take(&a);
    eqi("the window reports the posts it covers", a.posted, 4u);
    eqi("and the starts", a.started, 4u);
    eqi("and the live backlog, which is not a window quantity", a.queued, 0u);

    /* A second window over no traffic is the row that says the counters are per pass: a [GSTS]
     * gif: line that repeated the previous pass's numbers would look identical on screen and mean
     * the opposite. */
    ps2_gif_stats_take(&b);
    eqi("the next window has seen no posts", b.posted, 0u);
    eqi("no starts", b.started, 0u);
    eqi("no refusals", b.refused, 0u);
    eqi("no blocking sends", b.sync_sends, 0u);
    eqi("and no waits to report", b.waits, 0u);

    /* ...and the channel state survives the window, because it is not a count of the past.  The
     * GIF has to be behind here, or the post would drain to completion on its own and there would
     * be no in-flight transfer for the window to (not) clear. */
    ring_reset();
    sim_consumer(1000u);
    (void)ps2_gif_post(s_pkt[0], 1u);
    ps2_gif_stats_take(&a);
    eqi("a window does not clear an in-flight transfer", a.in_flight, 1u);
    eqi("nor the descriptor behind it", a.queued, 0u);
}

/* ── 6. the two refusals that are not S_FULL ──────────────────────────────────────────── */

static void test_degenerate_posts(void)
{
    ps2_gif_stats_t st;

    ring_reset();

    /* A null packet cannot be sent; refusing it is not a drop of anything, and it must not reach
     * the bus as address zero. */
    check("a null packet is refused", ps2_gif_post(0, 4u) != 0);
    eqi("and the channel never saw it", s_seen_n, 0u);
    eqi("with nothing queued", s_gif_n, 0u);

    /* A zero-length transfer carries no words, so it is not a piece of the picture the screen lost
     * either: accepted, and no start. */
    check("a zero-quadword post is accepted", ps2_gif_post(s_pkt[0], 0u) == 0);
    eqi("without touching the bus", s_seen_n, 0u);
    ps2_gif_stats(&st);
    eqi("and counted as neither a post nor a refusal", st.posted, 0u);
    eqi("nor a queued descriptor", st.queued, 0u);
}

/* ── 7. a descriptor's buffer outlives its transfer, as far as the ring can tell ──────── */

/* The ring cannot check the buffer-lifetime law by itself -- that is a property of who calls it,
 * and it is gated where the callers are: verify/tests/test_eegs_present.c drives the real staging
 * unit against this real ring.  What this file can pin is the half the ring owns: a descriptor that
 * has been handed to hw_start() is gone from the queue, so a producer that waits for `pending()` to
 * reach zero has waited for exactly the transfers that read its buffer and no more. */
static void test_pending_is_the_producers_wait_set(void)
{
    uint32_t i;

    ring_reset();
    /* Longer than the five busy() polls the five later posts make, so the first transfer is still
     * running when the sixth post returns and the backlog is a state the test can observe. */
    sim_consumer(20u);
    for (i = 0u; i < 6u; i++) (void)ps2_gif_post(s_pkt[i], 1u);
    eqi("six posts: one on the bus, five queued", ps2_gif_pending(), 6u);

    for (i = 0u; i < 3u; i++) {
        sim_complete();
        sim_tick(1u);
        (void)ps2_gif_drain();
    }
    eqi("three more started, so three are accounted for", s_seen_n, 4u);
    check("and pending() has not fallen below what the bus has not finished",
          ps2_gif_pending() >= 3u);

    sim_complete();
    (void)ps2_gif_reap();
    eqi("after a reap, nothing a producer owns is outstanding", ps2_gif_pending(), 0u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 LANE 10: the GIF writer ring -- bounds, order, and the\n");
    printf(" sender's own blocked time.  cap=%u descriptors\n", PS2_GIF_RING_CAP);
    printf("==========================================================\n");

    test_post_never_waits();
    test_order_across_the_wrap();
    test_reap_drains_and_counts();
    test_sync_note();
    test_stats_window();
    test_degenerate_posts();
    test_pending_is_the_producers_wait_set();

    /* Not a row about the ring but about this file: every transfer the suite started came off the
     * bus by answering polls, not by the budget forcing it.  A nonzero count would mean some test
     * asked for a consumer so slow that reap() was still looping when the budget fired -- which is
     * the hang this suite had before the sim grew a countdown, and the reason the budget exists. */
    eqi("no transfer needed the poll budget", s_deadlocked, 0u);

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the producer is refused or accepted, never blocked at the post\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the ring stopped its producer, lost order, or under-reported a wait\n");
    return 1;
}
