/*
 * test_ps2_gs_read.c -- the Local->Host request, word by word
 *
 * LANE 19 rung 2's gate.  What rung 2 buys is the ability to census the GS's own output without a
 * human at a window, and everything that can go wrong with it is invisible in exactly the way this
 * repo has learned to distrust: an AD write that lands on the wrong GP offset programs the right
 * register with the wrong word and the transfer still has a legal length; a DIRECT whose immediate
 * counts the block instead of the packet leaves TRXDIR in the FIFO for the next block to decode,
 * which is a second request armed from someone else's bytes; and a rectangle that is not a whole
 * number of quadwords per row is downloaded SHORT, because the hardware's size formula rounds down --
 * PCSX2's own comment on it says "games must take care to ensure transfer rectangles are exact
 * multiples of a qword" -- so the caller's tail bytes still hold the PREVIOUS read and the census
 * mixes two screens and reports a colour that was never on either.
 *
 * So every word below is compared with a literal derived by adding up the field's own bit positions
 * from ps2_gs_pack.h's table -- the test_ps2_gs_pack discipline, expectations the code under test did
 * not compute.  The labelled CONTROLS are the ones that could plausibly pass by construction and so
 * have to be shown to discriminate: the source half of BITBLTBUF against the destination half (a
 * header reading until this rung exercised it, so this is the first measurement of it), DIRECT's
 * count against the block's, the alignment law in BYTES rather than in the four-pixels-a-quadword
 * special case CT32 tempts you to write, and the refusal that leaves the caller's buffer untouched --
 * half a request group is a GS holding TRXPOS with no TRXDIR, which is not a smaller picture but no
 * picture at all.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>

/* The builder's own source, not a link against it: it is the file under test, its law-checker is
 * file-static on purpose, and the packer and the VRAM page arithmetic have to come with it for the
 * same reason the gs_rec, gs_tex and gs_poster gates bring them in -- one translation unit is what
 * this suite measures and what links into btron-ps2.elf. */
#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/drivers/ps2/ps2_gs_read.c"

/* The formats by number, spelled here instead of including ps2_gs.h: those four values ARE
 * ps2_gs.h's GS_PSM_* and the low three bits are all gs_read_bpp() reads. */
#define PSM_CT32 0u
#define PSM_CT24 1u
#define PSM_CT16 2u

#define SENTINEL 0xDEADBEEFDEADBEEFULL

static int s_checks, s_failures;

static void eq(const char *what, uint64_t got, uint64_t want)
{
    s_checks++;
    if (got == want) return;
    s_failures++;
    printf("  [FAIL] %-44s got=0x%016llx want=0x%016llx\n",
           what, (unsigned long long)got, (unsigned long long)want);
}

static void eq32(const char *what, uint32_t got, uint32_t want)
{
    eq(what, (uint64_t)got, (uint64_t)want);
}

static void fill(uint64_t *qw, unsigned n, uint64_t v)
{
    for (unsigned i = 0u; i < n; i++) qw[i] = v;
}

/* ── The sizes ─────────────────────────────────────────────────────────── */

