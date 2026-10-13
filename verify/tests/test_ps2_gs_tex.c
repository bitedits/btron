/*
 * verify/tests/test_ps2_gs_tex.c -- a GL texture plane's VRAM address, against words written by hand
 *
 * Row 5, step 5b of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  The discipline is
 * increments 2, 3 and 4's: `ps2_gs_tex.c`, the packer and the VRAM cursor are compiled into this
 * program, and every expectation was derived from the register field tables before the code ran.
 *
 * What is under test is the arithmetic between two sides that each hold half the truth: TinyGL owns
 * bytes in RDRAM (an upload extent inside a plane rounded to a power of two per axis) and the GS
 * owns an address in its own 4 MB.  A plane that is planned one 256-byte block off draws a
 * neighbouring texture; a `TBW` derived from the upload instead of the padded plane skews every row
 * of the picture by the difference; and a span rounded to the wrong unit puts two planes in one
 * swizzle block.  None of that is visible on a screen as a wrong number, only as a wrong icon, so
 * it is gated here as words.
 *
 * The two planes are the real ones: the XMB's icon atlas (a 384x384 upload in a 512x512 allocation)
 * and its font sheet (128x96 in 128x128), so the byte counts and the 132 pages left at the end are
 * the budget the plan of record quotes, not a toy's.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "../../src/drivers/ps2/ps2_gs_pack.c"
#include "../../src/drivers/ps2/ps2_gs_vram.c"
#include "../../src/drivers/ps2/ps2_gs_tex.c"

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

static void check_num(const char *what, unsigned long got, unsigned long want)
{
    s_checks++;
    if (got != want) {
        s_failures++;
        printf("  [FAIL] %s got=%lu want=%lu\n", what, got, want);
    }
}

/* The registry never reads the plane's bytes -- it keys on the pointer and the upload path in
 * ps2_gs.c streams them -- so a gate needs distinct, stable addresses and nothing more. */
static char s_planes[GS_TEX_MAX + 4][64];

static const void *plane_of(int i)
{
    return &s_planes[i][0];
}

/* The numbers the two real sheets produce, derived by hand and named once so every case below
 * quotes the same plan.
 *
 * The cursor starts above the canvas: 13 units of 64 px x 600 rows x 4 B = 1,996,800, rounded up to
 * whole 8 KB pages is 244 pages = 1,998,848.  An icon atlas of 512x512 CT32 texels is 1,048,576
 * bytes -- already a whole number of the 16 KB plane units -- so the font sheet at 128x128 =
 * 65,536 follows it at 3,047,424, and 4,194,304 - 3,112,960 = 1,081,344 bytes are left: 132 whole
 * 8 KB pages, which is the budget row 5 of the plan says the GS backend has to live inside. */
static const uint32_t k_atlas_byte  = 1998848u;
static const uint32_t k_atlas_tba   = 7808u;      /* 1,998,848 / 256 */
static const uint32_t k_font_byte   = 3047424u;
static const uint32_t k_font_tba    = 11904u;     /* 3,047,424 / 256 */
static const uint32_t k_left_after  = 1081344u;

/* TEX0: TBA[13:0] TBW[19:14] PSM[25:20] TW[29:26] TH[33:30] TCC[34], the rest zero.  Atlas:
 * 7,808 + (8<<14) + (0<<20 for CT32) + (9<<26) + (9<<30) + (1<<34) = 0x664021E80.  Font:
 * 11,904 + (2<<14) + (7<<26) + (7<<30) + (1<<34) = 0x5DC00AE80. */
static const uint64_t k_tex0_atlas = 0x664021E80ULL;
static const uint64_t k_tex0_font  = 0x5DC00AE80ULL;

/* TEX1: LCM[0] MXL[4:2] MMAG[5] MMIN[8:6] MTBA[9] L[20:19] K[43:32].  Both sheets magnify with a
 * bilinear filter and minify with the nearest -- no mipmap chain, so MTBA is 0 and MXL 0 -- which
 * is MMAG alone: 1<<5 = 0x20. */
static const uint64_t k_tex1       = 0x20ULL;

/* CLAMP: WMS[1:0] WMT[3:2] MINU[13:4] MAXU[23:14] MINV[33:24] MAXV[43:34], with both modes
 * REPEAT (1) and both limits the whole padded plane, so the wrap period is pad_w -- the same
 * law TinyGL's own mask applies.  Atlas: 1 + (1<<2) + (511<<14) + (511<<34) = 0x7FC007FC005.
 * Font: the same with 127 = 0x1FC001FC005. */
