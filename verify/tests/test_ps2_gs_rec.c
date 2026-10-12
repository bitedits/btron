/*
 * verify/tests/test_ps2_gs_rec.c — the vertex recorder's words, against words written by hand
 *
 * Increment 4 of the plan of record (doc/md/PS2.md, "The EE/GS backend").  Same discipline as
 * increments 2 and 3: every expectation below was derived from the GS manual's field table and
 * written out as a literal, none of them was printed by the code under test, and the recorder is
 * compiled into this program so the identical translation unit is what the gate measures and what
 * links into `btron-ps2.elf`.
 *
 * The three things only a hand literal catches are the three the row promises controls for: a
 * vertex coordinate left in pixels instead of scaled to the GS's 12.4 field, `ST`'s two floats in
 * the wrong halves of their quadword, and a `GIFTAG` whose `NLOOP` counts quadwords rather than
 * vertices.  All three produce a stream that is well-formed enough to DMA and that draws
 * something in the wrong place, which is why they cannot be caught downstream.
 *
 * The unit is asserted here too, because it was got wrong once: see gs_group_view.h.  A GIF
 * quadword is 128 bits and a packed GS register is 64 of them, so one register occupies one
 * quadword with its upper half unused -- two uint64_t, value first -- and the capture buffers below
 * are sized in pairs for that reason.  Reading them through the shared view is what keeps this file
 * from re-asserting the half-width layout it used to, which it did green.
 *
 * The buffer is 64 quadwords here rather than the shipped 4,096, because the boundary rule is the
 * part with logic in it and a split at the shipped size needs ten thousand vertices to reach.  The
 * arithmetic the small cap produces is spelled out in each check's comment, so the expectations
 * stay hand-derivable and do not depend on running the code first.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Before the includes, so the recorder's own `#ifndef` yields and this gate drives the split paths
 * with a buffer it can actually fill. */
#define GS_REC_QW_CAP 64u

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_rec.c"
#include "gs_group_view.h"

/* ── The capture sink ───────────────────────────────────────────────────
 * The recorder hands over a pointer to its own buffer and the contract is that the sink takes the
 * words before returning, so the capture copies them -- which is also the assertion that the
 * recorder does not expect anything to still be readable afterwards.  `qwc` counts quadwords and a
 * quadword is two of these words, so the capture is sized in pairs and the copy is twice the count.
 */
#define CAP_MAX 8
static uint64_t s_cap[CAP_MAX][GS_REC_QW_CAP * 2u];
static uint32_t s_cap_n[CAP_MAX];
static int      s_cap_i;
static int      s_over_cap;

static void capture(void *user, const uint64_t *qw, uint32_t qwc)
{
    (void)user;
    if (s_cap_i >= CAP_MAX) {
        s_over_cap++;
        return;
    }
    if (qwc > GS_REC_QW_CAP) {
        s_over_cap++;
        return;
    }
    memcpy(s_cap[s_cap_i], qw, GS_QW_BYTES(qwc));
    s_cap_n[s_cap_i] = qwc;
    s_cap_i++;
}

static void start(void)
{
    memset(s_cap, 0, sizeof(s_cap));
    memset(s_cap_n, 0, sizeof(s_cap_n));
    s_cap_i = 0;
    s_over_cap = 0;
    gs_rec_init(capture, NULL);
}

/* A vertex's four registers, spelled out the way the group's REGS list orders them. */
static const uint64_t k_prim  = 0x25CULL;     /* TRI_STRIP, IIP, TME, ABE, context 1 */
static const uint64_t k_prim3 = 0x25BULL;     /* the same with op = TRI (a list)     */
static const uint64_t k_rgbaq = 0x3F80000078563412ULL;
static const uint64_t k_st    = 0x400000003F800000ULL;   /* s = 1.0f, t = 2.0f */

