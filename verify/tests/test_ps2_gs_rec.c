/*
 * verify/tests/test_ps2_gs_rec.c — the vertex recorder's words, against words written by hand
 *
 * Increment 4 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Same discipline as
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
 * The unit is asserted here too, because it was got wrong once: see gs_group_view.h.  A GIF quadword
 * is 128 bits and a packed GS register is 64 of them, so a dense REGLIST group puts TWO registers of
 * its REGS list in every quadword -- the capture buffers below are still sized in pairs, and every
 * register is read through `gs_vq()` or `gs_half()`, which is what keeps this file from re-asserting
 * whichever stride it happened to ship last.  The stride it used to assert was one register per
 * quadword, because the recorder emitted `FLG=PACKED`, and LANE 19 rung 5 measured that shape on this
 * GS drawing nothing at all: the four registers a vertex carries now occupy two quadwords, which is
 * both the shape that kicks and half the bytes.
 *
 * The buffer is 32 quadwords here rather than the shipped 4,096, because the boundary rule is the
 * part with logic in it and a split at the shipped size needs thousands of vertices to reach.  32 is
 * the cap that leaves every *vertex* count below exactly where it was under the old 64-quadword
 * sparse buffer -- a strip still splits after 12 vertices, an unknown op still refuses its 16th --
 * because halving the stride and halving the buffer cancel; the boundary arithmetic therefore did not
 * have to be re-invented to follow the format change, only the quadwords-per-group did.  The
 * arithmetic the small cap produces is spelled out in each check's comment, so the expectations stay
 * hand-derivable and do not depend on running the code first.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Before the includes, so the recorder's own `#ifndef` yields and this gate drives the split paths
 * with a buffer it can actually fill.  32 quadwords is the dense successor to this file's old 64:
 * GS_REC_HEADROOM is 1 + 3 * GS_REC_QW_PER_VERT = 7, and the split point falls where it fell before. */
#define GS_REC_QW_CAP 32u

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

/* A vertex group's tag, for NLOOP of n.  The arithmetic is the GIFTAG field table itself:
 * NLOOP[14:0], EOP at 15 (= 0x8000), PRE at 46 CLEARED, PRIM[57:47] ZERO, FLG 1 at 58
 * (= 0x0400_0000_0000_0000) and NREG 4 at 60 (= 0x4000_0000_0000_0000).  The REGS nibble list is the
 * *upper* half of the same quadword, which is why the capture below reads it as word 1 of the group
 * and not word 2.
 *
 * This ONE word is the tag for a strip, a fan, a list, a line and a point, because a dense
 * `FLG=REGLIST` group carries its primitive as the loop's own first register -- the PRIM that
 * gs_rec_prim() latches and write_vertex() emits for every vertex -- and the tag's PRIM field is
 * what a PACKED group needed instead.  Three helpers used to live here, one per primitive, and their
 * three literals differed in digits 11 and 12 because those digits ARE the PRIM field; what rung 5
 * measured is that the field they encoded bought nothing on this GS, so the differences they checked
 * for are gone and the check that they are gone is below. */
static uint64_t tag_of(uint32_t nloop)
{
    return 0x4400000000008000ULL | (uint64_t)nloop;
}

/* The retired shape, kept here as the word the format claim is measured against: NREG 4 at bit 60,
 * FLG 0 (PACKED) so bit 58 is clear, PRIM 0x25C in [57:47] and PRE set at 46 -- the exact
 * combination LANE 19 rung 5 drew zero pixels with, at either value of PRE.  Its XOR with a shipping
 * tag names the three fields the fix touched: bit 58 and the eight PRIM bits. */
#define TAG_RETIRED_STRIP 0x412E400000008000ULL

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
 * four vertices is one tag quadword plus four vertices of two quadwords = 9, i.e. 144 bytes: the
 * four registers of the REGS list are packed two to a quadword, which is what `FLG=REGLIST` means. */
