/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Render Module (src/apps/clarity_render.c)
 * Text blitting (horizontal LTR and vertical RTL), bitmap nearest-neighbour scale,
 * TRON code unit input routing.
 * Uses dp.h pixel access and font_mgr.h glyph fetch.
 */

#include "clarity_doc.h"
#include <btron/dp.h>
#include <btron/font_mgr.h>
#include <btron/troncode.h>
#include <btron/tad.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <string.h>
#else
#include <stddef.h>
#include <stdint.h>
extern void *tkl_memset(void *s, int c, size_t n);
#define memset tkl_memset
#endif

/* ------------------------------------------------------------------ */
/* Internal glyph metrics                                               */
/* ------------------------------------------------------------------ */

/* Prototype uses a fixed 12 × 12 cell for simplicity.
 * In production this would come from fget_img(). */
#define GLYPH_W 12
#define GLYPH_H 12

/* ------------------------------------------------------------------ */
/* Text input                                                           */
/* ------------------------------------------------------------------ */

/*
 * Append a TRON code unit to the active text frame.
 * Handles TC_NL (new paragraph), backspace (TC 0x08), and printable codes.
 */
void clarity_render_key(ClarityDoc *doc, int fidx, UH tc)
{
    if (!doc || fidx < 0 || fidx >= doc->frame_count) return;
    ClarityFrame *f = &doc->frames[fidx];
    if (f->type != FRAME_TEXT) return;

    if (tc == 0x08) { /* backspace */
        if (f->text_len > 0) {
            f->text_len--;
            f->text[f->text_len] = 0;
        }
        return;
    }

    if (f->text_len < CLARITY_TEXT_BUF - 1) {
        f->text[f->text_len++] = tc;
    }
    doc->dirty = TRUE;
}

/* ------------------------------------------------------------------ */
/* Horizontal LTR text rendering                                        */
/* ------------------------------------------------------------------ */

/*
 * Blit a single 1-bit glyph into dev at (px, py).
 * glyph: pointer to GLYPH_H rows of bytes, each byte = 8 pixels MSB first.
 */
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

/*
 * Render TextFrame text in horizontal LTR mode.
 * Lines wrap at frame right edge; clipped at frame bottom.
 */
