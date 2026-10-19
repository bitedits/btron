/*
 * ps2_gs_vram.c — GS local-memory allocation in the registers' own page units
 *
 * See ps2_gs_vram.h for the two page sizes this keeps straight and why getting them wrong is
 * not a rounding detail: a frame buffer offset is a 9-bit index of 8 KB pages, so the same
 * byte offset the texture path expresses as page 7,808 is page 244 here.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "ps2_gs_vram.h"
#include "ps2_gs.h"      /* GS_PSM_CT32 / GS_PSM_CT16: the ordinals a canvas format is named by */

void gs_vram_init(gs_vram_t *v)
{
    /* Above the display, rounded to whole 8 KB pages so the first allocation after it is a legal
     * FRAME.FBA.  gs_disp_reserve_bytes() is the reservation's size and it is the larger of one
     * CT32 page and two CT16 pages -- which are the same 244 pages, so a canvas format change moves
     * no plane. */
    v->next = gs_disp_reserve_bytes();
    v->base = v->next;
    v->block_count = 0u;
}

uint32_t gs_vram_fbw(uint32_t pixels_w)
{
    return (pixels_w + 63u) / 64u;
}

uint32_t gs_vram_frame_bytes(uint32_t pixels_w, uint32_t pixels_h, uint32_t bpp)
{
    return gs_vram_fbw(pixels_w) * 64u * pixels_h * (bpp / 8u);
}

int gs_vram_take(gs_vram_t *v, uint32_t bytes, uint32_t *out_byte)
{
    uint32_t pages, span;

    if (bytes == 0 || v == 0 || out_byte == 0) return -1;

    pages = (bytes + GS_PAGE_FRAME - 1u) / GS_PAGE_FRAME;
    span  = pages * GS_PAGE_FRAME;
    if (v->next + span > GS_VRAM_BYTES) return -1;

    *out_byte = v->next;
    v->next  += span;

    if (v->block_count < GS_VRAM_MAX_BLOCKS) {
        v->blocks[v->block_count].offset = *out_byte;
        v->blocks[v->block_count].size   = span;
        v->blocks[v->block_count].used   = 1u;
        v->block_count++;
    }
    return 0;
}

uint32_t gs_vram_tex_bytes(uint32_t w_texels, uint32_t h_texels)
{
    /* CT32: four bytes a texel, and the width field is a whole number of 64-texel units, so the
     * plane the GS addresses is at least as wide as the unit even when the picture is not. */
    uint32_t w = (w_texels < 64u) ? 64u : w_texels;
    uint32_t bytes = w * h_texels * 4u;

    return (bytes + GS_PAGE_TEX_PLANE - 1u) / GS_PAGE_TEX_PLANE * GS_PAGE_TEX_PLANE;
}

int gs_vram_take_tex(gs_vram_t *v, uint32_t bytes, uint32_t *out_byte)
{
    uint32_t span;

    if (bytes == 0 || v == 0 || out_byte == 0) return -1;

    span = (bytes + GS_PAGE_TEX_PLANE - 1u) / GS_PAGE_TEX_PLANE * GS_PAGE_TEX_PLANE;
    if (v->next + span > GS_VRAM_BYTES) return -1;

    *out_byte = v->next;
    v->next  += span;

    if (v->block_count < GS_VRAM_MAX_BLOCKS) {
        v->blocks[v->block_count].offset = *out_byte;
        v->blocks[v->block_count].size   = span;
        v->blocks[v->block_count].used   = 1u;
        v->block_count++;
    }
    return 0;
}

int gs_vram_free(gs_vram_t *v, uint32_t byte)
{
    uint32_t i;
    if (v == 0) return -1;
    for (i = 0u; i < v->block_count; i++) {
        if (v->blocks[i].offset == byte && v->blocks[i].used) {
            v->blocks[i].used = 0u;
            return 0;
        }
    }
    return -1;
}

