/*
 * eegs_present.c — the staged GIF stream, and the two DMA bursts that follow an upload
 *
 * Increment 5c of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  See eegs_present.h
 * for the ordering rule this file exists to obey.  What is only visible here is the staging
 * buffer's own arithmetic: how many quadwords a frame costs, and what happens to a group that
 * does not fit.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "eegs_present.h"

#include "backend_eegs.h"
#include "../drivers/ps2/ps2_gif_ring.h"
#include "../drivers/ps2/ps2_gs.h"
#include "../drivers/ps2/ps2_gs_env.h"
#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_rec.h"
#include "../drivers/ps2/ps2_gs_vram.h"

/* The shipped XMB frame measures 7,434 vertices and 51 groups, which is 7,434 x 4 + 51 = 29,787
 * quadwords (doc/txt/PS2.txt, increment 5c's bench row).  A previous note here claimed a frame
 * records about 13,700, and the stage was built to fit a frame and a half of that; the first
 * image that actually presented through the GS said otherwise with `drop=42` on every frame,
 * because the stage held the first nine groups and refused the rest -- a partial picture with a
 * number attached to it.  65,536 quadwords is a frame with headroom for a second window's worth
 * of geometry, at 1 MiB of .bss against a machine with 32.  The bound is not provable from the
 * app, so the counter is the design: `drop` is the row a bench reads, and a non-zero one means
 * this port has met a frame it cannot put on the bus in one pass. */
#define EEGS_STAGE_QW_CAP 65536u

/* One group per recorder send: the vertex groups are bounded by GS_REC_QW_CAP, and a texture
 * switch is its own group of four quadwords.  256 is ~50x the measured frame's group count. */
#define EEGS_MAX_GROUPS 256u

/* LANE 10's bound, stated where both halves of it are visible.  The ring has to hold one whole
 * frame's transfers -- a group each, plus the environment packet that goes in front of them -- or a
 * refusal would mean "this port built too small a queue" instead of the one thing `refused=` is
 * allowed to mean: the GIF fell behind.  The pair lives in two files, so only the build can see
 * it; the idiom is core_ps2.c's negative array size, because PS2_CFLAGS is -std=c99 and
 * _Static_assert is C11.  If the group table ever outgrows the ring this is a compile error rather
 * than a picture with a piece missing. */
typedef char eegs_ring_holds_a_frame[
    (PS2_GIF_RING_CAP > EEGS_MAX_GROUPS + 1u) ? 1 : -1];

/* Each staged quadword is two uint64_t, exactly as the recorder's own buffer holds them: the DMA
 * reads 16 bytes at a time, and the GIF decodes one register per quadword. */
static uint64_t s_stage[EEGS_STAGE_QW_CAP * 2u] __attribute__((aligned(16)));
static uint32_t s_group_qwc[EEGS_MAX_GROUPS];
static uint32_t s_groups, s_used;

static uint32_t s_dropped;

static eegs_present_stats s_stats;

