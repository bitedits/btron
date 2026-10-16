/*
 * test_ps2_gs_poster.c -- the flat-colour SPRITE batch, word by word
 *
 * LANE 9b's gate (doc/txt/PS2.txt, "The lanes" row 9, the poster half).  A poster is the one
 * price-cut available on the `bg=` column, and its failure mode has no console line: a colour read
 * in the wrong byte order paints the desktop brown at the same packet length, a vertex computed by
 * a second encoder can land a sprite's right edge a sixteenth of a pixel from its neighbour's left
 * edge, and an NLOOP that counts rects the buffer does not hold hands channel 2 a QWC shorter than
 * the tag claims -- which the GIF decodes as the start of another packet.  None of those are
 * visible on the host, so none of them are argued about: every word below is compared with a
 * literal derived by adding up the field's own bit positions from ps2_gs_pack.h's table, the
 * test_ps2_gs_pack discipline of expectations the code under test did not compute.
 *
 * Three of the assertions are controls in LAW 16d's sense, and they are labelled: the colour,
 * told in a byte order that has all four channels different so a swap cannot hide inside a
 * symmetric pixel; the vertex, compared against the recorder's own float encoder for every
 * coordinate the field can hold, so the shift is shown to be the same encoding and not a rival one;
 * and the count, where a refused rect must disappear from NLOOP as well as from the buffer.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The builder's own source, not a link against it: it is the file under test, its helpers are
 * file-static on purpose, and the vertex comparison below needs one of them.  The packer comes in
 * first because the poster is made of its field encoders, and the same idiom as the gs_rec and
 * gs_tex gates means the identical translation unit is what this suite measures and what links
 * into btron-ps2.elf. */
#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_poster.c"

static int s_checks, s_failures;

static void eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-40s got=0x%016llx want=0x%016llx\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

static void eq32(const char *what, uint32_t got, uint32_t want)
{
    eq(what, (uint64_t)got, (uint64_t)want);
}

/* ── The sizes ─────────────────────────────────────────────────────────── */

static void sizes(void)
{
    /* One tag quadword -- the tag's own 64 bits with the REGS list riding its high half -- then TWO
     * GIF quadwords per rect, because a dense REGLIST group puts two 64-bit registers in every
     * 128-bit unit.  Both spellings of the count have to agree, because the caller sizes its buffer
     * with one and the packet is checked against the other. */
    eq32("qwc(1)", gs_poster_qwc(1u), 3u);
    eq32("qwc(2)", gs_poster_qwc(2u), 5u);
    eq32("qwc(119)", gs_poster_qwc(119u), 1u + 2u * 119u);
    eq32("macro(1) == fn(1)", GS_POSTER_QWC(1u), gs_poster_qwc(1u));
    /* A batch of nothing is not a packet, and a caller that sized a buffer for 0 quadwords and
     * then posted 1 would put the tag's own bytes on the bus as data. */
    eq32("qwc(0) is nothing", gs_poster_qwc(0u), 0u);

    /* The division that makes GS_POSTER_QW_PER_RECT exact, asserted here rather than in a `#if`:
     * `(GS_POSTER_REGS_N % 2u)` is not a preprocessor expression at all, because `4u` is not a
     * pp-number, so a guard in the header silently reads zero and can never fire.  Odd register
     * counts are legal GIF, but a half-quadword stride is not, and this is the only place the
     * evenness is a measured fact rather than an intention. */
    eq32("REGS_N is even, so the dense stride is exact", GS_POSTER_REGS_N % 2u, 0u);
    eq32("and per-rect quadwords are REGS_N/2", GS_POSTER_QW_PER_RECT, GS_POSTER_REGS_N / 2u);
}

/* ── One rect, every word ──────────────────────────────────────────────── */

/* The desktop's own flat colour as the canvas holds it: 0xff808000 is what [BENCH] poster price:
 * reads out of s_desktop_backbuffer for COLOR_TEAL, i.e. A=0xff B=0x80 G=0x80 R=0x00. */
#define TEAL 0xff808000u

