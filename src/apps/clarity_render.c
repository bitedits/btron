/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Render Module (src/apps/clarity_render.c)
 * Horizontal LTR, vertical RTL, full interactive typing with caret insertion,
 * backspace, delete, arrow navigation, and zoom scaling.
 */

#include "clarity_doc.h"
#include <btron/dp.h>
#include <btron/font_mgr.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <string.h>
#else
#include <string.h>
#endif

#define GLYPH_W  16
#define GLYPH_H  16

/* ------------------------------------------------------------------ */
/* Key Actions for Text Frames                                          */
/* ------------------------------------------------------------------ */

enum {
    CLARITY_ACT_CHAR = 0,
    CLARITY_ACT_BACKSPACE,
    CLARITY_ACT_DELETE,
    CLARITY_ACT_LEFT,
    CLARITY_ACT_RIGHT,
    CLARITY_ACT_HOME,
    CLARITY_ACT_END,
    CLARITY_ACT_ENTER
};

void clarity_handle_text_action(ClarityDoc *doc, int fidx, int action, UH tc)
{
    if (!doc || fidx < 0 || fidx >= doc->frame_count) return;
    ClarityFrame *f = &doc->frames[fidx];
    if (f->type != FRAME_TEXT) return;

    if (f->cursor_pos < 0) f->cursor_pos = (int)f->text_len;
    if (f->cursor_pos > (int)f->text_len) f->cursor_pos = (int)f->text_len;

    switch (action) {
        case CLARITY_ACT_CHAR:
            if (f->text_len < CLARITY_TEXT_BUF - 1) {
                /* Insert character at cursor_pos */
                for (int i = (int)f->text_len; i > f->cursor_pos; i--) {
                    f->text[i] = f->text[i - 1];
                }
                f->text[f->cursor_pos++] = tc;
                f->text_len++;
                doc->dirty = TRUE;
            }
            break;

        case CLARITY_ACT_ENTER:
            if (f->text_len < CLARITY_TEXT_BUF - 1) {
                for (int i = (int)f->text_len; i > f->cursor_pos; i--) {
                    f->text[i] = f->text[i - 1];
                }
                f->text[f->cursor_pos++] = (UH)'\n';
                f->text_len++;
                doc->dirty = TRUE;
            }
            break;

        case CLARITY_ACT_BACKSPACE:
            if (f->cursor_pos > 0 && f->text_len > 0) {
                for (int i = f->cursor_pos - 1; i < (int)f->text_len - 1; i++) {
                    f->text[i] = f->text[i + 1];
                }
                f->cursor_pos--;
                f->text_len--;
                f->text[f->text_len] = 0;
                doc->dirty = TRUE;
            }
            break;

        case CLARITY_ACT_DELETE:
            if (f->cursor_pos < (int)f->text_len) {
                for (int i = f->cursor_pos; i < (int)f->text_len - 1; i++) {
                    f->text[i] = f->text[i + 1];
                }
                f->text_len--;
                f->text[f->text_len] = 0;
                doc->dirty = TRUE;
            }
            break;

        case CLARITY_ACT_LEFT:
            if (f->cursor_pos > 0) f->cursor_pos--;
            break;

        case CLARITY_ACT_RIGHT:
            if (f->cursor_pos < (int)f->text_len) f->cursor_pos++;
            break;

        case CLARITY_ACT_HOME:
            f->cursor_pos = 0;
            break;

        case CLARITY_ACT_END:
            f->cursor_pos = (int)f->text_len;
            break;

        default:
            break;
    }
}

void clarity_render_key(ClarityDoc *doc, int fidx, UH tc)
{
    if (tc == 0x08) {
        clarity_handle_text_action(doc, fidx, CLARITY_ACT_BACKSPACE, 0);
    } else if (tc == '\r' || tc == '\n') {
        clarity_handle_text_action(doc, fidx, CLARITY_ACT_ENTER, 0);
    } else {
        clarity_handle_text_action(doc, fidx, CLARITY_ACT_CHAR, tc);
    }
}

/* ------------------------------------------------------------------ */
/* Glyph blitter                                                        */
/* ------------------------------------------------------------------ */