/* ── LANE 12h's invariance instrument (`PS2_IDENT=1`) ──────────────────────────────
 * The question this answers is the one that decides between the lane's two rungs: is the stream a
 * frame records the same byte string the previous frame recorded?  If it is, the rung is a replay
 * and the vertex arithmetic goes away; if it is not, the rung is a batch map and the arithmetic
 * gets cheaper instead.  Nothing else in this port can answer it, because the recorder keeps no
 * history -- LANE 10's buffer-lifetime law is that the words a group handed to the sink are gone
 * the moment the next group is written.
 *
 * So the instrument keeps a per-group hash instead of the words.  Two 32-bit FNV-1a lanes folded
 * over the group's uint64 halves give 64 bits of state for ~2 multiplies a word, both on 32-bit
 * operands (a 64-bit multiply on this o32 build is a software routine, which is exactly the cost
 * the instrument must not pay in a lane that measures cost), and the pair is folded inside the
 * copy loop that already touches each word.  Its error is one-directional, which is what makes
 * `sameqw=` usable as a decision rather than as a hint: identical bytes necessarily hash
 * identically, so a same-group claim is only wrong on a 2^-64 collision, while a different-group
 * claim is exact.
 *
 * The comparison is BY INDEX, and that is the design rather than a shortcut: a replay can re-send
 * a span only where that span sits in this frame too, so an inserted group that shifts every later
 * one is precisely the cost rung (ii) would pay, and it reads as a frame whose whole tail differs.
 * `tail_*` counts the groups only one of the two frames has, so a shifted frame is never mistaken
 * for an identical one, and the group indices that differ are named in `at=` so the reading side
 * can put them against the `g=` column the app's phase markers print.
 *
 * The per-group tuple is {hash low, hash high, qwc, the per-register lane hashes, the group's
 * kind}.  `qwc` is carried for the COLUMNS and not for the detection: `same=`/`diff=`/`tail=` are
 * reported in quadwords, and this is the field they are summed from.  A vertex-count change is
 * already caught by the words themselves -- the group then folds one more quadword and its tag's
 * NLOOP field moves with it -- which is what the controls measured by deleting both of the length
 * terms and losing no check at all.
 *
 * The lanes exist because `diff=` cannot say WHAT moved, and the answers send the next rung to
 * different files: a group whose XYZ2 quadwords are byte-identical while its RGBAQ moved is a mesh
 * whose geometry is the same every frame and only its shade changes, which wants a colour map, while
 * a moving XYZ2 is the vertex transform being re-solved.  A vertex group's data quadwords are PRIM,
 * RGBAQ, ST, XYZ2 in that REGS order, starting at quadword 1 -- the tag's quadword carries the REGS
 * list in its high half -- so each lane folds every fourth word of the group from there.  PRIM and
 * the tag are in none of the lanes and
 * only in the whole-group hash, which is why a group can count in all three lanes and still be
 * `diff=` -- that combination says the primitive descriptor moved, not the picture.  An AD group (a
 * texture switch) has no such four-register body, so it is marked by kind and counted in no lane;
 * `same=`/`diff=` count it like any other group. */
#ifndef BTRON_PS2_GS_IDENT
#define BTRON_PS2_GS_IDENT 0
#endif

#if BTRON_PS2_GS_IDENT
#define IDENT_H0 2166136261u
#define IDENT_H1 16777619u
#define IDENT_AT_MAX 8u

/* The per-group tuple's columns. */
#define ID_A 0u                    /* whole-group FNV lane a */
#define ID_B 1u                    /* whole-group FNV lane b */
#define ID_QW 2u                   /* this group's quadword count */
#define ID_RGB 3u                  /* the RGBAQ lane alone */
#define ID_ST 4u                   /* the ST lane alone */
#define ID_XYZ 5u                  /* the XYZ2 lane alone */
#define ID_SHAPE 6u                /* the XYZ2 lane read as DIFFERENCES */
#define ID_KIND 7u                 /* ID_VERTEX or ID_AD */
#define ID_N 8u

#define ID_VERTEX 1u
#define ID_AD 2u

/* Distinct, nonzero seeds, one per lane: a lane that folded nothing and a lane whose words happened
 * to hash to another lane's value are then both impossible readings, and the kind column is the
 * only thing the lane comparison trusts for "this group has this register at all". */
#define ID_SEED_RGB 0x9E3779B9u
#define ID_SEED_ST  0x85EBCA77u
#define ID_SEED_XYZ 0xC2B2AE3Du
#define ID_SEED_SHAPE 0x165667B1u

/* {fnv lane a, lane b, qwc, rgb, st, xyz, shape, kind}, one row per staged group, kept for both the
 * frame being built and the last frame that was presented. */
static uint32_t s_ident_cur[EEGS_MAX_GROUPS][ID_N];
static uint32_t s_ident_prev[EEGS_MAX_GROUPS][ID_N];
static uint32_t s_ident_prev_groups;
static uint32_t s_ident_frames;
static uint32_t s_ident_vertex;   /* vertex groups staged this frame, whatever they hashed to */

static void ident_fold(uint32_t *ha, uint32_t *hb, uint64_t w)
{
    *ha = (*ha ^ (uint32_t)w) * 16777619u;
    *hb = (*hb + (uint32_t)(w >> 32)) * 2246822519u;
}

/* One register lane's hash: a 32-bit FNV-1a step, one multiply per folded word, and a seed per lane
 * so a lane of three vertices cannot read as an empty one. */
static void ident_fold1(uint32_t *h, uint64_t w)
{
    *h = (*h ^ (uint32_t)w) * 16777619u;
}

/* A group index written as decimal into `s_ident_at`, with no libc in this TU: the present formats
 * nothing today, and pulling snprintf in for one bench column would put a printf back into a unit
 * the shipped image links. */
static char s_ident_at[4u * IDENT_AT_MAX + 8u];
static uint32_t s_ident_at_len;

static void at_putc(char c)
{
    if (s_ident_at_len + 2u < sizeof s_ident_at) s_ident_at[s_ident_at_len++] = c;
    s_ident_at[s_ident_at_len] = '\0';
}

