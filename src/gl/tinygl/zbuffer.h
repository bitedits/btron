#ifndef _tgl_zbuffer_h_
#define _tgl_zbuffer_h_

/*
 * Z buffer
 */

#include "zfeatures.h"
#include "GL/gl.h"


#define ZB_Z_BITS 16

#define ZB_POINT_Z_FRAC_BITS 14





/*
 * The sampler's index fields.  Where a texture's texels sit is its own business
 * (GLImage's smask/tmask/tshift, computed at upload); what is fixed here is the
 * arithmetic layout -- the low (ZB_POINT_*_FRAC_BITS + 1) bits of s or t are one
 * texel index, and a PIXEL is (1 << LOG2_PSZB) bytes.  A texture of row stride
 * (1 << rowpow) texels therefore puts t's field at
 * ZB_TEX_T_SHIFT_BASE - rowpow bits.
 */
#define ZB_TEX_S_SHIFT (ZB_POINT_S_FRAC_BITS + 1 - LOG2_PSZB)
#define ZB_TEX_T_SHIFT_BASE (ZB_POINT_T_FRAC_BITS + 1 - LOG2_PSZB)

/*The corrected mult mask prevents a bug relating to color interp. it's also why the color bit depth is so damn high.*/
#define COLOR_MULT_MASK (0xff0000)
#define COLOR_CORRECTED_MULT_MASK (0xfe0000)
#define COLOR_MASK 		(0xffffff)
#define COLOR_MIN_MULT (0x00ffff)
#define COLOR_SHIFT		16

/*
 * The PIXEL domain.
 *
 * `zb->pbuf` is not always a private buffer: the EGL surface hands TinyGL the window GDEV's
 * pixel array, so every span store below lands in the very memory the display controller
 * scans out.  On the PS2 that controller is the GS in CT32 mode, which wants memory bytes
 * R,G,B,A, while a BTRON COLOR is 0xAARRGGBB, i.e. bytes B,G,R,A.  The two orders differ by
 * exactly one thing -- the R and B fields exchanged, an involution -- and no GS register
 * permutes channels, so the exchange has to be inside the word TinyGL writes.
 *
 * Where to put it is a cost question.  Wrapping each store and each destination read costs
 * about seven more integer ops on every blended fragment of a gradient, on a CPU with no
 * floating point at all.  Deciding the field order once, here, costs nothing, because every
 * pixel in this library is built by RGB_TO_PIXEL and taken apart by the GET_* macros, and
 * those macros' arguments are channel values left-aligned at bit 16 -- a domain the rest of
 * the code never looks inside.  So the domain is canvas order on the PS2.
 *
 * Four facts make that sound rather than lucky, and a fifth site depends on them:
 *   - alpha is byte 3 in both orders: TGL_TEX_ALPHA, TGL_ALPHA_BYTE and the top-byte sets
 *     need no second definition;
 *   - green is byte 1 in both: only the R and B arms below change;
 *   - a texel's source is a byte array, so it crosses the boundary once per texel at upload
 *     and is converted through these same macros (image_util.c);
 *   - a PIXEL-to-PIXEL copy (glCopyTexImage2D, the pbuf-to-texture path) never crosses it;
 *   - the colour-key sentinels are 0x00ff00ff, magenta, which is a fixed point of the swap.
 */
#if defined(BTRON_PS2_TARGET)
/* red lands in the low byte, blue at bits 16-23 */
#define TGL_RED_FIELD_SHIFT 0
#define TGL_BLUE_FIELD_SHIFT 16
#define COLOR_R_GET32(r) (((r) >> 16) & 0xff)
#define COLOR_B_GET32(b) ((b) & 0xff0000)
#else
#define TGL_RED_FIELD_SHIFT 16
#define TGL_BLUE_FIELD_SHIFT 0
#define COLOR_R_GET32(r) ((r) & 0xff0000)
#define COLOR_B_GET32(b) ((b >> 16) & 0xff)
#endif
#define COLOR_G_GET32(g) ((g>>8) & 0xff00)