static void blit_glyph_1bit(GDEV *dev, const UB *glyph, int px, int py,
                             COLOR fg, int gw, int gh)
{
    if (!dev || !glyph) return;
    int bytes_per_row = (gw + 7) / 8;
    for (int row = 0; row < gh; row++) {
        for (int col = 0; col < gw; col++) {
            UB byte_val = glyph[row * bytes_per_row + (col / 8)];
            if (byte_val & (0x80u >> (col & 7))) {
                int sx = px + col;
                int sy = py + row;
                if (sx >= 0 && sx < (int)dev->width &&
                    sy >= 0 && sy < (int)dev->height) {
                    dev->pixels[sy * dev->width + sx] = fg;
                }
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Horizontal LTR text rendering                                        */
/* ------------------------------------------------------------------ */

static void render_text_hltr_zoom(GDEV *dev, const ClarityFrame *f,
                                 int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f) return;
    int x0 = ox + (f->bounds.left   * zoom) / 100 + 4;
    int y0 = oy + (f->bounds.top    * zoom) / 100 + 4;
    int x1 = ox + (f->bounds.right  * zoom) / 100 - 4;
    int y1 = oy + (f->bounds.bottom * zoom) / 100 - 4;

    WERR fdesc = fopn_fon();
    if (fdesc < 0) {
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        RECT dash = { (H)x0, (H)y0, (H)(x0 + 24), (H)(y0 + 2) };
        fill_rec(dev, &dash, COLOR_DKGRAY);
        return;
    }

    FSSPEC spec;
    memset(&spec, 0, sizeof(spec));
    spec.fclass = FTC_GOTHIC;
    spec.size.width  = GLYPH_W;
    spec.size.height = GLYPH_H;
    fset_fon((W)fdesc, &spec);

    int cx = x0;
    int cy = y0;
    int caret_x = x0, caret_y = y0;

    int c_pos = (f->cursor_pos >= 0 && f->cursor_pos <= (int)f->text_len)
                ? f->cursor_pos : (int)f->text_len;

    for (UW i = 0; i < f->text_len; i++) {
        if ((int)i == c_pos) {
            caret_x = cx;
            caret_y = cy;
        }

        UH tc = f->text[i];
        if (tc == '\n' || tc == '\r') {
            cx  = x0;
            cy += GLYPH_H + 3;
            if (cy + GLYPH_H > y1) break;
            continue;
        }

        /* Wrap at right edge */
        if (cx + GLYPH_W > x1) {
            cx  = x0;
            cy += GLYPH_H + 3;
        }
        if (cy + GLYPH_H > y1) break;

        /* Fetch glyph */
        UB glyph_buf[128];
        FDATA *fd = (FDATA *)(void *)glyph_buf;
        WERR res = fget_img((W)fdesc, fd, (W)sizeof(glyph_buf),
                            0, tc, FT_IMAGE);
        if (res >= 0 && fd->image) {
            blit_glyph_1bit(dev, fd->image, cx, cy, COLOR_BLACK,
                            fd->asize.width  ? fd->asize.width  : GLYPH_W,
                            fd->asize.height ? fd->asize.height : GLYPH_H);
            cx += (fd->asize.width ? fd->asize.width : GLYPH_W) + 1;
        } else {
            /* Fallback character box */
            RECT box = { (H)cx, (H)cy, (H)(cx + GLYPH_W - 1), (H)(cy + GLYPH_H - 1) };
            set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
            drw_rec(dev, &box);
            cx += GLYPH_W + 1;
        }
    }

    if (c_pos == (int)f->text_len) {
        caret_x = cx;
        caret_y = cy;
    }

    /* Live Text Insertion Caret */
    if (is_selected && caret_x <= x1 && caret_y + GLYPH_H <= y1 + 4) {
        set_col(dev, COLOR_NAVY, COLOR_WHITE);
        drw_lin(dev, (H)caret_x, (H)caret_y, (H)caret_x, (H)(caret_y + GLYPH_H - 1));
        drw_lin(dev, (H)(caret_x + 1), (H)caret_y, (H)(caret_x + 1), (H)(caret_y + GLYPH_H - 1));
    }

    fcls_fon((W)fdesc);
}

/* ------------------------------------------------------------------ */
/* Vertical RTL text rendering (縦書き)                                  */
/* ------------------------------------------------------------------ */

static void render_text_vrtl_zoom(GDEV *dev, const ClarityFrame *f,
                                 int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f) return;
    int x0 = ox + (f->bounds.left   * zoom) / 100 + 4;
    int y0 = oy + (f->bounds.top    * zoom) / 100 + 4;
    int x1 = ox + (f->bounds.right  * zoom) / 100 - 4;
    int y1 = oy + (f->bounds.bottom * zoom) / 100 - 4;

    WERR fdesc = fopn_fon();
    if (fdesc < 0) {
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        RECT dash = { (H)(x1 - 2), (H)y0, (H)x1, (H)(y0 + 24) };
        fill_rec(dev, &dash, COLOR_DKGRAY);
        return;
    }

    FSSPEC spec;
    memset(&spec, 0, sizeof(spec));
    spec.fclass = FTC_GOTHIC;
    spec.size.width  = GLYPH_W;
    spec.size.height = GLYPH_H;
    fset_fon((W)fdesc, &spec);

    int col_x = x1 - GLYPH_W;
    int cy    = y0;
    int caret_x = col_x, caret_y = y0;

    int c_pos = (f->cursor_pos >= 0 && f->cursor_pos <= (int)f->text_len)
                ? f->cursor_pos : (int)f->text_len;

    for (UW i = 0; i < f->text_len; i++) {
        if ((int)i == c_pos) {
            caret_x = col_x;
            caret_y = cy;
        }

        UH tc = f->text[i];
        if (tc == '\n' || tc == '\r') {
            col_x -= GLYPH_W + 4;
            cy     = y0;
            if (col_x < x0) break;
            continue;
        }

        if (cy + GLYPH_H > y1) {
            col_x -= GLYPH_W + 4;
            cy     = y0;
        }
        if (col_x < x0) break;

        UB glyph_buf[128];
        FDATA *fd = (FDATA *)(void *)glyph_buf;
        WERR res = fget_img((W)fdesc, fd, (W)sizeof(glyph_buf),
                            0, tc, FT_IMAGE);
        if (res >= 0 && fd->image) {
            blit_glyph_1bit(dev, fd->image, col_x, cy, COLOR_BLACK,
                            fd->asize.width  ? fd->asize.width  : GLYPH_W,
                            fd->asize.height ? fd->asize.height : GLYPH_H);
            cy += (fd->asize.height ? fd->asize.height : GLYPH_H) + 2;
        } else {
            RECT box = { (H)col_x, (H)cy, (H)(col_x + GLYPH_W - 1), (H)(cy + GLYPH_H - 1) };
            set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
            drw_rec(dev, &box);
            cy += GLYPH_H + 2;
        }
    }

    if (c_pos == (int)f->text_len) {
        caret_x = col_x;
        caret_y = cy;
    }

    /* Vertical insertion caret (horizontal bar below character) */
    if (is_selected && caret_x >= x0 && caret_y + 2 <= y1 + 4) {
        set_col(dev, COLOR_NAVY, COLOR_WHITE);
        drw_lin(dev, (H)caret_x, (H)caret_y, (H)(caret_x + GLYPH_W - 1), (H)caret_y);
        drw_lin(dev, (H)caret_x, (H)(caret_y + 1), (H)(caret_x + GLYPH_W - 1), (H)(caret_y + 1));
    }

    fcls_fon((W)fdesc);
}

/* ------------------------------------------------------------------ */
/* Public render dispatcher                                             */
/* ------------------------------------------------------------------ */

void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f || f->type != FRAME_TEXT) return;
    if (zoom <= 0) zoom = 100;

    /* Fill frame white interior */
    RECT bg;
    bg.left   = (H)(ox + (f->bounds.left   * zoom) / 100 + 1);
    bg.top    = (H)(oy + (f->bounds.top    * zoom) / 100 + 1);
    bg.right  = (H)(ox + (f->bounds.right  * zoom) / 100 - 1);
    bg.bottom = (H)(oy + (f->bounds.bottom * zoom) / 100 - 1);
    fill_rec(dev, &bg, COLOR_WHITE);

    if (f->flow == FLOW_V_RTL)
        render_text_vrtl_zoom(dev, f, ox, oy, zoom, is_selected);
    else
        render_text_hltr_zoom(dev, f, ox, oy, zoom, is_selected);
}

