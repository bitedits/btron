/*
 * src/gl/gscube_nexus.h — GScube Multi-Node Cluster Scaling Engine (Layer L6)
 *
 * Implements Layer 6 (L6) of the Clean-Room PS2 / GScube OpenGL Architecture:
 *   1. SIF-based uniform distribution (MVP matrix, lighting state) across N execution nodes.
 *   2. Dynamic screen-space tile assignment for Quake and UI rasterization.
 *   3. Scene entity and Hopf fibration bundle decomposition for Lil Cu 64.
 *   4. High-speed boundary composite pass using GIF DMA Local->Local blits.
 *
 * Single PS2 corresponds to N=1. GScube scales seamlessly to N > 1 paired EE+GS nodes.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_GSCUBE_NEXUS_H
#define BTRON_GL_GSCUBE_NEXUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GSCUBE_MAX_NODES 16u

typedef struct {
    uint32_t node_id;         /* 0 to total_nodes - 1 */
    uint32_t total_nodes;     /* 1 for single PS2; 2, 4, 8, 16 for GScube */
    uint32_t is_master;       /* 1 if node_id == 0 */
} gscube_node_config_t;

typedef struct {
    uint32_t x0, y0;          /* Sub-rectangle top-left screen pixels */
    uint32_t x1, y1;          /* Sub-rectangle bottom-right screen pixels (inclusive) */
    uint32_t vram_byte;       /* VRAM buffer offset for this node's render tile */
} gscube_tile_rect_t;

/* SIF RPC Uniform Broadcast Payload (Layer L1 -> All L6 Nodes) */
typedef struct {
    float    mvp[16];         /* 4x4 flattened ModelViewProjection matrix */
    float    light_dir[4];    /* Normalized light direction vector */
    float    light_color[4];  /* Light ambient / diffuse RGBA */
    uint32_t screen_w;        /* Master screen width */
    uint32_t screen_h;        /* Master screen height */
    uint32_t frame_seq;       /* Monotonic frame synchronization barrier ID */
} __attribute__((aligned(16))) gscube_uniform_payload_t;

/* Initialize node configuration in cluster */
void gscube_init_node(uint32_t node_id, uint32_t total_nodes);
const gscube_node_config_t *gscube_get_config(void);

/* Uniform broadcast over SIF / InterCore Nexus */
void gscube_broadcast_uniforms(const gscube_uniform_payload_t *payload);
const gscube_uniform_payload_t *gscube_get_active_uniforms(void);

/* Screen tile partitioning for Quake / UI rasterization */
void gscube_compute_tile_partition(uint32_t total_nodes, uint32_t screen_w, uint32_t screen_h,
                                   uint32_t node_id, gscube_tile_rect_t *out_tile);

/* Entity & fiber bundle decomposition for Lil Cu 64 */
uint32_t gscube_assign_entity_node(uint32_t entity_id, uint32_t total_nodes);
int      gscube_is_entity_local(uint32_t entity_id);

/* High-speed GIF DMA blit compositor for stitching sub-tiles onto master canvas */
uint32_t gscube_composite_packet_qwc(uint32_t num_tiles);
int      gscube_build_composite_packet(const gscube_tile_rect_t *tiles, uint32_t num_tiles,
                                       uint32_t dest_dba, uint32_t dest_dbw,
                                       uint64_t *out_qw);

#ifdef __cplusplus
}
#endif

#endif /* BTRON_GL_GSCUBE_NEXUS_H */