/*
 * The frame buffer's alpha byte is a constant, so it is written with the pixel rather
 * than fixed up after the fact.
 *
 * A BTRON COLOR is 0xAARRGGBB and a window's surface is composited as opaque, so every
 * pixel of a GL surface has to arrive with 0xFF on top.  That used to be a pass of its
 * own: tgl_swap_buffers() walked the whole surface OR-ing the byte in, which on the PS2
 * is one uncached read and one uncached write for every pixel the frame had already put
 * there -- the XMB's 384,000 px, measured at 10,494 us for a frame, with no arithmetic
 * in it at all.  The rasteriser holds each of those words in a register on its way to
 * memory anyway, so at the store the byte is one OR and zero extra traffic.
 *
 * The rule sits at the STORE, never in RGB_TO_PIXEL: the same macro builds texels, and a
 * texel's top byte is its alpha (TGL_TEX_ALPHA, the upload in image_util.c), which the
 * blend multiplies by.  A texel forced opaque would turn every icon and glyph in an atlas
 * into a solid square.  So FB_PIXEL() wraps the places that write a frame buffer, and
 * only those, and it is idempotent -- a pixel that already carries the byte is unchanged.
 *
 * Nobody reads a frame buffer's alpha back: the destination-alpha blend factor has no arm
 * in TGL_BLEND_FACTOR at all, because a canvas has no alpha to read, and glCopyTexImage2D
 * takes a PIXEL verbatim rather than through any arithmetic on its channels.  So the byte
 * is constant, it costs nothing to write where the pixel is stored, and no pass has to walk
 * the surface for it afterwards.
 */
#if TGL_FEATURE_RENDER_BITS == 32
#define ZB_FB_ALPHA 0xFF000000u
#else
#define ZB_FB_ALPHA 0u /* a 16-bit PIXEL has no alpha channel to carry */
#endif
#define FB_PIXEL(v) ((PIXEL)((v) | ZB_FB_ALPHA))

#define COLOR_R_GET16(r) ((r>>8) & 0xF800)
#define COLOR_G_GET16(g) ((((g)) >> 13) & 0x07E0)
#define COLOR_B_GET16(b) (((b) >> 19) & 31)

#if TGL_FEATURE_RENDER_BITS == 32
#define RGB_TO_PIXEL(r,g,b) \
  ( COLOR_R_GET32(r) | COLOR_G_GET32(g) | COLOR_B_GET32(b) )
#elif TGL_FEATURE_RENDER_BITS == 16
#define RGB_TO_PIXEL(r,g,b) \
	( COLOR_R_GET16(r) | COLOR_G_GET16(g) | COLOR_B_GET16(b)  )
#endif
/*This is how textures are sampled. if you want to do some sort of fancy texture filtering,*/
/*you do it here.  `tsmask`, `ttmask` and `ttshift` are the current texture's fields, bound*/
/*by DRAW_INIT() from the GLImage that ZB_setTexture() was given.*/
#define TEXTURE_SAMPLE(texture, s, t)														\
 (*(PIXEL*)( (GLbyte*)texture + 															\
   ( (((s) & tsmask) >> ZB_TEX_S_SHIFT) | (((t) & ttmask) >> ttshift) )					\
 ))
/*
 * Whether a fragment may update the depth plane.  GL 1.1 4.4.1: with GL_DEPTH_TEST off
 * the depth plane is not enabled at all, so no update occurs -- and this is the rule the
 * 2D interface lives by.  Reading zb->depth_write alone instead leaves every one of those
 * stores in the span loop, computing z >> 14 and writing a 16-bit value nothing will ever
 * read back, at a cost of one of the roughly sixty instructions a blended pixel is worth.
 */
#define ZB_DEPTH_WRITE(zb) ((zb)->depth_test ? (zb)->depth_write : 0)

