/*
 * verify/tests/test_eegs_raster.c -- what the raster stage decides, against words written by hand
 *
 * Row 5, step 5a of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The discipline is
 * increments 2, 3 and 4's: `backend_eegs.c` and the recorder are compiled into this program, and
 * every expectation was written from the GS field tables before the code ran.
 *
 * What is under test here is not the encodings -- `gs_pack_giftag`, `gs_pack_xyz2` and the rest
 * have their own hand literals in test_ps2_gs_pack.c and test_ps2_gs_rec.c -- but the six things
 * only the raster stage can get wrong:
 *
 *   1. which PRIM word a triangle asks for, i.e. op 3 with IIP/TME/ABE following the GL state and
 *      the texture context always 1 (every literal below is written as op + flags by hand);
 *   2. that a device-space vertex is passed through and never re-derived, because the y direction,
 *      the viewport map and the window's origin are already in the numbers the span loops walked;
 *   3. that a repeated state does not close the group -- the law that keeps a ribbon's 1,828
 *      triangles in one packet instead of 1,828 packets of three vertices;
 *   4. that a state change does, because every vertex of the open group carries the group's one
 *      latched PRIM word -- a second state needs a second group's second word.  The GIF tag itself
 *      names no primitive at all (see the dense tag below), so this boundary is the recorder's
 *      contract rather than the GIF's requirement;
 *   5. from 5b, the texture switch: which group a plane change becomes, what order its three
 *      registers travel in, that a repeated plane costs nothing, and that a textured primitive
 *      with no plane of its own loses `TME` rather than sampling the page the stream left behind;
 *   6. from LANE 12e, the PARK: a triangle is THREE vertices in the group its state already
 *      carries, always -- `verts` is 3 x `prims` and no group this stage opens carries the strip's
 *      OP.  That is the measured answer, not the design: continuing the stream saved 14% of the
 *      vertices and cost 9x the group boundaries, which filled the 256-entry stage table and dropped
 *      236 primitives of a real frame.  doc/txt/PS2.txt LANE 12e has the numbers, and these two
 *      checks are what a re-lived rung has to move before it can ship.
 *   7. from LANE 12f, the BATCH: `eegs_grid_strip()` records a retained mesh as one PRIM and a run
 *      of vertices, and the gate is that this DECODES -- through the recorder's own group layout --
 *      into the same triangle sequence the list path records for the same mesh, the zigzag's
 *      turn-around included, plus one group spelled out word by word from the field tables.  See the
 *      decoder's comment for what is compared and for the one thing that is not.
 *   8. from LANE 12h rung (i), the TEXTURED BATCH: `eegs_tex_quads()` records a retained icon or
 *      text mesh as the TRI list `vertex.c`'s own GL_QUADS split makes, and the same gate applies --
 *      it DECODES into the triangles the walk's list path records, corner payloads including the ST
 *      register, which is the one thing 12f's grey strip could never exercise because it writes 0
 *      there.  The pairing of the map's plane key through `gs_tex_find()` is this entry's own
 *      decision: one switch for a whole run, and a key the registry does not know demotes to the
 *      flat draw the walk folds to, counted in `texmiss` rather than drawn from somebody else's page.
 *
 * The buffer is 32 quadwords (the test's own #define, matching increment 4's gate), which is what
 * makes the last check also prove that a group cut by capacity never lands mid-triangle.  32 is the
 * dense successor to this file's old 64: the recorder's headroom and its stride halved together, so
 * a group still holds 12 vertices before it splits and every vertex-level expectation below is
 * unchanged -- only the quadwords per group and the tag words are.
 *
 * The quadwords are read through gs_group_view.h, the same view test_ps2_gs_rec.c uses: a GIF
 * quadword is 128 bits and every register this stage writes is 64 of them, so a captured group is
 * pairs of uint64_t and `qwc` counts pairs, not words -- and because a vertex group is a dense
 * `FLG=REGLIST`, each of those pairs carries TWO registers of the group's REGS list, which is why
 * the register reads below go through `gs_vq()` and not through a word index.  One statement of that
 * unit rather than two is deliberate -- see that header's comment on how the half-width layout got
 * through both gates.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define GS_REC_QW_CAP 32u

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_rec.c"
#include "../../src/gl/backend_eegs.c"
#include "gs_group_view.h"

/* LANE 12f's batch entry submits a whole mesh in one call, so one frame of it is many groups: a
 * 5x4 grid at this capacity is three vertex groups, and the list path that must decode to the same
 * thirty triangles is eight.  40 is above either with room for the AD group a textured case puts
 * first.
 *
 * A dropped capture used to be invisible: `capture()` returned without recording, and a comparison
 * of two decoded streams would then have been comparing whatever fitted.  `s_cap_drops` makes that
 * an assertion instead of an accident, and every check that decodes reads it first. */
#define CAP_MAX 40
static uint64_t s_cap[CAP_MAX][GS_REC_QW_CAP * 2u];
static uint32_t s_cap_n[CAP_MAX];
static int      s_cap_i;
static int      s_cap_drops;

static void capture(void *user, const uint64_t *qw, uint32_t qwc)
{
    (void)user;
    if (s_cap_i >= CAP_MAX || qwc > GS_REC_QW_CAP) {
        s_cap_drops++;
        return;
    }
    memcpy(s_cap[s_cap_i], qw, GS_QW_BYTES(qwc));
    s_cap_n[s_cap_i] = qwc;
    s_cap_i++;
}

/* 5b's rule is that a textured primitive with no plane of its own gets `TME` cleared, so a case
 * that keeps its textured hand literals has to name a plane first -- and naming one puts a switch
 * group at the front of the stream, because an AD group crosses the bus the moment it is written
 * while the vertices after it accumulate.  s_vg is where this frame's *vertex* groups start: 1
 * after start_plane(), 0 after start(), which is the whole difference between the two. */
static gs_tex_t s_plane;
static int s_vg;

#define VG(k)  (s_cap[s_vg + (k)])
#define VN(k)  (s_cap_n[s_vg + (k)])

static void start(void)
{
    memset(s_cap, 0, sizeof(s_cap));
    memset(s_cap_n, 0, sizeof(s_cap_n));
    s_cap_i = 0;
    s_cap_drops = 0;
    s_vg = 0;
    eegs_frame_begin(capture, NULL);
}

static void start_plane(void)
{
    start();
    eegs_texture(&s_plane);
    s_vg = 1;
}

static const eegs_tri_state_t k_stb  = { 1, 1, 1 };   /* smooth, textured, blended */
static const eegs_tri_state_t k_flat_tex = { 0, 1, 1 };
static const eegs_tri_state_t k_smooth_bare = { 1, 0, 0 };
static const eegs_tri_state_t k_blend_only = { 0, 0, 1 };

/* What the XMB ribbon's triangles look like to this stage: Gouraud because GL_SMOOTH is the default
 * and `xmb.c` never calls glShadeModel, blended because the ribbon sets `glBlendFunc(GL_DST_COLOR,
 * GL_ONE)`, untextured because it calls `glDisable(GL_TEXTURE_2D)` first.  The batch's own state
 * comes from the map's two flags rather than from a struct, and the two have to agree -- which is
 * what the strip-vs-list comparison below is for. */
static const eegs_tri_state_t k_grid_st = { 1, 0, 1 };

/* The register set every PRIM word in this gate carries, named ONCE because it is the bit LANE 19
 * rung 6 found wrong on the machine.  gs_pack_prim's field list is OP[2:0] IIP[3] TME[4] FGE[5]
 * ABE[6] AA1[7] FST[8] CTXT[9] FIX[11:10], and CTXT selects the register set the primitive draws
 * with -- ps2sdk pairs the FIRST set with the integer 0 (`GS_REG_FRAME_1` is 4Ch, `GS_REG_FRAME_2`
 * 4Dh, and its own call is `GS_REG_FRAME + context`), which is the set `ps2_gs_env.c` and
 * `eegs_texture()` program and the set `EEGS_CONTEXT` in the backend is now 0 to agree with.  A 1
 * here asks the GS for a set nothing has ever written: the frame is delivered, acknowledged and
 * clipped away, and every counter on this side of the GS says it was drawn. */
#define K_CTX 0u

/* The PRIM words, by hand, as sums of that field table -- CTXT included as a term so the bit cannot
 * be re-frozen at a stale value by an edit that touches the neighbours: a textured, blended, smooth
 * TRI is 3 + 8 + 16 + 64 = 91 = 0x5B; flat is 0x5B without IIP's 8 = 0x53; untextured and unblended
 * but smooth is 3 + 8 = 0x0B; a blended LINE with nothing else is 1 + 64 = 0x41; and a bare POINT is
 * nothing but the op and the context, so 0. */
static const uint64_t k_prim_tri_stb  = 3u + 8u + 16u + 64u + K_CTX;
static const uint64_t k_prim_tri_flat = 3u + 16u + 64u + K_CTX;
static const uint64_t k_prim_line     = 1u + 64u + K_CTX;
static const uint64_t k_prim_line_smooth = 1u + 8u + K_CTX;   /* LINE, untextured, unblended */
static const uint64_t k_prim_point    = 0u + K_CTX;

/* The tag around EVERY vertex group this stage writes, derived here from the field table rather than
 * trusted from the encoder: NLOOP[14:0], EOP[15] = 0x8000, PRE[46] = 0, PRIM[57:47] = 0,
 * FLG[59:58] = 1 -- a 1 in the low bit of digit 14, i.e. 0x0400_0000_0000_0000 -- and NREG[63:60] = 4
 * at 0x4000_0000_0000_0000.  It is ONE word for a TRI, a strip, a line and a point, because under
 * `FLG=REGLIST` the primitive travels as the loop's own first register and the tag carries none. */
static const uint64_t k_tag_dense = 0x4400000000008000ULL;

/* The shape this replaced, kept as the control it is: the same NREG 4 and EOP, with FLG 0 (PACKED),
 * PRE set at 46 and the textured smooth blended TRI's PRIM word in [57:47] -- BUILT from
 * `k_prim_tri_stb` rather than frozen as a second copy of the encoding, so this control follows the
 * CTXT law instead of quietly keeping an old one.  0x5B has its bits 0,1,3,4,6, which land on
 * 47,48,50,51,53: digit 11 is C (PRE's 4 plus op's bit 47), digit 12 is D (1+4+8), digit 13 is 2,
 * and digit 14 is 0 -- nothing of the primitive reaches bit 56 any more, because that bit WAS CTXT.
 * The word is therefore 0x402D_C000_0000_8000 and its XOR with the dense tag is
 * 0x042D_C000_0000_0000: bit 58 plus bit 46 plus those five PRIM bits and nothing else.  LANE 19 rung
 * 5 put these four registers in front of this tag and the census counted zero pixels, at either value
 * of PRE; rung 6 found the sixth bit, CTXT's own, was counting for nothing. */
