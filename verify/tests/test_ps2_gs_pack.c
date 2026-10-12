/*
 * test_ps2_gs_pack.c -- the GS's packet words, checked against hand-computed bit patterns
 *
 * Part of increment 2 of the plan of record (doc/md/PS2.md, "The EE/GS backend").  Every
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
    /* The page offset puts pixel 0 at 2048*16 = 0x8000, and the low 4 bits are the
     * fraction: quantisation is 1/16 px, and truncation (0.06 px*16 = 0.96) is not rounding
     * -- the assertion that separates them. */
    eq32("coord 0.0",        gs_vertex_coord16(0.0f),     0x8000u);
    eq32("coord 100.25",     gs_vertex_coord16(100.25f),  0x8644u);
    eq32("coord 1/16",       gs_vertex_coord16(0.0625f),  0x8001u);
    eq32("coord truncates",  gs_vertex_coord16(0.06f),    0x8000u);
    eq32("coord -1/16",      gs_vertex_coord16(-0.0625f), 0x7FFFu);

    /* X[15:0] Y[31:16] Z[63:32] */
    eq("xyz2", gs_pack_xyz2(0x8644u, 0x8CA8u, 0u), 0x000000008CA88644ULL);

    /* XYZF2 keeps 24 bits of Z and puts FOG in the top byte. */
    eq("xyzf2", gs_pack_xyzf2(0x8644u, 0x8CA8u, 0x00ABCDFFu, 0x7Fu),
       0x7FABCDFF8CA88644ULL);
    eq("xyzf2 z is 24 bits", gs_pack_xyzf2(0u, 0u, 0x12345678u, 0x7Fu),
       0x7F34567800000000ULL);
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

    /* only four nibbles exist in a tag, so a fifth and sixth register are dropped rather
     * than wrapping into the ones that count */
    const uint32_t too_many[6] = { 1, 2, 3, 4, 5, 6 };
    eq("regs past four", gs_pack_regs(too_many, 6), 0x4321ULL);

    uint64_t buf[4] = { 0, 0, 0, 0 };
    uint64_t *next = gs_pack_pair(buf, 0xDEADBEEFULL, GS_GP_FRAME_1);
    eq("ad pair data",  buf[0], 0xDEADBEEFULL);
    eq("ad pair reg",   buf[1], 0x4CULL);
    eq("ad pair cursor", (uint64_t)(uintptr_t)next, (uint64_t)(uintptr_t)&buf[2]);
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
    env_words();
    stream_words();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - a packet field is not where the manual puts it\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - every GS packet word matches its bit table\n");
    return 0;
}