static void one_rect(void)
{
    const gs_poster_rect r[1] = { { 0, 0, 800, 16 } };
    uint64_t qw[2u * 8u];
    int32_t got;

    /* Sentinels first: an assertion that a word is right is worth nothing unless the buffer could
     * have shown it wrong, and a packet that forgot to write a quadword would otherwise read 0 and
     * look like a legal NOP register write. */
    for (unsigned i = 0u; i < 16u; i++) qw[i] = 0xDEADBEEFDEADBEEFULL;

    got = gs_poster_packet(r, 1u, TEAL, qw, gs_poster_qwc(1u));

    eq32("one rect returns its own size", (uint32_t)got, 3u);

    /* GIFTAG, NLOOP=1 / EOP=1 / PRE=0 / PRIM=0 / FLG=REGLIST / NREG=4:
     *   1<<0 | 1<<15 | 0<<46 | 0<<47 | 1<<58 | 4<<60
     * = 0x0000000000000001 + 0x8000 + 0x0000000000000000 + 0x0000000000000000
     *   + 0x0400000000000000 + 0x4000000000000000 */
    eq("tag: NLOOP/EOP/PRE/PRIM/FLG/NREG", qw[0], 0x4400000000008001ULL);
    /* PRE is clear and the tag's PRIM field is 0, which is what makes the primitive arrive as the
     * group's first DATA register below -- draw2d.c's shape, and the one LANE 19 rung 5 measured as
     * drawing on this GS while the PACKED/PRE=1 form did not. */
    eq("tag: PRE clear", (qw[0] >> 46) & 1u, 0u);
    eq("tag: PRIM field unused", (qw[0] >> 47) & 0x7FFu, 0u);
    /* REGS list, PRIM 0x0 then RGBAQ 0x1 then XYZ2 0x5 then XYZ2 0x5, lowest nibble first:
     * 0x0 + (1<<4) + (5<<8) + (5<<12) = 0x10 + 0x500 + 0x5000 */
    eq("regs: PRIM,RGBAQ,XYZ2,XYZ2", qw[1], 0x5510ULL);

    /* PRIM's own data word: OP=6 with IIP, TME, ABE, CTXT and FIX all clear, so the sprite is
     * flat-shaded, untextured and unblended -- which is the entire reason a poster costs no page. */
    eq("data 0 low: PRIM sprite flat", qw[2], 0x6ULL);

    /* RGBAQ.  Low 32 bits are the canvas word UNTOUCHED, high 32 are Q=1.0f's own bits
     * (0x3f800000).  This is the byte-order law as a single equality: the poster and the upload
     * that it replaces must put the same 32 bits in the same VRAM word.  It now rides the half ABOVE
     * PRIM instead of its own quadword: the 64-bit padding this used to write is the stride LANE 19
     * rung 5 retired, and asserting the value here is what stops a reverted emitter from passing a
     * gate that only looked at qw[2]. */
    eq("data 0 high: RGBAQ == canvas px | Q", qw[3], 0x3F800000FF808000ULL);
    eq("data 0 high: a register, not padding", qw[3] != 0u, 1u);

    /* XYZ2 corners: X[15:0] Y[31:16], each 12.4 with the page at 2048 px folded in, and both of
     * them inside the second data quadword -- the sprite's second corner is its drawing kick.
     * (0,0)   -> 0x8000 , 0x8000
     * (800,16)-> 800*16 = 12800 = 0x3200 + 0x8000 = 0xB200 ; 16*16 = 0x100 + 0x8000 = 0x8100 */
    eq("data 1 low: XYZ2 top-left", qw[4], 0x0000000080008000ULL);
    eq("data 1 high: XYZ2 bottom-right", qw[5], 0x000000008100B200ULL);
    /* Nothing else belongs to a one-rect packet: the third quadword is the batch's neighbour's, not
     * this sprite's, and a builder that wrote a fourth would be handing the DMA more QWC than the
     * tag counted. */
    eq("and the packet stops at three quadwords", qw[6], 0xDEADBEEFDEADBEEFULL);

    /* The rect is the canvas's own width and the second corner is exclusive: 800 means the sprite
     * covers pixels 0..799, which is what the copy it replaces moves. */
    eq32("dropped: none", gs_poster_dropped(), 0u);
}

/* ── CONTROL: the colour is read as a canvas word, not as a COLOR ──────── */

static void colour_control(void)
{
    const gs_poster_rect r[1] = { { 4, 5, 6, 7 } };
    uint64_t qw[2u * 8u];

    /* Four DIFFERENT bytes, so no swap of R against B or of G against A can produce the same word
     * by symmetry.  A caller that passed this through the desktop's COLOR arithmetic -- where the
     * value is 0xRRGGBB with the byte order the canvas then exchanges -- would put 0x78 at bits
     * [23:16] instead of [7:0], and this assertion is where that difference stops being a rumour. */
    for (unsigned i = 0u; i < 16u; i++) qw[i] = 0u;
    gs_poster_packet(r, 1u, 0x12345678u, qw, gs_poster_qwc(1u));
    eq("CONTROL colour 0x12345678 -> RGBAQ", qw[3], 0x3F80000012345678ULL);

    /* And the same pixel through the encoder the upload path uses, spelled by hand: R 0x78 in the
     * low byte, G 0x56, B 0x34, A 0x12.  These two statements are the same claim from two
     * directions, which is what makes the first one a measurement rather than a tautology. */
    eq("rgbaq(0x78,0x56,0x34,0x12,1.0f)",
       gs_pack_rgbaq(0x78u, 0x56u, 0x34u, 0x12u, GS_Q_ONE), 0x3F80000012345678ULL);

    /* The negative half of the control: the same pixel read in the BTRON order -- R at [23:16] --
     * is a DIFFERENT word, and if the poster ever produced this one the desktop would be the wrong
     * colour while every packet length still matched. */
    eq("the swapped word is not what ships",
       gs_pack_rgbaq(0x34u, 0x56u, 0x78u, 0x12u, GS_Q_ONE) != 0x3F80000012345678ULL, 1u);
}

/* ── CONTROL: the poster's corner is the old float encoding, at every pixel ── */

/* The float spelling that production used before LANE 12a, kept here as this program's oracle.  It
 * is deliberately NOT in src/: a second encoding in the tree is the defect, and one in the gate is
 * the instrument that retires it. */
static uint32_t coord_float_ref(int32_t px)
{
    return (uint32_t)((int32_t)((float)px * 16.0f) + (int32_t)(2048u << 4)) & 0xFFFFu;
}

static void coord_control(void)
{
    int worst_diff = 0, n = 0;

    /* There is now one encoder in the tree, so this sweep no longer compares two of them -- it
     * compares the single one with the expression it replaced.  That is the assertion the poster
     * needs: a poster replaces a canvas upload, so its corner words have to be the words the
     * recorded stream would have put at the same pixel, over the whole range a canvas can carry.
     * 0x8000 at 0 px, 0xB200 at 800 px, and up to the field's own limit. */
    for (int px = 0; px <= 2047; px++) {
        const uint32_t a = gs_vertex_coord16(px);
        const uint32_t b = coord_float_ref(px);
        if (a != b) {
            if (!worst_diff) {
                printf("  [FAIL] coord differs at px=%d shift=0x%x float=0x%x\n",
                       px, (unsigned int)a, (unsigned int)b);
                worst_diff = 1;
            }
        }
        n++;
    }
    s_checks++;
    if (worst_diff) s_failures++;
    eq32("CONTROL coords compared, shift against the float it replaced", (uint32_t)n, 2048u);

    /* The field width, stated once: a coordinate is 16 bits and the page offset sits at its
     * middle, so 0 px is 0x8000 and not 0.  A poster that skipped the fold would draw its rects
     * 2048 px above the canvas it is replacing. */
    eq32("coord(0) is the page origin", gs_vertex_coord16(0), 0x8000u);
    eq32("coord(2047) is the last one", gs_vertex_coord16(2047), 0xFFF0u);
}

