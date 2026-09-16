/*
 * B-System (BTRON 3.20) Clarity DTP Engine – Render Module (src/apps/clarity_render.c)
 * Horizontal LTR, vertical RTL, word-wrapped interactive typing with caret insertion,
 * backspace, delete, arrow navigation (including Up/Down), and zoom scaling.
 */

#include "clarity_doc.h"
#include <btron/dp.h>
#include <btron/font_mgr.h>
#include <btron/troncode.h>
#include <btron/tad_browser.h>
#include <btron/tip.h>
#include <stdio.h>

#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#include <string.h>
#else
#include <string.h>
#endif

#define GLYPH_W  16
#define GLYPH_H  16

static inline int get_char_adv(UH tc, UH prev_tc)
{
    return tc_get_char_advance((TC)tc, (TC)prev_tc);
}

/* ------------------------------------------------------------------ */
/* Visual Line Layout for Word Wrapping                                */
/* ------------------------------------------------------------------ */

typedef struct {
    int start_pos;
    int end_pos;
    int y_rel;
} VisualLine;

static int clarity_compute_visual_lines_hltr(const ClarityFrame *f, int inner_w, VisualLine *lines, int max_lines)
{
    if (!f || max_lines <= 0) return 0;
    if (inner_w < 32) inner_w = 32;

    int line_count = 0;
    int line_start = 0;
    int last_break = -1;
    int cur_w = 0;
    int y = 0;
    UH prev_tc = 0;

    int i = 0;
    while (i < (int)f->text_len && line_count < max_lines) {
        UH tc = f->text[i];
        if (tc == '\n' || tc == '\r') {
            lines[line_count].start_pos = line_start;
            lines[line_count].end_pos = i;
            lines[line_count].y_rel = y;
            line_count++;
            y += GLYPH_H + 3;
            line_start = i + 1;
            last_break = -1;
            cur_w = 0;
            prev_tc = 0;
            i++;
            continue;
        }

        /* Check if this position begins a Virtual Body moniker [tag label] */
        int matched_vb = -1;
        for (int vi = 0; vi < f->vobj_count; vi++) {
            if ((int)f->vobjs[vi].text_offset == i) {
                matched_vb = vi;
                break;
            }
        }
        if (matched_vb >= 0) {
            const ClarityVObjLink *vl = &f->vobjs[matched_vb];
            const char *tag = (vl->type == VOBJ_TYPE_DRAW) ? "[IMG]" :
                              (strstr(vl->path, ".tad") || strstr(vl->path, ".TAD")) ? "[TAD]" :
                              (strstr(vl->path, ".md") || strstr(vl->path, ".MD")) ? "[MD]" : "[TXT]";
            char badge_str[128];
            snprintf(badge_str, sizeof(badge_str), "%s %s", tag, vl->label);
            int bw = tc_calc_string_width(badge_str, (int)strlen(badge_str)) + 12;
            if (bw < 40) bw = 40;

            if (cur_w + bw + 4 > inner_w && cur_w > 0) {
                lines[line_count].start_pos = line_start;
                lines[line_count].end_pos = i;
                lines[line_count].y_rel = y;
                line_count++;
                y += GLYPH_H + 3;
                line_start = i;
                last_break = -1;
                cur_w = 0;
                prev_tc = 0;
            }

            cur_w += bw + 4;
            i++;
            while (i < (int)f->text_len && f->text[i - 1] != ']') {
                i++;
            }
            last_break = i;
            prev_tc = 0;
            continue;
        }

        int adv = get_char_adv(tc, prev_tc);
        if (tc == ' ' || tc == '\t') {
            last_break = i;
        } else if (tc == '-' || tc == '/') {
            last_break = i + 1;
        } else if (((tc >> 8) == 0x9F || (tc >> 8) == 0x6F) &&
                   ((tc & 0xFF) == 0x0B || (tc & 0xFF) == 0x0C || (tc & 0xFF) == 0x0D || (tc & 0xFF) == 0x0E)) {
            /* Tibetan Tsheg / Shad word break */
            last_break = i + 1;
        } else if (tc >= 0x3000) {
            /* CJK Ideograph break point */
            last_break = i;
        }

        if (cur_w + adv > inner_w && cur_w > 0) {
            int break_pt = (last_break > line_start) ? last_break : i;
            lines[line_count].start_pos = line_start;
            lines[line_count].end_pos = break_pt;
            lines[line_count].y_rel = y;
            line_count++;
            y += GLYPH_H + 3;

            if (break_pt < (int)f->text_len && f->text[break_pt] == ' ') {
                line_start = break_pt + 1;
                i = break_pt + 1;
            } else {
                line_start = break_pt;
                i = break_pt;
            }
            last_break = -1;
            cur_w = 0;
            prev_tc = 0;
            continue;
        }

        cur_w += adv;
        if (adv > 0 || (tc >> 8) != 0x9F) {
            prev_tc = tc;
        }
        i++;
    }

    if (line_count < max_lines) {
        lines[line_count].start_pos = line_start;
        lines[line_count].end_pos = (int)f->text_len;
        lines[line_count].y_rel = y;
        line_count++;
    }

    return line_count;
}

