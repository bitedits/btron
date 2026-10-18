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
 * is 128 bits and a packed GS register is 64 of them, so a dense REGLIST group packs its REGS list
 * end to end with no gaps -- the capture buffers below are still sized in pairs, and every register is
 * read through `gs_vq()` or `gs_half()`, which is what keeps this file from re-asserting whichever
 * stride it happened to ship last.  It has shipped three: one register per quadword under
 * `FLG=PACKED` (LANE 19 rung 5 measured that shape drawing nothing at all), then two registers a
 * vertex in the four-register chain that replaced it (which rung 12e measured as ONE FLAT COLOUR),
 * and now the two chains that measurement left -- RGBAQ+XYZ2 untextured, RGBAQ+ST+XYZ2 textured.  A
 * vertex is therefore one quadword or one and a half, and a textured group with an odd loop count
 * closes on a pad half.  Every count below comes from that law and is spelled out in the comment
 * above the check it belongs to, so a future stride change has to be re-derived, not re-printed.
 *
 * The buffer is 32 quadwords here rather than the shipped 4,096, because the boundary rule is the
 * part with logic in it and a split at the shipped size needs thousands of vertices to reach.  In
 * halves -- the unit the split arithmetic now works in -- that is 64, and GS_REC_HEADROOM is 12, so
 * the early boundary is offered once the cursor passes 52 and the buffer is full at 64.  A
 * three-register vertex therefore first splits at its 17th vertex (a strip, a fan, a point list), at
 * the 19th for a triangle list that has to wait for a multiple of three, and refuses the 21st for an
 * op it cannot continue; nothing splits below 17.  The vertex counts the boundary tests drive are
 * chosen from those numbers, and the arithmetic each one produces is spelled out in its own comment.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* Before the includes, so the recorder's own `#ifndef` yields and this gate drives the split paths
 * with a buffer it can actually fill.  32 quadwords is 64 register halves, GS_REC_HEADROOM is
 * 2 + 3*3 + 1 = 12 of them, and every split point below is arithmetic on those two numbers. */
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

/* ── Reading the captured stream the way the GIF decodes it ──────────────
 *
 * LANE 19 rung 12v: every vertex group now goes out behind an AD write of its own primitive, because
 * rung 12u measured the two apart on the machine -- the captured chain alone laid ONE pixel at its
 * apex at either value of PRE, and the same chain behind an AD word laid a 610-pixel triangle.  So
 * the sink now sees two KINDS of group, and the tests below read them by kind rather than by fixed
 * capture index.  The kind is the tag's own FLG, which is how the decoder tells them: a vertex group
 * is `REGLIST`, and any register-write group -- PRIM's new one and the texture switches' old one --
 * is `PACKED`.
 *
 * CAP_MAX is 8 and the widest case below drives five sends (two vertex groups and three register
 * groups), so the buffers still fit; a capture overflow lands in `s_over_cap`, which every test that
 * counts groups also asserts at zero, so a kind-counting bug shows up as a refusal rather than as a
 * silently short list.
 */
static int group_is_vertex(int k)
{
    return k < s_cap_i && gs_flg_of(s_cap[k]) == GS_FLG_REGLIST;
}

static uint32_t vgroups(void)
{
    uint32_t i, n = 0u;
    for (i = 0u; i < (uint32_t)s_cap_i; i++) if (group_is_vertex((int)i)) n++;
    return n;
}

static uint32_t adgroups(void)
{
    uint32_t i, n = 0u;
    for (i = 0u; i < (uint32_t)s_cap_i; i++) if (!group_is_vertex((int)i)) n++;
    return n;
}

/* The words of the `i`-th group of a kind, and its quadword count.  Out of range returns the capture
 * slot's own (zeroed) base rather than a wild pointer, so a test that forgot to check the count reads
 * zeros and fails a literal instead of walking off the array. */
static uint64_t *vg(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (!group_is_vertex(k)) continue;
        if (seen++ == i) return s_cap[k];
    }
    return s_cap[0];
}

static uint32_t vg_n(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (!group_is_vertex(k)) continue;
        if (seen++ == i) return s_cap_n[k];
    }
    return 0u;
}

static uint64_t *adg(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (group_is_vertex(k)) continue;
        if (seen++ == i) return s_cap[k];
    }
    return s_cap[0];
}

static uint32_t adg_n(int i)
{
    int k, seen = 0;
    for (k = 0; k < s_cap_i; k++) {
        if (group_is_vertex(k)) continue;
        if (seen++ == i) return s_cap_n[k];
    }
    return 0u;
}

/* A vertex's registers, spelled out the way the group's REGS list orders them.  Every primitive in
 * this file but one carries TME, so nearly every group it opens is the three-register one and every
 * count is 1 header quadword plus ceil(3 * NLOOP / 2); `k_prim_flat` is the exception and
 * test_flat_group() is the only place that reaches the two-register chain. */
static const uint64_t k_prim  = 0x25CULL;     /* TRI_STRIP, IIP, TME, ABE, context 1 */
static const uint64_t k_prim3 = 0x25BULL;     /* the same with op = TRI (a list)     */
static const uint64_t k_prim_flat = 0x24CULL; /* the strip's flags, with TME cleared */
static const uint64_t k_rgbaq = 0x3F80000078563412ULL;
static const uint64_t k_st    = 0x400000003F800000ULL;   /* s = 1.0f, t = 2.0f */

/* A textured vertex group's tag, for NLOOP of n.  The arithmetic is the GIFTAG field table itself:
 * NLOOP[14:0], EOP at 15 (= 0x8000), PRE at 46 SET (= 0x0000_4000_0000_0000), PRIM[57:47], FLG 1 at
 * 58 (= 0x0400_0000_0000_0000) and NREG 3 at 60 (= 0x3000_0000_0000_0000).  The REGS nibble list is
 * the *upper* half of the same quadword, which is why the capture below reads it as word 1 of the
 * group and not word 2.
 *
 * This ONE word is the tag for a strip, a fan, a list, a line and a point, because PRE carries the
 * primitive once for the whole packet -- gs_rec_prim()'s word, in the tag and not repeated per vertex
 * -- and the per-vertex PRIM this file used to emit is what LANE 19 rung 12e's boxes D and F
 * measured as unnecessary.  Three helpers used to live here, one per primitive, and their
 * three literals differed in digits 11 and 12 because those digits ARE the PRIM field; what rung 5
 * measured is that the field they encoded bought nothing on this GS, so the differences they checked
 * for are gone and the check that they are gone is below. */
static uint64_t tag_of_prim(uint32_t nloop, uint64_t prim)
{
    return 0x3400400000008000ULL | ((prim & 0x7FFULL) << 47) | (uint64_t)nloop;
}

/* The same word for the two-register chain, which is every untextured group: digit 15 reads 2. */
static uint64_t tag_flat_of(uint32_t nloop, uint64_t prim)
{
    return 0x2400400000008000ULL | ((prim & 0x7FFULL) << 47) | (uint64_t)nloop;
}

