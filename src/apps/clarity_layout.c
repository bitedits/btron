/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Layout Module (src/apps/clarity_layout.c)
 * mm→pixel mapping with zoom scaling, calibrated paper sizes, multi-page
 * drawing with page separators, 3D drop-shadows, margin guides, frame hit-test,
 * and 8-point resize handles.
 */

#include "clarity_doc.h"
#include <btron/vobj.h>
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
        } else if (f->type == FRAME_TAD) {
            set_col(dev, sel ? COLOR_BLUE : COLOR_NAVY, COLOR_WHITE);
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
            const char *badge = (f->type == FRAME_TEXT) ? " [Text] " :
                                (f->type == FRAME_TAD)  ? " [TAD] "  : " [Image] ";
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

    const char *try_paths[16];
    int n_try = 0;
    try_paths[n_try++] = path;

    char rel_sys[256];
    if (strncmp(path, "/SYS/", 5) == 0) {
        snprintf(rel_sys, sizeof(rel_sys), "SYS/%s", path + 5);
        try_paths[n_try++] = rel_sys;
    } else if (strncmp(path, "SYS/", 4) == 0) {
        snprintf(rel_sys, sizeof(rel_sys), "/SYS/%s", path + 4);
        try_paths[n_try++] = rel_sys;
    }

    char cur_sys[256];
    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;
    snprintf(cur_sys, sizeof(cur_sys), "./SYS/%s", bname);
    try_paths[n_try++] = cur_sys;

    char icon_fallback[256];
    snprintf(icon_fallback, sizeof(icon_fallback), "assets/icons/%s", bname);
    try_paths[n_try++] = icon_fallback;

    /* Preference-style icon names and pure extensionless fallbacks */
    char pure_name[64];
    strncpy(pure_name, bname, sizeof(pure_name) - 1);
    pure_name[sizeof(pure_name) - 1] = '\0';
    char *dot = strrchr(pure_name, '.');
    if (dot) *dot = '\0';

    char pure_png[256], pure_gif[256], asset_png[256], asset_gif[256];
    snprintf(pure_png, sizeof(pure_png), "assets/icons/%s.png", pure_name);
    try_paths[n_try++] = pure_png;
    snprintf(pure_gif, sizeof(pure_gif), "assets/icons/%s.gif", pure_name);
    try_paths[n_try++] = pure_gif;
    snprintf(asset_png, sizeof(asset_png), "assets/%s.png", pure_name);
    try_paths[n_try++] = asset_png;
    snprintf(asset_gif, sizeof(asset_gif), "assets/%s.gif", pure_name);
    try_paths[n_try++] = asset_gif;

    int decoded = -1;
    for (int i = 0; i < n_try; i++) {
        if (decode_image_rgba(try_paths[i], &pixels, &w, &h) == 0 && pixels) {
            decoded = 0;
            break;
        }
    }

    if (decoded != 0 || !pixels) {
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

/* ================================================================
 * Z-Ordering Frame Manipulation & Duplication
 * ================================================================ */

int clarity_doc_send_to_back(ClarityDoc *doc, int frame_idx)
{
    if (!doc || frame_idx <= 0 || frame_idx >= doc->frame_count) return -1;
    ClarityFrame target = doc->frames[frame_idx];
    memmove(&doc->frames[1], &doc->frames[0], (size_t)frame_idx * sizeof(ClarityFrame));
    doc->frames[0] = target;
    doc->selected_frame = 0;
    doc->dirty = TRUE;
    return 0;
}

int clarity_doc_send_to_front(ClarityDoc *doc, int frame_idx)
{
    if (!doc || frame_idx < 0 || frame_idx >= doc->frame_count - 1) return -1;
    ClarityFrame target = doc->frames[frame_idx];
    memmove(&doc->frames[frame_idx], &doc->frames[frame_idx + 1],
            (size_t)(doc->frame_count - 1 - frame_idx) * sizeof(ClarityFrame));
    doc->frames[doc->frame_count - 1] = target;
    doc->selected_frame = doc->frame_count - 1;
    doc->dirty = TRUE;
    return 0;
}

int clarity_doc_send_backward(ClarityDoc *doc, int frame_idx)
{
    if (!doc || frame_idx <= 0 || frame_idx >= doc->frame_count) return -1;
    ClarityFrame tmp = doc->frames[frame_idx];
    doc->frames[frame_idx] = doc->frames[frame_idx - 1];
    doc->frames[frame_idx - 1] = tmp;
    doc->selected_frame = frame_idx - 1;
    doc->dirty = TRUE;
    return 0;
}

int clarity_doc_send_forward(ClarityDoc *doc, int frame_idx)
{
    if (!doc || frame_idx < 0 || frame_idx >= doc->frame_count - 1) return -1;
    ClarityFrame tmp = doc->frames[frame_idx];
    doc->frames[frame_idx] = doc->frames[frame_idx + 1];
    doc->frames[frame_idx + 1] = tmp;
    doc->selected_frame = frame_idx + 1;
    doc->dirty = TRUE;
    return 0;
}

int clarity_doc_duplicate_frame(ClarityDoc *doc, int frame_idx)
{
    if (!doc || frame_idx < 0 || frame_idx >= doc->frame_count) return -1;
    if (doc->frame_count >= CLARITY_MAX_FRAMES) return -1;

    ClarityFrame *src = &doc->frames[frame_idx];
    int new_idx = doc->frame_count;
    ClarityFrame *dst = &doc->frames[new_idx];

    *dst = *src;
    dst->id = (UB)(new_idx + 1);

    /* Offset duplicate frame by +16px canvas offset */
    H offset = 16;
    dst->bounds.left   += offset;
    dst->bounds.right  += offset;
    dst->bounds.top    += offset;
    dst->bounds.bottom += offset;

    /* Deep-copy image pixel payload if present */
    if (src->type == FRAME_IMAGE && src->bitmap && src->bmp_w > 0 && src->bmp_h > 0) {
        size_t sz = (size_t)src->bmp_w * (size_t)src->bmp_h * 4;
        dst->bitmap = (UB*)malloc(sz);
        if (dst->bitmap) {
            memcpy(dst->bitmap, src->bitmap, sz);
        }
    }

    doc->frame_count++;
    doc->selected_frame = new_idx;
    doc->dirty = TRUE;
    return new_idx;
}

void clarity_insert_tip_text(ClarityDoc *doc, int fidx, const char *utf8_text)
{
    if (!doc || !utf8_text || fidx < 0 || fidx >= doc->frame_count) return;
    ClarityFrame *f = &doc->frames[fidx];
    if (f->type != FRAME_TEXT) return;

    TC tc_buf[128];
    int tc_len = utf8_to_tc_string(utf8_text, tc_buf, 128);
    for (int i = 0; i < tc_len; i++) {
        clarity_handle_text_action(doc, fidx, CLARITY_ACT_CHAR, (UH)tc_buf[i]);
    }
}

int clarity_frame_load_text(ClarityFrame *f, const char *path, ID robj_id, const char *name)
{
    if (!f || f->type != FRAME_TEXT || !path) return -1;

    const char *try_paths[16];
    char path_bufs[10][256];
    int n_try = 0;
    try_paths[n_try++] = path;

    const char *bname = strrchr(path, '/');
    bname = bname ? bname + 1 : path;

    snprintf(path_bufs[0], 256, "./%s", path);
    try_paths[n_try++] = path_bufs[0];

    snprintf(path_bufs[1], 256, "assets/texts/%s", bname);
    try_paths[n_try++] = path_bufs[1];

    snprintf(path_bufs[2], 256, "./assets/texts/%s", bname);
    try_paths[n_try++] = path_bufs[2];

    snprintf(path_bufs[3], 256, "doc/md/%s", bname);
    try_paths[n_try++] = path_bufs[3];

    snprintf(path_bufs[4], 256, "./doc/md/%s", bname);
    try_paths[n_try++] = path_bufs[4];

    snprintf(path_bufs[5], 256, "SYS/%s", bname);
    try_paths[n_try++] = path_bufs[5];

    snprintf(path_bufs[6], 256, "./SYS/%s", bname);
    try_paths[n_try++] = path_bufs[6];

    snprintf(path_bufs[7], 256, "btron_store/%s", bname);
    try_paths[n_try++] = path_bufs[7];

    snprintf(path_bufs[8], 256, "./btron_store/%s", bname);
    try_paths[n_try++] = path_bufs[8];

    char raw_buf[16384];
    size_t nread = 0;
    BOOL read_ok = FALSE;

    /* 1. Try reading directly from Real Body persistent storage */
    ROBJ *r = NULL;
    if (path) {
        r = find_robj_by_path(path);
    }
    if (!r && robj_id >= 100) {
        ROBJ *cand = opn_robj(robj_id);
        if (cand && cand->type != VOBJ_TYPE_DRAW) {
            r = cand;
        }
    }
    if (r) {
        UW bytes = 0;
        if (rd_vobj_data(r, raw_buf, sizeof(raw_buf) - 1, &bytes) == E_OK && bytes > 0) {
            raw_buf[bytes] = '\0';
            nread = bytes;
            read_ok = TRUE;
        }
        cls_robj(r);
    }

    /* 2. Fall back to reading from file system */
    if (!read_ok) {
        FILE *fp = NULL;
        for (int i = 0; i < n_try; i++) {
            fp = fopen(try_paths[i], "rb");
            if (fp) break;
        }

        if (!fp) return -1;

        nread = fread(raw_buf, 1, sizeof(raw_buf) - 1, fp);
        fclose(fp);
        raw_buf[nread] = '\0';
    }

    /* Build text buffer with moniker header [TXT name] followed by content */
    char full_buf[20480];
    const char *display_name = (name && name[0]) ? name : bname;
    const char *tag = (strstr(path, ".md") || strstr(path, ".MD")) ? "MD" : "TXT";
    snprintf(full_buf, sizeof(full_buf), "[%s %s]\n\n%s", tag, display_name, raw_buf);

    /* Update moniker link at offset 0 */
    f->vobj_count = 1;
    f->vobjs[0].text_offset = 0;
    f->vobjs[0].target_robj = robj_id;
    f->vobjs[0].type = VOBJ_TYPE_TEXT;
    strncpy(f->vobjs[0].label, display_name, sizeof(f->vobjs[0].label) - 1);
    f->vobjs[0].label[sizeof(f->vobjs[0].label) - 1] = '\0';
    strncpy(f->vobjs[0].path, path, sizeof(f->vobjs[0].path) - 1);
    f->vobjs[0].path[sizeof(f->vobjs[0].path) - 1] = '\0';

    /* Convert UTF-8 content to TRON Code */
    int tlen = utf8_to_tc_string(full_buf, (TC*)f->text, CLARITY_TEXT_BUF - 1);
    f->text_len = (tlen > 0) ? (UW)tlen : 0;
    f->text[f->text_len] = 0;
    f->cursor_pos = 0;
    f->scroll_y = 0;
    f->robj_id = robj_id;
    strncpy(f->text_path, path, sizeof(f->text_path) - 1);
    f->text_path[sizeof(f->text_path) - 1] = '\0';

    return 0;
}

void clarity_handle_dnd_drop(ClarityDoc *doc, const BTRON_DND *dnd, H mx, H my, int ox, int oy)
{
    if (!doc || !dnd || !dnd->active) return;

    int path_len = (int)strlen(dnd->path);
    BOOL is_tad = (path_len > 4 && strcmp(dnd->path + path_len - 4, ".tad") == 0) ||
                  (path_len > 4 && strcmp(dnd->path + path_len - 4, ".TAD") == 0);
    BOOL is_text = (path_len > 4 && strcmp(dnd->path + path_len - 4, ".txt") == 0) ||
                   (path_len > 4 && strcmp(dnd->path + path_len - 4, ".TXT") == 0) ||
                   (path_len > 3 && strcmp(dnd->path + path_len - 3, ".md") == 0) ||
                   (path_len > 3 && strcmp(dnd->path + path_len - 3, ".MD") == 0);
    BOOL is_draw = (dnd->type == VOBJ_TYPE_DRAW) ||
                   (strstr(dnd->path, ".png") || strstr(dnd->path, ".gif") || strstr(dnd->path, ".bmp") ||
                    strstr(dnd->path, ".PNG") || strstr(dnd->path, ".GIF") || strstr(dnd->path, ".BMP"));

    /* 1. Hit test existing frames */
    int fidx = clarity_hittest_frame(doc, mx, my, ox, oy);
    if (fidx >= 0 && fidx < doc->frame_count) {
        ClarityFrame *f = &doc->frames[fidx];

        if (f->type == FRAME_IMAGE && is_draw) {
            clarity_frame_load_image(f, dnd->path, dnd->robj_id);
            doc->dirty = TRUE;
            return;
        }

        if (f->type == FRAME_TAD && is_tad) {
            strncpy(f->tad_path, dnd->path, sizeof(f->tad_path) - 1);
            strncpy(f->tad_title, dnd->name, sizeof(f->tad_title) - 1);
            f->robj_id = dnd->robj_id;
            doc->dirty = TRUE;
            return;
        }

        if (f->type == FRAME_TEXT && is_text) {
            /* Relink and load actual Real Body data into Text Frame immediately */
            clarity_frame_load_text(f, dnd->path, dnd->robj_id, dnd->name);
            doc->selected_frame = fidx;
            doc->dirty = TRUE;
            return;
        }

        /* Incompatible frame target: do not corrupt */
        return;
    }

    /* 2. Dropped onto empty canvas area -> create matching frame */
    int zoom = doc->zoom_pct ? doc->zoom_pct : 100;
    H cx = (H)(((mx - ox) * 100) / zoom);
    H cy = (H)(((my - oy) * 100) / zoom);

    if (is_draw) {
        ClarityFrame *nf = clarity_doc_add_frame(doc, FRAME_IMAGE, cx, cy, 200, 160);
        if (nf) {
            clarity_frame_load_image(nf, dnd->path, dnd->robj_id);
            doc->selected_frame = doc->frame_count - 1;
            doc->dirty = TRUE;
        }
    } else if (is_tad) {
        ClarityFrame *nf = clarity_doc_add_frame(doc, FRAME_TAD, cx, cy, 320, 120);
        if (nf) {
            strncpy(nf->tad_path, dnd->path, sizeof(nf->tad_path) - 1);
            strncpy(nf->tad_title, dnd->name, sizeof(nf->tad_title) - 1);
            nf->robj_id = dnd->robj_id;
            doc->selected_frame = doc->frame_count - 1;
            doc->dirty = TRUE;
        }
    } else if (is_text) {
        ClarityFrame *nf = clarity_doc_add_frame(doc, FRAME_TEXT, cx, cy, 320, 180);
        if (nf) {
            clarity_frame_load_text(nf, dnd->path, dnd->robj_id, dnd->name);
            doc->selected_frame = doc->frame_count - 1;
            doc->dirty = TRUE;
        }
    }
}

void clarity_init_sample_page(ClarityDoc *doc)
{
    if (!doc) return;
    for (int i = 0; i < doc->frame_count; i++) {
        if (doc->frames[i].bitmap) {
            free(doc->frames[i].bitmap);
            doc->frames[i].bitmap = NULL;
        }
    }
    memset(doc, 0, sizeof(ClarityDoc));
    doc->fmt            = FMT_A4;
    doc->page_count     = 2;
    doc->zoom_pct       = 75;
    doc->selected_frame = -1;
    doc->tool           = TOOL_SELECT;
    clarity_fmt_dimensions(doc);

    /* Frame 0: Image Frame linked with /SYS/clarity.png */
    ClarityFrame *f_img = clarity_doc_add_frame(doc, FRAME_IMAGE, 24, 24, 160, 160);
    if (f_img) {
        clarity_frame_load_image(f_img, "/SYS/clarity.png", 101);
    }

    /* Frame 1: Text Frame linked with HYPERMEDIA.md Real Body */
    ClarityFrame *f1 = clarity_doc_add_frame(doc, FRAME_TEXT, 200, 24, 400, 200);
    if (f1) {
        clarity_frame_load_text(f1, "doc/md/HYPERMEDIA.md", 102, "HYPERMEDIA.md");
    }

    /* Frame 2: TAD Placeholder Frame */
    ClarityFrame *f_tad = clarity_doc_add_frame(doc, FRAME_TAD, 24, 240, 576, 120);
    if (f_tad) {
        strncpy(f_tad->tad_path, "tad_bin/01_btron3_spec.tad", sizeof(f_tad->tad_path) - 1);
        strncpy(f_tad->tad_title, "【仕様書】BTRON3 3.20 OS Specification", sizeof(f_tad->tad_title) - 1);
        f_tad->robj_id = 104;
    }

    /* Frame 3: Guide */
    ClarityFrame *f_guide = clarity_doc_add_frame(doc, FRAME_TEXT, 24, 380, 576, 150);
    if (f_guide) {
        const char *guide =
            "【操作ガイド / Direct Manipulation Guide】\n"
            "1. キャビネットから画像/文書をドラッグ＆ドロップして配置\n"
            "2. TADファイルをドラッグしてTADプレースホルダー枠を作成\n"
            "3. 仮身やTAD枠をダブルクリックしてTAD Browser / エディタを開く\n"
            "4. [ファイル] → [保存] (Ctrl+S) で /SYS/Clarity-Sample.TAD に永続保存\n";
        int glen = utf8_to_tc_string(guide, (TC*)f_guide->text, CLARITY_TEXT_BUF - 1);
        f_guide->text_len = (glen > 0) ? (UW)glen : 0;
        f_guide->text[f_guide->text_len] = 0;
        f_guide->cursor_pos = (int)f_guide->text_len;
    }

    doc->selected_frame = 1;
}

