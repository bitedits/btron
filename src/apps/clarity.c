/*
 * B-System (BTRON 3.20) Clarity Publishing System (src/apps/clarity.c)
 * Minimal DTP prototype: three page formats, free-floating TextFrames and
 * ImageFrames, horizontal/vertical text input, bitmap placement, VOBJ save/load.
 *
 * Deliberately omits: linked frames, master pages, paragraph styles, TeX,
 * CMYK export, ruby/warichu/kinsoku. All cultural paper format enums and
 * structures below are preserved for spec fidelity but not wired to the
 * prototype UI (they remain in the codebase for the full implementation).
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/app_menu.h>
#include <btron/tad.h>
#include <btron/vobj.h>
#include <btron/event.h>

#include "clarity_doc.h"

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <libstr.h>
extern void *Imalloc(size_t sz);
extern void  Ifree(void *ptr);
extern void *tkl_memset(void *s, int c, size_t n);
extern void *tkl_memcpy(void *dst, const void *src, size_t n);
#define malloc  Imalloc
#define free    Ifree
#define memset  tkl_memset
#define memcpy  tkl_memcpy
#endif

/* Forward declarations for companion modules */
extern void clarity_fmt_dimensions(ClarityDoc *doc);
extern int  clarity_mm_to_px(int mm);
extern void clarity_draw_page(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern int  clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy);
extern int  clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy);
extern void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy);
extern void clarity_move_frame(ClarityFrame *f, H dx, H dy);
extern void clarity_render_key(ClarityDoc *doc, int fidx, UH tc);
extern void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy);
extern void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy);
extern ER   clarity_export_save(const ClarityDoc *doc, const char *name);
extern ER   clarity_export_load(ClarityDoc *doc, ID robj_id);

/* ================================================================
 * Spec-level cultural format enums (full set preserved for fidelity)
 * Only FMT_A4 / FMT_SHIROKU / FMT_PECHA are active in the prototype.
 * ================================================================ */

typedef enum {
    CLARITY_MODE_DTP     = 0,
    CLARITY_MODE_TEX     = 1,
    CLARITY_MODE_PECHA   = 2,   /* Horizontal Tibetan Pecha (དཔེ་ཆ་) */
    CLARITY_MODE_WASOBON = 3,   /* Japanese Vertical Wasōbon (和装本・縦書き) */
    CLARITY_MODE_PREVIEW = 4
} ClarityMode;

typedef enum {
    FLOW_HORIZONTAL_LTR   = 0,
    FLOW_HORIZONTAL_PECHA = 1,  /* Tibetan horizontal with folio margin markers */
    FLOW_VERTICAL_RTL     = 2   /* Japanese Traditional Vertical RTL (縦書き) */
} ClarityTextFlowDirection;

/* Native Japanese Legacy Paper Formats (和式伝統判型) */
typedef enum {
    JP_PAPER_MINO_BAN = 0,      /* 美濃判  273 × 394 mm */
    JP_PAPER_HANSHI,             /* 半紙    242 × 333 mm */
    JP_PAPER_SHIROKU_BAN,        /* 四六判  127 × 188 mm ← prototype active */
    JP_PAPER_KIKU_BAN,           /* 菊判    150 × 218 mm */
    JP_PAPER_SHINSHO_BAN,        /* 新書判  105 × 173 mm */
    JP_PAPER_BUNKO_BAN,          /* 文庫判  105 × 148 mm */
    JP_PAPER_HOSHO_BAN,          /* 大奉書  394 × 530 mm */
    JP_PAPER_DAIFUKUCHO,         /* 大福帳  160 × 240 mm */
    JP_PAPER_KAISHI,             /* 懐紙    145 × 175 mm */
    JP_PAPER_TANZAKU,            /* 短冊     60 × 363 mm */
    JP_PAPER_SHIKISHI,           /* 色紙    242 × 272 mm */
    JP_PAPER_ORIHON,             /* 折本     80 × 260 mm */
    JP_PAPER_KANSUBON,           /* 巻子本  280 × 1200+ mm */
    JP_PAPER_WASOBON_FUKUROTOJI  /* 和装本・袋綴じ 180 × 250 mm */
} JapaneseLegacyPaperFormat;