uint32_t gs_vram_compact(gs_vram_t *v)
{
    uint32_t reclaimed = 0u;
    if (v == 0) return 0u;

    while (v->block_count > 0u && !v->blocks[v->block_count - 1u].used) {
        uint32_t sz = v->blocks[v->block_count - 1u].size;
        reclaimed += sz;
        if (v->next >= sz) v->next -= sz;
        v->block_count--;
    }
    if (v->block_count == 0u) {
        if (v->next > v->base) {
            reclaimed += (v->next - v->base);
            v->next = v->base;
        }
    }
    return reclaimed;
}

int gs_vram_fba(uint32_t byte, uint32_t *out_fba)
{
    uint32_t page;

    if (out_fba == 0) return -1;
    if ((byte % GS_PAGE_FRAME) != 0) return -1;

    page = byte / GS_PAGE_FRAME;
    if (page > GS_FBA_MAX) return -1;

    *out_fba = page;
    return 0;
}

int gs_vram_tba(uint32_t byte, uint32_t *out_tba)
{
    uint32_t page;

    if (out_tba == 0) return -1;
    if ((byte % GS_PAGE_TEX) != 0) return -1;

    page = byte / GS_PAGE_TEX;
    if (page > GS_TBA_MAX) return -1;

    *out_tba = page;
    return 0;
}

uint32_t gs_vram_left(const gs_vram_t *v)
{
    return GS_VRAM_BYTES - v->next;
}

/* ── Display pages ─────────────────────────────────────────────────────── */

uint32_t gs_disp_bytes_pp(uint32_t psm)
{
    if (psm == GS_PSM_CT32) return 4u;
    if (psm == GS_PSM_CT16 || psm == GS_PSM_CT16S) return 2u;

    /* PSMCT24 and every other ordinal: 0, meaning "this file cannot lay a page out in it".  A
     * default of 4 would have made a CT24 dial look supported and written a 3-byte picture into a
     * page the reader walks 4 bytes at a time. */
    return 0u;
}

uint32_t gs_disp_px_per_qw(uint32_t psm)
{
    const uint32_t pp = gs_disp_bytes_pp(psm);
    return pp ? (16u / pp) : 0u;
}

uint32_t gs_disp_page_bytes(uint32_t psm)
{
    const uint32_t pp = gs_disp_bytes_pp(psm);
    return pp ? (GS_DISP_PX_W * GS_DISP_ROWS * pp) : 0u;
}

uint32_t gs_disp_page_span(uint32_t psm)
{
    const uint32_t bytes = gs_disp_page_bytes(psm);
    if (bytes == 0u) return 0u;
    return (bytes + GS_PAGE_FRAME - 1u) / GS_PAGE_FRAME * GS_PAGE_FRAME;
}

uint32_t gs_disp_reserve_bytes(void)
{
    const uint32_t one_ct32 = gs_disp_page_span(GS_PSM_CT32);
    const uint32_t two_ct16 = gs_disp_page_span(GS_PSM_CT16) * GS_DISP_PAGES;
    return (two_ct16 > one_ct32) ? two_ct16 : one_ct32;
}

int gs_disp_page(uint32_t index, uint32_t psm, gs_disp_page_t *out)
{
    gs_disp_page_t p;
    const uint32_t span = gs_disp_page_span(psm);

    if (out == 0 || span == 0u) return -1;
    if (index >= GS_DISP_PAGES) return -1;
    if ((index + 1u) * span > gs_disp_reserve_bytes()) return -1;

    p.index     = index;
    p.psm       = psm;
    p.byte      = index * span;
    p.rows      = GS_DISP_ROWS;
    p.fbw       = GS_VRAM_CANVAS_FBW;
    p.bytes_pp  = gs_disp_bytes_pp(psm);
    p.px_per_qw = gs_disp_px_per_qw(psm);
    p.bytes     = GS_DISP_PX_W * p.rows * p.bytes_pp;
    p.row_bytes = p.fbw * 64u * p.bytes_pp;

    /* Both field units, checked rather than assumed: the page is a whole number of 8 KB pages by
     * construction, so FBA must take it, and 8192 is a multiple of 256, so TBA must too.  Either
     * refusal means the layout above is wrong, not the request, so it is a -1 and not a rounding. */
    if (gs_vram_fba(p.byte, &p.fba) != 0) return -1;
    if (gs_vram_tba(p.byte, &p.tba) != 0) return -1;

    *out = p;
    return 0;
}

