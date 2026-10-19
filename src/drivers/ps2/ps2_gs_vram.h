/*
 * ps2_gs_vram.h — GS local-memory allocation, in the units the registers actually use
 *
 * The GS has 4 MB of local memory and addresses it with two different page sizes, which is
 * the fact this file exists to keep straight: a texture or transfer address (TEX0.TBA,
 * BITBLTBUF.SBA/DBA) is a 14-bit index of 64-word units -- 256 bytes -- and a frame or
 * Z-buffer base (FRAME.FBA, ZBUF.ZBA) is a 9-bit index of 2048-word units -- 8192 bytes.
 * Each width times its unit is exactly 4 MB, which is what makes the reading certain rather
 * than merely remembered; the field tables are quoted in doc/txt/PS2.txt, chapter 18.
 *
 * Everything here is arithmetic on byte offsets, so it is testable on the host: the gate
 * (verify/tests/test_ps2_gs_env.c) asserts the page math and, importantly, the one budget
 * consequence the whole backend design hangs on -- that the canvas and a second full-screen
 * layer do not coexist.  Zero proprietary Sony SDK code.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_VRAM_H
#define PS2_GS_VRAM_H

#include <stdint.h>

#define GS_VRAM_BYTES   (4u * 1024u * 1024u)
#define GS_PAGE_FRAME    8192u   /* FRAME.FBA / ZBUF.ZBA: 9 bits of 2048 words */
#define GS_PAGE_TEX       256u   /* TEX0.TBA / BITBLTBUF: 14 bits of 64 words */

#define GS_FBA_MAX      511u     /* the 9-bit frame/Z page index */
#define GS_TBA_MAX    16383u     /* the 14-bit texture/transfer page index */

/* The canvas the CPU blit streams into VRAM every present: CT32, 600 rows of a buffer whose
 * width field is 13 units of 64 pixels (800 px rounds up to 832).  That is the extent the
 * allocator must stay clear of, and it is rounded up to whole 8 KB pages below so the first
 * allocation after it is expressible as a 9-bit FBA without rounding.  What that rounding buys
 * is stated as arithmetic at the end of this file, in the display-page section: the reservation
 * is no longer the canvas's own extent but gs_disp_reserve_bytes(), and the two happen to be the
 * same 244 pages, which is the whole reason a second page costs nothing. */
#define GS_VRAM_CANVAS_ROWS     600u
#define GS_VRAM_CANVAS_FBW      13u
#define GS_VRAM_CANVAS_BYTES    (GS_VRAM_CANVAS_FBW * 64u * GS_VRAM_CANVAS_ROWS * 4u)

#define GS_VRAM_MAX_BLOCKS 64u

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint8_t  used;
} gs_vram_block_t;

/* A cursor over VRAM, in bytes, with block tracking for compaction.
 * v->next remains the bump pointer for full compatibility. */
typedef struct {
    uint32_t next;
    uint32_t base;
    uint32_t block_count;
    gs_vram_block_t blocks[GS_VRAM_MAX_BLOCKS];
} gs_vram_t;

/* Start above the canvas.  The GS backend never allocates page 0: that is where the PCRTC
 * reads the screen from, and the blit path writes it with DBA=0 today. */
void gs_vram_init(gs_vram_t *v);

/* Free a previously allocated span starting at `byte`. Returns 0 on success, -1 if not found. */
int gs_vram_free(gs_vram_t *v, uint32_t byte);

/* Compact the VRAM arena by reclaiming freed trailing spans. Returns bytes reclaimed. */
uint32_t gs_vram_compact(gs_vram_t *v);

/* Bytes needed by a buffer of pixels_w x pixels_h at bpp bits per pixel, in the GS's own
 * layout: the width field is a whole number of 64-pixel units, so the buffer is wider than
 * the picture it holds whenever the width is not a multiple of 64. */
uint32_t gs_vram_frame_bytes(uint32_t pixels_w, uint32_t pixels_h, uint32_t bpp);

/* Reserve that many bytes.  Returns 0 and the byte offset, or -1 and leaves the cursor
 * untouched when the request is empty or would run past the end of VRAM. */
int gs_vram_take(gs_vram_t *v, uint32_t bytes, uint32_t *out_byte);

