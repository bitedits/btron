/*
 * test_ps2_gs_env.c -- the drawing environment and its VRAM arithmetic, word for word
 *
 * Part of increment 3 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  This gates
 * two things that cannot be looked at: the address arithmetic that decides whether a buffer
 * fits in 4 MB of GS local memory at all, and the eleven-quadword stream that tells the GS
 * where to draw, what to clip to, and how to blend.
 *
 * Both are literals rather than expressions, for the reason the packet test states at length:
 * an expectation computed by the code it checks agrees vacuously.  The page numbers below come
 * from the register widths -- 9 bits of 8192-byte pages and 14 bits of 256-byte pages -- and
 * the field positions come from the manual's tables, so reading this file against the hardware
 * is an independent statement of the same layout.
 *
 * One check is worth calling out because it is not about this code: a second full-screen CT32
 * surface does not fit alongside the canvas and the icon atlas, by 176,128 bytes.  That
 * arithmetic is why increment 4 draws into the canvas the CPU has just blitted instead of into
 * a layer of its own, and if anyone ever changes the screen size or the window size enough for
 * it to fit, this row is the thing that should notice.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The three sources under test, compiled in: the encoders the environment packet is built
 * from, the allocator, and the builder itself. */
#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/drivers/ps2/ps2_gs_env.c"

static int s_checks, s_failures;

static void eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-30s got=0x%016llx want=0x%016llx\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

static void eq32(const char *what, uint32_t got, uint32_t want)
{
    eq(what, (uint64_t)got, (uint64_t)want);
}

static void eqi(const char *what, long got, long want)
{
    eq(what, (uint64_t)(long)got, (uint64_t)(long)want);
}

/* ── VRAM: the two page units and the budget they imply ────────────────── */

