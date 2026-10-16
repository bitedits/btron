/*
 * ps2_gs_poster.h — a flat-colour fill sent to the GS as a batch of SPRITE primitives
 *
 * LANE 9b of the plan of record (doc/txt/PS2.txt, "The lanes", row 9, the poster half).  Row 9a
 * refused to draw what a recorded stream was already going to bury; that door only opens downward,
 * because a refusal can only take away a fill something else is replacing.  The background has
 * nothing above it -- no stream is ever recorded over the desktop's own canvas -- so its price can
 * only come down by the GS drawing it.  This file is that drawing.
 *
 * What a poster is, and what it is not.  It is one GIF group of `n` SPRITE primitives, all the same
 * colour, in one transfer: NLOOP = n, NREG = 4, FLG = REGLIST and PRE clear, so PRIM, RGBAQ and the
 * two corners arrive as the group's own four registers, packed densely into two quadwords per rect.
 * It used to be FLG=PACKED with PRE set and one quadword per register; that is the shape LANE 19
 * rung 5 measured as drawing nothing on this GS, and the finding is recorded at the bottom of this
 * header.  It is not a texture page and not a second canvas:
 * the background's own 1,920,000 bytes do not fit in the 1,081,344 that [BENCH] gs planes: reports
 * free -- VRAM's unit is the allocator's 8 KB page, 132 of them left -- so a poster can carry a
 * FLAT COLOUR and nothing else.  That is the shape [BENCH] poster price: priced, of the plate's
 * 480,000 px -- 448,273 are one colour and 433,001 of those lie outside an art row's box.  What
 * [BENCH] poster census: added the next day is the other side of the same number: a live canvas
 * holds 57,923 px of that colour, and the cheapest possible per-pass decision -- one compare per
 * pixel -- costs 11,498 us against the 7,499 us the whole `bg=` column costs, so NO caller that
 * reads pixels can pay for itself at any content.  This file therefore ships the packet and no
 * PRODUCTION caller.  The one caller it does have -- src/cores/core_ps2.c's `gsquad` probe, LANE 19
 * rung 1 -- sends two one-rect batches, to ask whether the GS rasterises on the displayed page and
 * which of the two senders delivered it, and it reads no pixels, so it is not the caller the census
 * priced out.  doc/txt/PS2.txt's LANE 9 Price cell carries the rows and their reading; this file
 * does not restate them.
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
 * where getting it onto the bus is whoever owns the DMA's job -- in the image that is
 * src/cores/core_ps2.c's `gsquad` probe, which sends the finished packet through ps2_gs_gif_send().
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

/* GIF quadwords per rect.  This is HALF the register count because the group is a REGLIST list: in
 * that format the NREG 64-bit registers of one loop are packed densely into NREG/2 128-bit units,
 * the first in the low half, and LANE 19 rung 5 measured that this GS does not honour the sparse
 * form of the same list (see the rung-5 note below the reference builders).  The division is exact
 * only while the register count is even, which it is by construction, and make test-ps2-gs-poster
 * asserts it -- a fifth register would round down here and hand the GIF a group one word short. */
#define GS_POSTER_QW_PER_RECT (GS_POSTER_REGS_N / 2u)

/* Quadwords in a batch of `n` rects: one tag pair plus two data quadwords per rect.  Derived, never
 * repeated at the call site, so a caller cannot be handed a short buffer by a change here. */
#define GS_POSTER_QWC(n) (1u + GS_POSTER_QW_PER_RECT * (n))

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