/* A group's tag, for NLOOP of n.  The arithmetic is the GIFTAG field table itself:
 * NLOOP[14:0], EOP at 15 (= 0x8000), PRE at 46 (= 0x4000_0000_0000), PRIM[57:47] (0x25C shifted
 * 47 bits = 0x012E_0000_0000_0000: its set bits 9,6,4,3,2 land on 56,53,51,50,49), FLG 0, and
 * NREG 4 at 60 (= 0x4000_0000_0000_0000).  The REGS nibble list is the *upper* half of the same
 * quadword, which is why the capture below reads it as word 1 of the group and not word 2. */
static uint64_t tag_of(uint32_t nloop)
{
    return 0x412E400000008000ULL | (uint64_t)nloop;
}

/* The same word for PRIM = 0x25D (TRI_FAN): 0x25D is 0x25C with bit 0 set, and the tag's PRIM
 * field starts at bit 47, so exactly one more bit appears -- at digit 11, where PRE (bit 46)
 * already contributes 4, making it C.  Everything else is as above. */
static uint64_t tag_of_fan(uint32_t nloop)
{
    return 0x412EC00000008000ULL | (uint64_t)nloop;
}

/* And for PRIM = 0x25B (TRI list).  0x25C is 0b1001011100 and 0x25B is 0b1001011011: the list's
 * op replaces bit 2 with bits 1 and 0, so the tag loses bit 49 (2 off digit 12) and gains bits
 * 47 and 48 (8 on digit 11, 1 on digit 12).  Net: digit 12 E -> D, digit 11 4 -> C. */
static uint64_t tag_of_list(uint32_t nloop)
{
    return 0x412DC00000008000ULL | (uint64_t)nloop;
}

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

/* ── The words themselves ─────────────────────────────────────────────── */

/* One 4-vertex TRI_STRIP is exactly how a GL_QUADS four-cornered bar or icon reaches the GS, so
 * this is the shape the XMB emits most often and the one written out here in full.  A group of
 * four vertices is one tag quadword plus four vertices of four quadwords = 17, i.e. 272 bytes. */
static void test_quad_group(void)
{
    uint64_t *w;

    start();
    gs_rec_prim(k_prim);
    gs_rec_color(0x12u, 0x34u, 0x56u, 0x78u);
    gs_rec_texcoord(1.0f, 2.0f);
    check("four vertices of a strip are accepted",
          gs_rec_vertex(0.0f, 0.0f, 0u) == 0 && gs_rec_vertex(10.0f, 20.0f, 0u) == 0 &&
          gs_rec_vertex(320.0f, 240.0f, 0u) == 0 && gs_rec_vertex(-1.0f, 0.5f, 0u) == 0);
    gs_rec_end();

    check("exactly one group, so one DMA start for a quad", gs_rec_sends() == 1u);
    check("and no vertex counted twice", gs_rec_verts() == 4u);
    check("nothing needed a boundary it could not take", gs_rec_unsplittable() == 0u);
    check("one send captured, no sink overflow", s_cap_i == 1 && s_over_cap == 0);
    if (s_cap_i < 1) return;
    w = s_cap[0];

    check("the packet is one tag plus 4 vertices x 4 registers = 17 quadwords",
          gs_group_qw(4u) == 17u && s_cap_n[0] == 17u);
    check_word("GIFTAG: NLOOP counts the four vertices, not the 16 data quadwords",
               gs_qw(w, 0u), tag_of(4u));
    check_word("REGS: PRIM, RGBAQ, ST, XYZ2, in the tag's upper half",
               gs_regs_of(w), 0x5210ULL);

    /* Vertices are written in order and each carries its own state, which is the whole reason
     * this group is 4 quadwords wide instead of 3. */
    check_word("vertex 1 PRIM",     gs_vq(w, 1u, 0u), k_prim);
    check_word("vertex 1 RGBAQ",    gs_vq(w, 1u, 1u), k_rgbaq);
    check_word("vertex 1 ST",       gs_vq(w, 1u, 2u), k_st);
    /* (0,0) px: 0*16 + 2048*16 = 32768 = 0x8000 in both halves, Z 0 above them. */
    check_word("vertex 1 XYZ2: the page origin, in 12.4",
               gs_vq(w, 1u, 3u), 0x0000000080008000ULL);

    /* The unit law as a word.  Under the half-width layout this file used to assert, the pair member
     * at index 3 held RGBAQ -- which the GIF reads as the upper half of PRIM and then routes ST
     * into RGBAQ's place.  A packed 64-bit register's upper half is ignored, so it is zero and not
     * the previous group's neighbour. */
    check_word("the upper half of a packed register's quadword is padding",
               gs_upper_of(w, 1u), 0u);
    check_word("and the same for the last register of the last vertex",
               gs_upper_of(w, 16u), 0u);

    /* (10,20) px: 10*16 = 160 -> 0x80A0, 20*16 = 320 -> 0x8140. */
    check_word("vertex 2 XYZ2: 10 px is 0xA0 sixteenths, not 10",
               gs_vq(w, 2u, 3u), 0x00000000814080A0ULL);
    /* (320,240) px: 5120 -> 0x9400, 3840 -> 0x8F00. */
    check_word("vertex 3 XYZ2", gs_vq(w, 3u, 3u), 0x000000008F009400ULL);
    /* (-1,0.5) px: -16 + 32768 = 32752 = 0x7FF0, 8 + 32768 = 32776 = 0x8008.  X in the low half,
     * Y in the high half -- the pair the manual's XYZ2 table gives. */
    check_word("vertex 4 XYZ2: a negative pixel stays below the page origin",
               gs_vq(w, 4u, 3u), 0x0000000080087FF0ULL);
}

