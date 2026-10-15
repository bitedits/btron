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
 * LANE 12f added the two `tgl_grid_map*` functions at the bottom, and they are here rather than in
 * `src/gl/backend_eegs.c` for the reason in `doc/txt/PS2.txt`: the arithmetic they do has to be the
 * arithmetic this file already does, and a `-msoft-float` unit is allowed to carry a float
 * expression in double intermediates.  They are also the only way any float reaches the retained
 * mesh, because nothing in the pair crosses a translation-unit boundary as a float value -- the map
 * travels as memory and the points come back as integers, which is the ABI rule this unit is
 * exempt from precisely because it never breaks it.  LANE 12h rung (i) added the textured pair
 * beside them -- `tgl_grid_map_tex()` and `tgl_grid_map_tex_points()` -- for the same reason and
 * under the same rule: their texel chain ends in two floats, and those floats land in memory
 * (`tgl_tex_pt_t`) and nowhere in a register a caller would read.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include "zgl.h"
#include "../grid_map.h"
#include "../../drivers/ps2/ps2_gs_pack.h" /* gs_float_scale_pow2(), the ST field's unit */

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

/* ---- the retained-mesh map: LANE 12f rung (i) --------------------------------------
 *
 * `tgl_vertex_to_viewport()` above maps one vertex and needs the whole `GLVertex` to do it: it
 * reads `pc`, writes `zp`, and takes the viewport's constants out of the live context every time.
 * That is right for a caller who arrives with one vertex at a time.  It is wrong for a caller who
 * arrives with a mesh it already has in hand -- the XMB ribbon's `xb_rib_px`/`xb_rib_py`/
 * `xb_rib_bri`, 32x32 floats each, written this very frame by `xmb_wave.c` -- because the only part
 * of the map that varies across that mesh is the vertex itself.
 *
 * So the constants are lifted once and the points are mapped in one loop.  The two functions below
 * are a second encoding of the arithmetic above, which is a liability unless something measures
 * them against each other, and that something is `verify/tests/test_grid_map.c`: it links this
 * file's own `tgl_vertex_transform()` and `tgl_vertex_to_viewport()` as the comparator, sweeps
 * vertices that straddle every pixel boundary of the window, and requires bit equality -- not
 * tolerance.  Three facts make the two encodings able to agree at all, and `tgl_grid_map()` checks
 * the first two rather than assuming them:
 *
 *   - the walk's `winv` is `1.0 / pc.W`, and `pc.W` is `m[15]` for every vertex exactly when
 *     `matrix_model_projection_no_w_transform` is set, so the divide's operand is a constant and
 *     the divide itself happens once, here, spelled the same way as it is spelled above;
 *   - with a diagonal projection (`m[1]` and `m[4]` both exactly zero) and a mesh of
 *     `glVertex2f` points (Z exactly 0), the walk's four-term row sums reduce to two terms: the
 *     two dropped products are exact zeros, and adding an exact zero to a finite float changes
 *     nothing.  `m[2]` and `m[6]` are not tested because Z is 0 whatever they hold, and `m[9]`,
 *     `m[10]`, `m[11]` are not tested because `zp.z` never reaches the GS stream -- the recorded
 *     vertex's depth is `EEGS_Z`, a constant of the backend;
 *   - a grey mesh's three colour channels are the same truncate of the same float, and its alpha is
 *     the context's current alpha for every vertex, so one byte pair serves the whole draw.
 *
 * The arithmetic is written as an explicit product then an explicit sum, and the gate that compares
 * it is built with `-ffp-contract=off` for that reason: `x * mx + xc` is a contraction candidate and
 * the walk's `((x*m0 + y*m1) + z*m2) + m3` is not, so on a host with an FMA the two spellings could
 * round differently even though they are the same mathematics.  The Emotion Engine has no FMA at all
 * (the shipped `tgl_vertex_to_viewport` disassembles as `mul.s`, `mul.s`, `add.s`), so `off` is not a
 * workaround for a host quirk, it is the setting under which the host runs the image's arithmetic.
 */

/* The channel byte the walk's colour line produces: the truncate, the mask, then the byte the GS
 * adapter takes off it (`gs_vertex()` in clip.c reads `col->r >> 16`).  Both spellings are the
 * ones above and in clip.c, so a change to either is a change the differential gate sees. */
static inline GLuint grid_channel_byte(GLfloat v)
{
	return (GLuint)((TGL_TRUNC(v * COLOR_CORRECTED_MULT_MASK + COLOR_MIN_MULT) & COLOR_MASK) >> 16);
}