static uint64_t tag_of(uint32_t nloop)
{
    return tag_of_prim(nloop, k_prim);
}

/* The retired shape, kept here as the word the format claim is measured against: NREG 4 at bit 60,
 * FLG 0 (PACKED) so bit 58 is clear, PRIM 0x25C in [57:47] and PRE set at 46 -- the exact
 * combination LANE 19 rung 5 drew zero pixels with, at either value of PRE.  OR'd with this file's
 * NLOOP, its XOR with a shipping vertex tag is FLG's bit 58 and the NREG nibble and nothing else: the
 * two share PRE, PRIM, EOP and the loop count, which is the row that says the fix moved the group's
 * *shape* without touching the primitive it names. */
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

/* One 4-vertex TRI_STRIP is exactly how a GL_QUADS four-cornered bar or icon reaches the GS, so this
 * is the shape the XMB emits most often and the one written out here in full.  This primitive carries
 * TME, so its group is the three-register one: a tag quadword plus four vertices of one and a half
 * quadwords = 7 quadwords, 112 bytes, because `FLG=REGLIST` packs the list end to end and the fourth
 * vertex's kick lands on the closing quadword's high half. */
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

    check("exactly one vertex group, and two DMA starts because it carries its own PRIM write",
          vgroups() == 1u && gs_rec_sends() == 2u);
    check("and no vertex counted twice", gs_rec_verts() == 4u);
    check("nothing needed a boundary it could not take", gs_rec_unsplittable() == 0u);
    check("two sends captured, no sink overflow", s_cap_i == 2 && s_over_cap == 0);
    if (vgroups() < 1u) return;
    w = vg(0);

    /* The counts every split point and every size below is derived from, asserted rather than
     * assumed.  There is no longer one register count for the layer, so what the preprocessor could
     * never have checked -- `(GS_REC_REGS_N % 2u)` is not a pp-expression, since `4u` is not a
     * pp-number -- is now a fact about the two lists: an untextured vertex is two registers and
     * divides into quadwords exactly, a textured one is three and owes the group a pad half when the
     * loop count is odd.  A headroom sized for either list alone leaves half the buffer unused at the
     * split or overruns it, which is why it is asserted in halves here instead of reasoned about. */
    check("an untextured vertex is two registers", GS_REC_NREG_UNTEX == 2u);
    check("a textured vertex is three", GS_REC_NREG_TEX == 3u);
    check("and the set a caller hands over is the longer list",
          GS_REC_REGS_MAX == GS_REC_NREG_TEX);
    check("the headroom is a tag pair, three vertices and a pad, in halves",
          GS_REC_HEADROOM == 2u + 3u * GS_REC_REGS_MAX + 1u && GS_REC_HEADROOM == 12u);

    check("the packet is one tag plus four vertices of 1.5 quadwords = 7",
          gs_group_qw(4u, GS_REC_NREG_TEX) == 7u && vg_n(0) == 7u);
    check_word("GIFTAG: NLOOP counts the four vertices, not the 12 data registers",
               gs_qw(w, 0u), tag_of(4u));
    check_word("REGS: RGBAQ, ST, XYZ2 in the tag's upper half -- no NOP, no per-vertex PRIM",
               gs_regs_of(w), 0x521ULL);
    check("and the list reads back in that order, which is what a named lookup depends on",
          gs_nreg_of(w) == GS_REC_NREG_TEX &&
          gs_reg_nib(w, 0u) == GS_NIB_RGBAQ && gs_reg_nib(w, 1u) == GS_NIB_ST &&
          gs_reg_nib(w, 2u) == GS_NIB_XYZ2);

    /* The tag's own fields, named one by one: FLG says the registers are packed, and PRE with PRIM
     * name the primitive the packet draws -- which the group's own AD write in front of it also names,
     * because rung 12u measured the write, not the field, as what this GS programs from.  Both come
     * from the one word gs_rec_prim() was handed, so the row below and the one in test_prim_write()
     * cannot disagree. */
    check("FLG is REGLIST", gs_flg_of(w) == GS_FLG_REGLIST);
    check("PRE is set", gs_pre_of(w) == 1u);
    check("the tag's PRIM field is k_prim", gs_prim_of(w) == (k_prim & 0x7FFu));
    check("NREG is the three-register list", gs_nreg_of(w) == GS_REC_NREG_TEX);
    check("EOP is set, because every send here is a packet of its own", gs_eop_of(w) == 1u);
    /* The retired word, as the third term of the difference rather than a fourth assertion: both
     * carry PRE, this PRIM and this NLOOP, so what is left is FLG's bit 58 and the NREG nibble. */
    check_word("CONTROL retired PACKED tag, same NLOOP: FLG and NREG are all that differ",
               gs_qw(w, 0u) ^ (TAG_RETIRED_STRIP | 4u), 0x7400000000000000ULL);

    /* Vertices are written in order and each carries its own state */
    check_word("vertex 1 RGBAQ",    gs_vq(w, 1u, GS_NIB_RGBAQ), k_rgbaq);
    check_word("vertex 1 ST",       gs_vq(w, 1u, GS_NIB_ST), k_st);
    /* (0,0) px: 0*16 + 2048*16 = 32768 = 0x8000 in both halves, Z 0 above them. */
    check_word("vertex 1 XYZ2: the page origin, in 12.4",
               gs_vq(w, 1u, GS_NIB_XYZ2), 0x0000000080008000ULL);

    /* The same registers read as the stream's own halves rather than by name.  RGBAQ is half 2 -- the
     * first quadword past the header pair, in its LOW half -- ST is half 3 beside it, and the drawing
     * kick XYZ2 is half 4, the SECOND quadword's LOW half, because three registers do not divide into
     * two.  Under the four-register chain this file certified, RGBAQ was a quadword's low half with a
     * NOP beside it and XYZ2 was that quadword's high half; the register that moved down into the low
     * half is what the row below names, and a stride that drifted by a half would put the next
     * vertex's colour where this one's coordinate lives. */
    check_word("quadword 1 low half = vertex 1 RGBAQ",  gs_half(w, 1u, 0u), k_rgbaq);
    check_word("quadword 1 high half = vertex 1 ST",    gs_half(w, 1u, 1u), k_st);
    check_word("quadword 2 LOW half = vertex 1 XYZ2, the kick",
               gs_half(w, 2u, 0u), 0x0000000080008000ULL);
    /* Vertex 2's colour: the same value as vertex 1's, because the state is latched and this test sets
     * it once -- the row is about the POSITION (half 5, quadword 2's high half, straddling the
     * quadword boundary that a whole-quadword stride never had) and test_latched_state() is what
     * tests the value. */
    check_word("quadword 2 high half = vertex 2 RGBAQ, straddling the quadword",
               gs_upper_of(w, 2u), k_rgbaq);
    check_word("the whole group's last half is the last vertex's XYZ2",
               gs_upper_of(w, 6u), 0x00000000FFF07FF0ULL);

    /* (10,20) px: 10*16 = 160 -> 0x80A0, 20*16 = 320 -> 0x8140. */
    check_word("vertex 2 XYZ2: 10 px is 0xA0 sixteenths, not 10",
               gs_vq(w, 2u, GS_NIB_XYZ2), 0x00000000814080A0ULL);
    /* (320,240) px: 5120 -> 0x9400, 3840 -> 0x8F00. */
    check_word("vertex 3 XYZ2", gs_vq(w, 3u, GS_NIB_XYZ2), 0x000000008F009400ULL);
    /* (-1, 2047) px: -16 + 32768 = 32752 = 0x7FF0 and 2047*16 = 32752 + 32768 = 65 520 = 0xFFF0,
     * which is the field's own last value -- the corner a 2048 px canvas would wrap past.  X in the
     * low half, Y in the high half -- the pair the manual's XYZ2 table gives.  The fractional vertex
     * this case used to be (0.5 px, which truncated to 0x8008) went with the float parameter: under
     * LANE 12a the caller is a GLvertex whose x and y are already GLint, so a half-pixel vertex is
     * not a thing that can reach this function, and asserting one here would be asserting a literal
     * the production path cannot produce. */
    check_word("vertex 4 XYZ2: a negative pixel stays below the page origin",
               gs_vq(w, 4u, GS_NIB_XYZ2), 0x00000000FFF07FF0ULL);
}

