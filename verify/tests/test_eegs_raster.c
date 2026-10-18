/*
 * verify/tests/test_eegs_raster.c -- what the raster stage decides, against words written by hand
 *
 * Row 5, step 5a of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The discipline is
 * increments 2, 3 and 4's: `backend_eegs.c` and the recorder are compiled into this program, and
 * every expectation was written from the GS field tables before the code ran.
 *
 * What is under test here is not the encodings -- `gs_pack_giftag`, `gs_pack_xyz2` and the rest
 * have their own hand literals in test_ps2_gs_pack.c and test_ps2_gs_rec.c -- but the nine things
 * only the raster stage can get wrong:
 *
 *   1. which PRIM word a triangle asks for, i.e. op 3 with IIP/TME/ABE following the GL state and
 *      the texture context always 0, the register set the environment actually programs (every
 *      literal below is written as op + flags by hand);
 *   2. that a device-space vertex is passed through and never re-derived, because the y direction,
 *      the viewport map and the window's origin are already in the numbers the span loops walked;
 *   3. that a repeated state does not close the group -- the law that keeps a ribbon's 1,828
 *      triangles in one packet instead of 1,828 packets of three vertices;
 *   4. that a state change does, because the group's PRIM word and its REGS list are one tag, and a
 *      second state needs a second group's second tag.  The tag's PRIM field is what this stage asks
 *      for; whether the GS reads it there is item 9's question, and the answer is that it does not --
 *      so the same word travels a second time, as a register, in the descriptor that opens the chain;
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
 *   9. from LANE 19 rung 12v, PRIM as a REGISTER.  Rung 12u measured on the machine that a group tag's
 *      PRIM field does not program the GS: a captured triangle replayed alone laid one pixel, and laid
 *      610 the moment an AD write of the same word went in front of it.  The recorder now sends that
 *      write -- PACKED, NLOOP=1, NREG=1, one quadword of value and one of `GS_GP_PRIM` -- from
 *      `open_group()`, so the stream this stage emits is a sequence of PAIRS and every count below is
 *      `2 * vertex groups + switches`.  Two consequences are gated here rather than mentioned: a
 *      capture index is no longer a vertex group, so every read goes through `VG()`/`VN()`, which
 *      select by the tag's own FLG, and `groups_are()` asserts the pair (its shape, and that the word
 *      it writes is the word the chain's tag names) for every complete frame; and the descriptor
 *      leaves with the group's FIRST VERTEX, so "nothing has crossed the bus yet" now means "no vertex
 *      group has", which is what `test_grid_strip_group_bytes()` and `test_frame_boundary()` say.
 *      What the pair does NOT change is the vertex group's own budget: the AD write goes out through
 *      the same sink with the cursor at zero, so a group still cuts at 18 textured vertices and a
 *      3x3 mesh is still one 13-quadword chain.
 *
 * The buffer is 32 quadwords (the test's own #define, matching increment 4's gate), which is what
 * makes the last check also prove that a group cut by capacity never lands mid-triangle.  The unit
 * that decides where the cut falls is the recorder's: at 32 quadwords = 64 register halves, a textured
 * vertex costs 3 halves and is offered 4 for its pad and headroom, so a TRI list -- which may only be
 * cut at a multiple of three -- reaches 18 vertices before it cuts, and 36 vertices is TWO groups and
 * not three.  Every vertex-level expectation below is that arithmetic; only the quadwords per group
 * and the tag words change when the list is.
 *
 * The quadwords are read through gs_group_view.h, the same view test_ps2_gs_rec.c uses: a GIF
 * quadword is 128 bits and every register this stage writes is 64 of them, so a captured group is
 * pairs of uint64_t and `qwc` counts pairs, not words -- and because a vertex group is a dense
 * `FLG=REGLIST`, each of those pairs carries TWO registers of the group's REGS list, which is why
 * the register reads below go through `gs_vq()` and not through a word index.  One statement of that
 * unit rather than two is deliberate -- see that header's comment on how the half-width layout got
 * through both gates.  What a vertex costs in those pairs is not one number any more either: since
 * LANE 19 rung 12f a group's REGS list is its primitive's, two registers untextured and three
 * textured, so every size below is read as `gs_group_qw(nloop, nreg)` with the nreg the group names.
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

/* LANE 12f's batch entry submits a whole mesh in one call, so one frame of it is many groups: an
 * untextured vertex costs one quadword, so a group reaches 25 of them before the recorder offers a
 * boundary, and the 5x4 grid is two groups where the list that must decode to the same thirty
 * triangles is four.  Since LANE 19 rung 12v every one of those groups crosses the bus beside a
 * descriptor of its own, so the widest case here is the list's eight vertex groups in sixteen
 * captures; 40 is above that with room for the AD switch a textured case puts first.
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
 * while the vertices after it accumulate.  `s_vg` is how many of those switches this frame names:
 * 1 after start_plane(), 0 after start(), which is the whole difference between the two.
 *
 * LANE 19 rung 12v put a SECOND kind of AD group in this stream, and it is not a leading one: a
 * packed one-register write of PRIM goes out in front of EVERY vertex group, because rung 12u
 * measured on the machine that a group tag's PRIM field does not program the GS's PRIM register.
 * So `s_cap` is no longer "the switch, then the vertex groups" -- it alternates descriptor, chain,
 * descriptor, chain, and a capture index is no longer a way of naming a vertex group.  Reading one
 * as if it were is what every failure in this gate's first 12v run was: the descriptor's tag has no
 * PRIM field, so `gs_prim_of()` answers 0, and it names no register, so `gs_vq()` answers its
 * all-ones sentinel.
 *
 * The accessors below therefore select by the tag's own FLG -- the same way test_ps2_gs_rec.c reads
 * the stream since the first AD group landed -- and the pair is not an index convention but a law,
 * asserted for every complete frame by `groups_are()`: `s_cap_i == switches + 2 * vertex groups`. */
static gs_tex_t s_plane;
static int s_vg;

static int group_is_vertex(int k)
{
    return k >= 0 && (uint32_t)k < (uint32_t)s_cap_i && gs_flg_of(s_cap[k]) == GS_FLG_REGLIST;
}

