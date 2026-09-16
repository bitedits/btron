/*
 * B-System (BTRON 3.20) Clarity Publishing System (src/apps/clarity.c)
 * Professional DTP publishing environment:
 *  - Interactive frame creation, selection, move, and 8-handle resizing
 *  - Dynamic mouse pointer changes (SIZENWSE, SIZENESW, SIZENS, SIZEWE, IBEAM, MOVE)
 *  - Horizontal & Vertical 3D Scrollbars with draggable elevator thumbs & steppers
 *  - View menu with Zoom In (+), Zoom Out (-), 100% Actual, Fit to Window, presets
 *  - Calibrated paper sizing & multi-page support with Page Break Separators
 *  - Real-time text typing with insertion caret, backspace, enter, and arrows
 *  - Format selection (A4, Shiroku, Pecha) and VOBJ TAD Real Body export
 */

#include "clarity_doc.h"
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/app_menu.h>
#include <btron/tip.h>
#include <btron/event.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__) || defined(__linux__)
#if defined(__has_include)
#if __has_include(<SDL2/SDL.h>)
#include <SDL2/SDL.h>
#define HAVE_CLARITY_SDL 1
#elif __has_include(<SDL.h>)
#include <SDL.h>
#define HAVE_CLARITY_SDL 1
#endif
#endif
#endif

/* ------------------------------------------------------------------ */
/* Layout and Render module forward declarations                       */
/* ------------------------------------------------------------------ */

extern void clarity_fmt_dimensions(ClarityDoc *doc);
extern int  clarity_mm_to_px(int mm, int zoom_pct);
extern void clarity_draw_page(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy);
extern int  clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy);
extern int  clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy, int zoom_pct);
extern void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy, int zoom_pct);
extern void clarity_move_frame(ClarityFrame *f, H dx, H dy);

extern void clarity_render_key(ClarityDoc *doc, int fidx, UH tc);
extern void clarity_handle_text_action(ClarityDoc *doc, int fidx, int action, UH tc);
extern void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom, BOOL is_selected);
extern void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom);
extern ER   clarity_export_save(const ClarityDoc *doc, const char *name);
extern ER   clarity_export_load(ClarityDoc *doc, ID robj_id);

/* Text action enum matching clarity_render.c */
enum {
    ACT_CHAR = 0,
    ACT_BACKSPACE,
    ACT_DELETE,
    ACT_LEFT,
    ACT_RIGHT,
    ACT_HOME,
    ACT_END,
    ACT_ENTER,
    ACT_UP,
    ACT_DOWN
};

/* ------------------------------------------------------------------ */
/* UI Constants & Commands                                             */
/* ------------------------------------------------------------------ */

#define SCROLLBAR_SIZE           16
#define CLARITY_STEP_SCROLL      32

enum {
    /* File */
    CMD_FILE_NEW = 100,
    CMD_FILE_OPEN,
    CMD_FILE_SAVE,

    /* View / Zoom */
    CMD_VIEW_ZOOM_IN = 200,
    CMD_VIEW_ZOOM_OUT,
    CMD_VIEW_ACTUAL,
    CMD_VIEW_FIT,
    CMD_VIEW_ZOOM_50,
    CMD_VIEW_ZOOM_75,
    CMD_VIEW_ZOOM_100,
    CMD_VIEW_ZOOM_125,
    CMD_VIEW_ZOOM_150,

    /* Format */
    CMD_FMT_A4 = 300,
    CMD_FMT_SHIROKU,
    CMD_FMT_PECHA,
    CMD_PAGE_ADD,
    CMD_PAGE_REMOVE,

    /* Insert */
    CMD_INS_TEXT = 400,
    CMD_INS_IMAGE,
    CMD_INS_FLIP_FLOW
};

/* ------------------------------------------------------------------ */
/* Application state                                                   */
/* ------------------------------------------------------------------ */

static ClarityDoc    g_doc;
static WND          *g_wnd        = NULL;
static APP_MENU_BAR  g_menu;

/* Scroll state (offsets in canvas space) */
static int  g_scroll_x   = 0;
static int  g_scroll_y   = 0;

/* Scrollbar dragging */
static BOOL g_sb_drag_v  = FALSE;
static BOOL g_sb_drag_h  = FALSE;
static int  g_sb_start_y = 0;
static int  g_sb_start_x = 0;
static int  g_sb_orig_y  = 0;
static int  g_sb_orig_x  = 0;

/* Frame drag move state */
static BOOL g_drag_move   = FALSE;
static H    g_drag_prev_x = 0;
static H    g_drag_prev_y = 0;

/* Current active mouse cursor type */
static ClarityCursorType g_curr_cursor = CLARITY_CURSOR_ARROW;

/* Forward declarations */
static void clarity_paint(WND *wnd, GDEV *dev);
static void clarity_event(WND *wnd, const EVT *evt);
static void build_menu(void);
static void doc_new(ClarityPageFmt fmt);
static void clarity_set_cursor(ClarityCursorType type);
static void clarity_fit_window(void);

/* ------------------------------------------------------------------ */
/* Dynamic Mouse Cursor System                                         */
/* ------------------------------------------------------------------ */