static void sizes(void)
{
    /* Five writes and one tag, in quadwords -- and the block is that plus the code quadword VIF1
     * owns.  Every count the driver's DMA takes is built from these numbers, so they are compared
     * with the literals they come from rather than with each other. */
    eq32("REQ_NREG", GS_READ_REQ_NREG, 5u);
    eq32("REQ_QWC", GS_READ_REQ_QWC, 6u);
    eq32("BLOCK_QWC", GS_READ_BLOCK_QWC, 7u);
    eq32("BLOCK_U64", GS_READ_BLOCK_U64, 14u);
    /* Two uint64_t slots per quadword: a caller that allocates GS_READ_BLOCK_U64 cannot be short by
     * the account the driver's DMA uses. */
    eq32("BLOCK_U64 == 2 * BLOCK_QWC", GS_READ_BLOCK_U64, 2u * GS_READ_BLOCK_QWC);

    /* The download's length, which is the hardware's own formula (PCSX2 Gif_HandlerAD:
     * RRW*RRH*bpp>>7).  64x16 is the strip the gsread verb reads; 800x600 is the whole canvas. */
    eq32("image qwc 64x16 CT32", gs_read_image_qwc(64u, 16u, PSM_CT32), 256u);
    eq32("image bytes 64x16", gs_read_image_qwc(64u, 16u, PSM_CT32) * 16u, 4096u);
    eq32("image qwc 800x600 CT32", gs_read_image_qwc(800u, 600u, PSM_CT32), 120000u);
    /* The number that prices a whole-canvas read: 1,920,000 bytes of canvas through a reversed VIF
     * FIFO, which is why the verb reads strips and not the screen. */
    eq32("image bytes 800x600", gs_read_image_qwc(800u, 600u, PSM_CT32) * 16u, 1920000u);
    /* The same rectangles at other formats: 1024 px at 16 bits is 128 quadwords and 256 px at 24 bits
     * is 48, i.e. bpp enters the formula once and not twice. */
    eq32("image qwc 64x16 CT16", gs_read_image_qwc(64u, 16u, PSM_CT16), 128u);
    eq32("image qwc 16x16 CT24", gs_read_image_qwc(16u, 16u, PSM_CT24), 48u);
    /* A format with no byte width has no length, and 0 is the answer a caller must not mistake for
     * "an empty transfer". */
    eq32("image qwc of an unsupported psm", gs_read_image_qwc(64u, 16u, 4u), 0u);
    /* ...and past the 12-bit fields the product is not evaluated at all, so a caller that skipped the
     * packet builder cannot get a huge QWC out of a rectangle the registers cannot name. */
    eq32("image qwc of an over-wide rect", gs_read_image_qwc(4096u, 4096u, PSM_CT32), 0u);
    /* The four formats the low three bits can name, as bytes: the same table PCSX2 uses to size a
     * download, spelled out so a future change here is a change someone has to argue with. */
    eq32("bpp CT32", gs_read_bpp(PSM_CT32), 32u);
    eq32("bpp CT24", gs_read_bpp(PSM_CT24), 24u);
    eq32("bpp CT16", gs_read_bpp(PSM_CT16), 16u);
    eq32("bpp of the 8-bit class", gs_read_bpp(3u), 8u);
    eq32("bpp 0x0B: same class, bit-selects set", gs_read_bpp(11u), 8u);
    eq32("bpp psm 4 is nothing", gs_read_bpp(4u), 0u);
    eq32("bpp psm 7 is nothing", gs_read_bpp(7u), 0u);
}

/* ── The whole block, every word ───────────────────────────────────────── */

/* A request deliberately not the production one: a 16-bit format, a non-zero source page, a non-zero
 * origin.  Every field of every word is then distinguishable from its neighbour's, which is what a
 * literal has to be to be worth anything. */
static const gs_read_req k_req = { 0x123400u, 13u, PSM_CT16, 16u, 96u, 64u, 16u };

