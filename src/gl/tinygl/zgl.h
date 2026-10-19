#ifndef _tgl_zgl_h_
#define _tgl_zgl_h_
#ifndef NDEBUG
#define NDEBUG
#endif
#include <GL/gl.h>
#include "zbuffer.h"
#include "zfeatures.h"
#include "zmath.h"
#include <math.h>
#include <stdlib.h>

#include <string.h>
#ifndef M_PI
#define M_PI 3.1415926535897932384626433832795
#endif


enum {

#define ADD_OP(a, b, c) OP_##a,

#include "opinfo.h"

};


#if TGL_FEATURE_GL_POLYGON == 1

#define POLYGON_MAX_VERTEX 16
#else

#define POLYGON_MAX_VERTEX 4
#endif
/* Max # of specular light pow buffers */
#define MAX_SPECULAR_BUFFERS 32

/* # of entries in specular buffer */
#define SPECULAR_BUFFER_SIZE 512
/* specular buffer granularity */


#define MAX_MODELVIEW_STACK_DEPTH 32
#define MAX_PROJECTION_STACK_DEPTH 8
#define MAX_TEXTURE_STACK_DEPTH 8
#define MAX_NAME_STACK_DEPTH 16
#define MAX_TEXTURE_LEVELS 1
#define MAX_LIGHTS 16

#define VERTEX_ARRAY 0x0001
#define COLOR_ARRAY 0x0002
#define NORMAL_ARRAY 0x0004
#define TEXCOORD_ARRAY 0x0008

#define MAX_DISPLAY_LISTS 16384
#define OP_BUFFER_MAX_SIZE 4096

#define TGL_OFFSET_FILL 0x1
#define TGL_OFFSET_LINE 0x2
#define TGL_OFFSET_POINT 0x4

typedef struct GLSpecBuf {
	GLint shininess_i;
	GLint last_used;
	GLfloat buf[SPECULAR_BUFFER_SIZE + 1];
	struct GLSpecBuf* next;
} GLSpecBuf;

typedef struct GLLight {
	V4 ambient;
	V4 diffuse;
	V4 specular;
	V4 position;
	V3 spot_direction;
	V3 norm_spot_direction;
	V3 norm_position;
	GLfloat spot_exponent;
	GLfloat spot_cutoff;
	GLfloat attenuation[3];
	/* precomputed values */
	GLfloat cos_spot_cutoff;

	/* we use a linked list to know which are the enabled lights */
	
	struct GLLight *next, *prev;
	GLubyte enabled;
} GLLight;

typedef struct GLMaterial {
	V4 emission;
	V4 ambient;
	V4 diffuse;
	V4 specular;
	GLfloat shininess;

	/* computed values */
	GLint shininess_i;
	GLint do_specular;
} GLMaterial;

typedef struct GLViewport {
	V3 scale;
	V3 trans;
	GLint xmin, ymin, xsize, ysize;
	
} GLViewport;

typedef union {
	GLint op;
	GLfloat f;
	GLint i;
	GLuint ui;
	void* p;
} GLParam;

typedef struct GLParamBuffer {
	GLParam ops[OP_BUFFER_MAX_SIZE];
	struct GLParamBuffer* next;
} GLParamBuffer;

typedef struct GLList {
	GLParamBuffer* first_op_buffer;
	/* TODO: extensions for an hash table or a better allocating scheme */
} GLList;

typedef struct GLVertex {

	V3 normal;
	V4 coord;
	V4 tex_coord;
	V4 color;

	/* computed values */
	V4 ec;			 /* eye coordinates */
	V4 pc;			 /* coordinates in the normalized volume */
	ZBufferPoint zp; /* GLinteger coordinates for the rasterization */
	GLint clip_code; /* clip code */
	GLint edge_flag;
} GLVertex;