static void vram_pages(void)
{
    gs_vram_t v;
    uint32_t byte, page;
    int rc;

    gs_vram_init(&v);

    /* The canvas: FBW=13 units of 64 px, 600 rows, 4 bytes a pixel = 1,996,800 B, rounded up
     * to whole 8 KB pages because the first allocation after it has to be a legal FBA.
     * 1,996,800 / 8,192 = 243.75 -> 244 pages -> 1,998,848. */
    eq32("canvas extent", GS_VRAM_CANVAS_BYTES, 1996800u);
    eq32("cursor after canvas", v.next, 1998848u);
    eq32("vram left after canvas", gs_vram_left(&v), 2195456u);

    /* Width fields round up, which is why a 776-px window costs 832 px of VRAM per row. */
    eq32("fbw 800", gs_vram_fbw(800u), 13u);
    eq32("fbw 776", gs_vram_fbw(776u), 13u);
    eq32("fbw 64", gs_vram_fbw(64u), 1u);
    eq32("fbw 65", gs_vram_fbw(65u), 2u);

    eq32("frame bytes 800x600 32", gs_vram_frame_bytes(800u, 600u, 32u), 1996800u);
    eq32("frame bytes 776x536 32", gs_vram_frame_bytes(776u, 536u, 32u), 1783808u);
    eq32("frame bytes 384x384 32", gs_vram_frame_bytes(384u, 384u, 32u), 589824u);
    eq32("frame bytes 640x448 16", gs_vram_frame_bytes(640u, 448u, 16u), 573440u);

    /* The measurement that decides the architecture (doc/txt/PS2.txt, increment 3's paragraph):
     * canvas + a layer the size of the XMB window + the icon atlas is 4,370,432 B against a GS
     * that has 4,194,304.  Nothing here can fix that by rounding; the layer is the thing that
     * has to go, and increment 4 draws into the canvas instead. */
    eq32("no room for a second layer",
         (gs_vram_frame_bytes(800u, 600u, 32u) +
          gs_vram_frame_bytes(776u, 536u, 32u) +
          gs_vram_frame_bytes(384u, 384u, 32u)) > GS_VRAM_BYTES ? 1u : 0u, 1u);

    /* One atlas, exactly 72 pages, so the cursor stays on a page boundary without slack. */
    rc = gs_vram_take(&v, 589824u, &byte);
    eqi("take the atlas", rc, 0);
    eq32("atlas byte offset", byte, 1998848u);
    eq32("cursor after atlas", v.next, 2588672u);

    /* The same two offsets in the two fields.  1,998,848 is page 244 of 8 KB and page 7,808 of
     * 256 B; both are inside their field, and the difference between them is the 32x that a
     * caller working from one unit alone would get wrong. */
    rc = gs_vram_fba(1998848u, &page);
    eqi("fba atlas", rc, 0);
    eq32("fba atlas page", page, 244u);
    rc = gs_vram_tba(1998848u, &page);
    eqi("tba atlas", rc, 0);
    eq32("tba atlas page", page, 7808u);
    rc = gs_vram_fba(2588672u, &page);
    eqi("fba second", rc, 0);
    eq32("fba second page", page, 316u);
    rc = gs_vram_tba(2588672u, &page);
    eqi("tba second", rc, 0);
    eq32("tba second page", page, 10112u);

    /* Rejections.  An FBA cannot express 256-byte granularity, and that is a layout error, not
     * a rounding one; the 14-bit texture index reaches the very last page of VRAM and no
     * further; the 9-bit frame index stops at 511 pages = the whole 4 MB. */
    rc = gs_vram_fba(1998848u + 256u, &page);
    eqi("fba rejects a 256-byte offset", rc, -1);
    rc = gs_vram_tba(4194048u, &page);
    eqi("tba last page", rc, 0);
    eq32("tba last page index", page, 16383u);
    rc = gs_vram_fba(4194304u, &page);
    eqi("fba rejects past vram", rc, -1);

    /* Allocation failures leave the cursor where it was: a caller that gives up must not have
     * burned the address space. */
    gs_vram_init(&v);
    rc = gs_vram_take(&v, 0u, &byte);
    eqi("take nothing fails", rc, -1);
    eq32("take nothing keeps cursor", v.next, 1998848u);

    rc = gs_vram_take(&v, 2195456u, &byte);
    eqi("take the remainder", rc, 0);
    eq32("remainder byte", byte, 1998848u);
    eq32("cursor full", v.next, 4194304u);
    eq32("nothing left", gs_vram_left(&v), 0u);
    rc = gs_vram_take(&v, 8192u, &byte);
    eqi("take past the end fails", rc, -1);
    eq32("over-take keeps cursor", v.next, 4194304u);
}

/* ── The environment packet ─────────────────────────────────────────────── */

/* A window at (12, 20) that is 776x536, drawn into the canvas at page 0 ... except the frame
 * buffer here is the atlas-aligned page the allocator handed out, because what the register
 * wants is a page index and the builder must not care which one it is. */
static gs_draw_env sample_env(void)
{
    gs_draw_env e;

    e.frame_byte = 1998848u;      /* page 244 */
    e.frame_px_w = 776u;          /* FBW = 13 */
    e.frame_psm  = 0u;            /* CT32 */
    e.frame_fmsk = 0u;            /* write every channel */
    e.zbuf_byte  = 2588672u;      /* page 316 */
    e.zbuf_psm   = 3u;            /* PSMZ32 */
    e.zbuf_zmsk  = 1u;            /* no Z writes: a 2D UI has no depth */
    e.win_x      = 12u;
    e.win_y      = 20u;
    e.clip_x0    = 12u;
    e.clip_y0    = 20u;
    e.clip_x1    = 788u;          /* 12 + 776 */
    e.clip_y1    = 556u;          /* 20 + 536 */
    e.reg_alpha  = 0x1122334455667788ULL;
    e.reg_test   = 0x0011223344556677ULL;
    e.reg_clamp  = 0xFEDCBA9876543210ULL;
    e.reg_tex0   = 0x0123456789ABCDEFULL;
    e.reg_tex1   = 0xFEDCBA9876543201ULL;
    return e;
}

