/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Document Model (src/apps/clarity_doc.h)
 * Prototype data model: three page formats, free-floating frames, VOBJ serial format.
 * Deliberately omits linked frames, masters, paragraph styles, TeX.
 */

#ifndef _CLARITY_DOC_H_
#define _CLARITY_DOC_H_

#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 * Page formats (prototype: exactly three active formats)
 * Full Japanese / Pecha enum kept in clarity.c for spec fidelity.
 * ================================================================ */

typedef enum {
    FMT_A4      = 0,   /* A4 Portrait  210 × 297 mm – horizontal LTR */
    FMT_SHIROKU = 1,   /* 四六判       127 × 188 mm – vertical RTL   */
    FMT_PECHA   = 2    /* Kangyur Pecha 560 × 110 mm – horizontal LTR */
} ClarityPageFmt;

/* ================================================================
 * Frame types
 * ================================================================ */

typedef enum {
    FRAME_TEXT  = 0,
    FRAME_IMAGE = 1
} ClarityFrameType;

/* ================================================================
 * Text flow direction
 * ================================================================ */

typedef enum {
    FLOW_H_LTR = 0,   /* Horizontal left-to-right (A4, Pecha) */
    FLOW_V_RTL = 1    /* Vertical right-to-left  (四六判 縦書き) */
} ClarityFlow;

/* ================================================================
 * Editor tool state
 * ================================================================ */

typedef enum {
    TOOL_SELECT      = 0,
    TOOL_TEXT_FRAME  = 1,
    TOOL_IMAGE_FRAME = 2
} ClarityTool;

/* ================================================================
 * Single frame (text or image)
 * ================================================================ */

#define CLARITY_TEXT_BUF   4096   /* max TRON code units per frame   */
#define CLARITY_MAX_FRAMES   32   /* frames per document             */

typedef struct {
    UB             id;              /* 1-based; 0 = free slot          */
    ClarityFrameType type;
    RECT           bounds;          /* position in canvas pixels       */
    ClarityFlow    flow;

    /* --- TextFrame payload ---------------------------------------- */
    UH             text[CLARITY_TEXT_BUF]; /* TRON code units (TC)    */
    UW             text_len;

    /* --- ImageFrame payload --------------------------------------- */
    UB            *bitmap;          /* raw RGBA pixels (malloc'd)      */
    H              bmp_w;
    H              bmp_h;
} ClarityFrame;

/* ================================================================
 * Document root
 * ================================================================ */

typedef struct {
    ClarityPageFmt fmt;
    int            page_w_mm;       /* derived from fmt on init        */
    int            page_h_mm;
    ClarityFrame   frames[CLARITY_MAX_FRAMES];
    int            frame_count;
    int            selected_frame;  /* index into frames[], or -1      */
    ClarityTool    tool;
    BOOL           dirty;           /* unsaved changes flag            */

    /* Drag state for frame creation / resize */
    BOOL           dragging;
    H              drag_start_x;
    H              drag_start_y;
    int            drag_handle;     /* resize handle id 0-7, or -1     */
} ClarityDoc;

/* ================================================================
 * Canvas layout constants (96 dpi base)
 * ================================================================ */

#define CLARITY_DPI              96
#define CLARITY_CANVAS_MARGIN_PX 24  /* grey border around page        */
#define CLARITY_HANDLE_RADIUS     4  /* resize handle half-size px     */

/* ================================================================
 * Serial / persistence format
 * Magic: "CLRD", version byte, then per-frame records.
 * All integers little-endian.
 * ================================================================ */

#define CLARITY_SERIAL_MAGIC  "CLRD"
#define CLARITY_SERIAL_VER     1

/* Packed frame record written to VOBJ blob.
 * Variable-length tail: text_len*2 bytes of UH text,
 * then bmp_w*bmp_h*4 bytes of RGBA bitmap (may be 0). */
#pragma pack(push, 1)
typedef struct {
    UB  id;
    UB  type;
    H   left, top, right, bottom;
    UB  flow;
    UH  text_len;
    UH  bmp_w;
    UH  bmp_h;
    /* UH text[text_len]                  follows in stream */
    /* UB bitmap[bmp_w * bmp_h * 4]       follows in stream */
} ClaritySerialFrame;
#pragma pack(pop)

#ifdef __cplusplus
}
#endif

#endif /* _CLARITY_DOC_H_ */
