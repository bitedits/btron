/*
 * verify/tests/test_eegs_raster.c -- what the raster stage decides, against words written by hand
 *
 * Row 5, step 5a of the plan of record (doc/md/PS2.md, "The EE/GS backend").  The discipline is
 * increments 2, 3 and 4's: `backend_eegs.c` and the recorder are compiled into this program, and
 * every expectation was written from the GS field tables before the code ran.
 *
 * What is under test here is not the encodings -- `gs_pack_giftag`, `gs_pack_xyz2` and the rest
 * have their own hand literals in test_ps2_gs_pack.c and test_ps2_gs_rec.c -- but the four things
 * only the raster stage can get wrong:
 *
 *   1. which PRIM word a triangle asks for, i.e. op 3 with IIP/TME/ABE following the GL state and
 *      the texture context always 1 (every literal below is written as op + flags by hand);
 *   2. that a device-space vertex is passed through and never re-derived, because the y direction,
 *      the viewport map and the window's origin are already in the numbers the span loops walked;
 *   3. that a repeated state does not close the group -- the law that keeps a ribbon's 1,828
 *      triangles in one packet instead of 1,828 packets of three vertices;
 *   4. that a state change does, because the GIF tag carries the PRIM and one tag cannot describe
 *      two primitive states.
 *
 * The buffer is 64 quadwords (the test's own #define, as in increment 4's gate), which is what
 * makes the last check also prove that a group cut by capacity never lands mid-triangle.
 *
 * The quadwords are read through gs_group_view.h, the same view test_ps2_gs_rec.c uses: a GIF
 * quadword is 128 bits and every register this stage writes is 64 of them, so a captured group is
 * pairs of uint64_t and `qwc` counts pairs, not words.  One statement of that unit rather than two
 * is deliberate -- see that header's comment on how the half-width layout got through both gates.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define GS_REC_QW_CAP 64u

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_rec.c"
#include "../../src/gl/backend_eegs.c"
#include "gs_group_view.h"

#define CAP_MAX 8
static uint64_t s_cap[CAP_MAX][GS_REC_QW_CAP * 2u];
static uint32_t s_cap_n[CAP_MAX];
static int      s_cap_i;

static void capture(void *user, const uint64_t *qw, uint32_t qwc)
{
    (void)user;
    if (s_cap_i >= CAP_MAX || qwc > GS_REC_QW_CAP) return;
    memcpy(s_cap[s_cap_i], qw, GS_QW_BYTES(qwc));
    s_cap_n[s_cap_i] = qwc;
    s_cap_i++;
}

static void start(void)
{
    memset(s_cap, 0, sizeof(s_cap));
    memset(s_cap_n, 0, sizeof(s_cap_n));
    s_cap_i = 0;
    eegs_frame_begin(capture, NULL);
}

static const eegs_tri_state_t k_stb  = { 1, 1, 1 };   /* smooth, textured, blended */
static const eegs_tri_state_t k_flat_tex = { 0, 1, 1 };
static const eegs_tri_state_t k_smooth_bare = { 1, 0, 0 };
static const eegs_tri_state_t k_blend_only = { 0, 0, 1 };

/* The PRIM words, by hand: gs_pack_prim's own field list is OP[2:0] IIP[3] TME[4] FGE[5] ABE[6]
 * AA1[7] FST[8] CTXT[9] FIX[11:10], so with CTXT always 1 (512) a textured, blended, smooth TRI is
 * 3 + 8 + 16 + 64 + 512 = 603 = 0x25B; flat is 0x25B without IIP's 8 = 0x253; untextured and
 * unblended but smooth is 3 + 8 + 512 = 0x20B; a blended LINE with nothing else is
 * 1 + 64 + 512 = 0x241; and a bare POINT is 0 + 512 = 0x200. */
static const uint64_t k_prim_tri_stb  = 0x25BULL;
static const uint64_t k_prim_tri_flat = 0x253ULL;
static const uint64_t k_prim_line     = 0x241ULL;
static const uint64_t k_prim_point    = 0x200ULL;

/* The tag around a group whose PRIM is 0x25B.  Derived here rather than in code, and derived from
 * the field table: NLOOP[14:0], EOP[15] = 0x8000, PRE[46] = 4 in digit 11, PRIM[57:47] -- 0x25B is
 * 0b1001011011 so its bits 0,1,3,4,6,9 land on 47,48,50,51,53,56, giving digit 11 an 8 (C with PRE)
 * and digit 12 a 1+4+8 = D -- and NREG[63:60] = 4. */