static const uint64_t k_tag_retired_tri_stb =
    (4ULL << 60) | (k_prim_tri_stb << 47) | (1ULL << 46) | 0x8000ULL;

/* The AD group's tag, by hand: NLOOP[14:0]=3, EOP[15], ID[57:48]=0, PRE[46]=0, PRIM[57:47]=0 --
 * this group draws nothing -- and NREG[63:60]=1.  The same shape send_trx_setup_at() sends for the
 * canvas's four registers, which is the proof the shape has on the machine. */
static const uint64_t k_tag_ad3 = 0x1000000000008003ULL;

/* A smooth, blended TRI with TME cleared: 3 + 8 + 64 = 75 = 0x4B. */
static const uint64_t k_prim_tri_untex = 3u + 8u + 64u + K_CTX;

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

/* The pixel pair is integer because the stage that produces it is integer: `ZBufferPoint`'s x and y
 * are GLint, which is what the viewport map writes and what clip.c's gs_vertex() passes straight
 * through after LANE 12a.  A float here would be a coordinate this port cannot deliver.  s and t
 * stay floats -- the ST field is IEEE-754, that part is the hardware's own unit. */
static eegs_vertex_t vert(int32_t x_px, int32_t y_px, float s, float t, uint32_t rgba)
{
    eegs_vertex_t v;

    v.x_px = x_px;
    v.y_px = y_px;
    v.s = s;
    v.t = t;
    v.rgba = rgba;
    return v;
}

/* A texture plane whose three words are sentinels.  This stage's job is to deliver the registry's
 * words in a group of their own, not to derive them, so they are handed over as they came: values
 * computed by gs_pack_tex0() here would be compared against a copy of themselves, proving only
 * that a struct field survives a function call.  The derivation is test_ps2_gs_tex.c's gate. */
static gs_tex_t plane_words(uint64_t clamp, uint64_t tex0, uint64_t tex1)
{
    gs_tex_t t;

    memset(&t, 0, sizeof t);
    t.clamp = clamp;
    t.tex0  = tex0;
    t.tex1  = tex1;
    return t;
}

/* LANE 12h rung (i): `eegs_tex_quads()` pairs the map's `pixmap` key with a VRAM plane through the
 * GS registry, `gs_tex_find()` -- and the registry cannot join this TU.  `ps2_gs_tex.c` owns a
 * file-static `s_n` and `s_sink`, and so does `ps2_gs_rec.c`, which the capture above needs, so the
 * two have no legal spelling side by side.  It should not: what THIS stage decides is the pairing
 * and nothing else, so the lookup is a test-local identity keyed on the sheet the cases name,
 * handing back the sentinel plane's entry for its one key and 0 for every other -- the two answers
 * that are the entry's whole subject: one switch for a run, and a demotion for a key the registry
 * does not know.  The three words stay hand literals for the same reason they already are: their
 * derivation is `test_ps2_gs_tex.c`'s gate. */
static char s_sheet_key;                    /* stands for `im->pixmap` of the uploaded sheet */
static int  s_never_uploaded;               /* a bound texture with no plane: gs_tex_find() says 0 */
static const void *s_tex_key = &s_sheet_key;

const gs_tex_t *gs_tex_find(const void *plane)
{
    return plane == s_tex_key ? &s_plane : (const gs_tex_t *)0;
}

/* One triangle, its seven quadwords spelled out: the tag, then two quadwords per vertex carrying
 * the four registers of the REGS list two to a quadword -- PRIM with RGBAQ, then ST with the XYZ2
 * that kicks the draw. */
static void test_one_triangle(void)
{
    uint64_t *w;
    uint32_t k;
    int badpad;
    eegs_vertex_t v[3];

    start_plane();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFF332244u);
    v[1] = vert(767, 499, 1.0f, 2.0f, 0x00000000u);
    v[2] = vert(20, 250, 0.5f, 0.5f, 0xFFFFFFFFu);
    eegs_triangle(v, k_stb);
    eegs_frame_end();

    check_word("a smooth textured blended TRI asks for PRIM 0x5B", gs_vq(VG(0), 1u, 0u),
               k_prim_tri_stb);
    check("one triangle is one vertex group: a tag plus six data quadwords",
          s_cap_i == 1 + s_vg && VN(0) == gs_group_qw(3u) && VN(0) == 7u);
    check("three vertices, one triangle", eegs_verts() == 3u && eegs_prims() == 1u);
    if (s_cap_i < 1 + s_vg) return;
    w = VG(0);

    check_word("the group's tag: NLOOP 3, FLG=REGLIST, and no PRIM field",
               gs_qw(w, 0u), gs_pack_giftag(3u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u));
    check("and that tag is the hand literal, not only the encoder's word",
          gs_qw(w, 0u) == (k_tag_dense | 3u));
    check_word("CONTROL that word against the retired PACKED tag: FLG, PRIM and PRE, nothing else",
               gs_qw(w, 0u) ^ (k_tag_retired_tri_stb | 3u), 0x042DC00000000000ULL);
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
    /* 250 px is 4,000 sixteenths + 32,768 = 36,768 = 0x8FA0, and 20 px is 320 + 32,768 = 0x8140.
     * This case used to be 20.09375 px, whose 321.5 sixteenths the recorder truncated to 321, and it
     * is retired rather than marked passing: under LANE 12a no caller can hand this layer a
     * fraction, because the viewport map writes a GLint.  The truncation itself is still gated --
     * it is ps2_sf_fixsfsi(), which test_ps2_softfloat.c sweeps -- but nothing on the host now
     * checks that a clipped vertex's whole-pixel value is the one the map produced.  That is a real
     * gap opened by this change, and it is named here rather than papered over. */
    check_word("vertex 3 XYZ2: whole pixels, scaled and folded onto the page",
               gs_vq(w, 3u, 3u), 0x000000008FA08140ULL);
    check_word("vertex 3 RGBAQ: opaque white", gs_vq(w, 3u, 1u), 0x3F800000FFFFFFFFULL);

    /* The unit, one more time from this stage's side, and now reading in the direction the format
     * runs: of this group's six data quadwords every HIGH half is a register of the loop -- RGBAQ,
     * XYZ2, RGBAQ, XYZ2, RGBAQ, XYZ2 -- and none of them is padding.  Under the retired PACKED stride
     * all twelve of those halves were zero, which is the shape rung 5 measured as inert.  The one
     * legitimately zero register in this group, vertex 1's ST of (0,0), sits in a LOW half, where a
     * "no half is zero" census would have called it padding. */
    badpad = 0;
    for (k = 1u; k < VN(0); k++) {
        if (gs_upper_of(w, k) == 0u) badpad++;
    }
    check("and all six data quadwords carry two registers each, none padded", badpad == 0);
    check_word("vertex 1's zero ST is quadword 2's low half, not an empty half",
               gs_half(w, 2u, 0u), 0u);
}

/* The law with the most at stake: three triangles that want the same state are ONE group of nine
 * vertices, not three packets.  A group per triangle would be a DMA start per triangle, which is
 * the cost this port has been paying in the canvas all along and is not what "offload" means. */
static void test_state_reuse(void)
{
    eegs_vertex_t v[3];
    int i;

    start_plane();
    for (i = 0; i < 3; i++) {
        v[0] = vert(0, i, 0.0f, 0.0f, 0xFFu);
        v[1] = vert(10, i, 0.0f, 0.0f, 0xFFu);
        v[2] = vert(0, 10 + i, 0.0f, 0.0f, 0xFFu);
        eegs_triangle(v, k_stb);
    }
    eegs_frame_end();

    check("three same-state triangles are one send after the switch",
          s_cap_i == 1 + s_vg && gs_rec_sends() == 1u + s_vg);
    check("nine vertices, three triangles", eegs_verts() == 9u && eegs_prims() == 3u);
    if (s_cap_i < 1 + s_vg) return;
    check("NLOOP 9, so the group holds all three whole",
          (gs_qw(VG(0), 0u) & 0x7FFFu) == 9u);
    check("1 + 2*9 = 19 quadwords", VN(0) == gs_group_qw(9u) && VN(0) == 19u);

    /* LANE 12e's park, read off the group this frame built: a triangle is three vertices, so
     * `verts` is exactly 3 x `prims`, and the OP the group's PRIM carries is the list's 3, not the
     * strip's 4.  These are the two things a re-lived continuation has to move before it can ship,
     * and the second is why the strip primitive itself stays gated in test_ps2_gs_rec.c -- the
     * recorder implements it; this stage no longer asks for it. */
    check("the park holds: verts is 3 x prims, so no vertex was ever continued",
          eegs_verts() == 3u * eegs_prims());
    check_word("and this stage's group carries OP 3, the triangle list",
               gs_vq(VG(0), 1u, 0u) & 7u, 3u);
}

/* A state change is a group boundary by construction, and the PRIM word that follows it must be
 * the new one: every vertex carries the group's latched state in its own first register, and the tag
 * carries none -- so the two groups' tags here are the same word, and what makes them different
 * packets is the register at the head of each of their loops. */
static void test_state_change_closes(void)
{
    eegs_vertex_t v[3];

    start_plane();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFFu);
    v[1] = vert(1, 0, 0.0f, 0.0f, 0xFFu);
    v[2] = vert(0, 1, 0.0f, 0.0f, 0xFFu);
    eegs_triangle(v, k_stb);
    eegs_triangle(v, k_flat_tex);
    eegs_frame_end();

    check("a flat triangle after a smooth one is two vertex groups", s_cap_i == 2 + s_vg);
    if (s_cap_i < 2 + s_vg) return;
    check_word("the first group's PRIM keeps IIP", gs_vq(VG(0), 1u, 0u), k_prim_tri_stb);
    check_word("the second's drops it: 0x5B without the 8", gs_vq(VG(1), 1u, 0u),
               k_prim_tri_flat);
    check_word("and its tag is the identical dense word, NLOOP 3, state in its loops",
               gs_qw(VG(1), 0u), gs_pack_giftag(3u, 1u, 0u, 0u, GS_FLG_REGLIST, 4u));
    check("so the two groups' tags are the same 64 bits, and only their PRIM registers differ",
          gs_qw(VG(0), 0u) == gs_qw(VG(1), 0u) &&
          gs_vq(VG(0), 1u, 0u) != gs_vq(VG(1), 1u, 0u));
    check("so a state change costs one group, not one vertex group per triangle",
          VN(0) == gs_group_qw(3u) && VN(1) == gs_group_qw(3u));

    start_plane();
    eegs_triangle(v, k_stb);
    eegs_point(&v[0], k_stb);
    eegs_frame_end();
    check("a point after a triangle closes too, and is its own op", s_cap_i == 2 + s_vg);
    if (s_cap_i < 2 + s_vg) return;
    check_word("POINT with the same flags: 0 + 8 + 16 + 64",
               gs_vq(VG(1), 1u, 0u), k_prim_point | 8u | 16u | 64u);
    check("the point group has one vertex", (gs_qw(VG(1), 0u) & 0x7FFFu) == 1u);
}