/* display modes */
#define ZB_MODE_5R6G5B  1  /* true color 16 bits */
#define ZB_MODE_INDEX   2  /* color index 8 bits */
#define ZB_MODE_RGBA    3  /* 32 bit ABGR mode */
#define ZB_MODE_RGB24   4  /* 24 bit rgb mode */
#define ZB_NB_COLORS    225 /* number of colors for 8 bit display */



#define TGL_CLAMPI(imp) ( (imp>0)?((imp>COLOR_MASK)?COLOR_MASK:imp):0   )




#if TGL_FEATURE_RENDER_BITS == 32

/* 32 bit mode */

/*
 * The read side of the domain declared above: the same R/B exchange, and green untouched.
 * Each arm keeps its own instruction count -- only which of the pair carries the shift
 * moves -- so a span loop on the PS2 pays nothing for writing canvas order.
 */
#if defined(BTRON_PS2_TARGET)
#define GET_REDDER(p) ((p & 0xff) << 16)
#define GET_BLUEER(p) ((p & 0xff0000))
#define GET_RED(p) (p & 0xff)
#define GET_BLUE(p) ((p >> 16) & 0xff)
#else
#define GET_REDDER(p) ((p & 0xff0000))
#define GET_BLUEER(p) ((p & 0xff) << 16)
#define GET_RED(p) ((p >> 16) & 0xff)
#define GET_BLUE(p) (p & 0xff)
#endif
#define GET_GREENER(p) ((p & 0xff00) << 8)
/*These never change, DO NOT CHANGE THESE BASED ON COLOR INTERP BIT DEPTH*/
#define GET_GREEN(p) ((p>>8)&0xff)
typedef GLuint PIXEL;
#define PSZB 4
#define LOG2_PSZB 2
/* A 32-bit PIXEL carries the texel's alpha in its top byte. */
#define TGL_TEX_ALPHA(t) (((t) >> 24) & 0xff)

#elif TGL_FEATURE_RENDER_BITS == 16

/* 16 bit mode */
#define GET_REDDER(p) ((p & 0xF800)<<8)
#define GET_GREENER(p) ((p & 0x07E0)<<13)
#define GET_BLUEER(p) ((p & 31)<<19)
/*DO NOT CHANGE THESE BASED ON COLOR INTERP BITDEPTH*/
#define GET_RED(p) ((p & 0xF800)>>8)
#define GET_GREEN(p) ((p & 0x07E0)>>3)
#define GET_BLUE(p) ((p & 31)<<3)


typedef GLushort PIXEL;
#define PSZB 2 
#define LOG2_PSZB 1 
/* A 16-bit PIXEL has no alpha channel: 5R6G5B textures are opaque. */
#define TGL_TEX_ALPHA(t) 255


#else
#error "wrong TGL_FEATURE_RENDER_BITS"
#endif

#if TGL_FEATURE_LIT_TEXTURES == 1
#define RGB_MIX_FUNC(rr, gg, bb, tpix) \
	RGB_TO_PIXEL( \
		((rr * GET_RED(tpix))>>8),\
		((gg * GET_GREEN(tpix))>>8),\
		((bb * GET_BLUE(tpix))>>8)\
	)
#else
#define RGB_MIX_FUNC(rr, gg, bb, tpix)(tpix)
#endif



#define TGL_NO_BLEND_FUNC(source, srcalpha, dest){dest = FB_PIXEL(source);}
#define TGL_NO_BLEND_FUNC_RGB(rr, gg, bb, srcalpha, dest){dest = FB_PIXEL(RGB_TO_PIXEL(rr,gg,bb));}