/* Tibetan Pecha Canonical Formats (དཔེ་ཆ་) */
typedef enum {
    PECHA_SIZE_RINCHEN_TERDZO = 0, /* 大蔵経・宝庫判  650 × 140 mm */
    PECHA_SIZE_KANGYUR,            /* 標準経典判      560 × 110 mm ← prototype active */
    PECHA_SIZE_DERGE,              /* デルゲ木版大判  700 × 180 mm */
    PECHA_SIZE_POCKET_DHARMA       /* 行者暗誦判      320 ×  85 mm */
} TibetanPechaSize;

/* Full config structs (spec fidelity; not used in prototype UI) */
typedef struct {
    TibetanPechaSize pecha_size;
    int width_mm, height_mm;
    BOOL double_border_kheng_khe;
    BOOL enable_interlinear_mchan;
    char folio_left_label[32];
    int  folio_number;
    BOOL is_recto_verso;
} PechaLayoutConfig;

typedef struct {
    JapaneseLegacyPaperFormat format;
    int width_mm, height_mm;
    BOOL is_tategaki;
    BOOL enable_ruby;
    BOOL enable_warichu;
    BOOL enable_kinsoku;
    BOOL enable_tate_chu_yoko;
    int  gyo_dori_lines;
} WasobonLayoutConfig;

/* Legacy Cabinet-linked frame (spec fidelity; not used in prototype) */
typedef struct {
    int    frame_id;
    RECT   bounds;
    int    columns;
    ClarityTextFlowDirection flow;
    UW     linked_robj_id;
    int    next_frame_id;   /* linked-frame chain (omitted in prototype) */
} ClarityTextFrame;

/* TeX extern stubs (clarity_tex.c – unchanged) */
extern int clarity_tex_compile_mode(void *doc, const char *tex_source);
extern int clarity_tex_render_formula(const char *latex_math, void *dp_surface,
                                      int x, int y);

/* ================================================================
 * Menu command IDs
 * ================================================================ */

#define CMD_FILE_NEW      101
#define CMD_FILE_OPEN     102
#define CMD_FILE_SAVE     103
#define CMD_FMT_A4        201
#define CMD_FMT_SHIROKU   202
#define CMD_FMT_PECHA     203
#define CMD_INS_TEXT      301
#define CMD_INS_IMAGE     302
#define CMD_INS_FLIP_FLOW 303

/* ================================================================
 * Application state
 * ================================================================ */

static ClarityDoc    g_doc;
static WND          *g_wnd        = NULL;
static APP_MENU_BAR  g_menu;
static BOOL          g_running    = FALSE;

/* Canvas scroll / pan offset (page top-left in window client coords) */
static int g_ox = CLARITY_CANVAS_MARGIN_PX;
static int g_oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT;

/* Mouse drag state for move (when no handle selected) */
static BOOL g_drag_move     = FALSE;
static H    g_drag_prev_x   = 0;
static H    g_drag_prev_y   = 0;

/* ================================================================
 * Document helpers
 * ================================================================ */

static void doc_new(ClarityPageFmt fmt)
{
    /* Free existing bitmap payloads */
    for (int i = 0; i < g_doc.frame_count; i++) {
        if (g_doc.frames[i].bitmap) {
            free(g_doc.frames[i].bitmap);
            g_doc.frames[i].bitmap = NULL;
        }
    }
    memset(&g_doc, 0, sizeof(ClarityDoc));
    g_doc.fmt            = fmt;
    g_doc.selected_frame = -1;
    g_doc.tool           = TOOL_SELECT;
    clarity_fmt_dimensions(&g_doc);
}

