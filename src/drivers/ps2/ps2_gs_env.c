/*
 * ps2_gs_env.c — build the drawing-environment packet
 *
 * Pure: it fills a caller's buffer and touches no register, so every word it produces can be
 * compared with a hand-derived literal on the host.  Getting it onto the bus is
 * ps2_gs_draw_env_apply() in ps2_gs.c, which is the only place in this path that knows the
 * DMA exists.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_env.h"

#include "ps2_gs_pack.h"
#include "ps2_gs_vram.h"

uint32_t gs_draw_env_qwc(void)
{
    return 1u + GS_ENV_NREG;
}

int gs_draw_env_packet(const gs_draw_env *e, uint64_t *qw)
{
    /* One nibble, AD, per loop: each data quadword carries a register's 64 bits and that
     * register's GP offset, so nine writes need no more tag bookkeeping than one. */
    static const uint32_t s_regs_ad[1] = { GS_NIB_AD };
    uint64_t *c = qw;
    uint32_t frame_fba, zbuf_fba;

    if (e == 0 || qw == 0) return -1;

    /* An offset that is not a whole 8 KB page is not a plan, so this says so instead of
     * quietly drawing somewhere near it. */
    if (gs_vram_fba(e->frame_byte, &frame_fba) != 0) return -1;
    if (gs_vram_fba(e->zbuf_byte, &zbuf_fba) != 0) return -1;

    /* GIFTAG: NLOOP is the nine writes, EOP ends the packet, FLG PACKED with NREG 1, and the
     * tag's second quadword is the REGS list.  PRIM is 0 and PRE is 0 because this group
     * draws nothing; it only sets state. */
    c = gs_pack_pair(c, gs_pack_giftag(GS_ENV_NREG, 1u, 0u, 0u, GS_FLG_PACKED, 1u),
                     gs_pack_regs(s_regs_ad, 1u));

    /* The page origin: XYOFFSET is subtracted at the vertex kick, so a vertex that says
     * "2048 px plus 12 px" lands at screen x = 12 when the offset carries 2048 px plus the
     * window's own x.  Both halves are 12.4, hence the << 4. */
    c = gs_pack_pair(c,
                     gs_pack_xyoffset(GS_XYOFFSET_PAGE + (e->win_x << 4),
                                      GS_XYOFFSET_PAGE + (e->win_y << 4)),
                     GS_GP_XYOFFSET_1);

    /* Clip in screen pixels, no offset and no fixed point: the window's rect as the PCRTC
     * sees it, so a primitive cannot paint over the title bar or the neighbour's border. */
    c = gs_pack_pair(c,
                     gs_pack_scissor(e->clip_x0, e->clip_x1, e->clip_y0, e->clip_y1),
                     GS_GP_SCISSOR_1);

    c = gs_pack_pair(c,
                     gs_pack_frame(frame_fba, gs_vram_fbw(e->frame_px_w),
                                   e->frame_psm, e->frame_fmsk),
                     GS_GP_FRAME_1);

    c = gs_pack_pair(c, gs_pack_zbuf(zbuf_fba, e->zbuf_psm, e->zbuf_zmsk),
                     GS_GP_ZBUF_1);

    c = gs_pack_pair(c, e->reg_test,  GS_GP_TEST_1);
    c = gs_pack_pair(c, e->reg_alpha, GS_GP_ALPHA_1);

    /* The texture context of the window: where its atlas lives, how big a page it is, and how
     * to sample it.  A UI at 1:1 wants nearest, so TEX1's filters come from the caller, which
     * is the only thing that knows whether the app asked for anything else. */
    c = gs_pack_pair(c, e->reg_clamp, GS_GP_CLAMP_1);
    c = gs_pack_pair(c, e->reg_tex0,  GS_GP_TEX0_1);
    c = gs_pack_pair(c, e->reg_tex1,  GS_GP_TEX1_1);

    return 0;
}