int tgl_grid_map(tgl_grid_map_t* out) {
	GLContext* c = gl_get_context();
	const GLfloat* m;

	if (out == NULL) return -1;
	if (c == NULL) { out->refuse = TGL_GRID_REFUSE_ARG; return -1; }

	/* The product this function reads is computed at `glBegin`, not at `glOrtho`: a matrix change
	 * only sets `matrix_model_projection_updated` and leaves `matrix_model_projection` holding the
	 * PREVIOUS product.  Lifting it in that state would map this mesh through the last frame's
	 * window -- one wrong frame after a resize, with nothing printed to explain it -- and the
	 * alternative is calling `gl_M4_Mul()` from here, which is a `jal` in a `-mhard-float` unit and
	 * therefore an `sdc1`/`ldc1` pair around it: the same silent-garbage class this file's header
	 * exists to avoid.  So the honest answer is the one the caller can act on. */
	if (c->matrix_model_projection_updated) {
		out->refuse = TGL_GRID_REFUSE_STALE;
		return -1;
	}

	/* With lighting on, a vertex's colour comes from `gl_shade_vertex()` and not from the current
	 * colour, and `pc` comes from two matrix products instead of one.  With a texture enabled and
	 * bound, `zp.s`/`zp.t` are written and the recorded vertex carries texel coordinates this map
	 * does not compute.  Either way the batch's answers would be the answers of a draw the caller
	 * did not ask for. */
	if (c->lighting_enabled) {
		out->refuse = TGL_GRID_REFUSE_LIGHT;
		return -1;
	}
	if (c->texture_2d_enabled) {
		out->refuse = TGL_GRID_REFUSE_TEX;
		return -1;
	}

	m = &c->matrix_model_projection.m[0][0];
	if (!c->matrix_model_projection_no_w_transform) {
		out->refuse = TGL_GRID_REFUSE_W;
		return -1;
	}

	/* The two couplings, tested for exactly zero rather than for smallness: a projection that turns
	 * the page is not a slightly-diagonal one, and the terms this map drops are only exact zeros
	 * when they are zero. */
	if (m[1] != 0.0f || m[4] != 0.0f) {
		out->refuse = TGL_GRID_REFUSE_SKEW;
		return -1;
	}

	/* Every field is stored straight into `out`, one load and one store at a time, and that shape is
	 * the ABI law rather than a style.  Building the map in a local struct and copying it out with
	 * `*out = g` keeps all nine singles live at once: past the eighteen caller-saved ones the o32
	 * hard-float ABI has, the compiler spills a callee-saved pair to the stack with `sdc1` and reloads
	 * it with `ldc1` -- six pairs here, in the prologue and epilogue -- and the R5900 has neither
	 * instruction.  It is the same trap this file's header names for a `jal`, and the same one
	 * `tgl_grid_map_points()` below fell into and out of; TEST 66 of `make test-ps2` is what finds it,
	 * because the image runs and the host does not. */
	out->mx = m[0];
	out->xc = m[3];
	out->my = m[5];
	out->yc = m[7];
	out->winv = 1.0 / m[15];
	out->scale_x = c->viewport.scale.X;
	out->trans_x = c->viewport.trans.X;
	out->scale_y = c->viewport.scale.Y;
	out->trans_y = c->viewport.trans.Y;

	/* The clip rect, in the one unit that can state it as integers: the viewport's own pixel size.
	 * `eegs_grid_strip()` counts every mapped point against this range in its `over=` column, and the
	 * GS's own SCISSOR is programmed from the same rect, which is the walk's clipper's answer
	 * restated -- the derivation is in `grid_map.h` and the measurement of it is
	 * `verify/tests/test_grid_map.c`'s outside-window probes.  It is lifted here rather than derived
	 * downstream because the terms are floats on this side and the consumer is a `-msoft-float`
	 * unit. */
	out->vp_x0 = (GLint)c->viewport.xmin;
	out->vp_x1 = (GLint)(c->viewport.xmin + c->viewport.xsize - 1);
	out->vp_y0 = (GLint)c->viewport.ymin;
	out->vp_y1 = (GLint)(c->viewport.ymin + c->viewport.ysize - 1);

	/* The alpha byte, from the same current colour the walk copies into `v->color` -- which is what
	 * a mesh that sets its own intensity per point and its alpha once actually draws with. */
	out->a_byte = (GLuint)grid_channel_byte(c->current_color.W);

	/* The two PRIM flags, read where the GS adapter reads them. */
	out->smooth = (c->current_shade_model == GL_SMOOTH) ? 1u : 0u;
	out->blended = 0u;
#if TGL_FEATURE_BLEND == 1
	if (c->zb != NULL && c->zb->enable_blend != 0) out->blended = 1u;
#endif

	/* `ok` last, because a consumer that reads it reads everything above it, and a refusal above this
	 * line leaves `out` holding only its `refuse`. */
	out->refuse = TGL_GRID_REFUSE_NONE;
	out->ok = 1u;
	return 0;
}