/* The allocation unit of a texture plane.  A swizzled CT32 picture is built out of 4 KB blocks
 * of 32x32 texels inside 8 KB pages, and its address enters two fields with different units --
 * TEX0.TBA and BITBLTBUF.DBA count 256-byte blocks, TEX0.TBW counts 64-texel buffer widths -- so
 * a plane is rounded to 16 KB: a whole number of blocks, of pages, and of pages in even pairs,
 * which means no two planes ever share either unit.  The cost is nil for the two planes this
 * port has (512x512 and 128x128 are exact multiples of it), and it is the difference between a
 * narrow future plane and its neighbour's texels arriving in the same block. */
#define GS_PAGE_TEX_PLANE 16384u

/* A texture plane of w x h CT32 texels, rounded as above. */
uint32_t gs_vram_tex_bytes(uint32_t w_texels, uint32_t h_texels);

/* Reserve a texture plane.  Same law as gs_vram_take, on the 16 KB unit. */
int gs_vram_take_tex(gs_vram_t *v, uint32_t bytes, uint32_t *out_byte);

/* The same offset as the two register fields.  -1 when it cannot be expressed: an offset that
 * is not a whole 8 KB page is not a rounding problem for FBA, it is a plan that does not fit,
 * so the caller has to change the layout rather than silently move the buffer. */
int gs_vram_fba(uint32_t byte, uint32_t *out_fba);
int gs_vram_tba(uint32_t byte, uint32_t *out_tba);

/* FBW from a pixel width: units of 64 pixels, rounded up. */
uint32_t gs_vram_fbw(uint32_t pixels_w);

uint32_t gs_vram_left(const gs_vram_t *v);

/* ── Display pages: the VRAM the PCRTC reads ─────────────────────────────
 *
 * LANE 21's arithmetic, and the one number the double-buffer lane hangs on.  A CT32 canvas page
 * is 832 x 600 x 4 = 1,996,800 bytes and FRAME.FBA counts 2048-word (8,192-byte) pages, so the
 * reservation this file has always made for it is 244 pages = 1,998,848 bytes.  A PSMCT16 page is
 * half the bytes -- 998,400 -- which is 121.875 of those pages, so a page rounds up to 122 =
 * 999,424 and TWO of them are 1,998,848: exactly the space one CT32 page already occupies, to the
 * byte.  Flipping between two pages therefore needs no new VRAM, moves no texture plane, and leaves
 * gs_vram_left() at the 1,081,344 the census measured before this lane existed.  The rounding is
 * not tidiness: gs_vram_fba() refuses an offset that is not a whole 8 KB page, so 998,400 is not a
 * legal second base and 999,424 is.
 *
 * A CT24 page would have been the other candidate and is not the choice: 1,497,600 bytes a page
 * rounds to 183 pages, two of them 3,001,344, which leaves 1,192,960 for the atlas and the font
 * sheet -- 1,114,112 of that, so 78,848 bytes of slack against the 1,081,344 CT16 leaves.  It also
 * saves a quarter of the bandwidth where CT16 saves a half, and it is not a format the GS rasterises
 * into with dither, so the canvas and the stream would disagree about how 8 bits become the page.
 * CT16 is the format the hardware's own fragment path writes at 16 bpp, which is what gs_pack_5551()
 * matches, and it is the one that fits twice.
 *
 * The pages below are *named*, not allocated: gs_vram_init() keeps the allocator's cursor above both
 * of them, so a flip cannot fail the way a mid-frame allocation can. */

#define GS_DISP_PX_W   (GS_VRAM_CANVAS_FBW * 64u)   /* 832: the field, not the screen's 800 */
#define GS_DISP_ROWS   GS_VRAM_CANVAS_ROWS          /* 600 */
#define GS_DISP_PAGES  2u                           /* the two a PMODE circuit flip alternates on */

/* Bytes in one pixel of a canvas format, or 0 for a format this file cannot lay out a page for.
 * PSMCT24 is deliberately refused: three bytes a texel puts a row on no 4-, 8- or 16-byte boundary,
 * which is the law every one of the fields below has to satisfy. */
uint32_t gs_disp_bytes_pp(uint32_t psm);

/* Pixels in one GIF IMAGE quadword -- the stream's granularity, and the direction it moves when the
 * format halves: a quadword is 16 BYTES, so a 16-bit page takes EIGHT pixels per word where a 32-bit
 * one takes four.  The upload path's `x &= ~3` and its round-up-to-4 become ~7 and 8, which is why a
 * 16-bit upload is not the 32-bit one with a different shift constant. */
uint32_t gs_disp_px_per_qw(uint32_t psm);