static void one_block(void)
{
    uint64_t qw[GS_READ_BLOCK_U64];
    int32_t got;

    /* Sentinels first: an assertion that a word is right is worth nothing unless the buffer could
     * have shown it wrong, and a block that forgot to write a quadword would otherwise read 0 and look
     * like a legal FINISH data word. */
    fill(qw, GS_READ_BLOCK_U64, SENTINEL);
    got = gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC);
    eq32("block returns its own size", (uint32_t)got, 7u);

    /* The VIF code quadword: four 32-bit commands, in order NOP, NOP, FLUSHA, DIRECT(6).  The first
     * u64 is therefore 0 -- and asserting 0 against a buffer filled with a sentinel is exactly what
     * makes this assertion worth having, because a builder that skipped its code quadword would leave
     * 0xDEADBEEFDEADBEEF here and VIF1 would read a command out of the uninitialised words.
     * 0x13 = FLUSHA and 0x50 = DIRECT are ps2tek's command numbers; 0x50000006 is that function
     * shifted into [30:24] with the immediate 6 in [15:0] of its own word. */
    eq("codes: NOP, NOP", qw[0], 0u);
    eq("codes: FLUSHA, DIRECT(6)", qw[1], 0x5000000613000000ULL);

    /* GIFTAG: NLOOP=5 / EOP=1 / PRE=0 / PRIM=0 / FLG=PACKED / NREG=1:
     *   5<<0 | 1<<15 | 0<<46 | 0<<47 | 0<<58 | 1<<60
     * = 0x05 + 0x8000 + 0x1000000000000000 */
    eq("tag: NLOOP/EOP/PRE/PRIM/FLG/NREG", qw[2], 0x1000000000008005ULL);
    eq("tag: the one nibble is AD", qw[3], 0x0EULL);

    /* BITBLTBUF: SBA=0x1234 (0x123400 / 256), SBW=13, SPSM=2, DBA=0, DBW=0, DPSM=2.
     *   0x1234<<0 | 13<<16 | 2<<24 | 0<<32 | 0<<48 | 2<<56 = 0x02000000_020D1234 */
    eq("data 0: BITBLTBUF", qw[4], 0x02000000020D1234ULL);
    eq("data 0: its GP offset", qw[5], 0x50ULL);

    /* TRXPOS: SASX=16, SASY=96, DASX=DASY=0, DIR=0 -> 0x10 | 0x60<<16 */
    eq("data 1: TRXPOS", qw[6], 0x0000000000600010ULL);
    eq("data 1: its GP offset", qw[7], 0x51ULL);

    /* TRXREG: RRW=64, RRH=16 -> 0x40 | 0x10<<32 */
    eq("data 2: TRXREG", qw[8], 0x0000001000000040ULL);
    eq("data 2: its GP offset", qw[9], 0x52ULL);

    /* FINISH: the event is raised by the WRITE, so the value is 0 -- and it sits at 0x61, one past the
     * transfer block, the offset this port had to add to the nibble table to name it. */
    eq("data 3: FINISH value", qw[10], 0u);
    eq("data 3: its GP offset", qw[11], 0x61ULL);

    /* TRXDIR: INCD=1, Local->Host.  This word is the request; the four before it only describe it. */
    eq("data 4: TRXDIR", qw[12], 1u);
    eq("data 4: its GP offset", qw[13], 0x53ULL);

    eq32("no refusal happened", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_OK);

    /* The request group on its own, for a caller that supplies its own code quadword: same words,
     * starting one quadword earlier.  This is the equality that keeps the two builders from drifting
     * into two spellings of one packet. */
    fill(qw, GS_READ_BLOCK_U64, SENTINEL);
    got = gs_read_request_packet(&k_req, qw, GS_READ_REQ_QWC);
    eq32("request alone returns its own size", (uint32_t)got, 6u);
    eq("request alone: tag", qw[0], 0x1000000000008005ULL);
    eq("request alone: BITBLTBUF", qw[2], 0x02000000020D1234ULL);
    eq("request alone: TRXDIR offset", qw[11], 0x53ULL);
}

/* ── CONTROL: the SOURCE half carries the address, not the destination half ── */

static void source_control(void)
{
    uint64_t qw[GS_READ_BLOCK_U64];
    const uint32_t sba = 0x1234u;
    uint64_t swapped;

    fill(qw, GS_READ_BLOCK_U64, 0u);
    (void)gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC);

    /* SBA is [13:0] and SBW is [21:16] -- the two fields a Local->Host transfer actually reads,
     * because for a read the "buffer" is the page in VRAM and the row pitch is the width field.  Taken
     * out of the produced word field by field, so a shift error shows up as the wrong field rather than
     * as a different colour on a screen nobody is watching. */
    eq("CONTROL SBA is bits [13:0]", qw[4] & 0x3FFFu, sba);
    eq("CONTROL SBW is bits [21:16]", (qw[4] >> 16) & 0x3Fu, 13u);
    eq("CONTROL SPSM is bits [29:24]", (qw[4] >> 24) & 0x3Fu, PSM_CT16);
    /* ...and the destination half is empty by design: a read has no destination rectangle in VRAM. */
    eq("CONTROL DBA is bits [45:32] and is 0", (qw[4] >> 32) & 0x3FFFu, 0u);
    eq("CONTROL DBW is bits [53:48] and is 0", (qw[4] >> 48) & 0x3Fu, 0u);

    /* The discriminating half: the same two numbers put in the DESTINATION slots is a DIFFERENT word.
     * Without this the five assertions above could be a symmetric accident -- 0x1234 and 13 in either
     * order both look like a plausible BITBLTBUF, and only the swapped word shows the two halves of
     * the register are not interchangeable. */
    swapped = gs_pack_bitbltbuf(0u, 0u, k_req.psm, sba, 13u, k_req.psm);
    eq("CONTROL the destination-pair spelling is not what ships", swapped != qw[4], 1u);

    /* And the page unit itself: SBA counts 256-byte blocks, so the byte offset the caller named and
     * the field the GS decodes differ by exactly that factor.  A port that passed the byte offset
     * straight through would read from 0x1234 BYTES -- 1,165 px into a CT32 canvas -- and the census
     * would come back full of plausible colours from the wrong place. */
    eq("CONTROL the base is a 256-byte page index", (qw[4] & 0x3FFFu) * 256u, k_req.vram_byte);
}