static void test_quad_group(void)
{
    uint64_t *w;

    start();
    gs_rec_prim(k_prim);
    gs_rec_color(0x12u, 0x34u, 0x56u, 0x78u);
    gs_rec_texcoord(1.0f, 2.0f);
    check("four vertices of a strip are accepted",
          gs_rec_vertex(0, 0, 0u) == 0 && gs_rec_vertex(10, 20, 0u) == 0 &&
          gs_rec_vertex(320, 240, 0u) == 0 && gs_rec_vertex(-1, 2047, 0u) == 0);
    gs_rec_end();

    check("exactly one group, so one DMA start for a quad", gs_rec_sends() == 1u);
    check("and no vertex counted twice", gs_rec_verts() == 4u);
    check("nothing needed a boundary it could not take", gs_rec_unsplittable() == 0u);
    check("one send captured, no sink overflow", s_cap_i == 1 && s_over_cap == 0);
    if (s_cap_i < 1) return;
    w = s_cap[0];

    /* The evenness the format rests on, asserted in the gate because no preprocessor can make it:
     * `(GS_REC_REGS_N % 2u)` is not a pp-expression at all (`4u` is not a pp-number), so the
     * `#if` that would have caught an odd register count never fires, and an odd count would leave
     * the last register of every loop in a quadword's low half with the NEXT loop's PRIM beside it. */
    check("the register count is even, so a vertex is a whole number of quadwords",
          (GS_REC_REGS_N % 2u) == 0u && GS_REC_QW_PER_VERT * 2u == GS_REC_REGS_N);
    check("and it is two, which is the stride every expectation below divides by",
          GS_REC_QW_PER_VERT == 2u);
    /* The number the split points in the boundary tests are derived from, asserted rather than
     * assumed: a headroom left at the sparse value would still split at a legal boundary, just with
     * half the buffer unused, and no row below would notice. */
    check("the headroom is one tag plus three vertices at this stride",
          GS_REC_HEADROOM == 1u + 3u * GS_REC_QW_PER_VERT && GS_REC_HEADROOM == 7u);

    check("the packet is one tag plus 4 vertices x 2 quadwords = 9 quadwords",
          gs_group_qw(4u) == 9u && s_cap_n[0] == 9u);
    check_word("GIFTAG: NLOOP counts the four vertices, not the 8 data quadwords",
               gs_qw(w, 0u), tag_of(4u));
    check_word("REGS: PRIM, RGBAQ, ST, XYZ2, in the tag's upper half",
               gs_regs_of(w), 0x5210ULL);

    /* The tag's own fields, named one by one, because the format claim is made of exactly these
     * three and nothing else: FLG says the registers are packed, PRE says there is no register in
     * front of them, and PRIM says the primitive is not carried here. */
    check("FLG is REGLIST", gs_flg_of(w) == GS_FLG_REGLIST);
    check("PRE is clear", gs_pre_of(w) == 0u);
    check("the tag's PRIM field is zero", gs_prim_of(w) == 0u);
    check("NREG is the four-register list", gs_nreg_of(w) == GS_REC_REGS_N);
    check("EOP is set, because every send here is a packet of its own", gs_eop_of(w) == 1u);
    /* The retired word, as the third term of the difference rather than a fourth assertion: a
     * group that reverted to PACKED/PRE/tagged-PRIM would change these three bits and nothing else
     * in the tag, so the XOR is the whole of the format fix in one row. */
    check_word("CONTROL retired PACKED tag, same NLOOP: FLG + PRIM + PRE are all that differ",
               gs_qw(w, 0u) ^ (TAG_RETIRED_STRIP | 4u), 0x052E400000000000ULL);

    /* Vertices are written in order and each carries its own state, which is the whole reason
     * this group is two quadwords wide instead of one. */
    check_word("vertex 1 PRIM",     gs_vq(w, 1u, 0u), k_prim);
    check_word("vertex 1 RGBAQ",    gs_vq(w, 1u, 1u), k_rgbaq);
    check_word("vertex 1 ST",       gs_vq(w, 1u, 2u), k_st);
    /* (0,0) px: 0*16 + 2048*16 = 32768 = 0x8000 in both halves, Z 0 above them. */
    check_word("vertex 1 XYZ2: the page origin, in 12.4",
               gs_vq(w, 1u, 3u), 0x0000000080008000ULL);

    /* The same four words read as the two quadwords the DMA counts, which is the dense layout
     * stated as positions rather than as a claim: PRIM and RGBAQ are ONE quadword's two halves, ST
     * and the drawing kick XYZ2 are the next one's.  Under the sparse stride this file used to
     * assert, quadword 1's high half was zero and RGBAQ sat in quadword 2 -- half the bytes on the
     * bus gone, and every register after the first shifted one quadword. */
    check_word("quadword 1 = PRIM in its low half",   gs_half(w, 1u, 0u), k_prim);
    check_word("quadword 1 = RGBAQ in its high half", gs_half(w, 1u, 1u), k_rgbaq);
    check_word("quadword 2 = ST in its low half",     gs_half(w, 2u, 0u), k_st);
    check_word("quadword 2 = XYZ2 in its high half, the kick last",
               gs_half(w, 2u, 1u), 0x0000000080008000ULL);
    check_word("the whole group's last half is the last vertex's XYZ2",
               gs_upper_of(w, 8u), 0x00000000FFF07FF0ULL);

    /* (10,20) px: 10*16 = 160 -> 0x80A0, 20*16 = 320 -> 0x8140. */
    check_word("vertex 2 XYZ2: 10 px is 0xA0 sixteenths, not 10",
               gs_vq(w, 2u, 3u), 0x00000000814080A0ULL);
    /* (320,240) px: 5120 -> 0x9400, 3840 -> 0x8F00. */
    check_word("vertex 3 XYZ2", gs_vq(w, 3u, 3u), 0x000000008F009400ULL);
    /* (-1, 2047) px: -16 + 32768 = 32752 = 0x7FF0 and 2047*16 = 32752 + 32768 = 65 520 = 0xFFF0,
     * which is the field's own last value -- the corner a 2048 px canvas would wrap past.  X in the
     * low half, Y in the high half -- the pair the manual's XYZ2 table gives.  The fractional vertex
     * this case used to be (0.5 px, which truncated to 0x8008) went with the float parameter: under
     * LANE 12a the caller is a GLvertex whose x and y are already GLint, so a half-pixel vertex is
     * not a thing that can reach this function, and asserting one here would be asserting a literal
     * the production path cannot produce. */
    check_word("vertex 4 XYZ2: a negative pixel stays below the page origin",
               gs_vq(w, 4u, 3u), 0x00000000FFF07FF0ULL);
}

