/*
 * test_ps2_gs_pack.c -- the GS's packet words, checked against hand-computed bit patterns
 *
 * Part of increment 2 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Every
 * number the GS acts on is a field at a bit position inside a 64-bit word, and a field one
 * position off is not an error message -- it is a triangle with a valid-looking vertex that
 * lands 65 536 pixels away, or a texture page that reads the wrong 256-byte block.  There is
 * no screen to look at yet and the machine that could show one is the same machine that would
 * hide it, so the only instrument is this: src/drivers/ps2/ps2_gs_pack.c is compiled into
 * this program directly, and each encoder's output is compared with a literal that was
 * written by adding up the field's own bit positions.
 *
 * The expectations are literals, not expressions.  `1 << 51` is not derived from the code
 * under test and cannot therefore agree with it vacuously: reading the table below against
 * the manual is a second, independent statement of the same layout, which is what makes a
 * disagreement mean something.  Where the two sources this port could consult differ -- the
 * SDK's TEX0 and FRAME macros let CBA and PSM spill into reserved bits -- the literal here
 * follows the manual and the truncation assertions below pin the width that choice implies.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The builder's own source, not a link against it: it is the file under test and has no
 * reason to be compiled twice. */
#include "../../src/drivers/ps2/ps2_gs_pack.c"

static int s_checks, s_failures;

static void eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-22s got=0x%016llx want=0x%016llx\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

static void eq32(const char *what, uint32_t got, uint32_t want)
{
    eq(what, (uint64_t)got, (uint64_t)want);
}

/* ── Vertex and colour words ───────────────────────────────────────────── */

static void prim_words(void)
{
    /* TRI-STRIP with IIP, TME, ABE and context 1: 4 + 8 + 16 + 64 + 512 */
    eq("prim tri-strip g/t/a", gs_pack_prim(GS_OP_TRISTP, 1, 1, 1, 1, 0), 0x25CULL);

    /* SPRITE with IIP, TME, ABE and FIX=2 in PRIM's two-bit field at 11:10:
     * 6 + 8 + 16 + 64 + (2<<10) */
    eq("prim sprite fix=2", gs_pack_prim(GS_OP_SPRITE, 1, 1, 1, 0, 2), 0x85EULL);
}

static void color_words(void)
{
    /* R G B A in the low four bytes, Q's 32 bit pattern above them */
    eq("rgbaq", gs_pack_rgbaq(0x12, 0x34, 0x56, 0x78, GS_Q_ONE),
       0x3F80000078563412ULL);

    /* One byte per channel: what does not fit is gone, as it is on the bus. */
    eq("rgbaq truncates r", gs_pack_rgbaq(0x1FF, 0, 0, 0, 0), 0xFFULL);
    eq("rgbaq q=1.0 bits", gs_pack_rgbaq(0, 0, 0, 0, GS_Q_ONE) >> 32, 0x3F800000ULL);
}

static void texcoord_words(void)
{
    /* S = 1.0f (0x3F800000) low, T = 0.5f (0x3F000000) high */
    eq("st 1.0, 0.5", gs_pack_st(1.0f, 0.5f), 0x3F0000003F800000ULL);

    /* U at 13:0, V at 29:16: 0x1234 and 0x5678 masked to 14 bits gives 0x1234, 0x1678 */
    eq("uv", gs_pack_uv(0x1234, 0x5678), 0x16781234ULL);

    /* 0x4000 is the 15th bit, so U loses it entirely; V keeps 0x4001's bit 0, which is the
     * field's own bit 0 at position 16. */
    eq("uv past 14 bits", gs_pack_uv(0x4000, 0x4001), 0x00010000ULL);
}