/* ── LANE 19 rung 4: the reference-shaped controls ─────────────────────────────────────────────
 *
 * rung 3 closed the question "did the SPRITE land somewhere the strips do not cover" with a whole
 * page that holds no pixel of either magic colour, on either sender.  What it cannot say is WHICH
 * of three things is false, because all three are inside the same packet:
 *
 *   the GIF format -- this file emitted FLG=PACKED (REG ordering) with NREG=4, PRE=1, and one
 *     128-bit quadword per register;
 *   the vertex spelling -- this file puts each corner at exactly `px * 16 + 32768`;
 *   the primitive itself -- a SPRITE, which needs both corners in one loop and no texture read.
 *
 * The answer is not findable by reading, and it does not have to be: `third_party/ps2sdk` is in this
 * tree, and its `ee/draw/src/draw2d.c` ships packets that every SDK game has ever put on a real GS.
 * Spelled from that source, three packets each differ from this file's by ONE of the three
 * variables, so one census decides all three at once:
 *
 *   gs_poster_ref_sprite_packet(..., 0) -- the SDK's SPRITE SHAPE (draw2d.c:190-211:
 *     GIF_SET_TAG(1,0,0,0,GIF_FLG_REGLIST,4), the REGS list PRIM/RGBAQ/XYZ2/XYZ2, and two data
 *     quadwords holding the four 64-bit values densely) with THIS file's corner words.  A one-bit
 *     change from the failing RED/BLUE packet: FLG 0 -> 1 (and PRE 1 -> 0, which is the same
 *     decision -- the primitive word arrives as a register instead of from the tag).  If this lands
 *     and RED does not, the format is the fault.
 *   gs_poster_ref_sprite_packet(..., 1) -- the same, with the SDK's own corner offsets
 *     (draw2d.c:12-19: START_OFFSET 2047.5625f and END_OFFSET 2048.5625f, i.e. -7 and +9 in the
 *     field's own 1/16-pixel units).  One variable further on: if the two sprites differ only here,
 *     the missing nudge is what a SPRITE costs, not what it loses.
 *   gs_poster_ref_tri_packet() -- the SDK's flat TRIANGLE (draw2d.c:119-146: NREG=6 with
 *     PRIM/RGBAQ/XYZ2/XYZ2/XYZ2/NOP, three data quadwords, and the plain OFFSET 2048.0f -- a
 *     triangle needs no nudge, which is why the SDK does not apply one).  If the sprites above land
 *     and this does not, the fault is in the three-vertex pipeline rather than in the format.
 *
 * All three of those landed on the first rung-4 run, and gsquad's RED/BLUE did not.  The second run
 * added a fourth control to ask whether the difference was the tag or the register stride, and its
 * answer was louder than expected: the packet hung the GIF, which settled the stride question in
 * favour of this file's own convention and left the group's SHAPE as the variable.  See
 * gs_poster_ref_sprite_ad_packet() below for that finding and for the control that replaces it.
 *
 * These are diagnostics, not production paths: nothing here is a caller of anything, and the only
 * thing that sends them is core_ps2.c's `gsread`, which needs no witness to read the result now that
 * rung 2 and rung 3 exist.  Both builders go out the SYNCHRONISER for the same reason -- the sender
 * was rung 1's variable and rung 3 already showed neither sender delivers the current shape, so
 * making it a variable again would confound the three that matter.
 *
 * The one deliberate deviation from the SDK's text is EOP.  ps2sdk batches its packets into one big
 * transfer and lets its own flush end it (`GIF_SET_TAG(1,0,...)` -- EOP clear in every one of the
 * functions above), whereas each control here is a transfer of its own, so its tag sets EOP the way
 * gs_poster_packet() and the environment packet do.  Setting it is what makes a self-contained DMA
 * terminate; it is not one of the three variables under test. */
#define GS_REF_SPRITE_QWC 3u    /* one tag quadword plus two dense data quadwords (4 registers) */
#define GS_REF_TRI_QWC    4u    /* one tag quadword plus three dense data quadwords (6 registers) */

/* The SDK's own page offset in the vertex field's units: 2048.0f * 16 = 32768, which is
 * GS_XYOFFSET_PAGE, and the SDK's sprite nudge is that value minus 7 (left/top) and plus 9
 * (right/bottom).  Named so the gate below can state the literal instead of re-deriving it. */
#define GS_REF_PAGE       (2048u << 4)
#define GS_REF_START_ADJ  7u    /* 2047.5625f * 16 = 32761 = GS_REF_PAGE - GS_REF_START_ADJ */
#define GS_REF_END_ADJ    9u    /* 2048.5625f * 16 = 32777 = GS_REF_PAGE + GS_REF_END_ADJ */