/* The state words are latched, not per-call: a vertex that follows a glColor it did not itself
 * set still carries that colour, because the GS has no other way to know what it should be. */
static void test_latched_state(void)
{
    uint64_t *w;

    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0.0f, 0.0f, 0x11223344u);
    gs_rec_color(0x12u, 0x34u, 0x56u, 0x78u);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    gs_rec_end();

    if (s_cap_i < 1) { check("one group latched", 0); return; }
    w = s_cap[0];
    check("two vertices in one group", s_cap_n[0] == gs_group_qw(2u));
    check_word("GIFTAG NLOOP 2", gs_qw(w, 0u), tag_of(2u));
    /* The default is opaque white with Q = 1.0f, and Q is never anything else here: this layer is
     * affine, and Q is the perspective divisor the GS would divide ST by. */
    check_word("vertex 1 RGBAQ is the default opaque white, Q as its own bits",
               gs_vq(w, 1u, 1u), 0x3F800000FFFFFFFFULL);
    check_word("vertex 1 ST is the default 0,0", gs_vq(w, 1u, 2u), 0x0000000000000000ULL);
    check_word("vertex 1 XYZ2 carries the 32-bit Z the caller passed",
               gs_vq(w, 1u, 3u), 0x1122334480008000ULL);
    check_word("the colour set after vertex 1 reaches vertex 2, not vertex 1",
               gs_vq(w, 2u, 1u), k_rgbaq);
}

/* A primitive change is a group boundary by construction: the GIF tag carries both the REGS list
 * and the PRIM word, so one tag cannot describe two primitive types. */