/* The XMB's message-box border is a GL_LINE_LOOP, so the line path is on screen from the first
 * frame, not deferred: two endpoints per call, and a group that is a whole number of segments. */
static void test_lines(void)
{
    eegs_vertex_t v[2];
    int i;

    start();
    for (i = 0; i < 4; i++) {
        v[0] = vert(i, 0, 0.0f, 0.0f, 0xFF332244u);
        v[1] = vert(i, 10, 0.0f, 0.0f, 0xFF332244u);
        eegs_line(v, k_smooth_bare);
    }
    eegs_frame_end();

    check("four segments of the same state are one group", s_cap_i == 1);
    if (s_cap_i < 1) return;
    check("eight vertices = four segments",
          (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 8u && eegs_verts() == 8u);
    check_word("LINE, untextured, unblended, smooth: 1 + 8",
               gs_vq(s_cap[0], 1u, 0u), k_prim_line_smooth);
    check("and a counted primitive is counted whether or not it is a triangle",
          eegs_prims() == 4u);
    check("one tag plus eight two-quadword vertices = 17 quadwords",
          s_cap_n[0] == gs_group_qw(8u) && s_cap_n[0] == 17u);

    start();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFF332244u);
    v[1] = vert(0, 10, 0.0f, 0.0f, 0xFF332244u);
    eegs_line(v, k_blend_only);
    eegs_frame_end();
    if (s_cap_i < 1) return;
    check_word("the same op with only ABE set: 1 + 64", gs_vq(s_cap[0], 1u, 0u),
               k_prim_line);
    check("and a segment is a tag plus two two-quadword vertices",
          eegs_verts() == 2u && s_cap_n[0] == gs_group_qw(2u) && s_cap_n[0] == 5u);
}

/* Capacity at this layer's granularity: a group cut must not land between the first and second
 * vertex of a triangle, or the GS draws a two-vertex primitive and steals the third from the next
 * group.  At 32 quadwords the recorder's list rule cuts at 12 vertices, which is a whole number of
 * triangles; 12 triangles is 36 vertices, so three groups of 12 and one send per group. */
static void test_cut_is_always_whole_triangles(void)
{
    eegs_vertex_t v[3];
    int i;

    start_plane();
    /* Each triangle walks away from the last one so the NLOOP arithmetic below belongs to the LIST
     * primitive alone.  With LANE 12e parked that costs nothing -- every triangle is three vertices
     * whichever way its edges fall -- but a re-lived continuation would mix a one-vertex group into
     * a test whose whole subject is that a cut lands on a whole triangle. */
    for (i = 0; i < 12; i++) {
        v[0] = vert(1 + 4 * i, 1, 0.0f, 0.0f, 0xFFu);
        v[1] = vert(2 + 4 * i, 1, 0.0f, 0.0f, 0xFFu);
        v[2] = vert(1 + 4 * i, 2, 0.0f, 0.0f, 0xFFu);
        eegs_triangle(v, k_stb);
    }
    eegs_frame_end();

    check("36 vertices at this capacity is three sends",
          s_cap_i == 3 + s_vg && gs_rec_sends() == 3u + s_vg);
    check("nothing was refused", gs_rec_unsplittable() == 0u);
    for (i = s_vg; i < s_cap_i; i++) {
        char label[64];
        unsigned long n = (unsigned long)(gs_qw(s_cap[i], 0u) & 0x7FFFu);
        sprintf(label, "vertex group %d's NLOOP %lu is a whole number of triangles",
                i - s_vg, n);
        check(label, n % 3u == 0u && n > 0u);
        sprintf(label, "and vertex group %d is that many x 2 plus a tag", i - s_vg);
        check(label, s_cap_n[i] == gs_group_qw((uint32_t)n));
    }
    check("12 + 12 + 12 = 36", eegs_verts() == 36u);
    check("25 quadwords per group at this capacity", VN(0) == 25u);
}

/* The frame boundary: a group left open by an unfinished primitive must not be carried into the
 * next frame, the counters a bench reads are per-frame, and neither is the plane the frame samples. */