/* The other list: a primitive whose PRIM has TME clear draws with a constant colour, so its group
 * names two registers and a vertex costs exactly one quadword.  This is the chain ps2sdk's own Gouraud
 * cube uses and the one LANE 19 rung 12e's boxes C and D measured as INTERPOLATING, and it is
 * reachable only through the primitive's own texture bit -- the recorder chooses the list, so a caller
 * never asks for two registers by name. */
static void test_flat_group(void)
{
    uint64_t *w;

    check("the untextured field table word is what this file calls it",
          gs_pack_prim(GS_OP_TRISTP, 1u, 0u, 1u, 1u, 0u) == k_prim_flat);
    check("and the texture bit is its only difference from the strip's",
          k_prim_flat == (k_prim & ~0x10ULL));

    start();
    gs_rec_prim(k_prim_flat);
    gs_rec_color(0x12u, 0x34u, 0x56u, 0x78u);
    gs_rec_texcoord(1.0f, 2.0f);      /* latched, and never written: this list has no ST slot */
    check("four vertices of an untextured strip are accepted",
          gs_rec_vertex(0, 0, 0u) == 0 && gs_rec_vertex(10, 20, 0u) == 0 &&
          gs_rec_vertex(320, 240, 0u) == 0 && gs_rec_vertex(-1, 2047, 0u) == 0);
    gs_rec_end();

    check("one group, two sends (its PRIM write and its chain)",
          gs_rec_sends() == 2u && vgroups() == 1u && s_cap_i == 2 && s_over_cap == 0);
    if (vgroups() < 1u) return;
    w = vg(0);

    /* Two registers a vertex, four vertices, one header pair: 2 + 4*2 = 10 halves = 5 quadwords, 80
     * bytes -- two quadwords shorter than the same strip textured, and the whole-number stride that
     * makes an untextured group never owe a pad however many vertices it holds. */
    check("the packet is a tag plus 4 vertices x 1 quadword = 5",
          gs_group_qw(4u, GS_REC_NREG_UNTEX) == 5u && vg_n(0) == 5u);
    check_word("GIFTAG: NREG 2, PRE set, PRIM named in the tag and written in front of it",
               gs_qw(w, 0u), tag_flat_of(4u, k_prim_flat));
    check_word("REGS: RGBAQ then XYZ2 only", gs_regs_of(w), 0x51ULL);
    check("NREG is 2 and FLG is still REGLIST",
          gs_nreg_of(w) == GS_REC_NREG_UNTEX && gs_flg_of(w) == GS_FLG_REGLIST);
    check("the tag's PRIM is the untextured word: IIP kept, TME clear",
          gs_prim_of(w) == k_prim_flat && (((k_prim_flat >> 4) & 1u) == 0u));

    /* A two-wide list's registers fall in whole quadwords: RGBAQ low, XYZ2 high, one vertex per unit
     * and nothing straddling a boundary. */
    check_word("vertex 1 RGBAQ is quadword 1's low half", gs_half(w, 1u, 0u), k_rgbaq);
    check_word("vertex 1 XYZ2 is quadword 1's high half",
               gs_half(w, 1u, 1u), 0x0000000080008000ULL);
    check_word("vertex 4 XYZ2 is quadword 4's high half",
               gs_upper_of(w, 4u), 0x00000000FFF07FF0ULL);
    /* Asking the decoder for a register this group does not name must not read a neighbour: gs_vq()
     * answers all-ones, which is neither a colour nor a coordinate nor a zero it could be confused
     * with.  This is the row that says the ST was left out on purpose rather than lost. */
    check("an untextured group has no ST to read", gs_vq(w, 1u, GS_NIB_ST) == ~0ULL);

    /* Three vertices is an odd loop count, and an odd loop count of a TWO-register list is still an
     * even number of registers: the pad belongs to the textured chain alone, and padding here would
     * hand the DMA a quadword the tag's NLOOP times NREG does not account for. */
    start();
    gs_rec_prim(k_prim_flat);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_vertex(2, 0, 0u);
    gs_rec_end();
    check("three untextured vertices are 2 + 6 halves and no pad",
          vgroups() == 1u && vg_n(0) == 4u &&
          vg_n(0) == gs_group_qw(3u, GS_REC_NREG_UNTEX));
    /* (2, 0) px: 2*16 = 32 -> 32 768 + 32 = 0x8020, y at the page origin. */
    check("so the group's last half is a register and not a zero",
          gs_upper_of(vg(0), 3u) == 0x0000000080008020ULL);
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

    if (vgroups() < 1u) { check("one group latched", 0); return; }
    w = vg(0);
    check("two vertices in one group", vg_n(0) == gs_group_qw(2u, GS_REC_NREG_TEX));
    check_word("GIFTAG NLOOP 2", gs_qw(w, 0u), tag_of(2u));
    /* The default is opaque white with Q = 1.0f, and Q is never anything else here: this layer is
     * affine, and Q is the perspective divisor the GS would divide ST by. */
    check_word("vertex 1 RGBAQ is the default opaque white, Q as its own bits",
               gs_vq(w, 1u, GS_NIB_RGBAQ), 0x3F800000FFFFFFFFULL);
    check_word("vertex 1 ST is the default 0,0", gs_vq(w, 1u, GS_NIB_ST), 0x0000000000000000ULL);
    check_word("vertex 1 XYZ2 carries the 32-bit Z the caller passed",
               gs_vq(w, 1u, GS_NIB_XYZ2), 0x1122334480008000ULL);
    check_word("the colour set after vertex 1 reaches vertex 2, not vertex 1",
               gs_vq(w, 2u, GS_NIB_RGBAQ), k_rgbaq);
}