static void clarity_set_cursor(ClarityCursorType type)
{
    if (g_curr_cursor == type) return;
    g_curr_cursor = type;

#ifdef HAVE_CLARITY_SDL
#if defined(__GNUC__) || defined(__clang__)
__attribute__((weak)) SDL_Cursor* SDL_CreateSystemCursor(SDL_SystemCursor id);
__attribute__((weak)) void        SDL_SetCursor(SDL_Cursor *cursor);
#endif

    if (!SDL_CreateSystemCursor || !SDL_SetCursor) return;

    static SDL_Cursor *s_cursors[8] = { NULL };
    static BOOL s_inited = FALSE;
    if (!s_inited) {
        s_cursors[CLARITY_CURSOR_ARROW] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
        s_cursors[CLARITY_CURSOR_IBEAM] = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);
        s_cursors[CLARITY_CURSOR_MOVE]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEALL);
        s_cursors[CLARITY_CURSOR_NWSE]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENWSE);
        s_cursors[CLARITY_CURSOR_NESW]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENESW);
        s_cursors[CLARITY_CURSOR_NS]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZENS);
        s_cursors[CLARITY_CURSOR_WE]    = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
        s_cursors[CLARITY_CURSOR_HAND]  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
        s_inited = TRUE;
    }
    if (type >= 0 && type < 8 && s_cursors[type]) {
        SDL_SetCursor(s_cursors[type]);
    }
#endif
}

/* ------------------------------------------------------------------ */
/* Document helpers                                                    */
/* ------------------------------------------------------------------ */

static void doc_new(ClarityPageFmt fmt)
{
    for (int i = 0; i < g_doc.frame_count; i++) {
        if (g_doc.frames[i].bitmap) {
            free(g_doc.frames[i].bitmap);
            g_doc.frames[i].bitmap = NULL;
        }
    }
    memset(&g_doc, 0, sizeof(ClarityDoc));
    g_doc.fmt            = fmt;
    g_doc.page_count     = 2;   /* 2 pages by default */
    g_doc.zoom_pct       = 75;  /* 75% default fit */
    g_doc.selected_frame = -1;
    g_doc.tool           = TOOL_SELECT;
    clarity_fmt_dimensions(&g_doc);

    g_scroll_x = 0;
    g_scroll_y = 0;
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
    f->flow = (g_doc.fmt == FMT_SHIROKU) ? FLOW_V_RTL : FLOW_H_LTR;
    f->cursor_pos   = 0;
    g_doc.frame_count++;
    g_doc.dirty = TRUE;
    return f;
}

/* ------------------------------------------------------------------ */
/* Scrollbar Rendering (Canonical BTRON 3D Steppers & Elevators)       */
/* ------------------------------------------------------------------ */

static void paint_scrollbar_v(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_h)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)x, (H)(y + h));

    /* Up Button */
    RECT up_btn = { (H)x, (H)y, (H)(x + w), (H)(y + 16) };
    fill_rec(dev, &up_btn, COLOR_LTGRAY);
    drw_rec(dev, &up_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 4),  (H)(y + 11));
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 12), (H)(y + 11));
    drw_lin(dev, (H)(x + 4), (H)(y + 11), (H)(x + 12), (H)(y + 11));

    /* Down Button */
    int dy_b = y + h - 16;
    RECT dn_btn = { (H)x, (H)dy_b, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &dn_btn, COLOR_LTGRAY);
    drw_rec(dev, &dn_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 12), (H)(dy_b + 5));
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));
    drw_lin(dev, (H)(x + 12), (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));

    /* Elevator Thumb */
    int track_y = y + 16;
    int track_h = h - 32;
    if (track_h > 20) {
        int thumb_h = (max_scroll > 0) ? (track_h * view_h) / (view_h + max_scroll) : track_h;
        if (thumb_h < 16) thumb_h = 16;
        if (thumb_h > track_h) thumb_h = track_h;

        int thumb_y = track_y;
        if (max_scroll > 0) {
            thumb_y = track_y + (scroll * (track_h - thumb_h)) / max_scroll;
        }

        RECT thumb = { (H)(x + 1), (H)thumb_y, (H)(x + w - 1), (H)(thumb_y + thumb_h) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        set_col(dev, COLOR_WHITE, COLOR_GRAY);
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + w - 3), (H)(thumb_y + 1));
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + 2), (H)(thumb_y + thumb_h - 2));
    }
}

static void paint_scrollbar_h(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_w)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)(x + w), (H)y);

    /* Left Button */
    RECT lt_btn = { (H)x, (H)y, (H)(x + 16), (H)(y + h) };
    fill_rec(dev, &lt_btn, COLOR_LTGRAY);
    drw_rec(dev, &lt_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 4));
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 12));
    drw_lin(dev, (H)(x + 11), (H)(y + 4), (H)(x + 11), (H)(y + 12));

    /* Right Button */
    int rx_b = x + w - 16;
    RECT rt_btn = { (H)rx_b, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &rt_btn, COLOR_LTGRAY);
    drw_rec(dev, &rt_btn);
    set_col(dev, COLOR_BLACK, COLOR_LTGRAY);
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 12), (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 5),  (H)(y + 12));

    /* Elevator Thumb */
    int track_x = x + 16;
    int track_w = w - 32;
    if (track_w > 20) {
        int thumb_w = (max_scroll > 0) ? (track_w * view_w) / (view_w + max_scroll) : track_w;
        if (thumb_w < 16) thumb_w = 16;
        if (thumb_w > track_w) thumb_w = track_w;

        int thumb_x = track_x;
        if (max_scroll > 0) {
            thumb_x = track_x + (scroll * (track_w - thumb_w)) / max_scroll;
        }

        RECT thumb = { (H)thumb_x, (H)(y + 1), (H)(thumb_x + thumb_w), (H)(y + h - 1) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        set_col(dev, COLOR_WHITE, COLOR_GRAY);
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + thumb_w - 2), (H)(y + 2));
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + 1), (H)(y + h - 3));
    }
}