static void vertex_words(void)
{
    /* The page offset puts pixel 0 at 2048*16 = 0x8000.  The function's unit is a whole pixel
     * now -- see ps2_gs_pack.h's comment on what the round trip through binary32 used to cost --
     * and the four low bits of the field, which are the 1/16 fraction, are therefore always zero
     * on this path.  The literal below says so at pixel 100: 0x8640, and not the 0x8644 that a
     * quarter-pixel would have set.  The truncation-vs-rounding rows that used to sit here went
     * with the float parameter, and they are honestly retired rather than marked passing: on a
     * whole-pixel input the product is an integer, so the two answers cannot be told apart, and a
     * gate that cannot fail is not an instrument.  What replaces them is the sweep in
     * coord_matches_float(), which pins the shift to the spelling it replaced. */
    eq32("coord 0",       gs_vertex_coord16(0),     0x8000u);
    eq32("coord 100",     gs_vertex_coord16(100),   0x8640u);
    eq32("coord -1",      gs_vertex_coord16(-1),    0x7FF0u);
    eq32("coord 2047",    gs_vertex_coord16(2047),  0xFFF0u);
    eq32("coord -2048",   gs_vertex_coord16(-2048), 0x0000u);

    /* X[15:0] Y[31:16] Z[63:32] */
    eq("xyz2", gs_pack_xyz2(0x8644u, 0x8CA8u, 0u), 0x000000008CA88644ULL);

    /* XYZF2 keeps 24 bits of Z and puts FOG in the top byte. */
    eq("xyzf2", gs_pack_xyzf2(0x8644u, 0x8CA8u, 0x00ABCDFFu, 0x7Fu),
       0x7FABCDFF8CA88644ULL);
    eq("xyzf2 z is 24 bits", gs_pack_xyzf2(0u, 0u, 0x12345678u, 0x7Fu),
       0x7F34567800000000ULL);
}

/* The retired float spelling, kept here as the oracle it is: production has one encoder and this
 * program keeps the other, so the sweep below compares two independent statements instead of
 * comparing a shift with itself. */
static uint32_t coord_float_ref(int32_t px)
{
    return (uint32_t)((int32_t)((float)px * 16.0f) + (int32_t)(2048u << 4)) & 0xFFFFu;
}

static void coord_matches_float(void)
{
    int bad = 0;

    /* The range a clipped screen coordinate can take, both sides of the page origin, plus the
     * overflow past it -- the field wraps and both spellings wrap the same way, which the top of
     * the range checks without anyone having to argue that they do. */
    for (int32_t px = -2100; px <= 2100; px++)
        if (gs_vertex_coord16(px) != coord_float_ref(px))
            bad++;
    for (int32_t px = -100000; px <= 100000; px += 7)
        if (gs_vertex_coord16(px) != coord_float_ref(px))
            bad++;

    eq32("coord: shift == the float spelling it replaced", (uint32_t)bad, 0u);

    /* The control that says this sweep discriminates: one of the two lines it pins is the +16
     * scale, and a shift of 8 rather than 4 disagrees with the oracle everywhere. */
    bad = 0;
    for (int32_t px = 0; px <= 800; px++)
        if ((((uint32_t)px << 8) + GS_XYOFFSET_PAGE) & 0xFFFFu)
            bad++;
    eq32("coord: control, a wrong shift does differ", (bad != 0) ? 1u : 0u, 1u);
}

static uint32_t bits_of(float f)
{
    union { float f; uint32_t u; } b;
    b.f = f;
    return b.u;
}

static void scale_pow2_sweep(void)
{
    int bad = 0, folded = 0;

    /* gs_float_scale_pow2() claims the exponent subtraction and the multiply return the SAME
     * bits.  Checked against the multiply at every shift the vertex path could ask for, over the
     * integers a texel index can be and over the six inputs the guard exists for. */
    for (uint32_t shift = 1u; shift <= 12u; shift++) {
        const float want_unit = 1.0f / (float)(1u << shift);

        for (int32_t i = -70000; i <= 70000; i += 13) {
            const float f = (float)i;
            if (bits_of(gs_float_scale_pow2(f, shift)) != bits_of(f * want_unit))
                bad++;
        }
        /* Zero and its sign: the guard's first case. */
        if (bits_of(gs_float_scale_pow2(0.0f, shift)) != bits_of(0.0f * want_unit)) bad++;
        if (bits_of(gs_float_scale_pow2(-0.0f, shift)) != bits_of(-0.0f * want_unit)) bad++;
        /* Subnormal and the underflow edge: exponent field 0 or <= shift, so the multiply runs. */
        if (bits_of(gs_float_scale_pow2(1e-42f, shift)) != bits_of(1e-42f * want_unit)) bad++;
        if (bits_of(gs_float_scale_pow2(1.0e-38f, shift)) != bits_of(1.0e-38f * want_unit)) bad++;
        /* Infinities and a NaN are not numbers to rescale; the multiply leaves them as they are. */
        if (bits_of(gs_float_scale_pow2(1.0f / 0.0f, shift)) != bits_of((1.0f / 0.0f) * want_unit)) bad++;
        if (bits_of(gs_float_scale_pow2(-1.0f / 0.0f, shift)) != bits_of((-1.0f / 0.0f) * want_unit)) bad++;
        if (bits_of(gs_float_scale_pow2(0.0f / 0.0f, shift)) != bits_of((0.0f / 0.0f) * want_unit)) bad++;
    }

    eq32("scale pow2: fold == multiply, every class", (uint32_t)bad, 0u);

    /* And the fold was actually reached -- a sweep that only ever ran the fallback would pass
     * this file and prove nothing.  1.0f at shift 10 is the vertex path's own ordinary case:
     * 0x3F800000 with its exponent moved down ten, which is 0x3A800000. */
    folded = (bits_of(gs_float_scale_pow2(1.0f, 10u)) == 0x3A800000u &&
              bits_of(gs_float_scale_pow2(437.5f, 10u)) == (bits_of(437.5f) - (10u << 23))) ? 1 : 0;
    eq32("scale pow2: control, the exponent path runs", (uint32_t)folded, 1u);
}

