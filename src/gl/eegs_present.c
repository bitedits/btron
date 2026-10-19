/*
 * eegs_present.c — Staged GIF stream management and asynchronous DMA presentation
 *
 * Implements the deferred DMA staging buffer and presentation pipeline for PS2 GS.
 * See eegs_present.h for architectural invariants and ordering rules.
 *
 * Buffer Capacity & Sizing Principles:
 * 1. Staging Buffer Capacity (EEGS_STAGE_QW_CAP = 65,536 quadwords = 1 MiB):
 *    Sized to comfortably accommodate complex frames (such as multi-window desktops
 *    and multi-segment animated wave ribbons) with generous headroom in main RAM.
 * 2. Group Limits (EEGS_MAX_GROUPS = 256):
 *    Bounded so that an entire frame of GIF packet descriptors fits within the
 *    hardware GIF DMA ring buffer (`ps2_gif_ring`).
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "eegs_present.h"

#include "backend_eegs.h"
#include "../drivers/ps2/ps2_gif_ring.h"
#include "../drivers/ps2/ps2_gs.h"
#include "../drivers/ps2/ps2_gs_env.h"
#include "../drivers/ps2/ps2_gs_pack.h"
#include "../drivers/ps2/ps2_gs_rec.h"
#include "../drivers/ps2/ps2_gs_tex.h"
#include "../drivers/ps2/ps2_gs_vram.h"

#define EEGS_STAGE_QW_CAP 65536u
#define EEGS_MAX_GROUPS 256u

/*
 * Compile-time invariant: the GIF ring buffer capacity must strictly exceed
 * the maximum allowable groups per frame plus one environment packet.
 * Tested via negative array size under C99.
 */
typedef char eegs_ring_holds_a_frame[
    (PS2_GIF_RING_CAP > EEGS_MAX_GROUPS + 1u) ? 1 : -1];

/* Staged GIF packet buffer: aligned to 16-byte quadword boundaries for DMAC access */
static uint64_t s_stage[EEGS_STAGE_QW_CAP * 2u] __attribute__((aligned(16)));
static uint32_t s_group_qwc[EEGS_MAX_GROUPS];
static uint32_t s_groups, s_used;

/* Running word offsets for diagnostic inspection of recently dispatched groups */
#define EEGS_PEEK_GROUPS 8u
static uint32_t s_peek_off[EEGS_PEEK_GROUPS];
static uint32_t s_peek_qwc[EEGS_PEEK_GROUPS];
static uint32_t s_peek_n;

static uint32_t s_dropped;

static eegs_present_stats s_stats;

/* ── Inter-Frame Invariance Instrument (`PS2_IDENT=1`) ───────────────────────
 *
 * To determine whether successive frames submit identical geometry, rigid translations,
 * or dynamically deformed meshes, this diagnostic instrument computes per-group and
 * per-register 64-bit FNV-1a hashes:
 *
 * 1. Arithmetic Efficiency:
 *    Uses two parallel 32-bit FNV-1a hashes folded over 64-bit words, requiring only
 *    32-bit integer multiplications. This avoids expensive 64-bit software multiplication
 *    emulation on the 32-bit MIPS o32 ABI.
 *
 * 2. Positional Matching:
 *    Groups are compared by index between frame N and frame N-1. If groups match bit-for-bit,
 *    `same_groups` increments. If a mesh count shifts, `tail_groups` records the divergence.
 *
 * 3. Register Lane Analysis:
 *    In REGLIST vertex packets, registers are mapped by the GIF tag's REGS list:
 *      - XYZ2 lane: tests geometric coordinate invariance.
 *      - Shape lane: tests delta between consecutive vertices, detecting rigid translations.
 *      - RGBAQ lane: tests colour invariance.
 *      - ST lane: tests texture coordinate invariance.
 */
#ifndef BTRON_PS2_GS_IDENT
#define BTRON_PS2_GS_IDENT 0
#endif

#if BTRON_PS2_GS_IDENT
#define IDENT_H0 2166136261u
#define IDENT_H1 16777619u
#define IDENT_AT_MAX 8u

