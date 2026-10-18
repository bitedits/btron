#include "msghandling.h"
#include "zgl.h"
/* gl_fill_stats_t, the shape this file's counters are read through, belongs to the
 * application's side of the backend boundary rather than to tinygl, so it is included here
 * instead of being written twice.  The macro shims are switched off because this file defines
 * GL-adjacent names of its own and has no use for a dispatch table. */
#define BTRON_GL_NO_MACRO_SHIMS
#include "../gl_dispatch.h"

#define CLIP_XMIN (1 << 0)
#define CLIP_XMAX (1 << 1)
#define CLIP_YMIN (1 << 2)
#define CLIP_YMAX (1 << 3)
#define CLIP_ZMIN (1 << 4)
#define CLIP_ZMAX (1 << 5)

/* What used to be here was `gl_transform_to_viewport_clip_c()`, the clipper's own copy of the
 * viewport map -- the same three projection lines, the same four colour lines and the same two
 * texture lines as `tgl_vertex_to_viewport()` in src/gl/tinygl/vertex_stage.c, differing only in
 * spelling `(GLint)` where that unit writes `TGL_TRUNC()`.  Two encodings of one law is what this
 * port does not keep, and here the copy was also the expensive one: it lives in a -msoft-float
 * unit, so its nine casts, twelve multiplies, nine adds and one divide are 31 runtime libcalls
 * per vertex the clipper creates, priced at 0.98 ms a frame by the [XMBT] census's fdiv= column
 * (one divide per call, so the column counts the calls).  The shared unit is the second
 * -mhard-float TU in the image, so the same arithmetic runs inline on COP1, and the boundary row 7
 * made legal is the one this crosses: a `GLVertex *` goes in and nothing comes back, which is what
 * makes the two spellings interchangeable rather than merely similar.  They also truncate through
 * the same ps2_sf_fixsfsi() on both sides of the FPU dial -- the cast in the soft-float unit is
 * `__fixsfsi`, the inline in the hard-float one is its body -- so no vertex moves by a pixel when
 * the duplicate dies.  See doc/txt/PS2.txt, LANE 12b. */