static uint32_t vgroups(void)
{
    uint32_t i, n = 0u;
    for (i = 0u; i < (uint32_t)s_cap_i; i++) if (group_is_vertex((int)i)) n++;
    return n;
}

/* Every capture that is not a vertex group: the PRIM descriptors and the texture switches, which are
 * one shape -- PACKED, one REGS nibble of AD -- and differ only in how many registers they carry. */
static uint32_t adgroups(void)
{
    uint32_t i, n = 0u;
    for (i = 0u; i < (uint32_t)s_cap_i; i++) if (!group_is_vertex((int)i)) n++;
    return n;
}

static int vg_at(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (!group_is_vertex(k)) continue;
        if (seen++ == i) return k;
    }
    return -1;
}

static int ad_at(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (group_is_vertex(k)) continue;
        if (seen++ == i) return k;
    }
    return -1;
}

/* The descriptor that opened vertex group `i` is the capture immediately before it -- that is the
 * alternation, and `groups_are()` is the place that proves it rather than leaning on it. */
static int pd_at(int i)
{
    int k = vg_at(i);
    return k <= 0 ? -1 : k - 1;
}

/* Out of range answers the capture array's own zeroed slot 0 and a zero count, so a case that forgot
 * to check its group count fails a hand literal instead of reading past the buffer. */
static uint64_t *vg(int i)  { const int k = vg_at(i); return k < 0 ? s_cap[0] : s_cap[k]; }
static uint32_t vg_n(int i) { const int k = vg_at(i); return k < 0 ? 0u : s_cap_n[k]; }
static uint64_t *adg(int i) { const int k = ad_at(i); return k < 0 ? s_cap[0] : s_cap[k]; }
static uint64_t *pd(int i)  { const int k = pd_at(i); return k < 0 ? s_cap[0] : s_cap[k]; }
static uint32_t pd_n(int i) { const int k = pd_at(i); return k < 0 ? 0u : s_cap_n[k]; }

/* An AD group's data quadword carries the register's value in its LOW half and the register's GP
 * offset in its high half, so the PRIM word a descriptor writes is one whole quadword. */
static uint64_t pd_prim(int i) { return gs_qw(pd(i), 1u); }

#define VG(k)   vg(k)
#define VN(k)   vg_n(k)
#define PD(k)   pd(k)
#define PDN(k)  pd_n(k)

/* The pair law, asserted over a whole frame rather than assumed by an index: `nsw` texture switches
 * and `nv` descriptor/chain pairs, nothing else in the capture.  For each pair the descriptor is
 * checked as a group in its own right -- PACKED, one loop, NREG 1, its single REGS nibble AD, EOP set,
 * PRE clear, one tag quadword plus one register quadword, and its data word's GP offset is
 * `GS_GP_PRIM` -- and then against the chain it introduces: the 11 low bits of the word it writes are
 * the PRIM field of that chain's tag, which is the one fact the GS cannot survive a disagreement
 * about.  The recorder writes the two out of the same `s_prim`, so this is not a claim that they were
 * kept in step by hand; it is the claim that nothing between `open_group()` and the sink re-derived
 * one of them.
 *
 * A frame that is mid-flight is NOT this shape -- a descriptor whose vertices are still accumulating
 * is one AD group and no chain -- and the two cases that read the stream before `eegs_frame_end()`
 * say so in their own rows instead of reaching for this helper. */
static int groups_are(uint32_t nv, uint32_t nsw)
{
    uint32_t i;

    if ((uint32_t)s_cap_i != nsw + 2u * nv) return 0;
    if (vgroups() != nv || adgroups() != nsw + nv) return 0;
    if (gs_rec_prim_writes() != nv) return 0;
    if (gs_rec_sends() != (uint32_t)s_cap_i + (uint32_t)s_cap_drops) return 0;
    for (i = 0u; i < nv; i++) {
        const uint64_t *d = PD((int)i);

        if (group_is_vertex(pd_at((int)i))) return 0;
        if (gs_flg_of(d) != GS_FLG_PACKED || gs_nreg_of(d) != 1u ||
            gs_regs_of(d) != (uint64_t)GS_NIB_AD) return 0;
        if (gs_nloop_of(d) != 1u || gs_eop_of(d) != 1u || gs_pre_of(d) != 0u) return 0;
        if (PDN((int)i) != GS_REC_AD_QWC(1u)) return 0;
        if (gs_upper_of(d, 1u) != (uint64_t)GS_GP_PRIM) return 0;
        if ((pd_prim((int)i) & 0x7FFu) != gs_prim_of(VG((int)i))) return 0;
    }
    return 1;
}

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

static const eegs_tri_state_t k_stb  = { 1, 1, 1, 0 };   /* smooth, textured, blended */
static const eegs_tri_state_t k_flat_tex = { 0, 1, 1, 0 };
static const eegs_tri_state_t k_smooth_bare = { 1, 0, 0, 0 };
static const eegs_tri_state_t k_blend_only = { 0, 0, 1, 0 };

/* What the XMB ribbon's triangles look like to this stage: Gouraud because GL_SMOOTH is the default
 * and `xmb.c` never calls glShadeModel, blended because the ribbon sets `glBlendFunc(GL_DST_COLOR,
 * GL_ONE)`, untextured because it calls `glDisable(GL_TEXTURE_2D)` first.  The batch's own state
 * comes from the map's two flags rather than from a struct, and the two have to agree -- which is
 * what the strip-vs-list comparison below is for. */
static const eegs_tri_state_t k_grid_st = { 1, 0, 1, 0 };

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

/* A textured group's tag, by hand: NREG 3 for {RGBAQ, ST, XYZ2}.  Every field is the GIFTAG table
 * itself -- NLOOP[14:0], EOP at 15, PRE at 46, PRIM[57:47], FLG 1 at 58, NREG 3 at 60 -- and only the
 * primitive differs between the groups below, because PRE carries it once per packet instead of once
 * per vertex. */
static inline uint64_t eegs_tag(uint32_t nloop, uint64_t prim)
{
    return 0x3400400000008000ULL | ((prim & 0x7FFULL) << 47) | (uint64_t)nloop;
}