static void test_prim_closes(void)
{
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    gs_rec_vertex(1.0f, 0.0f, 0u);
    gs_rec_prim(k_prim3);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    gs_rec_end();

    check("the second gs_rec_prim flushed the strip as its own packet", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("strip tag: 2 vertices", gs_qw(s_cap[0], 0u), tag_of(2u));
    check_word("list tag: 1 vertex",    gs_qw(s_cap[1], 0u), tag_of_list(1u));
    check_word("and the new group carries the new PRIM", gs_vq(s_cap[1], 1u, 0u), k_prim3);
    check("two DMA starts, three vertices counted once each",
          gs_rec_sends() == 2u && gs_rec_verts() == 3u);
    check("the one-vertex group is a tag and four registers",
          s_cap_n[1] == gs_group_qw(1u) && s_cap_n[0] == gs_group_qw(2u));
}

/* glEnd with nothing pending must not put a NLOOP-0 tag on the bus, and a group that was opened
 * and never filled is dropped rather than sent. */
static void test_empty(void)
{
    start();
    gs_rec_end();
    check("an end with no primitive sends nothing", s_cap_i == 0 && gs_rec_sends() == 0u);

    gs_rec_prim(k_prim);
    gs_rec_end();
    check("a primitive with no vertices sends nothing", s_cap_i == 0 && gs_rec_sends() == 0u);

    start();
    check("a vertex before any primitive is refused, not written",
          gs_rec_vertex(0.0f, 0.0f, 0u) == -1);
    check("and nothing was sent or counted", s_cap_i == 0 && gs_rec_verts() == 0u);
}

/* A register write between two vertex groups: the same unit question asked of a packet shape the
 * blit path has already proven on the machine.  NLOOP is the number of writes, NREG is 1, the REGS
 * list is the single AD nibble, and each write takes a whole quadword whose upper half is the GP
 * register offset rather than padding -- which is the one place in this layer where the upper half
 * means something. */
static void test_ad_group(void)
{
    /* tex0_1 and clamp_1: the two context-1 registers a texture switch has to reach, and the
     * reason the switch is an AD group at all (GS_NIB_AD is the only nibble that carries a GP
     * offset, so it is the only one that names a register with no packed nibble of its own). */
    const uint64_t data[2] = { 0x0123456789ABCDEFULL, 0xFEDCBA9876543210ULL };
    const uint32_t offs[2] = { GS_GP_TEX0_1, GS_GP_CLAMP_1 };
    uint64_t *w;

    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    check("the switch closes the open group first", gs_rec_ad(data, offs, 2u) == 0);
    check("and the following vertex opens a fresh one", gs_rec_vertex(1.0f, 0.0f, 0u) == 0);
    gs_rec_end();

    check("three sends: the vertex group before it, the AD group, the group after",
          s_cap_i == 3);
    if (s_cap_i < 3) return;
    check("the vertices before it went out on their own",
          s_cap_n[0] == gs_group_qw(1u) && (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 1u);
    /* One tag quadword and then one quadword per register write: 5 words, 80 bytes. */
    check("the AD group is a tag plus two writes = 5 quadwords", s_cap_n[1] == 1u + 2u * 2u);
    /* Derived from the same field table as tag_of(), with its two differences named: NREG is 1
     * (digit 15 = 1 instead of 4), and PRE[46] and PRIM[57:47] are clear because a group that only
     * moves state has no primitive to carry -- so digits 11 and 12, which hold them for a vertex
     * group, are both 0.  What remains is NLOOP 2, EOP at bit 15 and NREG 1 at bit 60. */
    check_word("AD tag: NLOOP 2, NREG 1, PRIM and PRE clear, EOP set",
               gs_qw(s_cap[1], 0u), 0x1000000000008002ULL);
    check_word("AD REGS list: the single nibble 0x0E", gs_regs_of(s_cap[1]), 0x0EULL);
    w = s_cap[1];
    check_word("AD write 1: the register's value", gs_qw(w, 1u), data[0]);
    check_word("AD write 1: and its offset in the quadword's upper half",
               gs_upper_of(w, 1u), GS_GP_TEX0_1);
    check_word("AD write 2: the register's value", gs_qw(w, 2u), data[1]);
    check_word("AD write 2: and its offset", gs_upper_of(w, 2u), GS_GP_CLAMP_1);
    check("the AD group's upper halves are offsets, not padding",
          gs_upper_of(w, 1u) != 0u && gs_upper_of(w, 2u) != 0u);
    check_word("the group after the switch carries the PRIM it opened with",
               gs_vq(s_cap[2], 1u, 0u), k_prim);
    check("so the vertices either side of a switch are counted, not lost",
          gs_rec_verts() == 2u && gs_rec_sends() == 3u);

    /* Refusing rather than truncating is the point of the return value: a half-written TEX0 is a
     * texture whose dimensions and page address disagree, and no later word repairs it. */
    start();
    check("an empty AD group sends nothing and fails nothing", gs_rec_ad(data, offs, 0u) == 0);
    check("and a group larger than the buffer is refused whole",
          gs_rec_ad(data, offs, GS_REC_QW_CAP) == -1 && s_cap_i == 0);
}

/* ── The boundary rule ─────────────────────────────────────────────────
 * 64 quadwords, one for the tag, four per vertex, and GS_REC_HEADROOM = 1 + 3*4 = 13, so an open
 * group reaches 49 quadwords (12 vertices) and the 13th finds s_n + 4 = 53 past CAP-HEADROOM = 51
 * while still fitting the buffer: that is the early split, taken at a boundary the primitive can
 * continue from.
 */
static void test_strip_split(void)
{
    uint64_t *g1, *g2;
    uint32_t k;
    int i, badpad;

    start();
    gs_rec_prim(k_prim);
    for (i = 1; i <= 20; i++) {
        check("strip vertex accepted", gs_rec_vertex((float)i, (float)(2 * i), 0u) == 0);
        /* Each vertex carries its own colour and coordinate, so the re-emitted copies below are
         * comparable as words and not just as positions. */
        gs_rec_color((uint32_t)i, 0u, 0u, 255u);
    }
    gs_rec_end();

    check("20 vertices split into exactly two groups", s_cap_i == 2 && gs_rec_sends() == 2u);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    g2 = s_cap[1];

    check("the first group is 12 vertices plus its tag = 49 quadwords",
          s_cap_n[0] == gs_group_qw(12u) && s_cap_n[0] == 49u);
    check("the second is the 2 re-emitted plus 8 new plus its tag = 41",
          s_cap_n[1] == gs_group_qw(10u) && s_cap_n[1] == 41u);
    check_word("first group's NLOOP is 12 vertices", gs_qw(g1, 0u), tag_of(12u));
    check_word("second group's NLOOP is 10, counting its re-emitted edge",
               gs_qw(g2, 0u), tag_of(10u));
    check("12 + 10 = the 20 handed over plus the one edge the strip had to repeat",
          gs_rec_verts() == 20u && gs_rec_unsplittable() == 0u);

    /* The continuation is the last two vertices of the first group, written again in full --
     * colour and texture coordinate included, because the next triangle is drawn with the state
     * those vertices carried, not with whatever the split happened to latch. */
    check("re-emitted vertex A is the strip's 11th, all four registers",
          gs_vertex_equal(g2, 1u, g1, 11u));
    check("re-emitted vertex B is its 12th", gs_vertex_equal(g2, 2u, g1, 12u));
    check("the third vertex of the new group is the 13th, i.e. genuinely new",
          !gs_vertex_equal(g2, 3u, g1, 12u));
    /* The guard against a test that passes for the wrong reason: each vertex's PRIM word is the
     * same by design, so only the three state words after it can tell the re-emitted pair apart.
     * A continuation that wrote one vertex twice would be two identical quadword sets. */
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));

    /* And the padding halves travel with their registers: a continuation that wrote only the low
     * half of each pair would leave the previous group's words where the GIF reads the upper half. */
    badpad = 0;
    for (k = 1u; k < s_cap_n[1]; k++) {
        if (gs_upper_of(g2, k) != 0u) badpad++;
    }
    check("every data quadword of the second group is padded, none inherited", badpad == 0);
}