static ClarityFrame *doc_add_frame(ClarityFrameType type, H x, H y, H w, H h)
{
    if (g_doc.frame_count >= CLARITY_MAX_FRAMES) return NULL;
    ClarityFrame *f = &g_doc.frames[g_doc.frame_count];
    memset(f, 0, sizeof(ClarityFrame));
    f->id           = (UB)(g_doc.frame_count + 1);
    f->type         = type;
    f->bounds.left  = x;
    f->bounds.top   = y;
    f->bounds.right = (H)(x + w);
    f->bounds.bottom = (H)(y + h);
    /* Default flow: vertical for Shiroku, horizontal for the rest */
    f->flow = (g_doc.fmt == FMT_SHIROKU) ? FLOW_V_RTL : FLOW_H_LTR;
    g_doc.frame_count++;
    g_doc.dirty = TRUE;
    return f;
}

/* ================================================================
 * Paint callback
 * ================================================================ */

static void clarity_paint(WND *wnd, GDEV *dev)
{
    if (!wnd || !dev) return;

    /* Background canvas */
    RECT all;
    all.left   = 0;
    all.top    = 0;
    all.right  = wnd->client.right  - wnd->client.left;
    all.bottom = wnd->client.bottom - wnd->client.top;
    fill_rec(dev, &all, COLOR_LTGRAY);

    /* Page and frames */
    clarity_draw_page(dev, &g_doc, g_ox, g_oy);

    for (int i = 0; i < g_doc.frame_count; i++) {
        ClarityFrame *f = &g_doc.frames[i];
        if (f->id == 0) continue;
        if (f->type == FRAME_TEXT)
            clarity_render_text(dev, f, g_ox, g_oy);
        else
            clarity_render_image(dev, f, g_ox, g_oy);
    }

    clarity_draw_frames(dev, &g_doc, g_ox, g_oy);

    /* Status strip (set before painting bar so it renders on first frame) */
    const char *fmt_name = "A4";
    if (g_doc.fmt == FMT_SHIROKU) fmt_name = "四六判 (縦書き)";
    else if (g_doc.fmt == FMT_PECHA) fmt_name = "Pecha 560×110";

    char status[128];
    snprintf(status, sizeof(status),
             "  %s | %d frames | %s",
             fmt_name, g_doc.frame_count,
             g_doc.dirty ? "modified" : "saved");
    app_menu_set_right_text(&g_menu, status);

    /* Menu bar on top */
    app_menu_paint_bar(&g_menu, dev);
    if (g_menu.active_menu >= 0)
        app_menu_paint_dropdown(&g_menu, dev);
}

/* ================================================================
 * Menu command dispatch
 * ================================================================ */

static void handle_cmd(int cmd)
{
    switch (cmd) {
        case CMD_FILE_NEW:
            doc_new(g_doc.fmt);
            break;
        case CMD_FILE_OPEN:
            /* Prototype: reload current format */
            doc_new(g_doc.fmt);
            break;
        case CMD_FILE_SAVE:
            clarity_export_save(&g_doc, "ClarityDoc");
            g_doc.dirty = FALSE;
            break;

        case CMD_FMT_A4:
            g_doc.fmt = FMT_A4;
            clarity_fmt_dimensions(&g_doc);
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_SHIROKU:
            g_doc.fmt = FMT_SHIROKU;
            clarity_fmt_dimensions(&g_doc);
            g_doc.dirty = TRUE;
            break;
        case CMD_FMT_PECHA:
            g_doc.fmt = FMT_PECHA;
            clarity_fmt_dimensions(&g_doc);
            g_doc.dirty = TRUE;
            break;

        case CMD_INS_TEXT:
            g_doc.tool = TOOL_TEXT_FRAME;
            break;
        case CMD_INS_IMAGE:
            g_doc.tool = TOOL_IMAGE_FRAME;
            break;
        case CMD_INS_FLIP_FLOW:
            if (g_doc.selected_frame >= 0) {
                ClarityFrame *f = &g_doc.frames[g_doc.selected_frame];
                f->flow = (f->flow == FLOW_H_LTR) ? FLOW_V_RTL : FLOW_H_LTR;
                g_doc.dirty = TRUE;
            }
            break;

        default:
            break;
    }
    if (g_wnd) inval_wnd(g_wnd);
}