/* The per-group tuple's columns. */
#define ID_A 0u                    /* whole-group FNV lane a */
#define ID_B 1u                    /* whole-group FNV lane b */
#define ID_QW 2u                   /* this group's quadword count */
#define ID_RGB 3u                  /* the RGBAQ lane alone */
#define ID_ST 4u                   /* the ST lane alone */
#define ID_XYZ 5u                  /* the XYZ2 lane alone */
#define ID_SHAPE 6u                /* the XYZ2 lane read as DIFFERENCES */
#define ID_KIND 7u                 /* ID_VERTEX or ID_AD */
#define ID_N 8u

#define ID_VERTEX 1u
#define ID_AD 2u

/* Distinct, nonzero seeds, one per lane: a lane that folded nothing and a lane whose words happened
 * to hash to another lane's value are then both impossible readings, and the kind column is the
 * only thing the lane comparison trusts for "this group has this register at all". */
#define ID_SEED_RGB 0x9E3779B9u
#define ID_SEED_ST  0x85EBCA77u
#define ID_SEED_XYZ 0xC2B2AE3Du
#define ID_SEED_SHAPE 0x165667B1u

/* {fnv lane a, lane b, qwc, rgb, st, xyz, shape, kind}, one row per staged group, kept for both the
 * frame being built and the last frame that was presented. */
static uint32_t s_ident_cur[EEGS_MAX_GROUPS][ID_N];
static uint32_t s_ident_prev[EEGS_MAX_GROUPS][ID_N];
static uint32_t s_ident_prev_groups;
static uint32_t s_ident_frames;
static uint32_t s_ident_vertex;   /* vertex groups staged this frame, whatever they hashed to */

static void ident_fold(uint32_t *ha, uint32_t *hb, uint64_t w)
{
    *ha = (*ha ^ (uint32_t)w) * 16777619u;
    *hb = (*hb + (uint32_t)(w >> 32)) * 2246822519u;
}

/* One register lane's hash: a 32-bit FNV-1a step, one multiply per folded word, and a seed per lane
 * so a lane of three vertices cannot read as an empty one. */
static void ident_fold1(uint32_t *h, uint64_t w)
{
    *h = (*h ^ (uint32_t)w) * 16777619u;
}

/* A group index written as decimal into `s_ident_at`, with no libc in this TU: the present formats
 * nothing today, and pulling snprintf in for one bench column would put a printf back into a unit
 * the shipped image links. */
static char s_ident_at[4u * IDENT_AT_MAX + 8u];
static uint32_t s_ident_at_len;

static void at_putc(char c)
{
    if (s_ident_at_len + 2u < sizeof s_ident_at) s_ident_at[s_ident_at_len++] = c;
    s_ident_at[s_ident_at_len] = '\0';
}

static void at_put_index(uint32_t n, int first)
{
    char digits[10];
    uint32_t k = 0u;

    if (!first) at_putc(',');
    do {
        digits[k++] = (char)('0' + (n % 10u));
        n /= 10u;
    } while (n);
    while (k) at_putc(digits[--k]);
}
#endif /* BTRON_PS2_GS_IDENT */

uint32_t eegs_present_groups_staged(void)
{
    return s_groups;
}

/* Number of groups dispatched in the most recent send() invocation. */
uint32_t eegs_present_groups_sent(void)
{
    return s_peek_n;
}

/*
 * Reads raw GIF quadwords from staged group `group` for diagnostic validation.
 * Offsets and lengths mirror the packets actually committed to the DMA ring buffer.
 */
uint32_t eegs_present_group_peek(uint32_t group, uint64_t *out, uint32_t max_u64)
{
    uint32_t i, n;

    if (out == 0 || max_u64 == 0u || group >= s_peek_n) return 0u;

    n = 2u * s_peek_qwc[group];
    if (n > max_u64) n = max_u64;
    for (i = 0u; i < n; i++) out[i] = s_stage[2u * s_peek_off[group] + i];
    return n;
}

#if BTRON_PS2_GS_IDENT
/* Compare this frame's staged groups against the last presented frame's, index for index, and
 * carry this frame's tuples forward.  Called once per presented frame, from the stats block, so
 * the numbers on the row describe the stream that actually went on the bus -- and so a frame that
 * staged nothing never consumes a predecessor it did not replace. */