/* The same word for an untextured group, whose list is the two-register {RGBAQ, XYZ2} chain: digit 15
 * reads 2.  This is the shape LANE 19 rung 12e's boxes D and F measured as INTERPOLATING, and the
 * four-register list it replaced -- with its leading NOP slot and the ST a primitive with TME cleared
 * never samples -- is what measured as ONE FLAT COLOUR. */
static inline uint64_t eegs_tag_flat(uint32_t nloop, uint64_t prim)
{
    return 0x2400400000008000ULL | ((prim & 0x7FFULL) << 47) | (uint64_t)nloop;
}

/* The shape this replaced, kept as the control it is: NREG 4 and EOP, with FLG 0 (PACKED), PRE set at
 * 46 and the textured smooth blended TRI's PRIM word in [57:47] -- BUILT from `k_prim_tri_stb` rather
 * than frozen as a second copy of the encoding, so this control follows the CTXT law instead of
 * quietly keeping an old one.  Its XOR with a shipping tag therefore names only the two fields the
 * fix touched: NREG's nibble (4 against 3 textured, 4 against 2 untextured) and FLG's bit 58, because
 * PRE and PRIM are the same words in both.  LANE 19 rung 5 put the four registers in front of this
 * tag and the census counted zero pixels, at either value of PRE; rung 12e found the four-register
 * list itself was what drew one flat colour, and boxes D and F shipped the shorter chains that shade. */
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
    v.z = 0u;
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

/* One triangle, its six quadwords spelled out: the tag, then the three-register chain {RGBAQ, ST,
 * XYZ2} packed end to end, which makes a vertex one and a half quadwords and leaves the group owing a
 * pad half -- 11 halves of header and data, sent as 6 quadwords.  Since LANE 19 rung 12v that chain
 * travels with a companion: a two-quadword AD group in front of it that WRITES the PRIM the tag only
 * mentions, so the frame the GS sees is eight quadwords over two packets, and the words below are
 * still the chain's own. */
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

    check_word("a smooth textured blended TRI asks for PRIM 0x5B", gs_prim_of(VG(0)),
               k_prim_tri_stb);
    check("one triangle is one vertex group and one descriptor: a tag plus nine registers and "
          "their pad, preceded by the AD write that puts their PRIM on the machine",
          groups_are(1u, (uint32_t)s_vg) && VN(0) == gs_group_qw(3u, GS_REC_NREG_TEX) && VN(0) == 6u);
    check("three vertices, one triangle", eegs_verts() == 3u && eegs_prims() == 1u);
    if (vgroups() < 1u) return;
    w = VG(0);
    check_word("and the descriptor in front of that chain writes the same PRIM, whole",
               pd_prim(0), k_prim_tri_stb);

    check_word("the group's tag: NLOOP 3, FLG=REGLIST, NREG 3, PRIM in tag",
               gs_qw(w, 0u), eegs_tag(3u, k_prim_tri_stb));
    check_word("CONTROL that word against the retired PACKED tag: NREG and FLG differ",
               gs_qw(w, 0u) ^ (k_tag_retired_tri_stb | 3u), 0x7400000000000000ULL);
    check_word("REGS: RGBAQ, ST, XYZ2, in the tag's upper half",
               gs_regs_of(w), 0x521ULL);

    /* rgba 0xFF332244 is a=0xFF b=0x33 g=0x22 r=0x44, so the byte order the GS wants --
     * R[7:0] G[15:8] B[23:16] A[31:24] -- is 0xFF332244 unchanged, with Q = 1.0f above it. */
    check_word("vertex 1 RGBAQ unpacks the packed colour low byte first",
               gs_vq(w, 1u, GS_NIB_RGBAQ), 0x3F800000FF332244ULL);
    check_word("vertex 1 ST, 0,0", gs_vq(w, 1u, GS_NIB_ST), 0x0000000000000000ULL);
    check_word("vertex 1 XYZ2: the window's own top-left, in 12.4",
               gs_vq(w, 1u, GS_NIB_XYZ2), 0x0000000080008000ULL);

    /* The pass-through law, at the far corner of the measured 768x500 client: 767 px is 12,272
     * sixteenths and 12,272 + 32,768 = 45,040 = 0xAFF0; 499 px is 7,984 + 32,768 = 40,752 =
     * 0x9F30.  XYZ2 is X[15:0] then Y[31:16]. */
    check_word("vertex 2 XYZ2: 767,499 passed through, not re-projected",
               gs_vq(w, 2u, GS_NIB_XYZ2), 0x000000009F30AFF0ULL);
    check_word("vertex 2 RGBAQ: all four bytes zero is a=0, not a=255",
               gs_vq(w, 2u, GS_NIB_RGBAQ), 0x3F80000000000000ULL);
    check_word("vertex 2 ST: s = 1.0f low, t = 2.0f high",
               gs_vq(w, 2u, GS_NIB_ST), 0x400000003F800000ULL);
    /* 250 px is 4,000 sixteenths + 32,768 = 36,768 = 0x8FA0, and 20 px is 320 + 32,768 = 0x8140.
     * This case used to be 20.09375 px, whose 321.5 sixteenths the recorder truncated to 321, and it
     * is retired rather than marked passing: under LANE 12a no caller can hand this layer a
     * fraction, because the viewport map writes a GLint.  The truncation itself is still gated --
     * it is ps2_sf_fixsfsi(), which test_ps2_softfloat.c sweeps -- but nothing on the host now
     * checks that a clipped vertex's whole-pixel value is the one the map produced.  That is a real
     * gap opened by this change, and it is named here rather than papered over. */
    check_word("vertex 3 XYZ2: whole pixels, scaled and folded onto the page",
               gs_vq(w, 3u, GS_NIB_XYZ2), 0x000000008FA08140ULL);
    check_word("vertex 3 RGBAQ: opaque white",
               gs_vq(w, 3u, GS_NIB_RGBAQ), 0x3F800000FFFFFFFFULL);

    /* The unit, one more time from this stage's side, and now reading in the direction the format
     * runs: of this group's ten data halves, eight are registers of the loop and exactly two are not
     * ink -- vertex 1's ST of (0,0), which is a register that legitimately holds zero, and the pad the
     * eleventh half of an odd chain becomes.  Under the retired PACKED stride every second half of
     * every vertex was padding, which is the shape rung 5 measured as inert; under the four-register
     * dense list that replaced it the same census counted the NOP twice. */
    badpad = 0;
    for (k = 2u; k < VN(0) * 2u; k++) {
        if (gs_half(w, k / 2u, k % 2u) == 0u) badpad++;
    }
    check("ten data halves, two of them zero: vertex 1's ST and the odd chain's pad",
          badpad == 2 && VN(0) == 6u);
    check_word("vertex 1's zero ST is quadword 1's high half, a register and not a gap",
               gs_upper_of(w, 1u), 0u);
    check_word("and the group's last half is the pad close_group() wrote",
               gs_upper_of(w, 5u), 0u);
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

    check("three same-state triangles are still ONE vertex group after the switch, and the frame "
          "is its switch, its one descriptor and that group -- three sends, not one per triangle",
          groups_are(1u, (uint32_t)s_vg));
    check("nine vertices, three triangles", eegs_verts() == 9u && eegs_prims() == 3u);
    if (vgroups() < 1u) return;
    check("NLOOP 9, so the group holds all three whole",
          (gs_qw(VG(0), 0u) & 0x7FFFu) == 9u);
    check("2 + 3*9 = 29 halves, padded to 15 quadwords",
          VN(0) == gs_group_qw(9u, GS_REC_NREG_TEX) && VN(0) == 15u);

    /* LANE 12e's park, read off the group this frame built: a triangle is three vertices, so
     * `verts` is exactly 3 x `prims`, and the OP the group's PRIM carries is the list's 3, not the
     * strip's 4.  These are the two things a re-lived continuation has to move before it can ship,
     * and the second is why the strip primitive itself stays gated in test_ps2_gs_rec.c -- the
     * recorder implements it; this stage no longer asks for it. */
    check("the park holds: verts is 3 x prims, so no vertex was ever continued",
          eegs_verts() == 3u * eegs_prims());
    check_word("and this stage's group carries OP 3, the triangle list",
               gs_op_of(VG(0)), 3u);
}

