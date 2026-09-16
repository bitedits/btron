/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Layout Module (src/apps/clarity_layout.c)
 * mm→pixel mapping with zoom scaling, calibrated paper sizes, multi-page
 * drawing with page separators, 3D drop-shadows, margin guides, frame hit-test,
 * and 8-point resize handles.
 */

#include "clarity_doc.h"
#include <btron/image_decode.h>
#include <btron/dnd.h>
#include <stdlib.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/troncode.h>
#include <stdio.h>
#include <string.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <stddef.h>
#include <stdint.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

/* ------------------------------------------------------------------ */
/* Unit conversion with Zoom                                            */
/* ------------------------------------------------------------------ */

/* Convert millimetres to pixels at calibrated base DPI (72 dpi) and zoom_pct. */
int clarity_mm_to_px(int mm, int zoom_pct)
{
    if (zoom_pct <= 0) zoom_pct = 100;
    /* px = (mm * 72 * zoom_pct) / (25.4 * 100) = (mm * 72 * zoom_pct) / 2540 */
    long val = (long)mm * CLARITY_DPI * (long)zoom_pct;
    return (int)(val / 2540);
}

/* Base mm to unzoomed pixel conversion (at 100% zoom) */
int clarity_base_mm_to_px(int mm)
{
    long val = (long)mm * CLARITY_DPI * 100L;
    return (int)(val / 2540);
}

/* Fill the doc's page_w_mm / page_h_mm from its fmt field. */
void clarity_fmt_dimensions(ClarityDoc *doc)
{
    if (!doc) return;
    switch (doc->fmt) {
        case FMT_A4:
            /* Standard A4: 210 x 297 mm (ISO 216 1:sqrt(2)) */
            doc->page_w_mm = 210;
            doc->page_h_mm = 297;
            break;
        case FMT_SHIROKU:
            /* Traditional Japanese Shiroku (四六判): 127 x 188 mm */
            doc->page_w_mm = 127;
            doc->page_h_mm = 188;
            break;
        case FMT_PECHA:
            /* Tibetan Kangyur Pecha loose-leaf: 560 x 110 mm */
            doc->page_w_mm = 560;
            doc->page_h_mm = 110;
            break;
        default:
            doc->page_w_mm = 210;
            doc->page_h_mm = 297;
            break;
    }
    if (doc->page_count < 1) doc->page_count = 2; /* 2-page spread default */
    if (doc->zoom_pct <= 0)  doc->zoom_pct   = 75; /* 75% default fit */
}

/* ------------------------------------------------------------------ */
/* Page outline, Drop Shadows, Margin Guides & Page Separators         */
/* ------------------------------------------------------------------ */