void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom)
{
    if (!dev || !f || f->type != FRAME_IMAGE) return;
    if (zoom <= 0) zoom = 100;

    int fx = ox + (f->bounds.left * zoom) / 100 + 1;
    int fy = oy + (f->bounds.top  * zoom) / 100 + 1;
    int fw = ((f->bounds.right - f->bounds.left) * zoom) / 100 - 2;
    int fh = ((f->bounds.bottom - f->bounds.top) * zoom) / 100 - 2;

    RECT bg = { (H)fx, (H)fy, (H)(fx + fw), (H)(fy + fh) };
    fill_rec(dev, &bg, COLOR_LTGRAY);

    if (!f->bitmap || f->bmp_w == 0 || f->bmp_h == 0 || fw <= 0 || fh <= 0) return;

    for (int dy = 0; dy < fh; dy++) {
        int src_y = (dy * (int)f->bmp_h) / fh;
        for (int dx = 0; dx < fw; dx++) {
            int src_x = (dx * (int)f->bmp_w) / fw;
            int src_idx = (src_y * (int)f->bmp_w + src_x) * 4;
            UB r = f->bitmap[src_idx + 0];
            UB g = f->bitmap[src_idx + 1];
            UB b = f->bitmap[src_idx + 2];
            UB a = f->bitmap[src_idx + 3];
            COLOR col = ((UW)a << 24) | ((UW)r << 16) | ((UW)g << 8) | (UW)b;
            int sx = fx + dx;
            int sy = fy + dy;
            if (sx >= 0 && sx < (int)dev->width && sy >= 0 && sy < (int)dev->height) {
                dev->pixels[sy * dev->width + sx] = col;
            }
        }
    }
}