/* ── Two rects: stride, and the count a refusal leaves behind ──────────── */

static void two_rects(void)
{
    const gs_poster_rect r[2] = { { 0, 0, 800, 16 }, { 784, 599, 800, 600 } };
    uint64_t qw[2u * 16u];
    int32_t got;

    for (unsigned i = 0u; i < 32u; i++) qw[i] = 0xDEADBEEFDEADBEEFULL;
    got = gs_poster_packet(r, 2u, TEAL, qw, gs_poster_qwc(2u));
    eq32("two rects return their own size", (uint32_t)got, 5u);
    /* NLOOP is now the number of rects in the packet, and one DMA carries both: 2<<0 with the
     * rest of the tag unchanged.  This is also the first place the census's remaining unknown is
     * written down as a literal -- NLOOP>1 in a dense REGLIST group is what rung 5's verdict makes
     * the poster emit, and ps2sdk's own batching does it, though this GS has not been asked yet. */
    eq("tag: NLOOP=2", qw[0], 0x4400000000008002ULL);

    /* The second rect begins after two whole GIF quadwords, i.e. at uint64 index 2 + 4.  A stride
     * off by one register is a sprite whose colour is its corner and whose corner is the next
     * sprite's PRIM, and it draws nothing anywhere near where the caller meant. */
    eq("rect 1 data 0 low: PRIM", qw[6], 0x6ULL);
    eq("rect 1 data 0 high: RGBAQ", qw[7], 0x3F800000FF808000ULL);
    /* 784*16 = 12544 = 0x3100 + 0x8000 = 0xB100 ; 599*16 = 9584 = 0x2570 + 0x8000 = 0xA570
     * 800*16 = 0x3200 + 0x8000 = 0xB200 ; 600*16 = 9600 = 0x2580 + 0x8000 = 0xA580 */
    eq("rect 1 data 1 low: XYZ2 784,599", qw[8], 0x00000000A570B100ULL);
    eq("rect 1 data 1 high: XYZ2 800,600", qw[9], 0x00000000A580B200ULL);
    eq("and the batch ends there", qw[10], 0xDEADBEEFDEADBEEFULL);
    eq32("dropped: none", gs_poster_dropped(), 0u);

    /* CONTROL: a degenerate rect must vanish from NLOOP as well as from the buffer.  This is the
     * assertion that keeps a QWC from exceeding the packet: the tag's count and the quadwords that
     * follow it are the same number or channel 2 reads past the end of the batch and decodes the
     * overflow as another tag. */
    {
        const gs_poster_rect bad[3] = { { 0, 0, 800, 16 }, { 10, 10, 10, 20 }, { 16, 32, 300, 300 } };
        got = gs_poster_packet(bad, 3u, TEAL, qw, gs_poster_qwc(3u));
        eq32("a refused rect shrinks the packet", (uint32_t)got, 1u + 2u * 2u);
        eq("tag: NLOOP=2 after one refusal", qw[0], 0x4400000000008002ULL);
        eq32("dropped counts the refusal", gs_poster_dropped(), 1u);
        /* The survivor that follows the refusal is written at the SECOND slot, not the third: the
         * batch compacts, so the rects on the bus are the rects the tag counted.  Its own corners
         * are what says so -- (16,32) is neither rect's and appears nowhere else in this packet.
         * 16*16 = 0x100 + 0x8000 = 0x8100, 32*16 = 0x200 + 0x8000 = 0x8200; 300*16 = 0x12C0 +
         * 0x8000 = 0x92C0. */
        eq("compacted: slot 1 is the 16,32..300,300 rect", qw[8], 0x0000000082008100ULL);
        eq("compacted: and its second corner", qw[9], 0x0000000092C092C0ULL);
    }

    /* Zero surviving rects is not an empty packet: a tag with NLOOP 0 is malformed, so the answer
     * is -1 and the caller posts nothing. */
    {
        const gs_poster_rect all_bad[2] = { { 5, 5, 5, 5 }, { 9, 9, 8, 8 } };
        for (unsigned i = 0u; i < 24u; i++) qw[i] = 0u;
        got = gs_poster_packet(all_bad, 2u, TEAL, qw, gs_poster_qwc(2u));
        eq32("nothing drawable is refused", (uint32_t)(int32_t)got, 0xFFFFFFFFu);
        eq32("both refusals counted", gs_poster_dropped(), 2u);
    }
}

/* ── The limits a caller can hit ───────────────────────────────────────── */

