/*
 * ps2_gif_ring.h — the bounded, non-blocking writer ring in front of DMAC channel 2
 *
 * LANE 10 of the plan of record (doc/txt/PS2.txt, "The lanes", row 10).  The law it exists for is
 * VIOLATION 2 and it is not "be faster": it is S_FULL, the InterCore rule that a producer must not
 * be stopped by its consumer (doc/txt/ASYNC.txt §0.1, doc/txt/INTERPRETER.txt §3.2).  Until now the
 * presentation plane's producer was `ps2_gs_gif_send()`, which writes channel 2's CHCR and then
 * spins on its STR bit until the transfer has left the EE -- so the EE's one writer cursor on the
 * GIF was, by construction, held open by the GIF.
 *
 * What a ring means here, precisely.  Channel 2 carries one transfer at a time: a CHCR write while
 * STR is set is not queued by the hardware, it is ignored, so "queue the DMA" cannot mean "issue
 * many starts".  It means the EE keeps the *pending list* and the *waiting* apart:
 *
 *   post()   appends a descriptor and starts it if, and only if, the channel is free.  It never
 *            spins.  At capacity it refuses and counts the refusal -- the drop receipt this port
 *            already prints for the recorder (`drop=`/`coverdrop=` in [GSTS]) and the only
 *            legitimate answer a full consumer gets from a §3.2 producer.
 *   drain()  hands the channel as many queued descriptors as it will take, and returns while one
 *            is still in flight.  Also never spins.
 *   reap()   the completion check, called at the NEXT yield rather than at the post: it drains,
 *            and if a transfer is still running it waits for that one and counts the wait.
 *
 * The three together are the lane's deliverable: a bounded capacity, a drop count, and a
 * completion check at the next yield.  The fourth thing row 10 asks for is the instrument, and it
 * is `wait_us`/`hold_max_us`/`sync_us`: the sender's own blocked time, which is the number the
 * lane was opened without.
 *
 * The buffer-lifetime law, which is what makes this ring narrow on purpose.  A DMA reads its
 * packet while the EE is doing something else, so a descriptor's buffer must not be written again
 * until its transfer has been reaped.  That is true of the staged stream and of nothing else in
 * this driver: `setup_pkt` and `img_tag` in ps2_gs.c are statics reused by the very next chunk,
 * and the canvas is written by every console character.  So those producers stay synchronous --
 * `ps2_gs_gif_send()`, which now counts its own spin into `sync_us` instead of hiding it.  Two
 * producers post, and each is safe for its own reason: `src/gl/eegs_present.c`, whose staging
 * buffer has exactly one writer, is not touched between `send()` and the next `begin()`, and
 * calls `reap()` at that `begin()`; and `ps2_gs_draw_env_apply()`, whose `env_pkt` is overwritten
 * only by the next apply, which cannot happen before that same reap.  A posted packet is
 * therefore alive for one paint pass and no longer, which is the whole reason this seam was cut
 * where it was cut rather than at the register level.
 *
 * Completion here is DMAC's STR bit: it says the quadwords left the EE and reached the GIF, not
 * that the GS has finished drawing them.  That is the same boundary the synchronous sender has
 * always had, and the reason a layer's stream still follows its canvas band by program order
 * rather than by a flag.
 *
 * The ring is pure bookkeeping and touches no memory-mapped register: the three hooks at the
 * bottom of this file are the seam, and ps2_gs.c is the only thing in the image that implements
 * them.  Which is what lets verify/tests/test_ps2_gif_ring.c drive a full channel, an empty
 * channel and an over-capacity producer on a machine with no PS2 in it.
 *
 * Zero proprietary Sony SDK code: the CHCR/STR behaviour is ps2_gs.c's register table.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GIF_RING_H
#define PS2_GIF_RING_H

#include <stdint.h>

/* Transfers the ring can own at once: 512, which is "a frame's worth, the environment that has to
 * precede it, and 255 to spare", rounded up to a power of two because the ring indexes its cursors
 * by mask and a `rem` on a R5900 is a subroutine call on the very path this lane exists to make
 * cheap.  Sized from the producer that posts: src/gl/eegs_present.c puts one transfer per recorded
 * group on the bus, its group table is EEGS_MAX_GROUPS = 256, and one more carries the drawing
 * environment -- so the capacity cannot be reached by a legal frame, which is what makes `refused=`
 * mean what it says: the consumer fell behind, not the producer asked for too much.  It bounds the
 * *outstanding* transfers, queued plus the one on the bus, because the quantity the ring is
 * accountable for is other people's buffers.  It is 6,144 B of .bss against the 3,830,272 B this
 * image has.  Overridden by the host gate, which needs to reach capacity to prove the refusal path
 * exists at all; eegs_present.c ties this number to the group table it was derived from, at compile
 * time, through the repo's `typedef char name[(cond) ? 1 : -1]` idiom -- the image is -std=c99, so
 * _Static_assert is not available to it. */