void tgl_grid_map_points(const tgl_grid_map_t* m, const float* xs, const float* ys,
                         const float* bri, int n, tgl_grid_pt_t* out) {
	int i;

	if (m == NULL || !m->ok) return;
	if (xs == NULL || ys == NULL || bri == NULL || out == NULL) return;

	/* Three passes rather than one interleaved loop, and the reason is an instruction class rather
	 * than style.  The map's constants that X needs are `mx`, `xc`, `winv`, `scale_x`, `trans_x`;
	 * Y needs five more; colour needs none.  Interleaved, all ten are hoisted out of the induction
	 * variable and live across the whole body, and past the twenty caller-saved singles the o32
	 * hard-float ABI allows ($f0-$f19) the allocator reaches into $f20/$f22/$f24 -- where saving a
	 * *single* costs its even/odd pair, so the prologue becomes three `sdc1` and the epilogue three
	 * `ldc1`.  This CPU does not answer those, which is the same silent-garbage class as the `jal`
	 * the header above refuses: the values they hold are simply not there on return.  Split, each
	 * loop hoists five and stays inside the caller-saved set, and `make test-ps2` TEST 66 -- which
	 * greps the linked image for exactly those opcodes and is the control for this paragraph:
	 * re-interleave the three bodies and it fails on six instructions while every value test in
	 * `make test-grid-map` stays green, because the two shapes compute the same points. */
	for (i = 0; i < n; i++) {
		/* The association is `tgl_vertex_to_viewport()`'s own: product by `winv`, then by the
		 * viewport's scale, then the translation, then the truncate. */
		GLfloat pcx = (GLfloat)(GLfloat)((GLfloat)xs[i] * m->mx);

		pcx = (GLfloat)(pcx + m->xc);
		out[i].x_px = TGL_TRUNC(pcx * m->winv * m->scale_x + m->trans_x);
	}

	for (i = 0; i < n; i++) {
		GLfloat pcy = (GLfloat)(GLfloat)((GLfloat)ys[i] * m->my);

		pcy = (GLfloat)(pcy + m->yc);
		out[i].y_px = TGL_TRUNC(pcy * m->winv * m->scale_y + m->trans_y);
	}

	for (i = 0; i < n; i++) {
		/* Grey by contract: the mesh's intensity is the channel every vertex of a smooth grey
		 * triangle is drawn with, and the alpha byte is the draw's. */
		const GLuint grey = grid_channel_byte((GLfloat)bri[i]);

		out[i].rgba = (grey & 0xFFu) | ((grey & 0xFFu) << 8) | ((grey & 0xFFu) << 16) |
		              (m->a_byte << 24);
	}
}

/* ---- the textured mesh map: LANE 12h rung (i) --------------------------------------
 *
 * The menu's 1,436 vertices are the same walk at the same price as the ribbon's were before 12f,
 * and the ident run's `st=25` said what the textured batch is not: the texel coordinates never
 * move between frames, so no map has to DERIVE them -- the app hands the batch the same `u`/`v`
 * it hands `glTexCoord2f` today.  What the batch must still produce every frame is the walk's
 * CONVERSION of them, because the ST register is a float and the walk arrives there by two steps:
 * `zp.s = TGL_TRUNC(u*sscale + soff)` (this file's own line above) and then the fixed point
 * divided by its power-of-two scale on the exponent field (`gs_float_scale_pow2` in
 * `clip.c`'s `gs_vertex()`).  These two functions are the third encoding of that chain -- the
 * walk, `gs_vertex`, and here -- and the binding is the differential sweep of
 * `verify/tests/test_grid_map.c`: a real uploaded sheet so the mapper constants are the upload's
 * own, vertices through `tgl_vertex_transform` + `tgl_vertex_to_viewport`, bit equality with the
 * live `gs_vertex` on all five fields, on `-ffp-contract=off` for the reason this file's header
 * gives.  A texel coordinate that differs by one truncate samples the wrong texel, and that is
 * visible, not a log row -- which is why this paragraph is in the book (LANE 12h) before the code
 * is in the tree.
 *
 * The refusal list is 12f's with the TEXTURE term inverted: `tgl_grid_map()` refuses a bound-and-
 * enabled texture because it cannot say `s`/`t`; this one REQUIRES one, and requires it UPLOADED
 * (`images[0].pixmap` non-null), because the walk's own arm (`gl_draw_triangle_fill`) folds a
 * never-uploaded sheet back to an untextured flat draw, and a batch that claimed that state would
 * point the stream at a plane that does not exist.  The `pixmap` travels as an opaque key: the
 * plane's VRAM identity is the GS registry's business (`gs_tex_find()`), which this unit must not
 * learn about, so only `eegs_tex_quads()` resolves it. */