/* Which tag the reference sprite carries.  What the `fmt` argument that used to sit here was for, and
 * what it cost, is now the finding rather than the question, so the two forms that were it are split
 * into two builders with two lengths and no mode word.
 *
 * That run (2026-10-16, `make ps2-bench` with BENCH_GS=1) found YELLOW, MAGENTA and GREEN all in
 * page 0 with their exact pixel counts, and RED and BLUE nowhere: the packet that ships differs from
 * the packet that draws in the STRIDE as much as in the tag, and the mechanism looked like arithmetic
 * -- a PACKED group with NREG=4 of 64-bit registers is two quadwords per loop, so the four this
 * emitter writes would be read as PRIM=value, RGBAQ=the zero above it, ST=0, XYZ2=0.
 *
 * The follow-up run falsified that arithmetic, and in the most direct way an instrument has: a
 * PACKED/PRE=1 tag over a DENSE two-quadword body was sent, and the next three GIF reads all came
 * back `block-never-finished`.  The GIF never asked for the transfer to end -- it is still waiting
 * for data.  So a PACKED group with NREG=4 demands FOUR quadwords, one per register, padded to 128
 * bits each, which is exactly the body gs_poster_packet() already writes: the shipping packet is
 * well-formed for its own tag and still draws nothing.  What that leaves standing is not the stride
 * but the group's SHAPE -- FLG=PACKED carrying a four-register REGS list with PRE=1, a combination
 * ps2sdk never emits anywhere (every `GIF_FLG_PACKED` in third_party/ps2sdk is `NREG=1, GIF_REG_AD`).
 *
 * The builder below is therefore the SDK's other shape, not a mode of this one: the same four
 * registers as four AD writes, which is the form this port has already proven it can put on the bus
 * (send_trx_setup_at() at ps2_gs.c:220 drives every console text upload with a PACKED/NREG=1 group of
 * four AD quadwords, and the canvas appears).  If it lands, PACKED is innocent at NREG=1 and the
 * defect is the four-register REGS list -- and the fix for the poster and for ps2_gs_rec.c is the
 * dense REGLIST shape CYAN has already been shown to draw.  If it does not, PACKED cannot carry a
 * drawing group at all here, and the same fix follows for a stronger reason.
 *
 * It did: the third run found YELLOW at its exact 7,360 px in its own box, beside CYAN's 7,360,
 * MAGENTA's 7,567 and GREEN's 3,525, with RED and BLUE still nowhere in the page.  So PACKED is
 * innocent when its group is a single-register AD loop, and the register stride is innocent too --
 * what is left between the packet that draws and the packet that ships is a four-register REGS list
 * carried by PRE=1.  Those two are still one sentence, and rung 5 takes them apart: the shipping
 * body again, quadword for quadword, with PRE as the only argument the caller changes.
 *
 * Rung 5 ran the same evening -- the fourth BENCH_GS=1 census, six seconds of PCSX2, no window and no
 * witness -- and its answer is the second of the two branches above: orange=0/1840, with the whole
 * page accounted for (navy 443,217 + text 10,971 + CYAN 7,360 + MAGENTA 7,567 + GREEN 3,525 +
 * YELLOW 7,360 = 480,000, sent=0, below_other=0).  PRE=0 over the shipping body draws nothing, exactly
 * as PRE=1 did, so the tag's PRIM carry was never the defect and a multi-register PACKED group does
 * not kick on this GS whatever its primitive comes from.  That is the finding this file was written
 * against, and gs_poster_packet() now emits CYAN's proven shape instead: FLG=REGLIST, PRE=0, the same
 * four nibbles, TWO quadwords per rect.  gs_poster_ref_sprite_packed_packet() stays as the retired
 * shape -- see its own note for what it is for now. */
#define GS_REF_AD_QWC     5u    /* one tag quadword plus four AD quadwords, one register each */
#define GS_REF_PACKED_QWC 5u    /* one tag quadword plus four sparse data quadwords, NREG 4 */

/* Fill exactly GS_REF_SPRITE_QWC quadwords with the SDK-shaped flat SPRITE: FLG=REGLIST, PRE=0,
 * NREG=4, and the four 64-bit registers packed densely into two quadwords.  `nudge` selects the
 * corner words: 0 for this file's own `px * 16 + GS_REF_PAGE`, 1 for the SDK's START/END spelling.
 * Returns GS_REF_SPRITE_QWC, or -1 without touching
 * the buffer for a bad argument or a degenerate rect -- the same refusal law as gs_poster_packet(),
 * because a control that draws nothing would be indistinguishable from a format that rasterises
 * nothing. */
