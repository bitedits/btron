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

uint32_t gs_draw_dual_env_qwc(void)
{
    return 1u + GS_DUAL_ENV_NREG;
}

static uint64_t *emit_env_regs(const gs_draw_env *e, uint32_t context, uint32_t frame_fba, uint32_t zbuf_fba, uint64_t *c)
{
    uint32_t gp_xyoffset = (context == 0u) ? GS_GP_XYOFFSET_1 : GS_GP_XYOFFSET_2;
    uint32_t gp_scissor  = (context == 0u) ? GS_GP_SCISSOR_1  : GS_GP_SCISSOR_2;
    uint32_t gp_frame    = (context == 0u) ? GS_GP_FRAME_1    : GS_GP_FRAME_2;
    uint32_t gp_zbuf     = (context == 0u) ? GS_GP_ZBUF_1     : GS_GP_ZBUF_2;
    uint32_t gp_test     = (context == 0u) ? GS_GP_TEST_1     : GS_GP_TEST_2;
    uint32_t gp_alpha    = (context == 0u) ? GS_GP_ALPHA_1    : GS_GP_ALPHA_2;
    uint32_t gp_clamp    = (context == 0u) ? GS_GP_CLAMP_1    : GS_GP_CLAMP_2;
    uint32_t gp_tex0     = (context == 0u) ? GS_GP_TEX0_1     : GS_GP_TEX0_2;
    uint32_t gp_tex1     = (context == 0u) ? GS_GP_TEX1_1     : GS_GP_TEX1_2;

    /* The page origin: XYOFFSET.  The GS hardware computes screen coordinate
     * W = P - OFX.  Because vertices P are biased by +GS_XYOFFSET_PAGE (0x8000),
     * setting OFX = GS_XYOFFSET_PAGE - (win_x << 4) yields:
     * W = (x_px << 4 + 0x8000) - (0x8000 - win_x << 4) = (x_px + win_x) << 4. */
    c = gs_pack_pair(c,
                     gs_pack_xyoffset(GS_XYOFFSET_PAGE - (e->win_x << 4),
                                      GS_XYOFFSET_PAGE - (e->win_y << 4)),
                     gp_xyoffset);

    /* Clip in screen pixels: SCISSOR */
    c = gs_pack_pair(c,
                     gs_pack_scissor(e->clip_x0, e->clip_x1, e->clip_y0, e->clip_y1),
                     gp_scissor);

    c = gs_pack_pair(c,
                     gs_pack_frame(frame_fba, gs_vram_fbw(e->frame_px_w),
                                   e->frame_psm, e->frame_fmsk),
                     gp_frame);

    c = gs_pack_pair(c, gs_pack_zbuf(zbuf_fba, e->zbuf_psm, e->zbuf_zmsk),
                     gp_zbuf);

    c = gs_pack_pair(c, e->reg_test,  gp_test);
    c = gs_pack_pair(c, e->reg_alpha, gp_alpha);

    /* Texture trio */
    c = gs_pack_pair(c, e->reg_clamp, gp_clamp);
    c = gs_pack_pair(c, e->reg_tex0,  gp_tex0);
    c = gs_pack_pair(c, e->reg_tex1,  gp_tex1);

    return c;
}

int gs_draw_env_packet_ctx(const gs_draw_env *e, uint32_t context, uint64_t *qw)
{
    static const uint32_t s_regs_ad[1] = { GS_NIB_AD };
    uint64_t *c = qw;
    uint32_t frame_fba, zbuf_fba;

    if (e == 0 || qw == 0 || context > 1u) return -1;

    if (gs_vram_fba(e->frame_byte, &frame_fba) != 0) return -1;
    if (gs_vram_fba(e->zbuf_byte, &zbuf_fba) != 0) return -1;

    c = gs_pack_pair(c, gs_pack_giftag(GS_ENV_NREG, 1u, 0u, 0u, GS_FLG_PACKED, 1u),
                     gs_pack_regs(s_regs_ad, 1u));

    c = emit_env_regs(e, context, frame_fba, zbuf_fba, c);
    return 0;
}

int gs_draw_env_packet(const gs_draw_env *e, uint64_t *qw)
{
    return gs_draw_env_packet_ctx(e, 0u, qw);
}

int gs_draw_dual_env_packet(const gs_draw_env *ctx1, const gs_draw_env *ctx2, uint64_t *qw)
{
    static const uint32_t s_regs_ad[1] = { GS_NIB_AD };
    uint64_t *c = qw;
    uint32_t f1, z1, f2, z2;

    if (ctx1 == 0 || ctx2 == 0 || qw == 0) return -1;

    if (gs_vram_fba(ctx1->frame_byte, &f1) != 0) return -1;
    if (gs_vram_fba(ctx1->zbuf_byte, &z1) != 0) return -1;
    if (gs_vram_fba(ctx2->frame_byte, &f2) != 0) return -1;
    if (gs_vram_fba(ctx2->zbuf_byte, &z2) != 0) return -1;

    /* Single GIF packet with NLOOP = 18 AD writes */
    c = gs_pack_pair(c, gs_pack_giftag(GS_DUAL_ENV_NREG, 1u, 0u, 0u, GS_FLG_PACKED, 1u),
                     gs_pack_regs(s_regs_ad, 1u));

    c = emit_env_regs(ctx1, 0u, f1, z1, c);
    c = emit_env_regs(ctx2, 1u, f2, z2, c);
    return 0;
}