/* A state change is a group boundary by construction: the tag carries the primitive once, with PRE
 * set, so a second state cannot ride a group whose tag names the first one.  What makes the two
 * groups different packets is that tag word and nothing else -- the REGS list is the same three
 * registers, because both states here sample the same plane. */
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

    check("a flat triangle after a smooth one is two vertex groups, each with its own descriptor",
          groups_are(2u, (uint32_t)s_vg));
    if (vgroups() < 2u) return;
    check_word("the first group's PRIM keeps IIP", gs_prim_of(VG(0)), k_prim_tri_stb);
    check_word("the second's drops it: 0x5B without the 8", gs_prim_of(VG(1)),
               k_prim_tri_flat);
    check_word("and its tag carries its PRIM, NLOOP 3",
               gs_qw(VG(1), 0u), eegs_tag(3u, k_prim_tri_flat));
    check("so the two groups carry their distinct PRIM words",
          gs_prim_of(VG(0)) != gs_prim_of(VG(1)));
    check("and the descriptors are written per group, not cached per frame: the second one tells "
          "the machine 0x53 after the first told it 0x5B",
          pd_prim(0) == k_prim_tri_stb && pd_prim(1) == k_prim_tri_flat);
    check("so a state change costs one group, not one vertex group per triangle",
          VN(0) == gs_group_qw(3u, GS_REC_NREG_TEX) && VN(1) == gs_group_qw(3u, GS_REC_NREG_TEX));

    start_plane();
    eegs_triangle(v, k_stb);
    eegs_point(&v[0], k_stb);
    eegs_frame_end();
    check("a point after a triangle closes too, and is its own op",
          groups_are(2u, (uint32_t)s_vg));
    if (vgroups() < 2u) return;
    check_word("POINT with the same flags: 0 + 8 + 16 + 64",
               gs_prim_of(VG(1)), k_prim_point | 8u | 16u | 64u);
    check("the point group has one vertex", (gs_qw(VG(1), 0u) & 0x7FFFu) == 1u);
    check_word("and the point's own descriptor, not the triangle's, precedes it",
               pd_prim(1), k_prim_point | 8u | 16u | 64u);
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

    check("four segments of the same state are one group", groups_are(1u, 0u));
    if (vgroups() < 1u) return;
    check("eight vertices = four segments",
          (gs_qw(VG(0), 0u) & 0x7FFFu) == 8u && eegs_verts() == 8u);
    check_word("LINE, untextured, unblended, smooth: 1 + 8",
               gs_prim_of(VG(0)), k_prim_line_smooth);
    check("and a counted primitive is counted whether or not it is a triangle",
          eegs_prims() == 4u);
    check("one tag plus eight one-quadword untextured vertices = 9 quadwords",
          VN(0) == gs_group_qw(8u, GS_REC_NREG_UNTEX) && VN(0) == 9u);

    start();
    v[0] = vert(0, 0, 0.0f, 0.0f, 0xFF332244u);
    v[1] = vert(0, 10, 0.0f, 0.0f, 0xFF332244u);
    eegs_line(v, k_blend_only);
    eegs_frame_end();
    if (vgroups() < 1u) return;
    check_word("the same op with only ABE set: 1 + 64", gs_prim_of(VG(0)),
               k_prim_line);
    check_word("written to the machine by the group's own descriptor",
               pd_prim(0), k_prim_line);
    check("and a segment is a tag plus two one-quadword untextured vertices",
          eegs_verts() == 2u && VN(0) == gs_group_qw(2u, GS_REC_NREG_UNTEX) &&
          VN(0) == 3u);
}