/* ── CONTROL: DIRECT counts the PACKET, not the BLOCK ─────────────────── */

static void direct_control(void)
{
    uint64_t qw[GS_READ_BLOCK_U64];
    int32_t got;

    fill(qw, GS_READ_BLOCK_U64, 0u);
    got = gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC);

    /* DIRECT's immediate is in [15:0] of its code word, which is the HIGH u32 of the code quadword --
     * the fourth of the four commands, and the one that hands the GIF the packet that follows. */
    eq32("CONTROL DIRECT's immediate", (uint32_t)((qw[1] >> 32) & 0xFFFFu), GS_READ_REQ_QWC);
    /* Not the block's 7.  The code quadword is VIF1's own and never reaches the GIF; a DIRECT that
     * counted it would hand the GIF six quadwords and leave TRXDIR sitting in the FIFO for the NEXT
     * block to decode -- a second request armed from someone else's bytes. */
    eq("CONTROL DIRECT is not the block count",
       ((qw[1] >> 32) & 0xFFFFu) != GS_READ_BLOCK_QWC, 1u);
    /* And FLUSHA in the slot before it, not after: the paths have to be clear before the packet that
     * arms the request goes anywhere.  0x13 out of the low u32 is the proof the two did not swap. */
    eq32("CONTROL FLUSHA precedes DIRECT", (uint32_t)(qw[1] & 0xFF000000u) >> 24, 0x13u);

    /* The two halves of the claim, measured rather than asserted together: the packet after the codes
     * is exactly the number DIRECT says, the block is that plus one, and the tag counts the five
     * writes the packet holds. */
    eq32("CONTROL packet words == DIRECT", (uint32_t)got - 1u, (uint32_t)GS_READ_REQ_QWC);
    eq32("CONTROL DIRECT == packet words", (uint32_t)(qw[1] >> 32) & 0xFFFFu, (uint32_t)got - 1u);
    eq("CONTROL NLOOP == the five writes", qw[2] & 0x7FFFu, GS_READ_REQ_NREG);
    /* NREG 1 with a single AD nibble: five loops, one register each -- which is why the packet is six
     * quadwords and not eleven. */
    eq("CONTROL NREG is 1", (qw[2] >> 60) & 0xFu, 1u);
    eq("CONTROL FLG is PACKED", (qw[2] >> 58) & 0x3u, 0u);
    eq("CONTROL EOP ends it", (qw[2] >> 15) & 0x1u, 1u);
}

/* ── The production shape: the canvas as the port actually has it ──────── */