static void ident_report(uint32_t groups)
{
    uint32_t i, shown = 0u, more;
    const uint32_t n = (groups < s_ident_prev_groups) ? groups : s_ident_prev_groups;

    s_ident_at_len = 0u;
    s_ident_at[0] = '\0';

    s_stats.ident_frames = ++s_ident_frames;
    s_stats.ident_groups_prev = s_ident_prev_groups;
    s_stats.same_groups = s_stats.same_qw = 0u;
    s_stats.diff_groups = s_stats.diff_qw = 0u;
    s_stats.tail_groups = s_stats.tail_qw = 0u;
    s_stats.diff_first = s_stats.diff_last = s_stats.diff_more = 0u;
    s_stats.ident_xyz = s_stats.ident_rgb = s_stats.ident_st = s_stats.ident_shape = 0u;
    /* `v=` is the frame's OWN vertex-group count, staged or dropped, taken from the sink rather than
     * from the aligned prefix: `g=` minus `v=` is the AD groups, and a lane count against `v=` is the
     * arithmetic that says how many of this frame's meshes kept that field. */
    s_stats.ident_vertex_groups = s_ident_vertex;

    for (i = 0u; i < n; i++) {
        const int same = (s_ident_cur[i][ID_A] == s_ident_prev[i][ID_A]) &&
                         (s_ident_cur[i][ID_B] == s_ident_prev[i][ID_B]) &&
                         (s_ident_cur[i][ID_QW] == s_ident_prev[i][ID_QW]);

        /* The register lanes, over the aligned prefix and only where BOTH frames' group at this
         * index is a vertex group: an AD group has no XYZ2 quadword to be invariant about. */
        if (s_ident_cur[i][ID_KIND] == ID_VERTEX &&
            s_ident_prev[i][ID_KIND] == ID_VERTEX) {
            if (s_ident_cur[i][ID_XYZ] == s_ident_prev[i][ID_XYZ]) s_stats.ident_xyz++;
            if (s_ident_cur[i][ID_RGB] == s_ident_prev[i][ID_RGB]) s_stats.ident_rgb++;
            if (s_ident_cur[i][ID_ST] == s_ident_prev[i][ID_ST]) s_stats.ident_st++;
            if (s_ident_cur[i][ID_SHAPE] == s_ident_prev[i][ID_SHAPE]) s_stats.ident_shape++;
        }

        if (same) {
            s_stats.same_groups++;
            s_stats.same_qw += s_ident_cur[i][ID_QW];
            continue;
        }
        s_stats.diff_groups++;
        s_stats.diff_qw += s_ident_cur[i][ID_QW];
        if (s_stats.diff_groups == 1u) s_stats.diff_first = i;
        s_stats.diff_last = i;
        if (shown < IDENT_AT_MAX) {
            at_put_index(i, (shown == 0u) ? 1 : 0);
            shown++;
        }
    }

    /* The groups only one frame has, on both sides and in the same column: a frame that gained a
     * texture switch and one that lost a group are the same reading -- a span that cannot be
     * replayed where it sits. */
    for (i = n; i < groups; i++) {
        s_stats.tail_groups++;
        s_stats.tail_qw += s_ident_cur[i][ID_QW];
    }
    for (i = n; i < s_ident_prev_groups; i++) {
        s_stats.tail_groups++;
        s_stats.tail_qw += s_ident_prev[i][ID_QW];
    }

    more = s_stats.diff_groups - shown;
    s_stats.diff_more = more;
    if (more) {
        at_putc('+');
        at_put_index(more, 1);
    }

    for (i = 0u; i < groups; i++) {
        uint32_t c;

        for (c = 0u; c < ID_N; c++) s_ident_prev[i][c] = s_ident_cur[i][c];
    }
    s_ident_prev_groups = groups;
}

const char *eegs_present_ident_at(void)
{
    return s_ident_at;
}
#else
const char *eegs_present_ident_at(void)
{
    return "";
}
#endif /* BTRON_PS2_GS_IDENT */

/* The recorder's sink, called from inside the application's paint callback.  It copies: the
 * recorder reuses its own buffer for the next group the moment this returns, so a sink that
 * merely remembered a pointer would be pointing at the following group by present time. */