static void at_put_index(uint32_t n, int first)
{
    char digits[10];
    uint32_t k = 0u;

    if (!first) at_putc(',');
    do {
        digits[k++] = (char)('0' + (n % 10u));
        n /= 10u;
    } while (n);
    while (k) at_putc(digits[--k]);
}
#endif /* BTRON_PS2_GS_IDENT */

uint32_t eegs_present_groups_staged(void)
{
    return s_groups;
}

#if BTRON_PS2_GS_IDENT
/* Compare this frame's staged groups against the last presented frame's, index for index, and
 * carry this frame's tuples forward.  Called once per presented frame, from the stats block, so
 * the numbers on the row describe the stream that actually went on the bus -- and so a frame that
 * staged nothing never consumes a predecessor it did not replace. */
static void ident_report(uint32_t groups)
{
    uint32_t i, shown = 0u, more;
    const uint32_t n = (groups < s_ident_prev_groups) ? groups : s_ident_prev_groups;

    s_ident_at_len = 0u;
    s_ident_at[0] = '\0';

    s_stats.ident_frames = ++s_ident_frames;
    s_stats.ident_groups_prev = s_ident_prev_groups;
    s_stats.same_groups = s_stats.same_qw = 0u;
    s_stats.diff_groups = s_stats.diff_qw = 0u;
    s_stats.tail_groups = s_stats.tail_qw = 0u;
    s_stats.diff_first = s_stats.diff_last = s_stats.diff_more = 0u;
    s_stats.ident_xyz = s_stats.ident_rgb = s_stats.ident_st = s_stats.ident_shape = 0u;
    /* `v=` is the frame's OWN vertex-group count, staged or dropped, taken from the sink rather than
     * from the aligned prefix: `g=` minus `v=` is the AD groups, and a lane count against `v=` is the
     * arithmetic that says how many of this frame's meshes kept that field. */
    s_stats.ident_vertex_groups = s_ident_vertex;

    for (i = 0u; i < n; i++) {
        const int same = (s_ident_cur[i][ID_A] == s_ident_prev[i][ID_A]) &&
                         (s_ident_cur[i][ID_B] == s_ident_prev[i][ID_B]) &&
                         (s_ident_cur[i][ID_QW] == s_ident_prev[i][ID_QW]);

        /* The register lanes, over the aligned prefix and only where BOTH frames' group at this
         * index is a vertex group: an AD group has no XYZ2 quadword to be invariant about. */
        if (s_ident_cur[i][ID_KIND] == ID_VERTEX &&
            s_ident_prev[i][ID_KIND] == ID_VERTEX) {
            if (s_ident_cur[i][ID_XYZ] == s_ident_prev[i][ID_XYZ]) s_stats.ident_xyz++;
            if (s_ident_cur[i][ID_RGB] == s_ident_prev[i][ID_RGB]) s_stats.ident_rgb++;
            if (s_ident_cur[i][ID_ST] == s_ident_prev[i][ID_ST]) s_stats.ident_st++;
            if (s_ident_cur[i][ID_SHAPE] == s_ident_prev[i][ID_SHAPE]) s_stats.ident_shape++;
        }

        if (same) {
            s_stats.same_groups++;
            s_stats.same_qw += s_ident_cur[i][ID_QW];
            continue;
        }
        s_stats.diff_groups++;
        s_stats.diff_qw += s_ident_cur[i][ID_QW];
        if (s_stats.diff_groups == 1u) s_stats.diff_first = i;
        s_stats.diff_last = i;
        if (shown < IDENT_AT_MAX) {
            at_put_index(i, (shown == 0u) ? 1 : 0);
            shown++;
        }
    }

    /* The groups only one frame has, on both sides and in the same column: a frame that gained a
     * texture switch and one that lost a group are the same reading -- a span that cannot be
     * replayed where it sits. */
    for (i = n; i < groups; i++) {
        s_stats.tail_groups++;
        s_stats.tail_qw += s_ident_cur[i][ID_QW];
    }
    for (i = n; i < s_ident_prev_groups; i++) {
        s_stats.tail_groups++;
        s_stats.tail_qw += s_ident_prev[i][ID_QW];
    }

    more = s_stats.diff_groups - shown;
    s_stats.diff_more = more;
    if (more) {
        at_putc('+');
        at_put_index(more, 1);
    }

    for (i = 0u; i < groups; i++) {
        uint32_t c;

        for (c = 0u; c < ID_N; c++) s_ident_prev[i][c] = s_ident_cur[i][c];
    }
    s_ident_prev_groups = groups;
}

