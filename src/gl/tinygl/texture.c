/*
 * Texture Manager
 */

#include "zgl.h"

#if defined(BTRON_PS2_TARGET)
#include "../../drivers/ps2/ps2_gs_tex.h"

/* Row 5b of doc/txt/PS2.txt: a plane of texels the application has just uploaded is given its GS
 * VRAM address, and its bytes cross the bus, here and not in the presentation path -- an
 * application uploads and draws in the same frame, so a drain at present would put the primitives
 * that sample the plane ahead of the transfer that fills it.
 *
 * The key is `im->pixmap`, which is what a draw call can ask about later (clip.c's GS adapter),
 * and the geometry is the pair TinyGL already keeps: the uploaded extent and the power-of-two
 * plane behind it (`gl_image_alloc()`), because the GS addresses the padded one.  The registry
 * needs no room and the VRAM no page for a plane it is shown twice: a re-upload re-plans nothing
 * and only re-crosses the bytes. */
static void gl_tex_plane_publish(const GLImage *im)
{
	if (im->pixmap == NULL) return;
	gs_tex_register(im->pixmap, (uint32_t)im->xsize, (uint32_t)im->ysize,
	                (uint32_t)im->xsize_pad, (uint32_t)im->ysize_pad);
}
#else
static void gl_tex_plane_publish(const GLImage *im) { (void)im; }
#endif

static GLTexture* find_texture(GLint h) {
	GLTexture* t;
	GLContext* c = gl_get_context();
	t = c->shared_state.texture_hash_table[h & TEXTURE_HASH_TABLE_MASK];
	while (t != NULL) {
		if (t->handle == h)
			return t;
		t = t->next;
	}
	return NULL;
}

GLboolean glAreTexturesResident(GLsizei n, const GLuint* textures, GLboolean* residences) {
#define RETVAL GL_FALSE
	GLboolean retval = GL_TRUE;
	GLint i;
#include "error_check_no_context.h"

	for (i = 0; i < n; i++)
		if (find_texture(textures[i])) {
			residences[i] = GL_TRUE;
		} else {
			residences[i] = GL_FALSE;
			retval = GL_FALSE;
		}
	return retval;
}
GLboolean glIsTexture(GLuint texture) {
	GLContext* c = gl_get_context();
#define RETVAL GL_FALSE
#include "error_check.h"
	if (find_texture(texture))
		return GL_TRUE;
	return GL_FALSE;
}

void* glGetTexturePixmap(GLint text, GLint level, GLint* xsize, GLint* ysize) {
	GLTexture* tex;
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
	if (!(text >= 0 && level < MAX_TEXTURE_LEVELS))
#define ERROR_FLAG GL_INVALID_ENUM
#define RETVAL NULL
#include "error_check.h"
#else
	/*assert(text >= 0 && level < MAX_TEXTURE_LEVELS);*/
#endif
		tex = find_texture(text);
	if (!tex)
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_INVALID_ENUM
#define RETVAL NULL
#include "error_check.h"
#else
		return NULL;
#endif
		*xsize = tex->images[level].xsize;
	*ysize = tex->images[level].ysize;
	return tex->images[level].pixmap;
}

/* Every plane an image owns: the uploaded extent, the padded allocation behind it,
 * and the sampler/mapper fields that go with them. */
static void gl_image_clear(GLImage* im) {
	gl_free(im->pixmap);
	im->pixmap = NULL;
	im->xsize = im->ysize = 0;
	im->smask = im->tmask = 0;
	im->tshift = 0;
	im->sscale = im->soff = 0.0f;
	im->tscale = im->toff = 0.0f;
}

static void free_texture(GLContext* c, GLint h) {
	GLTexture *t, **ht;
	GLint level;

	t = find_texture(h);
	if (t->prev == NULL) {
		ht = &c->shared_state.texture_hash_table[t->handle & TEXTURE_HASH_TABLE_MASK];
		*ht = t->next;
	} else {
		t->prev->next = t->next;
	}
	if (t->next != NULL)
		t->next->prev = t->prev;

	for (level = 0; level < MAX_TEXTURE_LEVELS; level++)
		gl_image_clear(&t->images[level]);

	gl_free(t);
}