/* A primitive change is a group boundary by construction.  It is NOT one because of the tag: the
 * primitive travels in the tag's own PRIM field and in the AD write that precedes each group, and the
 * two tags below differ only in that field and in NLOOP -- which is what the CONTROL row asserts.  It
 * is one because the recorder's continuation rule -- which already-written vertices have to be written
 * again when a group is cut -- is a property of one primitive type, and a group that held two would
 * have one sliding window serving two masters. */
static void test_prim_closes(void)
{
    uint64_t *g2;

    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_prim(k_prim3);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();

    check("the second gs_rec_prim flushed the strip as its own packet",
          vgroups() == 2u && s_cap_i == 4u);
    if (vgroups() < 2u) return;
    check_word("strip tag: 2 vertices", gs_qw(vg(0), 0u), tag_of_prim(2u, k_prim));
    check_word("list tag: 1 vertex",
               gs_qw(vg(1), 0u), tag_of_prim(1u, k_prim3));
    check_word("CONTROL strip vs list tags: only NLOOP and PRIM bits differ",
               gs_qw(vg(0), 0u) ^ gs_qw(vg(1), 0u), 0x0003800000000003ULL);
    check("tags carry their primitives",
          gs_prim_of(vg(0)) == k_prim && gs_prim_of(vg(1)) == k_prim3);
    /* The other half of the same ask, in the order the GIF decodes them: each group's own primitive
     * was WRITTEN in the packet in front of it, and the two writes name the two different words -- so
     * the strip's triangles and the list's one are not drawn under a shared inherited PRIM. */
    check("and each group is preceded by a write of its own PRIM word",
          gs_qw(adg(0), 1u) == k_prim && gs_qw(adg(1), 1u) == k_prim3);
    g2 = vg(1);
    check("four DMA starts -- two groups and their two writes -- three vertices counted once each",
          gs_rec_sends() == 4u && gs_rec_verts() == 3u && gs_rec_prim_writes() == 2u);
    /* The strip's two vertices are 2 + 6 = 8 halves = 4 quadwords; the list's single vertex is 5
     * halves, padded to 6 and sent as 3 quadwords.  The retired four-register shape made them 5 and 3
     * by a different route -- two whole quadwords a vertex, no pad -- and the retired PACKED one made
     * them 9 and 5, which is why the sizes are asserted against the two-argument law and not as
     * digits. */
    check("the two-vertex group is 4 quadwords, the one-vertex group 5 halves padded to 3",
          vg_n(1) == gs_group_qw(1u, GS_REC_NREG_TEX) && vg_n(1) == 3u &&
          vg_n(0) == gs_group_qw(2u, GS_REC_NREG_TEX) && vg_n(0) == 4u);
    /* Half 5 -- quadword 2's high half -- is the pad: a zero that is not a register, because the tag
     * accounts for five data registers and no sixth.  This is where the retired list carried a NOP. */
    check("and the odd group's last half is the pad, not a stale word",
          gs_upper_of(g2, 2u) == 0ULL &&
          gs_vq(g2, 1u, GS_NIB_XYZ2) == 0x0000000080008000ULL);
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
    /* The write belongs to the GROUP, not to the declaration: a declared primitive that never got a
     * vertex costs nothing on the bus, which is what keeps a state-change-happy backend from spending
     * a DMA start per question it asks about shading. */
    check("a primitive with no vertices sends nothing",
          s_cap_i == 0 && gs_rec_sends() == 0u && gs_rec_prim_writes() == 0u);

    start();
    check("a vertex before any primitive is refused, not written",
          gs_rec_vertex(0, 0, 0u) == -1);
    check("and nothing was sent or counted", s_cap_i == 0 && gs_rec_verts() == 0u);
}

/* A register write between two vertex groups: the same unit question asked of a packet shape the
 * blit path has already proven on the machine.  NLOOP is the number of writes, NREG is 1, the REGS
 * list is the single AD nibble, and each write takes a whole quadword whose upper half is the GP
 * register offset.  This is the one group in the layer whose high half *names* a register instead of
 * being one -- a vertex group's high half holds the loop's next register, this one's holds the
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

    check("five sends: the two vertex groups, the texture switch between them, and the PRIM write "
          "each group opens with",
          s_cap_i == 5 && vgroups() == 2u && adgroups() == 3u);
    if (vgroups() < 2u) return;
    /* One textured vertex is 2 + 3 halves = 5, padded to 6 and sent as 3 quadwords -- the same number
     * the AD group below costs for a different reason (a tag plus one whole quadword per write), which
     * is exactly why the two are asserted against their own laws rather than compared to each other. */
    check("the single vertex before it went out on its own, padded to a whole quadword",
          vg_n(0) == gs_group_qw(1u, GS_REC_NREG_TEX) && vg_n(0) == 3u &&
          (gs_qw(vg(0), 0u) & 0x7FFFu) == 1u);
    check("the AD group is a tag plus two writes = 3 quadwords",
          adg_n(1) == GS_REC_AD_QWC(2u) && adg_n(1) == 3u);
    /* Derived from the same field table as tag_of(), with its two differences named: NREG is 1
     * (digit 15 = 1 instead of 3), and PRE[46] and PRIM[57:47] are clear because a group that only
     * moves state has no primitive to carry -- so digits 11 and 12, which hold them for a vertex
     * group, are both 0.  What remains is NLOOP 2, EOP at bit 15 and NREG 1 at bit 60. */
    check_word("AD tag: NLOOP 2, NREG 1, PRIM and PRE clear, EOP set",
               gs_qw(adg(1), 0u), 0x1000000000008002ULL);
    check_word("AD REGS list: the single nibble 0x0E", gs_regs_of(adg(1)), 0x0EULL);
    w = adg(1);
    check_word("AD write 1: the register's value", gs_qw(w, 1u), data[0]);
    check_word("AD write 1: and its offset in the quadword's upper half",
               gs_upper_of(w, 1u), GS_GP_TEX0_1);
    check_word("AD write 2: the register's value", gs_qw(w, 2u), data[1]);
    check_word("AD write 2: and its offset", gs_upper_of(w, 2u), GS_GP_CLAMP_1);
    check("the AD group's upper halves are offsets, not padding",
          gs_upper_of(w, 1u) != 0u && gs_upper_of(w, 2u) != 0u);
    check_word("the group after the switch carries the PRIM it opened with",
               gs_prim_of(vg(1)), k_prim);
    check("and it is preceded by a write of the same word again: the switch's own group carries "
          "TEX registers, not a primitive",
          gs_qw(adg(2), 1u) == k_prim);
    check("so the vertices either side of a switch are counted, not lost",
          gs_rec_verts() == 2u && gs_rec_sends() == 5u && gs_rec_prim_writes() == 2u);

    /* Refusing rather than truncating is the point of the return value: a half-written TEX0 is a
     * texture whose dimensions and page address disagree, and no later word repairs it. */
    start();
    check("an empty AD group sends nothing and fails nothing", gs_rec_ad(data, offs, 0u) == 0);
    check("and a group larger than the buffer is refused whole",
          gs_rec_ad(data, offs, GS_REC_QW_CAP) == -1 && s_cap_i == 0);
}