static void canvas_shape(void)
{
    /* VRAM page 0, the canvas's own width field (800 px rounds up to 13 units of 64), CT32, and the
     * first of the three strips the gsread verb takes out of it.  This is the request the driver is
     * going to send, so its arithmetic is checked here rather than discovered on the emulator. */
    const gs_read_req r = { 0u, 13u, PSM_CT32, 32u, 208u, 64u, 16u };
    uint64_t qw[GS_READ_BLOCK_U64];
    int32_t got;

    /* The width field is derived from the pixel width by the allocator's own helper, and the two
     * spellings of 800 px have to agree or the verb's pitch is not the upload's pitch. */
    eq32("canvas SBW is gs_vram_fbw(800)", r.sbw64, gs_vram_fbw(800u));
    eq32("and is 13", r.sbw64, 13u);

    fill(qw, GS_READ_BLOCK_U64, SENTINEL);
    got = gs_read_block_packet(&r, qw, GS_READ_BLOCK_QWC);
    eq32("a canvas strip builds", (uint32_t)got, 7u);
    /* SBA 0, SBW 13, SPSM 0, DBA 0, DBW 0, DPSM 0 -> 13<<16 and nothing else set. */
    eq("canvas BITBLTBUF", qw[4], 0x00000000000D0000ULL);
    /* 32 px in, at row 208: 0x20 | 0xD0<<16. */
    eq("canvas TRXPOS 32,208", qw[6], 0x0000000000D00020ULL);
    eq("canvas TRXREG 64x16", qw[8], 0x0000001000000040ULL);
    eq32("strip length in quadwords", gs_read_image_qwc(r.w, r.h, r.psm), 256u);

    /* The full canvas is NOT one transfer.  This block used to assert that it was, and the law that
     * refuted it is GS_READ_E_QWC: 800x600 at CT32 is 120,000 quadwords, CH_QWC is a 16-bit field,
     * and the count the DMA would have been handed is 120,000 mod 65,536 = 54,464 -- a census of the
     * first 272 rows plus whatever the buffer already held below them, printed as though it were the
     * page.  A page is a loop of bands, which is why ps2sdk's own screenshot() walks the screen one
     * row per transfer.  The pitch and VRAM arithmetic below still matter, because they are what a
     * band has to satisfy, and they are still checked here. */
    {
        const gs_read_req whole = { 0u, 13u, PSM_CT32, 0u, 0u, 800u, 600u };
        const gs_read_req band  = { 0u, 13u, PSM_CT32, 0u,   0u, 800u, 327u };
        const gs_read_req over  = { 0u, 13u, PSM_CT32, 0u,   0u, 800u, 328u };
        uint64_t end;

        fill(qw, GS_READ_BLOCK_U64, SENTINEL);
        got = gs_read_block_packet(&whole, qw, GS_READ_BLOCK_QWC);
        eq32("the whole canvas is refused", (uint32_t)got, 0xFFFFFFFFu);
        eq32("and names the 16-bit count", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_QWC);
        s_checks++;
        if (qw[0] != SENTINEL || qw[6] != SENTINEL || qw[13] != SENTINEL) {
            s_failures++;
            printf("  [FAIL] a QWC refusal still wrote bytes\n");
        }

        /* The band's bound, at the width the canvas actually has: 200 quadwords a row, so the tallest
         * single transfer is 327 rows (65,400) and 328 is 65,600 -- refused by one row.  The two cases
         * sit on either side of the field rather than at some safe middle, because a law that only
         * fires at 600 rows would pass here and still wrap at 400. */
        eq32("a row of the canvas is 200 quadwords", gs_read_image_qwc(800u, 1u, PSM_CT32), 200u);
        eq32("327 rows fit the count", gs_read_image_qwc(band.w, band.h, band.psm), 65400u);
        fill(qw, GS_READ_BLOCK_U64, SENTINEL);
        eq32("and build", (uint32_t)gs_read_block_packet(&band, qw, GS_READ_BLOCK_QWC), 7u);
        eq32("328 rows are 65600 quadwords", gs_read_image_qwc(over.w, over.h, over.psm), 65600u);
        fill(qw, GS_READ_BLOCK_U64, SENTINEL);
        eq32("and are refused", (uint32_t)gs_read_block_packet(&over, qw, GS_READ_BLOCK_QWC),
             (uint32_t)-1);
        eq32("CONTROL the bound is the count, not the rows",
             (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_QWC);

        /* The pitch arithmetic, spelled out by hand so the bound in the builder is not the only place
         * it is stated: 599 whole rows of 3,328 B, plus 800 px of the 600th -- inside VRAM, so the
         * refusal above is the count and not the address. */
        end = 599u * 13u * 64u * 4u + 800u * 4u;
        eq32("last row ends at the canvas's own end", (uint32_t)end, 1996672u);
        eq32("and that is inside VRAM", end <= GS_VRAM_BYTES, 1u);

        /* At 16 bpp the same refusal is 656 rows of the same width: the law is in quadwords, and a
         * rule that read as "no more than 327 rows" would let a CT16 page through at 1,048,576 B. */
        {
            const gs_read_req wide16 = { 0u, 13u, PSM_CT16, 0u, 0u, 800u, 656u };
            eq32("CT16 656 rows are 65600 quadwords",
                 gs_read_image_qwc(wide16.w, wide16.h, wide16.psm), 65600u);
            eq32("and are refused too", (uint32_t)gs_read_block_packet(&wide16, qw,
                                                                     GS_READ_BLOCK_QWC),
                 (uint32_t)-1);
        }
    }

    /* One row of the canvas as a strip of the whole width: the shape a band would take, and the case
     * where the quadword law is a no-op because 800 px is exactly 200 quadwords. */
    {
        const gs_read_req row = { 0u, 13u, PSM_CT32, 0u, 300u, 800u, 1u };
        fill(qw, GS_READ_BLOCK_U64, SENTINEL);
        got = gs_read_block_packet(&row, qw, GS_READ_BLOCK_QWC);
        eq32("a whole row builds", (uint32_t)got, 7u);
        eq32("and is 200 quadwords", gs_read_image_qwc(row.w, row.h, row.psm), 200u);
        eq("its row position", qw[6], 0x00000000012C0000ULL);   /* 300 = 0x12C in SASY */
    }
}

/* ── The laws a caller can hit, each with its own name ─────────────────── */

static void refusals(void)
{
    uint64_t qw[GS_READ_BLOCK_U64];

    /* Each case: the request, the law it breaks, and the error code that says so.  A caller that gets
     * -1 without a code cannot tell "no census" from "the census is black", which is the difference
     * between debugging the packet and debugging the screen. */
    struct { gs_read_req r; gs_read_err want; const char *what; } cases[] = {
        /* A format with no byte width: the 4-bit and undefined classes the GS refuses to download. */
        { {     0u, 13u,  4u,    0u,   0u,   64u, 16u }, GS_READ_E_PSM,   "an unsupported psm" },
        { {     0u, 13u,  7u,    0u,   0u,   64u, 16u }, GS_READ_E_PSM,   "psm 7" },
        /* RRW/RRH are 12 bits, and 0 is a transfer of nothing that the receipt would still fire for. */
        { {     0u, 13u,  0u,    0u,   0u,    0u, 16u }, GS_READ_E_SIZE,  "a zero width" },
        { {     0u, 13u,  0u,    0u,   0u,   64u,  0u }, GS_READ_E_SIZE,  "a zero height" },
        { {     0u, 13u,  0u,    0u,   0u, 4096u, 16u }, GS_READ_E_SIZE,  "a 13-bit width" },
        { {     0u, 13u,  0u,    0u,   0u,   64u, 4096u }, GS_READ_E_SIZE, "a 13-bit height" },
        /* SASX/SASY are 11 bits: the ORIGIN runs out before the size does, on a 4032-px buffer. */
        { {     0u, 63u,  0u, 2048u,   0u,   64u, 16u }, GS_READ_E_POS,   "x past the 11-bit field" },
        { {     0u, 63u,  0u,    0u, 2048u,  64u, 16u }, GS_READ_E_POS,   "y past the 11-bit field" },
        /* The quadword law, CT32: 2 px is half a quadword, and a row of 2 px is 8 bytes. */
        { {     0u, 13u,  0u,    2u,   0u,   64u, 16u }, GS_READ_E_ALIGN, "an origin off a quadword" },
        { {     0u, 13u,  0u,    0u,   0u,    2u, 16u }, GS_READ_E_ALIGN, "a row off a quadword" },
        /* The same law at 16 bpp, where a quadword is 8 px: 4 px is half one. */
        { {     0u, 13u,  2u,    4u,   0u,   64u, 16u }, GS_READ_E_ALIGN, "an origin off a qword, 16bpp" },
        /* The pitch: the area must lie inside the buffer SBW names, because the transfer walks whole
         * SBW rows and a row that overruns reads its neighbour's pixels at the wrong x. */
        { {     0u, 13u,  0u,  800u,   0u,   64u, 16u }, GS_READ_E_PITCH, "past the buffer's row" },
        { {     0u, 13u,  2u,  800u,   0u,   64u, 16u }, GS_READ_E_PITCH, "past the row, at 16 bpp" },
        { {     0u,  0u,  0u,    0u,   0u,   64u, 16u }, GS_READ_E_PITCH, "a buffer of no width" },
        /* SBW is a 6-bit field, so 64 units cannot be named at all. */
        { {     0u, 64u,  0u,    0u,   0u,   64u, 16u }, GS_READ_E_PITCH, "an over-wide SBW field" },
        /* The last row has to be inside the 4 MB, and no register says so -- only this bound. */
        { { GS_VRAM_BYTES - 256u, 13u, 0u,   0u,   0u,   64u,  2u }, GS_READ_E_PITCH, "rows past VRAM" },
        /* SBA counts whole 256-byte pages; 0x1080 is a page and a half, so it cannot be named. */
        { {  0x1080u, 13u,  0u,  0u,   0u,   64u, 16u }, GS_READ_E_PAGE,  "a base off a 256-byte page" },
    };
    unsigned i;

    for (i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
        int32_t got;

        fill(qw, GS_READ_BLOCK_U64, SENTINEL);
        got = gs_read_block_packet(&cases[i].r, qw, GS_READ_BLOCK_QWC);

        s_checks++;
        if (got == -1) {
            /* The control that makes a refusal worth having: the buffer is exactly as it arrived, so
             * "refused" means no request on the bus at all, and not a half-written one. */
            if (qw[0] != SENTINEL || qw[7] != SENTINEL || qw[13] != SENTINEL) {
                s_failures++;
                printf("  [FAIL] refused %s still wrote bytes\n", cases[i].what);
            }
        } else {
            s_failures++;
            printf("  [FAIL] %-34s was accepted (got=%d)\n", cases[i].what, (int)got);
        }
        eq32(cases[i].what, (uint32_t)gs_read_last_err(), (uint32_t)cases[i].want);
    }

    /* Room, and the null arguments: the caller's own bugs, refused the same way and for the same
     * reason -- nothing reaches the bus. */
    fill(qw, GS_READ_BLOCK_U64, SENTINEL);
    eq32("a short block is refused",
         (uint32_t)gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC - 1u), 0xFFFFFFFFu);
    eq32("and names the reason", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_ARGS);
    eq("a short block wrote no code word", qw[0], SENTINEL);
    eq("and no last word either", qw[13], SENTINEL);

    eq32("a null request is refused",
         (uint32_t)gs_read_request_packet(0, qw, GS_READ_REQ_QWC), 0xFFFFFFFFu);
    eq32("a null buffer is refused",
         (uint32_t)gs_read_request_packet(&k_req, 0, GS_READ_REQ_QWC), 0xFFFFFFFFu);
    eq32("a short request is refused",
         (uint32_t)gs_read_request_packet(&k_req, qw, GS_READ_REQ_QWC - 1u), 0xFFFFFFFFu);
    eq32("all three named", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_ARGS);
    eq32("and the block refuses a room of one",
         (uint32_t)gs_read_block_packet(&k_req, qw, 1u), 0xFFFFFFFFu);
    eq("and still wrote nothing", qw[0], SENTINEL);

    /* CONTROL: the alignment law is in BYTES, not in CT32's four pixels.  At 24 bpp a quadword holds
     * 16/3 pixels, which is not a whole number, so the only origins and widths that satisfy it are
     * multiples of 16 px -- and a builder that wrote `x & 3` would accept x=4, producing a row of
     * 12 bytes, i.e. a download whose length the hardware rounds down to 0 quadwords for a 4x1 strip:
     * a census of nothing, and an empty transfer that still fires its FINISH receipt. */
    {
        const gs_read_req ct24_ok  = { 0u, 13u, PSM_CT24, 16u, 0u, 16u, 16u };
        const gs_read_req ct24_bad = { 0u, 13u, PSM_CT24,  4u, 0u, 16u, 16u };
        const gs_read_req ct24_w   = { 0u, 13u, PSM_CT24,  0u, 0u,  4u, 16u };
        const gs_read_req ct24_n   = { 0u, 13u, PSM_CT24,  0u, 0u, 16u, 1u };

        fill(qw, GS_READ_BLOCK_U64, 0u);
        eq32("CONTROL CT24 at x=16 is legal",
             (uint32_t)gs_read_block_packet(&ct24_ok, qw, GS_READ_BLOCK_QWC), 7u);
        /* 16 px x 16 rows x 3 B = 768 B = 48 quadwords; the size law and the alignment law agree
         * because both are the same bytes-per-row statement. */
        eq32("CONTROL CT24 16x16 image qwc", gs_read_image_qwc(16u, 16u, PSM_CT24), 48u);
        eq32("CONTROL CT24 one row of 16 px", gs_read_image_qwc(16u, 1u, PSM_CT24), 3u);

        eq32("CONTROL CT24 at x=4 is refused",
             (uint32_t)gs_read_block_packet(&ct24_bad, qw, GS_READ_BLOCK_QWC), 0xFFFFFFFFu);
        eq32("CONTROL as an alignment law", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_ALIGN);
        eq32("CONTROL CT24 4 px wide is refused",
             (uint32_t)gs_read_block_packet(&ct24_w, qw, GS_READ_BLOCK_QWC), 0xFFFFFFFFu);
        eq32("CONTROL likewise", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_ALIGN);
        /* And the width that IS legal at 24 bpp is the one the x rule gives: 16 px, one row = 3
         * quadwords, which is the smallest CT24 read the hardware can express at all. */
        eq32("CONTROL CT24 16x1 is the unit",
             (uint32_t)gs_read_block_packet(&ct24_n, qw, GS_READ_BLOCK_QWC), 7u);
    }

    /* The counter is per call, so a refusal cannot be the reason the NEXT verb says no -- and a
     * success after one must clear it, or gsread's `refuse=` column would inherit gsquad's. */
    fill(qw, GS_READ_BLOCK_U64, 0u);
    eq32("the next call starts clean",
         (uint32_t)gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC), 7u);
    eq32("and clears the reason", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_OK);
    (void)gs_read_block_packet(&cases[0].r, qw, GS_READ_BLOCK_QWC);
    eq32("a refusal after a success names itself",
         (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_E_PSM);
    (void)gs_read_block_packet(&k_req, qw, GS_READ_BLOCK_QWC);
    eq32("and is gone once the call succeeds", (uint32_t)gs_read_last_err(), (uint32_t)GS_READ_OK);
}

