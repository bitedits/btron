#ifndef _XMB_WAVE_H_
#define _XMB_WAVE_H_

/*
 * src/apps/xmb_wave.h - the launcher's waving surface: its lattice, its noise and
 * the arithmetic that turns a grid of integers into a sheet of pixels.
 *
 * This is plan row 8 of doc/md/PS2.md ("Row 8: the wave's lattice on COP1").  The
 * sheet's maths was measured out of `src/apps/xmb.c` by the census of 2026-10-12 and
 * put in a file of its own for one reason: it is the largest block of single-precision
 * arithmetic left in an XMB frame that is not on the FPU.  A counted frame's `f_surface`
 * row read 160,893 soft-float libcalls in 22,053 us of a 69,534 us draw -- 86 % of that
 * stage's instructions and 23 % of the frame -- and row 7, which moved the geometry
 * kernel to COP1, left it exactly where it had been.
 *
 * The unit therefore takes `-mhard-float` when the image's FPU dial is on, as
 * `src/drivers/ps2/ps2_builtins.c` and `src/gl/tinygl/vertex_stage.c` already do, and
 * for the same reason as those two: what crosses its boundary is not a float.  The
 * effect clock arrives as IEEE-754 bits in an integer (`xb_wave_step()`), the sheet
 * leaves through memory the reader loads as a float, and the two arguments this CPU has
 * no instruction for -- a single-precision sine whose argument is wider than 4*pi, and
 * a double anywhere at all -- go back out through `xb_sinf_far_u32()` and
 * `xb_cosf_far_u32()`, which are bit patterns in and bit patterns out and live in the
 * `-msoft-float` unit that owns the animation.  The ABI of the image is unchanged: a
 * float in an integer register is what `-msoft-float` already passes, which is why the
 * link keeps reporting soft float for every object including this one.
 *
 * What it does not do is change an answer.  Every expression moved here is the same
 * operations, on the same operands, in the same order, as the line it replaced; the
 * screen projection is the same `(e + 1) * 0.5 * w`, evaluated per grid point instead
 * of per vertex; and where a value that used to be recomputed twelve times a cell is
 * now computed once, it is recomputed *nothing* -- `xb_noise3()`'s lattice index is an
 * integral float, so its eight neighbours are that integer plus a whole number.
 */

#include <stdint.h>

/* The sheet's mesh.  32x32 is the shape the reference driver's
 * pipeline_xmb_ribbon.cg.h vertex stage was written against; the measured price of the
 * mesh dial (32 -> 16) is in doc/md/PS2.md, "What a pixel costs, measured". */
#define XB_RIBBON_ROWS 32
#define XB_RIBBON_COLS 32

/* The sheet in clip space, and its baked brightness: written by xb_wave_step(), read
 * by anything that has to know where the surface is. */
extern float xb_rib_e0[XB_RIBBON_ROWS][XB_RIBBON_COLS];
extern float xb_rib_e1[XB_RIBBON_ROWS][XB_RIBBON_COLS];
extern float xb_rib_e2[XB_RIBBON_ROWS][XB_RIBBON_COLS];
extern float xb_rib_bri[XB_RIBBON_ROWS][XB_RIBBON_COLS];

/* The same sheet in screen pixels for a window of `w_px` x `h_px`, which is what the
 * drawing and the particles want: (e+1)*0.5*w and (1-e)*0.5*h, evaluated once per grid
 * point per step rather than once per *vertex* per primitive. */
extern float xb_rib_px[XB_RIBBON_ROWS][XB_RIBBON_COLS];
extern float xb_rib_py[XB_RIBBON_ROWS][XB_RIBBON_COLS];

/* How many lattice points the noise has been asked for, and how many of those were
 * already in its cache.  `miss` is the number of sines the sheet has actually paid for
 * since boot. */
extern unsigned xb_hash_hits;
extern unsigned xb_hash_miss;

/* Advance the sheet to effect time `t`, and project it into a window of `w_px` x
 * `h_px` pixels.  `t_bits` is the IEEE-754 single representation of the clock -- an
 * integer here, not a float, so that this call crosses the boundary between the image's
 * two floating-point conventions without putting a value in a register either side
 * reads differently.  Called once per painted frame, before anything is drawn. */
void xb_wave_step(uint32_t t_bits, int w_px, int h_px);

/* The wave unit's two callbacks, defined in the `-msoft-float` unit that owns the
 * animation clock (src/apps/xmb.c).  Both are `sinf`/`cosf` of an argument outside the
 * float fold's 4*pi range, where the answer needs the double-precision reduction this
 * CPU has no instruction for at all; they take and return the bits so the wave unit
 * never has a float in an integer register or an integer in a float register. */
uint32_t xb_sinf_far_u32(uint32_t x_bits);
uint32_t xb_cosf_far_u32(uint32_t x_bits);

/* A float's bits in a general register, and the reverse.  Under `-mhard-float` these are
 * one `mfc1`/`mtc1`, which the Emotion EE answers; anywhere else they are the store and
 * reload the compiler would do anyway.  They are the whole of this file's ABI: nothing
 * else in it moves a float across a function boundary. */
static inline uint32_t xb_bits_f(float f)
{
    uint32_t u;
    __builtin_memcpy(&u, &f, sizeof u);
    return u;
}

static inline float xb_float_u32(uint32_t u)
{
    float f;
    __builtin_memcpy(&f, &u, sizeof f);
    return f;
}

#endif /* _XMB_WAVE_H_ */
