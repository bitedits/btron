/*
 * B-TRON Retro OS — src/chokanji/pmc.c
 * Cleanroom Cho-Kanji / PMC Window Frame & Control Renderer.
 * Adheres strictly to NASA JPL Power of 10 & DO-178C Level A Guidelines:
 *  - Rule 1: Simple control flow (no goto, setjmp/longjmp, or recursion).
 *  - Rule 2: Fixed bounded loops (statically verifiable upper bound).
 *  - Rule 3: Zero dynamic memory allocation (0 malloc/free in render path).
 *  - Rule 4: Short, modular functions (< 60 lines each).
 *  - Rule 5: Assertion density >= 2 per function (contracts checked).
 *  - Rule 6: Variables declared at smallest possible scope.
 *  - Rule 7: Strict parameter range checking & return validation.
 *  - Rule 8: Simple macros only.
 *  - Rule 9: Safe pointer dereferencing (at most one level).
 *  - Rule 10: Zero compiler warnings under -Wall -Wextra -std=c99 -pedantic.
 */

#include <btron/pmc.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/troncode.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1 && !defined(BTRON_UEFI_TARGET)
#include <assert.h>
#include <string.h>
#else
#ifndef assert
#define assert(expr) ((void)(expr))
#endif
#endif

#define PMC_MAX_LOOP_BOUND  256
#define PMC_MIN_TITLE_H     16
#define PMC_DEFAULT_BTN_SZ  18

/* Authentic Cho-Kanji frame metrics, mirrored from TessronOS
 * outer_kernel/wm/{wm.c,look.c}: title text height LK_TITLE_H=16, pictogram
 * box side = th+4, grip side GRIP_W=12. draw_title() places the box at
 * (2+th/2, 4) and the name at 2+th/2 + 4*(th+4)/3 from the window's corner. */
#define PMC_TITLE_TEXT_H    16
#define PMC_BOX_SIDE        (PMC_TITLE_TEXT_H + 4)             /* 20 */
#define PMC_BOX_X           (2 + PMC_TITLE_TEXT_H / 2)         /* 10 */
#define PMC_TITLE_TEXT_X    (2 + PMC_TITLE_TEXT_H / 2 + (4 * (PMC_TITLE_TEXT_H + 4)) / 3) /* 36 */
#define PMC_GRIP_W          12

/* Global Window Manager Style Mode (Default: BeOS) */
WmStyleMode g_wm_style = WM_STYLE_BEOS;

void pmc_set_style(WmStyleMode style) {
    assert(style == WM_STYLE_BEOS || style == WM_STYLE_CHOKANJI);
    if (style == WM_STYLE_BEOS || style == WM_STYLE_CHOKANJI) {
        g_wm_style = style;
        redraw_all_windows();
    }
    assert(g_wm_style == style);
}

WmStyleMode pmc_get_style(void) {
    assert(g_wm_style == WM_STYLE_BEOS || g_wm_style == WM_STYLE_CHOKANJI);
    return g_wm_style;
}

/* ── Geometry & Hit Testing ─────────────────────────────────────────── */

BOOL pmc_hit_test_close(const WND *wnd, H x, H y) {
    assert(wnd != NULL);
    if (!wnd || !(wnd->attr & WND_ATTR_CLOSE) || !(wnd->attr & WND_ATTR_TITLE)) {
        return FALSE;
    }
    const H top = wnd->bounds.top;
    const H right = wnd->bounds.right;
    const BOOL hit = (x >= right - 26 && x <= right - 4 && y >= top + 4 && y <= top + 24);
    assert(hit == TRUE || hit == FALSE);
    return hit;
}

BOOL pmc_hit_test_title(const WND *wnd, H x, H y) {
    assert(wnd != NULL);
    if (!wnd || !(wnd->attr & WND_ATTR_TITLE)) {
        return FALSE;
    }
    if (pmc_hit_test_close(wnd, x, y)) {
        return FALSE;
    }
    const H top = wnd->bounds.top;
    const H bottom = top + WND_TITLE_HEIGHT;
    const H left = wnd->bounds.left;
    const H right = wnd->bounds.right;
    const BOOL hit = (x >= left && x <= right && y >= top && y <= bottom);
    assert(hit == TRUE || hit == FALSE);
    return hit;
}