/* ── The VIF code words, against the command table ─────────────────────── */

static void vif_codes(void)
{
    /* Each code is function in [30:24] and immediate in [23:0], and of the three this block uses only
     * DIRECT carries one.  Written out from ps2tek's table rather than from the builder, so the
     * macros in the header are checked and not merely restated. */
    eq32("NOP", GS_VIF1_NOP, 0x00000000u);
    eq32("FLUSHA", GS_VIF1_FLUSHA, 0x13000000u);
    eq32("DIRECT(6)", GS_VIF1_DIRECT(6u), 0x50000006u);
    eq32("DIRECT(0)", GS_VIF1_DIRECT(0u), 0x50000000u);
    eq32("DIRECT's immediate is the count", GS_VIF1_DIRECT(6u) & 0xFFFFu, 6u);
    /* MSKPATH3 is defined and deliberately unused -- recorded here so the omission ps2_gs_read.h
     * argues for is checkable rather than folklore: bit 15 of the immediate is the mask itself, which
     * is why ps2sdk's masked code word is 0x06008000 and the unmasking one is 0x06000000. */
    eq32("MSKPATH3(masked)", GS_VIF1_MSKPATH3(0x8000u), 0x06008000u);
    eq32("MSKPATH3(clear)", GS_VIF1_MSKPATH3(0u), 0x06000000u);
    /* The immediate field is 24 bits and nothing above it bleeds: a function number that ran into the
     * immediate would make the four codes of a quadword mean something other than four commands. */
    eq32("the immediate is 24 bits", GS_VIF1_DIRECT(0x1FFFFFFFu), 0x50FFFFFFu);
    /* The block is the code quadword plus the packet, in the units a caller allocates in: two u64
     * slots for the four 32-bit commands, then twelve for the six quadwords VIF1 hands to the GIF.  A
     * caller that sized its buffer from REQ_QWC would be two slots short and the block's last word --
     * TRXDIR, the one that arms the request -- is the word that would fall off the end. */
    eq32("BLOCK_U64 is the codes plus the packet", GS_READ_BLOCK_U64, 2u + 2u * GS_READ_REQ_QWC);
    eq32("the code quadword is 4 u32 commands", (GS_READ_BLOCK_U64 - 2u * GS_READ_REQ_QWC) * 2u, 4u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS Local->Host request vs hand-computed packet words\n");
    printf("==========================================================\n");

    sizes();
    one_block();
    source_control();
    direct_control();
    canvas_shape();
    refusals();
    vif_codes();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures) {
        printf("[CI-TEST] FAIL - a read request word is not what the field table says\n");
        return 1;
    }
    printf("[CI-TEST] SUCCESS - the VIF1 block matches its bit table, its laws and its source page\n");
    return 0;
}