#ifndef PS2_GIF_RING_CAP
#define PS2_GIF_RING_CAP 512u
#endif

/* Everything the lane asks to know, and everything [GSTS] gif: prints.  The counters are
 * monotone since boot except the two marked `now`, which describe this instant. */
typedef struct {
    uint32_t posted;        /* descriptors accepted by post() */
    uint32_t started;       /* transfers the ring handed to channel 2 */
    uint32_t refused;       /* S_FULL: post() found the ring at capacity */
    uint32_t queued;        /* now: accepted, not yet started */
    uint32_t in_flight;     /* now: 1 while a started transfer has STR set */
    uint32_t cap;           /* PS2_GIF_RING_CAP, so a row names the bound it hit */
    uint32_t waits;         /* reap() calls that had to wait for the channel */
    uint32_t wait_us;       /* total µs the presentation plane spent waiting at a reap */
    uint32_t wait_max_us;   /* the longest single such wait */
    uint32_t hold_max_us;   /* longest a descriptor sat queued before its transfer started */
    uint32_t sync_sends;    /* ps2_gs_gif_send() calls: the transfers that still block */
    uint32_t sync_us;       /* total µs spent inside those spins */
    uint32_t sync_max_us;   /* the longest single such spin */
} ps2_gif_stats_t;

/* Append one packet and start it if the channel is free.  `qwc` is quadwords, the unit DMAC QWC
 * takes, and must be > 0.  Returns 0 when the packet was accepted and 1 when the ring was full --
 * a refusal is a dropped piece of the picture, counted, and the caller keeps going.  Never waits
 * for the consumer, which is the entire point. */
int ps2_gif_post(const void *packet, uint32_t qwc);

/* Hand the channel every queued descriptor it will take.  Returns how many were started. */
uint32_t ps2_gif_drain(void);

/* The completion check at the next yield: drain, then wait out the transfer still running.  The
 * µs spent waiting are both returned and counted into wait_us/wait_max_us.  With an empty ring and
 * an idle channel this costs one register read and returns 0. */
uint32_t ps2_gif_reap(void);

/* How many transfers the ring has accepted and not seen complete: queued plus the one in flight,
 * which is the quantity PS2_GIF_RING_CAP bounds and therefore the number a producer that intends to
 * rewrite a buffer waits on. */
uint32_t ps2_gif_pending(void);

void ps2_gif_stats(ps2_gif_stats_t *out);

/* The same snapshot as one profiling window: everything the caller gets is "since the last time
 * this was asked", maxima included, which is the form a budget is checked against.  `queued`,
 * `in_flight` and `cap` describe this instant and are neither differenced nor cleared.  One
 * profile per [GSTS] row is why this exists; the shape is btron_render_stats_take()'s. */
void ps2_gif_stats_take(ps2_gif_stats_t *out);

/* One synchronous ps2_gs_gif_send() is over, and it waited `waited_us`.  Called by ps2_gs.c, not
 * by a producer, and only so the ring's own numbers can be read against what the blocking path
 * still costs -- the lane's instrument covers both halves of the bus. */
void ps2_gif_note_sync(uint32_t waited_us);

/* ── The seam: what the platform owes the ring ─────────────────────────────────────────
 * Declared here and defined in ps2_gs.c for the image, and in the host gate's own file for the
 * suite.  The ring calls busy() before every start and never issues one while it answers 1. */
int  ps2_gif_hw_busy(void);
void ps2_gif_hw_start(const void *packet, uint32_t qwc);
uint32_t ps2_gif_hw_us(void);

#endif /* PS2_GIF_RING_H */
