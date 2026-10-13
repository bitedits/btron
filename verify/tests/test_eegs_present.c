/*
 * verify/tests/test_eegs_present.c -- what reaches DMAC channel 2 for one layer, and in what order
 *
 * Increment 5c of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The three steps before
 * it were gated on words: what a vertex encodes to, what a group's tag says, what an environment
 * register holds.  This one has no new words at all -- `ps2_gs_env.c` builds the packet and
 * `backend_eegs.c` records the primitives, and both are already compared against hand literals by
 * their own suites.  What is under test here is the only thing this file can get wrong, and it is an
 * *order* plus two *counts*:
 *
 *   1. the environment goes out before the primitives, and both go out after the paint callback
 *      that produced them -- which is why the groups are staged at all;
 *   2. staging neither rewrites nor aliases them: the recorder hands out one buffer and reuses it
 *      for the next group the moment the sink returns, so a sink that remembered a pointer would
 *      present the last group twice and the first never;
 *   3. the two capacities are honoured and *counted* -- a group that does not fit is a piece of the
 *      picture that no console row would otherwise mention.
 *
 * The method for (2) is a differential rather than a literal: the same drawing is run twice, once
 * through the staging sink and once through a sink that captures straight into the test's own
 * buffer, and the two byte streams are compared.  That is the check that survives both halves of
 * the defect -- a copy that became a pointer assignment shows up here and shows up nowhere in the
 * word gates, because each group is individually correct at the instant it is written.
 *
 * `ps2_gs_gif_send()` is the bus and is stubbed; `ps2_gs_draw_env_apply()` is the seven lines that
 * live in `ps2_gs.c`, and the host copy below is those same seven lines -- build the packet, hand
 * it to the sender -- so that the ordering this suite asserts is the ordering the image produces.
 * Everything else is the shipped translation unit: the packer, the allocator, the recorder, the
 * raster stage and the present are #included, as in increments 2, 3 and 4.
 *
 * LANE 10 changed what "the bus" means here, and the suite got stronger for it.  The present no
 * longer sends, it POSTS, so the unit under test now includes the shipped ring
 * (src/drivers/ps2/ps2_gif_ring.c, compiled in as everything else in this file is) and the host
 * provides only its three hardware hooks.  What that buys is an order assertion made against the
 * structure that actually preserves order -- a FIFO with one transfer in flight -- instead of
 * against a stub that counted calls.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/drivers/ps2/ps2_gs_rec.c"
#include "../../src/drivers/ps2/ps2_gs_env.c"
#include "../../src/drivers/ps2/ps2_gif_ring.c"
#include "../../src/gl/backend_eegs.c"
#include "../../src/gl/eegs_present.c"

#include "gs_group_view.h"


static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (ok) return;
    s_failures++;
    printf("  [FAIL] %s\n", what);
}

static void eqw(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-52s got=0x%016llx want=0x%016llx\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

/* ── The bus: the ring's three hardware hooks ────────────────────────────────────────
 * The first few groups are kept whole for inspection; every start is counted, because a frame can
 * hold hundreds of groups and the counts are what the capacity checks read.
 *
 * `s_stall` is what makes this a *channel* and not a log.  At 0 a transfer reads complete on the
 * first busy() poll, which is the case the ordering and fidelity checks want: every post starts
 * immediately, so the sequence the bus saw is the sequence the present committed.  At 1 the channel
 * holds each transfer for a poll, which is the case the ring was built for -- and the row that
 * needs it is test_begin_reaps_the_last_frames_stream(): the present returns with groups still on
 * the bus, and only begin() is allowed to wait for them. */
#define KEEP 4u
static uint64_t s_keep[KEEP][GS_REC_QW_CAP * 2u];
static uint32_t s_keep_n[KEEP];
static uint32_t s_calls, s_bus_qw;
static uint32_t s_free, s_stall, s_stall_per;

int ps2_gif_hw_busy(void)
{
    if (!s_free) return 0;
    if (s_stall != 0u) { s_stall--; return 1; }
    s_free = 0u;
    return 0;
}