/* Capacity at this layer's granularity: a group cut must not land between the first and second
 * vertex of a triangle, or the GS draws a two-vertex primitive and steals the third from the next
 * group.  At 32 quadwords = 64 register halves a textured vertex is charged four of them (three for
 * its chain, one for the pad an odd chain owes), so the list rule -- which may only cut at a multiple
 * of three -- offers its first boundary at the 19th vertex and cuts there: 18 vertices, 2 + 54 halves
 * = 28 quadwords.  12 triangles is 36 vertices, so two groups of 18 and one send per group. */
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

    check("36 vertices at this capacity is two vertex groups, and two sends more than the pre-12v "
          "shape because each one now carries its own PRIM",
          groups_are(2u, (uint32_t)s_vg));
    check("nothing was refused", gs_rec_unsplittable() == 0u);
    for (i = 0; i < (int)vgroups(); i++) {
        char label[64];
        unsigned long n = (unsigned long)gs_nloop_of(VG(i));
        sprintf(label, "vertex group %d's NLOOP %lu is a whole number of triangles", i, n);
        check(label, n % 3u == 0u && n > 0u);
        sprintf(label, "and vertex group %d is %lu three-register vertices plus a tag", i, n);
        check(label, VN(i) == gs_group_qw((uint32_t)n, GS_REC_NREG_TEX));
    }
    check("18 + 18 = 36", eegs_verts() == 36u);
    check("28 quadwords per group at this capacity", VN(0) == 28u && VN(1) == 28u);
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
     * cross the bus when they are written, while the vertices that use them accumulate.  LANE 19
     * rung 12v adds a third state in between, and this is the case that names it: a group's PRIM
     * descriptor leaves WITH ITS FIRST VERTEX, because `open_group()` writes it, so a frame can have
     * put a register group on the bus and still hold every pixel back. */
    eegs_texture(&a);
    check("the switch group leaves before the frame ends", s_cap_i == 1 && adgroups() == 1u);
    eegs_triangle(v, k_stb);
    check("and no vertex group has -- though the group's PRIM has gone out with its first vertex",
          s_cap_i == 2 && vgroups() == 0u && gs_rec_prim_writes() == 1u);
    eegs_frame_end();
    check("the vertex group is sent when the frame ends", s_cap_i == 3);
    if (vgroups() < 1u) return;
    check_word("so the frame opens with the AD tag", gs_qw(adg(0), 0u), k_tag_ad3);
    check_word("and the triangle that follows it is the textured one the plane makes legal",
               gs_prim_of(VG(0)), k_prim_tri_stb);

    /* A new frame has no plane either: the environment packet that ends a frame writes `TEX0` for
     * the window's default, so the last frame's page is not live state to inherit.  Asking for a
     * texture before naming a plane therefore costs a cleared `TME` and a count, not a sample of
     * whatever the previous frame left there. */
    start();
    check("a new frame starts with no counters, no open group and nothing on the bus",
          eegs_prims() == 0u && eegs_verts() == 0u && gs_rec_sends() == 0u && s_cap_i == 0);
    check("its texture counters start empty too",
          eegs_tex_switches() == 0u && eegs_tex_missing() == 0u);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("a textured triangle with no plane of this frame's is recorded without TME, and still "
          "pays its own descriptor",
          groups_are(1u, 0u) && eegs_tex_missing() == 1u);
    if (vgroups() < 1u) return;
    check_word("and the state it carries is the untextured one",
               gs_prim_of(VG(0)), k_prim_tri_untex);
    check_word("the state its descriptor wrote, too", pd_prim(0), k_prim_tri_untex);

    /* The state the recorder lost with the group has to be re-sent too: a frame that names a plane
     * and then draws pays one switch group and one vertex group, even when the previous frame used
     * the identical PRIM word. */
    start();
    eegs_texture(&a);
    eegs_triangle(v, k_stb);
    eegs_frame_end();
    check("so the second frame pays its own switch group, and its group pays its own descriptor",
          groups_are(1u, 1u));
    if (vgroups() < 1u) return;
    check_word("and the group it opens with is the AD tag", gs_qw(adg(0), 0u), k_tag_ad3);
    check_word("with the state the re-named plane makes legal",
               gs_prim_of(VG(0)), k_prim_tri_stb);
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

    check("the switch is a group before the vertices it applies to, and they are preceded in turn "
          "by their own PRIM descriptor",
          groups_are(1u, 1u) && s_cap_i == 3);
    if (vgroups() < 1u) return;
    check_word("its tag", gs_qw(adg(0), 0u), k_tag_ad3);
    check_word("its one REGS nibble is AD", gs_regs_of(adg(0)), 0x0Eu);
    check("so it is one tag plus three register quadwords",
          s_cap_n[0] == GS_REC_AD_QWC(3u) && s_cap_n[0] == 4u);
    check_word("CLAMP first", gs_qw(adg(0), 1u), a.clamp);
    check_word("  at GP offset 0x08", gs_upper_of(adg(0), 1u), 0x08u);
    check_word("TEX0 second", gs_qw(adg(0), 2u), a.tex0);
    check_word("  at GP offset 0x06", gs_upper_of(adg(0), 2u), 0x06u);
    check_word("TEX1 third", gs_qw(adg(0), 3u), a.tex1);
    check_word("  at GP offset 0x14", gs_upper_of(adg(0), 3u), 0x14u);
    check_word("and the group after it is the textured primitive",
               gs_prim_of(VG(0)), k_prim_tri_stb);
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
    check("a repeated plane pays nothing: one switch group, one descriptor, one vertex group",
          groups_are(1u, 1u));
    if (vgroups() < 1u) return;
    check("and the two triangles are one group of six vertices, twenty halves = 10 quadwords",
          gs_nloop_of(VG(0)) == 6u &&
          VN(0) == gs_group_qw(6u, GS_REC_NREG_TEX) && VN(0) == 10u);
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
    check("no group for the plane of none, but the open group splits on the PRIM -- and a split "
          "group is a split descriptor too",
          groups_are(2u, 1u));
    if (vgroups() < 2u) return;
    check_word("the triangle before it kept TME", gs_prim_of(VG(0)), k_prim_tri_stb);
    check_word("and the one after it lost it", gs_prim_of(VG(1)), k_prim_tri_untex);
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
 * whole payload -- colour, texture coordinate and position, each read by its REGS-list nibble rather
 * than by a position that only one of the two lists has.  What is
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

