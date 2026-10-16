/*
 * src/gl/gscube_nexus.c — GScube Multi-Node Cluster Scaling Engine
 *
 * Clean-room implementation of Layer 6 (L6) cluster operations:
 *   - Uniform distribution over SIF / InterCore Nexus
 *   - Screen tile partitioning for parallel rasterization
 *   - Entity & fiber decomposition for AMP demo workloads
 *   - Local->Local GIF DMA hardware blit compositor
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "gscube_nexus.h"
#include "../drivers/ps2/ps2_gs.h"
#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_vram.h"
#include <string.h>

static gscube_node_config_t     s_node_cfg = { 0u, 1u, 1u };
static gscube_uniform_payload_t s_active_uniforms;

void gscube_init_node(uint32_t node_id, uint32_t total_nodes)
{
    if (total_nodes == 0u) total_nodes = 1u;
    if (total_nodes > GSCUBE_MAX_NODES) total_nodes = GSCUBE_MAX_NODES;
    if (node_id >= total_nodes) node_id = 0u;

    s_node_cfg.node_id     = node_id;
    s_node_cfg.total_nodes = total_nodes;
    s_node_cfg.is_master   = (node_id == 0u) ? 1u : 0u;
}

const gscube_node_config_t *gscube_get_config(void)
{
    return &s_node_cfg;
}

void gscube_broadcast_uniforms(const gscube_uniform_payload_t *payload)
{
    if (payload != NULL) {
        memcpy(&s_active_uniforms, payload, sizeof(gscube_uniform_payload_t));
    }
}

const gscube_uniform_payload_t *gscube_get_active_uniforms(void)
{
    return &s_active_uniforms;
}

void gscube_compute_tile_partition(uint32_t total_nodes, uint32_t screen_w, uint32_t screen_h,
                                   uint32_t node_id, gscube_tile_rect_t *out_tile)
{
    uint32_t cols = 1u, rows = 1u;
    uint32_t col, row;
    uint32_t tile_w, tile_h;

    if (out_tile == NULL) return;

    if (total_nodes <= 1u) {
        cols = 1u; rows = 1u;
    } else if (total_nodes <= 2u) {
        cols = 2u; rows = 1u;
    } else if (total_nodes <= 4u) {
        cols = 2u; rows = 2u;
    } else if (total_nodes <= 8u) {
        cols = 4u; rows = 2u;
    } else {
        cols = 4u; rows = 4u;
    }

    if (node_id >= total_nodes) node_id = 0u;

    col = node_id % cols;
    row = node_id / cols;

    tile_w = screen_w / cols;
    tile_h = screen_h / rows;

    out_tile->x0 = col * tile_w;
    out_tile->x1 = (col + 1u == cols) ? (screen_w - 1u) : ((col + 1u) * tile_w - 1u);

    out_tile->y0 = row * tile_h;
    out_tile->y1 = (row + 1u == rows) ? (screen_h - 1u) : ((row + 1u) * tile_h - 1u);

    out_tile->vram_byte = 0u;
}

uint32_t gscube_assign_entity_node(uint32_t entity_id, uint32_t total_nodes)
{
    if (total_nodes <= 1u) return 0u;
    return entity_id % total_nodes;
}

int gscube_is_entity_local(uint32_t entity_id)
{
    if (s_node_cfg.total_nodes <= 1u) return 1;
    return (entity_id % s_node_cfg.total_nodes) == s_node_cfg.node_id;
}

uint32_t gscube_composite_packet_qwc(uint32_t num_tiles)
{
    /* 1 GIFTAG quadword + (4 AD quadwords per tile) */
    return 1u + num_tiles * 4u;
}

int gscube_build_composite_packet(const gscube_tile_rect_t *tiles, uint32_t num_tiles,
                                  uint32_t dest_dba, uint32_t dest_dbw,
                                  uint64_t *out_qw)
{
    static const uint32_t s_regs_ad[1] = { GS_NIB_AD };
    uint64_t *c = out_qw;
    uint32_t i;

    if (tiles == NULL || out_qw == NULL || num_tiles == 0u) return -1;

    /* Single GIF packet with NLOOP = num_tiles * 4 */
    c = gs_pack_pair(c, gs_pack_giftag(num_tiles * 4u, 1u, 0u, 0u, GS_FLG_PACKED, 1u),
                     gs_pack_regs(s_regs_ad, 1u));

    for (i = 0u; i < num_tiles; i++) {
        const gscube_tile_rect_t *t = &tiles[i];
        uint32_t w, h;
        uint32_t sba, sbw;

        if (t->x1 < t->x0 || t->y1 < t->y0) return -1;
        w = t->x1 - t->x0 + 1u;
        h = t->y1 - t->y0 + 1u;

        sba = t->vram_byte / GS_PAGE_TEX;
        sbw = gs_vram_fbw(w);

        /* BITBLTBUF */
        c = gs_pack_pair(c, gs_pack_bitbltbuf(sba, sbw, GS_PSM_CT32,
                                             dest_dba, dest_dbw, GS_PSM_CT32),
                         GS_GP_BITBLTBUF);

        /* TRXPOS: SASX=0, SASY=0, DASX=t->x0, DASY=t->y0, DIR=0 */
        c = gs_pack_pair(c, gs_pack_trxpos(0u, 0u, t->x0, t->y0, 0u),
                         GS_GP_TRXPOS);

        /* TRXREG: width, height */
        c = gs_pack_pair(c, gs_pack_trxreg(w, h),
                         GS_GP_TRXREG);

        /* TRXDIR: 2 = Local->Local transfer */
        c = gs_pack_pair(c, gs_pack_trxdir(2u),
                         GS_GP_TRXDIR);
    }

    return 0;
}