static void eegs_stage_sink(void *user, const uint64_t *qw, uint32_t qwc)
{
    uint32_t i;
#if BTRON_PS2_GS_IDENT
    uint32_t ha = IDENT_H0, hb = IDENT_H1;
    uint32_t lrgb = ID_SEED_RGB, lst = ID_SEED_ST, lxyz = ID_SEED_XYZ;
    uint32_t lshape = ID_SEED_SHAPE;
    uint32_t px = 0u, py = 0u, pz = 0u, first = 1u;
    /* The group's kind comes out of its own tag, because the sink is handed words and nothing else:
     * a vertex group packs a multi-register REGS list and an AD group names one register and does
     * not, so NREG is bits [63:60] of the tag's low half while FLG is [59:58], and the pair of them
     * is the whole test.  Both fields are read because the lane mapping below is only true of a
     * packed list: NREG alone would let a group that names three registers in three separate
     * quadwords -- the shape LANE 19 rung 5 retired -- fold its XYZ2 lane out of what is actually its
     * ST.  The register count itself is not compared against anything here, because since rung 12f
     * the recorder's two lists are two and three wide and the mapping reads the count back off the
     * tag.  Anything else folds no lane.  The report side re-checks this column, so the gate here is
     * the redundant half of the pair -- measured, deleting it loses no check -- and it stays because
     * folding words a group does not have is worse to reason about than folding nothing and letting
     * one column say why. */
    const uint32_t nreg = (uint32_t)(qw[0] >> 60) & 0xFu;
    const uint32_t flg  = (uint32_t)(qw[0] >> 58) & 3u;
    uint32_t kind = 0u;

    if (flg == GS_FLG_REGLIST && nreg >= GS_REC_NREG_UNTEX && nreg <= GS_REC_REGS_MAX) kind = ID_VERTEX;
    else if (nreg == 1u && flg == GS_FLG_PACKED) kind = ID_AD;
#endif

    (void)user;

    if (qwc == 0u || s_groups >= EEGS_MAX_GROUPS || s_used + qwc > EEGS_STAGE_QW_CAP) {
        s_dropped++;
        return;
    }

    /* Word by word rather than through a memmove: this file is compiled into the host gate as
     * well as the image, and a copy loop has one implementation instead of two. */
    for (i = 0; i < 2u * qwc; i++) {
        s_stage[2u * s_used + i] = qw[i];
#if BTRON_PS2_GS_IDENT
        ident_fold(&ha, &hb, qw[i]);
        /* The tag occupies one quadword (header in low 64 bits, REGS list in high 64 bits).
         * Vertex data begins at quadword offset 2, packed densely in REGLIST order.
         * The loop dispatches by the active register nibble defined in the GIF tag. */
        if (kind == ID_VERTEX && i >= 2u) {
            const uint32_t reg = (i - 2u) % nreg;
            const uint32_t nib = (uint32_t)((qw[1u] >> (4u * reg)) & 0xFu);

            switch (nib) {
            case GS_NIB_RGBAQ: ident_fold1(&lrgb, qw[i]); break;
            case GS_NIB_ST:    ident_fold1(&lst,  qw[i]); break;
            case GS_NIB_XYZ2:
                ident_fold1(&lxyz, qw[i]);
                /*
                 * Shape invariance analysis: folds the difference between consecutive vertices
                 * (X, Y, Z coordinates). Translations preserve vertex deltas, allowing the instrument
                 * to distinguish rigid translations from mesh deformations.
                 */
                {
                    const uint64_t w = qw[i];
                    const uint32_t x = (uint32_t)(w & 0xFFFFu);
                    const uint32_t y = (uint32_t)((w >> 16) & 0xFFFFu);
                    const uint32_t z = (uint32_t)(w >> 32);

                    if (!first) {
                        ident_fold1(&lshape, x - px);
                        ident_fold1(&lshape, y - py);
                        ident_fold1(&lshape, z - pz);
                    }
                    px = x; py = y; pz = z;
                    first = 0u;
                }
                break;
            default: break;                              /* PRIM  */
            }
        }
#endif
    }
#if BTRON_PS2_GS_IDENT
    /* The tag PRIM and the REGS list are inside those words, so a group that changed its primitive
     * type or its loop count without changing a coordinate hashes differently on its own. */
    ha = (ha ^ qwc) * 16777619u;
    s_ident_cur[s_groups][ID_A] = ha;
    s_ident_cur[s_groups][ID_B] = hb;
    s_ident_cur[s_groups][ID_QW] = qwc;
    s_ident_cur[s_groups][ID_RGB] = lrgb;
    s_ident_cur[s_groups][ID_ST] = lst;
    s_ident_cur[s_groups][ID_XYZ] = lxyz;
    s_ident_cur[s_groups][ID_SHAPE] = lshape;
    s_ident_cur[s_groups][ID_KIND] = kind;
    if (kind == ID_VERTEX) s_ident_vertex++;
#endif

    s_group_qwc[s_groups++] = qwc;
    s_used += qwc;
}