typedef struct GLImage {
	/* The plane is allocated at upload, at the extent that was uploaded rounded up to a
	 * power of two on each axis (the sampler wraps by masking, so the extent it hides
	 * behind has to be a power of two). xsize/ysize stay the uploaded extent: they are
	 * what a texture coordinate maps onto, and they are what the padding hides. */
	PIXEL* pixmap;
	GLint xsize, ysize;		/* the uploaded extent: what a coordinate maps onto */
	GLint xsize_pad, ysize_pad; /* the allocated plane: each axis rounded up to a power of two */
	GLuint smask, tmask;	/* sampler: texel index fields, see TEXTURE_SAMPLE */
	GLint tshift;			/* sampler: row stride, as a shift */
	GLfloat sscale, soff;	/* mapper: texture coordinate -> fixed point texel index */
	GLfloat tscale, toff;
} GLImage;

/* textures */

#define TEXTURE_HASH_TABLE_SIZE 256
#define TEXTURE_HASH_TABLE_MASK 255
typedef struct GLTexture {
	GLImage images[MAX_TEXTURE_LEVELS];
	struct GLTexture *next, *prev;
	GLint handle;
} GLTexture;

/* buffers */
#define MAX_BUFFERS 2048
typedef struct GLBuffer {
	void* data;
	GLuint size;
} GLBuffer;

/* shared state */
typedef struct GLSharedState {
	GLList** lists;
	GLTexture** texture_hash_table;
	GLBuffer** buffers;
} GLSharedState;

struct GLContext;

typedef void (*gl_draw_triangle_func)(GLVertex* p0, GLVertex* p1, GLVertex* p2);

/* display context */

typedef struct GLContext {
	/* lights */
	GLLight lights[MAX_LIGHTS];
	/* viewport */
	GLViewport viewport;
	GLMaterial materials[2];
	GLVertex vertex[POLYGON_MAX_VERTEX];

	M4 matrix_model_view_inv;
	M4 matrix_model_projection;
	V4 ambient_light_model;
	V4 clear_color;
	V4 current_color;
	V4 current_normal;
	V4 current_tex_coord;
	V4 rasterpos;

	/*Pointers.*/
	/* shared state */
	GLSharedState shared_state;
	ZBuffer* zb;
	GLLight* first_light;
	GLTexture* current_texture;
	GLParamBuffer* current_op_buffer;
	M4* matrix_stack[3];
	M4* matrix_stack_ptr[3];
	gl_draw_triangle_func draw_triangle_front, draw_triangle_back;
	/* resize viewport function */
	GLint (*gl_resize_viewport)(GLint* xsize, GLint* ysize);
	GLfloat* texcoord_array;
	GLfloat* vertex_array;
	GLfloat* normal_array;
	GLfloat* color_array;

#if TGL_FEATURE_ALT_RENDERMODES == 1
	GLfloat* feedback_buffer;
	GLuint* select_buffer;
	GLuint *select_ptr, *select_hit;
	GLfloat* feedback_ptr;
#endif

	GLint local_light_model;
	GLint lighting_enabled;
	GLint light_model_two_side;

	/* materials */
	GLint color_material_enabled;
	GLint current_color_material_mode;
	GLint current_color_material_type;

	/* textures */

	GLint texture_2d_enabled;

	/* current list */

	GLint current_op_buffer_index;
	GLint exec_flag, compile_flag, print_flag;
	GLuint listbase;
	/* matrix */

	GLint matrix_mode;

	GLint matrix_stack_depth_max[3];

	GLint matrix_model_projection_updated;
	GLint matrix_model_projection_no_w_transform;
	GLint apply_texture_matrix;

	/* current state */
	GLint polygon_mode_back;
	GLint polygon_mode_front;

	GLint current_front_face;
	GLint current_shade_model;
	GLint current_cull_face;
	GLint cull_face_enabled;
	GLint normalize_enabled;

	/* selection */
#if TGL_FEATURE_ALT_RENDERMODES == 1
	GLint render_mode;

	GLint select_size;

	GLint select_overflow;
	GLint select_hits;
#endif
	/* glDrawBuffer, glRenderBuffer */
	GLenum drawbuffer;
	GLenum readbuffer;
	/* feedback */
	
#if TGL_FEATURE_ALT_RENDERMODES == 1

	GLuint feedback_size;
	GLint feedback_hits;
	GLubyte feedback_overflow;
	GLenum feedback_type;
	/* names */
	GLuint name_stack[MAX_NAME_STACK_DEPTH];
	GLint name_stack_size;
#endif

	/* clear */
	GLfloat clear_depth;

	GLint current_edge_flag;

	/* glBegin / glEnd */
	GLint in_begin;
	GLint begin_type;
	GLint vertex_n, vertex_cnt;

	/* opengl 1.1 arrays  */

	GLint vertex_array_size;
	GLint vertex_array_stride;
	GLint normal_array_stride;
	GLint color_array_size;
	GLint color_array_stride;

	GLint texcoord_array_size;
	GLint texcoord_array_stride;
	GLint client_states;

	/* opengl 1.1 polygon offset */
	GLfloat offset_factor;
	GLfloat offset_units;
	GLint offset_states;

	/* opengl blending */
	

	/* specular buffer. could probably be shared between contexts,
	  but that wouldn't be 100% thread safe */
#if TGL_FEATURE_SPECULAR_BUFFERS == 1
	GLSpecBuf* specbuf_first;
	GLint specbuf_used_counter;
	GLint specbuf_num_buffers;
#endif
	GLint zEnableSpecular; 

	/* raster position */
	GLint rasterpos_zz;
	GLfloat pzoomx, pzoomy;
	GLVertex rastervertex;
	/* text */
	GLTEXTSIZE textsize;
	/* buffers */
	GLint boundarraybuffer;
	GLint boundvertexbuffer;
	GLint boundnormalbuffer;
	GLint boundcolorbuffer;
	GLint boundtexcoordbuffer;
	GLubyte rasterposvalid;
#if TGL_FEATURE_ERROR_CHECK == 1
	GLenum error_flag;
#endif
} GLContext;