static void test_frame_boundary(void)
{
    const gs_tex_t a = plane_words(0x0000000102030405ULL, 0x060708090A0B0C0DULL,
                                  0x0E0F101112131415ULL);
    eegs_vertex_t v[3];

    start();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFFu);
    v[1] = vert(1, 0, 0.0f, 0.0f, 0xFFu);
    v[2] = vert(0, 1, 0.0f, 0.0f, 0xFFu);

    /* 5b's half of the boundary law, which is also ps2_gs_tex.h's second: the words that exist
     * cross the bus when they are written, while the vertices that use them accumulate. */
    eegs_texture(&a);
    check("the switch group leaves before the frame ends", s_cap_i == 1);
    eegs_triangle(v, k_stb);
    check("and no vertex group has", s_cap_i == 1);
    eegs_frame_end();
    check("the vertex group is sent when the frame ends", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("so the frame opens with the AD tag", gs_qw(s_cap[0], 0u), k_tag_ad3);
    check_word("and the triangle that follows is the textured one the plane makes legal",
               gs_vq(s_cap[1], 1u, 0u), k_prim_tri_stb);

    /* A new frame has no plane either: the environment packet that ends a frame writes `TEX0` for
     * the window's default, so the last frame's page is not live state to inherit.  Asking for a
     * texture before naming a plane therefore costs a cleared `TME` and a count, not a sample of
     * whatever the previous frame left there. */
    start();
    check("a new frame starts with no counters and no open group",
          eegs_prims() == 0u && eegs_verts() == 0u && gs_rec_sends() == 0u && s_cap_i == 0);
    check("its texture counters start empty too",
          eegs_tex_switches() == 0u && eegs_tex_missing() == 0u);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("a textured triangle with no plane of this frame's is recorded without TME",
          s_cap_i == 1 && eegs_tex_missing() == 1u);
    if (s_cap_i < 1) return;
    check_word("and the state it carries is the untextured one",
               gs_vq(s_cap[0], 1u, 0u), k_prim_tri_untex);

    /* The state the recorder lost with the group has to be re-sent too: a frame that names a plane
     * and then draws pays one switch group and one vertex group, even when the previous frame used
     * the identical PRIM word. */
    start();
    eegs_texture(&a);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("so the second frame pays its own switch group", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("and the group it opens with is the AD tag", gs_qw(s_cap[0], 0u), k_tag_ad3);
    check_word("with the state the re-named plane makes legal",
               gs_vq(s_cap[1], 1u, 0u), k_prim_tri_stb);
    check("and one switch counted, not the frame's whole triangle count",
          eegs_tex_switches() == 1u && eegs_tex_missing() == 0u);
}

/* 5b: the texture switch.
 *
 * A plane change is three AD writes in a group of its own, sent between the vertex groups: the
 * environment packet that could carry them is written once per frame and this port's frame binds
 * two sheets.  Their order is ps2_gs_env.c's trio -- CLAMP, TEX0, TEX1 -- so the same three
 * registers travel the same way in both places.
 */
static void test_texture_switch(void)
{
    const gs_tex_t a = plane_words(0x9999AAAABBBBCCCCULL, 0x1111222233334444ULL,
                                  0x5555666677778888ULL);
    eegs_vertex_t v[3];

    start();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFFFFFFFFu);
    v[1] = vert(64, 0, 64.0f, 0.0f, 0xFFFFFFFFu);
    v[2] = vert(0, 64, 0.0f, 64.0f, 0xFFFFFFFFu);

    eegs_texture(&a);
    eegs_triangle(v, k_stb);
    eegs_frame_end();

    check("the switch is a group before the vertices it applies to", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("its tag", gs_qw(s_cap[0], 0u), k_tag_ad3);
    check_word("its one REGS nibble is AD", gs_regs_of(s_cap[0]), 0x0Eu);
    check("so it is one tag plus three register quadwords",
          s_cap_n[0] == GS_REC_AD_QWC(3u) && s_cap_n[0] == 4u);
    check_word("CLAMP first", gs_qw(s_cap[0], 1u), a.clamp);
    check_word("  at GP offset 0x08", gs_upper_of(s_cap[0], 1u), 0x08u);
    check_word("TEX0 second", gs_qw(s_cap[0], 2u), a.tex0);
    check_word("  at GP offset 0x06", gs_upper_of(s_cap[0], 2u), 0x06u);
    check_word("TEX1 third", gs_qw(s_cap[0], 3u), a.tex1);
    check_word("  at GP offset 0x14", gs_upper_of(s_cap[0], 3u), 0x14u);
    check_word("and the group after it is the textured primitive",
               gs_vq(s_cap[1], 1u, 0u), k_prim_tri_stb);
    check("one plane change is one switch counted", eegs_tex_switches() == 1u);
    check("and no primitive lost a texture", eegs_tex_missing() == 0u);

    /* The law that keeps a ribbon in one packet: pointing at the plane the stream already names
     * costs no group and closes no vertex group.  The second triangle is elsewhere on purpose --
     * with LANE 12e parked two triangles sharing an edge would pack together anyway, and this check
     * is about the plane, not about the packing. */
    start();
    eegs_texture(&a);
    eegs_triangle(v, k_stb);
    eegs_texture(&a);
    v[0] = vert(128, 0, 128.0f, 0.0f, 0xFFFFFFFFu);
    v[1] = vert(192, 0, 192.0f, 0.0f, 0xFFFFFFFFu);
    v[2] = vert(128, 64, 128.0f, 64.0f, 0xFFFFFFFFu);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("a repeated plane pays nothing: one switch group and one vertex group",
          s_cap_i == 2);
    if (s_cap_i < 2) return;
    check("and the two triangles are one group of six vertices, thirteen quadwords",
          gs_nloop_of(s_cap[1]) == 6u && s_cap_n[1] == gs_group_qw(6u) && s_cap_n[1] == 13u);
    check("counted once", eegs_tex_switches() == 1u);

    /* Pointing at nothing sends nothing -- an untextured primitive reads no texture whatever TEX0
     * holds -- but it does clear the stream's plane, which is what stops the *next* textured
     * triangle from sampling the page the last switch left there. */
    start();
    eegs_texture(&a);
    eegs_triangle(v, k_stb);
    eegs_texture(NULL);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("no group for the plane of none, but the open group splits on the PRIM",
          s_cap_i == 3);
    if (s_cap_i < 3) return;
    check_word("the triangle before it kept TME", gs_vq(s_cap[1], 1u, 0u), k_prim_tri_stb);
    check_word("and the one after it lost it", gs_vq(s_cap[2], 1u, 0u), k_prim_tri_untex);
    check("still one switch emitted", eegs_tex_switches() == 1u);
    check("and the textured primitive with no plane is counted, not drawn from a stale page",
          eegs_tex_missing() == 1u);
}

/* ── LANE 12f gate (b): the recorded stream read back as triangles ──────────────────
 *
 * The claim this rung has to survive is not "the batch is shorter" and not "the counters say so" --
 * it is that one PRIM plus a run of vertices IS the thousand calls the immediate path makes, for
 * this mesh, corner by corner.  So the two paths are recorded separately and both are DECODED,
 * through the recorder's own group layout, into the triangles the GS would rasterise: three vertices
 * at a time for a list, every consecutive triple for a strip, with the two vertices a split re-
 * emits landing in the new group exactly where the continuation needs them.  A stream that overflowed
 * the capture, broke the size law, or used an op this decoder does not know returns -1 rather than a
 * short list, so a comparison can never pass on a partial decode.
 *
 * What is compared is the triangle ORDER and each triangle's three CORNERS, where a corner is its
 * whole payload -- colour, texture coordinate and position together, one quadword each.  What is
 * deliberately not compared is the order the three corners are NAMED in within one triangle: that is
 * the winding, and `vertex.c`'s `cnt & 1` exists to fix it for a rasteriser that culls.  This stream
 * culls nothing, and because a corner carries its own colour with its own position, permuting the
 * three of them cannot move a colour to a different vertex or a vertex to a different pixel.  The
 * control at the bottom of the case is what proves the comparator is not order-blind in the way that
 * matters: the same mesh with the zigzag's turn-around removed -- every row walked forwards -- must
 * NOT decode equal.
 */

typedef struct {
    uint64_t rgbaq;
    uint64_t st;
    uint64_t xyz2;
} payload_t;                     /* a corner: the three registers that are not the PRIM */

typedef struct {
    payload_t c[3];
} dec_tri_t;

#define TRI_MAX 96
#define GRID_MAX_PTS 20          /* rows*cols for the meshes this gate draws: 5x4 and 3x3 */

static tgl_grid_pt_t s_pts[GRID_MAX_PTS];

/* The AD group draws nothing, so it contributes no triangle; every other group this stage writes is a
 * dense `FLG=REGLIST` vertex group whose data quadwords are TWO PER VERTEX, the REGS list packed end
 * to end.  That is also where the primitive has to be read FROM: a dense group's tag leaves its PRIM
 * field clear, because the primitive travels as the first register of every loop instead -- `gs_op_of
 * (g)` on such a group answers "POINT", which is 0 and is the field's empty value, not a fact about
 * the drawing.  The shape guard is the same law seen from the other side: a group that names four
 * registers and does not pack them is the retired sparse shape, and a decoder that read its second
 * register out of the third one's place would decode a stream the GS never rasterised. */
static int decode_stream(dec_tri_t *dst, int dst_max)
{
    int n = 0, gi, k;

    for (gi = 0; gi < s_cap_i; gi++) {
        const uint64_t *g = s_cap[gi];
        uint32_t nloop, step, v, op;

        if (gs_regs_of(g) == 0x0Eu) continue;
        if (gs_nreg_of(g) != GS_REC_REGS_N) return -1;
        if (gs_flg_of(g) != GS_FLG_REGLIST) return -1;

        nloop = gs_nloop_of(g);
        if (s_cap_n[gi] != gs_group_qw(nloop)) return -1;    /* the size law is part of the decode */

        op = (uint32_t)(gs_vq(g, 1u, 0u) & 7u);
        if (op == GS_OP_TRI)            step = 3u;
        else if (op == GS_OP_TRISTP)    step = 1u;
        else return -1;

        for (v = 1u; v + 2u <= nloop; v += step) {
            dec_tri_t *t;

            if (n >= dst_max) return -1;
            t = &dst[n];
            for (k = 0; k < 3; k++) {
                const uint32_t vi = v + (uint32_t)k;
                t->c[k].rgbaq = gs_vq(g, vi, 1u);
                t->c[k].st    = gs_vq(g, vi, 2u);
                t->c[k].xyz2  = gs_vq(g, vi, 3u);
            }
            n++;
        }
    }
    return n;
}

static int payload_equal(payload_t a, payload_t b)
{
    return a.rgbaq == b.rgbaq && a.st == b.st && a.xyz2 == b.xyz2;
}

/* Each of a's corners has to be found among b's, and no corner may be used twice.  With the three
 * payloads of a triangle distinct -- which is why every mesh here gives every point its own colour,
 * and why the case checks it -- greedy matching is exact. */
static int tri_equal(const dec_tri_t *a, const dec_tri_t *b)
{
    int used[3] = { 0, 0, 0 };
    int i, j;

    for (i = 0; i < 3; i++) {
        int found = -1;

        for (j = 0; j < 3; j++) {
            if (!used[j] && payload_equal(a->c[i], b->c[j])) {
                found = j;
                break;
            }
        }
        if (found < 0) return 0;
        used[found] = 1;
    }
    return 1;
}

/* 0 when the two decoded streams are the same triangle sequence; otherwise the 1-based index of the
 * first triangle that differs, or -1 if the lengths differ. */
static int tri_seqs_equal(const dec_tri_t *A, int na, const dec_tri_t *B, int nb)
{
    int i;

    if (na != nb) return -1;
    for (i = 0; i < na; i++) {
        if (!tri_equal(&A[i], &B[i])) return i + 1;
    }
    return 0;
}

/* A mesh at (x, y) = (col, row) whole pixels -- the unit the viewport map delivers in -- with one
 * distinct grey byte per point.  Those are the two properties the comparator above needs, and they
 * are also what makes an expected XYZ2 or RGBAQ a literal rather than a formula. */
static void fill_grid(int rows, int cols)
{
    int r, c;

    for (r = 0; r < rows; r++) {
        for (c = 0; c < cols; c++) {
            const int i = r * cols + c;
            s_pts[i].x_px = c;
            s_pts[i].y_px = r;
            s_pts[i].rgba = 0xFF000000u | (uint32_t)(i + 1);
        }
    }
}

static int grid_colours_distinct(int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            if (s_pts[i].rgba == s_pts[j].rgba) return 0;
        }
    }
    return 1;
}

/* `xb_draw_ribbon()`'s submission order, and the order `eegs_grid_strip()` walks: for every row pair,
 * down the columns, with the odd row backwards so the strip never has to be broken at a turn-around.
 * serpentine=0 walks every row forwards instead, which is the control. */
static int zigzag(int *seq, int rows, int cols, int serpentine)
{
    int r, c, n = 0;

    for (r = 0; r < rows - 1; r++) {
        for (c = 0; c < cols; c++) {
            const int col = (serpentine && (r & 1)) ? (cols - 1 - c) : c;
            seq[n++] = r * cols + col;
            seq[n++] = (r + 1) * cols + col;
        }
    }
    return n;
}

static eegs_vertex_t point_vert(int i)
{
    /* The same three fields `emit_point()` fills from a mapped point: the texcoords are 0 because
     * the map this call consumes refuses a context with a texture enabled, and 0 is what
     * `gs_vertex()` writes for an untextured vertex. */
    return vert(s_pts[i].x_px, s_pts[i].y_px, 0.0f, 0.0f, s_pts[i].rgba);
}

/* The mesh the way the shipped path records it: one `eegs_triangle()` per triple of the submitted
 * sequence, which is exactly the ring-of-three's rule for a GL_TRIANGLE_STRIP. */
static void list_grid(const int *seq, int n)
{
    int i;

    for (i = 2; i < n; i++) {
        eegs_vertex_t v[3];

        v[0] = point_vert(seq[i - 2]);
        v[1] = point_vert(seq[i - 1]);
        v[2] = point_vert(seq[i]);
        eegs_triangle(v, k_grid_st);
    }
}

/* The batch entry reads five fields of the map -- `ok`, `smooth`, `blended` and the rect
 * `vp_x0`..`vp_y1` -- because the mesh it is handed has ALREADY been mapped, by
 * `tgl_grid_map_points()`, which is gate (a)'s subject in test_grid_map.c.  The affine coefficients
 * are zero here on purpose and nothing here reads them.  The rect is wide because the synthetic
 * points below are `x_px = c`, `y_px = r`: this suite's mesh lives in grid coordinates, and the
 * refusal gate for it is the vertex word's representable range, not the rect (see
 * test_grid_strip_range_gate), which supplies its own narrow rects to make the two questions come
 * apart. */
static tgl_grid_map_t grid_map(void)
{
    tgl_grid_map_t m;

    memset(&m, 0, sizeof m);
    m.ok = 1u;
    m.smooth = 1u;
    m.blended = 1u;
    m.vp_x0 = 0; m.vp_x1 = 4095;
    m.vp_y0 = 0; m.vp_y1 = 4095;
    return m;
}