void eegs_present_begin(void)
{
    /*
     * Asynchronous DMA Reaping:
     * Reaping completed GIF transfers occurs lazily at the start of the next frame's
     * staging rather than blocking synchronously at the end of send().
     * This allows the EE CPU to proceed with compositing while the Graphics Synthesizer
     * drains the prior frame's DMA bursts asynchronously in the background.
     */
    (void)ps2_gif_reap();

    s_groups = 0u;
    s_used = 0u;
    s_dropped = 0u;
#if BTRON_PS2_GS_IDENT
    s_ident_vertex = 0u;
#endif
    eegs_frame_begin(eegs_stage_sink, 0);
}

void eegs_present_end(void)
{
    /* Finalizes the active group. No DMA is initiated until after canvas compositing. */
    eegs_frame_end();
}

int eegs_present_send(const RECT *client)
{
    gs_draw_env env;
    uint32_t i, off, qw_sent, posted, sent;

    if (client == 0 || s_groups == 0u) return 0;
    if (client->right <= client->left || client->bottom <= client->top) return 0;

    /* Framebuffer destination page obtained from active VRAM allocator */
    const gs_disp_page_t *drawn = gs_disp_drawn();

    env.frame_byte   = drawn->byte;
    env.frame_px_w   = drawn->fbw * 64u;
    env.frame_psm    = drawn->psm;
    env.frame_fmsk   = 0u;

    if (eegs_depth_test_active()) {
        env.zbuf_byte    = gs_tex_zbuf_byte();
        env.zbuf_psm     = GS_PSMZ16;
        env.zbuf_zmsk    = 0u;
    } else {
        /* The depth buffer this port never uses.  TEST.ZTEN is clear and ZMSK is set, so the GS
         * neither reads nor writes these bytes; what it does need is a register that names a page no
         * other allocation owns, and the top page of VRAM is that page -- the texture cursor starts
         * above the canvas and grows *up* from there, so it reaches page 511 only after it has taken
         * every other byte in the chip. */
        env.zbuf_byte    = GS_VRAM_BYTES - GS_PAGE_FRAME;
        env.zbuf_psm     = GS_PSMZ32;
        env.zbuf_zmsk    = 1u;
    }

    /* The window's client rect, in both of the units the two registers want: XYOFFSET is the page
     * origin the vertices are relative to, so the app's (0,0) lands on the client's top-left, and
     * SCISSOR is in plain screen pixels and bounds the primitive to the same rect.  Exclusive
     * edges become inclusive ones here because that is what the field means. */
    env.win_x = (uint32_t)client->left;
    env.win_y = (uint32_t)client->top;
    env.clip_x0 = (uint32_t)client->left;
    env.clip_y0 = (uint32_t)client->top;
    env.clip_x1 = (uint32_t)client->right - 1u;
    env.clip_y1 = (uint32_t)client->bottom - 1u;

    if (eegs_depth_test_active()) {
        /* ZTEN=1 (Z test enable), ZTMETH=2 (GREATER_EQUAL) */
        env.reg_test  = gs_pack_test(0u, 0u, 0u, 0u, 0u, 0u, 1u, 2u);
    } else {
        /* No test of any kind: a 2D UI's order is its draw order, and an alpha test would throw away
         * fragments the software path drew. */
        env.reg_test  = 0u;
    }
    env.reg_alpha = gs_pack_alpha(GS_BLEND_CS, GS_BLEND_CD, GS_ALPHA_AS, GS_BLEND_CD, 0u);

    /* The texture trio is zero here because this port never has one texture per frame: the XMB
     * binds an icon sheet and a font sheet, and backend_eegs.c's eegs_texture() writes the three
     * registers between the vertex groups.  What has to be true before the first vertex is that
     * TME is off for an untextured primitive, and that is PRIM's field, not this one's. */
    env.reg_clamp = 0u;
    env.reg_tex0  = 0u;
    env.reg_tex1  = 0u;

#ifndef UNCACHED
#if defined(BTRON_PS2_TARGET)
#define UNCACHED(p) ((void *)((uintptr_t)(p) | 0x20000000UL))
#else
#define UNCACHED(p) (p)
#endif
#endif

    if (eegs_depth_test_active()) {
        /* Emit a color-masked (FMSK=0xFFFFFFFF) sprite clear of the entire screen to Z=0 so the
         * hardware depth buffer is clean before rendering this frame's 3D primitives. */
        static uint64_t clr_pkt[18] __attribute__((aligned(16)));
        uint64_t *cp = (uint64_t *)UNCACHED(clr_pkt);
        uint32_t zba = 0u, fba = 0u;
        (void)gs_vram_fba(env.zbuf_byte, &zba);
        /* The frame side of the same two conversions the environment packet made a few lines above,
         * from the same env: this clear runs between the environment and the primitives, and a FRAME
         * here that described a different page than the draws following it would mask the wrong
         * bytes -- the reason the words come from env and not from another copy of the literals. */
        (void)gs_vram_fba(env.frame_byte, &fba);

        *cp++ = gs_pack_giftag(8u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
        *cp++ = gs_pack_regs((const uint32_t[]){ GS_NIB_AD }, 1u);

        /* 1. XYOFFSET: screen origin (0, 0) */
        *cp++ = gs_pack_xyoffset(GS_XYOFFSET_PAGE, GS_XYOFFSET_PAGE);
        *cp++ = GS_GP_XYOFFSET_1;
        /* 2. SCISSOR: whole screen */
        *cp++ = gs_pack_scissor(0u, (uint32_t)PS2_SCREEN_WIDTH - 1u, 0u, 599u);
        *cp++ = GS_GP_SCISSOR_1;
        /* 3. FRAME: mask all color writes */
        *cp++ = gs_pack_frame(fba, gs_vram_fbw(env.frame_px_w), env.frame_psm, 0xFFFFFFFFu);
        *cp++ = GS_GP_FRAME_1;
        /* 4. ZBUF: unmasked */
        *cp++ = gs_pack_zbuf(zba, env.zbuf_psm, 0u);
        *cp++ = GS_GP_ZBUF_1;
        /* 5. TEST: ZTEN = 0 (always write Z) */
        *cp++ = 0u;
        *cp++ = GS_GP_TEST_1;
        /* 6. PRIM: SPRITE */
        *cp++ = gs_pack_prim(GS_OP_SPRITE, 0u, 0u, 0u, 0u, 0u);
        *cp++ = GS_GP_PRIM;
        /* 7. XYZ2: top-left (0, 0, Z=0) */
        *cp++ = gs_pack_xyz2(gs_vertex_coord16(0), gs_vertex_coord16(0), 0u);
        *cp++ = GS_GP_XYZ2;
        /* 8. XYZ2: bottom-right (PS2_SCREEN_WIDTH, 600, Z=0) */
        *cp++ = gs_pack_xyz2(gs_vertex_coord16(PS2_SCREEN_WIDTH), gs_vertex_coord16(600), 0u);
        *cp++ = GS_GP_XYZ2;

        if (ps2_gif_post(clr_pkt, 9u) != 0) return -1;
    }

    if (eegs_is_dual_context()) {
        /*
         * Dual Hardware Drawing Contexts (GS Context 1 & Context 2):
         *
         * The GS provides two concurrent drawing contexts selectable via PRIM.CTXT:
         *   - Context 1 (CTXT=0): Standard source-over alpha blending
         *     Equation: (Cs - Cd) * As >> 7 + Cd
         *   - Context 2 (CTXT=1): Additive destination brighten blending
         *     Equation: (Cd - 0) * As >> 7 + Cd  (i.e. Cd * (1 + As/128))
         *
         * Context 2 duplicates all geometric transformation, scissoring, and framebuffer
         * state from Context 1, differing exclusively in its programmed ALPHA blending equation.
         */
        gs_draw_env env2 = env;
        env2.reg_alpha = gs_pack_alpha(GS_BLEND_CD, GS_BLEND_ZERO, GS_ALPHA_AS, GS_BLEND_CD, 0u);
        if (ps2_gs_draw_dual_env_apply(&env, &env2) != 0) return -1;
    } else {
        if (ps2_gs_draw_env_apply(&env) != 0) return -1;
    }

    /*
     * Dispatch Staged Packets to GIF Ring Buffer:
     *
     * Packets are posted asynchronously to the ring FIFO. Because the buffer descriptors
     * reference the staging array, the memory remains valid until the next frame's reap.
     */
    off = 0u;
    qw_sent = 0u;
    posted = 0u;
    for (i = 0u; i < s_groups; i++) {
        if (ps2_gif_post(&s_stage[2u * off], s_group_qwc[i]) != 0) {
            s_dropped += s_groups - i;
            break;
        }
        qw_sent += s_group_qwc[i];
        posted++;
        if (i < EEGS_PEEK_GROUPS) {
            s_peek_off[i] = off;
            s_peek_qwc[i] = s_group_qwc[i];
        }
        off += s_group_qwc[i];
    }
    s_peek_n = (posted < EEGS_PEEK_GROUPS) ? posted : EEGS_PEEK_GROUPS;

    s_stats.groups = s_groups;
    s_stats.qw = qw_sent;
    s_stats.verts = eegs_verts();
    s_stats.prims = eegs_prims();
    s_stats.dropped = s_dropped;
    s_stats.unsplittable = gs_rec_unsplittable();
    s_stats.prim_writes = gs_rec_prim_writes();
    s_stats.tex_switches = eegs_tex_switches();
    s_stats.tex_missing = eegs_tex_missing();
    s_stats.prim_moves = eegs_prim_moves();
    s_stats.colour_changes = eegs_colour_changes();
    s_stats.colour_verts = eegs_colour_verts();
    s_stats.colour_first = eegs_colour_first();
    s_stats.colour_last = eegs_colour_last();
    s_stats.prim_iip = eegs_prim_iip();
    s_stats.prim_tme = eegs_prim_tme();
    s_stats.prim_decl = eegs_prim_decl();
    s_stats.wide_tri = eegs_wide_tri();
    s_stats.wide_flat = eegs_wide_flat();
    s_stats.big_area = eegs_big_area();
    s_stats.big_c0 = eegs_big_c0();
    s_stats.big_c1 = eegs_big_c1();
    s_stats.big_c2 = eegs_big_c2();
    s_stats.big_smooth = eegs_big_smooth();
    /* LANE 12f rung (ii): retained meshes drawn as one strip, and meshes that asked and were
     * refused.  The two are read together -- `slow=` climbing on a fixed-size window says the gate
     * is turning work away that used to be fast, and `batch=` beside a sheet with an edge missing
     * says the GS's clip window is not taking the overhang the batch relied on.  `why=` names the
     * last refusal, and `over=` counts the batched points outside the rect that SCISSOR is being
     * asked to cut. */
    s_stats.grid_batch = eegs_grid_batch();
    s_stats.grid_slow = eegs_grid_slow();
    s_stats.grid_why = eegs_grid_why();
    s_stats.grid_over = eegs_grid_over();
#if BTRON_PS2_GS_IDENT
    ident_report(s_groups);
#endif

    /* Whether this stream plates the rect it is scissored to, which is the condition the window
     * walk checks before it lets a layer's own body go unfilled in the canvas.  The clip's right
     * and bottom edges are exclusive in `client` and inclusive in the span, so the comparison is
     * against w-1 and h-1; `has_bbox` stays clear when the frame drew no vertex group at all,
     * which is a layer that painted nothing and therefore plates nothing. */
    {
        int32_t l = 0, t = 0, r = -1, b = -1;
        s_stats.has_bbox = (uint32_t)(gs_rec_bbox_first(&l, &t, &r, &b) == 0);
        s_stats.b0_l = l; s_stats.b0_t = t; s_stats.b0_r = r; s_stats.b0_b = b;
        s_stats.clip_w = (uint32_t)(client->right - client->left);
        s_stats.clip_h = (uint32_t)(client->bottom - client->top);
        s_stats.plate_covers = (uint32_t)(s_stats.has_bbox &&
                                          l <= 0 && t <= 0 &&
                                          r >= (int32_t)s_stats.clip_w - 1 &&
                                          b >= (int32_t)s_stats.clip_h - 1);
    }

    /* The frame is over: the next begin() starts a fresh stage, and a group that was sent is never
     * sent twice.  `sent` is the groups the ring ACCEPTED, not the groups the stage holds, so a
     * refusal shows up as the shorter frame it is wherever this return value is read. */
    sent = posted;
    s_groups = 0u;
    s_used = 0u;
    return (int)sent;
}

void eegs_present_get_stats(eegs_present_stats *out)
{
    if (out) *out = s_stats;
}