static void limits(void)
{
    const gs_poster_rect r[1] = { { 0, 0, 800, 16 } };
    uint64_t qw[2u * 12u];
    int32_t got;

    /* A short buffer is refused BEFORE the buffer is written, because a half-built group is a
     * picture with a hole in it and no receipt anywhere. */
    for (unsigned i = 0u; i < 24u; i++) qw[i] = 0xDEADBEEFDEADBEEFULL;
    got = gs_poster_packet(r, 1u, TEAL, qw, gs_poster_qwc(1u) - 1u);
    eq32("short buffer refused", (uint32_t)(int32_t)got, 0xFFFFFFFFu);
    eq("short buffer left the caller's bytes alone", qw[0], 0xDEADBEEFDEADBEEFULL);
    eq("and did not write the last word either", qw[5], 0xDEADBEEFDEADBEEFULL);

    /* A corner at the page's own origin wraps the 16-bit field: the guard refuses it, which is
     * deliberately stricter than gs_vertex_coord16(), and the host has no screen to discover the
     * difference with. */
    {
        const gs_poster_rect far[1] = { { 0, 0, 2048, 16 } };
        got = gs_poster_packet(far, 1u, TEAL, qw, gs_poster_qwc(1u));
        eq32("a corner at the page origin is refused", (uint32_t)(int32_t)got, 0xFFFFFFFFu);
        eq32("and counted", gs_poster_dropped(), 1u);
    }
    {
        const gs_poster_rect just_inside[1] = { { 0, 0, 2047, 16 } };
        got = gs_poster_packet(just_inside, 1u, TEAL, qw, gs_poster_qwc(1u));
        eq32("one pixel less draws", (uint32_t)got, 3u);
        /* 2047*16 = 32752 = 0x7FF0 + 0x8000 = 0xFFF0: still inside the field, still on this side
         * of the page. */
        eq("its corner word", qw[5], 0x000000008100FFF0ULL);
    }

    /* Negative origins are refused rather than clipped: clipping here would need the clip the
     * caller's environment sets, and a packet builder that guesses at a scissor is how a fill ends
     * up outside the rect it was paying for. */
    {
        const gs_poster_rect neg[1] = { { -4, 0, 800, 16 } };
        got = gs_poster_packet(neg, 1u, TEAL, qw, gs_poster_qwc(1u));
        eq32("a negative corner is refused", (uint32_t)(int32_t)got, 0xFFFFFFFFu);
    }

    /* Null arguments, including n=0: the caller that has nothing to posterise must not build a
     * packet, and must not be handed a size to post by mistake. */
    eq32("null rects refused", (uint32_t)(int32_t)gs_poster_packet(0, 1u, TEAL, qw, 64u), 0xFFFFFFFFu);
    eq32("null buffer refused", (uint32_t)(int32_t)gs_poster_packet(r, 1u, TEAL, 0, 64u), 0xFFFFFFFFu);
    eq32("n=0 refused", (uint32_t)(int32_t)gs_poster_packet(r, 0u, TEAL, qw, 64u), 0xFFFFFFFFu);

    /* The drop counter is per call, so a caller's `drop=` column cannot inherit yesterday's. */
    {
        const gs_poster_rect bad[1] = { { 1, 1, 1, 1 } };
        (void)gs_poster_packet(bad, 1u, TEAL, qw, gs_poster_qwc(1u));
        eq32("a call with a drop says so", gs_poster_dropped(), 1u);
        (void)gs_poster_packet(r, 1u, TEAL, qw, gs_poster_qwc(1u));
        eq32("the next call starts clean", gs_poster_dropped(), 0u);
    }
}

/* ── The 119-rect batch the measurement says the background needs ──────── */

static void full_background(void)
{
    /* [BENCH] poster price: read 13 flat row-bands and 119 changes of span on 2026-10-14, so a
     * whole-canvas poster is a batch of roughly that many rects.  The point of building it here is
     * the arithmetic a real present depends on: the buffer, the QWC the DMA takes, and the bytes
     * that cross channel 2, all of which are compared with counts derived from the rect number
     * rather than from what the loop happened to write. */
    gs_poster_rect r[128];
    uint64_t qw[2u * GS_POSTER_QWC(128u)];
    uint32_t i;
    int32_t got;

    for (i = 0u; i < 119u; i++) {
        r[i].x0 = (int32_t)(i % 4u) * 200;
        r[i].y0 = (int32_t)i;
        r[i].x1 = r[i].x0 + 200;
        r[i].y1 = (int32_t)i + 1;
    }
    /* Sentinels across the whole buffer first, so the "and the batch ends on it" row below is a
     * measurement of the packet's length rather than a read of uninitialised stack. */
    for (i = 0u; i < 2u * GS_POSTER_QWC(128u); i++) qw[i] = 0xDEADBEEFDEADBEEFULL;
    got = gs_poster_packet(r, 119u, TEAL, qw, gs_poster_qwc(119u));
    eq32("119 rects all drawn", (uint32_t)got, 239u);
    eq("tag: NLOOP=119", qw[0] & 0x7FFFu, 119u);
    eq32("none dropped", gs_poster_dropped(), 0u);
    /* The two numbers the DMA takes are one fact, not two: the QWC the caller hands channel 2 comes
     * from the return value, the loop count the GIF counts comes from the tag, and the dense stride
     * is what keeps them the same arithmetic -- 1 + 2 * NLOOP quadwords, exactly. */
    eq32("qwc is 1 + 2 * the tag's NLOOP", (uint32_t)got, 1u + 2u * (uint32_t)(qw[0] & 0x7FFFu));
    /* 239 GIF quadwords = 3,824 bytes, against the 6,798 us this batch is there to take off `bg=`
     * (15.7 ns/px x 433,001 px).  The ratio is the lane's whole argument in two numbers, and it is
     * why the batch is one transfer: 119 separate starts would spend the saving on the queueing.
     * Half of this size is rung 5's fix arriving free -- the sparse group this replaces put 477
     * quadwords on the same bus for the same 119 sprites. */
    eq32("bytes in the batch", (uint32_t)(got * 16u), 3824u);
    /* Every rect of the batch shares the one colour word, and the last rect's second corner is
     * where a stride bug would show up first: i=118 gives y1=119 and x1 = 0*200+200 or similar. */
    eq("last rect keeps the colour", qw[2u + 4u * 118u + 1u], 0x3F800000FF808000ULL);
    /* x0=400 -> 0x9900, y0=118 -> 0x8760 ; x1=600 -> 0xA580, y1=119 -> 0x8770 (each *16 + 0x8000).
     * Both corners of the LAST rect, at the LAST two 64-bit slots of the batch: a per-rect stride
     * that drifts by anything lands here first. */
    eq("last rect's own first corner", qw[2u + 4u * 118u + 2u], 0x0000000087609900ULL);
    eq("last rect's drawing kick", qw[2u + 4u * 118u + 3u], 0x000000008770A580ULL);
    eq("and the batch ends on it", qw[2u + 4u * 119u], 0xDEADBEEFDEADBEEFULL);
}