void clarity_draw_page(GDEV *dev, const ClarityDoc *doc, int ox, int oy)
{
    if (!dev || !doc) return;

    int zoom = doc->zoom_pct > 0 ? doc->zoom_pct : 100;
    int pw = clarity_mm_to_px(doc->page_w_mm, zoom);
    int ph = clarity_mm_to_px(doc->page_h_mm, zoom);
    int p_gap = (CLARITY_PAGE_GAP_PX * zoom) / 100;
    if (p_gap < 24) p_gap = 24;

    int pages = doc->page_count > 0 ? doc->page_count : 1;

    for (int p = 0; p < pages; p++) {
        int page_ox = ox;
        int page_oy = oy + p * (ph + p_gap);

        /* 1. Realistic Physical Sheet 3D Drop-Shadow (Bottom & Right) */
        RECT shadow;
        shadow.left   = (H)(page_ox + 4);
        shadow.top    = (H)(page_oy + 4);
        shadow.right  = (H)(page_ox + pw + 6);
        shadow.bottom = (H)(page_oy + ph + 6);
        fill_rec(dev, &shadow, COLOR_GRAY);

        /* 2. Crisp White Paper Sheet */
        RECT page;
        page.left   = (H)page_ox;
        page.top    = (H)page_oy;
        page.right  = (H)(page_ox + pw);
        page.bottom = (H)(page_oy + ph);
        fill_rec(dev, &page, COLOR_WHITE);

        /* Page outer border */
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        drw_rec(dev, &page);

        /* 3. Page Header Label / Badge */
        char page_lbl[64];
        if (doc->fmt == FMT_SHIROKU) {
            snprintf(page_lbl, sizeof(page_lbl), "四六判 [ 第 %d 頁・%s ]",
                     p + 1, (p % 2 == 0) ? "表" : "裏");
        } else if (doc->fmt == FMT_PECHA) {
            snprintf(page_lbl, sizeof(page_lbl), "Pecha དཔེ་ཆ་ [ 葉 %d / %d ]",
                     p + 1, pages);
        } else {
            snprintf(page_lbl, sizeof(page_lbl), "A4 Portrait [ Page %d of %d ]",
                     p + 1, pages);
        }
        drw_tc_string(dev, page_ox + 4, page_oy - 16, page_lbl, COLOR_DKGRAY, 0);

        /* 4. DTP Printable Margin Guides (15 mm inset) */
        int mg_x = clarity_mm_to_px(CLARITY_MARGIN_GUIDE_MM, zoom);
        int mg_y = clarity_mm_to_px(CLARITY_MARGIN_GUIDE_MM, zoom);
        if (pw > mg_x * 2 + 20 && ph > mg_y * 2 + 20) {
            RECT guide_r = { (H)(page_ox + mg_x), (H)(page_oy + mg_y),
                             (H)(page_ox + pw - mg_x), (H)(page_oy + ph - mg_y) };
            /* Light cyan / faint dotted margin boundary */
            set_col(dev, 0xFFB0D0E0, COLOR_WHITE);
            drw_rec(dev, &guide_r);
        }

        /* 5. Fine ruler tick marks every 10 mm along page edges */
        set_col(dev, COLOR_LTGRAY, COLOR_WHITE);
        int step = clarity_mm_to_px(10, zoom);
        if (step >= 6) {
            for (int x = page_ox; x <= page_ox + pw; x += step)
                drw_lin(dev, (H)x, (H)page_oy, (H)x, (H)(page_oy + 3));
            for (int y = page_oy; y <= page_oy + ph; y += step)
                drw_lin(dev, (H)page_ox, (H)y, (H)(page_ox + 3), (H)y);
        }

        /* 6. Tibetan Pecha ceremonial double-border (lcags-ri) */
        if (doc->fmt == FMT_PECHA) {
            RECT inner;
            inner.left   = (H)(page_ox + 6);
            inner.top    = (H)(page_oy + 6);
            inner.right  = (H)(page_ox + pw - 6);
            inner.bottom = (H)(page_oy + ph - 6);
            set_col(dev, COLOR_RED, COLOR_WHITE);
            drw_rec(dev, &inner);
            set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
            drw_rec(dev, &page);
        }

        /* 7. Page Separator (between consecutive pages) */
        if (p < pages - 1) {
            int sep_y = page_oy + ph + p_gap / 2;
            int sep_w = pw > 200 ? pw : 200;

            /* Etched double lines */
            drw_lin(dev, (H)(page_ox - 10), (H)(sep_y - 1), (H)(page_ox + sep_w + 10), (H)(sep_y - 1));
            set_col(dev, COLOR_WHITE, COLOR_LTGRAY);
            drw_lin(dev, (H)(page_ox - 10), (H)sep_y, (H)(page_ox + sep_w + 10), (H)sep_y);

            /* Center Page Break Plate */
            char sep_buf[80];
            snprintf(sep_buf, sizeof(sep_buf), "── 頁区切り [ Page Break: %d / %d ] ──",
                     p + 1, p + 2);
            int tw = (int)strlen(sep_buf) * 8;
            int bx = page_ox + (sep_w - tw) / 2;
            RECT badge_r = { (H)(bx - 6), (H)(sep_y - 8), (H)(bx + tw + 6), (H)(sep_y + 9) };
            fill_rec(dev, &badge_r, COLOR_LTGRAY);
            set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
            drw_rec(dev, &badge_r);
            drw_tc_string(dev, bx, sep_y - 6, sep_buf, COLOR_NAVY, COLOR_LTGRAY);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Frame drawing & Hit Testing with Zoom                               */
/* ------------------------------------------------------------------ */

static RECT frame_screen_rect_zoom(const ClarityFrame *f, int ox, int oy, int zoom)
{
    RECT r;
    r.left   = (H)(ox + (f->bounds.left   * zoom) / 100);
    r.top    = (H)(oy + (f->bounds.top    * zoom) / 100);
    r.right  = (H)(ox + (f->bounds.right  * zoom) / 100);
    r.bottom = (H)(oy + (f->bounds.bottom * zoom) / 100);
    return r;
}

static void draw_handle_colored(GDEV *dev, int cx, int cy, COLOR bg, COLOR border)
{
    RECT h;
    h.left   = (H)(cx - CLARITY_HANDLE_RADIUS);
    h.top    = (H)(cy - CLARITY_HANDLE_RADIUS);
    h.right  = (H)(cx + CLARITY_HANDLE_RADIUS);
    h.bottom = (H)(cy + CLARITY_HANDLE_RADIUS);
    fill_rec(dev, &h, bg);
    set_col(dev, border, bg);
    drw_rec(dev, &h);
}

void clarity_draw_frames(GDEV *dev, const ClarityDoc *doc, int ox, int oy)
{
    if (!dev || !doc) return;
    int zoom = doc->zoom_pct > 0 ? doc->zoom_pct : 100;

    for (int i = 0; i < doc->frame_count; i++) {
        const ClarityFrame *f = &doc->frames[i];
        if (f->id == 0) continue;

        RECT sr = frame_screen_rect_zoom(f, ox, oy, zoom);
        BOOL sel = (i == doc->selected_frame);

        if (f->type == FRAME_TEXT) {
            set_col(dev, sel ? COLOR_BLUE : COLOR_DKGRAY, COLOR_WHITE);
        } else {
            set_col(dev, sel ? COLOR_TEAL : COLOR_GRAY, COLOR_WHITE);
        }
        drw_rec(dev, &sr);

        /* Drop-target highlight during DND */
        if (doc->hover_drop_frame == i) {
            RECT hr = sr;
            hr.left -= 2; hr.top -= 2; hr.right += 2; hr.bottom += 2;
            set_col(dev, COLOR_CYAN, COLOR_WHITE);
            drw_rec(dev, &hr);
            hr.left -= 1; hr.top -= 1; hr.right += 1; hr.bottom += 1;
            drw_rec(dev, &hr);
        }

        /* Frame type badge at top right */
        if (sel) {
            const char *badge = (f->type == FRAME_TEXT) ? " [Text] " : " [Image] ";
            drw_tc_string(dev, sr.right - 48, sr.top - 14, badge, COLOR_BLUE, COLOR_WHITE);

            int mx = (sr.left + sr.right)  / 2;
            int my = (sr.top  + sr.bottom) / 2;

            /* 8 handles: 4 corners + 4 edge midpoints */
            for (int h = 0; h < 8; h++) {
                int hx = 0, hy = 0;
                switch (h) {
                    case 0: hx = sr.left;  hy = sr.top;    break;
                    case 1: hx = mx;       hy = sr.top;    break;
                    case 2: hx = sr.right; hy = sr.top;    break;
                    case 3: hx = sr.right; hy = my;        break;
                    case 4: hx = sr.right; hy = sr.bottom; break;
                    case 5: hx = mx;       hy = sr.bottom; break;
                    case 6: hx = sr.left;  hy = sr.bottom; break;
                    case 7: hx = sr.left;  hy = my;        break;
                }
                BOOL active = (doc->drag_handle == h);
                draw_handle_colored(dev, hx, hy,
                                    active ? COLOR_GOLD : COLOR_BLUE,
                                    active ? COLOR_BLACK : COLOR_WHITE);
            }
        }
    }
}

int clarity_hittest_frame(const ClarityDoc *doc, H x, H y, int ox, int oy)
{
    if (!doc) return -1;
    int zoom = doc->zoom_pct > 0 ? doc->zoom_pct : 100;

    for (int i = doc->frame_count - 1; i >= 0; i--) {
        const ClarityFrame *f = &doc->frames[i];
        if (f->id == 0) continue;
        RECT sr = frame_screen_rect_zoom(f, ox, oy, zoom);
        if (x >= sr.left && x <= sr.right && y >= sr.top && y <= sr.bottom) {
            return i;
        }
    }
    return -1;
}

int clarity_hittest_handle(const ClarityFrame *f, H x, H y, int ox, int oy, int zoom_pct)
{
    if (!f || f->id == 0) return -1;
    int zoom = zoom_pct > 0 ? zoom_pct : 100;

    RECT sr = frame_screen_rect_zoom(f, ox, oy, zoom);

    int mx = (sr.left + sr.right)  / 2;
    int my = (sr.top  + sr.bottom) / 2;
    int R  = CLARITY_HANDLE_RADIUS + 5; /* 9px hit radius */

    int hx[8] = { sr.left, mx, sr.right, sr.right, sr.right, mx, sr.left, sr.left };
    int hy[8] = { sr.top,  sr.top, sr.top, my, sr.bottom, sr.bottom, sr.bottom, my };

    for (int h = 0; h < 8; h++) {
        if (x >= hx[h] - R && x <= hx[h] + R &&
            y >= hy[h] - R && y <= hy[h] + R) return h;
    }
    return -1;
}

void clarity_hittest_full(const ClarityDoc *doc, H x, H y, int ox, int oy, ClarityHitInfo *info)
{
    if (!info) return;
    info->target = CLARITY_HIT_NONE;
    info->frame_idx = -1;
    info->handle_idx = -1;
    if (!doc) return;

    int zoom = doc->zoom_pct > 0 ? doc->zoom_pct : 100;

    /* 1. If a frame is selected, check its 8 handles first */
    if (doc->selected_frame >= 0 && doc->selected_frame < doc->frame_count) {
        const ClarityFrame *sf = &doc->frames[doc->selected_frame];
        if (sf->id != 0) {
            int h = clarity_hittest_handle(sf, x, y, ox, oy, zoom);
            if (h >= 0) {
                info->target = CLARITY_HIT_HANDLE;
                info->frame_idx = doc->selected_frame;
                info->handle_idx = h;
                return;
            }
        }
    }

    /* 2. Check frames from top to bottom (reverse order) */
    for (int i = doc->frame_count - 1; i >= 0; i--) {
        const ClarityFrame *f = &doc->frames[i];
        if (f->id == 0) continue;

        RECT sr = frame_screen_rect_zoom(f, ox, oy, zoom);
        int pad = 6; /* 6px outside frame margin */
        if (x < sr.left - pad || x > sr.right + pad ||
            y < sr.top - pad  || y > sr.bottom + pad) {
            continue;
        }

        /* Hit within frame bounds + pad.
         * Test perimeter band: outer margin + 10px inner border */
        int p_inner = 10;
        if (x <= sr.left + p_inner || x >= sr.right - p_inner ||
            y <= sr.top + p_inner  || y >= sr.bottom - p_inner) {
            info->target = CLARITY_HIT_PERIMETER;
            info->frame_idx = i;
            return;
        } else {
            info->target = CLARITY_HIT_INTERIOR;
            info->frame_idx = i;
            return;
        }
    }
}

void clarity_resize_frame_handle(ClarityFrame *f, int h, H mx, H my, int ox, int oy, int zoom_pct)
{
    if (!f) return;
    int zoom = zoom_pct > 0 ? zoom_pct : 100;

    /* Convert screen coords back to unzoomed canvas document space */
    H cx = (H)(((mx - ox) * 100) / zoom);
    H cy = (H)(((my - oy) * 100) / zoom);

#define MIN_W 32
#define MIN_H 24
    switch (h) {
        case 0: /* top-left */
            if (f->bounds.right  - cx >= MIN_W) f->bounds.left = cx;
            if (f->bounds.bottom - cy >= MIN_H) f->bounds.top  = cy;
            break;
        case 1: /* top-mid */
            if (f->bounds.bottom - cy >= MIN_H) f->bounds.top = cy;
            break;
        case 2: /* top-right */
            if (cx - f->bounds.left >= MIN_W)   f->bounds.right = cx;
            if (f->bounds.bottom - cy >= MIN_H) f->bounds.top   = cy;
            break;
        case 3: /* right-mid */
            if (cx - f->bounds.left >= MIN_W) f->bounds.right = cx;
            break;
        case 4: /* bot-right */
            if (cx - f->bounds.left   >= MIN_W) f->bounds.right  = cx;
            if (cy - f->bounds.top    >= MIN_H) f->bounds.bottom = cy;
            break;
        case 5: /* bot-mid */
            if (cy - f->bounds.top >= MIN_H) f->bounds.bottom = cy;
            break;
        case 6: /* bot-left */
            if (f->bounds.right - cx >= MIN_W) f->bounds.left   = cx;
            if (cy - f->bounds.top   >= MIN_H) f->bounds.bottom = cy;
            break;
        case 7: /* left-mid */
            if (f->bounds.right - cx >= MIN_W) f->bounds.left = cx;
            break;
        default: break;
    }
#undef MIN_W
#undef MIN_H
}

void clarity_move_frame(ClarityFrame *f, H dx, H dy)
{
    if (!f) return;
    f->bounds.left   = (H)(f->bounds.left   + dx);
    f->bounds.top    = (H)(f->bounds.top    + dy);
    f->bounds.right  = (H)(f->bounds.right  + dx);
    f->bounds.bottom = (H)(f->bounds.bottom + dy);
}

/* ================================================================
 * Virtual Body & Direct Manipulation DND Ingestion Helpers
 * ================================================================ */

ClarityFrame* clarity_doc_add_frame(ClarityDoc *doc, ClarityFrameType type, H x, H y, H w, H h)
{
    if (!doc || doc->frame_count >= CLARITY_MAX_FRAMES) return NULL;
    ClarityFrame *f = &doc->frames[doc->frame_count];
    memset(f, 0, sizeof(ClarityFrame));
    f->id           = (UB)(doc->frame_count + 1);
    f->type         = type;
    f->bounds.left  = x;
    f->bounds.top   = y;
    f->bounds.right = (H)(x + w);
    f->bounds.bottom = (H)(y + h);
    f->flow         = (doc->fmt == FMT_SHIROKU) ? FLOW_V_RTL : FLOW_H_LTR;
    f->cursor_pos   = 0;
    doc->frame_count++;
    doc->dirty      = TRUE;
    return f;
}

int clarity_frame_find_vobj_at(const ClarityFrame *f, H mx, H my)
{
    if (!f || f->type != FRAME_TEXT) return -1;
    for (int i = 0; i < f->vobj_count; i++) {
        const RECT *r = &f->vobjs[i].box;
        if (mx >= r->left && mx <= r->right && my >= r->top && my <= r->bottom) {
            return i;
        }
    }
    return -1;
}

void clarity_frame_insert_vobj(ClarityFrame *f, ID target_robj, VOBJ_TYPE type, const char *label, const char *path)
{
    if (!f || f->type != FRAME_TEXT || f->vobj_count >= CLARITY_MAX_VOBJS) return;
    ClarityVObjLink *link = &f->vobjs[f->vobj_count++];
    link->text_offset = f->cursor_pos;
    link->target_robj = target_robj;
    link->type        = type;
    strncpy(link->label, label ? label : "Object", sizeof(link->label) - 1);
    link->label[sizeof(link->label) - 1] = '\0';
    strncpy(link->path, path ? path : "", sizeof(link->path) - 1);
    link->path[sizeof(link->path) - 1] = '\0';

    /* Insert formatted moniker text into f->text at cursor_pos */
    char moniker[128];
    snprintf(moniker, sizeof(moniker), "[%s %s] ", (type == VOBJ_TYPE_DRAW) ? "#" : "*", link->label);
    size_t mlen = strlen(moniker);

    if (f->text_len + mlen < CLARITY_TEXT_BUF - 1) {
        /* Shift right */
        for (int i = (int)f->text_len - 1; i >= f->cursor_pos; i--) {
            f->text[i + mlen] = f->text[i];
        }
        for (size_t k = 0; k < mlen; k++) {
            f->text[f->cursor_pos + k] = (UH)(unsigned char)moniker[k];
        }
        f->text_len += mlen;
        f->cursor_pos += mlen;
        f->text[f->text_len] = 0;
    }
}

int clarity_frame_load_image(ClarityFrame *f, const char *path, ID robj_id)
{
    if (!f || !path) return -1;
    UB *pixels = NULL;
    H w = 0, h = 0;
    if (decode_image_rgba(path, &pixels, &w, &h) != 0 || !pixels) {
        return -1;
    }
    if (f->bitmap) {
        free(f->bitmap);
        f->bitmap = NULL;
    }
    f->bitmap = pixels;
    f->bmp_w = w;
    f->bmp_h = h;
    f->robj_id = robj_id;
    strncpy(f->img_path, path, sizeof(f->img_path) - 1);
    f->img_path[sizeof(f->img_path) - 1] = '\0';
    return 0;
}

void clarity_handle_dnd_drop(ClarityDoc *doc, const BTRON_DND *dnd, H mx, H my, int ox, int oy)
{
    if (!doc || !dnd || !dnd->active) return;

    /* 1. Hit test existing frames */
    int fidx = clarity_hittest_frame(doc, mx, my, ox, oy);
    if (fidx >= 0 && fidx < doc->frame_count) {
        ClarityFrame *f = &doc->frames[fidx];
        if (dnd->type == VOBJ_TYPE_DRAW) {
            if (f->type == FRAME_IMAGE) {
                clarity_frame_load_image(f, dnd->path, dnd->robj_id);
                doc->dirty = TRUE;
                return;
            }
            /* Dropped image onto Text Frame -> insert graphical Virtual Body */
            clarity_frame_insert_vobj(f, dnd->robj_id, dnd->type, dnd->name, dnd->path);
            doc->dirty = TRUE;
            return;
        } else {
            /* Dropped text/document onto Text Frame -> insert Virtual Body moniker */
            if (f->type == FRAME_TEXT) {
                f->cursor_pos = clarity_text_xy_to_pos(f, mx, my, ox, oy, doc->zoom_pct);
                clarity_frame_insert_vobj(f, dnd->robj_id, dnd->type, dnd->name, dnd->path);
                doc->dirty = TRUE;
                return;
            }
        }
    }

    /* 2. Dropped onto empty canvas area -> create matching frame */
    int zoom = doc->zoom_pct ? doc->zoom_pct : 100;
    H cx = (H)(((mx - ox) * 100) / zoom);
    H cy = (H)(((my - oy) * 100) / zoom);

    if (dnd->type == VOBJ_TYPE_DRAW) {
        ClarityFrame *nf = clarity_doc_add_frame(doc, FRAME_IMAGE, cx, cy, 200, 160);
        if (nf) {
            clarity_frame_load_image(nf, dnd->path, dnd->robj_id);
            doc->selected_frame = doc->frame_count - 1;
            doc->dirty = TRUE;
        }
    } else {
        ClarityFrame *nf = clarity_doc_add_frame(doc, FRAME_TEXT, cx, cy, 280, 140);
        if (nf) {
            clarity_frame_insert_vobj(nf, dnd->robj_id, dnd->type, dnd->name, dnd->path);
            doc->selected_frame = doc->frame_count - 1;
            doc->dirty = TRUE;
        }
    }
}