/* ── The PRIM write that opens a group (LANE 19 rung 12v) ─────────────────
 * Rung 12u put four packets on the machine for one lone triangle and named the register this layer
 * had never been writing: the captured chain laid ONE pixel at its apex with the tag's PRIM field as
 * its only ask, at both values of PRE, and laid a correct 610-pixel triangle once an AD write of the
 * same word went in front of it -- the two AD writes that differed in IIP alone then came out
 * interpolated and flat.  This is the shape that fix has to keep, so it is written out here as words
 * rather than left to the census: NLOOP 1, NREG 1, FLG PACKED, PRE clear, the REGS list the single AD
 * nibble, and one data quadword whose low half is the primitive and whose high half is its GP offset.
 *
 * GS_GP_PRIM is offset 0, so this register's data quadword IS the prim word -- the one AD write in
 * the layer whose upper half is legitimately zero, which is why the check that the offset travelled
 * is `gs_qw() == the word` and not a non-zero test on the half.
 */
static void test_prim_write(void)
{
    start();
    gs_rec_prim(k_prim_flat);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_vertex(2, 0, 0u);
    gs_rec_end();

    check("a group of three vertices carries one PRIM write and one chain",
          adgroups() == 1u && vgroups() == 1u && gs_rec_sends() == 2u &&
          gs_rec_prim_writes() == 1u);
    check("and the write is on the bus BEFORE the chain it opens",
          adg(0) == s_cap[0] && vg(0) == s_cap[1]);
    check("the AD group is one tag plus one write = 2 quadwords",
          adg_n(0) == GS_REC_AD_QWC(1u) && adg_n(0) == 2u);
    /* NLOOP 1 at [14:0], EOP at 15, NREG 1 at 60, and nothing else: FLG 0 (PACKED), PRE clear and the
     * PRIM field clear, because a group that only moves state has no primitive to name in its tag --
     * the same law the texture switch's own tag follows two tests above. */
    check_word("AD tag: NLOOP 1, NREG 1, PRE and PRIM clear, EOP set",
               gs_qw(adg(0), 0u), 0x1000000000008001ULL);
    check_word("AD REGS list: the single AD nibble", gs_regs_of(adg(0)), 0x0EULL);
    check_word("AD data: the whole untextured prim word, offset half included",
               gs_qw(adg(0), 1u), k_prim_flat);
    /* The bit 12u's V2/V3 pair graded: it asked the same chain for Gouraud and for flat, changed
     * nothing else, and the census read field=609 back against field=0.  The ask has to be IN the
     * word that travels, so this is the row that says the recorder ships it rather than loses it. */
    check("and the shading bit the GS interpolates on is set in the written word",
          ((gs_qw(adg(0), 1u) >> 3) & 1u) == 1u);
    check("with the texture bit clear, as this primitive asked",
          ((gs_qw(adg(0), 1u) >> 4) & 1u) == 0u);

    /* Why the write is once per GROUP and not once per CHANGE: the recorder does not own the other
     * writers of this register, so a cached "PRIM already says that" is a claim about machine state
     * another packet can move -- eegs_present.c's depth clear ends on PRIM=SPRITE in the same frame.
     * Re-declaring the identical word is therefore not a no-op here: two groups get two writes, and
     * a group that never arrives gets none. */
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_vertex(2, 0, 0u);
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_vertex(1, 0, 0u);
    gs_rec_vertex(2, 0, 0u);
    gs_rec_end();
    check("two groups of the same primitive are two writes",
          vgroups() == 2u && gs_rec_prim_writes() == 2u && adgroups() == 2u);
    check("and both name the identical word",
          gs_qw(adg(0), 1u) == k_prim && gs_qw(adg(1), 1u) == k_prim);

    start();
    gs_rec_prim(k_prim);
    gs_rec_prim(k_prim3);
    gs_rec_end();
    check("a declaration with no vertices writes nothing at all",
          s_cap_i == 0 && gs_rec_prim_writes() == 0u && gs_rec_sends() == 0u);
}

/* ── The boundary rule ─────────────────────────────────────────────────
 * 32 quadwords is 64 register halves.  A textured vertex costs 3 and is charged 4 -- its own three,
 * plus one for the pad the group will owe -- so before the p-th vertex of a group the cursor is at
 * 2 + 3(p-1) and the arithmetic is on 3p + 3.  That passes CAP - HEADROOM = 52 at p = 17, where an
 * open strip is cut, and passes the buffer's own 64 at p = 21.  Twenty vertices therefore make 16 in
 * the first group and 2 re-emitted + 4 new = 6 in the tail: group 1 is 2 + 48 = 50 halves = 25
 * quadwords, group 2 is 2 + 18 = 20 halves = 10.  Under the four-register chain this file certified
 * the same 20 vertices split after the 12th, because the headroom was 7 quadwords against a buffer of
 * 32 and a vertex cost 2; the counts below come from the arithmetic in this paragraph, not from a
 * printed run.
 *
 * Rung 12v did not move any of these split points, and saying so is the check that its fix is a packet
 * rather than a payload: the PRIM write travels as its own group through gs_rec_ad(), which sends from
 * the start of the buffer and leaves the cursor where it found it, so a vertex still costs the 3
 * halves it is charged plus the pad it owes and nothing else lives in the buffer.  What the fix does
 * move is the send count -- every case below now reads a write beside its chain.
 */