static const uint64_t k_clamp_atlas = 0x7FC007FC005ULL;
static const uint64_t k_clamp_font  = 0x1FC001FC005ULL;

/* What the registry says about a plane, all of it checked. */
static void plan_words(const char *tag, const gs_tex_t *t, uint32_t byte, uint32_t tba,
                       uint32_t tbw, uint32_t tw_exp, uint32_t th_exp, uint32_t qwc,
                       uint64_t tex0, uint64_t clamp)
{
    char label[96];

    sprintf(label, "%s: VRAM base offset", tag);
    check_num(label, t->byte, byte);
    sprintf(label, "%s: TEX0.TBA in 256-byte blocks", tag);
    check_num(label, t->tba, tba);
    sprintf(label, "%s: TEX0.TBW in units of 64 texels", tag);
    check_num(label, t->tbw, tbw);
    sprintf(label, "%s: TEX0.TW is the padded width's exponent", tag);
    check_num(label, t->tw_exp, tw_exp);
    sprintf(label, "%s: TEX0.TH is the padded height's exponent", tag);
    check_num(label, t->th_exp, th_exp);
    sprintf(label, "%s: the Host->Local stream's quadword count", tag);
    check_num(label, t->qwc, qwc);
    sprintf(label, "%s: TEX0", tag);
    check_word(label, t->tex0, tex0);
    sprintf(label, "%s: TEX1", tag);
    check_word(label, t->tex1, k_tex1);
    sprintf(label, "%s: CLAMP", tag);
    check_word(label, t->clamp, clamp);
}

/* The two sheets the XMB binds, planned and word-for-word. */
static void test_two_sheets(void)
{
    const gs_tex_t *atlas, *font;

    gs_tex_init();
    check_num("a fresh registry holds nothing", gs_tex_count(), 0u);
    check_num("and the cursor starts above the canvas", gs_tex_vram_left(),
              4194304u - 1998848u);

    atlas = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check("the icon atlas is planned", atlas != 0);
    if (atlas == 0) return;
    plan_words("atlas", atlas, k_atlas_byte, k_atlas_tba, 8u, 9u, 9u, 65536u,
               k_tex0_atlas, k_clamp_atlas);

    /* The law the whole file rests on: the GS addresses the plane, not the upload.  384 is what
     * the app wrote; 512 is what TEX0 names, and the difference is padding this plane owns. */
    check_num("the upload extent is recorded as what was uploaded", atlas->px_w, 384u);
    check_num("but TW names the 512-texel plane", 1u << atlas->tw_exp, 512u);
    check("and the stream carries the padded plane, padding included",
          atlas->pad_w == 512u && atlas->qwc == 512u * 512u / 4u);

    font = gs_tex_register(plane_of(1), 128u, 96u, 128u, 128u);
    check("the font sheet is planned", font != 0);
    if (font == 0) return;
    plan_words("font", font, k_font_byte, k_font_tba, 2u, 7u, 7u, 4096u,
               k_tex0_font, k_clamp_font);

    check("two planes, two slots, in registration order",
          gs_tex_count() == 2u && gs_tex_at(0u) == atlas && gs_tex_at(1u) == font);
    check("and a slot past the end answers nothing",
          gs_tex_at(2u) == 0 && gs_tex_at(0xFFFFFFFFu) == 0);

    /* No shared byte, in either unit the registers use: the spans are whole 16 KB planes. */
    check_num("the second plane starts after the first plane's whole span",
              font->byte, k_atlas_byte + 1048576u);
    check_num("which leaves the 132 pages the plan of record budgets",
              gs_tex_vram_left(), k_left_after);

    check("a draw call's question -- have these bytes an address? -- answers by pointer",
          gs_tex_find(plane_of(0)) == atlas && gs_tex_find(plane_of(1)) == font);
    check("and a texture that was never registered answers 0 rather than somebody else's page",
          gs_tex_find(plane_of(2)) == 0 && gs_tex_find(0) == 0);

    /* With no sink installed -- the host gate -- the bytes have not crossed, which is what a
     * present path logs. */
    check_num("both planes wait for their bytes", gs_tex_pending(), 2u);
    check("and an entry's own flag says so", atlas->pending == 1 && font->pending == 1);
}

/* The refusals, each of which has to leave the cursor and the registry exactly as they were: a
 * half-registered plane is a draw from an address nobody claimed. */
