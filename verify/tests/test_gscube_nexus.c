/*
 * verify/tests/test_gscube_nexus.c — GScube Multi-Node Cluster Scaling Gate
 *
 * Validates Layer 6 (L6) cluster operations against exact GS/GIF hardware registers:
 *   1. Node configuration and cluster topology invariants (N=1 -> N>1).
 *   2. Uniform broadcast state preservation (MVP matrix, directional lighting).
 *   3. Dynamic screen-space tile partitioning with zero gaps / overlaps.
 *   4. Entity decomposition for AMP demoscene and Quake PVS actor dispatch.
 *   5. Local->Local GIF DMA hardware blit composite packet generation.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/gl/gscube_nexus.c"

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got != want) {
        s_failures++;
        printf("  [FAIL] %-35s got=0x%016llx want=0x%016llx\n",
               what, (unsigned long long)got, (unsigned long long)want);
    }
}

static void check_u32(const char *what, uint32_t got, uint32_t want)
{
    check_eq(what, (uint64_t)got, (uint64_t)want);
}

static void test_node_config(void)
{
    const gscube_node_config_t *cfg;

    /* Default single-node initialization (N=1) */
    gscube_init_node(0u, 1u);
    cfg = gscube_get_config();
    check("default node is 0", cfg->node_id == 0u);
    check("default total nodes is 1", cfg->total_nodes == 1u);
    check("default node is master", cfg->is_master == 1u);

    /* 4-node cluster configuration */
    gscube_init_node(2u, 4u);
    cfg = gscube_get_config();
    check("node 2 id", cfg->node_id == 2u);
    check("total nodes 4", cfg->total_nodes == 4u);
    check("node 2 is worker", cfg->is_master == 0u);

    /* Master node on 16-node cluster */
    gscube_init_node(0u, 16u);
    cfg = gscube_get_config();
    check("node 0 on 16-node is master", cfg->is_master == 1u);
}

static void test_uniform_broadcast(void)
{
    gscube_uniform_payload_t p;
    const gscube_uniform_payload_t *act;
    uint32_t i;

    memset(&p, 0, sizeof(p));
    for (i = 0; i < 16; i++) p.mvp[i] = (float)(i + 1) * 0.5f;
    p.light_dir[0] = 0.0f; p.light_dir[1] = 0.7071f; p.light_dir[2] = 0.7071f; p.light_dir[3] = 0.0f;
    p.light_color[0] = 1.0f; p.light_color[1] = 0.8f; p.light_color[2] = 0.6f; p.light_color[3] = 1.0f;
    p.screen_w = 640u;
    p.screen_h = 480u;
    p.frame_seq = 120u;

    gscube_broadcast_uniforms(&p);
    act = gscube_get_active_uniforms();

    check("uniforms screen_w", act->screen_w == 640u);
    check("uniforms screen_h", act->screen_h == 480u);
    check("uniforms frame_seq", act->frame_seq == 120u);
    check("uniforms mvp[0]", act->mvp[0] == 0.5f);
    check("uniforms mvp[15]", act->mvp[15] == 8.0f);
    check("uniforms light_dir[1]", act->light_dir[1] == 0.7071f);
}

static void test_tile_partition(void)
{
    gscube_tile_rect_t t;

    /* N=1: Full screen */
    gscube_compute_tile_partition(1u, 640u, 480u, 0u, &t);
    check_u32("N=1 tile x0", t.x0, 0u);
    check_u32("N=1 tile y0", t.y0, 0u);
    check_u32("N=1 tile x1", t.x1, 639u);
    check_u32("N=1 tile y1", t.y1, 479u);

    /* N=4: 2x2 grid */
    /* Node 0: Top-left */
    gscube_compute_tile_partition(4u, 640u, 480u, 0u, &t);
    check_u32("N=4 node 0 x0", t.x0, 0u);
    check_u32("N=4 node 0 y0", t.y0, 0u);
    check_u32("N=4 node 0 x1", t.x1, 319u);
    check_u32("N=4 node 0 y1", t.y1, 239u);

    /* Node 1: Top-right */
    gscube_compute_tile_partition(4u, 640u, 480u, 1u, &t);
    check_u32("N=4 node 1 x0", t.x0, 320u);
    check_u32("N=4 node 1 y0", t.y0, 0u);
    check_u32("N=4 node 1 x1", t.x1, 639u);
    check_u32("N=4 node 1 y1", t.y1, 239u);

    /* Node 2: Bottom-left */
    gscube_compute_tile_partition(4u, 640u, 480u, 2u, &t);
    check_u32("N=4 node 2 x0", t.x0, 0u);
    check_u32("N=4 node 2 y0", t.y0, 240u);
    check_u32("N=4 node 2 x1", t.x1, 319u);
    check_u32("N=4 node 2 y1", t.y1, 479u);

    /* Node 3: Bottom-right */
    gscube_compute_tile_partition(4u, 640u, 480u, 3u, &t);
    check_u32("N=4 node 3 x0", t.x0, 320u);
    check_u32("N=4 node 3 y0", t.y0, 240u);
    check_u32("N=4 node 3 x1", t.x1, 639u);
    check_u32("N=4 node 3 y1", t.y1, 479u);
}