extern GLContext gl_ctx;
extern GLContext* gl_current_context;
static GLContext* gl_get_context(void) { return gl_current_context ? gl_current_context : &gl_ctx; }
void gl_init_context(GLContext *c, void *zbuffer1);
void gl_close_context(GLContext *c);

/* Vertices that have been through gl_Vertex, counted where a vertex is made rather than
 * where one is drawn: a triangle strip shares them, so this is the quantity the geometry
 * pipeline is billed by and the triangle count is not.  Defined in clip.c, which owns the
 * window these counts are read through, so that a single reset clears every counter in it. */
extern unsigned tgl_stat_verts;

extern void (*op_table_func[])(GLParam*);
extern GLint op_table_size[];
extern void gl_compile_op(GLParam* p);
static void gl_add_op(GLParam* p) {
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
#include "error_check.h"
#endif
	GLint op;
	op = p[0].op;
	if (c->exec_flag) {
		op_table_func[op](p);
#if TGL_FEATURE_ERROR_CHECK == 1
#include "error_check.h"
#endif
	}
	if (c->compile_flag) {
		gl_compile_op(p);
#if TGL_FEATURE_ERROR_CHECK == 1
#include "error_check.h"
#endif
	}
}

/* select.c */
void gl_add_select(GLuint zmin, GLuint zmax);
void gl_add_feedback(GLfloat token, GLVertex* v1, GLVertex* v2, GLVertex* v3, GLfloat passthrough_token_value);

/* clip.c */

#define CLIP_EPSILON (1E-5)