/* ------------------------------------------------------------------ */
/* Key Actions for Text Frames                                          */
/* ------------------------------------------------------------------ */

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

        case CLARITY_ACT_UP: {
            if (f->flow == FLOW_V_RTL) {
                if (f->cursor_pos > 0) f->cursor_pos--;
                break;
            }
            int inner_w = (f->bounds.right - f->bounds.left) - 8;
            VisualLine lines[256];
            int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
            if (nlines <= 0) break;

            int cur_l = 0;
            for (int l = 0; l < nlines; l++) {
                if (f->cursor_pos >= lines[l].start_pos && f->cursor_pos <= lines[l].end_pos) {
                    cur_l = l;
                    if (f->cursor_pos < lines[l].end_pos) break;
                }
            }
            if (cur_l > 0) {
                int target_l = cur_l - 1;
                int col_px = 0;
                UH prev_tmp = 0;
                for (int p = lines[cur_l].start_pos; p < f->cursor_pos; p++) {
                    int a = get_char_adv(f->text[p], prev_tmp);
                    col_px += a;
                    if (a > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                int cx = 0;
                int p = lines[target_l].start_pos;
                prev_tmp = 0;
                for (; p < lines[target_l].end_pos; p++) {
                    int adv = get_char_adv(f->text[p], prev_tmp);
                    if (col_px <= cx + adv / 2) break;
                    cx += adv;
                    if (adv > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                f->cursor_pos = p;
            } else {
                f->cursor_pos = 0;
            }
            break;
        }

        case CLARITY_ACT_DOWN: {
            if (f->flow == FLOW_V_RTL) {
                if (f->cursor_pos < (int)f->text_len) f->cursor_pos++;
                break;
            }
            int inner_w = (f->bounds.right - f->bounds.left) - 8;
            VisualLine lines[256];
            int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
            if (nlines <= 0) break;

            int cur_l = 0;
            for (int l = 0; l < nlines; l++) {
                if (f->cursor_pos >= lines[l].start_pos && f->cursor_pos <= lines[l].end_pos) {
                    cur_l = l;
                    if (f->cursor_pos < lines[l].end_pos) break;
                }
            }
            if (cur_l < nlines - 1) {
                int target_l = cur_l + 1;
                int col_px = 0;
                UH prev_tmp = 0;
                for (int p = lines[cur_l].start_pos; p < f->cursor_pos; p++) {
                    int a = get_char_adv(f->text[p], prev_tmp);
                    col_px += a;
                    if (a > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                int cx = 0;
                int p = lines[target_l].start_pos;
                prev_tmp = 0;
                for (; p < lines[target_l].end_pos; p++) {
                    int adv = get_char_adv(f->text[p], prev_tmp);
                    if (col_px <= cx + adv / 2) break;
                    cx += adv;
                    if (adv > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                f->cursor_pos = p;
            } else {
                f->cursor_pos = (int)f->text_len;
            }
            break;
        }

        case CLARITY_ACT_HOME: {
            int inner_w = (f->bounds.right - f->bounds.left) - 8;
            VisualLine lines[256];
            int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
            for (int l = 0; l < nlines; l++) {
                if (f->cursor_pos >= lines[l].start_pos && f->cursor_pos <= lines[l].end_pos) {
                    f->cursor_pos = lines[l].start_pos;
                    break;
                }
            }
            break;
        }

        case CLARITY_ACT_END: {
            int inner_w = (f->bounds.right - f->bounds.left) - 8;
            VisualLine lines[256];
            int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
            for (int l = 0; l < nlines; l++) {
                if (f->cursor_pos >= lines[l].start_pos && f->cursor_pos <= lines[l].end_pos) {
                    f->cursor_pos = lines[l].end_pos;
                    break;
                }
            }
            break;
        }

        case CLARITY_ACT_PAGEUP: {
            int inner_h = (f->bounds.bottom - f->bounds.top) - 8;
            f->scroll_y -= (inner_h > 0 ? inner_h / 2 : 40);
            if (f->scroll_y < 0) f->scroll_y = 0;
            doc->dirty = TRUE;
            break;
        }

        case CLARITY_ACT_PAGEDOWN: {
            int inner_w = (f->bounds.right - f->bounds.left) - 8;
            int inner_h = (f->bounds.bottom - f->bounds.top) - 8;
            if (inner_w < 32) inner_w = 32;
            VisualLine lines[256];
            int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
            int total_h = (nlines > 0) ? (lines[nlines - 1].y_rel + GLYPH_H + 3) : 0;
            int max_scroll = total_h - inner_h;
            if (max_scroll < 0) max_scroll = 0;

            f->scroll_y += (inner_h > 0 ? inner_h / 2 : 40);
            if (f->scroll_y > max_scroll) f->scroll_y = max_scroll;
            doc->dirty = TRUE;
            break;
        }

        default:
            break;
    }

    /* Auto-scroll to keep caret in visible area on typing/arrow navigation */
    if (f->flow != FLOW_V_RTL && action != CLARITY_ACT_PAGEUP && action != CLARITY_ACT_PAGEDOWN) {
        int inner_w = (f->bounds.right - f->bounds.left) - 8;
        int inner_h = (f->bounds.bottom - f->bounds.top) - 8;
        if (inner_w < 32) inner_w = 32;
        VisualLine lines[256];
        int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
        if (nlines > 0 && inner_h > 0) {
            int cur_l = 0;
            for (int l = 0; l < nlines; l++) {
                if (f->cursor_pos >= lines[l].start_pos && f->cursor_pos <= lines[l].end_pos) {
                    cur_l = l;
                    if (f->cursor_pos < lines[l].end_pos) break;
                }
            }
            int ly = lines[cur_l].y_rel;
            if (ly < f->scroll_y) {
                f->scroll_y = ly;
            } else if (ly + GLYPH_H + 3 > f->scroll_y + inner_h) {
                f->scroll_y = ly + GLYPH_H + 3 - inner_h;
            }
            int total_h = lines[nlines - 1].y_rel + GLYPH_H + 3;
            int max_scroll = total_h - inner_h;
            if (max_scroll < 0) max_scroll = 0;
            if (f->scroll_y > max_scroll) f->scroll_y = max_scroll;
            if (f->scroll_y < 0) f->scroll_y = 0;
        }
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
/* Click to Caret Position Finder                                      */
/* ------------------------------------------------------------------ */

int clarity_text_xy_to_pos(const ClarityFrame *f, H mx, H my, int ox, int oy, int zoom_pct)
{
    if (!f || f->type != FRAME_TEXT || f->text_len == 0) return 0;
    int zoom = zoom_pct > 0 ? zoom_pct : 100;

    int x0 = ox + (f->bounds.left   * zoom) / 100 + 4;
    int y0 = oy + (f->bounds.top    * zoom) / 100 + 4;
    int x1 = ox + (f->bounds.right  * zoom) / 100 - 4;
    int y1 = oy + (f->bounds.bottom * zoom) / 100 - 4;

    if (f->flow == FLOW_V_RTL) {
        int col_x = x1 - GLYPH_W;
        int cy    = y0;
        int best_pos = 0;
        int best_dist = 999999;

        for (int i = 0; i <= (int)f->text_len; i++) {
            int dx = mx - col_x;
            int dy = my - cy;
            int dist = dx * dx + dy * dy;
            if (dist < best_dist) {
                best_dist = dist;
                best_pos = i;
            }
            if (i < (int)f->text_len) {
                UH tc = f->text[i];
                if (tc == '\n' || tc == '\r') {
                    col_x -= GLYPH_W + 4;
                    cy = y0;
                } else {
                    cy += GLYPH_H + 2;
                    if (cy + GLYPH_H > y1) {
                        col_x -= GLYPH_W + 4;
                        cy = y0;
                    }
                }
            }
        }
        return best_pos;
    }

    int inner_w = x1 - x0;
    VisualLine lines[256];
    int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);
    if (nlines <= 0) return 0;

    int scroll_y = (f->scroll_y * zoom) / 100;
    int target_l = 0;
    if (my < y0 + lines[0].y_rel - scroll_y) {
        target_l = 0;
    } else {
        target_l = nlines - 1;
        for (int l = 0; l < nlines; l++) {
            int ly = y0 + lines[l].y_rel - scroll_y;
            if (my >= ly && my < ly + GLYPH_H + 3) {
                target_l = l;
                break;
            }
        }
    }

    if (mx <= x0) return lines[target_l].start_pos;

    int cx = x0;
    UH prev_tmp = 0;
    int p = lines[target_l].start_pos;
    for (; p < lines[target_l].end_pos; p++) {
        int adv = get_char_adv(f->text[p], prev_tmp);
        if (mx < cx + adv / 2) return p;
        cx += adv;
        if (adv > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
    }
    return lines[target_l].end_pos;
}

/* ------------------------------------------------------------------ */
/* Glyph blitter                                                        */
/* ------------------------------------------------------------------ */

static void blit_glyph_1bit(GDEV *dev, const UB *glyph, int px, int py,
                             COLOR fg, int gw, int gh)
{
    if (!dev || !glyph) return;
    int bytes_per_row = (gw + 7) / 8;
    for (int y = 0; y < gh; y++) {
        int sy = py + y;
        if (sy < dev->clip.top || sy >= dev->clip.bottom) continue;
        for (int x = 0; x < gw; x++) {
            int sx = px + x;
            if (sx < dev->clip.left || sx >= dev->clip.right) continue;
            int byte_idx = y * bytes_per_row + (x >> 3);
            int bit_idx  = 7 - (x & 7);
            if (glyph[byte_idx] & (1 << bit_idx)) {
                dev->pixels[sy * dev->width + sx] = fg;
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Horizontal LTR text rendering (Word-Wrapped with Internal Scroll)    */
/* ------------------------------------------------------------------ */

static void render_text_hltr_zoom(GDEV *dev, const ClarityFrame *f,
                                 int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f) return;
    int x0 = ox + (f->bounds.left   * zoom) / 100 + 4;
    int y0 = oy + (f->bounds.top    * zoom) / 100 + 4;
    int x1 = ox + (f->bounds.right  * zoom) / 100 - 4;
    int y1 = oy + (f->bounds.bottom * zoom) / 100 - 4;

    if (x1 <= x0 || y1 <= y0) return;

    /* Set strict clipping to frame interior - no drawing spills outside */
    RECT orig_clip = dev->clip;
    RECT f_clip = { (H)x0, (H)y0, (H)x1, (H)y1 };
    if (f_clip.left < orig_clip.left) f_clip.left = orig_clip.left;
    if (f_clip.top < orig_clip.top) f_clip.top = orig_clip.top;
    if (f_clip.right > orig_clip.right) f_clip.right = orig_clip.right;
    if (f_clip.bottom > orig_clip.bottom) f_clip.bottom = orig_clip.bottom;
    set_clip(dev, &f_clip);

    int inner_w = x1 - x0;
    VisualLine lines[256];
    int nlines = clarity_compute_visual_lines_hltr(f, inner_w, lines, 256);

    int scroll_y = (f->scroll_y * zoom) / 100;
    int caret_x = x0, caret_y = y0;
    BOOL caret_found = FALSE;
    int c_pos = (f->cursor_pos >= 0 && f->cursor_pos <= (int)f->text_len)
                ? f->cursor_pos : (int)f->text_len;

    for (int l = 0; l < nlines; l++) {
        int cx = x0;
        int cy = y0 + lines[l].y_rel - scroll_y;

        /* Skip lines outside the frame clipping viewport */
        if (cy + GLYPH_H + 3 < y0) {
            if (!caret_found && c_pos >= lines[l].start_pos && c_pos <= lines[l].end_pos) {
                UH prev_tmp = 0;
                int temp_cx = x0;
                for (int p = lines[l].start_pos; p < c_pos; p++) {
                    int a = get_char_adv(f->text[p], prev_tmp);
                    temp_cx += a;
                    if (a > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                caret_x = temp_cx;
                caret_y = cy;
                caret_found = TRUE;
            }
            continue;
        }
        if (cy > y1) {
            if (!caret_found && c_pos >= lines[l].start_pos && c_pos <= lines[l].end_pos) {
                UH prev_tmp = 0;
                int temp_cx = x0;
                for (int p = lines[l].start_pos; p < c_pos; p++) {
                    int a = get_char_adv(f->text[p], prev_tmp);
                    temp_cx += a;
                    if (a > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
                }
                caret_x = temp_cx;
                caret_y = cy;
                caret_found = TRUE;
            }
            continue;
        }

        UH prev_tc = 0;
        int prev_adv = 8;

        for (int p = lines[l].start_pos; p < lines[l].end_pos; ) {
            if (p == c_pos) {
                caret_x = cx;
                caret_y = cy;
                caret_found = TRUE;
            }

            /* Check if this position begins a Virtual Body moniker [tag label] */
            int matched_vb = -1;
            for (int vi = 0; vi < f->vobj_count; vi++) {
                if ((int)f->vobjs[vi].text_offset == p) {
                    matched_vb = vi;
                    break;
                }
            }

            if (matched_vb >= 0) {
                ClarityVObjLink *vl = (ClarityVObjLink*)&f->vobjs[matched_vb];
                const char *tag = (vl->type == VOBJ_TYPE_DRAW) ? "[IMG]" :
                                  (strstr(vl->path, ".tad") || strstr(vl->path, ".TAD")) ? "[TAD]" :
                                  (strstr(vl->path, ".md") || strstr(vl->path, ".MD")) ? "[MD]" : "[TXT]";
                char badge_str[128];
                snprintf(badge_str, sizeof(badge_str), "%s %s", tag, vl->label);
                int bw = tc_calc_string_width(badge_str, (int)strlen(badge_str)) + 12;
                if (bw < 40) bw = 40;

                RECT br = { (H)cx, (H)cy, (H)(cx + bw), (H)(cy + GLYPH_H) };
                fill_rec(dev, &br, COLOR_LTGRAY);
                set_col(dev, COLOR_NAVY, COLOR_LTGRAY);
                drw_rec(dev, &br);
                drw_tc_string(dev, cx + 4, cy, badge_str, COLOR_NAVY, COLOR_LTGRAY);

                vl->box = br;

                cx += bw + 4;
                /* Skip characters of moniker in f->text */
                p++;
                while (p < lines[l].end_pos && p < (int)f->text_len && f->text[p - 1] != ']') {
                    p++;
                }
                prev_tc = 0;
                prev_adv = 8;
                continue;
            }

            UH tc = f->text[p];
            int adv = get_char_adv(tc, prev_tc);

            H gw = 8, gh = GLYPH_H;
            const UB *bmp = get_glyph_bitmap((TC)tc, &gw, &gh);
            int render_x = cx;

            /* Tibetan Typography & Stacking Rules */
            if ((tc >> 8) == 0x9F) {
                UW cp = 0x0F00 + (tc & 0xFF);
                if ((cp >= 0x0F71 && cp <= 0x0F84) || (cp >= 0x0F90 && cp <= 0x0FBC)) {
                    /* Combining Vowel / Subjoined Consonant: stack onto previous base */
                    render_x = (prev_adv > 0) ? (cx - prev_adv) : cx;
                    adv = 0;
                } else if (cp == 0x0F0B || cp == 0x0F0C || cp == 0x0F0D || cp == 0x0F0E) {
                    adv = 3;
                } else {
                    adv = 8;
                }
            } else if (tc == ' ' && prev_tc == 0x9F0B) {
                adv = 0;
            }

            if (bmp) {
                blit_glyph_1bit(dev, bmp, render_x, cy, COLOR_BLACK, gw, gh);
            } else if (adv > 0) {
                RECT box = { (H)render_x, (H)cy, (H)(render_x + adv - 1), (H)(cy + GLYPH_H - 1) };
                set_col(dev, COLOR_DKGRAY, COLOR_WHITE);
                drw_rec(dev, &box);
            }

            cx += adv;
            if (adv > 0 || (tc >> 8) != 0x9F) {
                prev_adv = adv;
                prev_tc = tc;
            }
            p++;
        }

        if (!caret_found && c_pos == lines[l].end_pos && (l == nlines - 1 || f->text[c_pos - 1] == '\n' || f->text[c_pos - 1] == '\r')) {
            caret_x = cx;
            caret_y = cy;
            caret_found = TRUE;
        }
    }

    if (!caret_found && nlines > 0) {
        int last_l = nlines - 1;
        int cx = x0;
        UH prev_tmp = 0;
        for (int p = lines[last_l].start_pos; p < lines[last_l].end_pos; p++) {
            int a = get_char_adv(f->text[p], prev_tmp);
            cx += a;
            if (a > 0 || (f->text[p] >> 8) != 0x9F) prev_tmp = f->text[p];
        }
        caret_x = cx;
        caret_y = y0 + lines[last_l].y_rel - scroll_y;
    }

    /* Live Text Insertion Caret & TIP Composition */
    if (is_selected && caret_x <= x1 && caret_y >= y0 && caret_y + GLYPH_H <= y1 + 4) {
        set_col(dev, COLOR_NAVY, COLOR_WHITE);
        drw_lin(dev, (H)caret_x, (H)caret_y, (H)caret_x, (H)(caret_y + GLYPH_H - 1));
        drw_lin(dev, (H)(caret_x + 1), (H)caret_y, (H)(caret_x + 1), (H)(caret_y + GLYPH_H - 1));

        /* TIP Inline Precomposition / Conversion */
        if (tip_get_state() != TIP_STATE_IDLE) {
            char comp_buf[128] = "";
            tip_get_converted_text(comp_buf, sizeof(comp_buf));
            if (comp_buf[0] != '\0') {
                BOOL is_dotted = (tip_get_state() == TIP_STATE_PRECOMP);
                drw_tc_string_underlined(dev, (H)(caret_x + 3), (H)caret_y, comp_buf, COLOR_NAVY, COLOR_WHITE, is_dotted);
                tip_set_caret_pos((H)(caret_x + 3), (H)caret_y);
            }
        }
    }

    /* Restore original clipping rect */
    set_clip(dev, &orig_clip);
}

static void render_text_vrtl_zoom(GDEV *dev, const ClarityFrame *f,
                                 int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f) return;
    int x0 = ox + (f->bounds.left   * zoom) / 100 + 4;
    int y0 = oy + (f->bounds.top    * zoom) / 100 + 4;
    int x1 = ox + (f->bounds.right  * zoom) / 100 - 4;
    int y1 = oy + (f->bounds.bottom * zoom) / 100 - 4;

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

        H gw = (tc < 128) ? 8 : GLYPH_W;
        H gh = GLYPH_H;
        const UB *bmp = get_glyph_bitmap((TC)tc, &gw, &gh);
        if (bmp) {
            blit_glyph_1bit(dev, bmp, col_x, cy, COLOR_BLACK, gw, gh);
            cy += gh + 2;
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

    /* Vertical insertion caret & TIP Composition */
    if (is_selected && caret_x >= x0 && caret_y + 2 <= y1 + 4) {
        set_col(dev, COLOR_NAVY, COLOR_WHITE);
        drw_lin(dev, (H)caret_x, (H)caret_y, (H)(caret_x + GLYPH_W - 1), (H)caret_y);
        drw_lin(dev, (H)caret_x, (H)(caret_y + 1), (H)(caret_x + GLYPH_W - 1), (H)(caret_y + 1));

        /* TIP Inline Precomposition / Conversion */
        if (tip_get_state() != TIP_STATE_IDLE) {
            char comp_buf[128] = "";
            tip_get_converted_text(comp_buf, sizeof(comp_buf));
            if (comp_buf[0] != '\0') {
                BOOL is_dotted = (tip_get_state() == TIP_STATE_PRECOMP);
                drw_tc_string_underlined(dev, (H)caret_x, (H)(caret_y + 4), comp_buf, COLOR_NAVY, COLOR_WHITE, is_dotted);
                tip_set_caret_pos((H)caret_x, (H)(caret_y + 4));
            }
        }
    }
}

void clarity_render_text(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom, BOOL is_selected)
{
    if (!dev || !f || f->type != FRAME_TEXT) return;
    if (zoom <= 0) zoom = 100;

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

void clarity_scale_blit_rgba(GDEV *dev, const RECT *dst_rect, const UB *src, H src_w, H src_h)
{
    if (!dev || !dev->pixels || !dst_rect || !src || src_w <= 0 || src_h <= 0) return;

    int dw = dst_rect->right - dst_rect->left;
    int dh = dst_rect->bottom - dst_rect->top;
    if (dw <= 0 || dh <= 0) return;

    /* Calculate fitted rectangle preserving aspect ratio (letterbox / center) */
    int fit_w = dw;
    int fit_h = (int)((long)fit_w * (long)src_h / (long)src_w);
    if (fit_h > dh) {
        fit_h = dh;
        fit_w = (int)((long)fit_h * (long)src_w / (long)src_h);
    }
    if (fit_w <= 0) fit_w = 1;
    if (fit_h <= 0) fit_h = 1;

    int ox = dst_rect->left + (dw - fit_w) / 2;
    int oy = dst_rect->top  + (dh - fit_h) / 2;

    for (int dy = 0; dy < fit_h; dy++) {
        int py = oy + dy;
        if (py < 0 || py >= dev->height) continue;
        if (py < dev->clip.top || py >= dev->clip.bottom) continue;

        int sy = (dy * (int)src_h) / fit_h;
        if (sy >= (int)src_h) sy = (int)src_h - 1;

        for (int dx = 0; dx < fit_w; dx++) {
            int px = ox + dx;
            if (px < 0 || px >= dev->width) continue;
            if (px < dev->clip.left || px >= dev->clip.right) continue;

            int sx = (dx * (int)src_w) / fit_w;
            if (sx >= (int)src_w) sx = (int)src_w - 1;

            int s_idx = (sy * (int)src_w + sx) * 4;
            UB r = src[s_idx + 0];
            UB g = src[s_idx + 1];
            UB b = src[s_idx + 2];
            UB a = src[s_idx + 3];

            if (a == 0) continue;
            if (a >= 250) {
                COLOR col = 0xFF000000 | ((UW)r << 16) | ((UW)g << 8) | (UW)b;
                dev->pixels[py * dev->width + px] = col;
            } else {
                /* Alpha blend with existing pixel */
                COLOR dcol = dev->pixels[py * dev->width + px];
                UB dr = (UB)((dcol >> 16) & 0xFF);
                UB dg = (UB)((dcol >> 8)  & 0xFF);
                UB db = (UB)(dcol & 0xFF);

                UB out_r = (UB)((r * a + dr * (255 - a)) / 255);
                UB out_g = (UB)((g * a + dg * (255 - a)) / 255);
                UB out_b = (UB)((b * a + db * (255 - a)) / 255);
                COLOR col = 0xFF000000 | ((UW)out_r << 16) | ((UW)out_g << 8) | (UW)out_b;
                dev->pixels[py * dev->width + px] = col;
            }
        }
    }
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

    /* On-demand image loading if path is set but bitmap not yet loaded */
    if (!f->bitmap && f->img_path[0] != '\0') {
        clarity_frame_load_image((ClarityFrame*)f, f->img_path, f->robj_id);
    }

    if (f->bitmap && f->bmp_w > 0 && f->bmp_h > 0 && fw > 0 && fh > 0) {
        clarity_scale_blit_rgba(dev, &bg, f->bitmap, f->bmp_w, f->bmp_h);
        return;
    }

    /* Fallback image placeholder */
    set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
    drw_rec(dev, &bg);
    drw_tc_string(dev, fx + 8, fy + 8, "【画像枠 / Image Frame】", COLOR_NAVY, COLOR_LTGRAY);
    if (f->img_path[0] != '\0') {
        drw_tc_string(dev, fx + 8, fy + 26, f->img_path, COLOR_DKGRAY, COLOR_LTGRAY);
    }
}

void clarity_render_tad(GDEV *dev, const ClarityFrame *f, int ox, int oy, int zoom)
{
    if (!dev || !f || f->type != FRAME_TAD) return;
    if (zoom <= 0) zoom = 100;

    int fx = ox + (f->bounds.left * zoom) / 100 + 1;
    int fy = oy + (f->bounds.top  * zoom) / 100 + 1;
    int fw = ((f->bounds.right - f->bounds.left) * zoom) / 100 - 2;
    int fh = ((f->bounds.bottom - f->bounds.top) * zoom) / 100 - 2;

    RECT bg = { (H)fx, (H)fy, (H)(fx + fw), (H)(fy + fh) };
    fill_rec(dev, &bg, COLOR_WHITE);

    RECT header = { (H)fx, (H)fy, (H)(fx + fw), (H)(fy + 22) };
    fill_rec(dev, &header, COLOR_NAVY);
    drw_tc_string(dev, fx + 6, fy + 4, "【TAD 仮想実身 / Virtual Body】", COLOR_WHITE, COLOR_NAVY);

    set_col(dev, COLOR_NAVY, COLOR_WHITE);
    drw_rec(dev, &bg);

    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "文書: %s", f->tad_title[0] ? f->tad_title : "TAD Document");
    drw_tc_string(dev, fx + 10, fy + 30, title_buf, COLOR_BLACK, COLOR_WHITE);

    char path_buf[256];
    snprintf(path_buf, sizeof(path_buf), "実身: %s", f->tad_path[0] ? f->tad_path : "-");
    drw_tc_string(dev, fx + 10, fy + 48, path_buf, COLOR_DKGRAY, COLOR_WHITE);

    if (fh >= 80) {
        RECT btn = { (H)(fx + 10), (H)(fy + 68), (H)(fx + fw - 10), (H)(fy + 88) };
        fill_rec(dev, &btn, COLOR_LTGRAY);
        set_col(dev, COLOR_DKGRAY, COLOR_LTGRAY);
        drw_rec(dev, &btn);
        drw_tc_string(dev, fx + 16, fy + 72, "▶ TAD Browser で開く (Double click to open)", COLOR_NAVY, COLOR_LTGRAY);
    }
}