static void env_stream(void)
{
    uint64_t qw[2 * 10];
    gs_draw_env e = sample_env();
    int rc;
    uint32_t i;

    /* Tag pair plus nine writes.  Stated as a literal so a change to GS_ENV_NREG has to be
     * made here too -- the DMA's QWC and the buffer's size both come from this count. */
    eq32("env quadwords", gs_draw_env_qwc(), 10u);

    for (i = 0; i < 20u; i++) qw[i] = 0xA5A5A5A5A5A5A5A5ULL;
    rc = gs_draw_env_packet(&e, qw);
    eqi("env packet built", rc, 0);

    /* QW0 -- GIFTAG.  NLOOP 9 with EOP is 9 + 32768, and NREG 1 sits in the tag's own top
     * nibble at bit 60, which the first draft of this literal left out: 0x1<<60 + 0x8009.
     * The second half is the REGS list -- one nibble, AD = 0x0E. */
    eq("tag data half", qw[0], 0x1000000000008009ULL);
    eq("tag regs half", qw[1], 0x000000000000000EULL);

    /* QW1 -- XYOFFSET_1 (0x18).  12.4 at the 2048 page origin plus the window: 0x8000 + 12*16
     * = 0x80C0 in X, 0x8000 + 20*16 = 0x8140 in Y, Y at bit 32. */
    eq("xyoffset value", qw[2], 0x00008140000080C0ULL);
    eq("xyoffset reg", qw[3], 0x18ULL);

    /* QW2 -- SCISSOR_1 (0x40).  Eleven-bit plain pixels: 12, 788<<16, 20<<32, 556<<48. */
    eq("scissor value", qw[4], 0x022C00140314000CULL);
    eq("scissor reg", qw[5], 0x40ULL);

    /* QW3 -- FRAME (0x4C).  FBA 244 = 0xF4, FBW 13 at bit 16, PSM 0, FMSK 0. */
    eq("frame value", qw[6], 0x00000000000D00F4ULL);
    eq("frame reg", qw[7], 0x4CULL);

    /* QW4 -- ZBUF (0x4E).  ZBA 316 = 0x13C, ZSM 3 at bit 24, ZMSK 1 at bit 32. */
    eq("zbuf value", qw[8], 0x000000010300013CULL);
    eq("zbuf reg", qw[9], 0x4EULL);

    /* QW5..QW9 -- the five registers whose fields come from what the app asked for, so the
     * packet carries them as built.  Asserted verbatim: this is where a lost or reordered
     * write shows up, and a reordered ALPHA and TEST is a window that blends against nothing. */
    eq("test value",  qw[10], 0x0011223344556677ULL);
    eq("test reg",    qw[11], 0x47ULL);
    eq("alpha value", qw[12], 0x1122334455667788ULL);
    eq("alpha reg",   qw[13], 0x42ULL);
    eq("clamp value", qw[14], 0xFEDCBA9876543210ULL);
    eq("clamp reg",   qw[15], 0x08ULL);
    eq("tex0 value",  qw[16], 0x0123456789ABCDEFULL);
    eq("tex0 reg",    qw[17], 0x06ULL);
    eq("tex1 value",  qw[18], 0xFEDCBA9876543201ULL);
    eq("tex1 reg",    qw[19], 0x14ULL);

    /* A layout that does not fit is refused before a word is written: half a packet on the bus
     * would set some of the environment and leave the rest at whatever the last draw left. */
    e.frame_byte = 1998848u + 256u;
    for (i = 0; i < 20u; i++) qw[i] = 0xA5A5A5A5A5A5A5A5ULL;
    rc = gs_draw_env_packet(&e, qw);
    eqi("unaligned frame refused", rc, -1);
    eq("refusal writes nothing", qw[0], 0xA5A5A5A5A5A5A5A5ULL);
}

