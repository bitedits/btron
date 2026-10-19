/*
 * ps2_gs_rec.h — Graphics Interface (GIF) vertex packet recorder for PS2 GS
 *
 * Architecture & Hardware Invariants:
 * This module converts high-level primitive streams (points, lines, triangles, strips,
 * and fans) into densely packed GIF DMA packet streams for the Graphics Synthesizer.
 *
 * Dense REGLIST Packing:
 * 1. Untextured Primitives (GS_REC_NREG_UNTEX = 2 registers):
 *    Registers: RGBAQ (colour), XYZ2 (vertex position & drawing kick).
 *    Packed densely at 64 bits per register (1 quadword per vertex).
 * 2. Textured Primitives (GS_REC_NREG_TEX = 3 registers):
 *    Registers: RGBAQ (colour), ST (texture coordinates), XYZ2 (position & kick).
 *    Packed densely at 64 bits per register (1.5 quadwords per vertex, padded to 128-bit
 *    quadword boundary at group close).
 *
 * Primitive Programming Invariant:
 * On physical GS hardware, the GIF tag's embedded PRIM field with PRE=1 does not
 * reliably configure the primitive mode across packet boundaries. To guarantee
 * deterministic Gouraud shading and primitive selection across all GS revisions,
 * the recorder precedes every vertex burst with an explicit A+D packed register
 * write to GS_REG_PRIM (`GS_REC_AD_QWC`).
 *
 * Purity & Buffer Partitioning:
 * The recorder is pure: it accumulates words into a memory buffer and dispatches
 * them via a caller-provided callback (`gs_rec_sink`) without touching hardware
 * MMIO or DMAC registers directly.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_REC_H
#define PS2_GS_REC_H

#include <stdint.h>

/* Number of 64-bit registers emitted per vertex */
#define GS_REC_REGS_MAX   3u    /* Maximum registers per vertex (textured) */
#define GS_REC_NREG_TEX   3u    /* Textured: RGBAQ, ST, XYZ2 */
#define GS_REC_NREG_UNTEX 2u    /* Untextured: RGBAQ, XYZ2 */

/*
 * Quadwords required for `n` A+D register writes:
 * 1 tag quadword + `n` 128-bit A+D address/data quadwords (FLG=PACKED, NREG=1).
 */
#define GS_REC_AD_QWC(n) (1u + (n))

/* Stream staging buffer capacity in 128-bit quadwords (4096 QW = 64 KiB) */
#ifndef GS_REC_QW_CAP
#define GS_REC_QW_CAP 4096u
#endif

/* Callback invoked when a complete GIF packet group is finalized */
typedef void (*gs_rec_sink)(void *user, const uint64_t *qw, uint32_t qwc);

/* Initialize recorder state, associating the packet dispatch sink */
void gs_rec_init(gs_rec_sink sink, void *user);

/*
 * Declares the active primitive mode (GS_OP_TRI, GS_OP_TRISTRIP, etc.)
 * Closes any open group and stages an A+D write to GS_REG_PRIM before next burst.
 */
void gs_rec_prim(uint64_t prim);

/* Sets the RGBA colour and ST texture coordinates for subsequent vertices */
void gs_rec_color(uint32_t r, uint32_t g, uint32_t b, uint32_t a);
void gs_rec_texcoord(float s, float t);

/*
 * Appends a vertex in device screen pixels relative to the active XYOFFSET origin.
 * Converts integer pixel coordinates to GS 12.4 fixed-point format via gs_vertex_coord16().
 * Returns 0 on success, or -1 if no primitive group is currently open.
 */
int gs_rec_vertex(int32_t x_px, int32_t y_px, uint32_t z);

/* Closes the active primitive group, padding to 128-bit boundary, and dispatches to sink */
void gs_rec_end(void);

/*
 * Emits an inline A+D register configuration group mid-stream (e.g. for texture state switching).
 * Flushes any open vertex group first so state transitions occur at correct temporal boundaries.
 * Returns 0 on success, or -1 if `n` exceeds buffer capacity.
 */
int gs_rec_ad(const uint64_t *data, const uint32_t *gp_offs, uint32_t n);

/* Telemetry counters */
uint32_t gs_rec_sends(void);
uint32_t gs_rec_verts(void);
uint32_t gs_rec_unsplittable(void);
uint32_t gs_rec_prim_writes(void);

/*
 * Computes bounding rectangle of the first vertex group recorded this frame.
 * Populates `l`, `t`, `r`, `b` in device pixels.
 * Returns 0 if a group was dispatched, or -1 if no vertices have been recorded.
 */
int gs_rec_bbox_first(int32_t *l, int32_t *t, int32_t *r, int32_t *b);

#endif /* PS2_GS_REC_H */