/* The ST field's unit, spelled from `clip.c`'s `GS_TEX_S_SHIFT`/`GS_TEX_T_SHIFT` -- the same
 * pairing of `ZB_POINT_*_FRAC_BITS + 1` that names the upload's fixed-point scale.  A second
 * encoding by policy, bound by test: the comparator applies `gs_vertex`'s macro, and a drift
 * between the two is a bit mismatch the sweep prints. */
#define TGL_TEX_S_SHIFT (ZB_POINT_S_FRAC_BITS + 1)
#define TGL_TEX_T_SHIFT (ZB_POINT_T_FRAC_BITS + 1)

int tgl_grid_map_tex(tgl_tex_map_t* out) {
	GLContext* c = gl_get_context();
	const GLfloat* m;
	const GLImage* im;

	if (out == NULL) return -1;
	if (c == NULL) { out->g.refuse = TGL_GRID_REFUSE_ARG; return -1; }

	/* The textured question first, because it is the one this map is FOR: with no sheet bound-
	 * and-enabled-and-uploaded the draw is flat, and flat is the grey map's subject or the walk's,
	 * not this one's.  `texture_2d_enabled` alone is not enough -- `zgl.h` keeps the flag from
	 * `glEnable`, and a bind of a never-uploaded texture answers it while `pixmap` is still NULL. */
	if (!c->texture_2d_enabled || c->current_texture == NULL) {
		out->g.refuse = TGL_GRID_REFUSE_NOTEX;
		return -1;
	}
	im = &c->current_texture->images[0];
	if (im->pixmap == NULL) {
		out->g.refuse = TGL_GRID_REFUSE_NOTEX;
		return -1;
	}

	/* From here the conditions are 12f's, in 12f's order, reading 12f's fields: the stale
	 * product, lighting, the W, the skew.  The texture no longer refuses -- it is required --
	 * and every other clause means exactly what `tgl_grid_map()`'s block above says it means. */
	if (c->matrix_model_projection_updated) {
		out->g.refuse = TGL_GRID_REFUSE_STALE;
		return -1;
	}
	if (c->lighting_enabled) {
		out->g.refuse = TGL_GRID_REFUSE_LIGHT;
		return -1;
	}

	m = &c->matrix_model_projection.m[0][0];
	if (!c->matrix_model_projection_no_w_transform) {
		out->g.refuse = TGL_GRID_REFUSE_W;
		return -1;
	}
	if (m[1] != 0.0f || m[4] != 0.0f) {
		out->g.refuse = TGL_GRID_REFUSE_SKEW;
		return -1;
	}

	/* Direct field stores, the position lift identical to `tgl_grid_map()`'s, for the identical
	 * ABI reason written out there: a local struct and `*out = g` keeps the singles live past
	 * what the o32 hard-float file has, and the spills come out as `sdc1`/`ldc1`, which this CPU
	 * swallows without answering.  `a_byte` has no meaning here -- the colour is per-vertex and
	 * four-channel -- and is left at what the caller's memset put, so the field cannot pretend. */
	out->g.mx = m[0];
	out->g.xc = m[3];
	out->g.my = m[5];
	out->g.yc = m[7];
	out->g.winv = 1.0 / m[15];
	out->g.scale_x = c->viewport.scale.X;
	out->g.trans_x = c->viewport.trans.X;
	out->g.scale_y = c->viewport.scale.Y;
	out->g.trans_y = c->viewport.trans.Y;

	out->g.vp_x0 = (GLint)c->viewport.xmin;
	out->g.vp_x1 = (GLint)(c->viewport.xmin + c->viewport.xsize - 1);
	out->g.vp_y0 = (GLint)c->viewport.ymin;
	out->g.vp_y1 = (GLint)(c->viewport.ymin + c->viewport.ysize - 1);

	out->g.smooth = (c->current_shade_model == GL_SMOOTH) ? 1u : 0u;
	out->g.blended = 0u;
#if TGL_FEATURE_BLEND == 1
	if (c->zb != NULL && c->zb->enable_blend != 0) out->g.blended = 1u;
#endif

	/* The upload's own mapper constants and the plane key.  Both halves of the texel chain read
	 * these, and the same four values are what `tgl_vertex_to_viewport()` reads off
	 * `c->current_texture` per vertex -- lifted once here is the whole saving. */
	out->sscale = im->sscale;
	out->soff = im->soff;
	out->tscale = im->tscale;
	out->toff = im->toff;
	out->pixmap = im->pixmap;

	out->g.refuse = TGL_GRID_REFUSE_NONE;
	out->g.ok = 1u;
	return 0;
}