static void env_stream_ctx2(void)
{
    uint64_t qw[2 * 10];
    gs_draw_env e = sample_env();
    int rc;

    rc = gs_draw_env_packet_ctx(&e, 1u, qw);
    eqi("env ctx2 packet built", rc, 0);

    /* Tag matches */
    eq("tag data half ctx2", qw[0], 0x1000000000008009ULL);
    eq("tag regs half ctx2", qw[1], 0x000000000000000EULL);

    /* Context 2 register offsets */
    eq("xyoffset reg ctx2", qw[3], 0x19ULL);
    eq("scissor reg ctx2",  qw[5], 0x41ULL);
    eq("frame reg ctx2",    qw[7], 0x4DULL);
    eq("zbuf reg ctx2",     qw[9], 0x4FULL);
    eq("test reg ctx2",     qw[11], 0x48ULL);
    eq("alpha reg ctx2",    qw[13], 0x43ULL);
    eq("clamp reg ctx2",    qw[15], 0x09ULL);
    eq("tex0 reg ctx2",     qw[17], 0x07ULL);
    eq("tex1 reg ctx2",     qw[19], 0x15ULL);
}

static void env_stream_dual(void)
{
    uint64_t qw[2 * 19];
    gs_draw_env e1 = sample_env();
    gs_draw_env e2 = sample_env();
    int rc;

    eq32("dual env quadwords", gs_draw_dual_env_qwc(), 19u);

    rc = gs_draw_dual_env_packet(&e1, &e2, qw);
    eqi("dual env packet built", rc, 0);

    /* Tag with NLOOP=18 */
    eq("dual tag data half", qw[0], 0x1000000000008012ULL);
    eq("dual tag regs half", qw[1], 0x000000000000000EULL);

    /* First 9 writes are Context 1 */
    eq("dual ctx1 xyoffset reg", qw[3], 0x18ULL);
    eq("dual ctx1 frame reg",    qw[7], 0x4CULL);
    eq("dual ctx1 alpha reg",    qw[13], 0x42ULL);

    /* Second 9 writes are Context 2 (starting at offset 1 + 9 = 10, pairs at qw[20..37]) */
    eq("dual ctx2 xyoffset reg", qw[21], 0x19ULL);
    eq("dual ctx2 frame reg",    qw[25], 0x4DULL);
    eq("dual ctx2 alpha reg",    qw[31], 0x43ULL);
}

static void vram_compaction(void)
{
    gs_vram_t v;
    uint32_t b1, b2, reclaimed;
    int rc;

    gs_vram_init(&v);
    eq32("vram base before alloc", v.next, 1998848u);

    rc = gs_vram_take(&v, 589824u, &b1);
    eqi("take b1", rc, 0);
    eq32("b1 offset", b1, 1998848u);
    eq32("vram after b1", v.next, 2588672u);

    rc = gs_vram_take(&v, 589824u, &b2);
    eqi("take b2", rc, 0);
    eq32("b2 offset", b2, 2588672u);
    eq32("vram after b2", v.next, 3178496u);

    /* Free b2 (trailing block) */
    rc = gs_vram_free(&v, b2);
    eqi("free b2", rc, 0);

    /* Compact: reclaims b2's span */
    reclaimed = gs_vram_compact(&v);
    eq32("reclaimed b2", reclaimed, 589824u);
    eq32("cursor rewound after b2 compact", v.next, 2588672u);

    /* Free b1 */
    rc = gs_vram_free(&v, b1);
    eqi("free b1", rc, 0);

    /* Compact: reclaims b1's span, rewinds to base */
    reclaimed = gs_vram_compact(&v);
    eq32("reclaimed b1", reclaimed, 589824u);
    eq32("cursor rewound to base", v.next, 1998848u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS draw environment and VRAM budget vs hand math\n");
    printf("==========================================================\n");

    vram_pages();
    env_stream();
    env_stream_ctx2();
    env_stream_dual();
    vram_compaction();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - the environment packet and the VRAM math both match\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - an environment word or a page calculation is not what the GS sees\n");
    return 1;
}
