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
    /* One tag pair, then four GIF quadwords per rect.  Both spellings of the count have to agree,
     * because the caller sizes its buffer with one and the packet is checked against the other. */
    eq32("qwc(1)", gs_poster_qwc(1u), 5u);
    eq32("qwc(2)", gs_poster_qwc(2u), 9u);
    eq32("qwc(119)", gs_poster_qwc(119u), 1u + 4u * 119u);
    eq32("macro(1) == fn(1)", GS_POSTER_QWC(1u), gs_poster_qwc(1u));
    /* A batch of nothing is not a packet, and a caller that sized a buffer for 0 quadwords and
     * then posted 1 would put the tag's own bytes on the bus as data. */
    eq32("qwc(0) is nothing", gs_poster_qwc(0u), 0u);
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

    eq32("one rect returns its own size", (uint32_t)got, 5u);

    /* GIFTAG, NLOOP=1 / EOP=1 / PRE=1 / PRIM=6 / FLG=PACKED / NREG=4:
     *   1<<0 | 1<<15 | 1<<46 | 6<<47 | 0<<58 | 4<<60
     * = 0x0000000000000001 + 0x8000 + 0x0000400000000000 + 0x0003000000000000 + 0x4000000000000000 */
    eq("tag: NLOOP/EOP/PRE/PRIM/FLG/NREG", qw[0], 0x4003400000008001ULL);
    /* REGS list, PRIM 0x0 then RGBAQ 0x1 then XYZ2 0x5 then XYZ2 0x5, lowest nibble first:
     * 0x0 + (1<<4) + (5<<8) + (5<<12) = 0x10 + 0x500 + 0x5000 */
    eq("regs: PRIM,RGBAQ,XYZ2,XYZ2", qw[1], 0x5510ULL);

    /* PRIM's own data word: OP=6 with IIP, TME, ABE, CTXT and FIX all clear, so the sprite is
     * flat-shaded, untextured and unblended -- which is the entire reason a poster costs no page. */
    eq("data 0: PRIM sprite flat", qw[2], 0x6ULL);
    eq("data 0: high half is zero", qw[3], 0u);

    /* RGBAQ.  Low 32 bits are the canvas word UNTOUCHED, high 32 are Q=1.0f's own bits
     * (0x3f800000).  This is the byte-order law as a single equality: the poster and the upload
     * that it replaces must put the same 32 bits in the same VRAM word. */
    eq("data 1: RGBAQ == canvas px | Q", qw[4], 0x3F800000FF808000ULL);
    eq("data 1: high half is zero", qw[5], 0u);

    /* XYZ2 corners: X[15:0] Y[31:16], each 12.4 with the page at 2048 px folded in.
     * (0,0)   -> 0x8000 , 0x8000
     * (800,16)-> 800*16 = 12800 = 0x3200 + 0x8000 = 0xB200 ; 16*16 = 0x100 + 0x8000 = 0x8100 */
    eq("data 2: XYZ2 top-left", qw[6], 0x0000000080008000ULL);
    eq("data 2: high half is zero", qw[7], 0u);
    eq("data 3: XYZ2 bottom-right", qw[8], 0x000000008100B200ULL);
    eq("data 3: high half is zero", qw[9], 0u);

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
    eq("CONTROL colour 0x12345678 -> RGBAQ", qw[4], 0x3F80000012345678ULL);

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
    eq32("two rects return their own size", (uint32_t)got, 9u);
    /* NLOOP is now the number of rects in the packet, and one DMA carries both: 2<<0 with the
     * rest of the tag unchanged. */
    eq("tag: NLOOP=2", qw[0], 0x4003400000008002ULL);

    /* The second rect begins after four whole GIF quadwords, i.e. at uint64 index 2 + 8.  A stride
     * off by one register is a sprite whose colour is its corner and whose corner is the next
     * sprite's PRIM, and it draws nothing anywhere near where the caller meant. */
    eq("rect 1 data 0: PRIM", qw[10], 0x6ULL);
    eq("rect 1 data 1: RGBAQ", qw[12], 0x3F800000FF808000ULL);
    /* 784*16 = 12544 = 0x3100 + 0x8000 = 0xB100 ; 599*16 = 9584 = 0x2570 + 0x8000 = 0xA570
     * 800*16 = 0x3200 + 0x8000 = 0xB200 ; 600*16 = 9600 = 0x2580 + 0x8000 = 0xA580 */
    eq("rect 1 data 2: XYZ2 784,599", qw[14], 0x00000000A570B100ULL);
    eq("rect 1 data 3: XYZ2 800,600", qw[16], 0x00000000A580B200ULL);
    eq32("dropped: none", gs_poster_dropped(), 0u);

    /* CONTROL: a degenerate rect must vanish from NLOOP as well as from the buffer.  This is the
     * assertion that keeps a QWC from exceeding the packet: the tag's count and the quadwords that
     * follow it are the same number or channel 2 reads past the end of the batch and decodes the
     * overflow as another tag. */
    {
        const gs_poster_rect bad[3] = { { 0, 0, 800, 16 }, { 10, 10, 10, 20 }, { 16, 32, 300, 300 } };
        got = gs_poster_packet(bad, 3u, TEAL, qw, gs_poster_qwc(3u));
        eq32("a refused rect shrinks the packet", (uint32_t)got, 1u + 4u * 2u);
        eq("tag: NLOOP=2 after one refusal", qw[0], 0x4003400000008002ULL);
        eq32("dropped counts the refusal", gs_poster_dropped(), 1u);
        /* The survivor that follows the refusal is written at the SECOND slot, not the third: the
         * batch compacts, so the rects on the bus are the rects the tag counted.  Its own corners
         * are what says so -- (16,32) is neither rect's and appears nowhere else in this packet.
         * 16*16 = 0x100 + 0x8000 = 0x8100, 32*16 = 0x200 + 0x8000 = 0x8200; 300*16 = 0x12C0 +
         * 0x8000 = 0x92C0. */
        eq("compacted: slot 1 is the 16,32..300,300 rect", qw[14], 0x0000000082008100ULL);
        eq("compacted: and its second corner", qw[16], 0x0000000092C092C0ULL);
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
    eq("and did not write the last word either", qw[9], 0xDEADBEEFDEADBEEFULL);

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
        eq32("one pixel less draws", (uint32_t)got, 5u);
        /* 2047*16 = 32752 = 0x7FF0 + 0x8000 = 0xFFF0: still inside the field, still on this side
         * of the page. */
        eq("its corner word", qw[8], 0x000000008100FFF0ULL);
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
    uint64_t qw[2u * (1u + 4u * 128u)];
    uint32_t i;
    int32_t got;

    for (i = 0u; i < 119u; i++) {
        r[i].x0 = (int32_t)(i % 4u) * 200;
        r[i].y0 = (int32_t)i;
        r[i].x1 = r[i].x0 + 200;
        r[i].y1 = (int32_t)i + 1;
    }
    got = gs_poster_packet(r, 119u, TEAL, qw, gs_poster_qwc(119u));
    eq32("119 rects all drawn", (uint32_t)got, 477u);
    eq("tag: NLOOP=119", qw[0] & 0x7FFFu, 119u);
    eq32("none dropped", gs_poster_dropped(), 0u);
    /* 477 GIF quadwords = 7,632 bytes, against the 6,798 us this batch is there to take off `bg=`
     * (15.7 ns/px x 433,001 px).  The ratio is the lane's whole argument in two numbers, and it is
     * why the batch is one transfer: 119 separate starts would spend the saving on the queueing. */
    eq32("bytes in the batch", (uint32_t)(got * 16u), 7632u);
    /* Every rect of the batch shares the one colour word, and the last rect's second corner is
     * where a stride bug would show up first: i=118 gives y1=119 and x1 = 0*200+200 or similar. */
    eq("last rect keeps the colour", qw[2u + (4u * 2u) * 118u + 2u], 0x3F800000FF808000ULL);
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

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - a poster word is not what the field table says\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - the flat SPRITE batch matches its bit table, its colour and its corners\n");
    return 0;
}