/*
 * Give an image the extent (width,height) and a plane to hold it.
 *
 * The uploaded extent is what a texture coordinate maps onto and what the
 * application's texels are laid out in; it is stored as given, because resampling an
 * upload to a fixed square is what turned a 384x384 atlas into unreadable icons.  The
 * allocation is that extent rounded up to a power of two on each axis -- the sampler
 * wraps by masking, so the extent it can hide behind has to be a power of two -- and
 * the padding is zero, i.e. transparent black.  Returns 0 if the extent is too large
 * for a plane or the plane could not be had.
 */
static GLint gl_image_alloc(GLImage* im, GLint width, GLint height) {
	GLint xpad = 1, ypad = 1, xpow = 0, ypow = 0;

	if (width <= 0 || height <= 0 || width > TGL_FEATURE_TEXTURE_MAX_DIM || height > TGL_FEATURE_TEXTURE_MAX_DIM) {
		gl_image_clear(im);
		return 0;
	}
	while (xpad < width) {
		xpad <<= 1;
		xpow++;
	}
	while (ypad < height) {
		ypad <<= 1;
		ypow++;
	}

	if (im->pixmap != NULL && im->xsize_pad == xpad && im->ysize_pad == ypad) {
		/* Same plane, new contents: clearing is all the re-upload needs. */
		memset(im->pixmap, 0, (size_t)xpad * ypad * PSZB);
	} else {
		gl_free(im->pixmap);
		im->pixmap = gl_zalloc((GLint)((size_t)xpad * ypad * PSZB));
		if (im->pixmap == NULL) {
			im->xsize = im->ysize = im->xsize_pad = im->ysize_pad = 0;
			return 0;
		}
		im->xsize_pad = xpad;
		im->ysize_pad = ypad;
	}

	im->xsize = width;
	im->ysize = height;
	im->smask = (GLuint)(xpad - 1) << (ZB_POINT_S_FRAC_BITS + 1);
	im->tmask = (GLuint)(ypad - 1) << (ZB_POINT_T_FRAC_BITS + 1);
	im->tshift = ZB_TEX_T_SHIFT_BASE - xpow;
	/* One texel index is (ZB_POINT_*_FRAC_BITS + 1) bits of s/t, and a coordinate of 0
	 * sits half a texel in, so that a coordinate samples the centre of the texel it
	 * names rather than its edge. */
	im->soff = (GLfloat)(1 << ZB_POINT_S_FRAC_BITS);
	im->sscale = (GLfloat)(((GLint)width << (ZB_POINT_S_FRAC_BITS + 1)) - (1 << ZB_POINT_S_FRAC_BITS));
	im->toff = (GLfloat)(1 << ZB_POINT_T_FRAC_BITS);
	im->tscale = (GLfloat)(((GLint)height << (ZB_POINT_T_FRAC_BITS + 1)) - (1 << ZB_POINT_T_FRAC_BITS));
	return 1;
}

/* Convert an upload of `nch`-channel texels into the image's plane, row by row. */
static void gl_image_store(GLImage* im, const GLubyte* pixels, GLint nch) {
	GLint y;

	for (y = 0; y < im->ysize; y++)
		gl_convert_row_to_8A8R8G8B(im->pixmap + (GLint)((size_t)y * im->xsize_pad), im->xsize_pad,
								  pixels + (GLint)((size_t)y * im->xsize * nch), im->xsize, nch);

	gl_tex_plane_publish(im);
}

/* Hand the rasterizer everything it needs to sample one image: its plane and the three
 * fields that turn a texture coordinate into a byte offset in it.  Called once per
 * primitive, so a span loop never has to find out where a texel lives. */
void ZB_setTexture(ZBuffer* zb, const GLImage* im) {
	zb->current_texture = im->pixmap;
	zb->current_smask = im->smask;
	zb->current_tmask = im->tmask;
	zb->current_tshift = im->tshift;
}

GLTexture* alloc_texture(GLint h) {
	GLContext* c = gl_get_context();
	GLTexture *t, **ht;
#define RETVAL NULL
#include "error_check.h"
	t = gl_zalloc(sizeof(GLTexture));
	if (!t)
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_OUT_OF_MEMORY
#define RETVAL NULL
#include "error_check.h"
#else
		gl_fatal_error("GL_OUT_OF_MEMORY");
#endif

		ht = &c->shared_state.texture_hash_table[h & TEXTURE_HASH_TABLE_MASK];

	t->next = *ht;
	t->prev = NULL;
	if (t->next != NULL)
		t->next->prev = t;
	*ht = t;

	t->handle = h;

	return t;
}