void tgl_grid_map_tex_points(const tgl_tex_map_t* m, const float* xs, const float* ys,
                             const float* us, const float* vs, const float* cols, int n,
                             tgl_tex_pt_t* out) {
	int i;

	if (m == NULL || !m->g.ok) return;
	if (xs == NULL || ys == NULL || us == NULL || vs == NULL || cols == NULL || out == NULL)
		return;

	/* Four passes, for the reason 12f's three give in full above: hoisting is per-loop, the
	 * o32 hard-float file has twenty caller-saved singles, and a body that needs more spills
	 * an even/odd PAIR through `sdc1` -- an instruction this CPU does not answer.  Position X
	 * hoists five, Y five, texels four (`sscale`/`soff`/`tscale`/`toff`; `gs_float_scale_pow2`
	 * works on the bits in a general register and adds nothing to the float file), colour zero
	 * -- the mask pair is a compile-time constant the arithmetic folds. */
	for (i = 0; i < n; i++) {
		GLfloat pcx = (GLfloat)(GLfloat)((GLfloat)xs[i] * m->g.mx);

		pcx = (GLfloat)(pcx + m->g.xc);
		out[i].x_px = TGL_TRUNC(pcx * m->g.winv * m->g.scale_x + m->g.trans_x);
	}

	for (i = 0; i < n; i++) {
		GLfloat pcy = (GLfloat)(GLfloat)((GLfloat)ys[i] * m->g.my);

		pcy = (GLfloat)(pcy + m->g.yc);
		out[i].y_px = TGL_TRUNC(pcy * m->g.winv * m->g.scale_y + m->g.trans_y);
	}

	for (i = 0; i < n; i++) {
		/* The walk's two steps, spelled from the two lines they reproduce.  The truncate lands
		 * the texel coordinate in the upload's fixed point -- `trunc.s` is the conversion this
		 * CPU has no instruction for and `TGL_TRUNC` exists to answer -- and the scale moves
		 * only the exponent, exactly dividing by the 2^(frac+1) the fixed point was multiplied
		 * into.  The association of the multiply-add is `tgl_vertex_to_viewport`'s own. */
		const GLint s_fp = TGL_TRUNC(us[i] * m->sscale + m->soff);
		const GLint t_fp = TGL_TRUNC(vs[i] * m->tscale + m->toff);

		out[i].s = gs_float_scale_pow2((float)s_fp, TGL_TEX_S_SHIFT);
		out[i].t = gs_float_scale_pow2((float)t_fp, TGL_TEX_T_SHIFT);
	}

	for (i = 0; i < n; i++) {
		/* Per-vertex, per-channel: the byte each of `v->zp.r..a` would carry, in the field
		 * `gs_vertex` packs them into.  An icon quad's (br,br,br,a) and its shadow's (0,0,0,a)
		 * are both cases of this, and both are bit-identical to the walk's by the same
		 * truncate-mask-shift the grey map already gates. */
		const GLuint r = grid_channel_byte((GLfloat)cols[i * 4 + 0]);
		const GLuint g = grid_channel_byte((GLfloat)cols[i * 4 + 1]);
		const GLuint b = grid_channel_byte((GLfloat)cols[i * 4 + 2]);
		const GLuint a = grid_channel_byte((GLfloat)cols[i * 4 + 3]);

		out[i].rgba = (r & 0xFFu) | ((g & 0xFFu) << 8) | ((b & 0xFFu) << 16) | ((a & 0xFFu) << 24);
	}
}