/* ── Drawing-environment words ─────────────────────────────────────────── */

static void env_words(void)
{
    /* page 0x40, buffer width 0x10 units, CT32, 64x64 (TW=TH=6), RGBA:
     * 0x40 + (0x10<<14) + (6<<26) + (6<<30) + (1<<34) */
    eq("tex0 page+size", gs_pack_tex0(0x40u, 0x10u, 0u, 6u, 6u, 1u, 0u, 0u, 0u, 0u, 0u, 0u),
       0x0000000598040040ULL);

    /* the CLUT half: CBA[49:37] CPSM[54:51] CSM[55] CSA[60:56] CLD[63:61] */
    eq("tex0 clut fields", gs_pack_tex0(0u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 1u, 1u, 0x1Fu, 7u),
       0xFF88002000000000ULL);

    /* MMAG (LINEAR) at bit 5, K[43:32] */
    eq("tex1", gs_pack_tex1(0u, 0u, 1u, 0u, 0u, 0u, 0x123u), 0x0000012300000020ULL);

    /* WMS WMT then the four 10-bit limits at 4, 14, 24, 34 */
    eq("clamp", gs_pack_clamp(2u, 2u, 0u, 0x3Fu, 0u, 0x3Fu), 0x000000FC000FC00AULL);

    /* two 16-bit halves, X low and Y at 47:32 */
    eq("xyoffset", gs_pack_xyoffset(0x8000u, 0x8644u), 0x0000864400008000ULL);

    /* 11 bits each, at 0, 16, 32, 48; 800x600 is this port's canvas */
    eq("scissor 800x600", gs_pack_scissor(0u, 800u, 0u, 600u), 0x0258000003200000ULL);
    eq("scissor truncates", gs_pack_scissor(0u, 0x800u, 0u, 0u), 0ULL);

    /* A[1:0] B[3:2] C[5:4] D[7:6] FIX[39:32]: 1|8|16|192 = 0xD9 */
    eq("alpha", gs_pack_alpha(1u, 2u, 1u, 3u, 0x7Fu), 0x0000007F000000D9ULL);

    /* ATEN + ATMETH(4, always) at 3:1 + DATEN at 14 */
    eq("test", gs_pack_test(1u, 4u, 0u, 0u, 1u, 0u, 0u, 0u), 0x0000000000004009ULL);

    /* FBA[8:0] FBW[21:16] PSM[27:24] FMSK[63:32]; 13 units = the 800 px buffer rounded up.
     * PSM is four bits: a six-bit field would run into the write mask's reserved pad. */
    eq("frame", gs_pack_frame(0u, 13u, 0u, 0xFFFFFFFFu), 0xFFFFFFFF000D0000ULL);
    eq("frame psm is 4 bits", gs_pack_frame(0u, 0u, 0x30u, 0u), 0ULL);

    /* ZBA[8:0] ZSM[27:24] ZMSK[32] */
    eq("zbuf", gs_pack_zbuf(0u, 3u, 1u), 0x0000000103000000ULL);
}

/* ── Stream assembly ───────────────────────────────────────────────────── */