static void test_grid_strip_matches_list(int rows, int cols)
{
    tgl_grid_map_t m = grid_map();
    tgl_grid_map_t zero;
    static dec_tri_t A[TRI_MAX], B[TRI_MAX], C[TRI_MAX];
    int seq[GRID_MAX_PTS * 2];
    int na, nb, nc, nv, submitted, bad, strip_groups, list_groups;
    uint32_t strip_verts, strip_prims, list_verts, list_prims;
    char label[96];

    memset(&zero, 0, sizeof zero);
    fill_grid(rows, cols);

    sprintf(label, "%dx%d: every point carries a different colour, so corner matching is exact",
            rows, cols);
    check(label, grid_colours_distinct(rows * cols));
    check("a zeroed map is refused rather than drawn",
          eegs_grid_strip(&zero, s_pts, rows, cols) == -1);
    check("and a mesh smaller than one quad is refused too",
          eegs_grid_strip(&m, s_pts, 1, cols) == -1);
    check("the only exit left is the argument one, and it says so",
          eegs_grid_why() == TGL_GRID_REFUSE_ARG);

    /* The batch: one PRIM for the whole mesh. */
    start();
    submitted = eegs_grid_strip(&m, s_pts, rows, cols);
    eegs_frame_end();
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    sprintf(label, "%dx%d: the batch submitted (rows-1)*cols*2 vertices", rows, cols);
    check(label, submitted == (rows - 1) * cols * 2);
    strip_groups = s_cap_i;
    strip_verts = eegs_verts();
    strip_prims = eegs_prims();
    na = decode_stream(A, TRI_MAX);
    sprintf(label, "%dx%d: the batch's stream decodes", rows, cols);
    check(label, na >= 0);
    sprintf(label, "%dx%d: a strip of %d vertices is %d triangles, splits included",
            rows, cols, submitted, submitted - 2);
    check(label, na == submitted - 2);
    sprintf(label, "%dx%d: and the stage counted the same number", rows, cols);
    check(label, strip_prims == (uint32_t)(submitted - 2));
    check("the batch's groups all open their loops with the strip's OP 4 -- in the register, because "
          "a dense tag names no primitive",
          (gs_vq(VG(0), 1u, 0u) & 7u) == GS_OP_TRISTP &&
          (gs_vq(VG(strip_groups - 1), 1u, 0u) & 7u) == GS_OP_TRISTP);

    /* The shipped path, same grid, same submission order. */
    start();
    nv = zigzag(seq, rows, cols, 1);
    sprintf(label, "%dx%d: the zigzag submits the batch's vertex count", rows, cols);
    check(label, nv == submitted);
    list_grid(seq, nv);
    eegs_frame_end();
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    list_groups = s_cap_i;
    list_verts = eegs_verts();
    list_prims = eegs_prims();
    nb = decode_stream(B, TRI_MAX);
    sprintf(label, "%dx%d: the list's stream decodes", rows, cols);
    check(label, nb == na);
    bad = tri_seqs_equal(A, na, B, nb);
    sprintf(label, "%dx%d: the strip decodes to the same %d triangles as the list", rows, cols, na);
    check(label, bad == 0);
    if (bad != 0) {
        const dec_tri_t *ta = &A[bad - 1], *tb = &B[bad - 1];
        int i;

        printf("    first differing triangle: %d\n", bad);
        for (i = 0; i < 3; i++) {
            printf("      strip c%d xyz2=0x%016llx rgbaq=0x%016llx\n", i,
                   (unsigned long long)ta->c[i].xyz2, (unsigned long long)ta->c[i].rgbaq);
        }
        for (i = 0; i < 3; i++) {
            printf("      list  c%d xyz2=0x%016llx rgbaq=0x%016llx\n", i,
                   (unsigned long long)tb->c[i].xyz2, (unsigned long long)tb->c[i].rgbaq);
        }
    }
    printf("  %dx%d: batch %d group%s / %u vertices for %d triangles,"
           " list %d group%s / %u vertices\n",
           rows, cols, strip_groups, strip_groups == 1 ? "" : "s", strip_verts, na,
           list_groups, list_groups == 1 ? "" : "s", list_verts);

    /* The two counters LANE 12f exists to move. */
    sprintf(label, "%dx%d: the batch pays %d vertex groups where the list pays %d",
            rows, cols, strip_groups, list_groups);
    check(label, strip_groups < list_groups);
    sprintf(label, "%dx%d: the list's verts:prims is the parked 3:1 (%u/%u)",
            rows, cols, list_verts, list_prims);
    check(label, list_prims == (uint32_t)nb && list_verts == 3u * list_prims);
    sprintf(label, "%dx%d: and the batch's is not (%u/%u)", rows, cols, strip_verts, strip_prims);
    check(label, strip_verts != 3u * strip_prims);

    /* The control: the same mesh, the same list path, one law removed -- the odd row walked
     * forwards instead of backwards.  If this compared equal, the gate above would be measuring a
     * comparator that accepts any triangulation of the right vertices. */
    start();
    nv = zigzag(seq, rows, cols, 0);
    list_grid(seq, nv);
    eegs_frame_end();
    nc = decode_stream(C, TRI_MAX);
    sprintf(label, "%dx%d: the un-zigzagged mesh is REFUSED by the same comparison", rows, cols);
    check(label, nc == na && tri_seqs_equal(A, na, C, nc) != 0);
}

/* A smooth, untextured, blended TRISTP: op 4 + IIP 8 + ABE 64 = 76 = 0x4C.  Under the dense shape
 * that word is the FIRST REGISTER of every loop, not a tag field: the group's tag leaves
 * PRIM[57:47] clear, sets FLG[59:58]=1 and is otherwise the same NREG 4 / EOP word every other vertex
 * group here carries -- 0x44000000_00008000 | NLOOP, one word for the strip and the list and the line.
 * The retired PACKED form is kept below as its control, built from the same field table and this same
 * word: 0x4C at [57:47] with PRE[46] set, EOP[15] set and NREG[63:60]=4 -- digit 11 holds PRE alone
 * (4) because op 4 has no bit 0, digit 12 holds IIP's bit 50 and op's bit 49 (6), digit 13 holds
 * ABE's bit 53 (2), and digit 14 holds nothing of it now that CTXT is 0.  So the control is
 * 0x4026_4000_0000_8000 and its XOR with the dense tag is 0x0426_4000_0000_0000: the dense tag's own
 * bit 58, bit 46, and those three PRIM bits, which is the whole of what changed. */
static const uint64_t k_prim_stp_sb  = 4u + 8u + 64u + K_CTX;
static const uint64_t k_tag_retired_stp_sb =
    (4ULL << 60) | (k_prim_stp_sb << 47) | (1ULL << 46) | 0x8000ULL;
static const uint64_t k_prim_stp_unblended = 4u + 8u + K_CTX;   /* 0x0C */

/* The 3x3 mesh's ONE group, 25 quadwords, spelled out of the field tables by hand: twelve vertices
 * in the zigzag's own order, 0,3,1,4,2,5 then the turn-around's repeat of 5 and 8,4,7,3,6.  Each
 * point is (x, y) = (col, row) px, so XYZ2 is (0x8000 + 16*row)<<16 | (0x8000 + 16*col) with Z 0
 * above bit 32, and RGBAQ is 0x3F800000 above the grey the mesh was filled with. */
static const uint64_t k_grid3_rgbaq[12] = {
    0x3F800000FF000001ULL,   /* p0 */
    0x3F800000FF000004ULL,   /* p3 */
    0x3F800000FF000002ULL,   /* p1 */
    0x3F800000FF000005ULL,   /* p4 */
    0x3F800000FF000003ULL,   /* p2 */
    0x3F800000FF000006ULL,   /* p5 */
    0x3F800000FF000006ULL,   /* p5 again: the turn-around names it twice */
    0x3F800000FF000009ULL,   /* p8 */
    0x3F800000FF000005ULL,   /* p4 */
    0x3F800000FF000008ULL,   /* p7 */
    0x3F800000FF000004ULL,   /* p3 */
    0x3F800000FF000007ULL    /* p6 */
};

static const uint64_t k_grid3_xyz2[12] = {
    0x0000000080008000ULL,
    0x0000000080108000ULL,
    0x0000000080008010ULL,
    0x0000000080108010ULL,
    0x0000000080008020ULL,
    0x0000000080108020ULL,
    0x0000000080108020ULL,
    0x0000000080208020ULL,
    0x0000000080108010ULL,
    0x0000000080208010ULL,
    0x0000000080108000ULL,
    0x0000000080208000ULL
};

static void test_grid_strip_group_bytes(void)
{
    tgl_grid_map_t m = grid_map();
    uint64_t *w;
    uint32_t q, zero_halves;
    int k;

    fill_grid(3, 3);
    start();
    check("the 3x3 mesh submits twelve vertices and crosses no bus while it accumulates",
          eegs_grid_strip(&m, s_pts, 3, 3) == 12 && s_cap_i == 0);
    eegs_frame_end();

    check("the capture saw one group and dropped nothing", s_cap_i == 1 && s_cap_drops == 0);
    if (s_cap_i < 1) return;
    w = VG(0);
    check("one tag plus twelve two-quadword vertices = 25 quadwords", VN(0) == 25u);
    check("NLOOP 12", gs_nloop_of(w) == 12u);
    check_word("the group's tag, word for word", gs_qw(w, 0u), k_tag_dense | 12u);
    check_word("CONTROL: the retired PACKED tag, same NLOOP -- FLG, PRE and the strip's own PRIM bits, "
               "and nothing else",
               gs_qw(w, 0u) ^ (k_tag_retired_stp_sb | 12u), 0x0426400000000000ULL);
    check_word("REGS: PRIM, RGBAQ, ST, XYZ2", gs_regs_of(w), 0x5210ULL);

    for (k = 0; k < 12; k++) {
        const uint32_t v = (uint32_t)k + 1u;
        char label[96];

        sprintf(label, "vertex %d's PRIM is the strip's 0x4C", k);
        check_word(label, gs_vq(w, v, 0u), k_prim_stp_sb);
        sprintf(label, "vertex %d's RGBAQ", k);
        check_word(label, gs_vq(w, v, 1u), k_grid3_rgbaq[k]);
        sprintf(label, "vertex %d's ST is the untextured 0", k);
        check_word(label, gs_vq(w, v, 2u), 0ULL);
        sprintf(label, "vertex %d's XYZ2", k);
        check_word(label, gs_vq(w, v, 3u), k_grid3_xyz2[k]);
    }

    /* The stride, censused rather than assumed: 24 data quadwords therefore 48 register halves, and
     * the only zero halves anywhere in the group are the twelve ST registers this mesh never samples.
     * The retired sparse shape padded every second half of the vertex's quadwords AND gave the empty ST
     * a quadword of its own, so the same census there counted 24 + 12 zeros in 48 halves.  A vertex
     * that costs four quadwords cannot hide inside this number. */
    zero_halves = 0;
    for (q = 1u; q < VN(0); q++) {
        if (gs_half(w, q, 0u) == 0u) zero_halves++;
        if (gs_half(w, q, 1u) == 0u) zero_halves++;
    }
    check("48 data halves, exactly 12 of them the empty ST registers",
          VN(0) == 25u && zero_halves == 12);

    /* The two state bits that are the batch's own decisions: the blend flag comes from the map, and
     * the texture flag is cleared whatever the map says, because a mapped mesh samples nothing.  Both
     * are read from the loop's PRIM register, because in this shape the tag names no primitive at all.
     */
    start();
    m.blended = 0u;
    eegs_grid_strip(&m, s_pts, 3, 3);
    eegs_frame_end();
    if (s_cap_i < 1) return;
    check_word("an unblended map gives PRIM 0x0C", gs_vq(VG(0), 1u, 0u), k_prim_stp_unblended);
    check("and TME stays cleared", (gs_vq(VG(0), 1u, 0u) & 16u) == 0u);
}