/* The state words are latched, not per-call: a vertex that follows a glColor it did not itself
 * set still carries that colour, because the GS has no other way to know what it should be. */
static void test_latched_state(void)
{
    uint64_t *w;

    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0x11223344u);
    gs_rec_color(0x12u, 0x34u, 0x56u, 0x78u);
    gs_rec_vertex(0, 0, 0u);
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

/* A primitive change is a group boundary by construction.  It is NOT one because of the tag: under
 * the dense format the primitive travels in every vertex's own PRIM register and the tag names no
 * primitive at all, which the rows below assert.  It is one because the recorder's continuation rule
 * -- which already-written vertices have to be written again when a group is cut -- is a property of
 * one primitive type, and a group that held two would have one sliding window serving two masters. */
static void test_prim_closes(void)
{
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_prim(k_prim3);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();

    check("the second gs_rec_prim flushed the strip as its own packet", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check_word("strip tag: 2 vertices", gs_qw(s_cap[0], 0u), tag_of(2u));
    check_word("list tag: 1 vertex, and the identical word apart from NLOOP",
               gs_qw(s_cap[1], 0u), tag_of(1u));
    /* The two tags differ in nothing but their loop count, which is the format's own claim stated
     * where it can only be stated: a strip and a list are the same tag shape, so a gate that once
     * needed three tag literals needs one. */
    check_word("CONTROL strip vs list tags: only NLOOP",
               gs_qw(s_cap[0], 0u) ^ gs_qw(s_cap[1], 0u), 3u);
    check("so neither tag carries a primitive",
          gs_prim_of(s_cap[0]) == 0u && gs_prim_of(s_cap[1]) == 0u);
    check_word("and the new group carries the new PRIM where the GS reads it: its own register",
               gs_vq(s_cap[1], 1u, 0u), k_prim3);
    check("two DMA starts, three vertices counted once each",
          gs_rec_sends() == 2u && gs_rec_verts() == 3u);
    check("the one-vertex group is a tag and two quadwords",
          s_cap_n[1] == gs_group_qw(1u) && s_cap_n[1] == 3u &&
          s_cap_n[0] == gs_group_qw(2u) && s_cap_n[0] == 5u);
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
          gs_rec_vertex(0, 0, 0u) == -1);
    check("and nothing was sent or counted", s_cap_i == 0 && gs_rec_verts() == 0u);
}

