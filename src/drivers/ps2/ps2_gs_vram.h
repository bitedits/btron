/*
 * ps2_gs_vram.h — GS local-memory allocation, in the units the registers actually use
 *
 * The GS has 4 MB of local memory and addresses it with two different page sizes, which is
 * the fact this file exists to keep straight: a texture or transfer address (TEX0.TBA,
 * BITBLTBUF.SBA/DBA) is a 14-bit index of 64-word units -- 256 bytes -- and a frame or
 * Z-buffer base (FRAME.FBA, ZBUF.ZBA) is a 9-bit index of 2048-word units -- 8192 bytes.
 * Each width times its unit is exactly 4 MB, which is what makes the reading certain rather
 * than merely remembered; the field tables are quoted in doc/md/PS2.md, section 2.1.
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
 * allocation after it is expressible as a 9-bit FBA without rounding. */
#define GS_VRAM_CANVAS_ROWS     600u
#define GS_VRAM_CANVAS_FBW      13u
#define GS_VRAM_CANVAS_BYTES    (GS_VRAM_CANVAS_FBW * 64u * GS_VRAM_CANVAS_ROWS * 4u)

/* A bump cursor over VRAM, in bytes.  One cursor rather than a frame end and a texture end:
 * with two, a texture allocation and a frame allocation can be handed the same addresses,
 * and the failure is a screen of noise at runtime.  All sizes round up to GS_PAGE_FRAME, so
 * the cursor is always a valid 9-bit FBA candidate; the cost is at most 8 KB less than exact
 * for a small texture, against a total budget in which the icon atlas is 72 whole pages. */
typedef struct {
    uint32_t next;
} gs_vram_t;

/* Start above the canvas.  The GS backend never allocates page 0: that is where the PCRTC
 * reads the screen from, and the blit path writes it with DBA=0 today. */
void gs_vram_init(gs_vram_t *v);

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

#endif /* PS2_GS_VRAM_H */