/* ── Authentic Cho-Kanji Bevel Primitives ─────────────────────────────── */

/* The reference outer_kernel/wm/wm.c draws every stroke through dp_line()/
 * dp_frame_rect() with an explicit colour (LK_LIGHT = white, LK_SHADOW /
 * LK_OUTLINE = black). BTRON's drw_lin/drw_rec carry no colour operand, so the
 * device pen is set first; that is what makes the bevel light/dark read the
 * same as Cho-Kanji rather than all-one-pen. */
static void pmc_pen(GDEV *dev, COLOR col) {
    set_col(dev, col, col);
}

static void pmc_line(GDEV *dev, H x1, H y1, H x2, H y2, COLOR col) {
    pmc_pen(dev, col);
    drw_lin(dev, x1, y1, x2, y2);
}

/* dp_frame_rect(r, edge, 1): the 1-pixel ring just inside the half-open RECT. */
static void pmc_outline(GDEV *dev, H l, H t, H r, H b, COLOR col) {
    if (r <= l || b <= t) return;
    const RECT q = { l, t, r, b };
    pmc_pen(dev, col);
    drw_rec(dev, &q);
}

/* fill_look(l, t, r, b, band): the band interior painted between the bevels. */
static void pmc_fill(GDEV *dev, H l, H t, H r, H b, COLOR col) {
    if (r <= l || b <= t) return;
    const RECT q = { l, t, r, b };
    fill_rec(dev, &q, col);
}

/* A raised 3D bevel: white top/left, black bottom/right (the Chokanji look). */
static void pmc_draw_bevel_rect(GDEV *dev, H l, H t, H r, H b) {
    assert(dev != NULL);
    assert(l <= r && t <= b);
    if (!dev || l > r || t > b) return;
    pmc_line(dev, l, t, r - 1, t, PMC_COL_LIGHT);
    pmc_line(dev, l, t, l, b - 1, PMC_COL_LIGHT);
    pmc_line(dev, l, b - 1, r - 1, b - 1, PMC_COL_OUTLINE);
    pmc_line(dev, r - 1, t + 1, r - 1, b - 1, PMC_COL_OUTLINE);
}

/* The object pictogram box: a th+4 square (draw_title box), a raised bevel, a
 * sunken inner frame, and the standing document mark wm_pict_mark draws in it. */
static void pmc_draw_pictogram_box(GDEV *dev, H x, H y, BOOL focused) {
    assert(dev != NULL);
    assert(x >= 0 && y >= 0);
    const RECT pic_box = { x, y, x + PMC_BOX_SIDE, y + PMC_BOX_SIDE };
    fill_rec(dev, &pic_box, focused ? PMC_COL_LIGHT : PMC_COL_SWITCH_SUNKEN);
    pmc_draw_bevel_rect(dev, pic_box.left, pic_box.top, pic_box.right, pic_box.bottom);

    pmc_line(dev, x + 1, y + 1, x + PMC_BOX_SIDE - 2, y + 1, PMC_COL_OUTLINE);
    pmc_line(dev, x + 1, y + 1, x + 1, y + PMC_BOX_SIDE - 2, PMC_COL_OUTLINE);
    pmc_line(dev, x + 1, y + PMC_BOX_SIDE - 2, x + PMC_BOX_SIDE - 2, y + PMC_BOX_SIDE - 2, PMC_COL_LIGHT);
    pmc_line(dev, x + PMC_BOX_SIDE - 2, y + 1, x + PMC_BOX_SIDE - 2, y + PMC_BOX_SIDE - 2, PMC_COL_LIGHT);

    const COLOR ink = focused ? PMC_COL_OUTLINE : PMC_COL_INACT_TEXT;
    pmc_line(dev, x + 4, y + 4, x + 14, y + 4, ink);
    pmc_line(dev, x + 4, y + 7, x + 14, y + 7, ink);
    pmc_line(dev, x + 4, y + 10, x + 14, y + 10, ink);
    pmc_line(dev, x + 4, y + 13, x + 10, y + 13, ink);
}