void glInitTextures() {
	/* textures */
	GLContext* c = gl_get_context();
	c->texture_2d_enabled = 0;
	c->current_texture = find_texture(0);
}

void glGenTextures(GLint n, GLuint* textures) {
	GLContext* c = gl_get_context();
	GLint max, i;
	GLTexture* t;
#include "error_check.h"
	max = 0;
	for (i = 0; i < TEXTURE_HASH_TABLE_SIZE; i++) {
		t = c->shared_state.texture_hash_table[i];
		while (t != NULL) {
			if (t->handle > max)
				max = t->handle;
			t = t->next;
		}
	}
	for (i = 0; i < n; i++) {
		textures[i] = max + i + 1; /* MARK: How texture handles are created.*/
	}
}

void glDeleteTextures(GLint n, const GLuint* textures) {
	GLint i;
	GLTexture* t;
	GLContext* c = gl_get_context();
#include "error_check.h"
	for (i = 0; i < n; i++) {
		t = find_texture(textures[i]);
		if (t != NULL && t != 0) {
			if (t == c->current_texture) {
				glBindTexture(GL_TEXTURE_2D, 0);
#include "error_check.h"
			}
			free_texture(c, textures[i]);
		}
	}
}

void glopBindTexture(GLParam* p) {
	GLint target = p[1].i;
	GLint texture = p[2].i;
	GLTexture* t;
	GLContext* c = gl_get_context();
#if TGL_FEATURE_ERROR_CHECK == 1
	if (!(target == GL_TEXTURE_2D && target > 0))
#define ERROR_FLAG GL_INVALID_ENUM
#include "error_check.h"
#else
	
#endif
		t = find_texture(texture);
	if (t == NULL) {
		t = alloc_texture(texture);
#include "error_check.h"
	}
	if (t == NULL) { 
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_OUT_OF_MEMORY
#include "error_check.h"
#else
		gl_fatal_error("GL_OUT_OF_MEMORY");
#endif
	}
	c->current_texture = t;
}


void glCopyTexImage2D(GLenum target,		 
					  GLint level,			 
					  GLenum internalformat, 
					  GLint x,				 
					  GLint y,				 
					  GLsizei width,		 
					  GLsizei height, GLint border) {
	GLParam p[9];
#include "error_check_no_context.h"

	p[0].op = OP_CopyTexImage2D;
	p[1].i = target;
	p[2].i = level;
	p[3].i = internalformat;
	p[4].i = x;
	p[5].i = y;
	p[6].i = width;
	p[7].i = height;
	p[8].i = border;
	gl_add_op(p);
}
void glopCopyTexImage2D(GLParam* p) {
	GLImage* im;
	PIXEL* data;
	GLint i, j;
	GLint target = p[1].i;
	GLint level = p[2].i;
	GLint x = p[4].i;
	GLint y = p[5].i;
	GLsizei w = p[6].i;
	GLsizei h = p[7].i;
	GLint border = p[8].i;
	GLContext* c = gl_get_context();
	y -= h;

	if (c->readbuffer != GL_FRONT || c->current_texture == NULL || target != GL_TEXTURE_2D || border != 0) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_INVALID_OPERATION
#include "error_check.h"
#else
		return;
#endif
	}
	im = &c->current_texture->images[level];
	/* The copy keeps the requested extent too; the framebuffer is the source, so the
	 * texels are already PIXELs and only the row stride differs. */
	if (!gl_image_alloc(im, w, h)) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_INVALID_VALUE
#include "error_check.h"
#else
		return;
#endif
	}
	data = im->pixmap;
	/* TODO implement the scaling and stuff that the GL spec says it should have.*/
#if TGL_FEATURE_MULTITHREADED_COPY_TEXIMAGE_2D == 1
#ifdef _OPENMP
#pragma omp parallel for
#endif
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++) {
			data[i + j * im->xsize_pad] = c->zb->pbuf[((i + x) % (c->zb->xsize)) + ((j + y) % (c->zb->ysize)) * (c->zb->xsize)];
		}
#else
	for (j = 0; j < h; j++)
		for (i = 0; i < w; i++) {
			data[i + j * im->xsize_pad] = c->zb->pbuf[((i + x) % (c->zb->xsize)) + ((j + y) % (c->zb->ysize)) * (c->zb->xsize)];
		}
#endif
	gl_tex_plane_publish(im);
}