static void test_strip_split(void)
{
    uint64_t *g1, *g2;
    int i;

    start();
    gs_rec_prim(k_prim);
    for (i = 1; i <= 20; i++) {
        check("strip vertex accepted", gs_rec_vertex(i, 2 * i, 0u) == 0);
        /* Each vertex carries its own colour and coordinate, so the re-emitted copies below are
         * comparable as words and not just as positions. */
        gs_rec_color((uint32_t)i, 0u, 0u, 255u);
    }
    gs_rec_end();

    check("20 vertices split into exactly two groups",
          vgroups() == 2u && gs_rec_sends() == 4u && gs_rec_prim_writes() == 2u);
    if (vgroups() < 2u) return;
    g1 = vg(0);
    g2 = vg(1);

    check("the first group is 16 vertices plus its tag = 25 quadwords",
          vg_n(0) == gs_group_qw(16u, GS_REC_NREG_TEX) && vg_n(0) == 25u);
    check("the second is the 2 re-emitted plus 4 new plus its tag = 10",
          vg_n(1) == gs_group_qw(6u, GS_REC_NREG_TEX) && vg_n(1) == 10u);
    check_word("first group's NLOOP is 16 vertices", gs_qw(g1, 0u), tag_of(16u));
    check_word("second group's NLOOP is 6, counting its re-emitted edge",
               gs_qw(g2, 0u), tag_of(6u));
    check("16 + 6 = the 20 handed over plus the one edge the strip had to repeat",
          gs_rec_verts() == 20u && gs_rec_unsplittable() == 0u);

    /* The continuation is the last two vertices of the first group, written again in full --
     * colour and texture coordinate included, because the next triangle is drawn with the state
     * those vertices carried, not with whatever the split happened to latch. */
    check("re-emitted vertex A is the strip's 15th, all three registers",
          gs_vertex_equal(g2, 1u, g1, 15u));
    check("re-emitted vertex B is its 16th", gs_vertex_equal(g2, 2u, g1, 16u));
    check("the third vertex of the new group is the 17th, i.e. genuinely new",
          !gs_vertex_equal(g2, 3u, g1, 16u));
    /* The guard against a test that passes for the wrong reason: gs_vertex_equal() compares whole
     * register sets, so a continuation that wrote one vertex twice would make the pair equal and this
     * row would fail. */
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));

    /* Where those registers sit, stated as halves and as hand words.  The colour this test latches is
     * the iteration's number: gs_rec_color(i, 0, 0, 255) runs *after* vertex i, so the strip's 15th
     * vertex carries the r the 14th iteration set.  RGBAQ is R[7:0] G[15:8] B[23:16] A[31:24] with Q
     * above them, so that word is 0x3F800000_FF00000E -- A 255, B 0, G 0, R 14, Q 1.0f -- and it
     * belongs in quadword 1's LOW half, the first register of the first loop.  Under the four-register
     * chain this file certified the same colour sat in the SAME half, beside a NOP, and the 12th
     * vertex carried it; under the retired sparse shape it filled a quadword of its own. */
    check_word("the re-emitted vertex's RGBAQ is quadword 1's low half",
               gs_qw(g2, 1u), 0x3F800000FF00000EULL);
    check_word("and the second re-emitted vertex carries its own colour, r = 15, in quadword 2's high",
               gs_upper_of(g2, 2u), 0x3F800000FF00000FULL);
    /* Vertex 15 was (15, 30) px: 15*16 = 240 -> 32 768 + 240 = 0x80F0 and 30*16 = 480 -> 33 248 =
     * 0x81E0, X low and Y high.  XYZ2 is the loop's THIRD register, which under two registers to a
     * quadword is the second quadword's LOW half -- the half that held a loop's second register when
     * the chain was wider. */
    check_word("the re-emitted vertex's XYZ2 is quadword 2's low half",
               gs_vq(g2, 1u, GS_NIB_XYZ2), 0x0000000081E080F0ULL);

    /* And the stride counted over the whole group rather than asserted at one vertex: the tail's own
     * NLOOP times its own NREG, plus the header pair, is the number of halves the sink was handed.
     * Read the group at a fixed 2 per loop instead -- or at the 4 the sparse shape cost -- and its
     * last vertex is not where the count says it is, which is the row that names the format and not
     * the geometry. */
    check("second group tag carries strip PRIM and NLOOP 6",
          gs_prim_of(g2) == k_prim && gs_nloop_of(g2) == 6u);
    check("and its quadword count is its header pair plus three registers per loop, rounded up",
          vg_n(1) * 2u == 2u + gs_nloop_of(g2) * gs_nreg_of(g2));
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
    for (i = 0; i < 20; i++) {
        check("fan vertex accepted", gs_rec_vertex(i, i, 0u) == 0);
    }
    gs_rec_end();

    /* The same 3p + 3 arithmetic as the strip: the fan's boundary rule is satisfied from its second
     * vertex on, so it splits at the 17th too.  Group 1 holds 16 vertices; group 2 is the apex and the
     * fan's 16th vertex re-emitted, plus the four new ones. */
    check("the fan split once", vgroups() == 2u && s_cap_i == 4u);
    if (vgroups() < 2u) return;
    g1 = vg(0);
    g2 = vg(1);
    check_word("fan group 1 NLOOP 16", gs_qw(g1, 0u), tag_of_prim(16u, fan_prim));
    check("fan group 1 is 16 vertices at 3 registers plus the header pair",
          vg_n(0) == 25u && vg_n(0) == gs_group_qw(16u, GS_REC_NREG_TEX));
    check("fan group 2 re-emits the apex and the last edge, so 6 vertices and 10 quadwords",
          vg_n(1) == 10u && (gs_qw(g2, 0u) & 0x7FFFu) == 6u);
    check_word("and its tag carries fan PRIM, NLOOP 6", gs_qw(g2, 0u), tag_of_prim(6u, fan_prim));
    check("both fan tags name fan_prim",
          gs_prim_of(g1) == fan_prim && gs_prim_of(g2) == fan_prim &&
          gs_flg_of(g1) == GS_FLG_REGLIST && gs_flg_of(g2) == GS_FLG_REGLIST);
    check("and so do the two writes in front of them, the split's continuation included",
          gs_qw(adg(0), 1u) == fan_prim && gs_qw(adg(1), 1u) == fan_prim);
    check("the continued fan starts from the apex, not from vertex 15",
          gs_vertex_equal(g2, 1u, g1, 1u));
    check("and its second vertex is the fan's last, shared edge",
          gs_vertex_equal(g2, 2u, g1, 16u));
    check("the two re-emitted vertices are distinct, not one vertex written twice",
          !gs_vertex_equal(g2, 1u, g2, 2u));
    /* The apex and the shared edge as hand words in the halves they belong to.  The fan's vertices
     * are (i, i) px: the apex is (0, 0), so 0*16 + 2048*16 = 0x8000 in both coordinate fields, and the
     * sixteenth is (15, 15), so 15*16 = 240 -> 32 768 + 240 = 0x80F0 in both.  XYZ2 is the loop's
     * third register, so it sits at half 4 and half 7 of the group -- straddling quadwords exactly as
     * the textured stride says it must, where the four-register chain put it in the second quadword's
     * high half and the sparse one gave it a quadword of its own. */
    check_word("the re-emitted apex's XYZ2 is quadword 2's low half",
               gs_vq(g2, 1u, GS_NIB_XYZ2), 0x0000000080008000ULL);
    check_word("and the re-emitted edge's, at quadword 3's high half",
               gs_vq(g2, 2u, GS_NIB_XYZ2), 0x0000000080F080F0ULL);
    check_word("the genuinely new 17th vertex is (16, 16)",
               gs_vq(g2, 3u, GS_NIB_XYZ2), 0x0000000081008100ULL);
}

/* A triangle *list* has no shared vertices, so its only legal boundaries are between whole
 * triangles: cutting after the 17th vertex of a list would leave a triangle with two vertices and
 * steal its third from the next group. */