static const uint64_t k_tag_tri_stb = 0x412DC00000008000ULL;

static int s_checks, s_failures;

static void check(const char *what, int ok)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        printf("  [FAIL] %s\n", what);
    }
}

static void check_word(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got != want) {
        s_failures++;
        printf("  [FAIL] %s got=0x%016llx want=0x%016llx\n", what,
               (unsigned long long)got, (unsigned long long)want);
    }
}

static eegs_vertex_t vert(float x, float y, float s, float t, uint32_t rgba)
{
    eegs_vertex_t v;

    v.x_px = x;
    v.y_px = y;
    v.s = s;
    v.t = t;
    v.rgba = rgba;
    return v;
}

/* One triangle, its thirteen quadwords spelled out: the tag, then for each vertex the PRIM, the
 * RGBAQ, the ST and the XYZ2, each register owning a whole quadword. */
static void test_one_triangle(void)
{
    uint64_t *w;
    uint32_t k;
    int badpad;
    eegs_vertex_t v[3];

    start();
    v[0] = vert(0.0f, 0.0f, 0.0f, 0.0f, 0xFF332244u);
    v[1] = vert(767.0f, 499.0f, 1.0f, 2.0f, 0x00000000u);
    v[2] = vert(20.09375f, 250.0f, 0.5f, 0.5f, 0xFFFFFFFFu);
    eegs_triangle(v, k_stb);
    eegs_frame_end();

    check_word("a smooth textured blended TRI asks for PRIM 0x25B", gs_vq(s_cap[0], 1u, 0u),
               k_prim_tri_stb);
    check("one triangle is one group: a tag plus twelve data quadwords",
          s_cap_i == 1 && s_cap_n[0] == gs_group_qw(3u) && s_cap_n[0] == 13u);
    check("three vertices, one triangle", eegs_verts() == 3u && eegs_prims() == 1u);
    if (s_cap_i < 1) return;
    w = s_cap[0];

    check_word("the group's tag: NLOOP 3 with the triangle's own PRIM",
               gs_qw(w, 0u), gs_pack_giftag(3u, 1u, 1u, k_prim_tri_stb, GS_FLG_PACKED, 4u));
    check("and that tag is the hand literal, not only the encoder's word",
          gs_qw(w, 0u) == (k_tag_tri_stb | 3u));
    check_word("REGS: PRIM, RGBAQ, ST, XYZ2, in the tag's upper half",
               gs_regs_of(w), 0x5210ULL);

    /* rgba 0xFF332244 is a=0xFF b=0x33 g=0x22 r=0x44, so the byte order the GS wants --
     * R[7:0] G[15:8] B[23:16] A[31:24] -- is 0xFF332244 unchanged, with Q = 1.0f above it. */
    check_word("vertex 1 RGBAQ unpacks the packed colour low byte first",
               gs_vq(w, 1u, 1u), 0x3F800000FF332244ULL);
    check_word("vertex 1 ST, 0,0", gs_vq(w, 1u, 2u), 0x0000000000000000ULL);
    check_word("vertex 1 XYZ2: the window's own top-left, in 12.4",
               gs_vq(w, 1u, 3u), 0x0000000080008000ULL);

    /* The pass-through law, at the far corner of the measured 768x500 client: 767 px is 12,272
     * sixteenths and 12,272 + 32,768 = 45,040 = 0xAFF0; 499 px is 7,984 + 32,768 = 40,752 =
     * 0x9F30.  XYZ2 is X[15:0] then Y[31:16]. */
    check_word("vertex 2 XYZ2: 767,499 passed through, not re-projected",
               gs_vq(w, 2u, 3u), 0x000000009F30AFF0ULL);
    check_word("vertex 2 RGBAQ: all four bytes zero is a=0, not a=255",
               gs_vq(w, 2u, 1u), 0x3F80000000000000ULL);
    check_word("vertex 2 ST: s = 1.0f low, t = 2.0f high",
               gs_vq(w, 2u, 2u), 0x400000003F800000ULL);
    /* 20.09375 px is exactly 321.5 sixteenths, and the cast truncates -- the GS's own ftoi4 -- so
     * 321 + 32,768 = 33,089 = 0x8141; 250 px is 4,000 + 32,768 = 36,768 = 0x8FA0. */
    check_word("vertex 3 XYZ2: 321.5 sixteenths truncates to 321, it does not round",
               gs_vq(w, 3u, 3u), 0x000000008FA08141ULL);
    check_word("vertex 3 RGBAQ: opaque white", gs_vq(w, 3u, 1u), 0x3F800000FFFFFFFFULL);

    /* The unit, one more time from this stage's side: if the raster stage or the recorder wrote a
     * register per 64-bit word, twelve of these thirteen quadwords would carry a second value. */
    badpad = 0;
    for (k = 1u; k < s_cap_n[0]; k++) {
        if (gs_upper_of(w, k) != 0u) badpad++;
    }
    check("and all twelve data quadwords are one register wide", badpad == 0);
}

