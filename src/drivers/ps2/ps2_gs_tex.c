/*
 * ps2_gs_tex.c — a GL plane's VRAM address, and the three words that point the GS at it
 *
 * See ps2_gs_tex.h for the three laws.  What is only visible here is that the registry and the
 * VRAM cursor live in the same file: two callers each holding a cursor is how a texture and a
 * frame buffer end up sharing an address, and the symptom is a screen of noise with no cause.
 *
 * Nothing here sends anything.  The entry it hands back carries `pending` until the bytes cross,
 * and the drain is ps2_gs_tex_upload() in ps2_gs.c -- the file that owns DMAC channel 2 -- so the
 * whole of this translation unit is host-gateable against hand literals.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_tex.h"

#include "ps2_gs.h"                       /* GS_PSM_CT32, the one format this port uploads */
#include "ps2_gs_pack.h"
#include "ps2_gs_vram.h"

static gs_vram_t s_vram;
static gs_tex_t s_reg[GS_TEX_MAX];
static uint32_t s_n;
static gs_tex_sink s_sink;

/* log2 of a power of two, or -1.  TEX0.TW/TH are exponents, so an extent that is not a power of
 * two has no name in the register at all -- and `gl_image_alloc()` already rounded it, so a
 * non-power here is a caller that read the wrong field of its own image. */
static int pow2_exp(uint32_t n, uint32_t *out)
{
    uint32_t e = 0u;

    if (n == 0u || (n & (n - 1u)) != 0u) return -1;
    while ((1u << e) != n) e++;
    *out = e;
    return 0;
}

void gs_tex_install_sink(gs_tex_sink sink)
{
    s_sink = sink;
}

/* Hand slot `index` to whoever owns the bus, once its plan is complete.  With no sink installed --
 * the host gate -- the entry stays `pending`, which is what gs_tex_pending() counts. */
static void hand_over(uint32_t index)
{
    if (s_sink != 0) s_sink(index);
}

void gs_tex_init(void)
{
    uint32_t i;

    gs_vram_init(&s_vram);
    for (i = 0; i < GS_TEX_MAX; i++) s_reg[i] = (gs_tex_t){0};
    s_n = 0u;
    s_sink = 0;
}