#if TGL_FEATURE_BLEND == 1
/*
 * The three blend parameters cannot change while a primitive is being filled, so they are
 * read once per primitive here -- and the two combinations the in-tree applications
 * actually draw with, GL_FUNC_ADD with GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA and GL_FUNC_ADD
 * with GL_DST_COLOR / GL_ONE, are recognised once too.
 *
 * That recognition is the whole point of `tglblend_alpha` and `tglblend_brighten`.  Left to
 * itself the compiler puts the two factor switches and the equation switch inside the span
 * loop: the object code for a blended fill is an indirect `jr` through a jump table for every
 * fragment, plus a reload of the three parameters from stack slots the loop keeps spilling
 * to.  On a soft-float R5900 that table branch, not the arithmetic, is what a full-canvas
 * gradient is made of.  The recognised cases therefore have their own branch-free spellings
 * below, and every other combination still runs the general one.
 */
#define TGL_BLEND_VARS                                                                                                                                           \
	GLuint zbblendeq = zb->blendeq;                                                                                                                              \
	GLuint sfactor = zb->sfactor;                                                                                                                                \
	GLuint dfactor = zb->dfactor;                                                                                                                                \
	const GLuint tglblend_alpha = (zbblendeq == GL_FUNC_ADD && sfactor == GL_SRC_ALPHA && dfactor == GL_ONE_MINUS_SRC_ALPHA);                                     \
	const GLuint tglblend_brighten = (zbblendeq == GL_FUNC_ADD && sfactor == GL_DST_COLOR && dfactor == GL_ONE);

/*SORCERY to achieve 32 bit signed integer clamping*/


#define TGL_BLEND_SWITCH_CASE(sr,sg,sb,dr,dg,db,dest) 									\
		switch(zbblendeq){															\
			case GL_FUNC_ADD:															\
			default:																	\
				sr+=dr;sg+=dg;sb+=db;													\
				sr = TGL_CLAMPI(sr);													\
				sg = TGL_CLAMPI(sg);													\
				sb = TGL_CLAMPI(sb);													\
				dest = FB_PIXEL(RGB_TO_PIXEL(sr,sg,sb));											\
			break;																		\
			case GL_FUNC_SUBTRACT:														\
				sr-=dr;sg-=dg;sb-=db;													\
				sr = TGL_CLAMPI(sr);													\
				sg = TGL_CLAMPI(sg);													\
				sb = TGL_CLAMPI(sb);													\
				dest = FB_PIXEL(RGB_TO_PIXEL(sr,sg,sb));											\
			break;																		\
			case GL_FUNC_REVERSE_SUBTRACT:												\
				sr=dr-sr;sg=dg-sg;sb=db-sb;												\
				sr = TGL_CLAMPI(sr);													\
				sg = TGL_CLAMPI(sg);													\
				sb = TGL_CLAMPI(sb);													\
				dest = FB_PIXEL(RGB_TO_PIXEL(sr,sg,sb));											\
			break;																		\
			  																			\
		}

/*
 * One GL blend factor, applied to the channels (cr,cg,cb).  `scr/scg/scb` and
 * `dcr/dcg/dcb` are the source and destination colours this factor may read, and `sa`
 * is the source alpha byte, so one macro serves both switches: the source side passes
 * its own channels as the source colour and the destination's as the destination, and
 * the destination side swaps them.
 *
 * Integer only, and that is the point: every channel here is an 8-bit value held at
 * bit 16, exactly as GET_REDDER produces it, so a factor is one byte extract, one
 * multiply and one shift back.  (0xff0000 * 255) is the largest product and it still
 * fits a GLuint, so nothing here costs a float or a libm call on a -msoft-float
 * target.  The alpha cases are the only ones that read `sa`, and the GL_ONE/default
 * arm does nothing at all, so a blend that ignores alpha costs what it always cost.
 * GL_DST_ALPHA has nothing to read -- the frame buffer carries no alpha -- and falls
 * to the default.
 */
