/*
 * ps2_gs_poster.h — a flat-colour fill sent to the GS as a batch of SPRITE primitives
 *
 * LANE 9b of the plan of record (doc/txt/PS2.txt, "The lanes", row 9, the poster half).  Row 9a
 * refused to draw what a recorded stream was already going to bury; that door only opens downward,
 * because a refusal can only take away a fill something else is replacing.  The background has
 * nothing above it -- no stream is ever recorded over the desktop's own canvas -- so its price can
 * only come down by the GS drawing it.  This file is that drawing.
 *
 * What a poster is, and what it is not.  It is one packed GIF group of `n` SPRITE primitives, all
 * the same colour, in one transfer: PRIM, RGBAQ, XYZ2, XYZ2 per rect, NLOOP = n, NREG = 4, PRE set
 * so the tag itself carries the primitive word.  It is not a texture page and not a second canvas:
 * the background's own 1,920,000 bytes do not fit in the 1,081,344 that [BENCH] gs planes: reports
 * free -- VRAM's unit is the allocator's 8 KB page, 132 of them left -- so a poster can carry a
 * FLAT COLOUR and nothing else.  That is the shape [BENCH] poster price: priced, of the plate's
 * 480,000 px -- 448,273 are one colour and 433,001 of those lie outside an art row's box.  What
 * [BENCH] poster census: added the next day is the other side of the same number: a live canvas
 * holds 57,923 px of that colour, and the cheapest possible per-pass decision -- one compare per
 * pixel -- costs 11,498 us against the 7,499 us the whole `bg=` column costs, so NO caller that
 * reads pixels can pay for itself at any content.  This file therefore ships the packet and no
 * caller.  doc/txt/PS2.txt's LANE 9 Price cell carries the rows and their reading; this file does
 * not restate them.
 *
 * The colour is a canvas word, not a COLOR.  The canvas reaches VRAM as the Host->Local IMAGE
 * stream of ps2_gs_upload() with no conversion at all -- the pixels are already in the GS's own
 * byte order (ps2_gs.c's ps2_fb() comment, and make test-ps2's byte-order rows) -- and CT32's 32
 * bits are A[31:24] B[23:16] G[15:8] R[7:0], which is the same layout RGBAQ packs.  So the poster
 * takes the canvas's own 32-bit word, puts it in RGBAQ's low half untouched, and the byte-order law
 * is enforced rather than described: gs_poster_packet()'s own host gate asserts the low 32 bits of
 * the RGBAQ word EQUAL the pixel it was handed.  A port that read that word as a BTRON COLOR would
 * swap R and B and paint the desktop brown at the same byte count, and nothing on the bus would
 * change size to give it away.
 *
 * Which pixels may be posterised was the lane's premise, and the census answered it: matching the
 * flat colour IS the exclusion.  A poster is a fill that lands on VRAM AFTER the band that carried
 * the canvas, so it would win over anything the canvas holds -- but a caller whose set is "the
 * pixels equal to this colour" cannot erase the pointer, a window's body or a glyph, because none
 * of them is that colour.  No rect list is needed to be safe with the fact that there is ONE canvas
 * and the pointer is drawn INTO it: the live path is draw_baremetal_mouse_cursor() at
 * core_ps2.c:1216, which passes screen->pixels to draw_baremetal_cursor_raw() at
 * src/desktop/desktop.c:348-350, while core_ps2.c:6017 binds the desktop's GDEV to
 * s_desktop_backbuffer and core_ps2.c:6406 hands that same array to the GS -- so the cursor's
 * 16x16 box does land in the very bytes the GIF upload reads, and it leaves the posterisable set by
 * being a different colour.  A rect list would only be needed by the OTHER caller -- the one that
 * reuses the plate's cached rects without looking -- and the census prices that one out.  One
 * caution for anyone re-checking this against the driver: ps2_gs.c:662 ps2_gs_draw_cursor() saves
 * and repaints a 16x16 box through ps2_fb() in the driver's own shape and has NO caller in src/ --
 * a dead twin, so gate on the live path above, not on that function's idea of where the cursor is.
 *
 * Coordinates are integer pixels converted by the recorder's own gs_vertex_coord16(), which since
 * LANE 12a is a shift and an add and so needs no float here either: this file used to carry a
 * second encoder for the same field, which is exactly the defect 12a removes.  The law is stated
 * once, in ps2_gs_pack.h, and the equality that used to be checked between two encodings is now
 * checked between the shift and the float spelling it replaced -- see coord_matches_float() in
 * verify/tests/test_ps2_gs_pack.c, which sweeps every coordinate the field can hold.  What is left
 * to assert here is only which corner goes into which field, and the packet words below still carry
 * hand-written literals for that.
 *
 * Pure: it fills a caller's buffer and touches no register, so every word it produces can be
 * compared with a hand-derived literal on a machine with no PS2 in it -- the ps2_gs_env.c split,
 * where getting it onto the bus is whoever owns the DMA's job (see ps2_gs_poster_draw()).
 *
 * Zero proprietary Sony SDK code: the field tables are in ps2_gs_pack.h and doc/txt/PS2.txt.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef PS2_GS_POSTER_H
#define PS2_GS_POSTER_H

#include <stdint.h>

/* Register writes per rect: the primitive word, the flat colour, and the two corners.  The sprite
 * needs both corners in ONE loop -- a SPRITE is complete when its second XYZ2 arrives -- which is
 * why this is 4 and not the recorder's 4 (whose fourth is ST) and not 2. */
#define GS_POSTER_REGS_N 4u

/* Quadwords in a batch of `n` rects: one tag pair plus four data words per rect.  Derived, never
 * repeated at the call site, so a caller cannot be handed a short buffer by a change here. */
#define GS_POSTER_QWC(n) (1u + (GS_POSTER_REGS_N) * (n))

/* One rect of the batch, in the units the compositor already uses: x0/y0 the first pixel it
 * covers, x1/y1 the first pixel it does NOT.  A four-wide int array instead of a RECT so this file
 * stays out of the desktop's headers -- the caller converts, and the conversion is gated below. */
typedef struct {
    int32_t x0, y0, x1, y1;
} gs_poster_rect;

/* Quadwords a caller must have room for to posterise `n` rects. */
uint32_t gs_poster_qwc(uint32_t n);

/* Fill exactly gs_poster_qwc(n) quadwords at `qw` with one packed SPRITE group of `n` rects, all
 * the colour the canvas word `canvas_px` says.  `cap_qw` is the room the caller has: a batch that
 * does not fit returns -1 and leaves the buffer alone, because half a frame of flat teal is a
 * picture with a hole in it and no receipt.
 *
 * Returns the quadwords written, or -1 for a bad argument (no rects, a short buffer, a null
 * pointer).  A SPRITE whose second corner is not strictly beyond its first draws nothing, so
 * degenerate rects are refused the same way -- and counted, because a caller that silently loses
 * rects is a caller whose `bg=` fell for a reason the [GSTS] row cannot show. */
int32_t gs_poster_packet(const gs_poster_rect *r, uint32_t n, uint32_t canvas_px,
                         uint64_t *qw, uint32_t cap_qw);

/* How many of the rects handed in were refused as degenerate at the time the packet was built.
 * Cleared by each gs_poster_packet() call, and read by the caller's diagnostic row -- the same
 * shape as gs_rec_unsplittable(), which is the precedent for counting a loss the packet itself
 * cannot carry. */
uint32_t gs_poster_dropped(void);

#endif /* PS2_GS_POSTER_H */
