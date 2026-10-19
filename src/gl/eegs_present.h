/*
 * eegs_present.h — Staged GIF stream presentation for PS2 Graphics Synthesizer
 *
 * Architecture & Hardware Invariants:
 * In the hybrid 2D/3D presentation model, the desktop backbuffer and hardware-accelerated
 * OpenGL primitives are synthesized onto the GS framebuffer in strict order:
 *
 *   1. Framebuffer Canvas Upload (Host-to-Local IMAGE DMA stream).
 *   2. Graphics Synthesizer Environment Setup (GS_REG_FRAME, SCISSOR, TEST, ALPHA).
 *   3. Staged Primitive Geometry Bursts (Direct GIF DMA packets).
 *
 * Hardware Pipeline Constraints:
 * 1. Ordering Law: The software canvas upload must strictly precede primitive rendering
 *    because hardware fragments blend directly into existing framebuffer pixels in VRAM.
 *    Any primitive drawn before the canvas upload would be completely overwritten.
 * 2. Deferred DMA Staging: Because application paint callbacks execute inside the compositing
 *    pass while software rasterisation is ongoing, direct hardware DMAs cannot be issued
 *    interleaved with canvas writes. Geometry and state packets are recorded into a contiguous
 *    staging buffer and dispatched to the GIF ring buffer in batch upon frame completion.
 *
 * Zero proprietary Sony SDK code: register encodings follow ps2_gs_pack.h.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef BTRON_GL_EEGS_PRESENT_H
#define BTRON_GL_EEGS_PRESENT_H

#include <btron/types.h>

/* Start frame staging: resets staging pointer and initializes packet recording. */
void eegs_present_begin(void);

/* Finalize frame staging: closes the active packet group without initiating DMA. */
void eegs_present_end(void);

/*
 * Dispatches all staged GIF groups to the hardware GIF DMA ring buffer.
 * Precedes primitives with an environment packet scissored to the window's `client` rect.
 * Returns the number of staged primitive groups dispatched, or -1 on environment failure.
 */
int eegs_present_send(const RECT *client);

/* Frame presentation and hardware pipeline diagnostic statistics */
typedef struct {
    uint32_t groups;         /* Total GIF packet groups dispatched to DMAC */
    uint32_t qw;             /* Total 128-bit quadwords transferred */
    uint32_t verts;          /* Total vertices processed this frame */
    uint32_t prims;          /* Total primitives rasterised this frame */
    uint32_t dropped;        /* Packets dropped due to staging overflow (must be 0) */
    uint32_t unsplittable;   /* Refused packet splits (must be 0) */

    /*
     * Hardware Register Invariant:
     * On physical GS hardware, the GIF tag's embedded PRIM field with PRE=1 does not
     * reliably configure the primitive mode across packet boundaries. To ensure
     * deterministic rasterisation, the recorder precedes every vertex burst with an explicit
     * A+D packed register write to GS_REG_PRIM (`prim_writes`).
     */
    uint32_t prim_writes;
    uint32_t tex_switches;   /* Mid-stream texture register state transitions */
    uint32_t tex_missing;    /* Textured primitive missing texture plane (must be 0) */
    uint32_t prim_moves;     /* PRIM re-declarations requiring new group boundary */

    /* Shading and vertex colour telemetry */
    uint32_t colour_changes; /* Vertex color transitions across primitives */
    uint32_t colour_verts;   /* Vertices carrying explicit RGBAQ color */
    uint32_t colour_first;   /* First vertex color word */
    uint32_t colour_last;    /* Last vertex color word */

    /* Shading modes requested in PRIM descriptors */
    uint32_t prim_iip;       /* Flat (0) vs Gouraud (1) shading modes */
    uint32_t prim_tme;       /* Texture mapping enabled count */
    uint32_t prim_decl;      /* Total PRIM declarations */

    /* Triangle coverage analysis */
    uint32_t wide_tri, wide_flat;
    uint32_t big_area, big_c0, big_c1, big_c2, big_smooth;

    /* Retained mesh batch fast-path telemetry */
    uint32_t grid_batch;     /* Meshes successfully dispatched via fast-path batching */
    uint32_t grid_slow;      /* Meshes falling back to immediate-mode vertex walk */
    uint32_t grid_why;       /* Refusal reason code (TGL_GRID_REFUSE_*) for last fallback */
    uint32_t grid_over;      /* Vertices clipped by GS hardware SCISSOR rect */

    /* Framebuffer plating coverage */
    uint32_t clip_w, clip_h;
    int32_t  b0_l, b0_t, b0_r, b0_b;
    uint32_t has_bbox;
    uint32_t plate_covers;

    /*
     * Inter-Frame Invariance Telemetry (Active when compiled with PS2_IDENT=1):
     * Computes dual 32-bit FNV-1a hashes per staged group to determine whether consecutive
     * frames contain identical geometry, rigid translations, or dynamic deformations.
     */
    uint32_t ident_frames;
    uint32_t ident_groups_prev;   /* Group count of previous frame */
    uint32_t same_groups, same_qw;
    uint32_t diff_groups, diff_qw;
    uint32_t tail_groups, tail_qw;
    uint32_t diff_first, diff_last;
    uint32_t diff_more;

    /* Per-register lane invariance (XYZ2 coordinate, RGBAQ colour, ST texels) */
    uint32_t ident_xyz, ident_rgb, ident_st, ident_shape;
    uint32_t ident_vertex_groups;
} eegs_present_stats;

/* Retrieve statistics for the most recently presented frame */
void eegs_present_get_stats(eegs_present_stats *out);

/* Returns comma-separated list of group indices that differed from the prior frame */
const char *eegs_present_ident_at(void);

/* Number of packet groups currently staged for the active frame */
uint32_t eegs_present_groups_staged(void);

/* Number of packet groups dispatched in the last eegs_present_send() call */
uint32_t eegs_present_groups_sent(void);

/*
 * Diagnostic inspection: copies up to `max_u64` words of raw GIF quadwords from
 * staged group `group` into `out`. Returns the number of words copied.
 */
uint32_t eegs_present_group_peek(uint32_t group, uint64_t *out, uint32_t max_u64);

#endif /* BTRON_GL_EEGS_PRESENT_H */