const char *eegs_present_ident_at(void)
{
    return s_ident_at;
}
#else
const char *eegs_present_ident_at(void)
{
    return "";
}
#endif /* BTRON_PS2_GS_IDENT */

/* The recorder's sink, called from inside the application's paint callback.  It copies: the
 * recorder reuses its own buffer for the next group the moment this returns, so a sink that
 * merely remembered a pointer would be pointing at the following group by present time. */
static void eegs_stage_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    uint32_t i;
#if BTRON_PS2_GS_IDENT
    uint32_t ha = IDENT_H0, hb = IDENT_H1;
    uint32_t lrgb = ID_SEED_RGB, lst = ID_SEED_ST, lxyz = ID_SEED_XYZ;
    uint32_t lshape = ID_SEED_SHAPE;
    uint32_t px = 0u, py = 0u, pz = 0u, first = 1u;
    /* The group's kind comes out of its own tag, because the sink is handed words and nothing else:
     * a vertex group declares a four-register REGS list, an AD group declares one, and NREG is
     * bits [63:60] of the tag's low half.  Anything else folds no lane.  The report side re-checks
     * this column, so the gate here is the redundant half of the pair -- measured, deleting it loses
     * no check -- and it stays because folding words a group does not have is worse to reason about
     * than folding nothing and letting one column say why. */
    const uint32_t nreg = (uint32_t)(qw[0] >> 60) & 0xFu;
    uint32_t kind = 0u;

    if (nreg == GS_REC_REGS_N) kind = ID_VERTEX;
    else if (nreg == 1u) kind = ID_AD;
#endif

    (void)user;

    if (qwc == 0u || s_groups >= EEGS_MAX_GROUPS || s_used + qwc > EEGS_STAGE_QW_CAP) {
        s_dropped++;
        return;
    }

    /* Word by word rather than through a memmove: this file is compiled into the host gate as
     * well as the image, and a copy loop has one implementation instead of two. */
    for (i = 0; i < 2u * qwc; i++) {
        s_stage[2u * s_used + i] = qw[i];
#if BTRON_PS2_GS_IDENT
        ident_fold(&ha, &hb, qw[i]);
        /* The tag occupies ONE quadword -- its own 64 bits with the REGS list in the half above them
         * -- so the data body starts at quadword 1 and is GS_REC_REGS_N registers per vertex, in the
         * REGS order PRIM, RGBAQ, ST, XYZ2.  PRIM folds into no lane: the whole-group hash owns it,
         * which is what the book's residual reading is -- a group invariant in every lane yet
         * `diff=` had its PRIM, its tag or its loop count move. */
        if (kind == ID_VERTEX && (i >> 1) >= 1u) {
            switch (((i >> 1) - 1u) % GS_REC_REGS_N) {
            case 1u: ident_fold1(&lrgb, qw[i]); break;   /* RGBAQ */
            case 2u: ident_fold1(&lst,  qw[i]); break;   /* ST    */
            case 3u:
                ident_fold1(&lxyz, qw[i]);
                /* The shape lane reads XYZ2's own fields -- X[15:0], Y[31:16], Z[63:32] of the
                 * quadword's low half (gs_pack_xyz2) -- and folds the DIFFERENCE from the previous
                 * vertex rather than the coordinate.  It is therefore invariant under translating
                 * the whole group, which is the one thing `xyz=` cannot say: a differing `xyz=` over
                 * an invariant `shape=` is the same mesh in a new place, and a differing `shape=` is
                 * the mesh itself changing.  The first vertex folds nothing, so a group's shape is
                 * its vertex count and its relative structure and nothing about where it sits. */
                if ((i & 1u) == 0u) {
                    const uint64_t w = qw[i];
                    const uint32_t x = (uint32_t)(w & 0xFFFFu);
                    const uint32_t y = (uint32_t)((w >> 16) & 0xFFFFu);
                    const uint32_t z = (uint32_t)(w >> 32);

                    if (!first) {
                        ident_fold1(&lshape, x - px);
                        ident_fold1(&lshape, y - py);
                        ident_fold1(&lshape, z - pz);
                    }
                    px = x; py = y; pz = z;
                    first = 0u;
                }
                break;
            default: break;                              /* PRIM  */
            }
        }
#endif
    }