/* The walk's first ribbon triangle runs on a stream that has just been pointed at nothing, and so
 * does the batch -- `eegs_grid_strip()`'s one `eegs_texture(0)`.  The law that makes that safe is
 * 5b's: `TME` in the PRIM word, not the page in `TEX0`, decides whether a primitive samples. */
static void test_grid_strip_drops_the_plane(void)
{
    tgl_grid_map_t m = grid_map();
    eegs_vertex_t v[3];

    fill_grid(3, 3);
    start();
    eegs_texture(&s_plane);
    v[0] = point_vert(0);
    v[1] = point_vert(1);
    v[2] = point_vert(2);
    eegs_triangle(v, k_stb);
    check("the batch submits after the textured draw, as the ribbon does mid-frame",
          eegs_grid_strip(&m, s_pts, 3, 3) == 12);
    eegs_frame_end();

    check("switch group, the textured group, then the strip's own group", s_cap_i == 3);
    if (s_cap_i < 3) return;
    check_word("the textured group kept TME", gs_vq(s_cap[1], 1u, 0u), k_prim_tri_stb);
    check_word("the strip's group does not", gs_vq(s_cap[2], 1u, 0u), k_prim_stp_sb);
    check("the plane was already none, so the batch cost no second switch",
          eegs_tex_switches() == 1u && eegs_tex_missing() == 0u);
}

/* The bound `eegs_grid_strip()` refuses at.  GS_XYOFFSET_PAGE is (2048u << 4) and
 * `gs_vertex_coord16()` writes `(px * 16) + GS_XYOFFSET_PAGE` into XYZ2's 16-bit field, so a
 * window-local whole pixel is representable over exactly -2048 .. +2047.  Spelled as a literal
 * because the stage keeps the same number as a private macro: a gate that could not disagree with
 * the code it measures would not be measuring anything. */
#define GRID_XY_LIMIT 2048

/* LANE 12f rung (ii), as amended by the ribbon's own measurement: the batch does not clip, so the
 * only mesh it may refuse is one the GS cannot REPRESENT.  The clip rect is the SCISSOR's --
 * `eegs_present_send()` programs it from the same client rect the map carries -- and `over=` is the
 * column that makes that reliance an instrumented fact rather than a hope.
 *
 * Three claims, each with its own falsifier:
 *
 *   A mesh that leaves the rect is TAKEN.  The first version of the gate refused it, which is what
 *   the live runs printed as `why=7` on every frame of the wave while `f_ribbon` stayed at 22,282 us:
 *   `xb_paint()` projects the sheet with the window's 776x536 and draws it into the client's 768x500,
 *   so the ribbon's last column is 8 px outside, permanently.  The check is the vertices on the bus,
 *   not only the return value.
 *
 *   A mesh with a point outside the field's own extent, -2048 .. +2047, is refused and named RANGE --
 *   and the refusal happens BEFORE anything is recorded, so a frame that falls back to the immediate
 *   walk keeps exactly the stream it had.  That is why that case draws a triangle first and then
 *   decodes the group instead of only checking -1: a scan that ran after the emit loop would pass
 *   `== -1` and still leave the mesh's vertices on the bus.  An unrepresentable coordinate is also the
 *   one thing the SCISSOR cannot fix -- the field folds and the triangle lands somewhere else on the
 *   canvas.
 *
 *   A refusal leaves `over=` exactly where it found it, because the column describes the taken
 *   batches and not the geometry of a mesh the GS never saw.
 *
 * The controls, all run against this file by mutation on 2026-10-15: deleting the range branch makes
 * the two folding cases return 24 with batch=4 slow=0 why=NONE; putting the rect back as the refusal
 * condition -- the gate as first written -- makes every overhang case return -1 and name 7; and
 * committing `over` inside the scan rather than after it makes the refusal case read over=3. */
static void test_grid_strip_range_gate(void)
{
    tgl_grid_map_t m = grid_map();
    uint64_t *w;

    fill_grid(4, 4);                        /* x_px = col, y_px = row, both 0..3 */

    /* A rect that is exactly the grid's own extent, inclusive at both ends: nothing overhangs, so the
     * frame's `over=` is the column's own word for "the SCISSOR had no work to do". */
    start();
    m.vp_x0 = 0; m.vp_x1 = 3; m.vp_y0 = 0; m.vp_y1 = 3;
    check("a grid whose last column and last row sit on the rect's far edge is batched",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24);
    check("and the frame's two exits read batch=1 slow=0",
          eegs_grid_batch() == 1u && eegs_grid_slow() == 0u);
    check("with no refusal named -- the column is cleared with the counters, not left over",
          eegs_grid_why() == TGL_GRID_REFUSE_NONE);
    check("and nothing asked the GS's clip window to cut a point", eegs_grid_over() == 0u);

    /* One column outside the rect: the amended gate takes the mesh, which is the whole amendment --
     * the ribbon's 8 px of overdraw, restated at this suite's scale. */
    m.vp_x1 = 2;
    start();
    check("a mesh whose last column leaves the rect is batched anyway",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24);
    check("and the whole mesh reached the stream: 24 vertices, 22 triangles",
          eegs_verts() == 24u && eegs_prims() == 22u);
    check("with the refusal column counted, one point per row, and not refused",
          eegs_grid_over() == 4u && eegs_grid_slow() == 0u);
    check("named as nothing-refused, because a count is not a `why=`",
          eegs_grid_why() == TGL_GRID_REFUSE_NONE);
    m.vp_x1 = 3;

    /* The other three edges are the same scan, and each is the only edge cut. */
    m.vp_y1 = 2;
    start();
    check("a row past the far edge is taken and counted the same way",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24 && eegs_grid_over() == 4u);
    m.vp_y1 = 3; m.vp_y0 = 1;
    start();
    check("so is one before the near edge",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24 && eegs_grid_over() == 4u);
    m.vp_y0 = 0;

    /* Per POINT rather than per axis, which is what makes the column read as pixels: a rect cutting
     * one column and one row leaves 4 + 4 points, one of them counted twice by an axis-wise total
     * and once by this one. */
    m.vp_x1 = 2; m.vp_y1 = 2;
    start();
    check("two overhanging edges give 7 points, not 8",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24 && eegs_grid_over() == 7u);
    m.vp_x1 = 3; m.vp_y1 = 3;

    /* From here the rect is wide again -- `grid_map()`'s own 0..4095 -- so that a point the SCISSOR
     * would cut and a point the vertex word cannot carry are measured apart: the first lands in
     * `over=`, the second takes the whole mesh. */
    m = grid_map();
    start();
    s_pts[9].x_px = GRID_XY_LIMIT - 1;        /* 2047: the last value the field holds unwrapped */
    check("a point on the last representable pixel is batched",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24);
    check("and is not over, because the rect reaches past it", eegs_grid_over() == 0u);

    start();
    fill_grid(4, 4);
    s_pts[9].x_px = GRID_XY_LIMIT;            /* (2048*16 + 2048*16) mod 65536 == 0: it folds */
    {
        eegs_vertex_t tv[3];

        tv[0] = point_vert(0);
        tv[1] = point_vert(1);
        tv[2] = point_vert(2);
        eegs_triangle(tv, k_grid_st);
    }
    check("one pixel further is refused, and the mesh with it",
          eegs_grid_strip(&m, s_pts, 4, 4) == -1);
    check("named as the vertex word's range, not as the rect and not as a map that failed to lift",
          eegs_grid_why() == TGL_GRID_REFUSE_RANGE);
    eegs_frame_end();
    check("counted as slow, with no batch",
          eegs_grid_batch() == 0u && eegs_grid_slow() == 1u);
    check("the refusal recorded nothing: the frame is the one triangle's group and no more",
          s_cap_i == 1 && s_cap_drops == 0);
    if (s_cap_i >= 1) {
        w = VG(0);
        check("that group is still the triangle's three vertices, not three plus a strip",
              gs_nloop_of(w) == 3u);
    }

    /* The negative side is the same field with the page origin in the middle, and it is the side a
     * point above or left of the window's origin reaches.  The field is 16-bit UNSIGNED around
     * `GS_XYOFFSET_PAGE`, so its extent is asymmetric by one: -2048 packs to 0x0000 and 2047 to
     * 0xFFF0, and both are representable while 2048 and -2049 fold.  -2048 is also outside a rect
     * that starts at 0, so it is the taken case `over=` exists for. */
    start();
    fill_grid(4, 4);
    s_pts[0].x_px = -GRID_XY_LIMIT;
    check("the low end of the field is batched, and counted as one point of clipping",
          eegs_grid_strip(&m, s_pts, 4, 4) == 24 && eegs_grid_over() == 1u);

    start();
    fill_grid(4, 4);
    s_pts[0].x_px = -GRID_XY_LIMIT - 1;       /* packs to 0xFFF0, the same word as +2047 */
    check("one pixel past it is refused", eegs_grid_strip(&m, s_pts, 4, 4) == -1);
    check("on the x axis, at the low end, under the same name",
          eegs_grid_why() == TGL_GRID_REFUSE_RANGE);

    start();
    fill_grid(4, 4);
    s_pts[5].y_px = GRID_XY_LIMIT;
    check("and the y axis is the same field, not a second gate",
          eegs_grid_strip(&m, s_pts, 4, 4) == -1 &&
          eegs_grid_why() == TGL_GRID_REFUSE_RANGE);

    /* A refusal that comes after the scan has already walked overhangs: the running total is local
     * and committed only when the mesh is taken.  The scan goes in row order, so the offender below
     * is the mesh's last point and three outside-the-rect points precede it -- a counter incremented
     * in place would print 3 on a frame the GS batched nothing on. */
    start();
    fill_grid(4, 4);
    m.vp_x1 = 2;
    s_pts[15].y_px = GRID_XY_LIMIT;          /* row 3, col 3: outside the rect AND outside the field */
    check("a mesh that fails the range test at its last point is refused",
          eegs_grid_strip(&m, s_pts, 4, 4) == -1);
    check("and the frame's over= is still zero, not the three points passed on the way",
          eegs_grid_over() == 0u);
    m.vp_x1 = 3;

    /* `over=` is the frame's, like the other three: two taken meshes sum their outside points, because
     * the column is how much clipping this frame asked the GS for. */
    start();
    fill_grid(4, 4);
    m.vp_x1 = 2;
    eegs_grid_strip(&m, s_pts, 4, 4);
    m.vp_x1 = 3;
    eegs_grid_strip(&m, s_pts, 4, 4);
    check("two meshes, one of them overhanging: batch=2 slow=0 over=4",
          eegs_grid_batch() == 2u && eegs_grid_slow() == 0u && eegs_grid_over() == 4u);
    eegs_frame_end();

    /* A refused map reaches the same counter as a refused coordinate, because the counting is on this
     * entry's return path and not in the caller: the caller that could not capture a map hands over
     * the zeroed struct and gets -1 and slow=1 for its trouble. */
    start();
    {
        tgl_grid_map_t zero;
        memset(&zero, 0, sizeof zero);
        check("an uncaptured map is refused", eegs_grid_strip(&zero, s_pts, 4, 4) == -1);
        /* Without this, the row's `why=` would read 0 -- which is the column's own word for
         * "nothing refused" -- on exactly the frame that refused for want of a captured map. */
        check("named as never having been captured, not as no refusal",
              eegs_grid_why() == TGL_GRID_REFUSE_UNSET);
    }
    check("and counted as slow, the same column the range refusal feeds",
          eegs_grid_batch() == 0u && eegs_grid_slow() == 1u);

    /* And the counters are the frame's, not the build's: begin() clears all four, which is what makes
     * the row readable as one pass rather than as a running total. */
    start();
    check("a new frame starts batch, slow, why and over at zero",
          eegs_grid_batch() == 0u && eegs_grid_slow() == 0u &&
          eegs_grid_why() == TGL_GRID_REFUSE_NONE && eegs_grid_over() == 0u);
}