const gs_tex_t *gs_tex_register(const void *plane, uint32_t px_w, uint32_t px_h,
                                uint32_t pad_w, uint32_t pad_h)
{
    gs_tex_t *slot = 0;
    uint32_t i, index, tw_exp, th_exp, byte, tba, span;
    int recycled;

    if (plane == 0 || px_w == 0u || px_h == 0u) return 0;
    if (pow2_exp(pad_w, &tw_exp) != 0 || pow2_exp(pad_h, &th_exp) != 0) return 0;

    /* The uploaded extent has to live inside the plane it was written into, or the transfer's
     * RRW/RRH would read past the source and the clamp would name texels the app never wrote. */
    if (px_w > pad_w || px_h > pad_h) return 0;

    recycled = 0;
    for (i = 0; i < s_n; i++) {
        if (s_reg[i].plane != plane) continue;

        /* Same pointer, same padded geometry: `gl_image_alloc()`'s "same plane, new contents"
         * path.  The address and the three words stand; only the bytes cross again. */
        if (s_reg[i].pad_w == pad_w && s_reg[i].pad_h == pad_h) {
            s_reg[i].px_w = px_w;
            s_reg[i].px_h = px_h;
            s_reg[i].pending = 1;
            hand_over(i);
            return &s_reg[i];
        }

        /* Same pointer, different geometry: the allocation this entry describes was freed and the
         * heap handed its address to another texture.  Re-plan the slot below rather than reuse
         * its span, which is sized for the plane that is gone. */
        slot = &s_reg[i];
        index = i;
        recycled = 1;
        break;
    }

    if (slot == 0) {
        if (s_n == GS_TEX_MAX) return 0;
        index = s_n;
        slot = &s_reg[s_n];
    }

    /* A plane's own span, so that no two planes share a 4 KB swizzle block or a 256-byte block:
     * the size is the *padded* plane's, not the upload's, because the GS addresses the padded
     * extent and its padding has to belong to this plane rather than to the next one's texels. */
    span = gs_vram_tex_bytes(pad_w, pad_h);
    if (gs_vram_take_tex(&s_vram, span, &byte) != 0 || gs_vram_tba(byte, &tba) != 0) {
        /* No room.  The cursor did not move and nothing new was claimed; a recycled slot keeps
         * its dead entry, because the plane it described no longer exists and the alternative --
         * leaving it keyed on an address now owned by a texture it is not -- is the drawing from
         * somebody else's page this function exists to refuse. */
        if (recycled) {
            slot->plane = 0;
            slot->pending = 0;
        }
        return 0;
    }

    *slot = (gs_tex_t){0};
    slot->plane   = plane;
    slot->px_w    = px_w;
    slot->px_h    = px_h;
    slot->pad_w   = pad_w;
    slot->pad_h   = pad_h;
    slot->byte    = byte;
    slot->tba     = tba;
    slot->tbw     = gs_vram_fbw(pad_w);
    slot->tw_exp  = tw_exp;
    slot->th_exp  = th_exp;
    slot->qwc     = pad_w * pad_h / 4u;   /* four bytes a texel, sixteen a quadword */

    /* TCC=1 (RGBA, not a modulated colour key), TFUNC=0 (modulate), and the CLUT fields all
     * zero: a CT32 picture carries its own alpha and reads no CLUT page.
     *
     * The sampler is LINEAR on both axes -- magnification and minification.  That is what the
     * application asks for: `xmb.c` sets GL_TEXTURE_MIN_FILTER and GL_TEXTURE_MAG_FILTER to
     * GL_LINEAR on both sheets, and the POSIX reference (the virgl span loop) honours it, so
     * 1:1 texturing for HW OpenGL means the GS reads bilinearly too.  This was A/B'd against the
     * operator's icon black-rectangles and is NOT their author: the census reads the settled page
     * with correct white glyphs on blue under both filters, so the rectangles are the non-atomic
     * present (see rung 20y), not the sampler.  MMIN=1 is plain bilinear minification, not
     * trilinear: the GS only reaches for a mipmap chain at MMIN>=2 (which would read TEX1's L and
     * MTBA), so a value of 1 needs no chain this port does not build.  LINEAR/LINEAR is the
     * faithful match to the source branch. */
    slot->tex0 = gs_pack_tex0(tba, slot->tbw, GS_PSM_CT32, tw_exp, th_exp,
                              1u, 0u, 0u, 0u, 0u, 0u, 0u);
    slot->tex1 = gs_pack_tex1(0u, 0u, GS_FILTER_LINEAR, GS_FILTER_LINEAR,
                              0u, 0u, 0u);

    /* Repeat on both axes, over the whole plane: `MINU`/`MAXU` are the first and last texel
     * index, so the wrap period is pad_w -- which is what TinyGL's own mask does, and why an
     * overshooting coordinate reads this plane's padding rather than the next plane's texels.
     * The fields are 10 bits, so a plane wider than 1024 texels would have to be described by
     * a smaller clamp; nothing in this port allocates one. */
    slot->clamp = gs_pack_clamp(GS_REPEAT, GS_REPEAT, 0u, pad_w - 1u,
                                0u, pad_h - 1u);
    slot->pending = 1;
    if (!recycled) s_n = index + 1u;

    hand_over(index);
    return slot;
}

const gs_tex_t *gs_tex_find(const void *plane)
{
    uint32_t i;

    if (plane == 0) return 0;
    for (i = 0; i < s_n; i++)
        if (s_reg[i].plane == plane) return &s_reg[i];
    return 0;
}

uint32_t gs_tex_count(void) { return s_n; }

const gs_tex_t *gs_tex_at(uint32_t i)
{
    return (i < s_n) ? &s_reg[i] : 0;
}

void gs_tex_uploaded(uint32_t i)
{
    if (i < s_n) s_reg[i].pending = 0;
}

uint32_t gs_tex_pending(void)
{
    uint32_t i, n = 0u;

    for (i = 0; i < s_n; i++)
        if (s_reg[i].pending) n++;
    return n;
}

uint32_t gs_tex_vram_left(void)
{
    return gs_vram_left(&s_vram);
}

int gs_tex_free(const void *plane)
{
    uint32_t i;
    if (plane == 0) return -1;
    for (i = 0; i < s_n; i++) {
        if (s_reg[i].plane == plane) {
            if (s_reg[i].byte != 0) {
                gs_vram_free(&s_vram, s_reg[i].byte);
            }
            s_reg[i].plane = 0;
            s_reg[i].pending = 0;
            s_reg[i].byte = 0;
            return 0;
        }
    }
    return -1;
}

uint32_t gs_tex_compact(void)
{
    while (s_n > 0 && s_reg[s_n - 1].plane == 0) {
        s_n--;
    }
    return gs_vram_compact(&s_vram);
}

uint32_t gs_tex_zbuf_byte(void)
{
    /* Dedicated 16-bit Z-buffer in upper VRAM (Page 390 = 3,194,880 B).
     * 998,400 bytes (832*600*2) fits within the 132-page headroom above registered textures. */
    return (GS_VRAM_BYTES - 122u * GS_PAGE_FRAME);
}