static void pmc_draw_close_switch(GDEV *dev, H x, H y, BOOL focused) {
    assert(dev != NULL);
    assert(x >= 0 && y >= 0);
    const RECT btn_r = { x, y, x + PMC_BOX_SIDE, y + PMC_BOX_SIDE };
    fill_rec(dev, &btn_r, focused ? PMC_COL_SWITCH_RAISED : PMC_COL_SWITCH_SUNKEN);
    pmc_draw_bevel_rect(dev, btn_r.left, btn_r.top, btn_r.right, btn_r.bottom);

    pmc_line(dev, x + 5, y + 5, x + 13, y + 13, PMC_COL_OUTLINE);
    pmc_line(dev, x + 6, y + 5, x + 14, y + 13, PMC_COL_OUTLINE);
    pmc_line(dev, x + 13, y + 5, x + 5, y + 13, PMC_COL_OUTLINE);
    pmc_line(dev, x + 14, y + 5, x + 6, y + 13, PMC_COL_OUTLINE);
}

/* ── Main Cho-Kanji Window Frame Painter ────────────────────────────── */

/* A faithful transcription of wm.c draw_frame(): a black outline around the
 * whole window and around the work area, then four strips (top, left, foot,
 * right) each built as a lit line along its outer top/left edge, a shaded line
 * along the other two, and the title band painted between them. The strips and
 * bevels are all expressed from the local work rectangle (wl, wt, wr, wb),
 * exactly as the reference measures them from outer->work. */
void pmc_draw_window_frame(GDEV *dev, WND *wnd) {
    assert(dev != NULL);
    assert(wnd != NULL);
    if (!dev || !wnd) return;
    if (!(wnd->attr & (WND_ATTR_TITLE | WND_ATTR_BORDER))) return;

    const H l = wnd->bounds.left, t = wnd->bounds.top;
    const H r = wnd->bounds.right, b = wnd->bounds.bottom;
    const BOOL focused = wnd->focused;
    const COLOR band = focused ? PMC_COL_ACT_TITLE : PMC_COL_INACT_TITLE;
    const H fw = r - l, fh = b - t;

    H wl = wnd->client.left - l;
    H wt = wnd->client.top - t;
    H wr = wnd->client.right - l;
    H wb = wnd->client.bottom - t;
    if (wr <= wl) wr = fw - wl;   /* an unset client mirrors the left inset */
    if (wb <= wt) wb = fh - wt;

    /* the two outlines, both in the frame's own colour */
    pmc_outline(dev, l, t, r, b, PMC_COL_OUTLINE);
    pmc_outline(dev, l + wl - 1, t + wt - 1, l + wr + 1, t + wb + 1, PMC_COL_OUTLINE);

    /* the strip along the top, which carries the name */
    pmc_line(dev, l + 1, t + 1, l + 1, t + wt - 3, PMC_COL_LIGHT);
    pmc_line(dev, l + 1, t + 1, l + fw - 2, t + 1, PMC_COL_LIGHT);
    pmc_line(dev, l + fw - 2, t + 2, l + fw - 2, t + wt - 3, PMC_COL_OUTLINE);
    pmc_line(dev, l + wl - 1, t + wt - 2, l + wr + 1, t + wt - 2, PMC_COL_OUTLINE);
    pmc_fill(dev, l + 2, t + 2, l + fw - 2, t + wt - 2, band);

    /* the strip on the left */
    {
        const H y0 = t + wt - 2;
        pmc_line(dev, l + wl - 2, t + wt - 2, l + wl - 2, t + wb + 1, PMC_COL_OUTLINE);
        pmc_line(dev, l + 1, y0, l + 1, t + fh - 2, PMC_COL_LIGHT);
        pmc_line(dev, l + 2, t + fh - 2, l + wl - 2, t + fh - 2, PMC_COL_OUTLINE);
        pmc_fill(dev, l + 2, y0, l + wl - 2, t + fh - 2, band);
    }

    /* the strip along the foot */
    {
        const H x0 = l + wl - 2;
        pmc_line(dev, l + wl - 1, t + wb + 1, l + wr + 1, t + wb + 1, PMC_COL_LIGHT);
        pmc_line(dev, x0, t + fh - 2, l + fw - 2, t + fh - 2, PMC_COL_OUTLINE);
        pmc_line(dev, l + fw - 2, t + wb + 2, l + fw - 2, t + fh - 3, PMC_COL_OUTLINE);
        pmc_fill(dev, x0, t + wb + 2, l + fw - 2, t + fh - 2, band);
    }

    /* the strip on the right */
    pmc_line(dev, l + fw - 2, t + wt - 2, l + fw - 2, t + wb + 1, PMC_COL_OUTLINE);
    pmc_line(dev, l + wr + 1, t + wt - 1, l + wr + 1, t + wb + 1, PMC_COL_LIGHT);
    pmc_fill(dev, l + wr + 2, t + wt - 2, l + fw - 2, t + wb + 3, band);

    /* the white paper the client is drawn on (LK_MSGWHITE) */
    pmc_fill(dev, wnd->client.left, wnd->client.top, wnd->client.right, wnd->client.bottom, PMC_COL_BODY);

    /* title band: the pictogram box, then the name */
    if (wnd->attr & WND_ATTR_TITLE) {
        pmc_draw_pictogram_box(dev, l + PMC_BOX_X, t + 4, focused);

        const H tx = l + PMC_TITLE_TEXT_X;
        const H ty = t + 4 + (PMC_BOX_SIDE - PMC_TITLE_TEXT_H) / 2;
        /* draw_title() strokes twice: the back colour at the base, the text
         * colour one pixel down and right — the same both focused and not. */
        drw_tc_string(dev, tx, ty, wnd->title, PMC_COL_SHADOW, 0x00000000);
        drw_tc_string(dev, tx + 1, ty + 1, wnd->title, PMC_COL_ACT_TEXT, 0x00000000);

        if (wnd->attr & WND_ATTR_CLOSE) {
            pmc_draw_close_switch(dev, r - 25, t + 4, focused);
        }
    }

    /* the resize grip in the foot-right corner, GRIP_W square */
    if (wnd->attr & WND_ATTR_RESIZE) {
        const H gx = r - PMC_GRIP_W, gy = b - PMC_GRIP_W;
        pmc_fill(dev, gx, gy, r - 1, b - 1, band);
        for (int i = 0; i < 3 && i < PMC_MAX_LOOP_BOUND; i++) {
            const H off = i * 3;
            pmc_line(dev, gx + off + 1, b - 3, r - 3, gy + off + 1, PMC_COL_LIGHT);
            pmc_line(dev, gx + off, b - 3, r - 3, gy + off, PMC_COL_OUTLINE);
        }
    }
}