static void stream_words(void)
{
    /* NLOOP 4, EOP, PRIM 0x25C at [57:47], 4 registers at [63:60]:
     * 4 + (1<<15) + (0x25C<<47) + (4<<60) */
    eq("giftag", gs_pack_giftag(4u, 1u, 0u, 0x25CULL, GS_FLG_PACKED, 4u),
       0x412E000000008004ULL);

    /* IMAGE, as the blit path already builds it: NLOOP = the quadword count, FLG=2 in the
     * two bits at [59:58] (2<<58, not the sign bit), EOP at 15, and no REGS list:
     * 0x0800000000000000 + 0x8000 + 1200. */
    eq("giftag image", gs_pack_giftag(1200u, 1u, 0u, 0u, GS_FLG_IMAGE, 0u),
       0x08000000000084B0ULL);

    const uint32_t regs[4] = { GS_NIB_PRIM, GS_NIB_RGBAQ, GS_NIB_ST, GS_NIB_XYZ2 };
    eq("regs PRIM,RGBAQ,ST,XYZ2", gs_pack_regs(regs, 4), 0x5210ULL);

    /* The field is TEN nibbles wide, not four, and the sixth nibble is the one that matters: the
     * SDK's flat triangle (ee/draw/src/draw2d.c DRAW_TRIANGLE_REGLIST) names PRIM, RGBAQ, three
     * XYZ2 and a NOP -- six selects, so the third vertex lives in bits [20:23] and a packer that
     * stopped at four would build a two-vertex triangle that never kicks.  Hand-derived:
     * 1 | 2<<4 | 3<<8 | 4<<12 | 5<<16 | 6<<20 = 0x654321. */
    const uint32_t six[6] = { 1, 2, 3, 4, 5, 6 };
    eq("regs names six", gs_pack_regs(six, 6), 0x654321ULL);

    /* Ten is the last that fits, and an eleventh is dropped rather than wrapping into the EOP bit at
     * 63 -- the same reason the field has a bound at all. */
    const uint32_t ten[10] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };
    const uint32_t twelve[12] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    eq("regs fills ten", gs_pack_regs(ten, 10), 0xA987654321ULL);
    eq("regs past ten", gs_pack_regs(twelve, 12), 0xA987654321ULL);

    uint64_t buf[4] = { 0, 0, 0, 0 };
    uint64_t *next = gs_pack_pair(buf, 0xDEADBEEFULL, GS_GP_FRAME_1);
    eq("ad pair data",  buf[0], 0xDEADBEEFULL);
    eq("ad pair reg",   buf[1], 0x4CULL);
    eq("ad pair cursor", (uint64_t)(uintptr_t)next, (uint64_t)(uintptr_t)&buf[2]);
}

/* ── Transfer words, and the quadword itself (row 5b) ────────────────────
 *
 * The four BITBLTBUF/TRXPOS/TRXREG/TRXDIR encoders are the first in this file whose field
 * positions a screen has already proved: the canvas upload in ps2_gs.c has programmed them with
 * hand-written shifts since before this plan existed, so the control here is not a second opinion
 * but an equality -- the encoder has to produce the very words that path produces today, because
 * that path is what the machine has accepted.  The source half (SBA/SBW/SPSM) is ignored by a
 * Host->Local transfer and so is checked against the manual's widths only.
 *
 * The last block states the unit.  It belongs to a packet builder test rather than to the
 * recorder's because it is the packet the DMA has already taken: a GIF quadword is 128 bits, so
 * ten uint64_t are five quadwords, and a tag with NREG=1 and NLOOP=4 owns exactly those five.
 * The recorder first shipped one word per register and still passed its own gate; this is the
 * assertion that cannot be read two ways. */