#if BTRON_PS2_GS_IDENT
    /* The tag PRIM and the REGS list are inside those words, so a group that changed its primitive
     * type or its loop count without changing a coordinate hashes differently on its own. */
    ha = (ha ^ qwc) * 16777619u;
    s_ident_cur[s_groups][ID_A] = ha;
    s_ident_cur[s_groups][ID_B] = hb;
    s_ident_cur[s_groups][ID_QW] = qwc;
    s_ident_cur[s_groups][ID_RGB] = lrgb;
    s_ident_cur[s_groups][ID_ST] = lst;
    s_ident_cur[s_groups][ID_XYZ] = lxyz;
    s_ident_cur[s_groups][ID_SHAPE] = lshape;
    s_ident_cur[s_groups][ID_KIND] = kind;
    if (kind == ID_VERTEX) s_ident_vertex++;
#endif

    s_group_qwc[s_groups++] = qwc;
    s_used += qwc;
}

void eegs_present_begin(void)
{
    /* LANE 10's completion check, and it is here rather than at send() on purpose.  The stage is
     * the buffer the ring's descriptors point into, and this function is the one moment before the
     * next frame's words are written into it -- so this is where "has the GIF finished reading what
     * I committed last pass?" gets asked, once per paint pass, instead of once per transfer at the
     * moment of committing it.  A reap with nothing queued and nothing in flight is a register read
     * and a comparison; the µs it does spend waiting are counted into the ring's `wait_us`, which
     * is the number that says whether the presentation plane is still, in the end, being stopped by
     * its consumer. */
    (void)ps2_gif_reap();

    s_groups = 0u;
    s_used = 0u;
    s_dropped = 0u;
#if BTRON_PS2_GS_IDENT
    s_ident_vertex = 0u;
#endif
    eegs_frame_begin(eegs_stage_sink, 0);
}

void eegs_present_end(void)
{
    /* Closes the open group, which arrives at the sink above.  Nothing is sent here: the whole
     * point of staging is that the canvas upload has not happened yet. */
    eegs_frame_end();
}