/* The AD groups draw nothing, so they contribute no triangle -- and since LANE 19 rung 12v there are
 * two of them in a frame, the texture switches' three-register group and every vertex group's
 * one-register PRIM descriptor, so the skip is by the tag's own FLG and not by the register count.
 * Every other group this stage writes is a dense `FLG=REGLIST` vertex group whose registers are the
 * primitive's own list packed end to end -- RGBAQ and XYZ2 untextured, RGBAQ, ST and XYZ2 textured.
 * The primitive is read from the TAG, because PRE carries it once per packet: `gs_op_of(g)` is the
 * group's op, and there is no per-vertex PRIM register left to read it from.  The shape guard is the
 * same law seen from the other side: a group whose NREG is neither of the two this recorder writes,
 * or whose FLG is not the dense one, is not a group this decoder can index -- reading a
 * three-register loop at a four-register stride names the next vertex's colour where this one's
 * coordinate lives. */
static int decode_stream(dec_tri_t *dst, int dst_max)
{
    int n = 0, gi, k;

    for (gi = 0; gi < s_cap_i; gi++) {
        const uint64_t *g = s_cap[gi];
        uint32_t nloop, nreg, step, v, op;

        if (!group_is_vertex(gi)) continue;
        nreg = gs_nreg_of(g);
        if (nreg != GS_REC_NREG_TEX && nreg != GS_REC_NREG_UNTEX) return -1;
        if (gs_flg_of(g) != GS_FLG_REGLIST) return -1;

        nloop = gs_nloop_of(g);
        if (s_cap_n[gi] != gs_group_qw(nloop, nreg)) return -1; /* the size law is part of the decode */

        op = gs_op_of(g);
        if (op == GS_OP_TRI)            step = 3u;
        else if (op == GS_OP_TRISTP)    step = 1u;
        else return -1;

        for (v = 1u; v + 2u <= nloop; v += step) {
            dec_tri_t *t;

            if (n >= dst_max) return -1;
            t = &dst[n];
            for (k = 0; k < 3; k++) {
                const uint32_t vi = v + (uint32_t)k;
                t->c[k].rgbaq = gs_vq(g, vi, GS_NIB_RGBAQ);
                t->c[k].st    = gs_vq(g, vi, GS_NIB_ST);
                t->c[k].xyz2  = gs_vq(g, vi, GS_NIB_XYZ2);
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

    /* The batch: one PRIM per group for the whole mesh -- and per group means 12v's descriptor too,
     * which is why `groups_are()` below reads twice the pre-12v capture count. */
    start();
    submitted = eegs_grid_strip(&m, s_pts, rows, cols);
    eegs_frame_end();
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    sprintf(label, "%dx%d: the batch submitted (rows-1)*cols*2 vertices", rows, cols);
    check(label, submitted == (rows - 1) * cols * 2);
    strip_groups = (int)vgroups();
    sprintf(label, "%dx%d: the batch's %d vertex group%s each carry their own descriptor",
            rows, cols, strip_groups, strip_groups == 1 ? "" : "s");
    check(label, groups_are((uint32_t)strip_groups, 0u));
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
    check("the batch's groups all carry the strip's OP 4 in their tags",
          gs_op_of(VG(0)) == GS_OP_TRISTP &&
          gs_op_of(VG(strip_groups - 1)) == GS_OP_TRISTP);

    /* The shipped path, same grid, same submission order. */
    start();
    nv = zigzag(seq, rows, cols, 1);
    sprintf(label, "%dx%d: the zigzag submits the batch's vertex count", rows, cols);
    check(label, nv == submitted);
    list_grid(seq, nv);
    eegs_frame_end();
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    list_groups = (int)vgroups();
    sprintf(label, "%dx%d: the list's %d vertex group%s each carry their own descriptor too",
            rows, cols, list_groups, list_groups == 1 ? "" : "s");
    check(label, groups_are((uint32_t)list_groups, 0u));
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
    printf("  %dx%d: batch %d vertex group%s (%d packets with their descriptors) / %u vertices"
           " for %d triangles; list %d vertex group%s (%d packets) / %u vertices\n",
           rows, cols, strip_groups, strip_groups == 1 ? "" : "s", 2 * strip_groups,
           strip_verts, na,
           list_groups, list_groups == 1 ? "" : "s", 2 * list_groups, list_verts);

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

/* A smooth, untextured, blended TRISTP: op 4 + IIP 8 + ABE 64 = 76 = 0x4C.  That word travels in the
 * TAG, at PRIM[57:47] with PRE[46] set, once for the whole group -- the register list this primitive
 * gets is the two-register untextured one, so the tag's NREG nibble is 2 and the group's own word is
 * `eegs_tag_flat()`: 0x2400_4000_0000_8000 | PRIM<<47 | NLOOP.
 * The retired PACKED form is kept below as its control, built from the same field table and this same
 * PRIM word: 0x4C at [57:47] with PRE[46] set, EOP[15] set and NREG[63:60]=4.  Its XOR with the
 * shipping tag is therefore the two fields the fix touched and nothing else -- the NREG nibble, 4
 * against 2, and FLG's bit 58: 0x6400_0000_0000_0000. */
static const uint64_t k_prim_stp_sb  = 4u + 8u + 64u + K_CTX;
static const uint64_t k_tag_retired_stp_sb =
    (4ULL << 60) | (k_prim_stp_sb << 47) | (1ULL << 46) | 0x8000ULL;
static const uint64_t k_prim_stp_unblended = 4u + 8u + K_CTX;   /* 0x0C */

/* The 3x3 mesh's ONE group, 13 quadwords, spelled out of the field tables by hand: twelve vertices
 * of two registers apiece, packed end to end behind the tag pair -- 2 + 24 halves, an even chain that
 * owes no pad.  The vertices are in the zigzag's own order, 0,3,1,4,2,5 then the turn-around's repeat
 * of 5 and 8,4,7,3,6.  Each point is (x, y) = (col, row) px, so XYZ2 is (0x8000 + 16*row)<<16 |
 * (0x8000 + 16*col) with Z 0 above bit 32, and RGBAQ is 0x3F800000 above the grey the mesh was filled
 * with. */
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
    check("the 3x3 mesh submits twelve vertices and no VERTEX group crosses while it accumulates",
          eegs_grid_strip(&m, s_pts, 3, 3) == 12 && vgroups() == 0u);
    check("the one thing that did cross is the group's PRIM, written with its first vertex",
          s_cap_i == 1 && gs_rec_prim_writes() == 1u && !group_is_vertex(0));
    eegs_frame_end();

    check("the capture saw one vertex group and its descriptor, and dropped nothing",
          groups_are(1u, 0u) && s_cap_drops == 0);
    if (vgroups() < 1u) return;
    w = VG(0);
    check("one tag plus twelve one-quadword untextured vertices = 13 quadwords",
          VN(0) == gs_group_qw(12u, GS_REC_NREG_UNTEX) && VN(0) == 13u);
    check("NLOOP 12", gs_nloop_of(w) == 12u);
    check_word("the group's tag, word for word", gs_qw(w, 0u), eegs_tag_flat(12u, k_prim_stp_sb));
    check_word("CONTROL: the retired PACKED tag, same NLOOP: NREG and FLG differ",
               gs_qw(w, 0u) ^ (k_tag_retired_stp_sb | 12u), 0x6400000000000000ULL);
    check_word("REGS: RGBAQ, XYZ2 (untextured)", gs_regs_of(w), 0x51ULL);
    check_word("group PRIM in tag", gs_prim_of(w), k_prim_stp_sb);
    check_word("and the AD write that introduces this group sends the same word as a register",
               pd_prim(0), k_prim_stp_sb);
    check("and the tag names two registers, not four", gs_nreg_of(w) == GS_REC_NREG_UNTEX);

    for (k = 0; k < 12; k++) {
        const uint32_t v = (uint32_t)k + 1u;
        char label[96];

        sprintf(label, "vertex %d's RGBAQ", k);
        check_word(label, gs_vq(w, v, GS_NIB_RGBAQ), k_grid3_rgbaq[k]);
        sprintf(label, "vertex %d's XYZ2", k);
        check_word(label, gs_vq(w, v, GS_NIB_XYZ2), k_grid3_xyz2[k]);
    }
    /* The register this list does not name reads back as all-ones, which no ST ever is: the decoder
     * that asked an untextured group for its texture coordinate has to fail its own check rather than
     * print a zero it mistook for a default. */
    check_word("asking this group for an ST answers NO SUCH REGISTER",
               gs_vq(w, 1u, GS_NIB_ST), ~0ULL);

    /* The stride, censused rather than assumed: 24 data halves, and every one of them is a register
     * this mesh draws with.  The retired sparse shape padded every second half of the vertex's
     * quadwords AND gave an ST this primitive does not name a slot of its own, so the same census
     * there counted 24 zero halves in 48.  A vertex that costs two quadwords cannot hide inside this
     * number, and neither can one that costs a half it never writes. */
    zero_halves = 0;
    for (q = 1u; q < VN(0); q++) {
        if (gs_half(w, q, 0u) == 0u) zero_halves++;
        if (gs_half(w, q, 1u) == 0u) zero_halves++;
    }
    check("24 data halves and no zero among them", VN(0) == 13u && zero_halves == 0u);

    /* The two state bits that are the batch's own decisions: the blend flag comes from the map, and
     * the texture flag is cleared whatever the map says, because a mapped mesh samples nothing.  Both
     * are read from the tag's PRIM field, which is where PRE carries them once per packet; clearing
     * TME is also what chose the two-register list this group carries.
     */
    start();
    m.blended = 0u;
    eegs_grid_strip(&m, s_pts, 3, 3);
    eegs_frame_end();
    if (vgroups() < 1u) return;
    check_word("an unblended map gives PRIM 0x0C", gs_prim_of(VG(0)), k_prim_stp_unblended);
    check("and TME stays cleared", (gs_prim_of(VG(0)) & 16u) == 0u);
    check("the mesh still pays exactly one descriptor for one group", groups_are(1u, 0u));
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

    check("switch group, then the textured group and its descriptor, then the strip's own pair",
          groups_are(2u, 1u));
    if (vgroups() < 2u) return;
    check_word("the textured group kept TME", gs_prim_of(VG(0)), k_prim_tri_stb);
    check_word("the strip's group does not", gs_prim_of(VG(1)), k_prim_stp_sb);
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
    check("the refusal recorded nothing: the frame is the one triangle's group, its descriptor, "
          "and no more",
          groups_are(1u, 0u) && s_cap_drops == 0);
    if (vgroups() >= 1u) {
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
    int na, nb, nc, bad, submitted, batch_groups, list_groups, batch_pkts, list_pkts;
    uint32_t batch_verts, list_verts;

    fill_quads(2);

    /* The batch: one AD switch for the whole run, then ONE vertex group and the descriptor that
    names its primitive -- twelve emitted vertices for four triangles, because `eegs_triangle()` pays
    three corners per primitive and the quad's shared diagonal is named twice, exactly as the walk
    names it. */
    start();
    submitted = eegs_tex_quads(&m, s_qpts, 2);
    eegs_frame_end();
    check("two quads submit four triangles -- this entry counts prims, not the strip's vertices",
          submitted == 4);
    check("nothing was dropped by the capture", s_cap_drops == 0);
    check("nothing was refused by the recorder", gs_rec_unsplittable() == 0);
    check("one switch group, then one descriptor and one vertex group for the whole run",
          groups_are(1u, 1u));
    batch_groups = (int)vgroups();
    batch_pkts = s_cap_i;
    batch_verts = eegs_verts();
    check("twelve vertices, four triangles on the stage's own counters",
          eegs_verts() == 12u && eegs_prims() == 4u);
    check("one plane change paid the run, and no primitive lost its texture",
          eegs_tex_switches() == 1u && eegs_tex_missing() == 0u);
    check("batch=1 slow=0 over=0, and no refusal named",
          eegs_grid_batch() == 1u && eegs_grid_slow() == 0u &&
          eegs_grid_over() == 0u && eegs_grid_why() == TGL_GRID_REFUSE_NONE);
    if (vgroups() < 1u) return;

    check_word("the switch carries the AD trio's own tag", gs_qw(adg(0), 0u), k_tag_ad3);
    check_word("and its REGS nibble is AD", gs_regs_of(adg(0)), 0x0Eu);
    check_word("CLAMP first, the registry entry's word", gs_qw(adg(0), 1u), s_plane.clamp);
    check_word("TEX0 second", gs_qw(adg(0), 2u), s_plane.tex0);
    check_word("TEX1 third", gs_qw(adg(0), 3u), s_plane.tex1);

    w = VG(0);
    check("NLOOP 12, and the group's size is its law: a tag plus twelve three-register vertices",
          gs_nloop_of(w) == 12u && VN(0) == gs_group_qw(12u, GS_REC_NREG_TEX) && VN(0) == 19u);
    check_word("the group's tag carries its PRIM, NLOOP 12",
               gs_qw(w, 0u), eegs_tag(12u, k_prim_tri_stb));
    check_word("CONTROL: the retired PACKED tag for the same textured TRI",
               gs_qw(w, 0u) ^ (k_tag_retired_tri_stb | 12u), 0x7400000000000000ULL);
    check_word("group PRIM in tag", gs_prim_of(w), k_prim_tri_stb);
    check_word("and the descriptor in front of this chain wrote that word",
               pd_prim(0), k_prim_tri_stb);
    check_word("vertex 1 RGBAQ: quad 0's first corner, grey 1, Q = 1.0f",
               gs_vq(w, 1u, GS_NIB_RGBAQ), 0x3F800000FF000001ULL);
    /* The ST register, spelled by hand the way the field table says: 0.25f = 2^-2 is 0x3E800000 at
     * [31:0] and 0.125f = 2^-3 is 0x3E000000 at [63:32].  This word is the thing the grey strip
     * gate could never check -- it writes 0 there whatever the map says. */
    check_word("vertex 1 ST: s = 0.25 low, t = 0.125 high",
               gs_vq(w, 1u, GS_NIB_ST), 0x3E0000003E800000ULL);
    check_word("vertex 1 XYZ2: (0,0) px", gs_vq(w, 1u, GS_NIB_XYZ2), 0x0000000080008000ULL);
    /* 0.75f = 1.5 x 2^-1 = 0x3F400000 and 0.375f = 1.5 x 2^-2 = 0x3EC00000: mantissas that are not
     * bare exponents, so the full 32-bit pattern of both halves is checked, not only the sign. */
    check_word("vertex 2 ST: s = 0.75, t = 0.375",
               gs_vq(w, 2u, GS_NIB_ST), 0x3EC000003F400000ULL);
    check_word("vertex 7 RGBAQ: quad 1's first corner, grey 5",
               gs_vq(w, 7u, GS_NIB_RGBAQ), 0x3F800000FF000005ULL);
    check_word("vertex 7 XYZ2: x = 2 px", gs_vq(w, 7u, GS_NIB_XYZ2), 0x0000000080008020ULL);

    na = decode_stream(A, TRI_MAX);
    check("the batch's stream decodes to its four triangles", na == 4);

    /* The shipped path, same mesh, same submission order: what the clipper hands the stage per
     * textured quad today, adapter and all. */
    start();
    list_quads(2, gs_tex_find(s_tex_key), 0);
    eegs_frame_end();
    check("nothing dropped, nothing refused on the list's stream either",
          s_cap_drops == 0 && gs_rec_unsplittable() == 0);
    list_groups = (int)vgroups();
    list_pkts = s_cap_i;
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
    printf("  2-quad batch: %d vertex group / %d packets, %u vertices for %d triangles;"
           " list: %d vertex group / %d packets, %u vertices\n",
           batch_groups, batch_pkts, batch_verts, na, list_groups, list_pkts, list_verts);

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
    if (vgroups() >= 1u) {
        check_word("flat and unblended gives PRIM 0x13, TME intact",
                   gs_prim_of(VG(0)), k_prim_tex_plain);
        check("and the run is still one switch, one descriptor and one group",
              groups_are(1u, 1u));
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
    check("the refusal recorded nothing: the frame is the one triangle's group, its descriptor, "
          "and no more",
          groups_are(1u, 0u) && s_cap_drops == 0);
    if (vgroups() >= 1u) {
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
    check("it costs no SWITCH group at all: one vertex group, its one descriptor, zero switches",
          groups_are(1u, 0u) && eegs_tex_switches() == 0u);
    if (vgroups() >= 1u) {
        check_word("and the primitives came out flat: 0x4B, TME cleared",
                   gs_prim_of(VG(0)), k_prim_tri_untex);
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

static void test_dual_context_interleaving(void)
{
    static const eegs_vertex_t v[3] = {
        { 10, 10, 0u, 0.0f, 0.0f, 0xFFFFFFFFu },
        { 20, 10, 0u, 0.0f, 0.0f, 0xFFFFFFFFu },
        { 15, 20, 0u, 0.0f, 0.0f, 0xFFFFFFFFu }
    };
    /* Opaque: blended = 0 */
    eegs_tri_state_t st_opaque = { 1, 0, 0, 0 };
    /* Translucent: blended = 1 */
    eegs_tri_state_t st_blend  = { 1, 0, 1, 0 };

    eegs_enable_dual_context(1);
    check("dual context enabled", eegs_is_dual_context() == 1);

    start();
    /* Draw opaque triangle -> targets Context 1 (CTXT=0, bit 9=0) */
    eegs_triangle(v, st_opaque);
    /* Draw blended triangle -> targets Context 2 (CTXT=1, bit 9=1) */
    eegs_triangle(v, st_blend);
    eegs_frame_end();

    /* Two groups must be captured because PRIM changed between Context 1 and Context 2 -- and with
     * it the descriptor, because the context bit lives in the register the GS has to be told. */
    check("dual context interleaving produced 2 vertex groups", groups_are(2u, 0u));
    if (vgroups() >= 2u) {
        uint64_t prim_op = gs_prim_of(VG(0));
        uint64_t prim_bl = gs_prim_of(VG(1));
        check("group 0 PRIM CTXT bit is 0 (Context 1)", (prim_op & (1ULL << 9)) == 0);
        check("group 1 PRIM CTXT bit is 1 (Context 2)", (prim_bl & (1ULL << 9)) != 0);
        check("and the second context is the one its own descriptor writes",
              (pd_prim(1) & (1ULL << 9)) != 0 && (pd_prim(0) & (1ULL << 9)) == 0);
    }

    eegs_enable_dual_context(0);
    check("dual context disabled", eegs_is_dual_context() == 0);
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
    test_dual_context_interleaving();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the raster stage asks for the primitives it was told to\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a recorded triangle is not what the GS would draw\n");
    return 1;
}