static void transfer_words(void)
{
    /* SBA[13:0] SBW[21:16] SPSM[29:24] DBA[45:32] DBW[53:48] DPSM[61:56], every field at its own
     * maximum.  A 14- or 6-bit field stops two bits short of a nibble boundary, so the digit above
     * it carries that field's top two bits as 1+2 = 3, and every straddled digit is a 3: digits 3,
     * 5, 7, 11, 13 and 15. */
    eq("bitbltbuf all fields set",
       gs_pack_bitbltbuf(0x3FFFu, 0x3Fu, 0x3Fu, 0x3FFFu, 0x3Fu, 0x3Fu), 0x3F3F3FFF3F3F3FFFULL);

    /* What the working canvas upload writes for an 800 px destination: DBW = (800+63)/64 = 13 in
     * the six bits at 53:48, DPSM = CT32 = 0, and nothing else.  13 << 48 is a shift by 12 hex
     * digits, so 0xD sits at digit 12 -- literal, because ps2_gs.c's own `(uint64_t)fbw << 48` is
     * the thing this replaces and this is the word that path has always sent. */
    eq("bitbltbuf as the blit programs it",
       gs_pack_bitbltbuf(0u, 0u, 0u, 0u, 13u, 0u), 0x000D000000000000ULL);

    /* One bit past each field's width is gone, not spilled into its neighbour: SBA is 14 bits, the
     * two PSM fields 6, and a six-bit code that reached bit 6 would land in the destination page
     * address at 32. */
    eq("bitbltbuf truncates every field",
       gs_pack_bitbltbuf(0x4000u, 0x40u, 0x40u, 0x4000u, 0x40u, 0x40u), 0ULL);

    /* Four 11-bit slots, one per 16-bit half: source x at 0, source y at 16, dest x at 32, dest y
     * at 48.  Small distinct values make a swapped pair impossible to miss. */
    eq("trxpos four slots", gs_pack_trxpos(1u, 2u, 3u, 4u, 0u), 0x0004000300020001ULL);

    /* The 768x500 client's far corner, the numbers this port actually sends: 767 = 0x2FF at 42:32
     * and 499 = 0x1F3 at 58:48. */
    eq("trxpos 767,499 destination", gs_pack_trxpos(0u, 0u, 767u, 499u, 0u),
       0x01F302FF00000000ULL);
    /* DIR is the two bits at 60:59, so reading it as a Local->Local direction adds bit 59. */
    eq("trxpos dir 1", gs_pack_trxpos(0u, 0u, 767u, 499u, 1u), 0x09F302FF00000000ULL);
    eq("trxpos truncates at 11 bits", gs_pack_trxpos(0x800u, 0u, 0u, 0u, 4u), 0ULL);

    /* RRW[11:0] RRH[43:32]: the whole-canvas flush's 800x600, which is p[6] in ps2_gs.c. */
    eq("trxreg 800x600", gs_pack_trxreg(800u, 600u), 0x0000025800000320ULL);
    eq("trxreg truncates", gs_pack_trxreg(0x1000u, 0x1000u), 0ULL);

    /* TRXDIR: INCD[1:0], 0 being Host->Local -- the value the blit sends as a bare 0. */
    eq("trxdir host to local", gs_pack_trxdir(0u), 0ULL);
    eq("trxdir local to host", gs_pack_trxdir(1u), 1ULL);
    eq("trxdir truncates at 2 bits", gs_pack_trxdir(4u), 0ULL);

    /* The unit, stated as the five quadwords the DMA has taken for this port's whole life:
     * send_trx_setup()'s packet, rebuilt here out of the encoders above and compared with the ten
     * words that path hand-writes.  80 bytes, 10 uint64_t, 5 quadwords, NLOOP 4. */
    {
        const uint32_t ad_regs[1] = { GS_NIB_AD };
        uint64_t pkt[10];

        pkt[0] = gs_pack_giftag(4u, 1u, 0u, 0u, GS_FLG_PACKED, 1u);
        pkt[1] = gs_pack_regs(ad_regs, 1u);
        pkt[2] = gs_pack_bitbltbuf(0u, 0u, 0u, 0u, 13u, 0u);
        pkt[3] = GS_GP_BITBLTBUF;
        pkt[4] = gs_pack_trxpos(0u, 0u, 0u, 0u, 0u);
        pkt[5] = GS_GP_TRXPOS;
        pkt[6] = gs_pack_trxreg(800u, 600u);
        pkt[7] = GS_GP_TRXREG;
        pkt[8] = gs_pack_trxdir(0u);
        pkt[9] = GS_GP_TRXDIR;

        eq("setup tag: NLOOP 4, NREG 1, EOP", pkt[0], 0x1000000000008004ULL);
        eq("setup REGS: the AD nibble alone", pkt[1], 0x0EULL);
        eq("setup BITBLTBUF", pkt[2], 0x000D000000000000ULL);
        eq("setup TRXPOS", pkt[4], 0ULL);
        eq("setup TRXREG", pkt[6], 0x0000025800000320ULL);
        eq("setup TRXDIR", pkt[8], 0ULL);
        eq("quadwords in it: one tag plus four writes",
           (uint64_t)(sizeof(pkt) / (2u * sizeof(uint64_t))), 5u);
        eq("bytes in a quadword, so 5 x 16 = 80", (uint64_t)sizeof(pkt), 80u);
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS packet builder vs hand-computed bit layouts\n");
    printf("==========================================================\n");

    prim_words();
    color_words();
    texcoord_words();
    vertex_words();
    coord_matches_float();
    scale_pow2_sweep();
    env_words();
    stream_words();
    transfer_words();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - a packet field is not where the manual puts it\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - every GS packet word matches its bit table\n");
    return 0;
}