/* ================================================================
 * Event handler
 * ================================================================ */

static void clarity_event(WND *wnd, const EVT *evt)
{
    if (!wnd || !evt) return;

    /* Menu bar intercepts mouse events first */
    if (evt->type == EV_MOUSE_MOVE) {
        H rel_x = (H)(evt->pos.x - wnd->client.left);
        H rel_y = (H)(evt->pos.y - wnd->client.top);
        if (app_menu_handle_mouse_move(&g_menu, rel_x, rel_y))
            inval_wnd(wnd);

        /* Move selected frame */
        if (g_drag_move && g_doc.selected_frame >= 0) {
            H dx = (H)(evt->pos.x - g_drag_prev_x);
            H dy = (H)(evt->pos.y - g_drag_prev_y);
            clarity_move_frame(&g_doc.frames[g_doc.selected_frame], dx, dy);
            g_drag_prev_x = evt->pos.x;
            g_drag_prev_y = evt->pos.y;
            g_doc.dirty   = TRUE;
            inval_wnd(wnd);
        }

        /* Drag-resize handle */
        if (g_doc.dragging && g_doc.selected_frame >= 0 &&
            g_doc.drag_handle >= 0) {
            clarity_resize_frame_handle(
                &g_doc.frames[g_doc.selected_frame],
                g_doc.drag_handle,
                evt->pos.x, evt->pos.y,
                g_ox, g_oy);
            g_doc.dirty = TRUE;
            inval_wnd(wnd);
        }

        /* Drag-create new frame */
        if (g_doc.dragging && g_doc.drag_handle < 0 &&
            g_doc.selected_frame < 0) {
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_BUT_DOWN) {
        H rel_x = (H)(evt->pos.x - wnd->client.left);
        H rel_y = (H)(evt->pos.y - wnd->client.top);
        int cmd = -1, sub = -1;

        if (app_menu_handle_mouse_down(&g_menu, rel_x, rel_y, &cmd, &sub)) {
            if (cmd >= 0) handle_cmd(cmd);
            inval_wnd(wnd);
            return;
        }

        /* Canvas click */
        H cx = evt->pos.x;
        H cy = evt->pos.y;

        if (g_doc.tool == TOOL_SELECT) {
            /* Check handles on selected frame first */
            int handle = -1;
            if (g_doc.selected_frame >= 0) {
                handle = clarity_hittest_handle(
                    &g_doc.frames[g_doc.selected_frame], cx, cy, g_ox, g_oy);
            }
            if (handle >= 0) {
                /* Start resize drag */
                g_doc.dragging    = TRUE;
                g_doc.drag_handle = handle;
            } else {
                int fidx = clarity_hittest_frame(&g_doc, cx, cy, g_ox, g_oy);
                g_doc.selected_frame = fidx;
                g_doc.drag_handle    = -1;
                if (fidx >= 0) {
                    /* Start move drag */
                    g_drag_move   = TRUE;
                    g_drag_prev_x = cx;
                    g_drag_prev_y = cy;
                }
            }
        } else {
            /* Frame creation: record drag start */
            g_doc.dragging     = TRUE;
            g_doc.drag_handle  = -1;
            g_doc.drag_start_x = (H)(cx - g_ox);
            g_doc.drag_start_y = (H)(cy - g_oy);
            g_doc.selected_frame = -1;
        }
        inval_wnd(wnd);
        return;
    }

    if (evt->type == EV_BUT_UP) {
        H cx = evt->pos.x;
        H cy = evt->pos.y;

        if (g_doc.dragging && g_doc.drag_handle < 0 &&
            g_doc.tool != TOOL_SELECT) {
            /* Commit new frame */
            H fx = g_doc.drag_start_x;
            H fy = g_doc.drag_start_y;
            H fw = (H)((cx - g_ox) - fx);
            H fh = (H)((cy - g_oy) - fy);
            if (fw < 0) { fx = (H)(fx + fw); fw = (H)(-fw); }
            if (fh < 0) { fy = (H)(fy + fh); fh = (H)(-fh); }
            if (fw >= 16 && fh >= 16) {
                ClarityFrameType ft = (g_doc.tool == TOOL_IMAGE_FRAME)
                                    ? FRAME_IMAGE : FRAME_TEXT;
                ClarityFrame *nf = doc_add_frame(ft, fx, fy, fw, fh);
                if (nf)
                    g_doc.selected_frame = g_doc.frame_count - 1;
            }
            g_doc.tool = TOOL_SELECT;
        }

        g_doc.dragging    = FALSE;
        g_doc.drag_handle = -1;
        g_drag_move       = FALSE;
        inval_wnd(wnd);
        return;
    }

    if (evt->type == EV_KEY_DOWN) {
        int fidx = g_doc.selected_frame;
        if (fidx >= 0 && g_doc.frames[fidx].type == FRAME_TEXT) {
            UH tc = (UH)(evt->key & 0xFFFF);
            /* F10 is handled by the TIP / Mozc layer at a higher level;
             * here we just route printable TRON code units directly. */
            clarity_render_key(&g_doc, fidx, tc);
            inval_wnd(wnd);
        }
        /* Escape: back to SELECT tool */
        if (evt->key == BTRON_KEY_ESCAPE) {
            g_doc.tool = TOOL_SELECT;
            app_menu_close(&g_menu);
            inval_wnd(wnd);
        }
        return;
    }

    if (evt->type == EV_WND_CLOSE) {
        g_running = FALSE;
        return;
    }
}

/* ================================================================
 * Menu construction
 * ================================================================ */

static void build_menu(void)
{
    app_menu_init(&g_menu, APP_MENU_STYLE_CLASSIC_3D);

    /* File */
    int fi = app_menu_add_header(&g_menu, "File", 60);
    app_menu_add_item(&g_menu, fi, "New",  "Ctrl+N", CMD_FILE_NEW,  TRUE);
    app_menu_add_item(&g_menu, fi, "Open", "Ctrl+O", CMD_FILE_OPEN, TRUE);
    app_menu_add_item(&g_menu, fi, "Save", "Ctrl+S", CMD_FILE_SAVE, TRUE);

    /* Format */
    int fmti = app_menu_add_header(&g_menu, "Format", 80);
    app_menu_add_item(&g_menu, fmti, "A4 Portrait (210\xc3\x97""297 mm)",
                      "", CMD_FMT_A4,      TRUE);
    app_menu_add_item(&g_menu, fmti,
                      "\xe5\x9b\x9b\xe5\x85\xad\xe5\x88\xa4 Shiroku (127\xc3\x97""188 mm)",
                      "", CMD_FMT_SHIROKU, TRUE);
    app_menu_add_item(&g_menu, fmti,
                      "Pecha Kangyur (560\xc3\x97""110 mm)",
                      "", CMD_FMT_PECHA,   TRUE);

    /* Insert */
    int ii = app_menu_add_header(&g_menu, "Insert", 70);
    app_menu_add_item(&g_menu, ii, "Text Frame",  "T", CMD_INS_TEXT,      TRUE);
    app_menu_add_item(&g_menu, ii, "Image Frame", "I", CMD_INS_IMAGE,     TRUE);
    app_menu_add_separator(&g_menu, ii);
    app_menu_add_item(&g_menu, ii, "Flip Text Flow (H\xe2\x86\x94V)", "F",
                      CMD_INS_FLIP_FLOW, TRUE);
}

/* ================================================================
 * Window destruction hook
 * ================================================================ */

static void destroy_clarity(WND *wnd)
{
    (void)wnd;
    g_wnd = NULL;
}

/* ================================================================
 * open_clarity_window – non-blocking window opener.
 * Creates and returns the Clarity WND* without blocking the OS scheduler.
 * Matches standard B-System convention used by all applications.
 * ================================================================ */

WND* open_clarity_window(void)
{
    if (g_wnd) {
        WND *w = get_wnd_list();
        while (w && w != g_wnd) w = w->next;
        if (w == g_wnd) {
            top_wnd(g_wnd);
            return g_wnd;
        }
    }
    g_wnd = NULL;

    doc_new(FMT_A4);

    /* Populate rich default sample frame on page */
    ClarityFrame *f = doc_add_frame(FRAME_TEXT, 24, 24, 460, 240);
    if (f) {
        const char *sample = 
            "BTRON Clarity DTP Engine (電子帳票)\n"
            "Authentic Multi-Format Publishing System\n\n"
            "Format: A4 European Portrait (210x297 mm)\n"
            "Traditional Japanese 14 Formats (四六判・和装本)\n"
            "Tibetan Sacred Pecha Geometry (560x110 mm)\n\n"
            "Pure C99 Layout & VOBJ TAD Real Body Integration";
        f->text_len = 0;
        for (int i = 0; sample[i] && f->text_len < CLARITY_TEXT_BUF - 1; i++) {
            f->text[f->text_len++] = (UH)(unsigned char)sample[i];
        }
        g_doc.selected_frame = 0;
    }

    g_wnd = opn_wnd("電子帳票 – Clarity",
                    40, 60, 900, 620,
                    WND_ATTR_TITLE | WND_ATTR_CLOSE |
                    WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!g_wnd) return NULL;

    g_wnd->paint         = clarity_paint;
    g_wnd->event_handler = clarity_event;
    g_wnd->destroy       = destroy_clarity;

    build_menu();
    top_wnd(g_wnd);
    inval_wnd(g_wnd);
    return g_wnd;
}

/* ================================================================
 * clarity_app_open – non-blocking entry point (global menu / launcher).
 * Yields immediately back to OS scheduler / desktop event loop.
 * ================================================================ */

void clarity_app_open(void)
{
    open_clarity_window();
}

/* ================================================================
 * Legacy init helper (spec API surface; kept for header compatibility)
 * ================================================================ */

void clarity_init(ClarityDoc *doc)
{
    if (!doc) return;
    memset(doc, 0, sizeof(ClarityDoc));
    doc->fmt             = FMT_A4;
    doc->selected_frame  = -1;
    doc->tool            = TOOL_SELECT;
    clarity_fmt_dimensions(doc);
}

/* Legacy format-switch helpers (spec API surface; not used by prototype UI) */
void clarity_set_pecha_mode(ClarityDoc *doc, TibetanPechaSize size)
{
    if (!doc) return;
    doc->fmt = FMT_PECHA;
    (void)size; /* full implementation selects among 4 Pecha sizes */
    clarity_fmt_dimensions(doc);
}

void clarity_set_japanese_legacy_format(ClarityDoc *doc,
                                        JapaneseLegacyPaperFormat fmt,
                                        BOOL vertical)
{
    if (!doc) return;
    /* Prototype: only activate Shiroku-ban; others map to A4 */
    if (fmt == JP_PAPER_SHIROKU_BAN) {
        doc->fmt = FMT_SHIROKU;
    } else {
        doc->fmt = FMT_A4;
    }
    (void)vertical;
    clarity_fmt_dimensions(doc);
}

int clarity_link_cabinet_source(ClarityDoc *doc, int frame_idx, UW robj_id)
{
    /* Cabinet-linked frames omitted in prototype; stub preserved */
    (void)doc; (void)frame_idx; (void)robj_id;
    return 0;
}

void clarity_render_page(ClarityDoc *doc, int page_num)
{
    /* Multi-page rendering omitted in prototype; stub preserved */
    (void)doc; (void)page_num;
}
