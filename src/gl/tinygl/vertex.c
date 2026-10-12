#include "zgl.h"
#include <string.h>
void glopNormal(GLParam* p) {
	V3 v;
	GLContext* c = gl_get_context();
	v.X = p[1].f;
	v.Y = p[2].f;
	v.Z = p[3].f;

	c->current_normal.X = v.X;
	c->current_normal.Y = v.Y;
	c->current_normal.Z = v.Z;
	c->current_normal.W = 0;
}

void glopTexCoord(GLParam* p) {
	GLContext* c = gl_get_context();
	c->current_tex_coord.X = p[1].f;
	c->current_tex_coord.Y = p[2].f;
	c->current_tex_coord.Z = p[3].f;
	c->current_tex_coord.W = p[4].f;
}

void glopEdgeFlag(GLParam* p) {
	GLContext* c = gl_get_context();
	c->current_edge_flag = p[1].i;
}

void glopColor(GLParam* p) {
	GLContext* c = gl_get_context();
	c->current_color.X = p[1].f;
	c->current_color.Y = p[2].f;
	c->current_color.Z = p[3].f;
	c->current_color.W = p[4].f;

	if (c->color_material_enabled) {
		GLParam q[7];
		q[0].op = OP_Material;
		q[1].i = c->current_color_material_mode;
		q[2].i = c->current_color_material_type;
		q[3].f = p[1].f;
		q[4].f = p[2].f;
		q[5].f = p[3].f;
		q[6].f = p[4].f;
		glopMaterial(q);
	}
}

void glopBegin(GLParam* p) {
	GLint type;
	M4 tmp;
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
	if (c->in_begin != 0)
#define ERROR_FLAG GL_INVALID_OPERATION
#include "error_check.h"
#else
	
#endif
		type = p[1].i;
	c->begin_type = type;
	c->in_begin = 1;
	c->vertex_n = 0;
	c->vertex_cnt = 0;

	if (c->matrix_model_projection_updated) {

		if (c->lighting_enabled) {
			/* precompute inverse modelview */
			gl_M4_Inv(&tmp, c->matrix_stack_ptr[0]);
			gl_M4_Transpose(&c->matrix_model_view_inv, &tmp);
		} else {
			GLfloat* m = &c->matrix_model_projection.m[0][0];
			/* precompute projection matrix */
			gl_M4_Mul(&c->matrix_model_projection, c->matrix_stack_ptr[1], c->matrix_stack_ptr[0]);
			/* test to accelerate computation */
			c->matrix_model_projection_no_w_transform = 0;
			if (m[12] == 0.0 && m[13] == 0.0 && m[14] == 0.0)
				/*
				 if(c->matrix_model_projection.m[3][0] == 0.0 &&
					c->matrix_model_projection.m[3][1] == 0.0 &&
					c->matrix_model_projection.m[3][2] == 0.0)
				*/
				c->matrix_model_projection_no_w_transform = 1;
		}

		/* test if the texture matrix is not Identity */
		c->apply_texture_matrix = !gl_M4_IsId(c->matrix_stack_ptr[2]);

		c->matrix_model_projection_updated = 0;
	}
	/*  viewport- this is now updated on a glViewport call. 
	if (c->viewport.updated) {
		gl_eval_viewport(c);
		c->viewport.updated = 0;
	}
	 triangle drawing functions 
	*/
#if TGL_FEATURE_ALT_RENDERMODES == 1
	if (c->render_mode == GL_SELECT) {
		c->draw_triangle_front = gl_draw_triangle_select;
		c->draw_triangle_back = gl_draw_triangle_select;
	} else if (c->render_mode == GL_FEEDBACK) {
		c->draw_triangle_front = gl_draw_triangle_feedback;
		c->draw_triangle_back = gl_draw_triangle_feedback;
	} else
#endif
	{
		switch (c->polygon_mode_front) {
		case GL_POINT:
			c->draw_triangle_front = gl_draw_triangle_point;
			break;
		case GL_LINE:
			c->draw_triangle_front = gl_draw_triangle_line;
			break;
		default:
			c->draw_triangle_front = gl_draw_triangle_fill;
			break;
		}

		switch (c->polygon_mode_back) {
		case GL_POINT:
			c->draw_triangle_back = gl_draw_triangle_point;
			break;
		case GL_LINE:
			c->draw_triangle_back = gl_draw_triangle_line;
			break;
		default:
			c->draw_triangle_back = gl_draw_triangle_fill;
			break;
		}
	}
}

/* The per-vertex float arithmetic -- the projection and the map to the viewport -- is in
 * src/gl/tinygl/vertex_stage.c, which is the one unit besides ps2_builtins.c that the PS2 build
 * compiles with the FPU switched on.  It stays a call rather than a copy so that the laws below
 * have one home: `gl_clipcode()`, the viewport constants and the conversion prices are the same
 * ones this file used to hold.
 *
 * The normalise is the exception, and it is here on purpose: it is `sqrt(double)` from
 * include/gl/math.h, and a double is what the Emotion Engine has no instruction for at all --
 * compiling that line with the FPU on put 21 `.d` opcodes into the image.  `tgl_vertex_normalize()`
 * therefore stays in the -msoft-float unit and is reached with a pointer, which crosses no float
 * over the boundary and keeps the doubles priced as they are today (536 cycles, and never taken:
 * the XMB's census reads D=0 in every stage). */