/* The plane tests below are six float compares, and on one build of this port that is a
 * question about the instruction set rather than a formality.
 *
 * The PS2's geometry kernel (src/gl/tinygl/vertex_stage.c) is compiled `-mhard-float` so its
 * matrix and viewport arithmetic runs on the Emotion Engine's COP1, and because `gl_clipcode()`
 * is a header inline it is compiled *in that unit's law* for the vertices that pass through it.
 * Under that law clang lowered `a > b` as `b < a` in the unordered-inclusive form, `c.ule.s`, and
 * this CPU does not implement that encoding: like `c.un.s` and `trunc.w.s` before it, an
 * unrecognised COP1 compare does not raise, it leaves the condition code holding what the
 * previous compare wrote.  Measured on the XMB: three of a vertex's six tests answered with a
 * stale flag, so every face came out of the transform with a nonzero clip code, `gl_draw_triangle`
 * judged all 5,406 of them wholly outside the volume, and nothing was drawn -- a black screen
 * beside a `pc`/`zp` dump whose every number is correct.  The three tests written as `c.olt.s`
 * were right in the same vertex, which is what localized the defect to the encoding rather than
 * to the arithmetic or the volume.
 *
 * So the compare goes through the one form this unit answers, hand-written once in
 * `ps2_fp.h` next to the truncate the same build already reaches for.  Every other translation
 * unit of the port is `-msoft-float`, keeps the plain `<`, and pays a `__ltsf2` call that
 * dispatches to the same instruction; only the FPU-enabled build of this header sees the asm. */
#if defined(BTRON_PS2_TARGET) && defined(BTRON_PS2_FP_HW)
#include "../../drivers/ps2/ps2_fp.h" /* ps2_fp_olt_s(): c.olt.s, the encoding the EE answers */
#define TGL_CLIP_LT(a, b) (ps2_fp_olt_s(ps2_fp_bits_f(a), ps2_fp_bits_f(b)))
#else
#define TGL_CLIP_LT(a, b) ((a) < (b))
#endif

/* The guard band is rounded to the operand's own precision on purpose.  Left as
 * `w1 * (1.0 + CLIP_EPSILON)` the product is a double -- the literal is one, and C
 * promotes the float operand to meet it -- so on a target whose floats are really floats
 * every vertex pays an extend, a double multiply and a truncation for a quantity that is
 * then compared against floats anyway.  That round trip is what the PS2 XMB's census
 * measured as 3.19 double calls per vertex (one multiply and its two conversions, at
 * 2,108 and 4,216 per frame), and a double software op costs the image about twenty times
 * a single one.
 *
 * What changes is the margin, not the test: 1.00001 is not exactly representable in
 * binary, so the nearest float moves the six planes by well under one part in 10^7.  A
 * clip decision can flip only for a vertex that sits within that of a plane, and the
 * clip code is a region hint that already carries a 1E-5 band by design.  On the targets
 * where GLfloat is itself double the cast is a no-op and nothing moves at all. */
static GLint gl_clipcode(GLfloat x, GLfloat y, GLfloat z, GLfloat w1) {
	GLfloat w, nw;

	w = w1 * (GLfloat)(1.0 + CLIP_EPSILON);
	nw = -w;
	return TGL_CLIP_LT(x, nw) | (TGL_CLIP_LT(w, x) << 1) | (TGL_CLIP_LT(y, nw) << 2) | (TGL_CLIP_LT(w, y) << 3) | (TGL_CLIP_LT(z, nw) << 4) | (TGL_CLIP_LT(w, z) << 5);
}

#define CLIP_XMIN (1 << 0)
#define CLIP_XMAX (1 << 1)
#define CLIP_YMIN (1 << 2)
#define CLIP_YMAX (1 << 3)
#define CLIP_ZMIN (1 << 4)
#define CLIP_ZMAX (1 << 5)

static GLfloat clampf(GLfloat a, GLfloat min, GLfloat max) {
	if (a < min)
		return min;
	else if (a > max)
		return max;
	else
		return a;
}




/* triangle */

/*
 * Clipping
 */

/* We clip the segment [a,b] against the 6 planes of the normal volume.
 * We compute the point 'c' of GLintersection and the value of the parameter 't'
 * of the GLintersection if x=a+t(b-a).
 */



void gl_draw_triangle(GLVertex* p0, GLVertex* p1, GLVertex* p2);
void gl_draw_line(GLVertex* p0, GLVertex* p1);
void gl_draw_point(GLVertex* p0);

