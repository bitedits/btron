/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Layout Module (src/apps/clarity_layout.c)
 * mm→pixel mapping, page outline drawing, frame hit-test, resize handles.
 * Uses only dp.h primitives (drw_rec, drw_lin, fill_rec). No new dependencies.
 */

#include "clarity_doc.h"
#include <btron/dp.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stddef.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* ------------------------------------------------------------------ */
/* Unit conversion                                                      */
/* ------------------------------------------------------------------ */

/* Convert millimetres to pixels at CLARITY_DPI (96 dpi). */
int clarity_mm_to_px(int mm)
{
    /* px = mm * dpi / 25.4  – integer arithmetic, rounds down */
    return (mm * CLARITY_DPI) / 25;   /* 25 ≈ 25.4 for integer maths */
}

/* Fill the doc's page_w_mm / page_h_mm from its fmt field. */
void clarity_fmt_dimensions(ClarityDoc *doc)
{
    if (!doc) return;
    switch (doc->fmt) {
        case FMT_A4:
            doc->page_w_mm = 210; doc->page_h_mm = 297; break;
        case FMT_SHIROKU:
            doc->page_w_mm = 127; doc->page_h_mm = 188; break;
        case FMT_PECHA:
            doc->page_w_mm = 560; doc->page_h_mm = 110; break;
        default:
            doc->page_w_mm = 210; doc->page_h_mm = 297; break;
    }
}

/* ------------------------------------------------------------------ */
/* Page outline                                                         */
/* ------------------------------------------------------------------ */

/*
 * Draw the page rectangle and a light ruler grid.
 * ox, oy = canvas origin (top-left of the page in screen coords).
 */
void clarity_draw_page(GDEV *dev, const ClarityDoc *doc, int ox, int oy)
{
    if (!dev || !doc) return;

    int pw = clarity_mm_to_px(doc->page_w_mm);
    int ph = clarity_mm_to_px(doc->page_h_mm);

    /* White page fill */
    RECT page;
    page.left   = (H)ox;
    page.top    = (H)oy;
    page.right  = (H)(ox + pw);
    page.bottom = (H)(oy + ph);
    fill_rec(dev, &page, COLOR_WHITE);

    /* Page border */
    set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
    drw_rec(dev, &page);

    /* Ruler ticks every 10 mm */
    set_col(dev, COLOR_LTGRAY, COLOR_WHITE);
    int step = clarity_mm_to_px(10);
    if (step < 4) step = 4;

    for (int x = ox; x <= ox + pw; x += step)
        drw_lin(dev, (H)x, (H)oy, (H)x, (H)(oy + 4));
    for (int y = oy; y <= oy + ph; y += step)
        drw_lin(dev, (H)ox, (H)y, (H)(ox + 4), (H)y);

    /* Pecha: draw ceremonial double-border (lcags-ri) */
    if (doc->fmt == FMT_PECHA) {
        RECT inner;
        inner.left   = (H)(ox + 6);
        inner.top    = (H)(oy + 6);
        inner.right  = (H)(ox + pw - 6);
        inner.bottom = (H)(oy + ph - 6);
        set_col(dev, COLOR_RED, COLOR_WHITE);
        drw_rec(dev, &inner);
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        drw_rec(dev, &page);
    }
}

/* ------------------------------------------------------------------ */
/* Frame drawing                                                        */
/* ------------------------------------------------------------------ */

/*
 * Returns the screen RECT for a frame adjusted by canvas origin ox,oy.
 */
static RECT frame_screen_rect(const ClarityFrame *f, int ox, int oy)
{
    RECT r;
    r.left   = (H)(f->bounds.left   + ox);
    r.top    = (H)(f->bounds.top    + oy);
    r.right  = (H)(f->bounds.right  + ox);
    r.bottom = (H)(f->bounds.bottom + oy);
    return r;
}

/*
 * Draw a single resize handle (filled square) at (cx, cy).
 */
static void draw_handle(GDEV *dev, int cx, int cy)
{
    RECT h;
    h.left   = (H)(cx - CLARITY_HANDLE_RADIUS);
    h.top    = (H)(cy - CLARITY_HANDLE_RADIUS);
    h.right  = (H)(cx + CLARITY_HANDLE_RADIUS);
    h.bottom = (H)(cy + CLARITY_HANDLE_RADIUS);
    fill_rec(dev, &h, COLOR_BLUE);
    set_col(dev, COLOR_WHITE, COLOR_BLUE);
    drw_rec(dev, &h);
}

/*
 * Draw all frames. Selected frame gets 8 resize handles.
 */