void ps2_gif_hw_start(const void *packet, uint32_t qwc)
{
    if (s_calls < KEEP) {
        memcpy(s_keep[s_calls], packet, GS_QW_BYTES(qwc));
        s_keep_n[s_calls] = qwc;
    }
    s_calls++;
    s_bus_qw += qwc;
    s_free = 1u;
    s_stall = s_stall_per;
}

uint32_t ps2_gif_hw_us(void)
{
    return s_bus_qw;      /* any monotone value: this suite does not gate microseconds */
}

static void dma_reset(void)
{
    memset(s_keep, 0, sizeof(s_keep));
    memset(s_keep_n, 0, sizeof(s_keep_n));
    s_calls = 0u;
    s_bus_qw = 0u;
    s_free = 0u;
    s_stall = 0u;
    s_stall_per = 0u;
}

/* The host copy of ps2_gs.c's seven lines, and of their LANE 10 half: the environment is posted,
 * not sent, because `env_pkt` is a static that the next apply overwrites and the reap at the next
 * begin() is what bounds that. */
static uint64_t s_env_pkt[(GS_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));

int ps2_gs_draw_env_apply(const gs_draw_env *env)
{
    if (gs_draw_env_packet(env, s_env_pkt) != 0) return -1;
    if (ps2_gif_post(s_env_pkt, gs_draw_env_qwc()) != 0) return -1;
    return 0;
}


/* ── Drawing ─────────────────────────────────────────────────────────────────────── */

/* The XMB's own client rect, at the size doc/txt/PS2.txt measures: 768x500 at (16, 40). */
static const RECT k_client = { 16, 40, 784, 540 };

static void vtx(eegs_vertex_t *v, float x, float y)
{
    v->x_px = x;
    v->y_px = y;
    v->s = 0.0f;
    v->t = 0.0f;
    v->rgba = 0xFF332244u;
}

static void tri(float x, float y, unsigned smooth)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    vtx(&v[0], x, y);
    vtx(&v[1], x + 8.0f, y);
    vtx(&v[2], x, y + 8.0f);
    st.smooth = smooth;
    st.textured = 0u;
    st.blended = 0u;
    eegs_triangle(v, st);
}

/* Two triangles whose state differs, so the recorder closes one group and opens another: the
 * shortest frame that has more than one group in it, which is what the aliasing check needs. */
static void draw_two_groups(void)
{
    tri(10.0f, 20.0f, 1u);
    tri(30.0f, 40.0f, 0u);
}

/* ── 1. the order on the bus ─────────────────────────────────────────────────────── */

static void test_env_then_primitives(void)
{
    int sent;

    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    sent = eegs_present_send(&k_client);

    check("one triangle, two sends: the environment and then its group", s_calls == 2u);
    check("and send() counts the staged group, not the environment", sent == 1);
    if (s_calls < 2u) return;

    check("the first is the environment packet", s_keep_n[0] == gs_draw_env_qwc());
    eqw("its tag loops the nine registers", gs_qw(s_keep[0], 0u) & 0x7FFFu, GS_ENV_NREG);
    check("the second is one triangle's group", s_keep_n[1] == gs_group_qw(3u));
    eqw("whose tag loops three vertices", gs_qw(s_keep[1], 0u) & 0x7FFFu, 3u);
}

/* The nine writes, in the order ps2_gs_env.h states as part of the design. */
static const uint64_t k_env_order[GS_ENV_NREG] = {
    GS_GP_XYOFFSET_1, GS_GP_SCISSOR_1, GS_GP_FRAME_1, GS_GP_ZBUF_1, GS_GP_TEST_1,
    GS_GP_ALPHA_1,    GS_GP_CLAMP_1,   GS_GP_TEX0_1,  GS_GP_TEX1_1
};