#define TGL_BLEND_FACTOR(which, cr, cg, cb, scr, scg, scb, dcr, dcg, dcb, sa)			\
		switch(which){																	\
			case GL_ONE:																\
			default:																	\
			break;																		\
			case GL_SRC_ALPHA:															\
			cr=(cr*sa)>>8; cg=(cg*sa)>>8; cb=(cb*sa)>>8; break;							\
			case GL_ONE_MINUS_SRC_ALPHA:												\
			cr=(cr*(255-sa))>>8; cg=(cg*(255-sa))>>8; cb=(cb*(255-sa))>>8; break;		\
			case GL_SRC_COLOR:															\
			cr=(cr*((scr>>16)&0xff))>>8; cg=(cg*((scg>>16)&0xff))>>8;					\
			cb=(cb*((scb>>16)&0xff))>>8; break;											\
			case GL_ONE_MINUS_SRC_COLOR:												\
			cr=(cr*(255-((scr>>16)&0xff)))>>8; cg=(cg*(255-((scg>>16)&0xff)))>>8;		\
			cb=(cb*(255-((scb>>16)&0xff)))>>8; break;									\
			case GL_DST_COLOR:															\
			cr=(cr*((dcr>>16)&0xff))>>8; cg=(cg*((dcg>>16)&0xff))>>8;					\
			cb=(cb*((dcb>>16)&0xff))>>8; break;											\
			case GL_ONE_MINUS_DST_COLOR:												\
			cr=(cr*(255-((dcr>>16)&0xff)))>>8; cg=(cg*(255-((dcg>>16)&0xff)))>>8;		\
			cb=(cb*(255-((dcb>>16)&0xff)))>>8; break;									\
			case GL_ZERO:																\
			cr=0;cg=0;cb=0; break;														\
		}

/* The interpolated vertex alpha is left-aligned like r/g/b; a PIXEL's alpha is its top
 * byte, and GL's modulate multiplies the two. */
#define TGL_ALPHA_BYTE(a) (((a) >> 16) & 0xff)

/*
 * GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA / GL_FUNC_ADD written out with no switch, and
 * with no compare either.
 *
 * Every term is the same fixed-point quantity the general path computes -- a channel held
 * left-aligned at bit 16, scaled by an alpha byte and shifted back by eight, the two
 * terms summed -- and the byte the general path leaves in that channel after its clamp
 * and RGB_TO_PIXEL is exactly min(sum >> 16, 255), so the pixels come out bit-identical.
 *
 * The clamp is written as arithmetic rather than `if (v > COLOR_MASK) v = COLOR_MASK`
 * because of where it sits: in a gradient fill the sum crosses that boundary at a
 * different column in every row, so three such compares are three branch predictions the
 * span loop loses on average, and on the Emotion Engine a lost prediction costs more than
 * the subtract, shift and and that select the same minimum here.
 *
 * What is left is six multiplies -- the irreducible part of blending two colours.
 *
 * This is the one place in the library that packs a pixel without going through
 * RGB_TO_PIXEL, because the clamp above already produced each channel as a plain byte and
 * the field order is the only thing left to do.  So it is the one place that has to name
 * the fields: TGL_*_FIELD_SHIFT is where the domain decision reaches.
 */
#define TGL_BLEND_ALPHA_ADD(sr, sg, sb, dr, dg, db, sa, dest)                                                                                    \
	GLuint tb_ia_ = 255 - (sa);                                                                                                                     \
	GLint tb_d_;                                                                                                                                    \
	GLuint tb_v_;                                                                                                                                   \
	sr = (sr * (sa)) >> 8;                                                                                                                       \
	sg = (sg * (sa)) >> 8;                                                                                                                       \
	sb = (sb * (sa)) >> 8;                                                                                                                       \
	tb_d_ = (GLint)((sr + ((dr * tb_ia_) >> 8)) >> 16) - 255;                                                                                          \
	tb_v_ = (GLuint)(255 + (tb_d_ & (tb_d_ >> 31))) << TGL_RED_FIELD_SHIFT;                                                                                \
	tb_d_ = (GLint)((sg + ((dg * tb_ia_) >> 8)) >> 16) - 255;                                                                                          \
	tb_v_ |= (GLuint)(255 + (tb_d_ & (tb_d_ >> 31))) << 8;                                                                                                \
	tb_d_ = (GLint)((sb + ((db * tb_ia_) >> 8)) >> 16) - 255;                                                                                          \
	tb_v_ |= (GLuint)(255 + (tb_d_ & (tb_d_ >> 31))) << TGL_BLUE_FIELD_SHIFT;                                                                              \
	dest = FB_PIXEL(tb_v_);