static void test_refusals(void)
{
    uint32_t left;

    gs_tex_init();
    left = gs_tex_vram_left();

    check("a null plane is refused", gs_tex_register(0, 64u, 64u, 64u, 64u) == 0);
    check("so is an empty extent",
          gs_tex_register(plane_of(0), 0u, 64u, 64u, 64u) == 0 &&
          gs_tex_register(plane_of(0), 64u, 0u, 64u, 64u) == 0);
    /* TW/TH are exponents, so a padded axis that is not a power of two has no name in the
     * register at all -- and gl_image_alloc() already rounded it, so this is a caller that read
     * the wrong field of its own image. */
    check("a padded width that is not a power of two has no TEX0",
          gs_tex_register(plane_of(0), 100u, 128u, 100u, 128u) == 0);
    check("nor a padded height",
          gs_tex_register(plane_of(0), 128u, 100u, 128u, 100u) == 0);
    /* The transfer's RRW/RRH would read past the source and the clamp would name texels the app
     * never wrote. */
    check("an upload larger than its plane is refused",
          gs_tex_register(plane_of(0), 128u, 64u, 64u, 64u) == 0 &&
          gs_tex_register(plane_of(0), 64u, 128u, 64u, 64u) == 0);

    check_num("and none of them claimed VRAM", gs_tex_vram_left(), left);
    check_num("none of them took a slot", gs_tex_count(), 0u);

    /* An upload exactly the plane's size is the ordinary 1:1 case and must not be refused by the
     * extent check. */
    check("an upload that exactly fills its plane is planned",
          gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u) != 0);
}

/* The sink: the plan is complete when the bus owner is told, and the telling happens while the
 * application is still inside the upload -- not at frame end, which is the order that would put a
 * primitive sampling a plane before the transfer that fills it. */
static uint32_t s_sink_calls, s_sink_last, s_sink_seen_byte, s_sink_seen_qwc;
static int s_sink_seen_pending;

static void sink_take(uint32_t index)
{
    const gs_tex_t *t = gs_tex_at(index);

    s_sink_calls++;
    s_sink_last = index;
    /* Read what the entry says *during* the call: 0 here would be a plan handed over empty. */
    if (t != 0) {
        s_sink_seen_byte = t->byte;
        s_sink_seen_qwc = t->qwc;
        s_sink_seen_pending = t->pending;
    } else {
        s_sink_seen_byte = 0u;
        s_sink_seen_qwc = 0u;
        s_sink_seen_pending = -1;
    }
}

static void test_sink_at_registration(void)
{
    const gs_tex_t *atlas, *again;

    gs_tex_init();
    s_sink_calls = 0u;
    gs_tex_install_sink(sink_take);

    atlas = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check_num("the bus owner is told during the registration, once", s_sink_calls, 1u);
    check_num("with the slot it can read back", s_sink_last, 0u);
    check_num("and a plan already complete: the VRAM address", s_sink_seen_byte, k_atlas_byte);
    check_num("the stream length", s_sink_seen_qwc, 65536u);
    check("the bytes are pending right then, because the sink has not said otherwise",
          s_sink_seen_pending == 1);
    check_num("so the registry still counts it pending", gs_tex_pending(), 1u);

    /* The sink that says the bytes crossed is the only thing that clears the flag; the registry
     * cannot know about a DMA it does not own. */
    gs_tex_uploaded(0u);
    check_num("and once it has, nothing is pending", gs_tex_pending(), 0u);
    gs_tex_uploaded(9u);
    check("a slot that does not exist is not an error, just nothing", gs_tex_at(9u) == 0);

    /* Same pointer, same geometry: gl_image_alloc()'s "same plane, new contents".  The address and
     * the words stand and the bytes cross again -- one slot, no second span. */
    again = gs_tex_register(plane_of(0), 384u, 384u, 512u, 512u);
    check("the re-upload is the same entry", again == atlas);
    check_num("handed over again", s_sink_calls, 2u);
    check_num("with the same slot", s_sink_last, 0u);
    check("the same plan, so the same address and the same length",
          again->byte == k_atlas_byte && again->qwc == 65536u && again->tex0 == k_tex0_atlas);
    check("and a smaller upload extent into the same plane keeps the plane",
          gs_tex_register(plane_of(0), 256u, 256u, 512u, 512u) == atlas &&
          atlas->px_w == 256u && atlas->byte == k_atlas_byte);
    check_num("still one slot", gs_tex_count(), 1u);
    check_num("and still the VRAM the first registration took", gs_tex_vram_left(),
              4194304u - 1998848u - 1048576u);

    /* An installed sink of 0 means no bus: both entries are still planned, and pending counts
     * them -- the re-upload that no sink handed over this time included, because the flag is the
     * sink's to clear and nobody has. */
    gs_tex_install_sink(0);
    s_sink_calls = 0u;
    check("clearing the sink still plans",
          gs_tex_register(plane_of(1), 128u, 96u, 128u, 128u) != 0);
    check_num("and tells nobody", s_sink_calls, 0u);
    check_num("so both the new plane and the un-handed re-upload are pending",
              gs_tex_pending(), 2u);
}