int eegs_present_send(const RECT *client)
{
    gs_draw_env env;
    uint32_t i, off, qw_sent, posted, sent;

    if (client == 0 || s_groups == 0u) return 0;
    if (client->right <= client->left || client->bottom <= client->top) return 0;

    /* The frame the primitives land in is the displayed canvas: page 0, the width field the
     * PCRTC reads it with, CT32, and no write mask -- the layer replaces what it covers and
     * blends into it where ALPHA says to.  ps2_gs_set_mode() programs the same page for display,
     * which is the only reason drawing there is visible at all. */
    env.frame_byte   = 0u;
    env.frame_px_w   = (uint32_t)PS2_SCREEN_WIDTH;
    env.frame_psm    = GS_PSM_CT32;
    env.frame_fmsk   = 0u;

    /* The depth buffer this port never uses.  TEST.ZTEN is clear and ZMSK is set, so the GS
     * neither reads nor writes these bytes; what it does need is a register that names a page no
     * other allocation owns, and the top page of VRAM is that page -- the texture cursor starts
     * above the canvas and grows *up* from there, so it reaches page 511 only after it has taken
     * every other byte in the chip. */
    env.zbuf_byte    = GS_VRAM_BYTES - GS_PAGE_FRAME;
    env.zbuf_psm     = GS_PSMZ32;
    env.zbuf_zmsk    = 1u;

    /* The window's client rect, in both of the units the two registers want: XYOFFSET is the page
     * origin the vertices are relative to, so the app's (0,0) lands on the client's top-left, and
     * SCISSOR is in plain screen pixels and bounds the primitive to the same rect.  Exclusive
     * edges become inclusive ones here because that is what the field means. */
    env.win_x = (uint32_t)client->left;
    env.win_y = (uint32_t)client->top;
    env.clip_x0 = (uint32_t)client->left;
    env.clip_y0 = (uint32_t)client->top;
    env.clip_x1 = (uint32_t)client->right - 1u;
    env.clip_y1 = (uint32_t)client->bottom - 1u;

    /* No test of any kind: a 2D UI's order is its draw order, and an alpha test would throw away
     * fragments the software path drew.  The blend is the source-over pair named in
     * ps2_gs_pack.h, which is what ABE selects in the PRIM word. */
    env.reg_test  = 0u;
    env.reg_alpha = gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u);

    /* The texture trio is zero here because this port never has one texture per frame: the XMB
     * binds an icon sheet and a font sheet, and backend_eegs.c's eegs_texture() writes the three
     * registers between the vertex groups.  What has to be true before the first vertex is that
     * TME is off for an untextured primitive, and that is PRIM's field, not this one's. */
    env.reg_clamp = 0u;
    env.reg_tex0  = 0u;
    env.reg_tex1  = 0u;

    if (ps2_gs_draw_env_apply(&env) != 0) return -1;

    /* One DMA per group, in recorded order: the environment above has to reach the GS before the
     * first vertex, and each group is a complete packet with EOP set, which is what makes a
     * transfer per group legal rather than merely convenient.
     *
     * These are POSTED, not sent (LANE 10).  The stage is not written again before the reap at the
     * next begin(), so a descriptor can point into it while the EE goes on to composite the rest
     * of the frame, and the transfer's cost is no longer a hole in this pass.  Order survives
     * because the ring is a FIFO and because the only synchronous producer left -- the canvas band
     * that `ps2_upload_cover_in_layer()` puts back over a stream that buried it -- reaps before it
     * starts, so it can put nothing in front of a group it did not wait for.
     *
     * The first refusal ends the frame.  Continuing would let a later group take a slot that freed
     * up while an earlier one was lost, which is primitives out of draw order rather than a
     * missing tail; the lost groups are counted in the same `dropped` column the stage's own
     * overflow uses, because both mean a piece of the picture no screen has. */
    off = 0u;
    qw_sent = 0u;
    posted = 0u;
    for (i = 0u; i < s_groups; i++) {
        if (ps2_gif_post(&s_stage[2u * off], s_group_qwc[i]) != 0) {
            s_dropped += s_groups - i;
            break;
        }
        qw_sent += s_group_qwc[i];
        posted++;
        off += s_group_qwc[i];
    }

    s_stats.groups = s_groups;
    s_stats.qw = qw_sent;
    s_stats.verts = eegs_verts();
    s_stats.prims = eegs_prims();
    s_stats.dropped = s_dropped;
    s_stats.unsplittable = gs_rec_unsplittable();
    s_stats.tex_switches = eegs_tex_switches();
    s_stats.tex_missing = eegs_tex_missing();
    s_stats.prim_moves = eegs_prim_moves();
    /* LANE 12f rung (ii): retained meshes drawn as one strip, and meshes that asked and were
     * refused.  The two are read together -- `slow=` climbing on a fixed-size window says the gate
     * is turning work away that used to be fast, and `batch=` beside a sheet with an edge missing
     * says the GS's clip window is not taking the overhang the batch relied on.  `why=` names the
     * last refusal, and `over=` counts the batched points outside the rect that SCISSOR is being
     * asked to cut. */
    s_stats.grid_batch = eegs_grid_batch();
    s_stats.grid_slow = eegs_grid_slow();
    s_stats.grid_why = eegs_grid_why();
    s_stats.grid_over = eegs_grid_over();
#if BTRON_PS2_GS_IDENT
    ident_report(s_groups);
#endif

    /* Whether this stream plates the rect it is scissored to, which is the condition the window
     * walk checks before it lets a layer's own body go unfilled in the canvas.  The clip's right
     * and bottom edges are exclusive in `client` and inclusive in the span, so the comparison is
     * against w-1 and h-1; `has_bbox` stays clear when the frame drew no vertex group at all,
     * which is a layer that painted nothing and therefore plates nothing. */
    {
        int32_t l = 0, t = 0, r = -1, b = -1;
        s_stats.has_bbox = (uint32_t)(gs_rec_bbox_first(&l, &t, &r, &b) == 0);
        s_stats.b0_l = l; s_stats.b0_t = t; s_stats.b0_r = r; s_stats.b0_b = b;
        s_stats.clip_w = (uint32_t)(client->right - client->left);
        s_stats.clip_h = (uint32_t)(client->bottom - client->top);
        s_stats.plate_covers = (uint32_t)(s_stats.has_bbox &&
                                          l <= 0 && t <= 0 &&
                                          r >= (int32_t)s_stats.clip_w - 1 &&
                                          b >= (int32_t)s_stats.clip_h - 1);
    }

    /* The frame is over: the next begin() starts a fresh stage, and a group that was sent is never
     * sent twice.  `sent` is the groups the ring ACCEPTED, not the groups the stage holds, so a
     * refusal shows up as the shorter frame it is wherever this return value is read. */
    sent = posted;
    s_groups = 0u;
    s_used = 0u;
    return (int)sent;
}

void eegs_present_get_stats(eegs_present_stats *out)
{
    if (out) *out = s_stats;
}