int32_t gs_poster_ref_sprite_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                    uint64_t *qw, uint32_t cap_qw);

/* Fill exactly GS_REF_AD_QWC quadwords with the SAME flat SPRITE told the SDK's state-write way:
 * NLOOP 4, NREG 1, FLG=PACKED, PRE=0, the REGS list one AD nibble, and four data quadwords each
 * carrying one register's 64-bit value in its low half and that register's GP address in its high
 * one -- GS_GP_PRIM, GS_GP_RGBAQ and GS_GP_XYZ2 twice, so the drawing kick arrives with the second
 * corner.  Its four VALUES are gs_poster_ref_sprite_packet()'s to the bit; only their delivery
 * differs, which is the point.  The same refusals apply, and for the same reason. */
int32_t gs_poster_ref_sprite_ad_packet(const gs_poster_rect *r, uint32_t canvas_px, uint32_t nudge,
                                       uint64_t *qw, uint32_t cap_qw);

/* rung 5's packet, and the retired shape this file no longer emits.
 *
 * FLG=PACKED, NREG=4, the REGS list PRIM/RGBAQ/XYZ2/XYZ2, and FOUR data quadwords -- one 64-bit
 * register per 128-bit unit, the upper half zero, which rung 4's hung dense-under-PACKED control
 * proved is exactly the length this tag asks for.  With `pre` 1 this is the packet that shipped from
 * LANE 9b until rung 5, quadword for quadword, and the poster gate says so as a NEGATIVE assertion
 * now: what ships must differ from it in FLG, in PRE and in the stride, and if those two packets ever
 * agree again the format has been reverted while its host gate still passes.
 *
 * `pre` is the bit rung 5 put on the table, and the census read it as innocent: this builder at pre=0
 * left no pixel anywhere in page 0, where CYAN's identical values in the dense form filled their box
 * exactly.  So it has a second job -- it is the instrument's negative control.  core_ps2.c's `gsread`
 * sends it as VIOLET and expects 0, because a census that reports a pixel for a shape already known
 * not to draw is reporting something other than the shape, and every positive row on that run would
 * need re-reading. */
int32_t gs_poster_ref_sprite_packed_packet(const gs_poster_rect *r, uint32_t canvas_px,
                                           uint32_t pre, uint64_t *qw, uint32_t cap_qw);

/* Fill exactly GS_REF_TRI_QWC quadwords with the SDK-shaped flat TRIANGLE.  `v` is three vertices,
 * six ints, x0 y0 x1 y1 x2 y2 in screen pixels, and the colour is the canvas word again.  A
 * triangle with two identical vertices, or with all three collinear, encloses nothing, so this
 * refuses it too -- and a triangle whose any corner leaves the field's signed range is refused,
 * because the packet would be describing a different shape than the caller asked for. */
int32_t gs_poster_ref_tri_packet(const int32_t *v, uint32_t canvas_px,
                                 uint64_t *qw, uint32_t cap_qw);

/* ── LANE 19 rung 7: the one control whose fragment reaches the ALPHA stage ────────────────────
 *
 * Every control above is ABE=0, which bypasses ALPHA and writes the vertex's own colour out, so the
 * blend stage has never been measured by this census -- and rung 6's instrument structurally cannot
 * measure it, because a blended fragment whose equation collapses to `+ color3` paints the
 * destination's own word back and is byte-identical to no fragment on a page-vs-canvas read.  This
 * builder turns ABE on so a PIXEL can name the field-role model.  See its own note in the .c.
 *
 * CYAN's dense SPRITE shape (GS_REF_SPRITE_QWC) with two words changed: PRIM carries `abe`, and
 * RGBAQ's alpha byte carries the caller's `src_alpha` instead of the canvas word's, because the
 * vertex's As is one of the blend's operands and leaving it to an unset canvas A byte would add a
 * third unmeasured variable.  `canvas_rgb` supplies only the R/G/B.  The same refusals as the other
 * controls. */
int32_t gs_poster_ref_sprite_blend_packet(const gs_poster_rect *r, uint32_t canvas_rgb,
                                          uint32_t src_alpha, uint32_t abe,
                                          uint64_t *qw, uint32_t cap_qw);

#endif /* PS2_GS_POSTER_H */