/* What one page holds, and what it occupies: the extent actually written, and that rounded up to
 * whole FRAME.FBA pages so the next page's base is expressible.  0 for a refused format. */
uint32_t gs_disp_page_bytes(uint32_t psm);
uint32_t gs_disp_page_span(uint32_t psm);

/* Every display page in the configuration this file reserves: the larger of one CT32 page rounded
 * up and GS_DISP_PAGES CT16 pages.  Derived rather than written out, because the two configurations
 * must not be able to drift apart and move a texture plane between one bench run and the next. */
uint32_t gs_disp_reserve_bytes(void);

/* One page, in every unit a writer or a reader needs.  `fba`/`tba` are the same offset in the two
 * field units this file keeps straight, and both are computed here so no caller can guess which one
 * a register wants. */
typedef struct {
    uint32_t index;         /* 0 or 1 */
    uint32_t psm;           /* GS_PSM_CT32 or GS_PSM_CT16, from ps2_gs.h */
    uint32_t byte;          /* VRAM offset: a whole number of GS_PAGE_FRAME pages */
    uint32_t fba;           /* byte / 8192 -- FRAME.FBA, ZBUF.ZBA, DISPFB.FBP */
    uint32_t tba;           /* byte / 256  -- BITBLTBUF.DBA/SBA */
    uint32_t fbw;           /* GS_VRAM_CANVAS_FBW: units of 64 pixels */
    uint32_t bytes_pp;
    uint32_t px_per_qw;
    uint32_t row_bytes;     /* fbw * 64 * bytes_pp: VRAM's pitch, which the GIF stream cannot carry */
    uint32_t rows;
    uint32_t bytes;         /* the extent written: GS_DISP_PX_W * rows * bytes_pp */
} gs_disp_page_t;

/* -1 when the page cannot be described -- an index this configuration has no page for, a refused
 * format, or a span that would run past the reservation.  Nothing allocates. */
int gs_disp_page(uint32_t index, uint32_t psm, gs_disp_page_t *out);

/* ── The live display configuration: the canvas format and the two page indices ──
 *
 * One owner for the state, because it is one fact: the format the page is in, the page the PCRTC
 * reads, and the page a writer may touch.  Today all three are what the port has always had (CT32,
 * page 0, page 0), so routing the writers here is a no-op on the wire -- and that is the point:
 * LANE 21 rung 3 changes the format and rung 4 lets the two indices differ, and each of those is
 * then one call rather than a hunt for a literal.
 *
 * The state lives in the same TU as the arithmetic above so that the host gate can drive it --
 * ps2_gs.c cannot be compiled where there is no GS -- and it is single-threaded by this port's law:
 * the PS2 has no scheduler, so nothing can read a half-written configuration. */

/* Rebuild for a new canvas format.  Returns 0, or -1 and leaves the previous configuration in place
 * when gs_disp_page() refuses the format -- a screen that stays what it was beats one that follows
 * a half-applied dial. */
int gs_disp_set_psm(uint32_t psm);
uint32_t gs_disp_psm(void);

/* The page the PCRTC is showing -- what a census of the screen must read -- and the page writers
 * target.  With double-buffering off they are the same page, which is what they are today; with it
 * on, gs_disp_drawn() is always the page nobody is scanning. */
const gs_disp_page_t *gs_disp_shown(void);
const gs_disp_page_t *gs_disp_drawn(void);

/* Turn the two-page model on or off.  Off (the default) puts every writer on the shown page, which
 * is the port's current single-buffer present; on puts them on the other one and returns 0.  On
 * returns -1 without changing anything when the current format has no second page inside the
 * reservation -- PSMCT32's does not, PSMCT16's does -- because a back page that overlaps a texture
 * plane is a corruption, not a buffer.  Turning it on without a PMODE store that follows is a screen
 * that never changes, so this is LANE 21 rung 4's dial and nothing calls it with 1 until the flip
 * exists. */
int gs_disp_set_double(uint32_t on);
uint32_t gs_disp_is_double(void);

/* The store landed: the page that was being drawn is now the one being scanned, and the writers move
 * to the other.  Returns the new shown index.  Does NOT touch the hardware -- the PMODE write that
 * acts on this is the caller's, and it has to sit between a draw-sync and a vsync, which is a bus
 * question and not an allocator one. */
uint32_t gs_disp_flip_commit(void);

#endif /* PS2_GS_VRAM_H */