/* ── LANE 12h rung (i): the textured batch ────────────────────────────────────────────────
 *
 * f_menu's 1,436 textured vertices are the column `slow=` never counted, because none of them was
 * ever asked: the grey strip entry refuses a textured context by design (why=4), and the icon and
 * text runs stayed on the walk.  `eegs_tex_quads()` is the entry that was built for that state --
 * it submits vertex.c's own GL_QUADS split through `eegs_triangle()`, so its gate is the same law
 * 12f established, with one payload the grey strip could never exercise: the ST register, which is
 * the whole point of a textured draw and carries 0 on every vertex `eegs_grid_strip()` emits.
 */

/* Two quads make eight points, and this mesh keeps the strip's unit: whole pixels, quads laid side
 * by side along x.  The corner order is the one vertex.c's GL_QUADS case assumes -- (0,0), (1,0),
 * (1,1), (0,1) -- because the split below is that case's own: (v0,v1,v2) then (v0,v2,v3).  s, t and
 * the grey byte all differ per corner, which is what keeps the corner-set comparator exact. */
#define QUAD_MAX_PTS 32
static tgl_tex_pt_t s_qpts[QUAD_MAX_PTS];

static void fill_quads(int nquads)
{
    static const int cx[4] = { 0, 1, 1, 0 };
    static const int cy[4] = { 0, 0, 1, 1 };
    int q, k;

    for (q = 0; q < nquads; q++) {
        for (k = 0; k < 4; k++) {
            const int i = q * 4 + k;
            s_qpts[i].x_px = 2 * q + cx[k];
            s_qpts[i].y_px = cy[k];
            s_qpts[i].s    = 0.25f + 0.5f * (float)i;
            s_qpts[i].t    = 0.125f + 0.25f * (float)i;
            s_qpts[i].rgba = 0xFF000000u | (uint32_t)(i + 1);
        }
    }
}

/* The textured map over this suite's grey one: the affine coefficients are the same deliberate
 * zeros, because the mesh has ALREADY been mapped by `tgl_grid_map_tex_points()` before either
 * entry sees it -- that is gate (a)'s subject in test_grid_map.c.  The four mapper constants are
 * the 96x64 sheet's own from `texture.c` -- sscale = (96 << 11) - 1024 and friends -- carried so the
 * struct arriving here is the shape the caller hands over; nothing in `eegs_tex_quads()` reads
 * them, and nothing here re-derives them. */
static tgl_tex_map_t tex_map(const void *plane)
{
    tgl_tex_map_t m;

    memset(&m, 0, sizeof m);
    m.g      = grid_map();
    m.sscale = 195584.0f;            /* (96 << 11) - 1024 */
    m.soff   = 1024.0f;              /* half a texel, folded in by the mapper, not by us */
    m.tscale = 33292288.0f;          /* (64 << 19) - 262144 */
    m.toff   = 262144.0f;
    m.pixmap = plane;
    return m;
}

static eegs_vertex_t qvert(int i)
{
    return vert(s_qpts[i].x_px, s_qpts[i].y_px, s_qpts[i].s, s_qpts[i].t, s_qpts[i].rgba);
}

/* The shipped path for a textured quad mesh, as the walk's adapter makes it today: the plane named
 * once, then each quad through two `eegs_triangle()` calls.  alt_split=1 takes the OTHER diagonal
 * -- the control, same corners, same colours, different triangles. */
static void list_quads(int nquads, const gs_tex_t *plane, int alt_split)
{
    eegs_vertex_t v[3];
    int q;

    eegs_texture(plane);
    for (q = 0; q < nquads; q++) {
        const int b = q * 4;

        if (!alt_split) {
            v[0] = qvert(b);     v[1] = qvert(b + 1); v[2] = qvert(b + 2);
            eegs_triangle(v, k_stb);
            v[1] = qvert(b + 2); v[2] = qvert(b + 3);
            eegs_triangle(v, k_stb);
        } else {
            v[0] = qvert(b);     v[1] = qvert(b + 1); v[2] = qvert(b + 3);
            eegs_triangle(v, k_stb);
            v[0] = qvert(b + 1); v[1] = qvert(b + 2); v[2] = qvert(b + 3);
            eegs_triangle(v, k_stb);
        }
    }
}

/* A flat, unblended TRI that STILL samples: 3 + 16 = 19 = 0x13.  TME is the entry's own decision -- the
 * map required a bound-and-enabled sheet -- so zeroing the map's two flags must not clear it. */
static const uint64_t k_prim_tex_plain = 3u + 16u + K_CTX;

static void test_tex_quads_matches_list(void)
{
    tgl_tex_map_t m = tex_map(s_tex_key);
    static dec_tri_t A[TRI_MAX], B[TRI_MAX], C[TRI_MAX];
    uint64_t *w;
    int na, nb, nc, bad, submitted, batch_groups, list_groups;
    uint32_t batch_verts, list_verts;

    fill_quads(2);

    /* The batch: one AD switch for the whole run, then ONE vertex group -- twelve emitted vertices
     * for four triangles, because `eegs_triangle()` pays three corners per primitive and the quad's
     * shared diagonal is named twice, exactly as the walk names it. */
    start();
    submitted = eegs_tex_quads(&m, s_qpts, 2);
    eegs_frame_end();
    check("two quads submit four triangles -- this entry counts prims, not the strip's vertices",
          submitted == 4);
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    check("one switch group, then one vertex group for the whole run", s_cap_i == 2);
    batch_groups = s_cap_i;
    batch_verts = eegs_verts();
    check("twelve vertices, four triangles on the stage's own counters",
          eegs_verts() == 12u && eegs_prims() == 4u);
    check("one plane change paid the run, and no primitive lost its texture",
          eegs_tex_switches() == 1u && eegs_tex_missing() == 0u);
    check("batch=1 slow=0 over=0, and no refusal named",
          eegs_grid_batch() == 1u && eegs_grid_slow() == 0u &&
          eegs_grid_over() == 0u && eegs_grid_why() == TGL_GRID_REFUSE_NONE);
    if (s_cap_i < 2) return;

    check_word("the switch carries the AD trio's own tag", gs_qw(s_cap[0], 0u), k_tag_ad3);
    check_word("and its REGS nibble is AD", gs_regs_of(s_cap[0]), 0x0Eu);
    check_word("CLAMP first, the registry entry's word", gs_qw(s_cap[0], 1u), s_plane.clamp);
    check_word("TEX0 second", gs_qw(s_cap[0], 2u), s_plane.tex0);
    check_word("TEX1 third", gs_qw(s_cap[0], 3u), s_plane.tex1);

    w = VG(1);
    check("NLOOP 12, and the group's size is its law: one tag plus twelve two-quadword vertices",
          gs_nloop_of(w) == 12u && VN(1) == gs_group_qw(12u) && VN(1) == 25u);
    check_word("the group's tag is the one dense word -- this stage's TRI, strip, line and point all "
               "carry it, with only NLOOP differing",
               gs_qw(w, 0u), k_tag_dense | 12u);
    check_word("CONTROL: the retired PACKED tag for the same textured TRI",
               gs_qw(w, 0u) ^ (k_tag_retired_tri_stb | 12u), 0x042DC00000000000ULL);
    check_word("vertex 1 PRIM", gs_vq(w, 1u, 0u), k_prim_tri_stb);
    check_word("vertex 1 RGBAQ: quad 0's first corner, grey 1, Q = 1.0f",
               gs_vq(w, 1u, 1u), 0x3F800000FF000001ULL);
    /* The ST register, spelled by hand the way the field table says: 0.25f = 2^-2 is 0x3E800000 at
     * [31:0] and 0.125f = 2^-3 is 0x3E000000 at [63:32].  This word is the thing the grey strip
     * gate could never check -- it writes 0 there whatever the map says. */
    check_word("vertex 1 ST: s = 0.25 low, t = 0.125 high",
               gs_vq(w, 1u, 2u), 0x3E0000003E800000ULL);
    check_word("vertex 1 XYZ2: (0,0) px", gs_vq(w, 1u, 3u), 0x0000000080008000ULL);
    /* 0.75f = 1.5 x 2^-1 = 0x3F400000 and 0.375f = 1.5 x 2^-2 = 0x3EC00000: mantissas that are not
     * bare exponents, so the full 32-bit pattern of both halves is checked, not only the sign. */
    check_word("vertex 2 ST: s = 0.75, t = 0.375", gs_vq(w, 2u, 2u), 0x3EC000003F400000ULL);
    check_word("vertex 7 RGBAQ: quad 1's first corner, grey 5",
               gs_vq(w, 7u, 1u), 0x3F800000FF000005ULL);
    check_word("vertex 7 XYZ2: x = 2 px", gs_vq(w, 7u, 3u), 0x0000000080008020ULL);

    na = decode_stream(A, TRI_MAX);
    check("the batch's stream decodes to its four triangles", na == 4);

    /* The shipped path, same mesh, same submission order: what the clipper hands the stage per
     * textured quad today, adapter and all. */
    start();
    list_quads(2, gs_tex_find(s_tex_key), 0);
    eegs_frame_end();
    check("nothing dropped, nothing refused on the list's stream either",
          s_cap_drops == 0 && gs_rec_unsplittable() == 0);
    list_groups = s_cap_i;
    list_verts = eegs_verts();
    nb = decode_stream(B, TRI_MAX);
    check("the list decodes to the same count", nb == na);
    bad = tri_seqs_equal(A, na, B, nb);
    check("and the SAME four triangles, corner for corner, payload for payload -- the ST register "
          "included, which is this mesh's whole subject", bad == 0);
    if (bad != 0) {
        const dec_tri_t *ta = &A[bad - 1], *tb = &B[bad - 1];
        int i;

        printf("    first differing triangle: %d\n", bad);
        for (i = 0; i < 3; i++) {
            printf("      batch c%d xyz2=0x%016llx rgbaq=0x%016llx st=0x%016llx\n", i,
                   (unsigned long long)ta->c[i].xyz2, (unsigned long long)ta->c[i].rgbaq,
                   (unsigned long long)ta->c[i].st);
        }
        for (i = 0; i < 3; i++) {
            printf("      list  c%d xyz2=0x%016llx rgbaq=0x%016llx st=0x%016llx\n", i,
                   (unsigned long long)tb->c[i].xyz2, (unsigned long long)tb->c[i].rgbaq,
                   (unsigned long long)tb->c[i].st);
        }
    }
    printf("  2-quad batch: %d groups / %u vertices for %d triangles;"
           " list: %d groups / %u vertices\n",
           batch_groups, batch_verts, na, list_groups, list_verts);

    /* The control: the other diagonal of the same quad.  Same four corners, same four colours, a
     * different mesh -- if THAT decoded equal, the gate above would be comparing vertex sets rather
     * than the split `vertex.c` makes. */
    start();
    list_quads(2, gs_tex_find(s_tex_key), 1);
    eegs_frame_end();
    nc = decode_stream(C, TRI_MAX);
    check("the alternate split is REFUSED by the same comparison",
          nc == na && tri_seqs_equal(A, na, C, nc) != 0);

    /* The two lifted flags ride the map and the textured bit rides the entry: a flat, unblended
     * textured draw still samples, because the map only ever arrives bound-and-enabled. */
    start();
    {
        tgl_tex_map_t mf = tex_map(s_tex_key);
        mf.g.smooth  = 0u;
        mf.g.blended = 0u;
        eegs_tex_quads(&mf, s_qpts, 2);
    }
    eegs_frame_end();
    if (s_cap_i >= 2) {
        check_word("flat and unblended gives PRIM 0x13, TME intact",
                   gs_vq(VG(1), 1u, 0u), k_prim_tex_plain);
    }
}