/* A fan's continuation is different from a strip's: every triangle shares the apex, so the apex
 * outlives the two-deep window and is what has to be written again, along with the last vertex. */
static void test_fan_split(void)
{
    uint64_t *g1, *g2;
    const uint64_t fan_prim = 0x25DULL;    /* op 5 = TRIFAN, IIP|TME|ABE|ctxt */
    int i;

    check("the fan's PRIM word is the one the field table says",
          gs_pack_prim(GS_OP_TRIFAN, 1u, 1u, 1u, 1u, 0u) == fan_prim);

    start();
    gs_rec_prim(fan_prim);
    for (i = 0; i < 13; i++) {
        check("fan vertex accepted", gs_rec_vertex((float)i, (float)i, 0u) == 0);
    }
    gs_rec_end();

    check("the fan split once", s_cap_i == 2);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    g2 = s_cap[1];
    check_word("fan group 1 NLOOP 12", gs_qw(g1, 0u), tag_of_fan(12u));
    check("fan group 1 is 12 vertices at four quadwords plus the tag",
          s_cap_n[0] == 49u && s_cap_n[0] == gs_group_qw(12u));
    /* Thirteen vertices, twelve of them in the first group: the second is the two re-emitted
     * ones plus the thirteenth, three vertices and 13 quadwords. */
    check("fan group 2 re-emits the apex and the last edge, so 3 vertices and 13 quadwords",
          s_cap_n[1] == 13u && (gs_qw(g2, 0u) & 0x7FFFu) == 3u);
    check_word("and its tag is the fan's own PRIM, not the strip's", gs_qw(g2, 0u), tag_of_fan(3u));
    check("the continued fan starts from the apex, not from vertex 11",
          gs_vertex_equal(g2, 1u, g1, 1u));
    check("and its second vertex is the fan's last, shared edge",
          gs_vertex_equal(g2, 2u, g1, 12u));
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));
}

