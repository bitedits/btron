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

/* LANE 12h's invariance instrument is compiled into this suite unconditionally: the same dial that
 * turns it on for a bench run (`PS2_IDENT=1`) turns it off for the shipped image, and a suite that
 * left it off would be gating a TU whose instrument branch never compiled. */
#define BTRON_PS2_GS_IDENT 1

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/drivers/ps2/ps2_gs_rec.c"
#include "../../src/drivers/ps2/ps2_gs_env.c"
#include "../../src/drivers/ps2/ps2_gif_ring.c"
#include "../../src/gl/backend_eegs.c"
#include "../../src/gl/eegs_present.c"

#include "gs_group_view.h"

/* `backend_eegs.c` now carries `eegs_tex_quads()`, which asks the GS registry through
 * `gs_tex_find()` -- and the registry lives in `ps2_gs_tex.c`, a TU that cannot join this one: its
 * file-static `s_n` and `s_sink` collide with `ps2_gs_rec.c`'s own of the same names.  This suite's
 * subject is the order and counts of a present, and it never calls the quad entry, so the registry
 * simply has nothing in it here: 0 for every key, which is the same answer a frame gets on the
 * machine before any plane has been uploaded -- the demoted flat draw, not somebody else's page. */
const gs_tex_t *gs_tex_find(const void *plane)
{
    (void)plane;
    return (const gs_tex_t *)0;
}


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

static uint64_t s_dual_env_pkt[(GS_DUAL_ENV_NREG + 1u) * 2u] __attribute__((aligned(16)));

int ps2_gs_draw_dual_env_apply(const gs_draw_env *ctx1, const gs_draw_env *ctx2)
{
    if (gs_draw_dual_env_packet(ctx1, ctx2, s_dual_env_pkt) != 0) return -1;
    if (ps2_gif_post(s_dual_env_pkt, gs_draw_dual_env_qwc()) != 0) return -1;
    return 0;
}


/* ── Drawing ─────────────────────────────────────────────────────────────────────── */

/* The XMB's own client rect, at the size doc/txt/PS2.txt measures: 768x500 at (16, 40). */
static const RECT k_client = { 16, 40, 784, 540 };

/* Whole pixels, because that is the unit the stage is handed: `eegs_vertex_t`'s x_px/y_px are
 * GLint after LANE 12a, and the only fraction on this path is a texel coordinate, which is `s`/`t`
 * and is set inside vtx() rather than passed to it. */
static void vtx(eegs_vertex_t *v, int32_t x_px, int32_t y_px)
{
    v->x_px = x_px;
    v->y_px = y_px;
    v->s = 0.0f;
    v->t = 0.0f;
    v->rgba = 0xFF332244u;
}

static void tri(int32_t x_px, int32_t y_px, unsigned smooth)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    vtx(&v[0], x_px, y_px);
    vtx(&v[1], x_px + 8, y_px);
    vtx(&v[2], x_px, y_px + 8);
    st.smooth = smooth;
    st.textured = 0u;
    st.blended = 0u;
    eegs_triangle(v, st);
}

/* Two triangles whose state differs, so the recorder closes one group and opens another: the
 * shortest frame that has more than one group in it, which is what the aliasing check needs. */
static void draw_two_groups(void)
{
    tri(10, 20, 1u);
    tri(30, 40, 0u);
}

/* A triangle whose colour is named rather than fixed, for the invariance instrument's colour-only
 * case: `vtx()` gives every other drawing the same rgba, so a hash that folded only the drawing kick
 * would be invisible to everything except this. */
static void tri_rgba(int32_t x_px, int32_t y_px, unsigned smooth, uint32_t rgba)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    vtx(&v[0], x_px, y_px);
    vtx(&v[1], x_px + 8, y_px);
    vtx(&v[2], x_px, y_px + 8);
    v[0].rgba = v[1].rgba = v[2].rgba = rgba;
    st.smooth = smooth;
    st.textured = 0u;
    st.blended = 0u;
    eegs_triangle(v, st);
}

/* ── 1. the order on the bus ─────────────────────────────────────────────────────── */