/* The refusals, the fold and the counters -- the strip's battery re-asked against this entry's two
 * extra degrees of freedom: the plane key, and the triangle-vs-vertex return unit. */
static void test_tex_quad_gates(void)
{
    tgl_tex_map_t m = tex_map(s_tex_key);
    tgl_grid_map_t gm = grid_map();
    eegs_vertex_t tv[3];
    uint64_t *w;

    fill_quads(2);

    /* The three argument refusals share one name and one column. */
    start();
    check("a null map is refused", eegs_tex_quads(0, s_qpts, 1) == -1);
    check("a null mesh is refused", eegs_tex_quads(&m, 0, 1) == -1);
    check("and so is a mesh of no quads", eegs_tex_quads(&m, s_qpts, 0) == -1);
    check("named as the argument case", eegs_grid_why() == TGL_GRID_REFUSE_ARG);
    check("counted slow three times, batched once never",
          eegs_grid_slow() == 3u && eegs_grid_batch() == 0u);

    /* A caller-zeroed map carries ok=0 AND no reason: the same UNSET the strip's gate gives, on the
     * shared column, because the [GSTS] row reads one battery for both entries. */
    {
        tgl_tex_map_t zero;
        memset(&zero, 0, sizeof zero);
        check("an uncaptured map is refused and named UNSET",
              eegs_tex_quads(&zero, s_qpts, 1) == -1 &&
              eegs_grid_why() == TGL_GRID_REFUSE_UNSET);
    }

    /* A refusal the map carries is handed over under its own name -- and NOTEX (10) is this map's
     * inverse question, the one the grey map can never produce. */
    {
        tgl_tex_map_t mn = tex_map(s_tex_key);
        mn.g.ok = 0u;
        mn.g.refuse = TGL_GRID_REFUSE_NOTEX;
        check("the textured map's inverse arrives named: why=NOTEX, not flattened to UNSET",
              eegs_tex_quads(&mn, s_qpts, 1) == -1 &&
              eegs_grid_why() == TGL_GRID_REFUSE_NOTEX);
    }

    /* Overhang, the ribbon law restated on quads: the rect ending at x=2 leaves quad 1's two x=3
     * corners outside, permanently -- exactly the wave's 8 px at this suite's scale.  Taken, and
     * counted for the SCISSOR. */
    start();
    m.g.vp_x1 = 2;
    check("a quad mesh whose last two points leave the rect is batched anyway",
          eegs_tex_quads(&m, s_qpts, 2) == 4);
    check("and the whole mesh reached the stream: 12 vertices, 4 prims",
          eegs_verts() == 12u && eegs_prims() == 4u);
    check("two points counted for the clip, nothing refused",
          eegs_grid_over() == 2u && eegs_grid_why() == TGL_GRID_REFUSE_NONE);
    m.g.vp_x1 = 4095;

    /* The range refusal, with a triangle already in the frame: the strip's law verbatim -- a
     * refusal records nothing and commits nothing.  Two x=3 overhangs precede the offender in the
     * scan; a counter incremented in place would print 2 on a frame that batched nothing. */
    start();
    fill_grid(3, 3);
    tv[0] = point_vert(0); tv[1] = point_vert(1); tv[2] = point_vert(2);
    eegs_triangle(tv, k_grid_st);
    fill_quads(2);
    s_qpts[7].x_px = GRID_XY_LIMIT;        /* folds in the 12.4 field, as the strip's case shows */
    m.g.vp_x1 = 2;
    check("a corner outside the vertex word's field refuses the whole mesh",
          eegs_tex_quads(&m, s_qpts, 2) == -1);
    check("named RANGE, not the rect it also leaves", eegs_grid_why() == TGL_GRID_REFUSE_RANGE);
    check("and over= stays zero: the running count is committed only with the mesh",
          eegs_grid_over() == 0u);
    check("counted slow, batched nothing",
          eegs_grid_batch() == 0u && eegs_grid_slow() == 1u);
    eegs_frame_end();
    check("the refusal recorded nothing: the frame is the one triangle's group and no more",
          s_cap_i == 1 && s_cap_drops == 0);
    if (s_cap_i >= 1) {
        w = VG(0);
        check("and that group is still the triangle's three vertices", gs_nloop_of(w) == 3u);
    }
    m.g.vp_x1 = 4095;

    /* The field's last value is a legal coordinate: 2047 packs to 0xFFF0 and draws. */
    start();
    fill_quads(2);
    s_qpts[7].x_px = GRID_XY_LIMIT - 1;
    check("the same corner at 2047 px is batched", eegs_tex_quads(&m, s_qpts, 2) == 4);
    check("and not over, because the restored rect reaches past it", eegs_grid_over() == 0u);

    /* The demotion: a key the registry does not know -- a texture bound with its bytes never
     * uploaded -- draws the way the walk itself folds that state, FLAT, and says so in `texmiss`.
     * It is batched, not refused: the walk drew these quads either way; the only question is who
     * counts them. */
    start();
    {
        tgl_tex_map_t md = tex_map(&s_never_uploaded);
        check("an unregistered sheet is still batched, as a flat draw",
              eegs_tex_quads(&md, s_qpts, 2) == 4);
    }
    eegs_frame_end();
    check("it costs no AD group at all: one vertex group, zero switches",
          s_cap_i == 1 && eegs_tex_switches() == 0u);
    if (s_cap_i >= 1) {
        check_word("and the primitives came out flat: 0x4B, TME cleared",
                   gs_vq(VG(0), 1u, 0u), k_prim_tri_untex);
    }
    check("four triangles, four misses -- the column the bench reads as texmiss",
          eegs_tex_missing() == 4u);
    check("a demotion is not a refusal: batch=1 slow=0",
          eegs_grid_batch() == 1u && eegs_grid_slow() == 0u);

    /* The counters are shared by the two entries, because the [GSTS] row is one battery: a strip
     * mesh and a quad mesh in one frame sum into batch=, and the plane the strip dropped the quads
     * pick up again -- one switch, in order. */
    start();
    fill_grid(4, 4);
    check("the strip entry takes its mesh in the same frame: 24 vertices",
          eegs_grid_strip(&gm, s_pts, 4, 4) == 24);
    fill_quads(1);
    check("the quad entry takes its mesh too: 2 triangles",
          eegs_tex_quads(&m, s_qpts, 1) == 2);
    check("batch=2 slow=0 across both entries",
          eegs_grid_batch() == 2u && eegs_grid_slow() == 0u);
    check("the strip pointed at nothing, the quads named the sheet: exactly one switch",
          eegs_tex_switches() == 1u && eegs_tex_missing() == 0u);

    /* And the six columns are the frame's: begin() zeroes the pair it shares with the strip's four. */
    start();
    check("a new frame starts batch, slow, why, over, switches and missing at zero",
          eegs_grid_batch() == 0u && eegs_grid_slow() == 0u &&
          eegs_grid_why() == TGL_GRID_REFUSE_NONE && eegs_grid_over() == 0u &&
          eegs_tex_switches() == 0u && eegs_tex_missing() == 0u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 EE/GS raster stage vs hand-built GIF groups (cap=%u qw)\n", GS_REC_QW_CAP);
    printf("==========================================================\n");    /* The plane start_plane() names, so that the textured cases draw against a plane the way the
     * adapter in clip.c does.  Its three words are sentinels and nothing here compares them: a
     * stable object whose *identity* the stage can cache is what these cases need, and the words
     * themselves are gate by gs_tex_register()'s own suite. */
    s_plane = plane_words(0xA1A2A3A4A5A6A7A8ULL, 0xB1B2B3B4B5B6B7B8ULL,
                          0xC1C2C3C4C5C6C7C8ULL);

    test_one_triangle();
    test_state_reuse();
    test_state_change_closes();
    test_lines();
    test_cut_is_always_whole_triangles();
    test_frame_boundary();
    test_texture_switch();
    test_grid_strip_matches_list(3, 3);
    test_grid_strip_matches_list(5, 4);
    test_grid_strip_group_bytes();
    test_grid_strip_drops_the_plane();
    test_grid_strip_range_gate();
    test_tex_quads_matches_list();
    test_tex_quad_gates();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the raster stage asks for the primitives it was told to\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a recorded triangle is not what the GS would draw\n");
    return 1;
}