/* A pointer the C heap handed to a different texture: the identity matches, the geometry does not,
 * so the entry is re-planned and takes a fresh span.  Reusing the old span is the bug this case
 * exists for -- the abandoned plane's bytes would still be addressed by the new plane's TEX0. */
static void test_recycled_pointer(void)
{
    const gs_tex_t *first, *again;
    uint32_t old_byte, left;
    uint64_t old_tex0, old_clamp;

    gs_tex_init();
    first = gs_tex_register(plane_of(0), 128u, 128u, 128u, 128u);
    check("a 128x128 sheet is planned at the cursor", first != 0 && first->byte == k_atlas_byte);
    if (first == 0) return;
    /* Taken before the re-plan, because the re-plan writes through the same entry: `again` and
     * `first` are one object, so "the old span is gone" can only be said against a copy. */
    old_byte  = first->byte;
    old_tex0  = first->tex0;
    old_clamp = first->clamp;
    left = gs_tex_vram_left();
    check_num("and it claimed one 64 KiB span", left, 4194304u - 1998848u - 65536u);

    again = gs_tex_register(plane_of(0), 64u, 64u, 64u, 64u);
    check("the same pointer with different geometry is the same slot", again == first);
    check_num("re-planned into a fresh span, not the old one", again->byte, 1998848u + 65536u);
    check_num("whose TBA is that offset in 256-byte blocks", again->tba, 8064u);
    check("and whose words all moved with it",
          again->tbw == 1u && again->tw_exp == 6u && again->th_exp == 6u);
    check_word("TEX0 for a 64x64 plane at 2,064,384", again->tex0, 0x598005F80ULL);
    check_word("CLAMP over 64 texels on both axes", again->clamp, 0xFC000FC005ULL);
    check_num("the abandoned 64 KiB is spent, not hidden", gs_tex_vram_left(),
              4194304u - 2064384u - 16384u);
    check("and no word the entry hands out now names the abandoned span",
          again->byte != old_byte && again->tex0 != old_tex0 && again->clamp != old_clamp);
    check_num("still one slot", gs_tex_count(), 1u);

    /* A *third* plane lands after the re-plan, which is the proof the cursor moved rather than the
     * slot being patched in place. */
    check("a new plane starts where the re-plan ended",
          gs_tex_register(plane_of(1), 64u, 64u, 64u, 64u) != 0 &&
          gs_tex_at(1u)->byte == 2064384u + 16384u);
}

/* The cap and the end of VRAM, both of which have to refuse rather than draw from an address
 * nobody owns. */
static void test_cap_and_exhaustion(void)
{
    const gs_tex_t *a, *b, *c;
    uint32_t i, left;

    gs_tex_init();
    for (i = 0u; i < GS_TEX_MAX; i++)
        if (gs_tex_register(plane_of((int)i), 64u, 64u, 64u, 64u) == 0) break;
    check("the registry fills to its cap without complaint", i == GS_TEX_MAX);
    check_num("every slot is taken", gs_tex_count(), GS_TEX_MAX);
    left = gs_tex_vram_left();
    check("a plane past the cap is refused, not silently dropped into a slot in use",
          gs_tex_register(plane_of((int)GS_TEX_MAX), 64u, 64u, 64u, 64u) == 0);
    check_num("the cap did not move", gs_tex_count(), GS_TEX_MAX);
    check_num("nor the cursor", gs_tex_vram_left(), left);

    /* Now the real limit: 4 MB, from a cursor already 1,998,848 in.  Two 512x512 planes fit and a
     * third does not. */
    gs_tex_init();
    a = gs_tex_register(plane_of(0), 512u, 512u, 512u, 512u);
    b = gs_tex_register(plane_of(1), 512u, 512u, 512u, 512u);
    check("two icon atlases fit", a != 0 && b != 0);
    left = gs_tex_vram_left();
    check_num("and the room they leave is 96 KiB", left, 4194304u - 4096000u);
    c = gs_tex_register(plane_of(2), 512u, 512u, 512u, 512u);
    check("a third does not: it is refused whole", c == 0);
    check_num("with the cursor untouched", gs_tex_vram_left(), left);
    check_num("and no slot half-written", gs_tex_count(), 2u);

    /* A plane that does fit still lands. */
    check("a sheet that fits the remainder is planned",
          gs_tex_register(plane_of(3), 128u, 128u, 128u, 128u) != 0);
    check_num("leaving 32 KiB", gs_tex_vram_left(), 32768u);
    check("and a 256x256 plane needs more than that",
          gs_tex_register(plane_of(4), 256u, 256u, 256u, 256u) == 0);

    /* Recycling a pointer whose re-plan does not fit kills the entry rather than leaving it keyed
     * on an address the old plane owned: the alternative is a draw from somebody else's page. */
    check("the recycled re-plan that does not fit is refused",
          gs_tex_register(plane_of(0), 256u, 256u, 256u, 256u) == 0);
    check("the entry is dead rather than stale",
          gs_tex_at(0u) != 0 && gs_tex_at(0u)->plane == 0 && gs_tex_at(0u)->pending == 0);
    check("and the dead key no longer answers a draw call", gs_tex_find(plane_of(0)) == 0);
    check("a new plane is still planned after it", gs_tex_register(plane_of(5), 64u, 64u,
                                                                   64u, 64u) != 0);
    check_num("which lands in the next free slot, not the dead one", gs_tex_count(), 4u);
    check("so the dead slot is skipped: the living three are the ones that answer",
          gs_tex_at(3u)->plane == plane_of(5) && gs_tex_at(0u)->plane == 0);
    check_num("and the killed entry is the only one that stops counting as pending",
              gs_tex_pending(), 3u);
}