/* ------------------------------------------------------------------ */
/* Viewport and Fit Window Calculation                                 */
/* ------------------------------------------------------------------ */

static void get_viewport_and_content_bounds(int *out_vw, int *out_vh,
                                            int *out_cw, int *out_ch,
                                            int *out_max_x, int *out_max_y)
{
    int win_w = g_wnd ? (g_wnd->client.right - g_wnd->client.left) : 900;
    int win_h = g_wnd ? (g_wnd->client.bottom - g_wnd->client.top) : 600;

    int vw = win_w - SCROLLBAR_SIZE;
    int vh = win_h - APP_MENU_BAR_HEIGHT - SCROLLBAR_SIZE;
    if (vw < 100) vw = 100;
    if (vh < 100) vh = 100;

    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;
    int pw = clarity_mm_to_px(g_doc.page_w_mm, zoom);
    int ph = clarity_mm_to_px(g_doc.page_h_mm, zoom);
    int p_gap = (CLARITY_PAGE_GAP_PX * zoom) / 100;
    if (p_gap < 24) p_gap = 24;

    int pages = g_doc.page_count > 0 ? g_doc.page_count : 1;

    int cw = pw + CLARITY_CANVAS_MARGIN_PX * 2;
    int ch = ph * pages + p_gap * (pages - 1) + CLARITY_CANVAS_MARGIN_PX * 2;

    int max_x = (cw > vw) ? (cw - vw) : 0;
    int max_y = (ch > vh) ? (ch - vh) : 0;

    if (out_vw) *out_vw = vw;
    if (out_vh) *out_vh = vh;
    if (out_cw) *out_cw = cw;
    if (out_ch) *out_ch = ch;
    if (out_max_x) *out_max_x = max_x;
    if (out_max_y) *out_max_y = max_y;
}

static void clarity_fit_window(void)
{
    int win_w = g_wnd ? (g_wnd->client.right - g_wnd->client.left) : 900;
    int win_h = g_wnd ? (g_wnd->client.bottom - g_wnd->client.top) : 600;

    int avail_w = win_w - SCROLLBAR_SIZE - CLARITY_CANVAS_MARGIN_PX * 2;
    int avail_h = win_h - APP_MENU_BAR_HEIGHT - SCROLLBAR_SIZE - CLARITY_CANVAS_MARGIN_PX * 2;
    if (avail_w < 100) avail_w = 100;
    if (avail_h < 100) avail_h = 100;

    /* Base unzoomed sizes */
    int base_pw = clarity_mm_to_px(g_doc.page_w_mm, 100);
    int base_ph = clarity_mm_to_px(g_doc.page_h_mm, 100);
    if (base_pw <= 0) base_pw = 1;
    if (base_ph <= 0) base_ph = 1;

    int fit_w_pct = (avail_w * 100) / base_pw;
    int fit_h_pct = (avail_h * 100) / base_ph;
    int fit_pct = (fit_w_pct < fit_h_pct) ? fit_w_pct : fit_h_pct;

    if (fit_pct < 25) fit_pct = 25;
    if (fit_pct > 150) fit_pct = 150;

    g_doc.zoom_pct = fit_pct;
    g_scroll_x = 0;
    g_scroll_y = 0;
}

/* ------------------------------------------------------------------ */
/* Paint callback                                                      */
/* ------------------------------------------------------------------ */