void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy)
{
    if (!dev || !doc) return;

    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];
        if (f->id == 0) continue;

        RECT sr = frame_screen_rect(f, ox, oy);
        BOOL sel = (i == doc->selected_frame);

        if (f->type == FRAME_TEXT) {
            set_col(dev, sel ? COLOR_BLUE : COLOR_DKGRAY, COLOR_WHITE);
        } else {
            set_col(dev, sel ? COLOR_TEAL : COLOR_GRAY, COLOR_WHITE);
        }
        drw_rec(dev, &sr);

        if (sel) {
            int mx = (sr.left + sr.right)  / 2;
            int my = (sr.top  + sr.bottom) / 2;
            /* 8 handles: 4 corners + 4 edge midpoints */
            draw_handle(dev, sr.left,  sr.top);
            draw_handle(dev, mx,       sr.top);
            draw_handle(dev, sr.right, sr.top);
            draw_handle(dev, sr.right, my);
            draw_handle(dev, sr.right, sr.bottom);
            draw_handle(dev, mx,       sr.bottom);
            draw_handle(dev, sr.left,  sr.bottom);
            draw_handle(dev, sr.left,  my);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Hit-testing                                                          */
/* ------------------------------------------------------------------ */

/*
 * Returns the index of the topmost frame whose bounds contain screen
 * point (x, y), or -1 if none. Iterates in reverse so topmost (last)
 * frame wins.
 */
int clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy)
{
    if (!doc) return -1;
    for (int i = doc->frame_count - 1; i >= 0; i--) {
        const ClarityFrame *f = &doc->frames[i];
        if (f->id == 0) continue;
        H sx = (H)(f->bounds.left + ox);
        H sy = (H)(f->bounds.top  + oy);
        H ex = (H)(f->bounds.right  + ox);
        H ey = (H)(f->bounds.bottom + ox);
        if (x >= sx && x <= ex && y >= sy && y <= ey) return i;
    }
    return -1;
}

/*
 * Returns the handle id (0-7) for the selected frame, or -1.
 * Handle layout mirrors draw order in clarity_draw_frames.
 *   0 top-left  1 top-mid   2 top-right
 *   3 right-mid 4 bot-right 5 bot-mid
 *   6 bot-left  7 left-mid
 */
int clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy)
{
    if (!f || f->id == 0) return -1;

    RECT sr;
    sr.left   = (H)(f->bounds.left   + ox);
    sr.top    = (H)(f->bounds.top    + oy);
    sr.right  = (H)(f->bounds.right  + ox);
    sr.bottom = (H)(f->bounds.bottom + oy);

    int mx = (sr.left + sr.right)  / 2;
    int my = (sr.top  + sr.bottom) / 2;
    int R  = CLARITY_HANDLE_RADIUS + 2; /* slightly larger hit zone */

    int hx[8] = { sr.left, mx, sr.right, sr.right, sr.right, mx, sr.left, sr.left };
    int hy[8] = { sr.top,  sr.top, sr.top, my, sr.bottom, sr.bottom, sr.bottom, my };

    for (int h = 0; h < 8; h++) {
        if (x >= hx[h] - R && x <= hx[h] + R &&
            y >= hy[h] - R && y <= hy[h] + R) return h;
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Frame manipulation helpers                                           */
/* ------------------------------------------------------------------ */

/*
 * Update a frame's bounds while dragging handle h to (mx, my).
 * Enforces minimum 16px size.
 */
void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy)
{
    if (!f) return;
    /* Convert screen coords back to canvas-local */
    H cx = (H)(mx - ox);
    H cy = (H)(my - oy);
#define MIN_SZ 16
    switch (h) {
        case 0: /* top-left */
            if (f->bounds.right  - cx >= MIN_SZ) f->bounds.left = cx;
            if (f->bounds.bottom - cy >= MIN_SZ) f->bounds.top  = cy;
            break;
        case 1: /* top-mid */
            if (f->bounds.bottom - cy >= MIN_SZ) f->bounds.top = cy;
            break;
        case 2: /* top-right */
            if (cx - f->bounds.left >= MIN_SZ)   f->bounds.right = cx;
            if (f->bounds.bottom - cy >= MIN_SZ) f->bounds.top   = cy;
            break;
        case 3: /* right-mid */
            if (cx - f->bounds.left >= MIN_SZ) f->bounds.right = cx;
            break;
        case 4: /* bot-right */
            if (cx - f->bounds.left   >= MIN_SZ) f->bounds.right  = cx;
            if (cy - f->bounds.top    >= MIN_SZ) f->bounds.bottom = cy;
            break;
        case 5: /* bot-mid */
            if (cy - f->bounds.top >= MIN_SZ) f->bounds.bottom = cy;
            break;
        case 6: /* bot-left */
            if (f->bounds.right - cx >= MIN_SZ) f->bounds.left   = cx;
            if (cy - f->bounds.top   >= MIN_SZ) f->bounds.bottom = cy;
            break;
        case 7: /* left-mid */
            if (f->bounds.right - cx >= MIN_SZ) f->bounds.left = cx;
            break;
        default: break;
    }
#undef MIN_SZ
}

/*
 * Move selected frame by (dx, dy) in canvas pixels.
 */
void clarity_move_frame(ClarityFrame *f, H dx, H dy)
{
    if (!f) return;
    f->bounds.left   = (H)(f->bounds.left   + dx);
    f->bounds.top    = (H)(f->bounds.top    + dy);
    f->bounds.right  = (H)(f->bounds.right  + dx);
    f->bounds.bottom = (H)(f->bounds.bottom + dy);
}