/* The unit question the whole registry rests on: a plane's span is the *padded* picture's, on the
 * 16 KB unit gs_vram_tex_bytes() documents. */
static void test_plane_span(void)
{
    /* 32x128 texels: the width field is a whole number of 64-texel units, so the plane the GS
     * addresses is 64 wide and the span is 64*128*4 = 32,768, not 32*128*4 = 16,384. */
    check_num("a narrow plane is padded to the width unit before it is rounded",
              gs_vram_tex_bytes(32u, 128u), 32768u);
    check_num("and a 64x64 plane is one 16 KiB unit", gs_vram_tex_bytes(64u, 64u), 16384u);
    check_num("a 512x512 atlas is 64 of them", gs_vram_tex_bytes(512u, 512u), 1048576u);
    check_num("which is also its quadword count x 16",
              gs_vram_tex_bytes(512u, 512u), 65536u * 16u);
    /* The case the 16 KB unit exists for: 64x32 CT32 is 8,192 bytes, which is a whole 8 KB page and
     * half a plane.  Rounded to the page a frame buffer uses, two such planes would share one 4 KB
     * swizzle block -- the unit the picture is built out of -- which is why the answer is 16,384. */
    check_num("a plane smaller than one 16 KiB unit still takes the whole unit",
              gs_vram_tex_bytes(64u, 32u), 16384u);
    check_num("and one 64x64 plane is the smallest legal span",
              gs_vram_tex_bytes(64u, 64u), 16384u);
    check_num("TBW names 64-texel units, rounded up", gs_vram_fbw(512u), 8u);
    /* The atlas's 384-wide upload would be 6 units, and 6 is the wrong answer: rows would be read
     * 128 texels apart from where gl_image_alloc() put them.  TBW comes from the plane, which is
     * what test_two_sheets() already gated at 8. */
    check_num("an upload's own width is not the field's value", gs_vram_fbw(384u), 6u);

    gs_tex_init();
    check("the registry plans a narrow plane at its padded width",
          gs_tex_register(plane_of(6), 32u, 128u, 64u, 128u) != 0);
    check("with TBW from the plane, not the upload",
          gs_tex_at(0u)->tbw == 1u && gs_tex_at(0u)->px_w == 32u);
    check_num("and a stream that carries the whole padded plane", gs_tex_at(0u)->qwc,
              64u * 128u / 4u);
    check_num("which is the span the cursor moved by", gs_tex_vram_left(),
              4194304u - 1998848u - 32768u);
}

int main(void)
{
    printf("==========================================================\n");
    printf(" PS2 GS texture-plane registry vs hand-built TEX0/TEX1/CLAMP...\n");
    printf("==========================================================\n");

    test_two_sheets();
    test_refusals();
    test_sink_at_registration();
    test_recycled_pointer();
    test_cap_and_exhaustion();
    test_plane_span();

    printf("  checks=%d failures=%d\n", s_checks, s_failures);
    if (s_failures == 0) {
        printf("[CI-TEST] SUCCESS - every plane's address and words are what the fields say\n");
        return 0;
    }
    printf("[CI-TEST] FAIL - a texture plane is planned in a unit the registers do not use\n");
    return 1;
}
