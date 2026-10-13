/*
 * src/gl/tinygl/vertex_stage.c -- the per-vertex float arithmetic, as one translation unit
 *
 * Row 7 of the plan of record (doc/txt/PS2.txt, ch. 16 "The increments").  Once the GS draws the
 * pixels, what is left of an XMB frame is float calls: the measured eegs frame 2 makes 460,861
 * of them, 373,820 of which are an add, a multiply, a divide or a compare, each paying the
 * 28 cycles the port measures for a soft-float libcall -- 15 of those cycles being the `jal`
 * and its prologue, which is the whole cost of the call and none of the cost of the arithmetic.
 * This file is where that subtraction happens.  It holds the two functions `vertex.c` used to
 * keep static, moved over unchanged but for the nine casts named below, and it is the second
 * unit in the image that is compiled `-mhard-float` -- the first being `ps2_builtins.c`, and the
 * rule that makes that legal being the same in both places: the unit crosses no floating value
 * over a translation-unit boundary, so `-msoft-float` stays an ABI law for everything else.
 *
 * What leaves here is a pointer and what returns is nothing.  `GLVertex` fields are read and
 * written through the pointer, and the floats inside them are memory, not registers, so no
 * value is ever handed to a caller in an FPR the callee would read in a `$v0`.  That is also why
 * the two directions of conversion are treated differently below: `int -> float` is `cvt.s.w`,
 * which the Emotion Engine answers, while `float -> int` has no instruction at all -- the EE has
 * no `trunc.w.s`, and asking it for one left `$f4` holding the *previous* result (see
 * `src/drivers/ps2/ps2_builtins.c`, above where `fp_hw_cvt_w_s` used to live).  So every cast in
 * this file goes through `TGL_TRUNC()`, which on the hardware-FPU build inlines the truncate the
 * port already owns -- `ps2_sf_fixsfsi()` in `src/drivers/ps2/ps2_fp.h`, the same exponent-band
 * shift the rest of the image reaches for through `__fixsfsi()`, and identical by being literally
 * the same text -- with the float's bits moved out of the FPU by `mfc1`.  Inlined rather than
 * called, because a call here makes the compiler save and restore its live FPR pairs as `sdc1` and
 * `ldc1`, which this CPU does not answer either; see the block below.  On every other target
 * `TGL_TRUNC(x)` is `(GLint)(x)` and this file is `vertex.c`'s own text.
 *
 * One thing deliberately did not move: `gl_V3_Norm_Fast()`, the normalise inside the lighting
 * branch.  It is `sqrt(double)` from `include/gl/math.h`, and a double is both the bit engine's
 * 536 cycles and an instruction class this CPU cannot execute -- compiling this file with the
 * FPU on put 21 `.d` instructions into the image, all of them traced to that one line.  It stays
 * behind `tgl_vertex_normalize()`, which lives in the `-msoft-float` unit and takes a pointer, so
 * the doubles stay where they are today and this unit's op set stays inside what COP1 answers.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "zgl.h"

/* The nine `(GLint)` casts below are float -> int, the one conversion this CPU has no instruction
 * for.  Under `-mhard-float` clang will happily emit `trunc.w.s` for a C cast, the image gate's
 * byte scan knows that encoding (0x4600010d), and the EE's answer to it is to do nothing at all
 * and leave the register holding whatever the last conversion produced -- which is a wrong number,
 * silently.  Calling out for the truncation instead is not the fix either, and that was measured
 * on this file: a `jal` makes the compiler preserve its live FPRs across it, and in the O32
 * 32-bit-FPR file those are even/odd pairs, so the prologue came out as `sdc1` and the epilogue as
 * `ldc1` -- two more encodings this CPU swallows without answering, which is what the image gate's
 * other FPU row forbids by name.  So the cast inlines the bit engine the whole image already uses,
 * reached through the header that now owns it, with the float's bits moved to a general register
 * by `mfc1` (an instruction the EE does answer).  Same arithmetic, same answers, one fewer call. */
#if defined(BTRON_PS2_TARGET) && defined(BTRON_PS2_FP_HW)
#include "../../drivers/ps2/ps2_fp.h" /* ps2_sf_fixsfsi(): the truncate, with its band table */

static inline GLint TGL_TRUNC(GLfloat f)
{
    uint32_t bits;
    __builtin_memcpy(&bits, &f, sizeof bits);
    return (GLint)ps2_sf_fixsfsi(bits);
}
#else
#define TGL_TRUNC(f) ((GLint)(f))
#endif