static void test_env_fields(void)
{
    gs_draw_env want;
    static uint64_t ref[(GS_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));
    uint32_t i;

    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    eegs_present_send(&k_client);
    if (s_calls < 2u) { check("the environment was sent", 0); return; }

    for (i = 0u; i < GS_ENV_NREG; i++)
        eqw("environment register order", gs_upper_of(s_keep[0], i + 1u), k_env_order[i]);

    /* Three of the four negotiation words are named here by hand rather than by reference,
     * because they are the words increment 5c had to find a fact for.  FRAME's literal is the
     * field table read directly: FBW[21:16] of 64-pixel units, 800 px rounded up to 13, over a
     * FBA[8:0] of 8 KB pages naming page 0, PSM[27:24] CT32 and no FMSK. */
    eqw("FRAME is the canvas page at FBW 13", gs_qw(s_keep[0], 3u), 13u << 16);
    eqw("TEST is zero: no alpha test, no depth test", gs_qw(s_keep[0], 5u), 0u);
    eqw("ALPHA is (Cs - Cd) * As + Cd", gs_qw(s_keep[0], 6u), 0x44u);
    eqw("CLAMP names no plane", gs_qw(s_keep[0], 7u), 0u);
    eqw("TEX0 names no plane", gs_qw(s_keep[0], 8u), 0u);
    eqw("TEX1 names no plane", gs_qw(s_keep[0], 9u), 0u);

    /* The whole packet against one built from the field values 5c is supposed to fill in, written
     * here in the units the struct documents: byte offsets, screen pixels, inclusive clip edges.
     * The packer is not under test -- its own suite gates its words -- so this row's information
     * is the *values*, and it says e.g. that clip_x1 is the client's right edge minus one and not
     * the window's width, or that ZBUF is the last page and not page 0, the canvas. */
    memset(&want, 0, sizeof(want));
    want.frame_byte = 0u;
    want.frame_px_w = 800u;                        /* PS2_SCREEN_WIDTH */
    want.frame_psm  = 0u;                          /* GS_PSM_CT32 */
    want.zbuf_byte  = 4186112u;                    /* 4 MB minus one 8 KB frame page */
    want.zbuf_psm   = 3u;                          /* GS_PSMZ32 */
    want.zbuf_zmsk  = 1u;
    want.win_x = 16u;
    want.win_y = 40u;
    want.clip_x0 = 16u;
    want.clip_y0 = 40u;
    want.clip_x1 = 783u;                           /* right edge 784, inclusive */
    want.clip_y1 = 539u;
    want.reg_alpha = 0x44u;
    check("the reference packet builds", gs_draw_env_packet(&want, ref) == 0);
    for (i = 0u; i < gs_draw_env_qwc(); i++)
        eqw("environment packet is the hand-filled one, quadword by quadword",
            gs_qw(s_keep[0], i), gs_qw(ref, i));
}

/* ── 2. staging copies ───────────────────────────────────────────────────────────── */

#define DIR_CAP 8u
static uint64_t s_dir[DIR_CAP][GS_REC_QW_CAP * 2u];
static uint32_t s_dir_n[DIR_CAP];
static uint32_t s_dir_i;

static void direct_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    (void)user;
    if (s_dir_i >= DIR_CAP) return;
    memcpy(s_dir[s_dir_i], qw, GS_QW_BYTES(qwc));
    s_dir_n[s_dir_i] = qwc;
    s_dir_i++;
}