/*
 * GL_DST_COLOR / GL_ONE / GL_FUNC_ADD written out the same way: the source factor is one
 * multiply of each channel by the destination's own byte, the destination factor is GL_ONE
 * and so does nothing, and the equation adds them.  That is the compositor the XMB ribbon
 * uses -- it brightens what the gradient left behind by (1 + c) -- and it is the only
 * large-area fill in the image that is not the alpha case.
 *
 * Every term is the term the general path computes, in the same order, on channels in the
 * same left-aligned domain, so the pixels are bit-identical.  Only two things are dropped,
 * both provably no-ops here: the `& 0xff` on the multiplier byte, because a GET_*REDER result
 * is a single byte already at bit 16 and cannot be wider, and the `imp > 0` arm of the clamp,
 * because an unsigned sum of a shifted product and a channel is never negative and 0 clamps
 * to 0 either way.  The remaining clamp is `min(sum, COLOR_MASK)`, written as arithmetic for
 * the reason given above.
 */
#define TGL_BLEND_BRIGHTEN(sr, sg, sb, dr, dg, db, dest)                                                                                     \
	GLint tbb_d_;                                                                                                                             \
	sr = ((sr * (dr >> 16)) >> 8) + dr;                                                                                                       \
	sg = ((sg * (dg >> 16)) >> 8) + dg;                                                                                                       \
	sb = ((sb * (db >> 16)) >> 8) + db;                                                                                                       \
	tbb_d_ = (GLint)sr - COLOR_MASK;                                                                                                          \
	sr = (GLuint)(COLOR_MASK + (tbb_d_ & (tbb_d_ >> 31)));                                                                                     \
	tbb_d_ = (GLint)sg - COLOR_MASK;                                                                                                          \
	sg = (GLuint)(COLOR_MASK + (tbb_d_ & (tbb_d_ >> 31)));                                                                                     \
	tbb_d_ = (GLint)sb - COLOR_MASK;                                                                                                          \
	sb = (GLuint)(COLOR_MASK + (tbb_d_ & (tbb_d_ >> 31)));                                                                                     \
	dest = FB_PIXEL(RGB_TO_PIXEL(sr, sg, sb));

/*
 * NOTE: in the general path below, the source factor is applied first, so a destination
 * factor that reads the source colour reads it already multiplied by its own factor.  No
 * combination the in-tree applications use depends on this.
 */
#define TGL_BLEND_FUNC(source, srcalpha, dest){											\
	if (tglblend_alpha){															\
		GLuint sa_ = (srcalpha);															\
		GLuint s_ = (source);																\
		GLuint d_ = (dest);																\
		GLuint sr_ = GET_REDDER(s_), sg_ = GET_GREENER(s_), sb_ = GET_BLUEER(s_);			\
		GLuint dr_ = GET_REDDER(d_), dg_ = GET_GREENER(d_), db_ = GET_BLUEER(d_);			\
		TGL_BLEND_ALPHA_ADD(sr_, sg_, sb_, dr_, dg_, db_, sa_, dest)						\
	} else if (tglblend_brighten){														\
		GLuint s_ = (source);																\
		GLuint d_ = (dest);																\
		GLuint sr_ = GET_REDDER(s_), sg_ = GET_GREENER(s_), sb_ = GET_BLUEER(s_);			\
		GLuint dr_ = GET_REDDER(d_), dg_ = GET_GREENER(d_), db_ = GET_BLUEER(d_);			\
		TGL_BLEND_BRIGHTEN(sr_, sg_, sb_, dr_, dg_, db_, dest)								\
	} else {																				\
	{																					\
	GLuint sr, sg, sb, dr, dg, db;														\
	GLuint sa = (srcalpha);																\
	{	GLuint temp = source;															\
	sr = GET_REDDER(temp); sg = GET_GREENER(temp); sb = GET_BLUEER(temp);				\
	temp = dest;																		\
	dr = GET_REDDER(temp); dg = GET_GREENER(temp); db = GET_BLUEER(temp);}				\
		TGL_BLEND_FACTOR(sfactor, sr, sg, sb, sr, sg, sb, dr, dg, db, sa)				\
		TGL_BLEND_FACTOR(dfactor, dr, dg, db, sr, sg, sb, dr, dg, db, sa)				\
		TGL_BLEND_SWITCH_CASE(sr,sg,sb,dr,dg,db,dest)									\
	}																					\
	}																					\
}