void tgl_vertex_to_viewport(GLVertex* v) {
	GLContext* c = gl_get_context();
	{
		GLfloat winv = 1.0 / v->pc.W;
		v->zp.x = TGL_TRUNC(v->pc.X * winv * c->viewport.scale.X + c->viewport.trans.X);
		v->zp.y = TGL_TRUNC(v->pc.Y * winv * c->viewport.scale.Y + c->viewport.trans.Y);
		v->zp.z = TGL_TRUNC(v->pc.Z * winv * c->viewport.scale.Z + c->viewport.trans.Z);
	}

	v->zp.r = TGL_TRUNC(v->color.v[0] * COLOR_CORRECTED_MULT_MASK + COLOR_MIN_MULT) & COLOR_MASK;
	v->zp.g = TGL_TRUNC(v->color.v[1] * COLOR_CORRECTED_MULT_MASK + COLOR_MIN_MULT) & COLOR_MASK;
	v->zp.b = TGL_TRUNC(v->color.v[2] * COLOR_CORRECTED_MULT_MASK + COLOR_MIN_MULT) & COLOR_MASK;
	v->zp.a = TGL_TRUNC(v->color.v[3] * COLOR_CORRECTED_MULT_MASK + COLOR_MIN_MULT) & COLOR_MASK;

	if (c->texture_2d_enabled && c->current_texture != NULL) {
		/* The texture's own extent, not the implementation's default: see GLImage. */
		const GLImage* im = &c->current_texture->images[0];
		v->zp.s = TGL_TRUNC(v->tex_coord.X * im->sscale + im->soff);
		v->zp.t = TGL_TRUNC(v->tex_coord.Y * im->tscale + im->toff);
	}
}

void tgl_vertex_transform(GLVertex* v) {
	GLfloat* m;
	GLContext* c = gl_get_context();

	if (c->lighting_enabled)

	{
		/* eye coordinates needed for lighting */
		V4* n;
		m = &c->matrix_stack_ptr[0]->m[0][0];
		v->ec.X = (v->coord.X * m[0] + v->coord.Y * m[1] + v->coord.Z * m[2] + m[3]);
		v->ec.Y = (v->coord.X * m[4] + v->coord.Y * m[5] + v->coord.Z * m[6] + m[7]);
		v->ec.Z = (v->coord.X * m[8] + v->coord.Y * m[9] + v->coord.Z * m[10] + m[11]);
		v->ec.W = (v->coord.X * m[12] + v->coord.Y * m[13] + v->coord.Z * m[14] + m[15]);

		/* projection coordinates */
		m = &c->matrix_stack_ptr[1]->m[0][0];
		v->pc.X = (v->ec.X * m[0] + v->ec.Y * m[1] + v->ec.Z * m[2] + v->ec.W * m[3]);
		v->pc.Y = (v->ec.X * m[4] + v->ec.Y * m[5] + v->ec.Z * m[6] + v->ec.W * m[7]);
		v->pc.Z = (v->ec.X * m[8] + v->ec.Y * m[9] + v->ec.Z * m[10] + v->ec.W * m[11]);
		v->pc.W = (v->ec.X * m[12] + v->ec.Y * m[13] + v->ec.Z * m[14] + v->ec.W * m[15]);

		m = &c->matrix_model_view_inv.m[0][0];
		n = &c->current_normal;

		v->normal.X = (n->X * m[0] + n->Y * m[1] + n->Z * m[2]);
		v->normal.Y = (n->X * m[4] + n->Y * m[5] + n->Z * m[6]);
		v->normal.Z = (n->X * m[8] + n->Y * m[9] + n->Z * m[10]);

		if (c->normalize_enabled) {
			tgl_vertex_normalize(&v->normal);
		}
	}

	else {
		/* no eye coordinates needed, no normal */
		/* NOTE: W = 1 is assumed */
		m = &c->matrix_model_projection.m[0][0];

		v->pc.X = (v->coord.X * m[0] + v->coord.Y * m[1] + v->coord.Z * m[2] + m[3]);
		v->pc.Y = (v->coord.X * m[4] + v->coord.Y * m[5] + v->coord.Z * m[6] + m[7]);
		v->pc.Z = (v->coord.X * m[8] + v->coord.Y * m[9] + v->coord.Z * m[10] + m[11]);
		if (c->matrix_model_projection_no_w_transform) {
			v->pc.W = m[15];
		} else {
			v->pc.W = (v->coord.X * m[12] + v->coord.Y * m[13] + v->coord.Z * m[14] + m[15]);
		}
	}

	v->clip_code = gl_clipcode(v->pc.X, v->pc.Y, v->pc.Z, v->pc.W);
}