static void test_staging_copies(void)
{
    uint32_t g;

    /* The reference: the same drawing, captured by a sink that copies into the test's buffer and
     * never touches the bus. */
    s_dir_i = 0u;
    memset(s_dir_n, 0, sizeof(s_dir_n));
    eegs_frame_begin(direct_sink, NULL);
    draw_two_groups();
    eegs_frame_end();
    check("the reference frame has two groups", s_dir_i == 2u);
    if (s_dir_i < 2u) return;

    dma_reset();
    eegs_present_begin();
    draw_two_groups();
    eegs_present_end();
    eegs_present_send(&k_client);

    /* env, then the two vertex groups */
    check("the staged frame put three packets on the bus", s_calls == 3u);
    if (s_calls < 3u) return;
    for (g = 0u; g < 2u; g++) {
        char label[64];
        snprintf(label, sizeof(label), "staged group %u is the recorded one, word for word", g);
        check(label, s_keep_n[1u + g] == s_dir_n[g] &&
                     memcmp(s_keep[1u + g], s_dir[g], GS_QW_BYTES(s_dir_n[g])) == 0);
    }
    /* The two groups have to differ from each other or the comparison above could be satisfied by
     * a buffer that was copied twice from the same place -- which is the defect. */
    check("and the two groups are distinguishable",
          s_dir_n[0] != s_dir_n[1] ||
          memcmp(s_dir[0], s_dir[1], GS_QW_BYTES(s_dir_n[0])) != 0);
}

/* ── 3. nothing to send ──────────────────────────────────────────────────────────── */

static void test_empty_frames(void)
{
    const RECT gone = { 0, 0, 0, 0 };

    dma_reset();
    eegs_present_begin();
    eegs_present_end();
    check("a frame that painted nothing sends nothing", eegs_present_send(&k_client) == 0);
    check("and touches the bus zero times", s_calls == 0u);

    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    check("a degenerate rect is refused", eegs_present_send(&gone) == 0);
    check("and sends nothing", s_calls == 0u);
    check("a null rect is refused", eegs_present_send(NULL) == 0);

    /* A send empties the stage: the same frame must not be put on the bus twice. */
    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    check("a real frame does go out", eegs_present_send(&k_client) == 1);
    check("and the next send has nothing left", eegs_present_send(&k_client) == 0);
    check("one environment and one group in total", s_calls == 2u);

    /* And a frame nobody sent is discarded by the next begin(), not presented later: the present
     * loop takes the layer only when the composite re-recorded it, so a leftover would draw an
     * older picture over a newer canvas. */
    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    eegs_present_begin();
    tri(150.0f, 250.0f, 1u);
    eegs_present_end();
    check("the second frame alone is what goes out", eegs_present_send(&k_client) == 1);
    check("two sends, not four", s_calls == 2u);
}

/* ── 4. the two capacities ───────────────────────────────────────────────────────── */

static void test_stage_capacity(void)
{
    eegs_present_stats st;
    uint32_t i;
    int sent;

    /* Enough same-state geometry to fill the staging buffer past its cap, counted from the cap
     * rather than from a constant that happens to exceed it today: a packed triangle is 12
     * quadwords, so one triangle per quadword of cap is twelve full stages of geometry.  Raising
     * EEGS_STAGE_QW_CAP cannot turn this row into a silent pass that way. */
    const uint32_t tris = EEGS_STAGE_QW_CAP;
    dma_reset();
    eegs_present_begin();
    for (i = 0u; i < tris; i++) tri((float)i, 20.0f, 1u);
    eegs_present_end();
    sent = eegs_present_send(&k_client);
    eegs_present_get_stats(&st);

    check("an over-full frame drops groups and says so", st.dropped > 0u);
    check("and still sends what fitted", sent > 0);
    eqw("the quadwords sent are the ones the bus counted",
        s_bus_qw - gs_draw_env_qwc(), (uint64_t)st.qw);
    check("never more than the staging buffer holds", st.qw <= EEGS_STAGE_QW_CAP);
    check("and not bailed out of early either", st.qw > EEGS_STAGE_QW_CAP / 2u);
    eqw("and send() returns the group count it measured", (uint64_t)sent, st.groups);

    /* The counters are per frame: a fresh small frame must not inherit the overflow. */
    dma_reset();
    eegs_present_begin();
    tri(100.0f, 200.0f, 1u);
    eegs_present_end();
    eegs_present_send(&k_client);
    eegs_present_get_stats(&st);
    eqw("the drop counter is reset by begin()", st.dropped, 0u);
    eqw("and a whole frame is measured as its groups", st.qw, gs_group_qw(3u));
}