static void render_text_hltr(GDEV *dev, const ClarityFrame *f,
                              int ox, int oy)
{
    if (!dev || !f) return;
    int x0 = f->bounds.left  + ox + 2;
    int y0 = f->bounds.top   + oy + 2;
    int x1 = f->bounds.right + ox - 2;
    int y1 = f->bounds.bottom + oy - 2;

    /* Open a font set at our fixed prototype size */
    WERR fdesc = fopn_fon();
    if (fdesc < 0) {
        /* Font system unavailable: draw placeholder dashes */
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        RECT dash;
        dash.left   = (H)x0;
        dash.top    = (H)y0;
        dash.right  = (H)(x0 + 24);
        dash.bottom = (H)(y0 + 2);
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

    for (UW i = 0; i < f->text_len; i++) {
        UH tc = f->text[i];
        if (tc == TC_NL || tc == TC_CR) {
            cx  = x0;
            cy += GLYPH_H + 2;
            if (cy + GLYPH_H > y1) break;
            continue;
        }

        /* Wrap at right edge */
        if (cx + GLYPH_W > x1) {
            cx  = x0;
            cy += GLYPH_H + 2;
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
            /* Glyph unavailable: draw a small box */
            RECT box;
            box.left   = (H)cx;
            box.top    = (H)cy;
            box.right  = (H)(cx + GLYPH_W - 1);
            box.bottom = (H)(cy + GLYPH_H - 1);
            set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
            drw_rec(dev, &box);
            cx += GLYPH_W + 1;
        }
    }

    /* Cursor caret (blinking handled at higher level; draw always for now) */
    if (cx < x1 && cy + GLYPH_H <= y1) {
        drw_lin(dev, (H)cx, (H)cy, (H)cx, (H)(cy + GLYPH_H - 1));
    }

    fcls_fon((W)fdesc);
}

/* ------------------------------------------------------------------ */
/* Vertical RTL text rendering (縦書き)                                  */
/* ------------------------------------------------------------------ */

/*
 * Render TextFrame text in vertical RTL mode.
 * Columns run right-to-left; lines within each column run top-to-bottom.
 */
static void render_text_vrtl(GDEV *dev, const ClarityFrame *f,
                              int ox, int oy)
{
    if (!dev || !f) return;
    int x0 = f->bounds.left   + ox + 2;
    int y0 = f->bounds.top    + oy + 2;
    int x1 = f->bounds.right  + ox - 2;
    int y1 = f->bounds.bottom + oy - 2;

    WERR fdesc = fopn_fon();
    if (fdesc < 0) {
        set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
        drw_lin(dev, (H)x1, (H)y0, (H)x1, (H)(y0 + 20));
        return;
    }

    FSSPEC spec;
    memset(&spec, 0, sizeof(spec));
    spec.fclass = FTC_MINCHO;  /* Mincho for traditional vertical Japanese */
    spec.size.width  = GLYPH_W;
    spec.size.height = GLYPH_H;
    fset_fon((W)fdesc, &spec);

    /* Start from the right column, go left */
    int col_x = x1 - GLYPH_W;
    int cy    = y0;

    for (UW i = 0; i < f->text_len; i++) {
        UH tc = f->text[i];
        if (tc == TC_NL || tc == TC_CR) {
            col_x -= GLYPH_W + 2;
            cy = y0;
            if (col_x < x0) break;
            continue;
        }

        if (cy + GLYPH_H > y1) {
            /* Next column */
            col_x -= GLYPH_W + 2;
            cy = y0;
            if (col_x < x0) break;
        }

        UB glyph_buf[128];
        FDATA *fd = (FDATA *)(void *)glyph_buf;
        WERR res = fget_img((W)fdesc, fd, (W)sizeof(glyph_buf),
                            0, tc, FT_IMAGE);
        if (res >= 0 && fd->image) {
            blit_glyph_1bit(dev, fd->image, col_x, cy, COLOR_BLACK,
                            fd->asize.width  ? fd->asize.width  : GLYPH_W,
                            fd->asize.height ? fd->asize.height : GLYPH_H);
            cy += (fd->asize.height ? fd->asize.height : GLYPH_H) + 1;
        } else {
            RECT box;
            box.left   = (H)col_x;
            box.top    = (H)cy;
            box.right  = (H)(col_x + GLYPH_W - 1);
            box.bottom = (H)(cy    + GLYPH_H - 1);
            set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
            drw_rec(dev, &box);
            cy += GLYPH_H + 1;
        }
    }

    /* Vertical cursor */
    if (col_x >= x0 && cy + GLYPH_H <= y1)
        drw_lin(dev, (H)col_x, (H)cy, (H)(col_x + GLYPH_W - 1), (H)cy);

    fcls_fon((W)fdesc);
}

/* ------------------------------------------------------------------ */
/* Public text render dispatcher                                        */
/* ------------------------------------------------------------------ */

void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy)
{
    if (!dev || !f || f->type != FRAME_TEXT) return;

    /* Fill frame background */
    RECT bg;
    bg.left   = (H)(f->bounds.left   + ox + 1);
    bg.top    = (H)(f->bounds.top    + oy + 1);
    bg.right  = (H)(f->bounds.right  + ox - 1);
    bg.bottom = (H)(f->bounds.bottom + oy - 1);
    fill_rec(dev, &bg, COLOR_WHITE);

    if (f->flow == FLOW_V_RTL)
        render_text_vrtl(dev, f, ox, oy);
    else
        render_text_hltr(dev, f, ox, oy);
}

/* ------------------------------------------------------------------ */
/* Image render                                                         */
/* ------------------------------------------------------------------ */

/*
 * Blit f->bitmap (raw RGBA, f->bmp_w × f->bmp_h) into the ImageFrame
 * rect using nearest-neighbour scaling.
 */
void clarity_render_image(GDEV *dev, const ClarityFrame *f, int ox, int oy)
{
    if (!dev || !f || f->type != FRAME_IMAGE) return;

    int fx = f->bounds.left   + ox + 1;
    int fy = f->bounds.top    + oy + 1;
    int fw = f->bounds.right  - f->bounds.left - 2;
    int fh = f->bounds.bottom - f->bounds.top  - 2;

    /* Fill with light-grey placeholder if no bitmap */
    RECT bg;
    bg.left   = (H)fx;
    bg.top    = (H)fy;
    bg.right  = (H)(fx + fw);
    bg.bottom = (H)(fy + fh);
    fill_rec(dev, &bg, COLOR_LTGRAY);

    if (!f->bitmap || f->bmp_w == 0 || f->bmp_h == 0 ||
        fw <= 0 || fh <= 0) return;

    /* Nearest-neighbour scale */
    for (int dy = 0; dy < fh; dy++) {
        int src_y = (dy * (int)f->bmp_h) / fh;
        for (int dx = 0; dx < fw; dx++) {
            int src_x = (dx * (int)f->bmp_w) / fw;
            int src_idx = (src_y * (int)f->bmp_w + src_x) * 4;
            UB r = f->bitmap[src_idx + 0];
            UB g = f->bitmap[src_idx + 1];
            UB b = f->bitmap[src_idx + 2];
            UB a = f->bitmap[src_idx + 3];
            COLOR col = ((UW)a << 24) | ((UW)r << 16) |
                        ((UW)g <<  8) |  (UW)b;
            int sx = fx + dx;
            int sy = fy + dy;
            if (sx >= 0 && sx < (int)dev->width &&
                sy >= 0 && sy < (int)dev->height) {
                dev->pixels[sy * dev->width + sx] = col;
            }
        }
    }
}