static void clarity_paint(WND *wnd, GDEV *dev)
{
    if (!wnd || !dev) return;

    int win_w = wnd->client.right  - wnd->client.left;
    int win_h = wnd->client.bottom - wnd->client.top;

    int vw = 0, vh = 0, cw = 0, ch = 0, max_x = 0, max_y = 0;
    get_viewport_and_content_bounds(&vw, &vh, &cw, &ch, &max_x, &max_y);

    if (g_scroll_x > max_x) g_scroll_x = max_x;
    if (g_scroll_x < 0) g_scroll_x = 0;
    if (g_scroll_y > max_y) g_scroll_y = max_y;
    if (g_scroll_y < 0) g_scroll_y = 0;

    int ox = CLARITY_CANVAS_MARGIN_PX - g_scroll_x;
    int oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT - g_scroll_y;

    /* 1. Canvas Background Plate */
    RECT canvas_viewport = { 0, APP_MENU_BAR_HEIGHT, (H)vw, (H)(APP_MENU_BAR_HEIGHT + vh) };
    fill_rec(dev, &canvas_viewport, COLOR_LTGRAY);

    /* 2. Pages (Sheets, Dropshadows, Margin Guides, Separators) */
    clarity_draw_page(dev, &g_doc, ox, oy);

    /* 3. Text & Image Frames with Zoom */
    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;
    for (int i = 0; i < g_doc.frame_count; i++) {
        ClarityFrame *f = &g_doc.frames[i];
        if (f->id == 0) continue;
        BOOL is_sel = (i == g_doc.selected_frame);
        if (f->type == FRAME_TEXT) {
            clarity_render_text(dev, f, ox, oy, zoom, is_sel);
        } else {
            clarity_render_image(dev, f, ox, oy, zoom);
        }
    }

    /* 4. Frame Outlines & 8-point Resize Handles */
    clarity_draw_frames(dev, &g_doc, ox, oy);

    /* 5. Right Margin Vertical Scrollbar */
    paint_scrollbar_v(dev, vw, APP_MENU_BAR_HEIGHT, SCROLLBAR_SIZE, vh,
                      g_scroll_y, max_y, vh);

    /* 6. Bottom Horizontal Scrollbar */
    paint_scrollbar_h(dev, 0, APP_MENU_BAR_HEIGHT + vh, vw, SCROLLBAR_SIZE,
                      g_scroll_x, max_x, vw);

    /* 7. Bottom-Right Corner Filler */
    RECT corner = { (H)vw, (H)(APP_MENU_BAR_HEIGHT + vh), (H)win_w, (H)win_h };
    fill_rec(dev, &corner, COLOR_LTGRAY);
    drw_rec(dev, &corner);

    /* 8. Menu Bar & Status Strip */
    const char *fmt_name = "A4";
    if (g_doc.fmt == FMT_SHIROKU) fmt_name = "四六判";
    else if (g_doc.fmt == FMT_PECHA) fmt_name = "Pecha";

    char status[128];
    snprintf(status, sizeof(status), "  %s | %d%% | %d 頁 | %d 個 | %s",
             fmt_name, g_doc.zoom_pct, g_doc.page_count, g_doc.frame_count,
             g_doc.dirty ? "modified" : "saved");
    app_menu_set_right_text(&g_menu, status);

    app_menu_paint_bar(&g_menu, dev);
    if (g_menu.active_menu >= 0) {
        app_menu_paint_dropdown(&g_menu, dev);
    }
}

/* ------------------------------------------------------------------ */
/* Menu Command Dispatch                                               */
/* ------------------------------------------------------------------ */