/* ── LANE 19 rung 4: the reference-shaped packets, against the SDK's own words ─────────
 *
 * These builders exist because the whole-page census (rung 3) found no pixel of either magic colour
 * anywhere in page 0, which left three variables standing inside one packet: the GIF format (FLG with
 * PRE=1), the sprite's corner nudge, and the SPRITE primitive itself.  What decides between them is a
 * packet that a shipped PS2 program is known to draw with, so the expectations below are literals
 * hand-derived from third_party/ps2sdk/ee/draw/src/draw2d.c -- its GIF_SET_TAG(NLOOP,EOP,PRE,PRIM,
 * FLG,NREG) with FLG=GIF_FLG_REGLIST (1) and NREG 4 / 6, its DRAW_SPRITE_REGLIST and
 * DRAW_TRIANGLE_REGLIST nibble lists, and its two dense data quadwords / three -- and the code under
 * test did not compute any of them.  The one deviation from the SDK's text is EOP, which the header
 * explains: a control here is a DMA of its own, so its tag ends it.
 *
 * The census answered for those three on 2026-10-16: cyan=7360/7360 at bbox 64,544..223,589,
 * magenta=7567 at 240,544..400,590, green=3525 at 434,544..590,588, and red=0, blue=0 over the whole
 * 480,000-px page.  Every SDK-shaped packet drew; the port's did not.  That narrowed the difference
 * to two things that had been living in one sentence -- the tag and the STRIDE -- and the follow-up
 * run settled which, by failing: this port's PACKED/PRE=1 tag over the SDK's dense two-quadword body
 * left the GIF waiting for data that never came, and the three GIF reads after it all returned
 * `block-never-finished`.  A PACKED group with NREG=4 therefore wants FOUR quadwords, one per
 * register, which is exactly what gs_poster_packet() writes: the stride was never the defect, the
 * shipping packet is well-formed for its own tag, and the variable still standing is the shape of the
 * group -- a four-register REGS list with PRE=1 -- against the SDK's OTHER precedent, a PACKED group
 * of single-register AD writes, which is the form send_trx_setup_at() at ps2_gs.c:220 uses for every
 * console upload this port has ever put on the screen.
 *
 * The last sentence was rung 5's to break, and it broke in favour of the group rather than the bit:
 * the same four-register PACKED body sent again with PRE cleared put zero pixels of its own colour
 * anywhere on the page, while the identical four values densely packed as REGLIST filled their box
 * exactly (orange=0/1840 against cyan=7360/7360, with all 480,000 page pixels accounted for).  PRE
 * was innocent, so a multi-register FLG=PACKED group never kicks on this GS, and gs_poster_packet()
 * now emits CYAN's shape.  Every expectation below is re-derived for that emitter, and the PACKED
 * builder stays in the tree as the census's negative control -- a packet expected to draw nothing,
 * forever, and gated to keep differing from what ships.
 *
 * So the CONTROL assertions below come in two pairs, and rung 5 rewrote what the first pair says.  It
 * used to hold the shipping poster and the SDK's shape apart by FLG and PRE so that a census which
 * separated them was a statement about the tag; the census did separate them, the tag lost, and
 * gs_poster_packet() now emits the SDK's shape -- so the first pair is the stronger claim that they
 * are ONE packet, word for word, which is what lets this port's flat fill be quoted as the shape a
 * shipped PS2 program is known to draw with.  The second pair (AD against REGLIST) is unchanged: the
 * four register VALUES of the AD packet equal the dense packet's, and only their delivery -- one per
 * quadword with the GP address above each, instead of two to a quadword with a REGS list -- differs.
 * A gate that cannot show that equality would let the AD builder drift into a second model of the
 * sprite and the census's YELLOW row would stop being one bit of an answer.
 */
