/*
 * B-TRON Specification Compatible Header: types.h
 * Cleanroom implementation of standard TRON fundamental types.
 */

#ifndef _BTRON_TYPES_H_
#define _BTRON_TYPES_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __TYPEDEF_H__
/* Fundamental TRON integer types */
typedef int8_t            B;   /* 8-bit signed integer (Byte) */
typedef int16_t           H;   /* 16-bit signed integer (Halfword) */
typedef int32_t           W;   /* 32-bit signed integer (Word) */
typedef int64_t           D;   /* 64-bit signed integer (Doubleword) */

typedef uint8_t           UB;  /* 8-bit unsigned integer */
typedef uint16_t          UH;  /* 16-bit unsigned integer */
typedef uint32_t          UW;  /* 32-bit unsigned integer */
typedef uint64_t          UD;  /* 64-bit unsigned integer */

/* Variable Word types */
typedef void*             VW;  /* Pointer / variable word */
typedef void*             VP;  /* Generic pointer */
typedef int16_t           VH;  /* Variable halfword */
typedef int8_t            VB;  /* Variable byte */

/* TRON Object & System types */
typedef int32_t           ID;  /* TRON Object ID */
typedef int32_t           ER;  /* TRON Error Code */
typedef ER                ERR; /* Alias for ER used in specification */
typedef W                 WERR;/* Word or Error return type */
typedef uint32_t          BOOL;/* Boolean value */
typedef uint16_t          TC;  /* TRON Character Code (16-bit) */
#else
typedef ER                ERR; /* Alias for ER used in specification */
typedef W                 WERR;/* Word or Error return type */
#endif
#ifndef _COLOR_DEFINED_
#define _COLOR_DEFINED_
typedef uint32_t          COLOR;/* TRON Color value (RGBA) */
#endif

/* ── The framebuffer byte order ─────────────────────────────────────────────
 * A COLOR is 0xAARRGGBB, whose little-endian bytes are B,G,R,A.  The PS2's
 * Graphics Synthesizer stores a CT32 pixel with the bytes R,G,B,A -- so a word
 * read out of the GS's VRAM as a uint32_t is 0xAABBGGRR: the same colour with
 * its red and blue fields exchanged.  Neither the GIF's Host->Local bit transfer
 * nor the PCRTC's display read can permute channels, so the exchange has to live
 * somewhere in the port, and it now lives in the canvas.
 *
 * BTRON_PIXEL_FB() performs it.  It is its own inverse, which is the whole
 * convention and the only rule:
 *
 *   a colour word ENTERS a pixel array through it, and
 *   a pixel array's word LEAVES for anything that looks at R, G or B through it,
 *   and a pixel array copied into a pixel array is never passed through it --
 *   that copy is byte order agnostic, which is why the compositor's spans,
 *   row blits and background cache needed no change at all.
 *
 * Every site the rule touches is in the PS2 link's own leaf stores and component
 * readers, and `make test-ps2-fb-order` draws a saturated colour through each
 * primitive and checks the word that lands in the canvas.  On every other target
 * -- the Pi 400 first among them -- the macro is the identity and the canvas
 * holds what it always held.
 */
#if defined(BTRON_PS2_TARGET)
#define BTRON_PIXEL_FB(c) ((COLOR)((((COLOR)(c) & 0xFF00FF00u) |              \
                                    (((COLOR)(c) & 0x00FF0000u) >> 16) |      \
                                    (((COLOR)(c) & 0x000000FFu) << 16))))
#else
#define BTRON_PIXEL_FB(c) ((COLOR)(c))
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/* Geometry Primitives */
typedef struct {
    H x;
    H y;
} PNT;

typedef struct {
    H left;
    H top;
    H right;
    H bottom;
} RECT;

/* Pattern fill primitive */
typedef struct {
    UB pat[8];  /* 8x8 monochrome bitmap pattern */
} PAT;

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_TYPES_H_ */