static void test_env_then_primitives(void)
{
    int sent;

    dma_reset();
    eegs_present_begin();
    tri(100, 200, 1u);
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
    tri(100, 200, 1u);
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
    tri(100, 200, 1u);
    eegs_present_end();
    check("a degenerate rect is refused", eegs_present_send(&gone) == 0);
    check("and sends nothing", s_calls == 0u);
    check("a null rect is refused", eegs_present_send(NULL) == 0);

    /* A send empties the stage: the same frame must not be put on the bus twice. */
    dma_reset();
    eegs_present_begin();
    tri(100, 200, 1u);
    eegs_present_end();
    check("a real frame does go out", eegs_present_send(&k_client) == 1);
    check("and the next send has nothing left", eegs_present_send(&k_client) == 0);
    check("one environment and one group in total", s_calls == 2u);

    /* And a frame nobody sent is discarded by the next begin(), not presented later: the present
     * loop takes the layer only when the composite re-recorded it, so a leftover would draw an
     * older picture over a newer canvas. */
    dma_reset();
    eegs_present_begin();
    tri(100, 200, 1u);
    eegs_present_end();
    eegs_present_begin();
    tri(150, 250, 1u);
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
    for (i = 0u; i < tris; i++) tri((int32_t)i, 20, 1u);
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
    tri(100, 200, 1u);
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
    for (i = 0u; i < tris; i++) tri((int32_t)i, 20, (i & 1u) != 0u ? 1u : 0u);
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

/* ── 7. LANE 12h's invariance instrument ─────────────────────────────────────────────

   The question the lane asks is whether a frame's recorded stream is the previous frame's byte
   string, because the answer picks the rung: an invariant span can be re-sent where it sits (a
   replay), and a varying one can only be produced faster (a textured batch map).  The instrument is
   a per-group hash folded at the sink and carried one presented frame forward -- LANE 10's buffer-
   lifetime law says the words themselves may not be kept -- so what this section can gate is the
   *carry and the reading*, and that is what it asserts:

     - a repeat of the same drawing reports every group and every quadword identical, with an empty
       `at=`;
     - a moved vertex reports its own group only, by index, and the whole length of it;
     - a changed colour with the geometry untouched is still a difference, which is the assertion
       that the hash folds the group's four registers rather than the drawing kick alone;
     - a frame that gained or lost a group puts the unmatched groups in `tail=` on BOTH sides, and
       compares only the aligned prefix -- the positional rule a replay is bound by;
     - a frame that staged nothing does not consume its predecessor, because the comparison is
       between presented frames;
     - `at=` names the first eight differences and counts the rest rather than dropping them;
     - `eegs_present_groups_staged()` counts groups the sink has RECEIVED, i.e. the ones already
       closed, which is what the app's phase markers attribute a group index against;
     - and, in 7b, the same compare split by GS register: the drawing kick, the colour and the texel
       coordinates each credited only where their own quadwords are byte-identical, with AD groups
       and the PRIM word credited by neither.

   The instrument compares against whatever was presented before, so every case here presents its
   own predecessor first and reads the frame after it: the tests are then independent of the suite's
   history rather than depending on the order the sections run in. */

static void frame_of(void (*draw)(void), eegs_present_stats *st)
{
    eegs_present_begin();
    draw();
    eegs_present_end();
    eegs_present_send(&k_client);
    eegs_present_get_stats(st);
}

static void draw_two_groups_moved(void)
{
    tri(11, 20, 1u);          /* one pixel on x: the first group's XYZ2 word moves */
    tri(30, 40, 0u);
}

/* The same two groups with a third in FRONT of them: every one of them is present in both frames,
 * and none of them is at the index it was, which is what the positional rule refuses to credit. */
static void draw_shifted_right(void)
{
    tri(5, 5, 0u);
    tri(10, 20, 1u);
    tri(30, 40, 0u);
}

/* Same coordinates, one colour bit different: nothing the geometry sees. */
static void draw_colour_a(void) { tri_rgba(10, 20, 1u, 0xFF332244u); }
static void draw_colour_b(void) { tri_rgba(10, 20, 1u, 0xFF332245u); }

/* `s_n_tris` groups -- the state alternates, so each triangle closes its own -- of which the first
 * `s_n_moved` are shifted one pixel between two frames. */
static uint32_t s_n_tris = 12u, s_n_moved;

static void draw_n_groups(void)
{
    uint32_t i;

    for (i = 0u; i < s_n_tris; i++)
        tri((int32_t)(10u + i * 5u + ((i < s_n_moved) ? 1u : 0u)), 20, (i & 1u) != 0u ? 1u : 0u);
}

static void test_ident_invariant(void)
{
    eegs_present_stats a, b;

    dma_reset();
    frame_of(draw_two_groups, &a);            /* establish the predecessor */
    frame_of(draw_two_groups, &b);

    check("the second presentation is the frame after the first", b.ident_frames == a.ident_frames + 1u);
    eqw("every group matches the same-index group before it", b.same_groups, b.groups);
    eqw("and every quadword with it", b.same_qw, b.qw);
    eqw("nothing differed", b.diff_groups, 0u);
    eqw("nothing was left over", b.tail_groups, 0u);
    eqw("the predecessor's group count is on the row", b.ident_groups_prev, 2u);
    check("and the difference list is empty", eegs_present_ident_at()[0] == '\0');
}

static void test_ident_moved_vertex(void)
{
    eegs_present_stats b;

    dma_reset();
    frame_of(draw_two_groups, NULL);
    frame_of(draw_two_groups_moved, &b);

    eqw("one group differs", b.diff_groups, 1u);
    eqw("the other is untouched", b.same_groups, 1u);
    eqw("it is the first one", b.diff_first, 0u);
    eqw("and only the first", b.diff_last, 0u);
    eqw("a differing group counts its whole length", b.diff_qw, gs_group_qw(3u));
    check("at= names its index", strcmp(eegs_present_ident_at(), "0") == 0);
}

static void test_ident_colour_only(void)
{
    eegs_present_stats b;

    dma_reset();
    frame_of(draw_colour_a, NULL);
    frame_of(draw_colour_b, &b);

    eqw("a colour bit with the geometry still identical is a difference", b.diff_groups, 1u);
    eqw("nothing reads as identical", b.same_groups, 0u);
    eqw("and the quadwords of that group are all charged to it", b.diff_qw, gs_group_qw(3u));
}

static void test_ident_positional_tail(void)
{
    eegs_present_stats b;

    /* Gained a group: the aligned prefix still matches, the extra one is a tail. */
    dma_reset();
    frame_of(draw_two_groups, NULL);
    s_n_tris = 2u; s_n_moved = 0u;
    frame_of(draw_n_groups, NULL);
    s_n_tris = 3u; s_n_moved = 0u;
    frame_of(draw_n_groups, &b);
    eqw("the two aligned groups match", b.same_groups, 2u);
    eqw("the third is a tail, not a match", b.tail_groups, 1u);
    eqw("and its quadwords are counted", b.tail_qw, gs_group_qw(3u));

    /* Lost one: the tail is on the other side and must still be reported, because a replay of a
     * span that is not there is exactly the mistake the positional rule exists to catch. */
    s_n_tris = 3u; s_n_moved = 0u;
    frame_of(draw_n_groups, NULL);
    s_n_tris = 2u; s_n_moved = 0u;
    frame_of(draw_n_groups, &b);
    eqw("a frame that lost a group reports its tail too", b.tail_groups, 1u);
    eqw("and compares only the aligned prefix", b.same_groups + b.diff_groups, 2u);

    /* An inserted group shifts everything after it, so the shift reads as differences rather than
     * as a match: that is the cost a replay pays, and it is not allowed to look free. */
    s_n_tris = 2u; s_n_moved = 0u;
    frame_of(draw_n_groups, NULL);
    frame_of(draw_shifted_right, &b);
    eqw("a span that moved index is not credited as identical", b.same_groups, 0u);
    eqw("both aligned groups differ", b.diff_groups, 2u);
    eqw("and the group only this frame has is the tail", b.tail_groups, 1u);
}

static void test_ident_empty_frame_keeps_predecessor(void)
{
    eegs_present_stats b;
    int sent;

    dma_reset();
    frame_of(draw_two_groups, NULL);

    eegs_present_begin();
    eegs_present_end();
    sent = eegs_present_send(&k_client);
    check("a frame that staged nothing sends nothing", sent == 0);

    frame_of(draw_two_groups, &b);
    eqw("the next frame still compares with the one before the gap", b.same_groups, 2u);
    eqw("nothing became a tail", b.tail_groups, 0u);
}

static void test_ident_at_list(void)
{
    eegs_present_stats b;

    dma_reset();
    s_n_tris = 12u; s_n_moved = 0u;
    frame_of(draw_n_groups, NULL);
    s_n_moved = 10u;
    frame_of(draw_n_groups, &b);

    eqw("ten of the twelve groups differ", b.diff_groups, 10u);
    eqw("two match", b.same_groups, 2u);
    check("at= names the first eight and counts the rest",
          strcmp(eegs_present_ident_at(), "0,1,2,3,4,5,6,7+2") == 0);
    eqw("the remainder is its own column", b.diff_more, 2u);
    eqw("first is the first difference", b.diff_first, 0u);
    eqw("last is the ninth", b.diff_last, 9u);
}

static void test_staged_group_count(void)
{
    dma_reset();
    eegs_present_begin();
    eqw("a fresh frame has staged nothing", eegs_present_groups_staged(), 0u);
    tri(10, 20, 1u);
    eqw("an open group is not staged yet", eegs_present_groups_staged(), 0u);
    tri(30, 40, 0u);
    eqw("the state change closed the first", eegs_present_groups_staged(), 1u);
    eegs_present_end();
    eqw("and end() closes the last", eegs_present_groups_staged(), 2u);
}

/* Same coordinates, same colour, a different smoothing bit: only the group's PRIM words move.  PRIM
 * is folded into the whole-group hash and into no register lane, which is the residual the three
 * lanes leave deliberately -- a group that `diff=` reports while all three say invariant had its
 * primitive descriptor or its loop count move, not its picture. */
static void draw_prim_a(void) { tri(10, 20, 1u); }
static void draw_prim_b(void) { tri(10, 20, 0u); }

/* One texture switch between two triangles, i.e. vertex group, AD group, vertex group.  The switch
 * is `eegs_texture()`'s own three-register AD group, so the frame has a group with no four-register
 * body in it -- the case the lane comparison has to exclude rather than guess about. */
static void draw_with_switch(void)
{
    gs_tex_t tex;

    memset(&tex, 0, sizeof(tex));
    tex.plane = (const void *)1u;
    tex.clamp = 0x11u;
    tex.tex0 = 0x22u;
    tex.tex1 = 0x33u;

    tri(10, 20, 1u);
    eegs_texture(&tex);
    tri(30, 40, 0u);
}

/* ── 7b. the same comparison split by GS register ────────────────────────────────────

   `diff=` says a frame is not its own predecessor; only the three lanes say WHY, and the why is what
   picks the file to change: an invariant XYZ2 with a moving RGBAQ is the same mesh re-shaded and
   wants a colour map, while a moving XYZ2 is the vertex transform being re-solved.  The four cases
   below are chosen so that each lane is the one that moves in exactly one of them, which is also the
   control on the lane-to-register mapping: fold one field one quadword out of place -- the tag's
   quadword carries the REGS list, so the body starts at quadword 1 and a mapping off by one is the
   natural error -- and the moved-vertex and colour-only rows trade answers. */
/* Same two groups with the FIRST one translated by a pixel -- the deltas between its three vertices
 * are unchanged, which is the whole point of the `shape=` lane -- and, in the scaled variant, the
 * same three vertices with one of them moved 1 px further in y: the deltas change, so the group is a
 * different mesh rather than the same mesh in a new place. */
static void draw_two_groups_moved_shape(void)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    vtx(&v[0], 11, 20);
    vtx(&v[1], 19, 20);
    vtx(&v[2], 11, 29);          /* the y delta is 9 here and 8 in `tri()` */
    st.smooth = 1u;
    st.textured = 0u;
    st.blended = 0u;
    eegs_triangle(v, st);
    tri(30, 40, 0u);
}