static void handle_cmd(int cmd)
{
    switch (cmd) {
        case CMD_FILE_NEW:
            doc_new(g_doc.fmt);
            break;
        case CMD_FILE_OPEN:
            doc_new(g_doc.fmt);
            break;
        case CMD_FILE_SAVE:
            clarity_export_save(&g_doc, "ClarityDoc");
            g_doc.dirty = FALSE;
            break;

        /* View / Zoom */
        case CMD_VIEW_ZOOM_IN:
            if (g_doc.zoom_pct < 200) g_doc.zoom_pct += 25;
            break;
        case CMD_VIEW_ZOOM_OUT:
            if (g_doc.zoom_pct > 25) g_doc.zoom_pct -= 25;
            break;
        case CMD_VIEW_ACTUAL:
        case CMD_VIEW_ZOOM_100:
            g_doc.zoom_pct = 100;
            break;
        case CMD_VIEW_FIT:
            clarity_fit_window();
            break;
        case CMD_VIEW_ZOOM_50:
            g_doc.zoom_pct = 50;
            break;
        case CMD_VIEW_ZOOM_75:
            g_doc.zoom_pct = 75;
            break;
        case CMD_VIEW_ZOOM_125:
            g_doc.zoom_pct = 125;
            break;
        case CMD_VIEW_ZOOM_150:
            g_doc.zoom_pct = 150;
            break;

        /* Formats */
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

        /* Pages */
        case CMD_PAGE_ADD:
            if (g_doc.page_count < 8) {
                g_doc.page_count++;
                g_doc.dirty = TRUE;
            }
            break;
        case CMD_PAGE_REMOVE:
            if (g_doc.page_count > 1) {
                g_doc.page_count--;
                g_doc.dirty = TRUE;
            }
            break;

        /* Insert */
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

/* ------------------------------------------------------------------ */
/* Event handler                                                       */
/* ------------------------------------------------------------------ */

static void clarity_event(WND *wnd, const EVT *evt)
{
    if (!wnd || !evt) return;

    H rel_x = (H)(evt->pos.x - wnd->client.left);
    H rel_y = (H)(evt->pos.y - wnd->client.top);

    int vw = 0, vh = 0, cw = 0, ch = 0, max_x = 0, max_y = 0;
    get_viewport_and_content_bounds(&vw, &vh, &cw, &ch, &max_x, &max_y);

    int ox = CLARITY_CANVAS_MARGIN_PX - g_scroll_x;
    int oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT - g_scroll_y;
    int zoom = g_doc.zoom_pct > 0 ? g_doc.zoom_pct : 100;

    /* ── 1. Mouse Move ─────────────────────────────────────────────── */
    if (evt->type == EV_MOUSE_MOVE) {

        /* Menu bar hover */
        if (app_menu_handle_mouse_move(&g_menu, rel_x, rel_y)) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
            inval_wnd(wnd);
            return;
        }

        /* Vertical Scrollbar Dragging */
        if (g_sb_drag_v) {
            int track_h = vh - 32;
            int dy = evt->pos.y - g_sb_start_y;
            if (track_h > 0 && max_y > 0) {
                g_scroll_y = g_sb_orig_y + (dy * max_y) / track_h;
                if (g_scroll_y < 0) g_scroll_y = 0;
                if (g_scroll_y > max_y) g_scroll_y = max_y;
            }
            inval_wnd(wnd);
            return;
        }

        /* Horizontal Scrollbar Dragging */
        if (g_sb_drag_h) {
            int track_w = vw - 32;
            int dx = evt->pos.x - g_sb_start_x;
            if (track_w > 0 && max_x > 0) {
                g_scroll_x = g_sb_orig_x + (dx * max_x) / track_w;
                if (g_scroll_x < 0) g_scroll_x = 0;
                if (g_scroll_x > max_x) g_scroll_x = max_x;
            }
            inval_wnd(wnd);
            return;
        }

        /* Frame Move Dragging */
        if (g_drag_move && g_doc.selected_frame >= 0) {
            H dx = (H)(((rel_x - g_drag_prev_x) * 100) / zoom);
            H dy = (H)(((rel_y - g_drag_prev_y) * 100) / zoom);
            if (dx != 0 || dy != 0) {
                clarity_move_frame(&g_doc.frames[g_doc.selected_frame], dx, dy);
                g_drag_prev_x = rel_x;
                g_drag_prev_y = rel_y;
                g_doc.dirty   = TRUE;
            }
            clarity_set_cursor(CLARITY_CURSOR_MOVE);
            inval_wnd(wnd);
            return;
        }

        /* Frame Handle Resize Dragging */
        if (g_doc.dragging && g_doc.selected_frame >= 0 && g_doc.drag_handle >= 0) {
            clarity_resize_frame_handle(
                &g_doc.frames[g_doc.selected_frame],
                g_doc.drag_handle,
                rel_x, rel_y,
                ox, oy, zoom);
            g_doc.dirty = TRUE;
            inval_wnd(wnd);
            return;
        }

        /* Frame Creation Dragging */
        if (g_doc.dragging && g_doc.drag_handle < 0 && g_doc.selected_frame < 0) {
            clarity_set_cursor(CLARITY_CURSOR_MOVE);
            inval_wnd(wnd);
            return;
        }

        /* Dynamic Cursor Update on Hover */
        if (rel_y < APP_MENU_BAR_HEIGHT) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
        } else if (rel_x >= vw || rel_y >= APP_MENU_BAR_HEIGHT + vh) {
            clarity_set_cursor(CLARITY_CURSOR_ARROW);
        } else {
            /* Inside Canvas Viewport */
            ClarityHitInfo hinfo;
            clarity_hittest_full(&g_doc, rel_x, rel_y, ox, oy, &hinfo);

            if (hinfo.target == CLARITY_HIT_HANDLE) {
                switch (hinfo.handle_idx) {
                    case 0:
                    case 4: clarity_set_cursor(CLARITY_CURSOR_NWSE); break;
                    case 2:
                    case 6: clarity_set_cursor(CLARITY_CURSOR_NESW); break;
                    case 1:
                    case 5: clarity_set_cursor(CLARITY_CURSOR_NS);   break;
                    case 3:
                    case 7: clarity_set_cursor(CLARITY_CURSOR_WE);   break;
                    default: clarity_set_cursor(CLARITY_CURSOR_ARROW); break;
                }
            } else if (hinfo.target == CLARITY_HIT_PERIMETER) {
                clarity_set_cursor(CLARITY_CURSOR_MOVE);
            } else if (hinfo.target == CLARITY_HIT_INTERIOR) {
                if (g_doc.frames[hinfo.frame_idx].type == FRAME_TEXT) {
                    clarity_set_cursor(CLARITY_CURSOR_IBEAM);
                } else {
                    clarity_set_cursor(CLARITY_CURSOR_ARROW);
                }
            } else {
                clarity_set_cursor(CLARITY_CURSOR_ARROW);
            }
        }
        return;
    }

    /* ── 2. Mouse Button Down ───────────────────────────────────────── */
    if (evt->type == EV_BUT_DOWN) {

        /* Menu Bar clicks */
        int cmd = -1, sub = -1;
        if (app_menu_handle_mouse_down(&g_menu, rel_x, rel_y, &cmd, &sub)) {
            if (cmd >= 0) handle_cmd(cmd);
            inval_wnd(wnd);
            return;
        }

        /* Vertical Scrollbar clicks */
        if (rel_x >= vw && rel_x < vw + SCROLLBAR_SIZE &&
            rel_y >= APP_MENU_BAR_HEIGHT && rel_y < APP_MENU_BAR_HEIGHT + vh) {
            int sy = rel_y - APP_MENU_BAR_HEIGHT;
            if (sy < 16) {
                /* Up button */
                g_scroll_y -= CLARITY_STEP_SCROLL;
                if (g_scroll_y < 0) g_scroll_y = 0;
            } else if (sy >= vh - 16) {
                /* Down button */
                g_scroll_y += CLARITY_STEP_SCROLL;
                if (g_scroll_y > max_y) g_scroll_y = max_y;
            } else {
                /* Track or Thumb */
                int track_h = vh - 32;
                int thumb_h = (max_y > 0) ? (track_h * vh) / (vh + max_y) : track_h;
                if (thumb_h < 16) thumb_h = 16;
                if (thumb_h > track_h) thumb_h = track_h;

                int thumb_y = 16 + (max_y > 0 ? (g_scroll_y * (track_h - thumb_h)) / max_y : 0);
                if (sy >= thumb_y && sy <= thumb_y + thumb_h) {
                    g_sb_drag_v  = TRUE;
                    g_sb_start_y = evt->pos.y;
                    g_sb_orig_y  = g_scroll_y;
                } else if (sy < thumb_y) {
                    g_scroll_y -= vh;
                    if (g_scroll_y < 0) g_scroll_y = 0;
                } else {
                    g_scroll_y += vh;
                    if (g_scroll_y > max_y) g_scroll_y = max_y;
                }
            }
            inval_wnd(wnd);
            return;
        }

        /* Horizontal Scrollbar clicks */
        if (rel_y >= APP_MENU_BAR_HEIGHT + vh && rel_y < APP_MENU_BAR_HEIGHT + vh + SCROLLBAR_SIZE &&
            rel_x >= 0 && rel_x < vw) {
            int sx = rel_x;
            if (sx < 16) {
                /* Left button */
                g_scroll_x -= CLARITY_STEP_SCROLL;
                if (g_scroll_x < 0) g_scroll_x = 0;
            } else if (sx >= vw - 16) {
                /* Right button */
                g_scroll_x += CLARITY_STEP_SCROLL;
                if (g_scroll_x > max_x) g_scroll_x = max_x;
            } else {
                /* Track or Thumb */
                int track_w = vw - 32;
                int thumb_w = (max_x > 0) ? (track_w * vw) / (vw + max_x) : track_w;
                if (thumb_w < 16) thumb_w = 16;
                if (thumb_w > track_w) thumb_w = track_w;

                int thumb_x = 16 + (max_x > 0 ? (g_scroll_x * (track_w - thumb_w)) / max_x : 0);
                if (sx >= thumb_x && sx <= thumb_x + thumb_w) {
                    g_sb_drag_h  = TRUE;
                    g_sb_start_x = evt->pos.x;
                    g_sb_orig_x  = g_scroll_x;
                } else if (sx < thumb_x) {
                    g_scroll_x -= vw;
                    if (g_scroll_x < 0) g_scroll_x = 0;
                } else {
                    g_scroll_x += vw;
                    if (g_scroll_x > max_x) g_scroll_x = max_x;
                }
            }
            inval_wnd(wnd);
            return;
        }

        /* Canvas Click (using client-relative coordinates) */
        H cx = rel_x;
        H cy = rel_y;

        if (g_doc.tool == TOOL_SELECT) {
            ClarityHitInfo hinfo;
            clarity_hittest_full(&g_doc, cx, cy, ox, oy, &hinfo);

            if (hinfo.target == CLARITY_HIT_HANDLE) {
                g_doc.dragging    = TRUE;
                g_doc.drag_handle = hinfo.handle_idx;
                inval_wnd(wnd);
                return;
            }

            if (hinfo.target == CLARITY_HIT_PERIMETER) {
                g_doc.selected_frame = hinfo.frame_idx;
                g_drag_move   = TRUE;
                g_drag_prev_x = cx;
                g_drag_prev_y = cy;
                inval_wnd(wnd);
                return;
            }

            if (hinfo.target == CLARITY_HIT_INTERIOR) {
                g_doc.selected_frame = hinfo.frame_idx;
                ClarityFrame *f = &g_doc.frames[hinfo.frame_idx];
                if (f->type == FRAME_TEXT) {
                    f->cursor_pos = clarity_text_xy_to_pos(f, cx, cy, ox, oy, zoom);
                }
                inval_wnd(wnd);
                return;
            }

            /* Canvas background click deselects */
            g_doc.selected_frame = -1;
            inval_wnd(wnd);
            return;
        }

        /* Tool active: start drag-creation */
        if (g_doc.tool == TOOL_TEXT_FRAME || g_doc.tool == TOOL_IMAGE_FRAME) {
            g_doc.dragging     = TRUE;
            g_doc.drag_start_x = (H)(((cx - ox) * 100) / zoom);
            g_doc.drag_start_y = (H)(((cy - oy) * 100) / zoom);
            g_doc.drag_handle  = -1;
            return;
        }
        return;
    }

    /* ── 3. Mouse Button Up ─────────────────────────────────────────── */
    if (evt->type == EV_BUT_UP) {
        g_sb_drag_v = FALSE;
        g_sb_drag_h = FALSE;
        g_drag_move = FALSE;
        g_doc.drag_handle = -1;

        if (g_doc.dragging &&
            (g_doc.tool == TOOL_TEXT_FRAME || g_doc.tool == TOOL_IMAGE_FRAME)) {
            g_doc.dragging = FALSE;
            H cur_unzoomed_x = (H)(((rel_x - ox) * 100) / zoom);
            H cur_unzoomed_y = (H)(((rel_y - oy) * 100) / zoom);
            H fx = g_doc.drag_start_x;
            H fy = g_doc.drag_start_y;
            H fw = (H)(cur_unzoomed_x - fx);
            H fh = (H)(cur_unzoomed_y - fy);
            if (fw < 0) { fx += fw; fw = -fw; }
            if (fh < 0) { fy += fh; fh = -fh; }
            if (fw < 32) fw = 32;
            if (fh < 24) fh = 24;

            ClarityFrameType ft = (g_doc.tool == TOOL_TEXT_FRAME)
                                  ? FRAME_TEXT : FRAME_IMAGE;
            ClarityFrame *nf = doc_add_frame(ft, fx, fy, fw, fh);
            if (nf) {
                g_doc.selected_frame = g_doc.frame_count - 1;
            }
            g_doc.tool = TOOL_SELECT;
            inval_wnd(wnd);
            return;
        }

        g_doc.dragging = FALSE;
        return;
    }

    /* ── 4. Key Down / Typing ───────────────────────────────────────── */
    if (evt->type == EV_KEY_DOWN) {
        UW key = evt->key;
        uint16_t mod = (uint16_t)(uintptr_t)evt->data;
        BOOL ctrl = (mod & BTRON_KMOD_CTRL) != 0;
        BOOL shift = (mod & BTRON_KMOD_SHIFT) != 0;

        /* Menu navigation shortcuts */
        int menu_cmd = 0;
        if (app_menu_handle_key(&g_menu, key, mod, &menu_cmd)) {
            if (menu_cmd != 0) handle_cmd(menu_cmd);
            inval_wnd(wnd);
            return;
        }

        /* Ctrl shortcuts */
        if (ctrl) {
            if (key == '=' || key == '+') {
                handle_cmd(CMD_VIEW_ZOOM_IN);
                return;
            } else if (key == '-' || key == '_') {
                handle_cmd(CMD_VIEW_ZOOM_OUT);
                return;
            } else if (key == '0') {
                handle_cmd(CMD_VIEW_ACTUAL);
                return;
            } else if (key == 'f' || key == 'F') {
                handle_cmd(CMD_VIEW_FIT);
                return;
            } else if (key == 's' || key == 'S') {
                handle_cmd(CMD_FILE_SAVE);
                return;
            } else if (key == 'n' || key == 'N') {
                handle_cmd(CMD_FILE_NEW);
                return;
            }
        }

        /* Check Mozc / Tibetan TIP input method */
        char tip_buf[128] = "";
        if (tip_process_key(key, mod, tip_buf, sizeof(tip_buf))) {
            if (tip_buf[0] != '\0' && g_doc.selected_frame >= 0) {
                for (int i = 0; tip_buf[i]; i++) {
                    clarity_render_key(&g_doc, g_doc.selected_frame, (UH)(unsigned char)tip_buf[i]);
                }
                inval_wnd(wnd);
            }
            return;
        }

        /* Escape key: clear selection or tool */
        if (key == BTRON_KEY_ESCAPE) {
            g_doc.tool = TOOL_SELECT;
            app_menu_close(&g_menu);
            inval_wnd(wnd);
            return;
        }

        /* Interactive Text Entry into Selected TextFrame */
        int fidx = g_doc.selected_frame;
        if (fidx >= 0 && g_doc.frames[fidx].type == FRAME_TEXT) {
            if (key == BTRON_KEY_BACKSPACE || key == 0x08) {
                clarity_handle_text_action(&g_doc, fidx, ACT_BACKSPACE, 0);
            } else if (key == BTRON_KEY_DELETE || key == 0x7F) {
                clarity_handle_text_action(&g_doc, fidx, ACT_DELETE, 0);
            } else if (key == BTRON_KEY_RETURN || key == BTRON_KEY_KP_ENTER || key == '\r' || key == '\n') {
                clarity_handle_text_action(&g_doc, fidx, ACT_ENTER, 0);
            } else if (key == BTRON_KEY_LEFT) {
                clarity_handle_text_action(&g_doc, fidx, ACT_LEFT, 0);
            } else if (key == BTRON_KEY_RIGHT) {
                clarity_handle_text_action(&g_doc, fidx, ACT_RIGHT, 0);
            } else if (key == BTRON_KEY_UP) {
                clarity_handle_text_action(&g_doc, fidx, ACT_UP, 0);
            } else if (key == BTRON_KEY_DOWN) {
                clarity_handle_text_action(&g_doc, fidx, ACT_DOWN, 0);
            } else if (key == BTRON_KEY_HOME) {
                clarity_handle_text_action(&g_doc, fidx, ACT_HOME, 0);
            } else if (key == BTRON_KEY_END) {
                clarity_handle_text_action(&g_doc, fidx, ACT_END, 0);
            } else if (key >= 32 && key <= 126) {
                /* Printable ASCII with shift handling */
                char ch = (char)key;
                if (shift) {
                    if (ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
                    else {
                        switch (ch) {
                            case '1': ch = '!'; break;
                            case '2': ch = '@'; break;
                            case '3': ch = '#'; break;
                            case '4': ch = '$'; break;
                            case '5': ch = '%'; break;
                            case '6': ch = '^'; break;
                            case '7': ch = '&'; break;
                            case '8': ch = '*'; break;
                            case '9': ch = '('; break;
                            case '0': ch = ')'; break;
                            case '-': ch = '_'; break;
                            case '=': ch = '+'; break;
                            case '[': ch = '{'; break;
                            case ']': ch = '}'; break;
                            case '\\': ch = '|'; break;
                            case ';': ch = ':'; break;
                            case '\'': ch = '"'; break;
                            case ',': ch = '<'; break;
                            case '.': ch = '>'; break;
                            case '/': ch = '?'; break;
                            case '`': ch = '~'; break;
                        }
                    }
                }
                clarity_handle_text_action(&g_doc, fidx, ACT_CHAR, (UH)(unsigned char)ch);
            } else if (key >= 0x0100 && key <= 0xFFFF) {
                /* Direct TRON code / Unicode */
                clarity_handle_text_action(&g_doc, fidx, ACT_CHAR, (UH)key);
            }
            inval_wnd(wnd);
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Menu Construction                                                  */
/* ------------------------------------------------------------------ */

static void build_menu(void)
{
    app_menu_init(&g_menu, APP_MENU_STYLE_CLASSIC_3D);

    /* 1. File */
    int fi = app_menu_add_header(&g_menu, "ファイル(F)", 100);
    app_menu_add_item(&g_menu, fi, "新規作成 (New)",  "Ctrl+N", CMD_FILE_NEW,  TRUE);
    app_menu_add_item(&g_menu, fi, "開く (Open)...",   "Ctrl+O", CMD_FILE_OPEN, TRUE);
    app_menu_add_item(&g_menu, fi, "保存 (Save)",      "Ctrl+S", CMD_FILE_SAVE, TRUE);

    /* 2. View / Zoom */
    int vi = app_menu_add_header(&g_menu, "表示(V)", 80);
    app_menu_add_item(&g_menu, vi, "拡大 (Zoom In +)",     "Ctrl++", CMD_VIEW_ZOOM_IN,  TRUE);
    app_menu_add_item(&g_menu, vi, "縮小 (Zoom Out -)",    "Ctrl+-", CMD_VIEW_ZOOM_OUT, TRUE);
    app_menu_add_item(&g_menu, vi, "等倍 (Actual 100%)",   "Ctrl+0", CMD_VIEW_ACTUAL,   TRUE);
    app_menu_add_item(&g_menu, vi, "全体表示 (Fit Window)", "Ctrl+F", CMD_VIEW_FIT,      TRUE);
    app_menu_add_separator(&g_menu, vi);
    app_menu_add_item(&g_menu, vi, "50% 表示",   "", CMD_VIEW_ZOOM_50,  TRUE);
    app_menu_add_item(&g_menu, vi, "75% 表示",   "", CMD_VIEW_ZOOM_75,  TRUE);
    app_menu_add_item(&g_menu, vi, "100% 表示",  "", CMD_VIEW_ZOOM_100, TRUE);
    app_menu_add_item(&g_menu, vi, "125% 表示",  "", CMD_VIEW_ZOOM_125, TRUE);
    app_menu_add_item(&g_menu, vi, "150% 表示",  "", CMD_VIEW_ZOOM_150, TRUE);

    /* 3. Format */
    int fmti = app_menu_add_header(&g_menu, "判型(P)", 80);
    app_menu_add_item(&g_menu, fmti, "A4 判型 (210×297 mm)", "", CMD_FMT_A4, TRUE);
    app_menu_add_item(&g_menu, fmti, "四六判 (127×188 mm・縦書き)", "", CMD_FMT_SHIROKU, TRUE);
    app_menu_add_item(&g_menu, fmti, "Pecha 経典 (560×110 mm)", "", CMD_FMT_PECHA, TRUE);
    app_menu_add_separator(&g_menu, fmti);
    app_menu_add_item(&g_menu, fmti, "ページ追加 (Add Page)", "+", CMD_PAGE_ADD, TRUE);
    app_menu_add_item(&g_menu, fmti, "ページ削除 (Remove Page)", "-", CMD_PAGE_REMOVE, TRUE);

    /* 4. Insert */
    int ii = app_menu_add_header(&g_menu, "挿入(I)", 80);
    app_menu_add_item(&g_menu, ii, "文字列枠 (Text Frame)",  "T", CMD_INS_TEXT,  TRUE);
    app_menu_add_item(&g_menu, ii, "画像枠 (Image Frame)",   "I", CMD_INS_IMAGE, TRUE);
    app_menu_add_separator(&g_menu, ii);
    app_menu_add_item(&g_menu, ii, "書字方向切替 (横↔縦)", "F", CMD_INS_FLIP_FLOW, TRUE);
}

/* ------------------------------------------------------------------ */
/* Window Destruction Hook                                             */
/* ------------------------------------------------------------------ */

static void destroy_clarity(WND *wnd)
{
    (void)wnd;
    g_wnd = NULL;
    clarity_set_cursor(CLARITY_CURSOR_ARROW);
}

/* ------------------------------------------------------------------ */
/* Public Window Opener (Canonical Non-Blocking BTRON Convention)     */
/* ------------------------------------------------------------------ */

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

    /* Populate rich default sample frame */
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
        f->cursor_pos = (int)f->text_len;
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
    clarity_fit_window();

    top_wnd(g_wnd);
    inval_wnd(g_wnd);
    return g_wnd;
}

void clarity_app_open(void)
{
    open_clarity_window();
}

/* ------------------------------------------------------------------ */
/* Legacy init helper (spec API surface; kept for header compatibility)*/
/* ------------------------------------------------------------------ */

void clarity_init(ClarityDoc *doc)
{
    if (!doc) return;
    memset(doc, 0, sizeof(ClarityDoc));
    doc->fmt             = FMT_A4;
    doc->page_count      = 2;
    doc->zoom_pct        = 75;
    doc->selected_frame  = -1;
    doc->tool            = TOOL_SELECT;
    clarity_fmt_dimensions(doc);
}

void clarity_set_pecha_mode(ClarityDoc *doc, int size)
{
    if (!doc) return;
    doc->fmt = FMT_PECHA;
    (void)size;
    clarity_fmt_dimensions(doc);
}