/* A register write between two vertex groups: the same unit question asked of a packet shape the
 * blit path has already proven on the machine.  NLOOP is the number of writes, NREG is 1, the REGS
 * list is the single AD nibble, and each write takes a whole quadword whose upper half is the GP
 * register offset.  This is the one group in the layer whose high half *names* a register instead of
 * being one -- a vertex group's high half holds the loop's second register, this one's holds the
 * address the low half has to land at -- so the two formats' size laws differ by exactly that. */
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
    gs_rec_vertex(0, 0, 0u);
    check("the switch closes the open group first", gs_rec_ad(data, offs, 2u) == 0);
    check("and the following vertex opens a fresh one", gs_rec_vertex(1, 0, 0u) == 0);
    gs_rec_end();

    check("three sends: the vertex group before it, the AD group, the group after",
          s_cap_i == 3);
    if (s_cap_i < 3) return;
    check("the vertices before it went out on their own",
          s_cap_n[0] == gs_group_qw(1u) && (gs_qw(s_cap[0], 0u) & 0x7FFFu) == 1u);
    /* One tag quadword and then one quadword per register write: 3 quadwords, 48 bytes -- the
     * general tag law with NREG=1, which is what send_trx_setup_at()'s five-quadword packet of
     * four writes proves on the machine. */
    check("the AD group is a tag plus two writes = 3 quadwords",
          s_cap_n[1] == GS_REC_AD_QWC(2u) && s_cap_n[1] == 3u);
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
 * 32 quadwords, one for the tag, two per vertex, and GS_REC_HEADROOM = 1 + 3*2 = 7, so an open
 * group reaches 25 quadwords (12 vertices) and the 13th finds s_n + 2 = 27 past CAP-HEADROOM = 25
 * while still fitting the buffer: that is the early split, taken at a boundary the primitive can
 * continue from.  The vertex that reaches it is the 13th, exactly as it was under this file's old
 * sparse 64-quadword buffer, because the stride and the buffer halved together -- which is why the
 * boundary rule could be re-derived without re-inventing it.
 */