/* ── The live configuration ────────────────────────────────────────────── */

static gs_disp_page_t s_disp[GS_DISP_PAGES];
static uint32_t s_disp_psm   = GS_PSM_CT32;
static uint32_t s_disp_built = 0u;      /* 1 once s_disp[] holds s_disp_psm */
static uint32_t s_disp_shown = 0u;
static uint32_t s_disp_drawn = 0u;
static uint32_t s_disp_double = 0u;     /* rung 4's dial: may the two indices differ? */

static void gs_disp_build(void)
{
    uint32_t i;
    for (i = 0u; i < GS_DISP_PAGES; i++) {
        /* Cannot fail for a psm gs_disp_page_span() accepted: the reservation is sized from the same
         * span and gs_disp_page() checks the index against it. */
        (void)gs_disp_page(i, s_disp_psm, &s_disp[i]);
    }
    s_disp_built = 1u;
    if (s_disp_shown >= GS_DISP_PAGES) s_disp_shown = 0u;
    if (!s_disp_double) {
        s_disp_drawn = s_disp_shown;                    /* one page, one writer: today's present */
    } else {
        s_disp_drawn = (s_disp_shown + 1u) % GS_DISP_PAGES;
    }
}

const gs_disp_page_t *gs_disp_shown(void)
{
    if (!s_disp_built) gs_disp_build();
    return &s_disp[s_disp_shown];
}

const gs_disp_page_t *gs_disp_drawn(void)
{
    if (!s_disp_built) gs_disp_build();
    return &s_disp[s_disp_drawn];
}

uint32_t gs_disp_psm(void)
{
    if (!s_disp_built) gs_disp_build();
    return s_disp_psm;
}

int gs_disp_set_psm(uint32_t psm)
{
    if (gs_disp_page_span(psm) == 0u) return -1;
    if (psm == s_disp_psm && s_disp_built) return 0;
    s_disp_psm = psm;
    gs_disp_build();
    return 0;
}

uint32_t gs_disp_is_double(void)
{
    return s_disp_double;
}

int gs_disp_set_double(uint32_t on)
{
    gs_disp_page_t hidden;

    if (!s_disp_built) gs_disp_build();
    if (!on) {
        s_disp_double = 0u;
        s_disp_drawn = s_disp_shown;
        return 0;
    }
    /* The second page has to exist before anything can be drawn into it.  It does for CT16, where
     * two rounded pages are exactly the reservation, and it does not for CT32, where a second
     * full-width 32-bit page would run under the texture planes: 3,997,696 bytes of display leaves
     * nothing for the atlas.  Refusing here is what makes "double buffer at 32 bpp" a -1 rather than
     * a screen whose back page is somebody else's texels. */
    if (gs_disp_page((s_disp_shown + 1u) % GS_DISP_PAGES, s_disp_psm, &hidden) != 0) return -1;

    s_disp_double = 1u;
    /* The write target follows immediately: a frame that drew into the page it is showing would be
     * the flicker this lane exists to remove. */
    s_disp_drawn = (s_disp_shown + 1u) % GS_DISP_PAGES;
    return 0;
}

uint32_t gs_disp_flip_commit(void)
{
    if (!s_disp_built) gs_disp_build();
    if (!s_disp_double) return s_disp_shown;    /* nothing was staged: no page may be declared seen */
    s_disp_shown = s_disp_drawn;
    s_disp_drawn = (s_disp_shown + 1u) % GS_DISP_PAGES;
    return s_disp_shown;
}