/* One group whose three vertices are the same shape a pixel to the right. */
static void draw_one_translated(void)
{
    eegs_vertex_t v[3];
    eegs_tri_state_t st;

    vtx(&v[0], 11, 20);
    vtx(&v[1], 19, 20);
    vtx(&v[2], 11, 28);
    st.smooth = 1u;
    st.textured = 0u;
    st.blended = 0u;
    eegs_triangle(v, st);
}

static void test_ident_lanes(void)
{
    eegs_present_stats b;

    /* One vertex moved, nothing else: its own group's kick differs, its colour and texels do not. */
    dma_reset();
    frame_of(draw_two_groups, NULL);
    frame_of(draw_two_groups_moved, &b);
    eqw("two vertex groups, one of them moved", b.diff_groups, 1u);
    eqw("the frame's own vertex-group count", b.ident_vertex_groups, 2u);
    eqw("the untouched group is the only one the drawing kick credits", b.ident_xyz, 1u);
    eqw("while the colour lane still counts both", b.ident_rgb, 2u);
    eqw("and so does the texel lane", b.ident_st, 2u);
    eqw("and BOTH are shape-invariant, because a translated mesh is the same mesh",
        b.ident_shape, 2u);

    /* The mirror image: one colour bit, the geometry byte-identical. */
    dma_reset();
    frame_of(draw_colour_a, NULL);
    frame_of(draw_colour_b, &b);
    eqw("a colour-only frame still differs as a group", b.diff_groups, 1u);
    eqw("and its colour lane is the one that counts nothing", b.ident_rgb, 0u);
    eqw("the drawing kick is invariant", b.ident_xyz, 1u);
    eqw("and the texels with it", b.ident_st, 1u);
    eqw("and the shape with them", b.ident_shape, 1u);

    /* Only PRIM moved: the whole-group hash sees it and no lane does. */
    dma_reset();
    frame_of(draw_prim_a, NULL);
    frame_of(draw_prim_b, &b);
    eqw("a moved primitive descriptor is a difference", b.diff_groups, 1u);
    eqw("while all three lanes call the group invariant", b.ident_xyz, 1u);
    eqw("the colour lane included", b.ident_rgb, 1u);
    eqw("and the texel lane", b.ident_st, 1u);
    eqw("and the shape lane", b.ident_shape, 1u);

    /* An AD group folds no lane, so three identical groups give two lane credits -- and `v=` is the
     * column that lets the reader see that, rather than infer it from `texsw=`. */
    dma_reset();
    frame_of(draw_with_switch, NULL);
    frame_of(draw_with_switch, &b);
    eqw("the switched frame repeats itself wholly", b.same_groups, 3u);
    eqw("it is three groups: two vertex and one AD", b.diff_groups, 0u);
    eqw("and the frame says so", b.ident_vertex_groups, 2u);
    eqw("and each lane counts only the two vertex groups", b.ident_xyz, 2u);
    eqw("the colour lane likewise", b.ident_rgb, 2u);
    eqw("the texel lane likewise", b.ident_st, 2u);
    eqw("the shape lane likewise", b.ident_shape, 2u);

    /* The pair the lane exists for, on a group of three vertices rather than a lone triangle: a
     * TRANSLATION is invariant in `shape=` and differs in `xyz=`, and a RESHAPE differs in both.
     * These two rows are what makes "the same mesh moved" a reading instead of a guess. */
    dma_reset();
    frame_of(draw_two_groups, NULL);
    frame_of(draw_one_translated, &b);
    eqw("one group, translated", b.diff_groups, 1u);
    eqw("its coordinates moved", b.ident_xyz, 0u);
    eqw("its shape did not", b.ident_shape, 1u);

    dma_reset();
    frame_of(draw_one_translated, NULL);
    frame_of(draw_two_groups_moved_shape, &b);
    eqw("a reshaped group is a difference", b.diff_groups, 1u);
    eqw("its coordinates moved", b.ident_xyz, 0u);
    eqw("and so did its shape -- the delta between two of its vertices changed", b.ident_shape, 0u);
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
    test_ident_invariant();
    test_ident_moved_vertex();
    test_ident_colour_only();
    test_ident_positional_tail();
    test_ident_empty_frame_keeps_predecessor();
    test_ident_at_list();
    test_staged_group_count();
    test_ident_lanes();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the environment precedes the stream and the stream survives staging\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - the present would put the wrong words on the bus\n");
    return 1;
}