#define TGL_BLEND_FUNC_RGB(rr, gg, bb, srcalpha, dest){									\
	if (tglblend_alpha){															\
		GLuint sa_ = (srcalpha);															\
		GLuint d_ = (dest);																\
		GLuint sr_ = (rr) & COLOR_MASK, sg_ = (gg) & COLOR_MASK, sb_ = (bb) & COLOR_MASK;	\
		GLuint dr_ = GET_REDDER(d_), dg_ = GET_GREENER(d_), db_ = GET_BLUEER(d_);			\
		TGL_BLEND_ALPHA_ADD(sr_, sg_, sb_, dr_, dg_, db_, sa_, dest)						\
	} else if (tglblend_brighten){														\
		GLuint d_ = (dest);																\
		GLuint sr_ = (rr) & COLOR_MASK, sg_ = (gg) & COLOR_MASK, sb_ = (bb) & COLOR_MASK;	\
		GLuint dr_ = GET_REDDER(d_), dg_ = GET_GREENER(d_), db_ = GET_BLUEER(d_);			\
		TGL_BLEND_BRIGHTEN(sr_, sg_, sb_, dr_, dg_, db_, dest)								\
	} else {																				\
	{																					\
		GLuint sr = rr & COLOR_MASK, sg = gg & COLOR_MASK, sb = bb & COLOR_MASK, dr, dg, db;	\
		GLuint sa = (srcalpha);															\
		{GLuint temp = dest;															\
		dr = GET_REDDER(temp); dg = GET_GREENER(temp); db = GET_BLUEER(temp);}			\
		TGL_BLEND_FACTOR(sfactor, sr, sg, sb, sr, sg, sb, dr, dg, db, sa)				\
		TGL_BLEND_FACTOR(dfactor, dr, dg, db, sr, sg, sb, dr, dg, db, sa)				\
		TGL_BLEND_SWITCH_CASE(sr,sg,sb,dr,dg,db,dest)									\
	}																					\
	}																					\
}

#else
#define TGL_BLEND_VARS /* a comment */
#define TGL_ALPHA_BYTE(a) (((a) >> 16) & 0xff)
#define TGL_BLEND_FUNC(source, srcalpha, dest){dest = FB_PIXEL(source);}
#define TGL_BLEND_FUNC_RGB(rr, gg, bb, srcalpha, dest){dest = FB_PIXEL(RGB_TO_PIXEL(rr,gg,bb));}
#endif


typedef struct {

    
    
    GLushort *zbuf;
    PIXEL *pbuf;
    PIXEL *current_texture;
    /* The current texture's sampler fields, bound by ZB_setTexture() from its GLImage
     * so the span loop takes them as given instead of finding out per texel. */
    GLuint current_smask, current_tmask;
    GLint current_tshift;
    

	/* point size*/
    GLfloat pointsize;





    /* opengl polygon stipple*/

#if TGL_FEATURE_POLYGON_STIPPLE == 1
    GLubyte stipplepattern[TGL_POLYGON_STIPPLE_BYTES];
    GLuint dostipple;
#endif
	GLenum blendeq, sfactor, dfactor;
    GLint enable_blend;
    GLint xsize,ysize;
    GLint linesize; /* line size, in bytes */
    /* depth */
    GLint depth_test;
    GLint depth_write;
    GLubyte frame_buffer_allocated;
} ZBuffer;

