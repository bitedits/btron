/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Document Model (src/apps/clarity_doc.h)
 * Prototype data model: three page formats, free-floating frames, VOBJ serial format.
 * Deliberately omits linked frames, masters, paragraph styles, TeX.
 */

#ifndef _CLARITY_DOC_H_
#define _CLARITY_DOC_H_

#include <btron/types.h>
#include <btron/dp.h>

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
    int            cursor_pos;      /* text insertion caret index      */

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
    int            page_count;      /* number of pages in document     */
    int            active_page;     /* currently focused page (0-based)*/
    int            zoom_pct;        /* view zoom percentage (25..200)  */
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
 * Canvas layout constants (72 dpi base: PostScript publishing point)
 * ================================================================ */

#define CLARITY_DPI               72   /* 72 pt/in calibrated DTP scale   */
#define CLARITY_CANVAS_MARGIN_PX  24   /* grey border around page         */
#define CLARITY_PAGE_GAP_PX       40   /* vertical space between pages    */
#define CLARITY_HANDLE_RADIUS      4   /* resize handle half-size px      */
#define CLARITY_MARGIN_GUIDE_MM   15   /* inner printable margin guide mm */

/* ================================================================
 * Mouse cursor types for frame resizing and text editing
 * ================================================================ */

typedef enum {
    CLARITY_CURSOR_ARROW = 0,
    CLARITY_CURSOR_IBEAM = 1,
    CLARITY_CURSOR_MOVE  = 2,
    CLARITY_CURSOR_NWSE  = 3,
    CLARITY_CURSOR_NESW  = 4,
    CLARITY_CURSOR_NS    = 5,
    CLARITY_CURSOR_WE    = 6,
    CLARITY_CURSOR_HAND  = 7
} ClarityCursorType;

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


/* ================================================================
 * Hit Testing and Controls
 * ================================================================ */

#define CLARITY_PERIMETER_PX  5   /* thickness of border band for moving */

typedef enum {
    CLARITY_HIT_NONE = 0,
    CLARITY_HIT_HANDLE,     /* target is one of 8 reper handles (0..7) */
    CLARITY_HIT_PERIMETER,  /* target is border band (initiates frame move) */
    CLARITY_HIT_INTERIOR    /* target is frame body (text caret / selection) */
} ClarityHitTarget;

typedef struct {
    ClarityHitTarget target;
    int frame_idx;
    int handle_idx;         /* 0..7 if CLARITY_HIT_HANDLE */
} ClarityHitInfo;

/* Text action enum */
enum {
    CLARITY_ACT_CHAR = 0,
    CLARITY_ACT_BACKSPACE,
    CLARITY_ACT_DELETE,
    CLARITY_ACT_LEFT,
    CLARITY_ACT_RIGHT,
    CLARITY_ACT_HOME,
    CLARITY_ACT_END,
    CLARITY_ACT_ENTER,
    CLARITY_ACT_UP,
    CLARITY_ACT_DOWN
};

/* Hit testing & Controls Function Prototypes */
void clarity_hittest_full(const ClarityDoc *doc, H x, H y, int ox, int oy, ClarityHitInfo *info);
int  clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy, int zoom_pct);
int  clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy);
void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy, int zoom_pct);
void clarity_move_frame(ClarityFrame *f, H dx, H dy);
void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy);

/* Text Rendering & Editing Function Prototypes */
void clarity_handle_text_action(ClarityDoc *doc, int fidx, int action, UH tc);
void clarity_render_key(ClarityDoc *doc, int fidx, UH tc);
int  clarity_text_xy_to_pos(const ClarityFrame *f, H mx, H my, int ox, int oy, int zoom_pct);
void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom, BOOL is_selected);
void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom);
ClarityDoc* clarity_get_doc(void);

#ifdef __cplusplus
}
#endif

#endif /* _CLARITY_DOC_H_ */