/* A triangle *list* has no shared vertices, so its only legal boundaries are between whole
 * triangles: cutting after the 11th vertex of a list would leave a triangle with two vertices and
 * steal its third from the next group. */
static void test_list_boundary(void)
{
    uint64_t *g1;

    start();
    gs_rec_prim(k_prim3);
    for (int i = 0; i < 13; i++) {
        check("list vertex accepted", gs_rec_vertex((float)i, 0.0f, 0u) == 0);
    }
    gs_rec_end();

    check("the list split once", s_cap_i == 2);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    check_word("and it waited for the multiple of three: 12 vertices, not 11",
               gs_qw(g1, 0u), tag_of_list(12u));
    check("the tail group holds the thirteenth vertex alone", s_cap_n[1] == gs_group_qw(1u));
    check("and a list re-emits nothing: its tag says one vertex",
          (gs_qw(s_cap[1], 0u) & 0x7FFFu) == 1u);
}

/* An op this recorder does not know cannot be continued, so the boundary rule has no answer: the
 * vertex is refused, the counter says so, and the group it left behind is still a well-formed
 * packet.  This is the one path where a vertex is lost, which is why it is counted rather than
 * merely returned -- the shipped 4,096-quadword buffer makes it unreachable for any real mesh, and
 * if it ever stops being unreachable the counter is the row that shows it. */
static void test_unsplittable(void)
{
    const uint64_t bad_prim = 0x25FULL;    /* op 7: not one of the GS's seven */
    int refused = 0, i;

    check("the undefined op is what the field table says",
          gs_pack_prim(7u, 1u, 1u, 1u, 1u, 0u) == bad_prim);

    start();
    gs_rec_prim(bad_prim);
    for (i = 0; i < 16; i++) {
        if (gs_rec_vertex((float)i, 0.0f, 0u) != 0) refused++;
    }
    gs_rec_end();

    check("15 fit and only the 16th was refused", refused == 1 && gs_rec_verts() == 15u);
    check("the refusal is counted, not silent", gs_rec_unsplittable() == 1u);
    check("the 15 that were taken are still sent as one group", s_cap_i == 1);
    if (s_cap_i < 1) return;
    /* PRIM 0x25F = 0b1001011111, so op 7 sets the field's bits 0,1,2 and the tag gains bits 47,
     * 48, 49 over the strip's: digit 12 is F (1+2+4+8) and digit 11 is C (8 for bit 47, 4 for
     * PRE).  Everything above it is the same as tag_of()'s. */
    check_word("with its own NLOOP intact", gs_qw(s_cap[0], 0u), 0x412FC00000008000ULL | 15u);
    check("15 vertices plus a tag is 61 quadwords, inside the 64",
          s_cap_n[0] == 61u && s_cap_n[0] == gs_group_qw(15u));
}

/* POINT and a LINE list are the two remaining boundary shapes.  Both PRIM words here are the
 * strip's own flag set -- IIP|TME|ABE|context 1, which is 8+16+64+512 = 600 = 0x258 for op 0 --
 * with only the three op bits changed, so POINT is 0x258 and LINE is 0x259. */