/*
 * The two upload operations share one rule: what the application handed over is what
 * is stored.  `format` decides the source's stride (3 or 4 bytes) and therefore which
 * channels the texel carries; `internalformat` does not, and a 1/2/3/4 there is the
 * application asking for a reduced store, which this implementation does not do --
 * the texels keep every channel the source supplied.
 */
static GLint gl_tex_format_channels(GLint format) {
	if (format == GL_RGBA)
		return 4;
	if (format == GL_RGB)
		return 3;
	return 0;
}

void glopTexImage1D(GLParam* p) {
	GLint target = p[1].i;
	GLint level = p[2].i;
	GLint width = p[4].i;
	GLint height = 1;
	GLint border = p[5].i;
	GLint format = p[6].i;
	GLint type = p[7].i;
	void* pixels = p[8].p;
	GLImage* im;
	const GLint nch = gl_tex_format_channels(format);
	GLContext* c = gl_get_context();

	if (!(c->current_texture != NULL && target == GL_TEXTURE_1D && level == 0 && border == 0 && nch != 0 &&
		  type == GL_UNSIGNED_BYTE)) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_INVALID_ENUM
#include "error_check.h"
#else
		/* Loud on purpose: with the error flags compiled out, a silently dropped upload
		 * is indistinguishable from a texture that simply draws wrong. */
		gl_fatal_error("glTexImage1D: combination of parameters not handled!!");
#endif
	}

	im = &c->current_texture->images[level];
	if (!gl_image_alloc(im, width, height)) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_OUT_OF_MEMORY
#include "error_check.h"
#else
		gl_fatal_error("GL_OUT_OF_MEMORY");
#endif
	}
	gl_image_store(im, (const GLubyte*)pixels, nch);
}
void glopTexImage2D(GLParam* p) {
	GLint target = p[1].i;
	GLint level = p[2].i;
	GLint width = p[4].i;
	GLint height = p[5].i;
	GLint border = p[6].i;
	GLint format = p[7].i;
	GLint type = p[8].i;
	void* pixels = p[9].p;
	GLImage* im;
	const GLint nch = gl_tex_format_channels(format);
	GLContext* c = gl_get_context();

	if (!(c->current_texture != NULL && target == GL_TEXTURE_2D && level == 0 && border == 0 && nch != 0 &&
		  type == GL_UNSIGNED_BYTE)) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_INVALID_ENUM
#include "error_check.h"
#else
		gl_fatal_error("glTexImage2D: combination of parameters not handled!!");
#endif
	}

	im = &c->current_texture->images[level];
	if (!gl_image_alloc(im, width, height)) {
#if TGL_FEATURE_ERROR_CHECK == 1
#define ERROR_FLAG GL_OUT_OF_MEMORY
#include "error_check.h"
#else
		gl_fatal_error("GL_OUT_OF_MEMORY");
#endif
	}
	gl_image_store(im, (const GLubyte*)pixels, nch);
}

/* TODO: not all tests are done */
/*
void glopTexEnv(GLContext* c, GLParam* p) {
	GLint target = p[1].i;
	GLint pname = p[2].i;
	GLint param = p[3].i;

	if (target != GL_TEXTURE_ENV) {

	error:
#if TGL_FEATURE_ERROR_CHECK == 1

#define ERROR_FLAG GL_INVALID_ENUM
#include "error_check.h"
#else
		gl_fatal_error("glTexParameter: unsupported option");
#endif

	}

	if (pname != GL_TEXTURE_ENV_MODE)
		goto error;

	if (param != GL_DECAL)
		goto error;
}
*/
/* TODO: not all tests are done */
/*
void glopTexParameter(GLContext* c, GLParam* p) {
	GLint target = p[1].i;
	GLint pname = p[2].i;
	GLint param = p[3].i;

	if (target != GL_TEXTURE_2D &&
		target != GL_TEXTURE_1D) {
	error:
		tgl_warning("glTexParameter: unsupported option");
		return;
	}

	switch (pname) {
	case GL_TEXTURE_WRAP_S:
	case GL_TEXTURE_WRAP_T:
		if (param != GL_REPEAT)
			goto error;
		break;
	}
}
*/

/*
void glopPixelStore(GLContext* c, GLParam* p) {
	GLint pname = p[1].i;
	GLint param = p[2].i;

	if (pname != GL_UNPACK_ALIGNMENT || param != 1) {
		gl_fatal_error("glPixelStore: unsupported option");
	}
}
*/