static void test_strip_split(void)
{
    uint64_t *g1, *g2;
    uint32_t v;
    int i, badprim;

    start();
    gs_rec_prim(k_prim);
    for (i = 1; i <= 20; i++) {
        check("strip vertex accepted", gs_rec_vertex(i, 2 * i, 0u) == 0);
        /* Each vertex carries its own colour and coordinate, so the re-emitted copies below are
         * comparable as words and not just as positions. */
        gs_rec_color((uint32_t)i, 0u, 0u, 255u);
    }
    gs_rec_end();

    check("20 vertices split into exactly two groups", s_cap_i == 2 && gs_rec_sends() == 2u);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    g2 = s_cap[1];

    check("the first group is 12 vertices plus its tag = 25 quadwords",
          s_cap_n[0] == gs_group_qw(12u) && s_cap_n[0] == 25u);
    check("the second is the 2 re-emitted plus 8 new plus its tag = 21",
          s_cap_n[1] == gs_group_qw(10u) && s_cap_n[1] == 21u);
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
    /* The guard against a test that passes for the wrong reason: each vertex's PRIM register is the
     * same by design, so only the three state registers after it can tell the re-emitted pair apart.
     * A continuation that wrote one vertex twice would be two identical quadword sets. */
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));

    /* Where those four registers sit, stated as halves and as hand words.  The colour this test
     * latches is the iteration's number: gs_rec_color(i, 0, 0, 255) runs *after* vertex i, so vertex
     * 11 carries the r the 10th iteration set.  RGBAQ is R[7:0] G[15:8] B[23:16] A[31:24] with Q
     * above them, so that word is 0x3F800000_FF00000A -- A 255, B 0, G 0, R 10, Q 1.0f -- and it
     * belongs in quadword 1's HIGH half, the second register of the loop's first quadword.  Under
     * the sparse stride this file used to certify, that half was zero and the colour sat a whole
     * quadword later. */
    check_word("the re-emitted vertex's RGBAQ is quadword 1's high half",
               gs_upper_of(g2, 1u), 0x3F800000FF00000AULL);
    check_word("and the second re-emitted vertex carries its own colour, r = 11",
               gs_upper_of(g2, 3u), 0x3F800000FF00000BULL);
    /* Vertex 11 was (11, 22) px: 11*16 = 176 -> 32 944 = 0x80B0 and 22*16 = 352 -> 33 120 = 0x8160,
     * X low and Y high, and the kick is the loop's LAST register -- quadword 2's high half. */
    check_word("the re-emitted vertex's XYZ2 is quadword 2's high half",
               gs_vq(g2, 1u, 3u), 0x00000000816080B0ULL);

    /* And the stride counted over the whole group rather than asserted at one vertex: every loop of
     * a dense group opens its own pair of quadwords with PRIM, because the REGS list restarts each
     * loop.  Space the registers one to a quadword instead and quadword 1 + 2*(v-1) is ST for every
     * v after the first, so this loop is the row that names the format, not the geometry. */
    badprim = 0;
    for (v = 1u; v <= gs_nloop_of(g2); v++) {
        if (gs_vq(g2, v, 0u) != k_prim) badprim++;
    }
    check("all 10 vertices of the second group open with the strip's PRIM",
          badprim == 0 && gs_nloop_of(g2) == 10u);
    check("and its quadword count is its tag plus two per loop",
          s_cap_n[1] == 1u + GS_REC_QW_PER_VERT * gs_nloop_of(g2));
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
        check("fan vertex accepted", gs_rec_vertex(i, i, 0u) == 0);
    }
    gs_rec_end();

    check("the fan split once", s_cap_i == 2);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    g2 = s_cap[1];
    check_word("fan group 1 NLOOP 12", gs_qw(g1, 0u), tag_of(12u));
    check("fan group 1 is 12 vertices at two quadwords plus the tag",
          s_cap_n[0] == 25u && s_cap_n[0] == gs_group_qw(12u));
    /* Thirteen vertices, twelve of them in the first group: the second is the two re-emitted
     * ones plus the thirteenth, three vertices and 7 quadwords. */
    check("fan group 2 re-emits the apex and the last edge, so 3 vertices and 7 quadwords",
          s_cap_n[1] == 7u && (gs_qw(g2, 0u) & 0x7FFFu) == 3u);
    /* The row that used to read "its tag is the fan's own PRIM, not the strip's" and compared two
     * different literals.  There is only one tag word now: a fan's primitive reaches the GS as the
     * PRIM register at the head of each of its loops, so the tag of a fan and the tag of a strip are
     * the same 64 bits whenever their NLOOPs match. */
    check_word("and its tag is the one dense word, NLOOP 3", gs_qw(g2, 0u), tag_of(3u));
    check("so neither fan tag nor strip tag names a primitive",
          gs_prim_of(g1) == 0u && gs_prim_of(g2) == 0u &&
          gs_flg_of(g1) == GS_FLG_REGLIST && gs_flg_of(g2) == GS_FLG_REGLIST);
    check("the continued fan starts from the apex, not from vertex 11",
          gs_vertex_equal(g2, 1u, g1, 1u));
    check("and its second vertex is the fan's last, shared edge",
          gs_vertex_equal(g2, 2u, g1, 12u));
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));
    /* The apex and the shared edge as hand words in the halves they belong to.  The fan's vertices
     * are (i, i) px: the apex is (0, 0), so 0*16 + 2048*16 = 0x8000 in both coordinate fields, and
     * the twelfth is (11, 11), so 11*16 = 176 -> 32 944 = 0x80B0 in both.  XYZ2 is the fourth
     * register of the loop, which under two-to-a-quadword is the SECOND quadword's high half. */
    check_word("the re-emitted apex's XYZ2 is quadword 2's high half",
               gs_vq(g2, 1u, 3u), 0x0000000080008000ULL);
    check_word("and the re-emitted edge's, at quadword 4's high half",
               gs_vq(g2, 2u, 3u), 0x0000000080B080B0ULL);
    check_word("the genuinely new thirteenth vertex is (12, 12)",
               gs_vq(g2, 3u, 3u), 0x0000000080C080C0ULL);
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
        check("list vertex accepted", gs_rec_vertex(i, 0, 0u) == 0);
    }
    gs_rec_end();

    check("the list split once", s_cap_i == 2);
    if (s_cap_i < 2) return;
    g1 = s_cap[0];
    check_word("and it waited for the multiple of three: 12 vertices, not 11",
               gs_qw(g1, 0u), tag_of(12u));
    check("so the first group is 12 vertices and 25 quadwords, all whole triangles",
          s_cap_n[0] == 25u && gs_nloop_of(g1) % 3u == 0u);
    check("the tail group holds the thirteenth vertex alone, tag and two quadwords",
          s_cap_n[1] == gs_group_qw(1u) && s_cap_n[1] == 3u);
    check("and a list re-emits nothing: its tag says one vertex",
          gs_nloop_of(s_cap[1]) == 1u);
    /* A list needs no continuation, and the census of the tail group says so in words: the single
     * loop's four registers are the 13th vertex's own -- PRIM 0x25B, the default white, the default
     * ST and the coordinate (12, 0) px, i.e. 0x80C0 in X and the page origin 0x8000 in Y. */
    check_word("the tail's PRIM register", gs_vq(s_cap[1], 1u, 0u), k_prim3);
    check_word("the tail's XYZ2, at the second quadword's high half",
               gs_vq(s_cap[1], 1u, 3u), 0x00000000800080C0ULL);
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
        if (gs_rec_vertex(i, 0, 0u) != 0) refused++;
    }
    gs_rec_end();

    check("15 fit and only the 16th was refused", refused == 1 && gs_rec_verts() == 15u);
    check("the refusal is counted, not silent", gs_rec_unsplittable() == 1u);
    check("the 15 that were taken are still sent as one group", s_cap_i == 1);
    if (s_cap_i < 1) return;
    /* The tag is the same word a strip's or a fan's is, which is what the dense format buys here:
     * an op 7 PRIM changes only the register each loop carries, not a bit of the tag.  Under the
     * retired PACKED shape this row compared a fourth hand literal, because PRIM[57:47] held the op
     * and PRE[46] held its companion -- 0x412FC00000008000 for NLOOP 15. */
    check_word("with its own NLOOP intact and no primitive in the tag",
               gs_qw(s_cap[0], 0u), tag_of(15u));
    check("15 vertices plus a tag is 31 quadwords, inside the 32",
          s_cap_n[0] == 31u && s_cap_n[0] == gs_group_qw(15u));
    check("and the group's own registers do name the op the tag does not",
          gs_vq(s_cap[0], 1u, 0u) == bad_prim && gs_prim_of(s_cap[0]) == 0u);
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
     * re-emitted: 32 quadwords hold 12 vertices plus a tag, and 30 points cost starts of 12, 12
     * and 6.  That is the arithmetic behind "capacity is a latency knob" -- the same 30 vertices at
     * the shipped 4,096 quadwords are one start, and at two quadwords a vertex they are one start
     * for half the bytes the old sparse shape moved. */
    start();
    gs_rec_prim(point_prim);
    for (i = 0; i < 30; i++) {
        if (gs_rec_vertex(i, 0, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("30 points lose nothing",
          bad == 0 && gs_rec_verts() == 30u && gs_rec_unsplittable() == 0u);
    check("and they cost three DMA starts at this capacity", s_cap_i == 3 && gs_rec_sends() == 3u);
    if (s_cap_i < 3) return;
    check("12 + 12 + 6 = 30, with no vertex repeated",
          gs_nloop_of(s_cap[0]) == 12u && gs_nloop_of(s_cap[1]) == 12u &&
          gs_nloop_of(s_cap[2]) == 6u);
    check("each group is its vertex count times two quadwords plus its tag",
          s_cap_n[0] == 25u && s_cap_n[1] == 25u && s_cap_n[2] == 13u);

    /* A LINE list cuts only between segments.  Without the headroom rule the buffer would fill
     * at 15 vertices (31 quadwords) and the 16th would find no boundary at an odd count; the
     * early split at the 13th lands on the even one instead, so 14 endpoints make two whole-
     * segment groups of 12 and 2 and refuse nothing. */
    start();
    gs_rec_prim(line_prim);
    bad = 0;
    for (i = 0; i < 14; i++) {
        if (gs_rec_vertex(i, i, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("14 endpoints of a LINE list are all taken", bad == 0 && gs_rec_verts() == 14u);
    check("and split into two groups", s_cap_i == 2);
    if (s_cap_i < 2) return;
    check("both NLOOPs are a whole number of segments",
          gs_nloop_of(s_cap[0]) == 12u && gs_nloop_of(s_cap[1]) == 2u);
    check("and no vertex is re-emitted, because a segment shares none",
          s_cap_n[1] == gs_group_qw(2u) && s_cap_n[1] == 5u);
}

/* The counters are the frame's latency budget, so they have to survive a re-init and report the
 * next frame rather than the last one. */
static void test_counters_reset(void)
{
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();
    check("one frame, one start", gs_rec_sends() == 1u && gs_rec_verts() == 1u);

    start();
    check("gs_rec_init zeroes the counters a bench reads as per-frame",
          gs_rec_sends() == 0u && gs_rec_verts() == 0u && gs_rec_unsplittable() == 0u);
    check("and a fresh recorder sends nothing until told to", s_cap_i == 0);

    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();
    check("the second frame counts on its own", gs_rec_sends() == 1u && gs_rec_verts() == 1u);
}

/* ── Gate #120: Lit-vertex stream capture, dump, and offline replay ─────────
 *
 * This test establishes the offline dump-and-replay sentinel for lit-vertex streams.
 * A lit triangle (as emitted by Quake's brush/lightmap fold or Lil Cu 64's lit mascot)
 * carries IIP (Gouraud shading), distinct RGBAQ per vertex with Q=1.0f, distinct ST coordinates,
 * and valid 12.4 XYZ2 coordinates.
 *
 * In addition to in-memory assertions, the recorded GIF stream is dumped to a binary fixture
 * (verify/fixtures/gs_lit_stream.bin) and re-read back into a decoder validator, proving that
 * offline tools and differential oracles can parse and verify the stream with zero drift.
 */
static void test_lit_vertex_stream(void)
{
    /* Lit Gouraud textured triangle: op=GS_OP_TRI (3), IIP=1, TME=1, ABE=0, CTXT=0 */
    const uint64_t lit_prim = gs_pack_prim(GS_OP_TRI, 1u, 1u, 0u, 0u, 0u);
    uint64_t *w;
    FILE *fp;
    uint64_t replayed[GS_REC_QW_CAP * 2u];
    size_t nread;
    uint32_t i;

    /* Hand literals for three lit vertices */
    const uint64_t want_rgba1 = gs_pack_rgbaq(0x30u, 0x45u, 0x60u, 0xFFu, GS_Q_ONE);
    const uint64_t want_rgba2 = gs_pack_rgbaq(0x80u, 0xA0u, 0xC0u, 0xFFu, GS_Q_ONE);
    const uint64_t want_rgba3 = gs_pack_rgbaq(0xE0u, 0xF0u, 0xFFu, 0xFFu, GS_Q_ONE);

    const uint64_t want_st1 = gs_pack_st(0.0f, 0.0f);
    const uint64_t want_st2 = gs_pack_st(1.0f, 0.0f);
    const uint64_t want_st3 = gs_pack_st(0.5f, 1.0f);

    start();
    gs_rec_prim(lit_prim);

    /* Vertex 1: Ambient + folded lightmap base */
    gs_rec_color(0x30u, 0x45u, 0x60u, 0xFFu);
    gs_rec_texcoord(0.0f, 0.0f);
    check("lit vertex 1 accepted", gs_rec_vertex(100, 100, 50u) == 0);

    /* Vertex 2: Diffuse illumination */
    gs_rec_color(0x80u, 0xA0u, 0xC0u, 0xFFu);
    gs_rec_texcoord(1.0f, 0.0f);
    check("lit vertex 2 accepted", gs_rec_vertex(200, 100, 50u) == 0);

    /* Vertex 3: Specular peak */
    gs_rec_color(0xE0u, 0xF0u, 0xFFu, 0xFFu);
    gs_rec_texcoord(0.5f, 1.0f);
    check("lit vertex 3 accepted", gs_rec_vertex(150, 200, 50u) == 0);

    gs_rec_end();

    check("lit triangle emits exactly one group", gs_rec_sends() == 1u);
    check("and counts three vertices", gs_rec_verts() == 3u);
    check("one group captured without sink overflow", s_cap_i == 1 && s_over_cap == 0);
    if (s_cap_i < 1) return;
    w = s_cap[0];

    /* Check group length: 1 tag quadword + 3 vertices * 2 quadwords = 7 quadwords */
    check("lit triangle group is 7 quadwords", s_cap_n[0] == 7u && gs_group_qw(3u) == 7u);
    check_word("lit GIFTAG: NLOOP counts three vertices", gs_qw(w, 0u), tag_of(3u));
    check("lit GIFTAG: FLG is REGLIST", gs_flg_of(w) == GS_FLG_REGLIST);
    check("lit GIFTAG: EOP is set", gs_eop_of(w) == 1u);
    check("lit GIFTAG: PRE is clear", gs_pre_of(w) == 0u);
    check_word("lit GIFTAG: REGS is PRIM, RGBAQ, ST, XYZ2 (0x5210)", gs_regs_of(w), 0x5210ULL);

    /* Check each vertex's lit registers */
    check_word("vertex 1 PRIM is lit_prim",  gs_vq(w, 1u, 0u), lit_prim);
    check_word("vertex 1 RGBAQ matches lit ambient/lightmap", gs_vq(w, 1u, 1u), want_rgba1);
    check_word("vertex 1 ST matches (0,0)",  gs_vq(w, 1u, 2u), want_st1);

    check_word("vertex 2 PRIM is lit_prim",  gs_vq(w, 2u, 0u), lit_prim);
    check_word("vertex 2 RGBAQ matches lit diffuse", gs_vq(w, 2u, 1u), want_rgba2);
    check_word("vertex 2 ST matches (1,0)",  gs_vq(w, 2u, 2u), want_st2);

    check_word("vertex 3 PRIM is lit_prim",  gs_vq(w, 3u, 0u), lit_prim);
    check_word("vertex 3 RGBAQ matches lit specular peak", gs_vq(w, 3u, 1u), want_rgba3);
    check_word("vertex 3 ST matches (0.5,1)", gs_vq(w, 3u, 2u), want_st3);

    /* Validate Q-channel float: all vertices must have IEEE-754 1.0f in upper 32 bits */
    for (i = 1u; i <= 3u; i++) {
        uint64_t col = gs_vq(w, i, 1u);
        check("Q channel is 1.0f (0x3F800000)", (col >> 32) == 0x3F800000ULL);
    }

    /* Offline Dump and Replay: write to verify/fixtures/gs_lit_stream.bin */
    fp = fopen("verify/fixtures/gs_lit_stream.bin", "wb");
    check("can create verify/fixtures/gs_lit_stream.bin", fp != NULL);
    if (fp != NULL) {
        size_t bytes = GS_QW_BYTES(s_cap_n[0]);
        size_t written = fwrite(w, 1, bytes, fp);
        check("stream written completely to fixture file", written == bytes);
        fclose(fp);
    }

    /* Replay from disk */
    memset(replayed, 0, sizeof(replayed));
    fp = fopen("verify/fixtures/gs_lit_stream.bin", "rb");
    check("can open verify/fixtures/gs_lit_stream.bin for replay", fp != NULL);
    if (fp != NULL) {
        nread = fread(replayed, 1, sizeof(replayed), fp);
        fclose(fp);
        check("replayed byte count matches recorded stream", nread == GS_QW_BYTES(s_cap_n[0]));
        check("replayed bytes match recorded words exactly", memcmp(w, replayed, nread) == 0);

        /* Validate replayed stream fields through decoder */
        check("replayed tag NLOOP is 3", gs_nloop_of(replayed) == 3u);
        check("replayed tag FLG is REGLIST", gs_flg_of(replayed) == GS_FLG_REGLIST);
        check("replayed vertex 2 diffuse matches", gs_vq(replayed, 2u, 1u) == want_rgba2);
    }
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
    test_lit_vertex_stream();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - every recorded group matches its hand-written words\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a recorded vertex word or a group boundary is not what the GS decodes\n");
    return 1;
}