static void test_point_and_line_boundaries(void)
{
    const uint64_t point_prim = gs_pack_prim(GS_OP_POINT, 1u, 1u, 1u, 1u, 0u);
    const uint64_t line_prim  = gs_pack_prim(GS_OP_LINE,  1u, 1u, 1u, 1u, 0u);
    int bad = 0, i;

    check("point PRIM: op 0 under the strip's flags", point_prim == 0x258ULL);
    check("line PRIM: op 1 under the same flags",     line_prim == 0x259ULL);

    /* Every vertex is its own primitive, so a boundary is always available and nothing is
     * re-emitted: 64 quadwords hold 12 vertices plus a tag, and 30 points cost starts of 12, 12
     * and 6.  That is the arithmetic behind "capacity is a latency knob" -- the same 30 vertices at
     * the shipped 4,096 quadwords are one start. */
    start();
    gs_rec_prim(point_prim);
    for (i = 0; i < 30; i++) {
        if (gs_rec_vertex((float)i, 0.0f, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("30 points lose nothing",
          bad == 0 && gs_rec_verts() == 30u && gs_rec_unsplittable() == 0u);
    check("and they cost three DMA starts at this capacity", s_cap_i == 3 && gs_rec_sends() == 3u);
    if (s_cap_i < 3) return;
    check("12 + 12 + 6 = 30, with no vertex repeated",
          (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 12u && (gs_qw(s_cap[1], 0u) & 0x7FFFu) == 12u &&
          (gs_qw(s_cap[2], 0u) & 0x7FFFu) == 6u);
    check("each group is its vertex count times four registers plus its tag",
          s_cap_n[0] == 49u && s_cap_n[1] == 49u && s_cap_n[2] == 25u);

    /* A LINE list cuts only between segments.  Without the headroom rule the buffer would fill
     * at 15 vertices (61 quadwords) and the 16th would find no boundary at an odd count; the
     * early split at the 13th lands on the even one instead, so 14 endpoints make two whole-
     * segment groups of 12 and 2 and refuse nothing. */
    start();
    gs_rec_prim(line_prim);
    bad = 0;
    for (i = 0; i < 14; i++) {
        if (gs_rec_vertex((float)i, (float)i, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("14 endpoints of a LINE list are all taken", bad == 0 && gs_rec_verts() == 14u);
    check("and split into two groups", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check("both NLOOPs are a whole number of segments",
          (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 12u && (gs_qw(s_cap[1], 0u) & 0x7FFFu) == 2u);
    check("and no vertex is re-emitted, because a segment shares none",
          s_cap_n[1] == gs_group_qw(2u));
}

/* The counters are the frame's latency budget, so they have to survive a re-init and report the
 * next frame rather than the last one. */
static void test_counters_reset(void)
{
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    gs_rec_end();
    check("one frame, one start", gs_rec_sends() == 1u && gs_rec_verts() == 1u);

    start();
    check("gs_rec_init zeroes the counters a bench reads as per-frame",
          gs_rec_sends() == 0u && gs_rec_verts() == 0u && gs_rec_unsplittable() == 0u);
    check("and a fresh recorder sends nothing until told to", s_cap_i == 0);

    gs_rec_prim(k_prim);
    gs_rec_vertex(0.0f, 0.0f, 0u);
    gs_rec_end();
    check("the second frame counts on its own", gs_rec_sends() == 1u && gs_rec_verts() == 1u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS vertex recorder vs hand-built GIF groups (cap=%u qw)\n", GS_REC_QW_CAP);
    printf("==========================================================\n");

    test_quad_group();
    test_latched_state();
    test_prim_closes();
    test_empty();
    test_ad_group();
    test_strip_split();
    test_fan_split();
    test_list_boundary();
    test_unsplittable();
    test_point_and_line_boundaries();
    test_counters_reset();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - every recorded group matches its hand-written words\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a recorded vertex word or a group boundary is not what the GS decodes\n");
    return 1;
}