typedef struct {
  GLint x,y,z;     /* integer coordinates in the zbuffer */
  GLint s,t;       /* coordinates for the mapping */
  GLint r,g,b;     /* color indexes */
  GLint a;         /* alpha, left-aligned like the colour channels */
  
  GLfloat sz,tz;   /* temporary coordinates for mapping */
} ZBufferPoint;

/* zgl.h owns GLImage; the rasterizer only ever needs to be handed one. */
struct GLImage;

/* zbuffer.c */

ZBuffer *ZB_open(int xsize,int ysize,int mode,



		 void *frame_buffer);


void ZB_close(ZBuffer *zb);

void ZB_resize(ZBuffer *zb,void *frame_buffer,GLint xsize,GLint ysize);
void ZB_clear(ZBuffer *zb,GLint clear_z,GLint z,
	      GLint clear_color,GLint r,GLint g,GLint b);
/* linesize is in BYTES */
void ZB_copyFrameBuffer(ZBuffer *zb,void *buf,GLint linesize);

/* zdither.c */

/*
void ZB_initDither(ZBuffer *zb,GLint nb_colors,
		   unsigned char *color_indexes,GLint *color_table);
void ZB_closeDither(ZBuffer *zb);
void ZB_ditherFrameBuffer(ZBuffer *zb,unsigned char *dest,
			  GLint linesize);
*/
/* zline.c */

void ZB_plot(ZBuffer *zb,ZBufferPoint *p);
void ZB_line(ZBuffer *zb,ZBufferPoint *p1,ZBufferPoint *p2);
void ZB_line_z(ZBuffer * zb, ZBufferPoint * p1, ZBufferPoint * p2);

/* ztriangle.c */

/* Implemented in texture.c, where the GLImage it reads is defined. */
void ZB_setTexture(ZBuffer *zb, const struct GLImage *im);

void ZB_fillTriangleFlat(ZBuffer *zb,
		 ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);

void ZB_fillTriangleFlatNOBLEND(ZBuffer *zb,
		 ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);


void ZB_fillTriangleSmooth(ZBuffer *zb,
		   ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);

void ZB_fillTriangleSmoothNOBLEND(ZBuffer *zb,
		   ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);
/*
 * Affine texture mapping.  This is the case clip.c routes to when a triangle's three
 * vertices carry the same interpolated z, because there 1/z is constant over its area and
 * the perspective path's per-scanline re-derivation of the s/t steps has a constant answer.
 * Removed by Gek once the perspective path was the only one left; the condition above is
 * what makes it worth carrying again.
 */
void ZB_fillTriangleMapping(ZBuffer *zb,
			   ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);

void ZB_fillTriangleMappingNOBLEND(ZBuffer *zb,
				 ZBufferPoint *p1,ZBufferPoint *p2,ZBufferPoint *p3);
void ZB_fillTriangleMappingPerspective(ZBuffer *zb,
                    ZBufferPoint *p0,ZBufferPoint *p1,ZBufferPoint *p2);


void ZB_fillTriangleMappingPerspectiveNOBLEND(ZBuffer *zb,
                    ZBufferPoint *p0,ZBufferPoint *p1,ZBufferPoint *p2);

typedef void (*ZB_fillTriangleFunc)(ZBuffer  *,
	    ZBufferPoint *,ZBufferPoint *,ZBufferPoint *);

/* memory.c */
#if TGL_FEATURE_CUSTOM_MALLOC == 1
void gl_free(void *p);
void *gl_malloc(GLint size);
void *gl_zalloc(GLint size);
#else
#include<string.h>
#include<stdlib.h>
static void gl_free(void* p) { free(p); }
static void* gl_malloc(GLint size) { return malloc(size); }
static void* gl_zalloc(GLint size) { return calloc(1, size); }
#endif

#endif /* _tgl_zbuffer_h_ */
