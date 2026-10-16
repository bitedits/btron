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

void gs_vram_init(gs_vram_t *v)
{
    v->next = (GS_VRAM_CANVAS_BYTES + GS_PAGE_FRAME - 1u) / GS_PAGE_FRAME * GS_PAGE_FRAME;
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