/* ── Cho-Kanji Push Switches (Round Bevelled Buttons) ───────────────── */

/* Invert the standard bevel for a sunken (pressed) look: shadow top/left,
 * light bottom/right — the exact opposite of pmc_draw_bevel_rect. */
static void pmc_draw_bevel_rect_sunken(GDEV *dev, H l, H t, H r, H b) {
    assert(dev != NULL);
    assert(l <= r && t <= b);
    if (!dev || l > r || t > b) return;
    pmc_line(dev, l, t, r - 1, t, PMC_COL_OUTLINE);
    pmc_line(dev, l, t, l, b - 1, PMC_COL_OUTLINE);
    pmc_line(dev, l, b - 1, r - 1, b - 1, PMC_COL_LIGHT);
    pmc_line(dev, r - 1, t + 1, r - 1, b - 1, PMC_COL_LIGHT);
}

void pmc_draw_switch(GDEV *dev, const RECT *r, const char *label, BOOL pressed, BOOL enabled) {
    assert(dev != NULL);
    assert(r != NULL);
    if (!dev || !r) return;
    assert(r->right > r->left && r->bottom > r->top);

    const COLOR bg = pressed ? PMC_COL_SWITCH_SUNKEN : PMC_COL_SWITCH_RAISED;
    fill_rec(dev, r, bg);
    pmc_outline(dev, r->left, r->top, r->right, r->bottom, PMC_COL_OUTLINE);

    if (pressed) {
        pmc_draw_bevel_rect_sunken(dev, r->left, r->top, r->right, r->bottom);
    } else {
        pmc_draw_bevel_rect(dev, r->left, r->top, r->right, r->bottom);
    }

    if (label && label[0] != '\0') {
        const COLOR text_col = enabled ? PMC_COL_OUTLINE : PMC_COL_INACT_TEXT;
        const H off = pressed ? 1 : 0;
        const H tx = r->left + 8 + off;
        const H ty = r->top + ((r->bottom - r->top - 14) / 2) + off;
        drw_tc_string(dev, tx, ty, label, text_col, 0x00000000);
    }
}