#define clip_funcdef(name, sign, dir, dir1, dir2)                                                                                                              \
	static GLfloat name(V4* c, V4* a, V4* b) {                                                                                                                 \
		GLfloat t, dX, dY, dZ, dW, den;                                                                                                                        \
		dX = (b->X - a->X);                                                                                                                                    \
		dY = (b->Y - a->Y);                                                                                                                                    \
		dZ = (b->Z - a->Z);                                                                                                                                    \
		dW = (b->W - a->W);                                                                                                                                    \
		den = -(sign d##dir) + dW;                                                                                                                             \
		if (den == 0)                                                                                                                                          \
			t = 0;                                                                                                                                             \
		else                                                                                                                                                   \
			t = (sign a->dir - a->W) / den;                                                                                                                    \
		c->dir1 = a->dir1 + t * d##dir1;                                                                                                                       \
		c->dir2 = a->dir2 + t * d##dir2;                                                                                                                       \
		c->W = a->W + t * dW;                                                                                                                                  \
		c->dir = sign c->W;                                                                                                                                    \
		return t;                                                                                                                                              \
	}
clip_funcdef(clip_xmin, -, X, Y, Z)

	clip_funcdef(clip_xmax, +, X, Y, Z)

		clip_funcdef(clip_ymin, -, Y, X, Z)

			clip_funcdef(clip_ymax, +, Y, X, Z)
			
				clip_funcdef(clip_zmin, -, Z, X, Y)

					clip_funcdef(clip_zmax, +, Z, X, Y)

static GLfloat (*clip_proc[6])(V4*, V4*, V4*) = {clip_xmin, clip_xmax, clip_ymin, clip_ymax, clip_zmin, clip_zmax};
/* point */
#if TGL_FEATURE_ALT_RENDERMODES == 1
static void gl_add_select1(GLint z1, GLint z2, GLint z3) {
	GLint min, max;
	min = max = z1;
	if (z2 < min)
		min = z2;
	if (z3 < min)
		min = z3;
	if (z2 > max)
		max = z2;
	if (z3 > max)
		max = z3;

	gl_add_select(0xffffffff - min, 0xffffffff - max);
}
#else
#define gl_add_select1(a, b, c) /*a comment*/
#endif

#if defined(BTRON_PS2_TARGET)
#include "../backend_eegs.h"
#include "../../drivers/ps2/ps2_gs_pack.h"   /* gs_float_scale_pow2(), the ST field's unit */

#define GS_TEX_S_SHIFT (ZB_POINT_S_FRAC_BITS + 1)
#define GS_TEX_T_SHIFT (ZB_POINT_T_FRAC_BITS + 1)

static void gs_vertex(eegs_vertex_t *out, const ZBufferPoint *p,
                      const ZBufferPoint *flat, int textured)
{
	const ZBufferPoint *col = (flat != NULL) ? flat : p;
	GLContext *c = gl_get_context();
	const int depth_test = (c != NULL && c->zb != NULL && c->zb->depth_test != 0);

	out->x_px = p->x;
	out->y_px = p->y;
	out->z = depth_test ? ((uint32_t)p->z << 2) : 0u;
	out->rgba = (((unsigned)col->r >> 16) & 0xFFu) |
	            (((((unsigned)col->g >> 16) & 0xFFu)) << 8) |
	            (((((unsigned)col->b >> 16) & 0xFFu)) << 16) |
	            (((((unsigned)col->a >> 16) & 0xFFu)) << 24);
	out->s = textured ? gs_float_scale_pow2((float)p->s, GS_TEX_S_SHIFT) : 0.0f;
	out->t = textured ? gs_float_scale_pow2((float)p->t, GS_TEX_T_SHIFT) : 0.0f;
}

static eegs_tri_state_t gs_state(int smooth, int textured, int blended, int depth_test)
{
	eegs_tri_state_t st;

	st.smooth = smooth ? 1u : 0u;
	st.textured = textured ? 1u : 0u;
	st.blended = blended ? 1u : 0u;
	st.depth_test = depth_test ? 1u : 0u;
	return st;
}

static int gs_blended(const GLContext *c)
{
#if TGL_FEATURE_BLEND == 1
	return (c->zb->enable_blend != 0);
#else
	(void)c;
	return 0;
#endif
}
#endif

void gl_draw_point(GLVertex* p0) {
	GLContext* c = gl_get_context();
#if defined(BTRON_PS2_TARGET)
	if (gl_active_backend() == GL_BACKEND_EEGS) {
		if (p0->clip_code == 0) {
			const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
			const int blended = gs_blended(c);
			eegs_tri_state_t st = gs_state(0, 0, blended, depth_test);
			eegs_vertex_t v[1];
			gs_vertex(&v[0], &p0->zp, NULL, 0);
			eegs_point(v, st);
		}
		return;
	}
#endif
	if (p0->clip_code == 0) {
#if TGL_FEATURE_ALT_RENDERMODES == 1
		if (c->render_mode == GL_SELECT) {
			gl_add_select(p0->zp.z, p0->zp.z);

		} else if (c->render_mode == GL_FEEDBACK) {
			gl_add_feedback(GL_POINT_TOKEN, p0, NULL, NULL, 0);
		} else
#endif
		{
			ZB_plot(c->zb, &p0->zp);
		}
	}
}

/* line */

/*
 * Line Clipping
 */

static void GLinterpolate(GLVertex* q, GLVertex* p0, GLVertex* p1, GLfloat t) { 
	GLint i;
	q->pc.X = p0->pc.X + (p1->pc.X - p0->pc.X) * t;
	q->pc.Y = p0->pc.Y + (p1->pc.Y - p0->pc.Y) * t;
	q->pc.Z = p0->pc.Z + (p1->pc.Z - p0->pc.Z) * t;
	q->pc.W = p0->pc.W + (p1->pc.W - p0->pc.W) * t;
#ifdef _OPENMP
#pragma omp simd
#endif
	for (i = 0; i < 3; i++)
		q->color.v[i] = p0->color.v[i] + (p1->color.v[i] - p0->color.v[i]) * t;
}

/* Line Clipping algorithm from 'Computer Graphics', Principles and
   Practice */
static GLint ClipLine1(GLfloat denom, GLfloat num, GLfloat* tmin, GLfloat* tmax) {
	GLfloat t;

	if (denom > 0) {
		t = num / denom;
		if (t > *tmax)
			return 0;
		if (t > *tmin)
			*tmin = t;
	} else if (denom < 0) {
		t = num / denom;
		if (t < *tmin)
			return 0;
		if (t < *tmax)
			*tmax = t;
	} else if (num > 0)
		return 0;
	return 1;
}
void gl_draw_line(GLVertex* p1, GLVertex* p2) {
	GLContext* c = gl_get_context();
	GLfloat dx, dy, dz, dw, x1, y1, z1, w1;

	GLVertex q1, q2;
	GLint cc1, cc2;

	cc1 = p1->clip_code;
	cc2 = p2->clip_code;

	if ((cc1 | cc2) == 0) {
#if defined(BTRON_PS2_TARGET)
		if (gl_active_backend() == GL_BACKEND_EEGS) {
			const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
			const int blended = gs_blended(c);
			eegs_tri_state_t st = gs_state(0, 0, blended, depth_test);
			eegs_vertex_t v[2];
			gs_vertex(&v[0], &p1->zp, NULL, 0);
			gs_vertex(&v[1], &p2->zp, NULL, 0);
			eegs_line(v, st);
			return;
		}
#endif
#if TGL_FEATURE_ALT_RENDERMODES == 1
		if (c->render_mode == GL_SELECT) {
			gl_add_select1(p1->zp.z, p2->zp.z, p2->zp.z);
		} else if (c->render_mode == GL_FEEDBACK) {
			gl_add_feedback(GL_LINE_TOKEN, p1, p2, NULL, 0);
		} else
#endif
		{
			if (c->zb->depth_test)
				ZB_line_z(c->zb, &p1->zp, &p2->zp);
			else
				ZB_line(c->zb, &p1->zp, &p2->zp);
		}
	} else if ((cc1 & cc2) != 0) {
		return;
	} else {
		dx = p2->pc.X - p1->pc.X;
		dy = p2->pc.Y - p1->pc.Y;
		dz = p2->pc.Z - p1->pc.Z;
		dw = p2->pc.W - p1->pc.W;
		x1 = p1->pc.X;
		y1 = p1->pc.Y;
		z1 = p1->pc.Z;
		w1 = p1->pc.W;

		GLfloat tmin = 0;
		GLfloat tmax = 1;
		if (ClipLine1(dx + dw, -x1 - w1, &tmin, &tmax) && ClipLine1(-dx + dw, x1 - w1, &tmin, &tmax) && ClipLine1(dy + dw, -y1 - w1, &tmin, &tmax) &&
			ClipLine1(-dy + dw, y1 - w1, &tmin, &tmax) && ClipLine1(dz + dw, -z1 - w1, &tmin, &tmax) && ClipLine1(-dz + dw, z1 - w1, &tmin, &tmax)) {

			GLinterpolate(&q1, p1, p2, tmin);
			GLinterpolate(&q2, p1, p2, tmax);
			tgl_vertex_to_viewport(&q1);
			tgl_vertex_to_viewport(&q2);
#if defined(BTRON_PS2_TARGET)
			if (gl_active_backend() == GL_BACKEND_EEGS) {
				const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
				const int blended = gs_blended(c);
				eegs_tri_state_t st = gs_state(0, 0, blended, depth_test);
				eegs_vertex_t v[2];
				gs_vertex(&v[0], &q1.zp, NULL, 0);
				gs_vertex(&v[1], &q2.zp, NULL, 0);
				eegs_line(v, st);
				return;
			}
#endif
#if TGL_FEATURE_ALT_RENDERMODES == 1
			if (c->render_mode == GL_SELECT) {
				gl_add_select1(q1.zp.z, q2.zp.z, q2.zp.z);
			} else if (c->render_mode == GL_FEEDBACK) {
				gl_add_feedback(GL_LINE_TOKEN, &q1, &q2, NULL, 0);
			} else
#endif
			{
				if (c->zb->depth_test)
					ZB_line_z(c->zb, &q1.zp, &q2.zp);
				else
					ZB_line(c->zb, &q1.zp, &q2.zp);
			}
		}
	}
}



/* ── Where the faces go ────────────────────────────────────────────────────────────
 *
 * `gl_draw_triangle` is the one funnel every polygon face passes on its way to a fill, and the
 * four returns below are the only ways a face can be asked for and never drawn: zero area on
 * screen, culled by the winding, wholly outside the clip volume, or partly inside and handed to
 * the clipper.  A frame that spends its microseconds in the vertex stages and covers nothing is
 * separated by these six counts into which of those it did -- which is a question no amount of
 * reading the transform can answer, because all four look identical from the outside and each
 * has a different thing to fix.
 *
 * `clip_or` is the OR of every clip code the funnel saw: zero means nothing was outside, and a
 * bit that is always set (XMAX against a W of 1, say) names the plane the geometry never got
 * inside of, which is how an unapplied projection shows up without a float formatter.
 *
 * Counted like the fill totals above -- one integer increment at a decision the function was
 * already making, no register taken from a span loop -- and read by the PS2 bench's row, so a
 * shipped frame carries the cost and nothing else. */
static unsigned s_face_total, s_face_degen, s_face_cull, s_face_out;
static unsigned s_face_partial, s_face_drawn, s_face_clipor;

/* A float as its own bits, for the rows below: the console has no float formatter to trust
 * (src/apps/xmb.c prints fixed-point for the same reason), and a reader of a bit pattern can
 * still see the sign, the exponent and whether the number is 1.0.  Moved through memcpy rather
 * than a cast, which is the same one instruction and is not a type-pun. */
static unsigned tgl_bits(GLfloat f)
{
    unsigned u;
    __builtin_memcpy(&u, &f, sizeof u);
    return u;
}

/* The counts say a face died; only the numbers say where it died.  The first faces through the
 * funnel are kept whole -- the coordinates the application asked for, the projected coordinates
 * the transform returned, the clip code it derived from them, and the viewport mapping it stored
 * beside them -- because `out=5406 clipor=0x2f` has exactly two readings that need different
 * fixes: pc.X/Y/Z are genuinely outside the volume, in which case the transform is the thing to
 * audit, or they are inside it and `gl_clipcode()` said so anyway, in which case the arithmetic
 * is fine and the comparisons on this CPU are not.  The input coordinates settle a third
 * possibility the pair cannot: that the vertex never carried what the app wrote. */
#define TGL_FACE_DUMP_N 4
#define TGL_FACE_DUMP_W 12
static unsigned s_face_dump[TGL_FACE_DUMP_N][TGL_FACE_DUMP_W];
static unsigned s_face_dump_n;

void tgl_face_dump_copy(unsigned *out, unsigned *n_out)
{
    unsigned i, j;

    if (n_out)
        *n_out = s_face_dump_n;
    if (!out)
        return;
    for (i = 0; i < s_face_dump_n; i++)
        for (j = 0; j < TGL_FACE_DUMP_W; j++)
            out[i * TGL_FACE_DUMP_W + j] = s_face_dump[i][j];
}

static void tgl_face_dump_one(const GLVertex *p0, GLint co)
{
    unsigned *d;

    if (s_face_dump_n >= TGL_FACE_DUMP_N)
        return;
    d = s_face_dump[s_face_dump_n++];
    d[0]  = tgl_bits(p0->coord.X);
    d[1]  = tgl_bits(p0->coord.Y);
    d[2]  = tgl_bits(p0->coord.Z);
    d[3]  = tgl_bits(p0->pc.X);
    d[4]  = tgl_bits(p0->pc.Y);
    d[5]  = tgl_bits(p0->pc.Z);
    d[6]  = tgl_bits(p0->pc.W);
    d[7]  = (unsigned)co;
    d[8]  = (unsigned)p0->zp.x;
    d[9]  = (unsigned)p0->zp.y;
    d[10] = (unsigned)p0->zp.z;
    d[11] = (unsigned)p0->clip_code;
}

/* The context state that decides those exits, as raw words: the viewport's own scale and
 * translation, the three corners of the model-projection matrix that an ortho sets, and the
 * flags the funnel reads. */
#define TGL_FACE_STATS_N 20

void tgl_face_stats(unsigned *out, int reset)
{
    const GLContext *c = gl_get_context();
    const GLfloat *mp = &c->matrix_model_projection.m[0][0];

    if (out) {
        out[0]  = s_face_total;
        out[1]  = s_face_degen;
        out[2]  = s_face_cull;
        out[3]  = s_face_out;
        out[4]  = s_face_partial;
        out[5]  = s_face_drawn;
        out[6]  = s_face_clipor;
        out[7]  = (unsigned)c->viewport.xsize;
        out[8]  = (unsigned)c->viewport.ysize;
        out[9]  = tgl_bits(c->viewport.scale.X);
        out[10] = tgl_bits(c->viewport.trans.X);
        out[11] = tgl_bits(c->viewport.scale.Y);
        out[12] = tgl_bits(c->viewport.trans.Y);
        out[13] = tgl_bits(mp[0]);   /* m[0][0] */
        out[14] = tgl_bits(mp[5]);   /* m[1][1] */
        out[15] = tgl_bits(mp[15]);  /* m[3][3] */
        out[16] = (unsigned)c->polygon_mode_front;
        out[17] = (unsigned)c->cull_face_enabled;
        out[18] = (unsigned)c->current_front_face;
        out[19] = (unsigned)c->matrix_model_projection_no_w_transform;
    }
    if (!reset)
        return;
    s_face_total = s_face_degen = s_face_cull = 0u;
    s_face_out = s_face_partial = s_face_drawn = s_face_clipor = 0u;
    s_face_dump_n = 0u;
}

/*Triangles*/

static void updateTmp(GLVertex* q, GLVertex* p0, GLVertex* p1, GLfloat t) { 
	GLContext* c = gl_get_context();
	{

		q->color.v[0] = p0->color.v[0] + (p1->color.v[0] - p0->color.v[0]) * t;
		q->color.v[1] = p0->color.v[1] + (p1->color.v[1] - p0->color.v[1]) * t;
		q->color.v[2] = p0->color.v[2] + (p1->color.v[2] - p0->color.v[2]) * t;
		/* Alpha too.  A clipped primitive is re-generated here rather than passed through,
		 * so leaving this out would reset the fade of every edge vertex that crosses a clip
		 * plane -- visible as a hard alpha seam on a triangle that is otherwise tweening. */
		q->color.v[3] = p0->color.v[3] + (p1->color.v[3] - p0->color.v[3]) * t;
	}

#if TGL_OPTIMIZATION_HINT_BRANCH_COST < 1
	if (c->texture_2d_enabled)
#endif
	{
		q->tex_coord.X = p0->tex_coord.X + (p1->tex_coord.X - p0->tex_coord.X) * t;
		q->tex_coord.Y = p0->tex_coord.Y + (p1->tex_coord.Y - p0->tex_coord.Y) * t;
	}

	q->clip_code = gl_clipcode(q->pc.X, q->pc.Y, q->pc.Z, q->pc.W);
	if (q->clip_code == 0)
		tgl_vertex_to_viewport(q);
}

static void gl_draw_triangle_clip(GLVertex* p0, GLVertex* p1, GLVertex* p2, GLint clip_bit); 

void gl_draw_triangle(GLVertex* p0, GLVertex* p1, GLVertex* p2) {
	GLContext* c = gl_get_context();
	GLint co, cc[3], front;

	cc[0] = p0->clip_code;
	cc[1] = p1->clip_code;
	cc[2] = p2->clip_code;

	co = cc[0] | cc[1] | cc[2];

	s_face_total++;
	s_face_clipor |= (unsigned)co;
	tgl_face_dump_one(p0, co);

	/* we handle the non clipped case here to go faster */
	if (co == 0) {
		GLfloat norm;
		norm = (GLfloat)(p1->zp.x - p0->zp.x) * (GLfloat)(p2->zp.y - p0->zp.y) - (GLfloat)(p2->zp.x - p0->zp.x) * (GLfloat)(p1->zp.y - p0->zp.y);

		if (norm == 0) {
			s_face_degen++;
			return;
		}

		front = norm < 0.0;
		front = front ^ c->current_front_face; 

		/* back face culling */
		if (c->cull_face_enabled) {
			/* most used case first */
			if (c->current_cull_face == GL_BACK) {
				if (front == 0) {
					s_face_cull++;
					return;
				}
				c->draw_triangle_front(p0, p1, p2);
			} else if (c->current_cull_face == GL_FRONT) {
				if (front != 0) {
					s_face_cull++;
					return;
				}
				c->draw_triangle_back(p0, p1, p2);
			} else {
				s_face_cull++;
				return;
			}
		} else {
			/* no culling */
			s_face_drawn++;
			if (front) {
				c->draw_triangle_front(p0, p1, p2);
			} else {
				c->draw_triangle_back(p0, p1, p2);
			}
		}
	} else {
		/* GLint c_and = cc[0] & cc[1] & cc[2];*/
		if ((cc[0] & cc[1] & cc[2]) == 0) { /* Don't draw a triangle with no points*/
			s_face_partial++;
			gl_draw_triangle_clip(p0, p1, p2, 0);
		} else {
			s_face_out++;
		}
	}
}

static void gl_draw_triangle_clip(GLVertex* p0, GLVertex* p1, GLVertex* p2, GLint clip_bit) {
	
	GLint co, c_and, co1, cc[3], edge_flag_tmp, clip_mask;
	
	GLVertex* q[3];

	cc[0] = p0->clip_code;
	cc[1] = p1->clip_code;
	cc[2] = p2->clip_code;

	co = cc[0] | cc[1] | cc[2];
	if (co == 0) {
		gl_draw_triangle(p0, p1, p2);
	} else {

		c_and = cc[0] & cc[1] & cc[2];
		/* the triangle is completely outside */
		if (c_and != 0)
			return;

		/* find the next direction to clip */
		while (clip_bit < 6 && (co & (1 << clip_bit)) == 0) {
			clip_bit++;
		}

		/* this test can be true only in case of rounding errors */
		if (clip_bit == 6) { /* The 2 bit and the 4 bit.*/
#if 0
      tgl_warning("Error:\n");tgl_warning("%f %f %f %f\n",p0->pc.X,p0->pc.Y,p0->pc.Z,p0->pc.W);tgl_warning("%f %f %f %f\n",p1->pc.X,p1->pc.Y,p1->pc.Z,p1->pc.W);tgl_warning("%f %f %f %f\n",p2->pc.X,p2->pc.Y,p2->pc.Z,p2->pc.W);
#endif
			return;
		}

		clip_mask = 1 << clip_bit;
		co1 = (cc[0] ^ cc[1] ^ cc[2]) & clip_mask;

		if (co1) {
			/* one point outside */

			if (cc[0] & clip_mask) {
				q[0] = p0;
				q[1] = p1;
				q[2] = p2;
			} else if (cc[1] & clip_mask) {
				q[0] = p1;
				q[1] = p2;
				q[2] = p0;
			} else {
				q[0] = p2;
				q[1] = p0;
				q[2] = p1;
			}
			{
				GLVertex tmp1, tmp2;
				GLfloat tt;
				tt = clip_proc[clip_bit](&tmp1.pc, &q[0]->pc, &q[1]->pc);
				updateTmp(&tmp1, q[0], q[1], tt);

				tt = clip_proc[clip_bit](&tmp2.pc, &q[0]->pc, &q[2]->pc);
				updateTmp(&tmp2, q[0], q[2], tt);

				tmp1.edge_flag = q[0]->edge_flag;
				edge_flag_tmp = q[2]->edge_flag;
				q[2]->edge_flag = 0;
				gl_draw_triangle_clip(&tmp1, q[1], q[2], clip_bit + 1);

				tmp2.edge_flag = 1;
				tmp1.edge_flag = 0;
				q[2]->edge_flag = edge_flag_tmp;
				gl_draw_triangle_clip(&tmp2, &tmp1, q[2], clip_bit + 1);
			}
		} else {
			/* two points outside */

			if ((cc[0] & clip_mask) == 0) {
				q[0] = p0;
				q[1] = p1;
				q[2] = p2;
			} else if ((cc[1] & clip_mask) == 0) {
				q[0] = p1;
				q[1] = p2;
				q[2] = p0;
			} else {
				q[0] = p2;
				q[1] = p0;
				q[2] = p1;
			}
			{
				GLVertex tmp1, tmp2;
				GLfloat tt;
				tt = clip_proc[clip_bit](&tmp1.pc, &q[0]->pc, &q[1]->pc);
				updateTmp(&tmp1, q[0], q[1], tt);

				tt = clip_proc[clip_bit](&tmp2.pc, &q[0]->pc, &q[2]->pc);
				updateTmp(&tmp2, q[0], q[2], tt);

				tmp1.edge_flag = 1;
				tmp2.edge_flag = q[2]->edge_flag;
				gl_draw_triangle_clip(q[0], &tmp1, &tmp2, clip_bit + 1);
			}
		}
	}
}

/* see vertex.c to see how the draw functions are assigned.*/
void gl_draw_triangle_select(GLVertex* p0, GLVertex* p1, GLVertex* p2) { gl_add_select1(p0->zp.z, p1->zp.z, p2->zp.z); }
void gl_draw_triangle_feedback(GLVertex* p0, GLVertex* p1, GLVertex* p2) { gl_add_feedback(GL_POLYGON_TOKEN, p0, p1, p2, 0); }

/* Which fill path each triangle took, and how much of the screen it was asked to cover.
 *
 * A fill costs its setup once and its area per pixel, so these two counts are what tells a
 * stage bound by *pixels* from a stage bound by *triangles* -- which is the difference
 * between making the blend cheaper and making there be fewer of them.  They are counted
 * here, at the dispatch, rather than inside the span loops: an accumulator in ztriangle.h
 * takes a register from the very loop it is meant to be measuring, while the area of a
 * triangle is a handful of integer operations against a fill that is either hundreds of
 * thousands of pixels or hundreds of cycles of setup.
 *
 * The coordinates are viewport-clipped by the time a triangle reaches this function, so
 * each product is bounded by the canvas (800 x 600 = 480,000 against 2,147,483,647) and the
 * area stays 32-bit -- which matters here because a 64-bit product would need __muldi3, and
 * the PS2 image forbids every named undefined symbol. */
static unsigned s_fill_tris, s_fill_covered;
static unsigned s_fill_tex_tris, s_fill_tex_covered;
static unsigned s_fill_blended;
static unsigned s_fill_scanlines;

/* Time is bracketed here rather than inside ztriangle.h's loops: two reads of the clock per
 * fill is some thousands of cycles across a stage of thousands of fills, while an
 * accumulator inside the span loop would take a register from the thing being timed.  What
 * it measures is the fill call including its per-triangle setup and its per-scanline edge
 * walk, so what the stage's own time leaves over is the vertex pipeline and the clipping.
 * The clock is this boundary's service (gl_dispatch.c), and it answers 0 where the target
 * has installed none, which leaves the sum at zero -- the absence is visible as zero, never
 * mistaken for an instant rasterizer. */
static unsigned s_fill_us;

/* The geometry pipeline is billed by vertices, which a strip shares out and a triangle count
 * does not see; incremented in vertex.c. */
unsigned tgl_stat_verts;

static unsigned tgl_tri_covered(const ZBufferPoint *a, const ZBufferPoint *b, const ZBufferPoint *c)
{
    const long cross = (long)(b->x - a->x) * (long)(c->y - a->y)
                     - (long)(c->x - a->x) * (long)(b->y - a->y);

    /* The exact covered count differs from the area by the half-pixel rules on each edge,
     * which is at most the perimeter -- some 2,600 pixels against a full-screen quad's
     * 384,000, and nothing the caller reads is a decision between two candidate costs. */
    return (unsigned)(cross < 0 ? -cross : cross) / 2u;
}

/* Copy the counts into the caller's window and, when reset is non-zero, start that window
 * again -- so the window is whatever the application brackets.  The struct is gl_fill_stats_t
 * from src/gl/gl_dispatch.h, which is the application's side of this boundary and is included
 * here rather than mirrored: the field names are written once. */
void tgl_fill_stats(gl_fill_stats_t *out, int reset)
{
    out->tris            = s_fill_tris;
    out->covered         = s_fill_covered;
    out->textured_tris   = s_fill_tex_tris;
    out->textured_covered = s_fill_tex_covered;
    out->blended_tris    = s_fill_blended;
    out->scanlines       = s_fill_scanlines;
    out->fill_us         = s_fill_us;
    out->verts           = tgl_stat_verts;
    out->reported        = 1;

    if (!reset)
        return;
    s_fill_tris = s_fill_covered = 0u;
    s_fill_tex_tris = s_fill_tex_covered = 0u;
    s_fill_blended = 0u;
    s_fill_scanlines = 0u;
    s_fill_us = 0u;
    tgl_stat_verts = 0u;
}

static void tgl_count_fill(const ZBufferPoint *a, const ZBufferPoint *b, const ZBufferPoint *c,
                           int textured, int blended)
{
    const unsigned cov = tgl_tri_covered(a, b, c);
    unsigned ymin = a->y, ymax = a->y;

    if (b->y < ymin) ymin = b->y;
    if (b->y > ymax) ymax = b->y;
    if (c->y < ymin) ymin = c->y;
    if (c->y > ymax) ymax = c->y;

    s_fill_tris++;
    s_fill_covered += cov;
    s_fill_scanlines += ymax - ymin + 1u;
    if (textured) {
        s_fill_tex_tris++;
        s_fill_tex_covered += cov;
    }
    if (blended) s_fill_blended++;
}

#if defined(BTRON_PS2_TARGET)
static void gs_edges(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2)
{
	const int smooth = (c->current_shade_model == GL_SMOOTH);
	const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
	const eegs_tri_state_t st = gs_state(smooth, 0, gs_blended(c), depth_test);
	const ZBufferPoint *flat = smooth ? NULL : &p2->zp;
	eegs_vertex_t v[2];

	if (p0->edge_flag) {
		gs_vertex(&v[0], &p0->zp, flat, 0);
		gs_vertex(&v[1], &p1->zp, flat, 0);
		eegs_line(v, st);
	}
	if (p1->edge_flag) {
		gs_vertex(&v[0], &p1->zp, flat, 0);
		gs_vertex(&v[1], &p2->zp, flat, 0);
		eegs_line(v, st);
	}
	if (p2->edge_flag) {
		gs_vertex(&v[0], &p2->zp, flat, 0);
		gs_vertex(&v[1], &p0->zp, flat, 0);
		eegs_line(v, st);
	}
}

static void gs_points(GLContext *c, GLVertex *p0, GLVertex *p1, GLVertex *p2)
{
	const int smooth = (c->current_shade_model == GL_SMOOTH);
	const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
	const eegs_tri_state_t st = gs_state(smooth, 0, gs_blended(c), depth_test);
	const ZBufferPoint *flat = smooth ? NULL : &p2->zp;
	eegs_vertex_t v[1];

	if (p0->edge_flag) {
		gs_vertex(v, &p0->zp, flat, 0);
		eegs_point(v, st);
	}
	if (p1->edge_flag) {
		gs_vertex(v, &p1->zp, flat, 0);
		eegs_point(v, st);
	}
	if (p2->edge_flag) {
		gs_vertex(v, &p2->zp, flat, 0);
		eegs_point(v, st);
	}
}
#endif /* BTRON_PS2_TARGET */

/* see vertex.c to see how the draw functions are assigned.*/
void gl_draw_triangle_fill(GLVertex* p0, GLVertex* p1, GLVertex* p2) { 
	GLContext* c = gl_get_context();
	GLImage* im = NULL;
#if TGL_FEATURE_BLEND == 1
	const int fill_blended = (c->zb->enable_blend != 0);
#else
	const int fill_blended = 0;   /* the arms below take the plain path whatever the state */
#endif

	if (c->texture_2d_enabled && c->current_texture != NULL) {
		im = &c->current_texture->images[0];
		/* A texture that is bound but was never uploaded has no plane.  Sampling it would
		 * read through a NULL pixmap, so an absent plane is treated exactly like no texture
		 * and the primitive takes the flat/smooth path it would have taken untextured. */
		if (im->pixmap == NULL)
			im = NULL;
	}

	/* Counted once, before the dispatch, with the same conditions the arms below read --
	 * including here, where an absent plane has already been folded back to untextured. */
	tgl_count_fill(&p0->zp, &p1->zp, &p2->zp, im != NULL, fill_blended);

#if defined(BTRON_PS2_TARGET)
	/* Counted above, then handed to the GS instead of filled here: the counters describe what the
	 * rasteriser was asked to draw, and that question is the same whichever one draws it.  What
	 * stops is the per-pixel work, so the fill's microseconds below are legitimately never
	 * accumulated -- a GS frame reads its fill time as zero because there is no fill to time. */
	if (gl_active_backend() == GL_BACKEND_EEGS) {
		const int textured = (im != NULL);
		const int smooth = (c->current_shade_model == GL_SMOOTH);
		const int depth_test = (c->zb != NULL && c->zb->depth_test != 0);
		const ZBufferPoint *flat = smooth ? NULL : &p2->zp;
		eegs_tri_state_t st = gs_state(smooth, textured, fill_blended, depth_test);
		eegs_vertex_t v[3];

		/* 5b: the plane this triangle samples, if its bytes ever got a VRAM address.  Pointing the
		 * stream at it is what makes `TEX0` name this sheet rather than the last one bound, and a
		 * plane that is missing -- an upload the registry could not place -- reaches the stage as
		 * 0, which clears `TME` and counts the primitive instead of sampling the page `TEX0`
		 * happens to hold. */
		eegs_texture(textured ? gs_tex_find(im->pixmap) : NULL);

		gs_vertex(&v[0], &p0->zp, flat, textured);
		gs_vertex(&v[1], &p1->zp, flat, textured);
		gs_vertex(&v[2], &p2->zp, flat, textured);
		eegs_triangle(v, st);
		return;
	}
#endif

	const unsigned t_fill_t0 = gl_perf_us();

	if (im != NULL) {
		/* if(c->current_texture)*/
#if TGL_FEATURE_LIT_TEXTURES == 1
		if (c->current_shade_model != GL_SMOOTH) {
			p1->zp.r = p2->zp.r;
			p1->zp.g = p2->zp.g;
			p1->zp.b = p2->zp.b;

			p0->zp.r = p2->zp.r;
			p0->zp.g = p2->zp.g;
			p0->zp.b = p2->zp.b;
		}
#endif

		ZB_setTexture(c->zb, im);
		/* One comparison per triangle decides which texture mapper runs.  Equal
		 * interpolated z at the three vertices means 1/z is constant over the whole area,
		 * and there the affine path is the same interpolation without the per-scanline
		 * float block, not an approximation of it.  That is the ordinary case for a 2D
		 * interface, whose projection has no w divide, and the case the [XMBT] raster rows
		 * price: glyph rows of about five pixels each, every scanline re-deriving its own
		 * texture steps. */
		const int z_const = p0->zp.z == p1->zp.z && p1->zp.z == p2->zp.z;
#if TGL_FEATURE_BLEND == 1
		if (c->zb->enable_blend) {
			if (z_const)
				ZB_fillTriangleMapping(c->zb, &p0->zp, &p1->zp, &p2->zp);
			else
				ZB_fillTriangleMappingPerspective(c->zb, &p0->zp, &p1->zp, &p2->zp);
		} else {
			if (z_const)
				ZB_fillTriangleMappingNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
			else
				ZB_fillTriangleMappingPerspectiveNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
		}
#else
		if (z_const)
			ZB_fillTriangleMappingNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
		else
			ZB_fillTriangleMappingPerspectiveNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
#endif
	} else if (c->current_shade_model == GL_SMOOTH) {
#if TGL_FEATURE_BLEND == 1
		if (c->zb->enable_blend)
			ZB_fillTriangleSmooth(c->zb, &p0->zp, &p1->zp, &p2->zp);
		else
			ZB_fillTriangleSmoothNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
#else
		ZB_fillTriangleSmoothNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
#endif
	} else {
#if TGL_FEATURE_BLEND == 1
		if (c->zb->enable_blend)
			ZB_fillTriangleFlat(c->zb, &p0->zp, &p1->zp, &p2->zp);
		else
			ZB_fillTriangleFlatNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
#else
		ZB_fillTriangleFlatNOBLEND(c->zb, &p0->zp, &p1->zp, &p2->zp);
#endif
	}

	s_fill_us += gl_perf_us() - t_fill_t0;
}

/* Render a clipped triangle in line mode */

void gl_draw_triangle_line(GLVertex* p0, GLVertex* p1, GLVertex* p2) {
	GLContext* c = gl_get_context();
#if defined(BTRON_PS2_TARGET)
	if (gl_active_backend() == GL_BACKEND_EEGS) {
		gs_edges(c, p0, p1, p2);
		return;
	}
#endif
	if (c->zb->depth_test) {
		if (p0->edge_flag)
			ZB_line_z(c->zb, &p0->zp, &p1->zp);
		if (p1->edge_flag)
			ZB_line_z(c->zb, &p1->zp, &p2->zp);
		if (p2->edge_flag)
			ZB_line_z(c->zb, &p2->zp, &p0->zp);
	} else {
		if (p0->edge_flag)
			ZB_line(c->zb, &p0->zp, &p1->zp);
		if (p1->edge_flag)
			ZB_line(c->zb, &p1->zp, &p2->zp);
		if (p2->edge_flag)
			ZB_line(c->zb, &p2->zp, &p0->zp);
	}
}

/* Render a clipped triangle in point mode */
void gl_draw_triangle_point(GLVertex* p0, GLVertex* p1, GLVertex* p2) {
	GLContext* c = gl_get_context();
#if defined(BTRON_PS2_TARGET)
	if (gl_active_backend() == GL_BACKEND_EEGS) {
		gs_points(c, p0, p1, p2);
		return;
	}
#endif
	if (p0->edge_flag)
		ZB_plot(c->zb, &p0->zp);
	if (p1->edge_flag)
		ZB_plot(c->zb, &p1->zp);
	if (p2->edge_flag)
		ZB_plot(c->zb, &p2->zp);
}