/* The law with the most at stake: three triangles that want the same state are ONE group of nine
 * vertices, not three packets.  A group per triangle would be a DMA start per triangle, which is
 * the cost this port has been paying in the canvas all along and is not what "offload" means. */
static void test_state_reuse(void)
{
    eegs_vertex_t v[3];
    int i;

    start();
    for (i = 0; i < 3; i++) {
        v[0] = vert(0.0f, (float)i, 0.0f, 0.0f, 0xFFu);
        v[1] = vert(10.0f, (float)i, 0.0f, 0.0f, 0xFFu);
        v[2] = vert(0.0f, 10.0f + (float)i, 0.0f, 0.0f, 0xFFu);
        eegs_triangle(v, k_stb);
    }
    eegs_frame_end();

    check("three same-state triangles are one send", s_cap_i == 1 && gs_rec_sends() == 1u);
    check("nine vertices, three triangles", eegs_verts() == 9u && eegs_prims() == 3u);
    if (s_cap_i < 1) return;
    check("NLOOP 9, so the group holds all three whole",
          (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 9u);
    check("1 + 4*9 = 37 quadwords", s_cap_n[0] == gs_group_qw(9u) && s_cap_n[0] == 37u);
}

/* A state change is a group boundary by construction, and the PRIM word that follows it must be
 * the new one: the tag carries the state for the group's first primitive, and every vertex carries
 * it again. */
static void test_state_change_closes(void)
{
    eegs_vertex_t v[3];

    start();
    v[0] = vert(0.0f, 0.0f, 0.0f, 0.0f, 0xFFu);
    v[1] = vert(1.0f, 0.0f, 0.0f, 0.0f, 0xFFu);
    v[2] = vert(0.0f, 1.0f, 0.0f, 0.0f, 0xFFu);
    eegs_triangle(v, k_stb);
    eegs_triangle(v, k_flat_tex);
    eegs_frame_end();

    check("a flat triangle after a smooth one is two groups", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("the first group's PRIM keeps IIP", gs_vq(s_cap[0], 1u, 0u), k_prim_tri_stb);
    check_word("the second's drops it: 0x25B without the 8", gs_vq(s_cap[1], 1u, 0u),
               k_prim_tri_flat);
    check_word("and the second tag carries the new state",
               gs_qw(s_cap[1], 0u),
               gs_pack_giftag(3u, 1u, 1u, k_prim_tri_flat, GS_FLG_PACKED, 4u));
    check("so a state change costs one group, not one vertex group per triangle",
          s_cap_n[0] == gs_group_qw(3u) && s_cap_n[1] == gs_group_qw(3u));

    start();
    eegs_triangle(v, k_stb);
    eegs_point(&v[0], k_stb);
    eegs_frame_end();
    check("a point after a triangle closes too, and is its own op", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("POINT with the same flags: 0 + 8 + 16 + 64 + 512",
               gs_vq(s_cap[1], 1u, 0u), k_prim_point | 8u | 16u | 64u);
    check("the point group has one vertex", (gs_qw(s_cap[1], 0u) & 0x7FFFu) == 1u);
}

/* The XMB's message-box border is a GL_LINE_LOOP, so the line path is on screen from the first
 * frame, not deferred: two endpoints per call, and a group that is a whole number of segments. */
static void test_lines(void)
{
    eegs_vertex_t v[2];
    int i;

    start();
    for (i = 0; i < 4; i++) {
        v[0] = vert((float)i, 0.0f, 0.0f, 0.0f, 0xFF332244u);
        v[1] = vert((float)i, 10.0f, 0.0f, 0.0f, 0xFF332244u);
        eegs_line(v, k_smooth_bare);
    }
    eegs_frame_end();

    check("four segments of the same state are one group", s_cap_i == 1);
    if (s_cap_i < 1) return;
    check("eight vertices = four segments",
          (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 8u && eegs_verts() == 8u);
    check_word("LINE, untextured, unblended, smooth: 1 + 8 + 512",
               gs_vq(s_cap[0], 1u, 0u), 0x209ULL);
    check("and a counted primitive is counted whether or not it is a triangle",
          eegs_prims() == 4u);
    check("one tag plus eight four-register vertices = 33 quadwords",
          s_cap_n[0] == gs_group_qw(8u) && s_cap_n[0] == 33u);

    start();
    v[0] = vert(0.0f, 0.0f, 0.0f, 0.0f, 0xFF332244u);
    v[1] = vert(0.0f, 10.0f, 0.0f, 0.0f, 0xFF332244u);
    eegs_line(v, k_blend_only);
    eegs_frame_end();
    if (s_cap_i < 1) return;
    check_word("the same op with only ABE set: 1 + 64 + 512", gs_vq(s_cap[0], 1u, 0u),
               k_prim_line);
    check("and a segment is a tag plus two four-quadword vertices",
          eegs_verts() == 2u && s_cap_n[0] == gs_group_qw(2u) && s_cap_n[0] == 9u);
}

/* Capacity at this layer's granularity: a group cut must not land between the first and second
 * vertex of a triangle, or the GS draws a two-vertex primitive and steals the third from the next
 * group.  At 64 quadwords the recorder's list rule cuts at 12 vertices, which is a whole number of
 * triangles; 12 triangles is 36 vertices, so three groups of 12 and one send per group. */
static void test_cut_is_always_whole_triangles(void)
{
    eegs_vertex_t v[3];
    int i;

    start();
    v[0] = vert(1.0f, 1.0f, 0.0f, 0.0f, 0xFFu);
    v[1] = vert(2.0f, 1.0f, 0.0f, 0.0f, 0xFFu);
    v[2] = vert(1.0f, 2.0f, 0.0f, 0.0f, 0xFFu);
    for (i = 0; i < 12; i++) eegs_triangle(v, k_stb);
    eegs_frame_end();

    check("36 vertices at this capacity is three sends", s_cap_i == 3 && gs_rec_sends() == 3u);
    check("nothing was refused", gs_rec_unsplittable() == 0u);
    for (i = 0; i < s_cap_i; i++) {
        char label[64];
        unsigned long n = (unsigned long)(gs_qw(s_cap[i], 0u) & 0x7FFFu);
        sprintf(label, "group %d's NLOOP %lu is a whole number of triangles", i, n);
        check(label, n % 3u == 0u && n > 0u);
        sprintf(label, "and group %d is that many x 4 plus a tag", i);
        check(label, s_cap_n[i] == gs_group_qw((uint32_t)n));
    }
    check("12 + 12 + 12 = 36", eegs_verts() == 36u);
    check("49 quadwords per group at this capacity", s_cap_n[0] == 49u);
}

/* The frame boundary: a group left open by an unfinished primitive must not be carried into the
 * next frame, and the counters a bench reads are per-frame. */
static void test_frame_boundary(void)
{
    eegs_vertex_t v[3];

    start();
    v[0] = vert(0.0f, 0.0f, 0.0f, 0.0f, 0xFFu);
    v[1] = vert(1.0f, 0.0f, 0.0f, 0.0f, 0xFFu);
    v[2] = vert(0.0f, 1.0f, 0.0f, 0.0f, 0xFFu);
    eegs_triangle(v, k_stb);
    check("nothing is sent before the frame ends", s_cap_i == 0);
    eegs_frame_end();
    check("and it is sent when the frame ends", s_cap_i == 1);

    start();
    check("a new frame starts with no counters and no open group",
          eegs_prims() == 0u && eegs_verts() == 0u && gs_rec_sends() == 0u);
    eegs_triangle(v, k_stb);
    /* The state is the same word as the last frame used, and yet it must be re-sent: the recorder
     * has no group, so a group has to be opened with a tag that carries it. */
    eegs_frame_end();
    check("the same state after a reset still opens a group", s_cap_i == 1);
    if (s_cap_i < 1) return;
    check_word("with the state it asked for", gs_vq(s_cap[0], 1u, 0u), k_prim_tri_stb);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 EE/GS raster stage vs hand-built GIF groups (cap=%u qw)\n", GS_REC_QW_CAP);
    printf("==========================================================\n");

    test_one_triangle();
    test_state_reuse();
    test_state_change_closes();
    test_lines();
    test_cut_is_always_whole_triangles();
    test_frame_boundary();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the raster stage asks for the primitives it was told to\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a recorded triangle is not what the GS would draw\n");
    return 1;
}