static void ref_packets(void)
{
    const gs_poster_rect r[1] = { { 0, 0, 800, 16 } };
    uint64_t ref[2u * GS_REF_AD_QWC], ad[2u * GS_REF_AD_QWC], post[2u * 8u];
    uint64_t pk[2u * GS_REF_PACKED_QWC], pk0[2u * GS_REF_PACKED_QWC];
    int32_t got;

    for (unsigned i = 0u; i < 2u * GS_REF_AD_QWC; i++) {
        ref[i] = 0xDEADBEEFDEADBEEFULL;
        ad[i] = 0xDEADBEEFDEADBEEFULL;
    }
    for (unsigned i = 0u; i < 2u * GS_REF_PACKED_QWC; i++) {
        pk[i] = 0xDEADBEEFDEADBEEFULL;
        pk0[i] = 0xDEADBEEFDEADBEEFULL;
    }

    /* FLG=REGLIST is 1, so the tag is NLOOP 1 (0x1) + EOP (0x8000) + FLG 1<<58 (0x0400000000000000)
     * + NREG 4<<60 (0x4000000000000000) = 0x4400000000008001, and PRE is CLEAR: the primitive word
     * arrives as the group's first register, which is the SDK's shape -- and, since rung 5's fix, the
     * poster's shape too, which the equality block below is written to prove. */
    got = gs_poster_ref_sprite_packet(r, TEAL, 0u, ref, GS_REF_SPRITE_QWC);
    eq32("ref sprite size", (uint32_t)got, 3u);
    eq("ref tag: REGLIST, NREG 4, PRE 0", ref[0], 0x4400000000008001ULL);
    eq("ref regs: same four nibbles", ref[1], 0x5510ULL);
    eq("ref data: PRIM", ref[2], 0x6ULL);
    eq("ref data: RGBAQ shares the quadword", ref[3], 0x3F800000FF808000ULL);
    eq("ref data: XYZ2 both corners", ref[4], 0x0000000080008000ULL);
    eq("ref data: and the second one", ref[5], 0x000000008100B200ULL);

    /* CONTROL, first pair: the same rect through the shipping emitter.  Before rung 5 these two
     * packets differed by FLG, by PRE and by stride, and the equalities here were only about the four
     * VALUES so that a census separating the two was a statement about the format.  The census
     * separated them, the format lost, and the emitter changed -- so this block is now the strongest
     * sentence the gate can write: what this port posts for a flat fill IS, word for word, what
     * draw2d.c posts for a sprite on a real PS2, all six 64-bit halves and the length as well. */
    got = gs_poster_packet(r, 1u, TEAL, post, gs_poster_qwc(1u));
    eq32("CONTROL the poster and the SDK sprite are the same length", gs_poster_qwc(1u),
         GS_REF_SPRITE_QWC);
    {
        uint32_t same = 1u;
        for (unsigned i = 0u; i < 2u * GS_REF_SPRITE_QWC; i++) {
            if (ref[i] != post[i]) same = 0u;
        }
        eq32("CONTROL and every word of the packet agrees", same, 1u);
    }

    /* The AD form, spelled from ps2_gs.c:229 rather than from draw2d.c: NLOOP 4 because the group is
     * four single-register loops, EOP for a transfer of its own, PRE clear because the primitive
     * arrives as a register write, FLG=PACKED, NREG 1<<60 = 0x1000000000000000, so the tag's low half
     * is 0x1000000000008004 and its REGS half is the one AD nibble 0xE -- the same two words
     * send_trx_setup_at() writes before its BITBLTBUF/TRXPOS/TRXREG/TRXDIR quartet. */
    got = gs_poster_ref_sprite_ad_packet(r, TEAL, 0u, ad, GS_REF_AD_QWC);
    eq32("AD sprite size", (uint32_t)got, 5u);
    eq("AD tag: PACKED, NREG 1, PRE 0", ad[0], 0x1000000000008004ULL);
    eq("AD regs: one AD nibble", ad[1], 0xEULL);
    /* The four GP addresses above the values: GS_GP_PRIM 0, GS_GP_RGBAQ 1, GS_GP_XYZ2 5 twice, and
     * the second XYZ2 is what kicks the drawing.  These are NOT padding zeros, which is the whole
     * difference between an AD quadword and this file's retired PACKED form. */
    eq("AD data: PRIM over its address", ad[2], 0x6ULL);
    eq("AD address: PRIM", ad[3], 0x0ULL);
    eq("AD data: RGBAQ", ad[4], 0x3F800000FF808000ULL);
    eq("AD address: RGBAQ", ad[5], 0x1ULL);
    eq("AD data: first corner", ad[6], 0x0000000080008000ULL);
    eq("AD address: XYZ2", ad[7], 0x5ULL);
    eq("AD data: second corner", ad[8], 0x000000008100B200ULL);
    eq("AD address: XYZ2 again", ad[9], 0x5ULL);

    /* CONTROL, second pair: the four VALUES are the dense packet's, unchanged, so the census's CYAN
     * and YELLOW counts differ by the group's shape and by nothing a coordinate could do. */
    eq("CONTROL AD and REGLIST carry the same PRIM", ad[2], ref[2]);
    eq("CONTROL the same colour", ad[4], ref[3]);
    eq("CONTROL the same first corner", ad[6], ref[4]);
    eq("CONTROL the same second corner", ad[8], ref[5]);
    /* And the falsifying half of the stride law this run proved: the dense packet's RGBAQ shares a
     * quadword with PRIM, the AD packet's cannot, because its own quadword's high half is an address.
     * Were both builders to drift to one layout, these two rows would agree and the control above
     * would compare a packet against itself. */
    eq("CONTROL the two layouts really differ", ad[3] != ref[3], 1u);

    /* The nudge: draw2d.c's START_OFFSET 2047.5625f and END_OFFSET 2048.5625f, i.e. the page origin
     * minus 7 and plus 9 in the field's own 1/16-pixel units.  0x8000-7 = 0x7FF9 and
     * 0xB200+9 = 0xB209, 0x8100+9 = 0x8109. */
    got = gs_poster_ref_sprite_packet(r, TEAL, 1u, ref, GS_REF_SPRITE_QWC);
    eq32("nudged sprite same size", (uint32_t)got, 3u);
    eq("nudged first corner -7/16 px", ref[4], 0x000000007FF97FF9ULL);
    eq("nudged second corner +9/16 px", ref[5], 0x000000008109B209ULL);
    /* One copy of that arithmetic for both shapes: the AD packet with nudge=1 has to land on the same
     * two corner words, or YELLOW would be testing the coordinates as well as the delivery. */
    got = gs_poster_ref_sprite_ad_packet(r, TEAL, 1u, ad, GS_REF_AD_QWC);
    eq32("nudged AD same size", (uint32_t)got, 5u);
    eq("CONTROL both shapes nudge alike, low corner", ad[6], ref[4]);
    eq("CONTROL and high corner", ad[8], ref[5]);

    /* ── rung 5: the retired shape, kept as the census's negative control ────────────────────
     *
     * The three runs above left one sentence standing between the packet that draws and the packet
     * that ships: a four-register REGS list carried by PRE=1.  Two decisions in one, so this builder
     * split them -- FLG=PACKED, NREG=4, the same four nibbles, FOUR sparse data quadwords (the length
     * the hung dense-under-PACKED control proved this tag asks for), and `pre` as the only argument
     * the caller changes.  The census read it on 2026-10-16: zero pixels of this packet's own colour
     * anywhere on a page whose 480,000 pixels were every one of them otherwise accounted for, at BOTH
     * values of `pre`.  PRE was the bit that did nothing, the group's shape was the defect, and
     * gs_poster_packet() moved to CYAN's dense REGLIST form -- which leaves this builder as the shape
     * that no longer ships, and therefore as the one thing the census has to keep finding at zero.
     * The literals are the old poster's own, hand-derived: NLOOP 1 (0x1) + EOP (0x8000) + PRE 1<<46
     * (0x0000400000000000) + PRIM 6<<47 (0x0003000000000000) + FLG PACKED (0<<58) + NREG 4<<60
     * (0x4000000000000000) = 0x4003400000008001; clearing PRE gives 0x4003000000008001. */
    got = gs_poster_ref_sprite_packed_packet(r, TEAL, 1u, pk, GS_REF_PACKED_QWC);
    eq32("packed sprite size", (uint32_t)got, 5u);
    eq("packed tag: PACKED, NREG 4, PRE 1", pk[0], 0x4003400000008001ULL);
    eq("packed regs: the poster's four nibbles", pk[1], 0x5510ULL);
    eq("packed data: PRIM", pk[2], 0x6ULL);
    eq("packed data: and its padded high half", pk[3], 0u);
    eq("packed data: RGBAQ", pk[4], 0x3F800000FF808000ULL);
    eq("packed data: first corner", pk[6], 0x0000000080008000ULL);
    eq("packed data: second corner", pk[8], 0x000000008100B200ULL);

    /* PRE alone, exactly as it was measured: two tags whose XOR is the PRE bit and nothing else, with
     * the whole rest of the packet byte-identical.  That equality is what made the census's orange
     * row a statement about one argument rather than about the packet; had the builder taken `pre`
     * anywhere else -- into the REGS word, into a corner -- the reading would have been about a
     * different packet entirely, and the answer "PRE is innocent" would have been unearned. */
    got = gs_poster_ref_sprite_packed_packet(r, TEAL, 0u, pk0, GS_REF_PACKED_QWC);
    eq32("pre=0 same size", (uint32_t)got, 5u);
    eq("pre=0 tag: PRE clear", pk0[0], 0x4003000000008001ULL);
    eq("CONTROL pre toggles ONE bit, 1<<46", pk[0] ^ pk0[0], 1ULL << 46);
    {
        uint32_t same = 1u;
        for (unsigned i = 1u; i < 2u * GS_REF_PACKED_QWC; i++) {
            if (pk[i] != pk0[i]) same = 0u;
        }
        eq32("CONTROL and nothing else in the packet", same, 1u);
    }

    /* The falsifying half of the FIX rather than of a hypothesis.  What ships must differ from this
     * retired packet in PRE, in the tag's own PRIM field and in FLG -- three bits, all inside the tag,
     * so the difference is one literal: 1<<46 + 6<<47 + 1<<58 = 0x0403400000000000 -- and in the
     * STRIDE, where the word after PRIM is zero padding here and RGBAQ in the shipping packet.  If
     * these two ever agree word for word again, the emitter has been reverted to the shape that draws
     * nothing while this gate still passes, which is the exact regression rung 5 was built to make
     * impossible; the census's VIOLET row is the runtime half of the same claim. */
    eq("CONTROL retired vs shipping tag: PRE, PRIM and FLG", pk[0] ^ post[0],
       0x0403400000000000ULL);
    eq("CONTROL and the retired body pads where the dense body carries RGBAQ",
       pk[3] != post[3], 1u);
    eq32("CONTROL the retired shape spends two more quadwords per sprite",
         GS_REF_PACKED_QWC - gs_poster_qwc(1u), 2u);

    /* And the three shapes still kept apart: this tag is not CYAN's and not the shipping one, and its
     * body is not YELLOW's -- a PACKED/NREG=4 group's high half is padding where an AD group's is a
     * register address, which is the distinction the hung control paid a run to establish. */
    eq("CONTROL PACKED/PRE=1 is not the REGLIST tag", pk[0] != 0x4400000000008001ULL, 1u);
    eq("CONTROL PACKED/NREG 4 pads where PACKED/NREG 1 addresses", pk[5] != ad[5], 1u);

    /* The triangle: NREG 6, so the list is PRIM 0, RGBAQ 1, three XYZ2 5 and a NOP 0xF in nibble
     * five -- 0xF55510 -- and the tag's NREG field is 6<<60 = 0x6000000000000000, so 0x6400000000008001.
     * Three vertices make the drawing kick on the third, which is what this shape is for. */
    {
        const int32_t v[6] = { 0, 0, 800, 16, 400, 600 };
        for (unsigned i = 0u; i < 8u; i++) ref[i] = 0xDEADBEEFDEADBEEFULL;
        got = gs_poster_ref_tri_packet(v, TEAL, ref, GS_REF_TRI_QWC);
        eq32("ref triangle size", (uint32_t)got, 4u);
        eq("ref tri tag: NREG 6", ref[0], 0x6400000000008001ULL);
        eq("ref tri regs: PRIM,RGBAQ,XYZ2x3,NOP", ref[1], 0xF55510ULL);
        eq("ref tri data: PRIM is OP 3", ref[2], 0x3ULL);
        eq("ref tri data: RGBAQ", ref[3], 0x3F800000FF808000ULL);
        eq("ref tri data: vertex 0", ref[4], 0x0000000080008000ULL);
        eq("ref tri data: vertex 1", ref[5], 0x000000008100B200ULL);
        /* 400*16 = 6400 = 0x1900 + 0x8000 = 0x9900; 600*16 = 9600 = 0x2580 + 0x8000 = 0xA580 */
        eq("ref tri data: vertex 2", ref[6], 0x00000000A5809900ULL);
        /* The NOP's data word, which draw2d.c:141 writes as 0 and which the packer's cap of four
         * nibbles would have deleted along with the third vertex. */
        eq("ref tri data: the NOP's word", ref[7], 0u);
    }

    /* A six-nibble list is the reason gs_pack_regs() learned that the REGS field is ten nibbles
     * wide, and this is the falsifying half: at the old cap of four the list would have read 0x5510,
     * i.e. a triangle of three registers, and the primitive would never have kicked. */
    eq("CONTROL six nibbles survive the packer", gs_pack_regs((const uint32_t[6]){0,1,5,5,5,0xF}, 6u),
       0xF55510ULL);

    /* The refusals, each for a reason a control cannot afford: a packet that draws nothing looks
     * exactly like a format that rasterises nothing.  They are stated for both sprite shapes because
     * the two builders share the rect check and the refusal law -- a gate that only covered one of
     * them would let the other hand the census a zero-pixel control that reads as a negative. */
    for (unsigned i = 0u; i < 8u; i++) ref[i] = 0u;
    {
        const gs_poster_rect flat[1] = { { 100, 100, 100, 160 } };
        eq32("an empty rect is refused",
             (uint32_t)(int32_t)gs_poster_ref_sprite_packet(flat, TEAL, 1u, ref, GS_REF_SPRITE_QWC),
             0xFFFFFFFFu);
        eq32("and nothing was written", ref[0], 0u);
        eq32("the same rect is refused in the AD shape",
             (uint32_t)(int32_t)gs_poster_ref_sprite_ad_packet(flat, TEAL, 0u, ref, GS_REF_AD_QWC),
             0xFFFFFFFFu);
        eq32("and it too wrote nothing", ref[0], 0u);
        /* rung 5's retired shape shares the same law, and after the fix the reason is sharper still:
         * this builder is the census's NEGATIVE control, so its expected row is zero pixels -- and a
         * degenerate rect that slipped through here would make that zero a fact about an empty area
         * rather than about a dead group, which is exactly the confusion a negative control must not
         * leave room for. */
        eq32("the same rect is refused in the PACKED shape",
             (uint32_t)(int32_t)gs_poster_ref_sprite_packed_packet(flat, TEAL, 0u, ref,
                                                                  GS_REF_PACKED_QWC),
             0xFFFFFFFFu);
        eq32("and it too wrote nothing", ref[0], 0u);
    }
    {
        const int32_t line[6] = { 0, 0, 100, 100, 200, 200 };
        eq32("collinear vertices are refused",
             (uint32_t)(int32_t)gs_poster_ref_tri_packet(line, TEAL, ref, GS_REF_TRI_QWC),
             0xFFFFFFFFu);
        eq32("and nothing was written", ref[0], 0u);
    }
    {
        const int32_t off[6] = { 0, 0, 2048, 16, 400, 600 };
        eq32("a corner past the page origin is refused",
             (uint32_t)(int32_t)gs_poster_ref_tri_packet(off, TEAL, ref, GS_REF_TRI_QWC),
             0xFFFFFFFFu);
    }
    {
        const gs_poster_rect rr[1] = { { 0, 0, 800, 16 } };
        uint64_t short_buf[8];
        for (unsigned i = 0u; i < 8u; i++) short_buf[i] = 0xDEADBEEFDEADBEEFULL;
        eq32("a short buffer is refused",
             (uint32_t)(int32_t)gs_poster_ref_sprite_packet(rr, TEAL, 0u, short_buf,
                                                           GS_REF_SPRITE_QWC - 1u),
             0xFFFFFFFFu);
        /* The AD packet is the longer of the two, so this is the refusal that catches a builder that
         * sizes its cap by the dense form's macro -- five quadwords asked for as four. */
        eq32("a short buffer is refused for AD too",
             (uint32_t)(int32_t)gs_poster_ref_sprite_ad_packet(rr, TEAL, 0u, short_buf,
                                                              GS_REF_AD_QWC - 1u),
             0xFFFFFFFFu);
        eq("a short buffer keeps its own bytes", short_buf[0], 0xDEADBEEFDEADBEEFULL);
        eq32("null arguments refused",
             (uint32_t)(int32_t)gs_poster_ref_sprite_packet(0, TEAL, 0u, ref, 8u), 0xFFFFFFFFu);
        eq32("and for the AD builder",
             (uint32_t)(int32_t)gs_poster_ref_sprite_ad_packet(0, TEAL, 0u, ref, 8u), 0xFFFFFFFFu);
        eq32("and for the triangle",
             (uint32_t)(int32_t)gs_poster_ref_tri_packet(0, TEAL, ref, 8u), 0xFFFFFFFFu);
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS poster batch vs hand-computed packet words\n");
    printf("==========================================================\n");

    sizes();
    one_rect();
    colour_control();
    coord_control();
    two_rects();
    limits();
    full_background();
    ref_packets();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - a poster word is not what the field table says\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - the flat SPRITE batch matches its bit table, its colour and its corners\n");
    return 0;
}