static void test_list_boundary(void)
{
    uint64_t *g1, *g2;
    int i;

    start();
    gs_rec_prim(k_prim3);
    for (i = 0; i < 20; i++) {
        check("list vertex accepted", gs_rec_vertex(i, 0, 0u) == 0);
    }
    gs_rec_end();

    /* The headroom rule is what pays for this: at the 17th vertex the arithmetic passes 52 but nloop
     * is 16, not a multiple of three, so the recorder keeps appending -- 18 vertices is 56 halves,
     * still inside the buffer -- and cuts at the 19th, where 60 halves is under 64 and nloop is 18.
     * Without the headroom the group would fill on the 20th vertex's 62 + 4 and the list's last
     * triangle would have no boundary to be cut at. */
    check("the list split once", vgroups() == 2u && s_cap_i == 4u);
    if (vgroups() < 2u) return;
    g1 = vg(0);
    g2 = vg(1);
    check_word("and it waited for the multiple of three: 18 vertices, not 17",
               gs_qw(g1, 0u), tag_of_prim(18u, k_prim3));
    check("so the first group is 18 vertices and 28 quadwords, all whole triangles",
          vg_n(0) == 28u && gs_nloop_of(g1) % 3u == 0u &&
          vg_n(0) == gs_group_qw(18u, GS_REC_NREG_TEX));
    check("the tail group holds the two vertices left: header pair plus 3 registers each = 4",
          vg_n(1) == gs_group_qw(2u, GS_REC_NREG_TEX) && vg_n(1) == 4u);
    check("and a list re-emits nothing: its tag says two vertices",
          gs_nloop_of(g2) == 2u);
    check_word("the tail's PRIM in tag", gs_prim_of(g2), k_prim3);
    /* The list's 19th vertex is (18, 0) px: 18*16 = 288 -> 32 768 + 288 = 0x8120, y at the page
     * origin, Z 0.  It is the first triple past the header pair, so its XYZ2 is half 4 -- quadword 2's
     * low half. */
    check_word("the tail's first XYZ2, at the second quadword's low half",
               gs_vq(g2, 1u, GS_NIB_XYZ2), 0x0000000080008120ULL);
    check("and nothing was refused", gs_rec_unsplittable() == 0u && gs_rec_verts() == 20u);
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
    for (i = 0; i < 21; i++) {
        if (gs_rec_vertex(i, 0, 0u) != 0) refused++;
    }
    gs_rec_end();

    /* 3p + 3 crosses the buffer itself at p = 21 -- 66 halves against 64 -- and the early test at
     * p = 17..20 has no legal answer for an unknown op, so twenty vertices are taken and the
     * twenty-first is the single refusal. */
    check("20 fit and only the 21st was refused", refused == 1 && gs_rec_verts() == 20u);
    check("the refusal is counted, not silent", gs_rec_unsplittable() == 1u);
    check("the 20 that were taken are still sent as one group", vgroups() == 1u && s_cap_i == 2u);
    if (vgroups() < 1u) return;
    /* The tag is the same word a strip's or a fan's is, which is what the dense format buys here:
     * an op 7 PRIM changes only the register each loop carries, not a bit of the tag.  Under the
     * retired PACKED shape this row compared a fourth hand literal, because PRIM[57:47] held the op
     * and PRE[46] held its companion -- 0x412FC00000008000 for NLOOP 20. */
    check_word("with its own NLOOP intact and primitive in the tag",
               gs_qw(vg(0), 0u), tag_of_prim(20u, bad_prim));
    check("20 vertices plus a header pair is 62 halves = 31 quadwords, inside the 32 and no pad",
          vg_n(0) == 31u && vg_n(0) == gs_group_qw(20u, GS_REC_NREG_TEX));
    check("and the group's tag names the undefined op",
          gs_prim_of(vg(0)) == bad_prim);
    /* Even an op the recorder cannot continue gets its own write: the refusal is about the boundary
     * rule, not about whether the stream is allowed to say what it is drawing. */
    check("and the write in front of it names it too", gs_qw(adg(0), 1u) == bad_prim);
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

    /* Every vertex is its own primitive, so a boundary is always available and nothing is re-emitted:
     * the split falls where the headroom rule says, after the 16th, and 30 points make starts of 16
     * and 14 vertices -- 25 quadwords and 22 of them.  That is the arithmetic behind "capacity is a
     * latency knob": the same 30 points at the shipped 4,096 quadwords are ONE start, and at this
     * buffer they are two where the four-register chain cost three and the sparse shape cost five. */
    start();
    gs_rec_prim(point_prim);
    for (i = 0; i < 30; i++) {
        if (gs_rec_vertex(i, 0, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("30 points lose nothing",
          bad == 0 && gs_rec_verts() == 30u && gs_rec_unsplittable() == 0u);
    check("and they cost two groups, plus one PRIM write each, at this capacity",
          vgroups() == 2u && gs_rec_sends() == 4u);
    if (vgroups() < 2u) return;
    check("16 + 14 = 30, with no vertex repeated",
          gs_nloop_of(vg(0)) == 16u && gs_nloop_of(vg(1)) == 14u);
    check("each group is its header pair plus its vertex count times 3 registers",
          vg_n(0) == 25u && vg_n(1) == 22u &&
          vg_n(1) == gs_group_qw(14u, GS_REC_NREG_TEX));

    /* A LINE list cuts only between segments, so the headroom rule is what saves it here as well: the
     * boundary arithmetic passes 52 at the 17th vertex, when nloop is 16 and even, and 18 endpoints
     * make whole-segment groups of 16 and 2 that refuse nothing. */
    start();
    gs_rec_prim(line_prim);
    bad = 0;
    for (i = 0; i < 18; i++) {
        if (gs_rec_vertex(i, i, 0u) != 0) bad++;
    }
    gs_rec_end();
    check("18 endpoints of a LINE list are all taken", bad == 0 && gs_rec_verts() == 18u);
    check("and split into two groups", vgroups() == 2u && s_cap_i == 4u);
    if (vgroups() < 2u) return;
    check("both NLOOPs are a whole number of segments",
          gs_nloop_of(vg(0)) == 16u && gs_nloop_of(vg(1)) == 2u);
    check("and no vertex is re-emitted, because a segment shares none",
          vg_n(1) == gs_group_qw(2u, GS_REC_NREG_TEX) && vg_n(1) == 4u);
    /* The tail's first endpoint is (16, 16) px: 16*16 = 256 -> 32 768 + 256 = 0x8100 in both fields.
     * A continuation that re-emitted -- which a LINE *list* must never do -- would put (15, 15) here. */
    check_word("the tail's first vertex is the 17th endpoint, not a repeat",
               gs_vq(vg(1), 1u, GS_NIB_XYZ2), 0x0000000081008100ULL);
}

/* The counters are the frame's latency budget, so they have to survive a re-init and report the
 * next frame rather than the last one. */
static void test_counters_reset(void)
{
    start();
    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();
    check("one frame, one group and its write, i.e. two starts",
          gs_rec_sends() == 2u && gs_rec_prim_writes() == 1u && gs_rec_verts() == 1u);

    start();
    check("gs_rec_init zeroes the counters a bench reads as per-frame",
          gs_rec_sends() == 0u && gs_rec_verts() == 0u && gs_rec_unsplittable() == 0u &&
          gs_rec_prim_writes() == 0u);
    check("and a fresh recorder sends nothing until told to", s_cap_i == 0);

    gs_rec_prim(k_prim);
    gs_rec_vertex(0, 0, 0u);
    gs_rec_end();
    check("the second frame counts on its own",
          gs_rec_sends() == 2u && gs_rec_prim_writes() == 1u && gs_rec_verts() == 1u);
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

    check("lit triangle emits exactly one group, and two sends because it carries its own PRIM write",
          vgroups() == 1u && gs_rec_sends() == 2u && gs_rec_prim_writes() == 1u);
    check("and counts three vertices", gs_rec_verts() == 3u);
    check("both sends captured without sink overflow", s_cap_i == 2 && s_over_cap == 0);
    if (vgroups() < 1u) return;
    w = vg(0);

    /* Check group length: the header pair, then 3 vertices x 3 registers = 11 halves, and the odd
     * count is padded to 12 -- six quadwords, 96 bytes.  The same triangle untextured would have been
     * 2 + 6 halves and four quadwords: the two extra are the ST a lightmap sample needs. */
    check("lit triangle group is 6 quadwords",
          vg_n(0) == 6u && gs_group_qw(3u, GS_REC_NREG_TEX) == 6u);
    check_word("lit GIFTAG: NLOOP counts three vertices", gs_qw(w, 0u), tag_of_prim(3u, lit_prim));
    check("lit GIFTAG: FLG is REGLIST", gs_flg_of(w) == GS_FLG_REGLIST);
    check("lit GIFTAG: EOP is set", gs_eop_of(w) == 1u);
    check("lit GIFTAG: PRE is set", gs_pre_of(w) == 1u);
    check("lit GIFTAG: PRIM is lit_prim", gs_prim_of(w) == (lit_prim & 0x7FFu));
    /* The word the GS is actually programmed with travels in the packet IN FRONT of the chain, and it
     * is the IIP bit inside that word which rung 12u's V2/V3 pair measured as the lever the
     * rasteriser interpolates on.  GS_GP_PRIM is offset 0, so the write's whole quadword IS the word. */
    check_word("lit PRIM write: the same word, with its offset half reading 0",
               gs_qw(adg(0), 1u), lit_prim);
    check("lit GIFTAG: NREG is three", gs_nreg_of(w) == GS_REC_NREG_TEX);
    check_word("lit GIFTAG: REGS is RGBAQ, ST, XYZ2 (0x521)", gs_regs_of(w), 0x521ULL);

    /* Check each vertex's lit registers, by NAME -- which is what the replay decoder below has to do
     * too, because the position of XYZ2 in this list is decided by the group's own tag. */
    check_word("vertex 1 RGBAQ matches lit ambient/lightmap",
               gs_vq(w, 1u, GS_NIB_RGBAQ), want_rgba1);
    check_word("vertex 1 ST matches (0,0)",  gs_vq(w, 1u, GS_NIB_ST), want_st1);
    check_word("vertex 2 RGBAQ matches lit diffuse", gs_vq(w, 2u, GS_NIB_RGBAQ), want_rgba2);
    check_word("vertex 2 ST matches (1,0)",  gs_vq(w, 2u, GS_NIB_ST), want_st2);
    check_word("vertex 3 RGBAQ matches lit specular peak",
               gs_vq(w, 3u, GS_NIB_RGBAQ), want_rgba3);
    check_word("vertex 3 ST matches (0.5,1)", gs_vq(w, 3u, GS_NIB_ST), want_st3);
    /* (150, 200) px at Z 50: 150*16 = 2400 -> 32 768 + 2 400 = 0x8960, 200*16 = 3200 -> 0x8C80, Z in
     * the field above them. */
    check_word("vertex 3 XYZ2 carries its depth",
               gs_vq(w, 3u, GS_NIB_XYZ2), 0x000000328C808960ULL);
    check("and the group closes on the pad, not on a stale half", gs_upper_of(w, 5u) == 0ULL);

    /* Validate Q-channel float: all vertices must have IEEE-754 1.0f in upper 32 bits */
    for (i = 1u; i <= 3u; i++) {
        uint64_t col = gs_vq(w, i, GS_NIB_RGBAQ);
        check("Q channel is 1.0f (0x3F800000)", (col >> 32) == 0x3F800000ULL);
    }

    /* Offline Dump and Replay: write to verify/fixtures/gs_lit_stream.bin.
     * The fixture is the VERTEX group only, 96 bytes, so its replay is a chain that has to be told
     * what to draw -- which after rung 12v is a fact the stream states in the packet in front of it.
     * A reader that wants the lit triangle on a machine therefore prepends the two-quadword AD write
     * asserted above, or takes the whole frame from the live capture; widening the fixture to the pair
     * is #120's call, not this row's. */
    fp = fopen("verify/fixtures/gs_lit_stream.bin", "wb");
    check("can create verify/fixtures/gs_lit_stream.bin", fp != NULL);
    if (fp != NULL) {
        size_t bytes = GS_QW_BYTES(vg_n(0));
        size_t written = fwrite(w, 1, bytes, fp);
        check("stream written completely to fixture file", written == bytes);
        check("and the fixture is 6 quadwords = 96 bytes", bytes == 96u);
        fclose(fp);
    }

    /* Replay from disk */
    memset(replayed, 0, sizeof(replayed));
    fp = fopen("verify/fixtures/gs_lit_stream.bin", "rb");
    check("can open verify/fixtures/gs_lit_stream.bin for replay", fp != NULL);
    if (fp != NULL) {
        nread = fread(replayed, 1, sizeof(replayed), fp);
        fclose(fp);
        check("replayed byte count matches recorded stream", nread == GS_QW_BYTES(vg_n(0)));
        check("replayed bytes match recorded words exactly", memcmp(w, replayed, nread) == 0);

        /* Validate replayed stream fields through decoder */
        check("replayed tag NLOOP is 3", gs_nloop_of(replayed) == 3u);
        check("replayed tag FLG is REGLIST", gs_flg_of(replayed) == GS_FLG_REGLIST);
        check("replayed tag NREG is 3, so the reader knows a vertex is 1.5 quadwords",
              gs_nreg_of(replayed) == GS_REC_NREG_TEX);
        check("replayed vertex 2 diffuse matches",
              gs_vq(replayed, 2u, GS_NIB_RGBAQ) == want_rgba2);
        check("replayed vertex 2's kick is where the decoded stride puts it",
              gs_vq(replayed, 2u, GS_NIB_XYZ2) == gs_vq(w, 2u, GS_NIB_XYZ2));
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS vertex recorder vs hand-built GIF groups (cap=%u qw)\n", GS_REC_QW_CAP);
    printf("==========================================================\n");

    test_quad_group();
    test_flat_group();
    test_latched_state();
    test_prim_closes();
    test_empty();
    test_ad_group();
    test_prim_write();
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
