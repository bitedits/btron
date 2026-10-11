#include "zgl.h"

/*
 * image conversion
 */

/*
 * One row of an upload, converted into a plane the caller advances by `rowstride`.
 * `nch` is the source's channel count (3 for GL_RGB, 4 for GL_RGBA), and it is the
 * whole reason this exists: reading a 4-byte texel as a 3-byte one walks the source
 * one byte further off every texel, which is what the corruption on a real display
 * looked like.  A 32-bit PIXEL carries the texel's alpha in its top byte, so an RGBA
 * source's alpha goes where the blender can find it; 5R6G5B has no alpha channel and
 * drops it.
 *
 * This loop, not the blender, is where a texel's byte order is decided: the source is a
 * byte array (R first) and a PIXEL is a word, so RGB_TO_PIXEL is called with each channel
 * left-aligned at bit 16 the way it expects.  Doing it through the macro rather than with
 * written-out shifts is what makes the PS2's canvas-order domain need exactly one swap per
 * texel here and none anywhere in a span loop.  The alpha byte is at bit 24 in both orders.
 */
void gl_convert_row_to_8A8R8G8B(GLuint* pixmap, GLint rowstride, const GLubyte* src, GLint xsize, GLint nch) {
	GLint i;
	const GLubyte* p = src;
	(void)rowstride; /* the caller advances by it between rows */

#if TGL_FEATURE_RENDER_BITS == 32
	if (nch == 4) {
		for (i = 0; i < xsize; i++) {
			pixmap[i] = ((GLuint)p[3] << 24) | RGB_TO_PIXEL(((GLuint)p[0] << 16), ((GLuint)p[1] << 16), ((GLuint)p[2] << 16));
			p += 4;
		}
		return;
	}
	for (i = 0; i < xsize; i++) {
		pixmap[i] = RGB_TO_PIXEL(((GLuint)p[0] << 16), ((GLuint)p[1] << 16), ((GLuint)p[2] << 16));
		p += 3;
	}
#else
	for (i = 0; i < xsize; i++) {
		pixmap[i] = ((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | ((p[2] & 0xF8) >> 3);
		p += nch;
	}
#endif
}

/*
 * linear GLinterpolation with xf,yf normalized to 2^16
 */

#define INTERP_NORM_BITS 16
#define INTERP_NORM (1 << INTERP_NORM_BITS)

static GLint GLinterpolate_imutil(GLint v00, GLint v01, GLint v10, GLint xf, GLint yf) {
	return v00 + (((v01 - v00) * xf + (v10 - v00) * yf) >> INTERP_NORM_BITS);
}

/*
 * TODO: more accurate resampling
 */

void gl_resizeImage(GLubyte* dest, GLint xsize_dest, GLint ysize_dest, GLubyte* src, GLint xsize_src, GLint ysize_src) {
	GLubyte *pix, *pix_src;
	GLfloat x1, y1, x1inc, y1inc;
	GLint xi, yi, j, xf, yf, x, y;

	pix = dest;
	pix_src = src;
	
	x1inc = (GLfloat)(xsize_src - 1) / (GLfloat)(xsize_dest - 1);
	y1inc = (GLfloat)(ysize_src - 1) / (GLfloat)(ysize_dest - 1);

	y1 = 0;
	for (y = 0; y < ysize_dest; y++) {
		x1 = 0;
		for (x = 0; x < xsize_dest; x++) {
			xi = (GLint)x1;
			yi = (GLint)y1;
			xf = (GLint)((x1 - floor(x1)) * INTERP_NORM);
			yf = (GLint)((y1 - floor(y1)) * INTERP_NORM);

			if ((xf + yf) <= INTERP_NORM) {
				for (j = 0; j < 3; j++) {
					pix[j] = GLinterpolate_imutil(pix_src[(yi * xsize_src + xi) * 3 + j], pix_src[(yi * xsize_src + xi + 1) * 3 + j],
												  pix_src[((yi + 1) * xsize_src + xi) * 3 + j], xf, yf);
				}
			} else {
				xf = INTERP_NORM - xf;
				yf = INTERP_NORM - yf;
				for (j = 0; j < 3; j++) {
					pix[j] = GLinterpolate_imutil(pix_src[((yi + 1) * xsize_src + xi + 1) * 3 + j], pix_src[((yi + 1) * xsize_src + xi) * 3 + j],
												  pix_src[(yi * xsize_src + xi + 1) * 3 + j], xf, yf);
				}
			}

			pix += 3;
			x1 += x1inc;
		}
		y1 += y1inc;
	}
	
}

#define FRAC_BITS 16

/* resizing with no GLinterlating nor nearest pixel */

void gl_resizeImageNoInterpolate(GLubyte* dest, GLint xsize_dest, GLint ysize_dest, GLubyte* src, GLint xsize_src, GLint ysize_src) {
	GLubyte *pix, *pix_src, *pix1;
	GLint x1, y1, x1inc, y1inc;
	GLint xi, yi, x, y;

	pix = dest;
	pix_src = src;

	x1inc = (GLint)((GLfloat)((xsize_src) << FRAC_BITS) / (GLfloat)(xsize_dest));
	y1inc = (GLint)((GLfloat)((ysize_src) << FRAC_BITS) / (GLfloat)(ysize_dest));

	y1 = 0;
	for (y = 0; y < ysize_dest; y++) {
		x1 = 0;
		for (x = 0; x < xsize_dest; x++) {
			xi = x1 >> FRAC_BITS;
			yi = y1 >> FRAC_BITS;
			pix1 = pix_src + (yi * xsize_src + xi) * 3;

			pix[0] = pix1[0];
			pix[1] = pix1[1];
			pix[2] = pix1[2];

			pix += 3;
			x1 += x1inc;
		}
		y1 += y1inc;
	}
}
