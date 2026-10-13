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

/* The recorder's sink, called from inside the application's paint callback.  It copies: the
 * recorder reuses its own buffer for the next group the moment this returns, so a sink that
 * merely remembered a pointer would be pointing at the following group by present time. */
static void eegs_stage_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    uint32_t i;

    (void)user;

    if (qwc == 0u || s_groups >= EEGS_MAX_GROUPS || s_used + qwc > EEGS_STAGE_QW_CAP) {
        s_dropped++;
        return;
    }

    /* Word by word rather than through a memmove: this file is compiled into the host gate as
     * well as the image, and a copy loop has one implementation instead of two. */
    for (i = 0; i < 2u * qwc; i++) s_stage[2u * s_used + i] = qw[i];
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