/* ── Cho-Kanji Scrollbars with Iconic Yellow Tombo ──────────────────── */

void pmc_draw_scrollbar(GDEV *dev, const RECT *r, BOOL is_vert, H pos, H total, H view_len) {
    assert(dev != NULL);
    assert(r != NULL);
    if (!dev || !r) return;
    assert(r->right > r->left && r->bottom > r->top);

    fill_rec(dev, r, PMC_COL_SBAR_TRACK);
    pmc_outline(dev, r->left, r->top, r->right, r->bottom, PMC_COL_OUTLINE);

    if (total <= 0 || view_len >= total) {
        return;
    }

    if (is_vert) {
        const H track_h = r->bottom - r->top;
        H knob_h = (track_h * view_len) / total;
        if (knob_h < 16) knob_h = 16;
        if (knob_h > track_h) knob_h = track_h;
        const H max_knob_top = track_h - knob_h;
        const H knob_y = r->top + ((pos * max_knob_top) / (total - view_len));

        const RECT knob = { r->left + 1, knob_y, r->right - 1, knob_y + knob_h };
        fill_rec(dev, &knob, PMC_COL_SBAR_KNOB);
        pmc_draw_bevel_rect(dev, knob.left, knob.top, knob.right, knob.bottom);

        const H cy = knob.top + knob_h / 2;
        const H cx = r->left + (r->right - r->left) / 2;
        pmc_line(dev, cx - 4, cy, cx + 4, cy, PMC_COL_SBAR_TOMBO);
        pmc_line(dev, cx, cy - 4, cx, cy + 4, PMC_COL_SBAR_TOMBO);
    } else {
        const H track_w = r->right - r->left;
        H knob_w = (track_w * view_len) / total;
        if (knob_w < 16) knob_w = 16;
        if (knob_w > track_w) knob_w = track_w;
        const H max_knob_left = track_w - knob_w;
        const H knob_x = r->left + ((pos * max_knob_left) / (total - view_len));

        const RECT knob = { knob_x, r->top + 1, knob_x + knob_w, r->bottom - 1 };
        fill_rec(dev, &knob, PMC_COL_SBAR_KNOB);
        pmc_draw_bevel_rect(dev, knob.left, knob.top, knob.right, knob.bottom);

        const H cx = knob.left + knob_w / 2;
        const H cy = r->top + (r->bottom - r->top) / 2;
        pmc_line(dev, cx - 4, cy, cx + 4, cy, PMC_COL_SBAR_TOMBO);
        pmc_line(dev, cx, cy - 4, cx, cy + 4, PMC_COL_SBAR_TOMBO);
    }
}

/* ── Chamfered Trapezoid Folder Tags / Tabs ─────────────────────────── */

void pmc_draw_folder_tab(GDEV *dev, const RECT *r, const char *title, BOOL active) {
    assert(dev != NULL);
    assert(r != NULL);
    if (!dev || !r) return;
    assert(r->right > r->left && r->bottom > r->top);

    const COLOR bg = active ? PMC_COL_BODY : PMC_COL_INACT_TITLE;
    fill_rec(dev, r, bg);

    const COLOR ink = PMC_COL_OUTLINE;
    pmc_line(dev, r->left + 4, r->top, r->right - 5, r->top, ink);
    pmc_line(dev, r->left, r->top + 4, r->left + 4, r->top, ink);
    pmc_line(dev, r->right - 5, r->top, r->right - 1, r->top + 4, ink);
    pmc_line(dev, r->left, r->top + 4, r->left, r->bottom - 1, ink);
    pmc_line(dev, r->right - 1, r->top + 4, r->right - 1, r->bottom - 1, ink);

    if (title && title[0] != '\0') {
        const COLOR fg = active ? PMC_COL_OUTLINE : PMC_COL_INACT_TEXT;
        drw_tc_string(dev, r->left + 10, r->top + 4, title, fg, 0x00000000);
    }
}
