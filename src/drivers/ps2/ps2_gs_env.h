/*
 * ps2_gs_env.h — the GS drawing environment, as one GIF stream
 *
 * The blit path programs four registers (BITBLTBUF, TRXPOS, TRXREG, TRXDIR) and never needs
 * the rest.  Drawing primitives needs nine more, and each of them has to be true before the
 * first vertex arrives: where the pixels land (FRAME), whether depth is consulted (ZBUF,
 * TEST), what the blend does (ALPHA), which part of VRAM a texture is in and how it is
 * sampled (TEX0, TEX1, CLAMP), where a vertex's (0,0) is on the screen (XYOFFSET) and how
 * far a primitive may reach (SCISSOR).
 *
 * They get written as a stream of AD quadwords, not as ps2_gs_poke64() calls.  That is a
 * hardware fact and not a style preference: the GIF feeds a register write to the GS in the
 * same order it receives it, so a packet programs the whole environment with one DMA, while
 * nine separate stores would each have to wait for the bus and none of them would be ordered
 * against the primitive stream that follows.  make test-ps2 asserts the apply path contains
 * no poke64 call at all.
 *
 * Order is part of the design.  FRAME and ZBUF name the target before anything can write to
 * it; XYOFFSET and SCISSOR bound the geometry before a vertex is decoded; TEST and ALPHA are
 * per-fragment state that the first quad reads; the texture trio last, because TEX0 with a
 * TBA the allocator has not filled yet is only ever reached by a textured primitive, and the
 * env packet draws none.
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/txt/PS2.txt.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_ENV_H
#define PS2_GS_ENV_H

#include <stdint.h>

/* How many registers the environment packet writes, and how many quadwords that is: one tag
 * pair plus one AD quadword per register.  A caller that allocates gs_draw_env_qwc()
 * quadwords therefore cannot be given a short buffer by a future change here -- the count is
 * derived, not repeated. */
#define GS_ENV_NREG  9u

/* The environment, in the units its inputs arrive in: byte offsets into GS local memory (from
 * ps2_gs_vram.c), pixel widths, screen pixels.  The four registers whose fields are a
 * negotiation between what an app asked for and what the GS can do are carried already
 * encoded -- they are built by the encoders in ps2_gs_pack.h at the place that knows what the
 * app wanted, which is increment 4's job, and this packet's job is to put them somewhere. */
typedef struct {
    uint32_t frame_byte;    /* VRAM offset of the frame buffer, whole 8 KB pages */
    uint32_t frame_px_w;    /* buffer width in pixels; FBW rounds it up to 64s */
    uint32_t frame_psm;     /* GS_PSM_CT32 and friends, from ps2_gs.h */
    uint32_t frame_fmsk;    /* write mask; 0 writes every channel */
    uint32_t zbuf_byte;
    uint32_t zbuf_psm;
    uint32_t zbuf_zmsk;     /* 1 disables Z writes, which is what a 2D UI wants */

    uint32_t win_x, win_y;  /* the window's origin on screen, in pixels: the page origin */

    uint32_t clip_x0, clip_y0, clip_x1, clip_y1;    /* SCISSOR, screen pixels */

    uint64_t reg_alpha;
    uint64_t reg_test;
    uint64_t reg_clamp;
    uint64_t reg_tex0;
    uint64_t reg_tex1;
} gs_draw_env;

/* Quadwords in the packet: the tag plus GS_ENV_NREG AD writes. */
uint32_t gs_draw_env_qwc(void);

/* Fill exactly gs_draw_env_qwc() quadwords at `qw` (16-byte aligned: the DMA reads quadwords).
 * Returns 0, or -1 without touching the buffer when a VRAM offset in `e` cannot be expressed
 * as a frame-page index -- a layout problem, and the only answer that keeps it from becoming a
 * drawing at the wrong address.
 *
 * The tag's EOP is set because this stream ends with the environment; a caller that wants to
 * append primitives puts them in a following stream rather than changing this one, which is
 * what keeps the env packet's length a constant the gate can state. */
int gs_draw_env_packet(const gs_draw_env *e, uint64_t *qw);

/* Send that packet.  Declared here and defined in ps2_gs.c, because that is the file that owns
 * DMAC channel 2; the environment itself is described above and gated on the host, and this is
 * the seven lines that put it on the bus.  Returns gs_draw_env_packet()'s -1 without touching
 * the DMA when the layout does not fit. */
int ps2_gs_draw_env_apply(const gs_draw_env *env);

#endif /* PS2_GS_ENV_H */
