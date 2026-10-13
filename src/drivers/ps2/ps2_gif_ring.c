/*
 * ps2_gif_ring.c — the ring's bookkeeping, with no hardware in sight
 *
 * LANE 10 (doc/txt/PS2.txt, "The lanes", row 10).  See ps2_gif_ring.h for the law, the three
 * operations, and the buffer-lifetime rule that decides which producers may post.  This file is
 * the part of that law that is checkable on a host: a FIFO of descriptors, one flag for the
 * transfer channel 2 is running, and the counters the [GSTS] gif: row prints.  It reads no
 * memory-mapped register and calls nothing but its three hooks, so verify/tests/test_ps2_gif_ring.c
 * can hold the channel open, or refuse to ever free it, and watch what the producer does -- the
 * positive controls LAW 16d asks for, neither of which is possible against a register.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gif_ring.h"

typedef struct {
    const void *packet;
    uint32_t    qwc;
    uint32_t    posted_us;
} gif_desc_t;

/* The ring itself, and the three cursors that make it one: `s_head` is the descriptor channel 2
 * will take next, `s_tail` is where the next post lands, and `s_n` says how many are between them.
 * `s_n` is kept separate from (tail - head) so an empty ring and a full one are different states
 * rather than the same index twice -- which is the only reason a capacity of 512 really holds 512
 * descriptors and the refusal path is reachable at all. */
static gif_desc_t s_ring[PS2_GIF_RING_CAP];
static uint32_t s_head, s_tail, s_n;

/* 1 from the moment a transfer is started until something has read channel 2's STR and found it
 * clear.  Only ever one, because the hardware takes one transfer at a time. */
static uint32_t s_in_flight;

static ps2_gif_stats_t s_st;

/* The ring indexes by mask, not by modulo: PS2_GIF_RING_CAP is a power of two precisely so that
 * the post path costs one AND, because `rem` on a R5900 is a subroutine call and this is the path
 * the lane exists to make cheap. */
#if (PS2_GIF_RING_CAP & (PS2_GIF_RING_CAP - 1u)) != 0u
#error "PS2_GIF_RING_CAP must be a power of two -- the ring indexes s_head/s_tail by mask"
#endif
#define PS2_GIF_RING_MASK (PS2_GIF_RING_CAP - 1u)

uint32_t ps2_gif_drain(void)
{
    uint32_t started = 0u;

    for (;;) {
        /* Something is on the bus.  It stays "in flight" until a busy() read says otherwise, and
         * only this function may start a second transfer -- so the order the EE chose is the
         * order the GIF sees, which is what lets a layer's environment precede its primitives. */
        if (s_in_flight) {
            if (ps2_gif_hw_busy()) break;
            s_in_flight = 0u;
        }
        if (s_n == 0u) break;

        {
            gif_desc_t *d = &s_ring[s_head];
            const uint32_t hold = ps2_gif_hw_us() - d->posted_us;

            if (hold > s_st.hold_max_us) s_st.hold_max_us = hold;
            ps2_gif_hw_start(d->packet, d->qwc);
            s_in_flight = 1u;
            s_head = (s_head + 1u) & PS2_GIF_RING_MASK;
            s_n--;
            s_st.started++;
            started++;
        }
    }

    return started;
}

int ps2_gif_post(const void *packet, uint32_t qwc)
{
    if (packet == 0) return 1;

    /* A zero-quadword transfer is not a piece of the picture, so it is not a piece of the picture
     * that got dropped either: accepted, and the channel is not asked to do nothing. */
    if (qwc == 0u) return 0;

    if (s_n >= PS2_GIF_RING_CAP) {
        /* S_FULL, the §3.2 answer: refuse, count it, and let the producer go on.  The words this
         * descriptor would have carried are lost from this pass and reappear at the next one,
         * which is what a drop count is for -- it is a receipt, not an error handler. */
        s_st.refused++;
        return 1;
    }

    s_ring[s_tail].packet = packet;
    s_ring[s_tail].qwc = qwc;
    s_ring[s_tail].posted_us = ps2_gif_hw_us();
    s_tail = (s_tail + 1u) & PS2_GIF_RING_MASK;
    s_n++;
    s_st.posted++;

    /* Not a spin: start it now if the channel happens to be free, and return either way.  This is
     * the difference between this line and the old sender -- the EE asks, and if the answer is "the
     * GIF is busy" the answer costs nothing. */
    (void)ps2_gif_drain();
    return 0;
}

uint32_t ps2_gif_reap(void)
{
    const uint32_t t0 = ps2_gif_hw_us();
    uint32_t waited;

    /* Drain, wait the one transfer still running, drain again, until the ring is empty AND nothing
     * is in flight.  A caller reaps because it is about to write over a buffer a queued descriptor
     * points into, so "left one queued" would be the corruption this whole design is avoiding. */
    for (;;) {
        (void)ps2_gif_drain();
        if (s_n == 0u && !s_in_flight) break;
        while (ps2_gif_hw_busy())
            ;
        s_in_flight = 0u;
    }

    waited = ps2_gif_hw_us() - t0;
    if (waited != 0u) {
        s_st.waits++;
        s_st.wait_us += waited;
        if (waited > s_st.wait_max_us) s_st.wait_max_us = waited;
    }
    return waited;
}

uint32_t ps2_gif_pending(void)
{
    return s_n + s_in_flight;
}

void ps2_gif_stats(ps2_gif_stats_t *out)
{
    if (out == 0) return;
    s_st.queued = s_n;
    s_st.in_flight = s_in_flight;
    s_st.cap = PS2_GIF_RING_CAP;
    *out = s_st;
}

void ps2_gif_stats_take(ps2_gif_stats_t *out)
{
    ps2_gif_stats(out);
    if (out == 0) return;

    /* One profiling window, the same shape btron_render_stats_take() gives the compositor's
     * columns: the counts become "this pass" and the maxima become "this pass's worst", which is
     * the only form in which a budget can be read against them.  `queued`, `in_flight` and `cap`
     * are live state rather than a window's, so they are reported and left alone. */
    s_st.posted = s_st.started = s_st.refused = 0u;
    s_st.waits = s_st.wait_us = s_st.wait_max_us = 0u;
    s_st.hold_max_us = 0u;
    s_st.sync_sends = s_st.sync_us = s_st.sync_max_us = 0u;
}

void ps2_gif_note_sync(uint32_t waited_us)
{
    s_st.sync_sends++;
    s_st.sync_us += waited_us;
    if (waited_us > s_st.sync_max_us) s_st.sync_max_us = waited_us;
}