static void test_group_capacity(void)
{
    eegs_present_stats st;
    uint32_t i;
    int sent;
    const uint32_t tris = EEGS_MAX_GROUPS + 44u;

    /* One group per triangle, by alternating the PRIM state every time: the vertex-count cap can
     * only be reached by groups, and this is the shape that reaches it first. */
    dma_reset();
    eegs_present_begin();
    for (i = 0u; i < tris; i++) tri((float)i, 20.0f, (i & 1u) != 0u ? 1u : 0u);
    eegs_present_end();
    sent = eegs_present_send(&k_client);
    eegs_present_get_stats(&st);

    eqw("exactly the table's worth of groups survives", st.groups, EEGS_MAX_GROUPS);
    eqw("and every one of them is sent", (uint64_t)sent, EEGS_MAX_GROUPS);
    eqw("the rest are counted, not lost", st.dropped, tris - EEGS_MAX_GROUPS);
    check("the whole frame is under the quadword cap", st.qw < EEGS_STAGE_QW_CAP);
}

/* ── 5. what the stats are for ───────────────────────────────────────────────────── */

static void test_stats(void)
{
    eegs_present_stats st;

    dma_reset();
    eegs_present_begin();
    draw_two_groups();
    eegs_present_end();
    eegs_present_send(&k_client);
    eegs_present_get_stats(&st);

    eqw("vertices recorded", st.verts, 6u);
    eqw("primitives recorded", st.prims, 2u);
    eqw("texture switches", st.tex_switches, 0u);
    eqw("textured primitives with no plane", st.tex_missing, 0u);
    eqw("recorder refusals", st.unsplittable, 0u);
}

/* ── 6. the yield: begin() is where the previous frame's stream finishes ─────────────

   LANE 10's whole trade, stated as one row.  The present commits a frame's transfers and returns
   without waiting for any of them -- that is the non-blocking half, and with the channel held open
   by `s_stall_per` it is visible as transfers on the bus that have not completed.  The next pass's
   begin() then has to empty the ring before it may write the stage again, because the stage is the
   buffer those transfers read.  A begin() that merely drained would return with a group still in
   flight and the recorder would be overwriting bytes channel 2 is reading: the picture would be
   correct on a fast consumer and corrupted exactly when the GIF fell behind, which is the class of
   defect no word gate sees. */
static void test_begin_reaps_the_last_frames_stream(void)
{
    dma_reset();
    s_stall_per = 1u;

    eegs_present_begin();
    draw_two_groups();
    eegs_present_end();
    eegs_present_send(&k_client);

    eqw("the frame put all three transfers on the bus", s_calls, 3u);
    check("and none of them had finished when the present returned", ps2_gif_pending() > 0u);

    /* The yield, and the buffer-lifetime law that makes it a wait rather than a poll. */
    eegs_present_begin();
    eqw("begin() left nothing outstanding", ps2_gif_pending(), 0u);
    eqw("the held frame is the same three transfers, not six", s_calls, 3u);

    /* The next frame starts clean, which is the part a ring that leaked a descriptor would fail:
     * its groups would go out behind a previous window's primitives. */
    draw_two_groups();
    eegs_present_end();
    eegs_present_send(&k_client);
    eqw("the following frame is its own three transfers", s_calls, 6u);
    check("and they are the only ones outstanding", ps2_gif_pending() <= 3u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 EE/GS present: order, staging fidelity and capacity\n");
    printf("   stage=%u qw  groups=%u  rec cap=%u qw  env=%u qw  ring=%u\n",
           EEGS_STAGE_QW_CAP, EEGS_MAX_GROUPS, GS_REC_QW_CAP, gs_draw_env_qwc(),
           PS2_GIF_RING_CAP);
    printf("==========================================================\n");

    test_env_then_primitives();
    test_env_fields();
    test_staging_copies();
    test_empty_frames();
    test_stage_capacity();
    test_group_capacity();
    test_stats();
    test_begin_reaps_the_last_frames_stream();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the environment precedes the stream and the stream survives staging\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the present would put the wrong words on the bus\n");
    return 1;
}