int tgl_vertex_normalize(V3* n) {
	return gl_V3_Norm_Fast(n);
}

void glopVertex(GLParam* p) {
	GLVertex* v;
	GLint n, i, cnt;
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
	if (c->in_begin == 0)
#define ERROR_FLAG GL_INVALID_OPERATION
#include "error_check.h"
#else
	
#endif

		n = c->vertex_n;
	cnt = c->vertex_cnt;
	cnt++;
	c->vertex_cnt = cnt;
	tgl_stat_verts++;

	/* new vertex entry */
	v = &c->vertex[n];
	n++;

	v->coord.X = p[1].f;
	v->coord.Y = p[2].f;
	v->coord.Z = p[3].f;
	v->coord.W = p[4].f;

	tgl_vertex_transform(v);

	/* color */

	if (c->lighting_enabled) {
		gl_shade_vertex(v);
#include "error_check.h"
		
	} else {
		v->color = c->current_color;
	}
	/* tex coords */
#if TGL_OPTIMIZATION_HINT_BRANCH_COST < 1
	if (c->texture_2d_enabled)
#endif
	{
		if (c->apply_texture_matrix) {
			gl_M4_MulV4(&v->tex_coord, c->matrix_stack_ptr[2], &c->current_tex_coord);
		} else {
			v->tex_coord = c->current_tex_coord;
		}
	}
	/* precompute the mapping to the viewport */
#if TGL_OPTIMIZATION_HINT_BRANCH_COST < 2
	if (v->clip_code == 0)
#endif
	{
		tgl_vertex_to_viewport(v);
	}

	/* edge flag */
	v->edge_flag = c->current_edge_flag;

	switch (c->begin_type) {
	case GL_POINTS:
		gl_draw_point(&c->vertex[0]);
		n = 0;
		break;

	case GL_LINES:
		if (n == 2) {
			gl_draw_line(&c->vertex[0], &c->vertex[1]);
			n = 0;
		}
		break;
	case GL_LINE_STRIP:
#if TGL_FEATURE_GL_POLYGON == 1
	case GL_LINE_LOOP:
#endif
		switch (n) {
		case 1: {
			c->vertex[2] = c->vertex[0];
		} break;
		case 2: {
			gl_draw_line(&c->vertex[0], &c->vertex[1]);
			c->vertex[0] = c->vertex[1];
			n = 1;
		} break;
		default:
			break;
		};
		break;
	case GL_TRIANGLES:
		if (n == 3) {
			gl_draw_triangle(&c->vertex[0], &c->vertex[1], &c->vertex[2]);
			n = 0;
		}
		break;
	case GL_TRIANGLE_STRIP:
		if (cnt >= 3) {
			if (n == 3)
				n = 0;
			/* needed to respect triangle orientation */
			switch (cnt & 1) {
			case 0:
				gl_draw_triangle(&c->vertex[2], &c->vertex[1], &c->vertex[0]);
				break;
			default:
			case 1:
				gl_draw_triangle(&c->vertex[0], &c->vertex[1], &c->vertex[2]);
				break;
			}
		}
		break;
	case GL_TRIANGLE_FAN:
		if (n == 3) {
			gl_draw_triangle(&c->vertex[0], &c->vertex[1], &c->vertex[2]);
			c->vertex[1] = c->vertex[2];
			n = 2;
		}
		break;

	case GL_QUADS:
		if (n == 4) {
			c->vertex[2].edge_flag = 0;
			gl_draw_triangle(&c->vertex[0], &c->vertex[1], &c->vertex[2]);
			c->vertex[2].edge_flag = 1;
			c->vertex[0].edge_flag = 0;
			gl_draw_triangle(&c->vertex[0], &c->vertex[2], &c->vertex[3]);
			n = 0;
		}
		break;

	case GL_QUAD_STRIP:
		if (n == 4) {
			gl_draw_triangle(&c->vertex[0], &c->vertex[1], &c->vertex[2]);
			gl_draw_triangle(&c->vertex[1], &c->vertex[3], &c->vertex[2]);
			for (i = 0; i < 2; i++)
				c->vertex[i] = c->vertex[i + 2];
			n = 2;
		}
		break;

#if TGL_FEATURE_GL_POLYGON == 1
	case GL_POLYGON:
		break;
#endif
#if TGL_FEATURE_ERROR_CHECK == 1
	default:
		gl_fatal_error("glBegin: type %x not handled\n", c->begin_type);
#else
	default:
		break;
#endif
	}

	c->vertex_n = n;
}

void glopEnd(GLParam* param) {
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
	if (c->in_begin != 1)
#define ERROR_FLAG GL_INVALID_OPERATION
#include "error_check.h"
#else
	
	/* Assume it went alright.*/
#endif



#if TGL_FEATURE_GL_POLYGON == 1
		if (c->begin_type == GL_LINE_LOOP) {
			if (c->vertex_cnt >= 3) {
				gl_draw_line(&c->vertex[0], &c->vertex[2]);
			}
		} else if (c->begin_type == GL_POLYGON) {
			GLint i = c->vertex_cnt;
			while (i >= 3) {
				i--;
				gl_draw_triangle(&c->vertex[i], &c->vertex[0], &c->vertex[i - 1]);
			}
		}
#endif
	c->in_begin = 0;
}