static void test_entity_decomposition(void)
{
    gscube_init_node(1u, 4u);

    check_u32("entity 0 on 4 nodes", gscube_assign_entity_node(0u, 4u), 0u);
    check_u32("entity 1 on 4 nodes", gscube_assign_entity_node(1u, 4u), 1u);
    check_u32("entity 2 on 4 nodes", gscube_assign_entity_node(2u, 4u), 2u);
    check_u32("entity 5 on 4 nodes", gscube_assign_entity_node(5u, 4u), 1u);

    /* For node 1 of 4 */
    check("entity 1 is local to node 1", gscube_is_entity_local(1u) == 1);
    check("entity 5 is local to node 1", gscube_is_entity_local(5u) == 1);
    check("entity 0 is not local to node 1", gscube_is_entity_local(0u) == 0);
    check("entity 2 is not local to node 1", gscube_is_entity_local(2u) == 0);
}

static void test_composite_packet(void)
{
    gscube_tile_rect_t tiles[4];
    uint64_t qw[2 * 17];
    int rc;

    /* Build 4 tiles */
    gscube_compute_tile_partition(4u, 640u, 480u, 0u, &tiles[0]);
    gscube_compute_tile_partition(4u, 640u, 480u, 1u, &tiles[1]);
    gscube_compute_tile_partition(4u, 640u, 480u, 2u, &tiles[2]);
    gscube_compute_tile_partition(4u, 640u, 480u, 3u, &tiles[3]);

    tiles[0].vram_byte = 1998848u;
    tiles[1].vram_byte = 2244608u;
    tiles[2].vram_byte = 2490368u;
    tiles[3].vram_byte = 2736128u;

    check_u32("composite packet qwc", gscube_composite_packet_qwc(4u), 17u);

    rc = gscube_build_composite_packet(tiles, 4u, 0u, 10u, qw);
    check("composite packet built successfully", rc == 0);

    /* QW0: GIFTAG with NLOOP=16, EOP=1 */
    check_eq("composite giftag data", qw[0], 0x1000000000008010ULL);
    check_eq("composite giftag regs", qw[1], 0x000000000000000EULL);

    /* QW1..QW4: Tile 0 blit registers */
    /* BITBLTBUF (0x50) */
    check_eq("tile 0 bitbltbuf reg", qw[3], 0x50ULL);
    /* TRXPOS (0x51): DASX=0, DASY=0 */
    check_eq("tile 0 trxpos reg", qw[5], 0x51ULL);
    /* TRXREG (0x52): RRW=320, RRH=240 */
    check_eq("tile 0 trxreg value", qw[6], (240ULL << 32) | 320ULL);
    check_eq("tile 0 trxreg reg", qw[7], 0x52ULL);
    /* TRXDIR (0x53): 2 = Local->Local */
    check_eq("tile 0 trxdir value", qw[8], 2ULL);
    check_eq("tile 0 trxdir reg", qw[9], 0x53ULL);

    /* Tile 1 blit registers: DASX=320, DASY=0 */
    check_eq("tile 1 trxpos value", qw[12], (0ULL << 48) | (320ULL << 32));
    check_eq("tile 1 trxpos reg", qw[13], 0x51ULL);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 / GScube Cluster Scaling Engine (Layer L6) Tests\n");
    printf("==========================================================\n");

    test_node_config();
    test_uniform_broadcast();
    test_tile_partition();
    test_entity_decomposition();
    test_composite_packet();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - GScube cluster scaling engine operates strictly within register limits\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - GScube cluster partition or composite mismatch\n");
    return 1;
}