void gl_draw_triangle_point(GLVertex* p0, GLVertex* p1, GLVertex* p2);	
void gl_draw_triangle_line(GLVertex* p0, GLVertex* p1, GLVertex* p2);	
void gl_draw_triangle_fill(GLVertex* p0, GLVertex* p1, GLVertex* p2);	
void gl_draw_triangle_select(GLVertex* p0, GLVertex* p1, GLVertex* p2); 
void gl_draw_triangle_feedback(GLVertex* p0, GLVertex* p1, GLVertex* p2);

/* vertex_stage.c -- the per-vertex float arithmetic, in the one tinygl unit that the PS2 build
 * compiles with the FPU switched on (row 7 of doc/txt/PS2.txt).  Both take a pointer and return
 * nothing, which is what makes that legal: `-msoft-float` is an argument-passing law, and no
 * floating value crosses this boundary in either direction. */
void tgl_vertex_transform(GLVertex* v);
void tgl_vertex_to_viewport(GLVertex* v);

/* vertex.c -- the normalise the lighting branch asks for, kept in the -msoft-float unit because
 * it is sqrt(double) and the Emotion Engine has no double-precision instruction.  A V3* in, an
 * int out, so it is callable from the unit above. */
int tgl_vertex_normalize(V3* n);
/* matrix.c */
void gl_print_matrix(const GLfloat* m);
/*
void glopLoadIdentity(GLParam *p);
void glopTranslate(GLParam *p);*/

/* light.c */
void gl_enable_disable_light(GLint light, GLint v);
void gl_shade_vertex(GLVertex* v);

void glInitTextures();
void glEndTextures();
GLTexture* alloc_texture(GLint h);
/* Unlink one texture, free every plane it holds, free the node.  The one path a GLTexture
 * takes out of the world, shared by glDeleteTextures() and by a context close that finds
 * textures the application never deleted -- see texture.c for why freeing the node alone
 * leaves the largest half of it behind. */
void gl_texture_node_destroy(GLContext* c, GLTexture* t);

/* image_util.c */
/* Upload one row of `xsize` texels of an `nch`-channel (3=RGB, 4=RGBA) source into a
 * plane whose row stride is `rowstride` texels -- the plane is padded to a power of
 * two, the source is not. */
void gl_convert_row_to_8A8R8G8B(GLuint* pixmap, GLint rowstride, const GLubyte* src, GLint xsize, GLint nch);
void gl_resizeImage(GLubyte* dest, GLint xsize_dest, GLint ysize_dest, GLubyte* src, GLint xsize_src, GLint ysize_src);
void gl_resizeImageNoInterpolate(GLubyte* dest, GLint xsize_dest, GLint ysize_dest, GLubyte* src, GLint xsize_src, GLint ysize_src);



void gl_fatal_error(char* format, ...);

/* specular buffer "api" */
GLSpecBuf* specbuf_get_buffer(const GLint shininess_i, const GLfloat shininess);



/* glopXXX functions */

#define ADD_OP(a, b, c) void glop##a(GLParam*);
#include "opinfo.h"

/* this clip epsilon is needed to avoid some rounding errors after
   several clipping stages */

static void gl_eval_viewport() {
	GLContext* c = gl_get_context();
	GLViewport* v;
	GLfloat zsize = (1 << (ZB_Z_BITS + ZB_POINT_Z_FRAC_BITS));

	v = &c->viewport;

	v->trans.X = ((v->xsize - 0.5) / 2.0) + v->xmin;
	v->trans.Y = ((v->ysize - 0.5) / 2.0) + v->ymin;
	v->trans.Z = ((zsize - 0.5) / 2.0) + ((1 << ZB_POINT_Z_FRAC_BITS)) / 2;

	v->scale.X = (v->xsize - 0.5) / 2.0;
	v->scale.Y = -(v->ysize - 0.5) / 2.0;
	v->scale.Z = -((zsize - 0.5) / 2.0);
}

#endif /* _tgl_zgl_h_ */
