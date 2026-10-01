/*
 * B-TRON Retro OS — src/chokanji/xfconv.c
 * Authentic Cho-Kanji BTRON eXchange Format (XF) Converter.
 * Single C99 file adhering to NASA JPL Power of 10 Guidelines:
 *  - Fixed memory footprint (zero dynamic allocation).
 *  - Bounded loops on TAD segment parsing and conversion.
 *  - Converts between XF (TAD binary byte stream) ↔ plain UTF-8 ↔ RTF.
 *  - Full interactive PMC Cho-Kanji GUI dialog window.
 *  - Pure client-local coordinate space (0,0) to (w,h) on wnd->dev.
 */

#include <btron/types.h>
#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/pmc.h>
#include <btron/chokanji.h>
#include <btron/troncode.h>
#include <btron/tad.h>
#include <btron/error.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef E_NORES
#define E_NORES E_LIMIT
#endif

#define XF_MAX_FILE     (256 * 1024)   /* 256 KB maximum XF buffer */
#define XF_MAX_TEXT     (128 * 1024)   /* 128 KB maximum text output */
#define XF_MAX_SEGS     2048           /* Maximum segment count */
#define XF_MAGIC_LEN    4
#define XF_LOOP_BOUND   XF_MAX_SEGS

static const UB XF_MAGIC[XF_MAGIC_LEN] = { 0x54U, 0x41U, 0x44U, 0x20U }; /* "TAD " */
static const UB XF_RTF_HEADER[]  = "{\\rtf1\\ansi\\deff0 ";
static const UB XF_RTF_FOOTER[]  = "}";

typedef struct {
    UB  seg_id;
    D   data_offset;
    D   data_len;
} XFSeg;

static XFSeg s_segs[XF_MAX_SEGS];

/* ── Parse XF file into segment table ───────────────────────────────── */
static ER xfconv_parse(const UB *xf, int xf_len, int *n_segs_out) {
    if (!xf || !n_segs_out) return E_PAR;
    *n_segs_out = 0;
    if (xf_len < XF_MAGIC_LEN) return E_PAR;
    if (memcmp(xf, XF_MAGIC, XF_MAGIC_LEN) != 0) return E_PAR;

    int pos = XF_MAGIC_LEN;
    int n_segs = 0;

    for (int iter = 0; iter < XF_LOOP_BOUND && pos < xf_len; iter++) {
        if (pos + 5 > xf_len) break;
        if (xf[pos] != 0xFFU) { pos++; continue; }
        const UB seg_id = xf[pos + 1];
        const D seg_len = ((D)xf[pos + 2] << 16) |
                          ((D)xf[pos + 3] <<  8) |
                           (D)xf[pos + 4];
        pos += 5;
        if (pos + seg_len > xf_len) break;

        if (n_segs < XF_MAX_SEGS) {
            s_segs[n_segs].seg_id      = seg_id;
            s_segs[n_segs].data_offset = pos;
            s_segs[n_segs].data_len    = seg_len;
            n_segs++;
        }
        pos += (int)seg_len;
    }
    *n_segs_out = n_segs;
    return E_OK;
}

/* ── Extract concatenated plain UTF-8 text ──────────────────────────── */
ER xfconv_to_utf8(const UB *xf, int xf_len, char *out, int out_cap, int *out_len) {
    if (!xf || !out || !out_len) return E_PAR;

    int n_segs = 0;
    ER er = xfconv_parse(xf, xf_len, &n_segs);
    if (er != E_OK) return er;

    int di = 0;
    for (int si = 0; si < n_segs; si++) {
        if (s_segs[si].seg_id != TS_TEXT) continue;
        const UB *seg_data = xf + s_segs[si].data_offset;
        const D   seg_len  = s_segs[si].data_len;
        int pi = 0;
        for (int ci = 0; ci < 2048 && pi + 1 < (int)seg_len; ci++) {
            const TC code = ((TC)seg_data[pi] << 8) | seg_data[pi + 1];
            pi += 2;
            if (code == 0 || code == 0xFFFF) break;
            if (code == TC_NL || code == TC_CR) {
                if (di + 1 >= out_cap) break;
                out[di++] = '\n';
                continue;
            }
            if (code < 0x80U) {
                if (di + 1 >= out_cap) break;
                out[di++] = (char)code;
            } else {
                char u8_seq[8] = {0};
                int u8_len = tc_to_utf8(code, u8_seq, sizeof(u8_seq));
                if (u8_len > 0 && di + u8_len < out_cap) {
                    memcpy(out + di, u8_seq, (size_t)u8_len);
                    di += u8_len;
                }
            }
        }
    }
    out[di] = '\0';
    *out_len = di;
    return E_OK;
}

/* ── Wrap plain UTF-8 text into minimal XF / TAD byte stream ────────── */
ER xfconv_from_utf8(const char *text, int text_len, UB *xf_out, int xf_cap, int *xf_out_len) {
    if (!text || !xf_out || !xf_out_len) return E_PAR;
    if (text_len < 0) text_len = (int)strlen(text);

    int di = 0;
    if (di + XF_MAGIC_LEN >= xf_cap) return E_NORES;
    memcpy(xf_out + di, XF_MAGIC, XF_MAGIC_LEN);
    di += XF_MAGIC_LEN;

    TC tc_seq[2048];
    const int tc_count = utf8_to_tc_string(text, tc_seq, 2048);
    if (tc_count < 0) return E_PAR;

    const D seg_payload_len = (D)(tc_count * 2);
    if (di + 5 + (int)seg_payload_len >= xf_cap) return E_NORES;

    xf_out[di++] = 0xFFU;
    xf_out[di++] = (UB)TS_TEXT;
    xf_out[di++] = (UB)((seg_payload_len >> 16) & 0xFFU);
    xf_out[di++] = (UB)((seg_payload_len >>  8) & 0xFFU);
    xf_out[di++] = (UB)( seg_payload_len        & 0xFFU);

    for (int i = 0; i < tc_count; i++) {
        xf_out[di++] = (UB)((tc_seq[i] >> 8) & 0xFFU);
        xf_out[di++] = (UB)( tc_seq[i]       & 0xFFU);
    }

    *xf_out_len = di;
    return E_OK;
}

/* ── Convert XF → minimal RTF ───────────────────────────────────────── */
ER xfconv_to_rtf(const UB *xf, int xf_len, char *rtf_out, int rtf_cap, int *rtf_len) {
    if (!xf || !rtf_out || !rtf_len) return E_PAR;

    int di = 0;
    const int hlen = (int)strlen((const char*)XF_RTF_HEADER);
    if (di + hlen >= rtf_cap) return E_NORES;
    memcpy(rtf_out + di, XF_RTF_HEADER, (size_t)hlen); di += hlen;

    static char utf8_tmp[16384];
    int utf8_len = 0;
    ER er = xfconv_to_utf8(xf, xf_len, utf8_tmp, sizeof(utf8_tmp) - 1, &utf8_len);
    if (er != E_OK) return er;

    for (int i = 0; i < utf8_len && di + 12 < rtf_cap; i++) {
        const UB c = (UB)utf8_tmp[i];
        if (c < 0x80U) {
            if (c == (UB)'\\' || c == (UB)'{' || c == (UB)'}') {
                rtf_out[di++] = '\\';
            }
            rtf_out[di++] = (char)c;
        } else {
            rtf_out[di++] = '\\'; rtf_out[di++] = '\'';
            rtf_out[di++] = '3';  rtf_out[di++] = 'F';
        }
    }

    const int flen = (int)strlen((const char*)XF_RTF_FOOTER);
    if (di + flen < rtf_cap) {
        memcpy(rtf_out + di, XF_RTF_FOOTER, (size_t)flen);
        di += flen;
    }
    if (di < rtf_cap) rtf_out[di] = '\0';
    *rtf_len = di;
    return E_OK;
}

/* ── Interactive Cho-Kanji GUI Dialog Window ────────────────────────── */

typedef enum {
    XF_MODE_TO_UTF8 = 0,
    XF_MODE_FROM_UTF8 = 1,
    XF_MODE_TO_RTF = 2,
    XF_MODE_COUNT = 3
} XFMode;

static const char *const s_mode_labels[XF_MODE_COUNT] = {
    "XF → UTF-8", "UTF-8 → XF(TAD)", "XF → RTF"
};

typedef struct {
    WND *wnd;
    XFMode mode;
    char input_text[128];
    char output_text[256];
    char status[64];
    RECT mode_buttons[XF_MODE_COUNT];
    RECT convert_btn;
} XFConvState;

static XFConvState g_xfconv;

static void xfconv_run(void) {
    if (g_xfconv.mode == XF_MODE_FROM_UTF8) {
        UB xf_buf[512];
        int xf_len = 0;
        ER er = xfconv_from_utf8(g_xfconv.input_text, (int)strlen(g_xfconv.input_text),
                                 xf_buf, sizeof(xf_buf), &xf_len);
        if (er == E_OK) {
            snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text),
                     "[TAD Stream: %d bytes | Magic: TAD  | TS_TEXT segment created]", xf_len);
            snprintf(g_xfconv.status, sizeof(g_xfconv.status), "UTF-8 → XF(TAD) 変換成功 (%dバイト)", xf_len);
        } else {
            snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text), "(変換エラー: %d)", er);
            snprintf(g_xfconv.status, sizeof(g_xfconv.status), "変換失敗: %d", er);
        }
    } else {
        /* Generate synthetic TAD stream then convert to UTF-8 or RTF */
        UB xf_sample[256];
        int xf_sample_len = 0;
        xfconv_from_utf8(g_xfconv.input_text, (int)strlen(g_xfconv.input_text),
                         xf_sample, sizeof(xf_sample), &xf_sample_len);

        if (g_xfconv.mode == XF_MODE_TO_UTF8) {
            char txt_buf[256];
            int txt_len = 0;
            ER er = xfconv_to_utf8(xf_sample, xf_sample_len, txt_buf, sizeof(txt_buf) - 1, &txt_len);
            if (er == E_OK) {
                snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text), "%s", txt_buf);
                snprintf(g_xfconv.status, sizeof(g_xfconv.status), "XF → UTF-8 変換成功 (%dバイト)", txt_len);
            } else {
                snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text), "(変換エラー: %d)", er);
            }
        } else {
            char rtf_buf[256];
            int rtf_len = 0;
            ER er = xfconv_to_rtf(xf_sample, xf_sample_len, rtf_buf, sizeof(rtf_buf) - 1, &rtf_len);
            if (er == E_OK) {
                snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text), "%s", rtf_buf);
                snprintf(g_xfconv.status, sizeof(g_xfconv.status), "XF → RTF 変換成功 (%dバイト)", rtf_len);
            } else {
                snprintf(g_xfconv.output_text, sizeof(g_xfconv.output_text), "(変換エラー: %d)", er);
            }
        }
    }
}

void xfconv_paint(WND *wnd, GDEV *dev) {
    (void)wnd;
    if (!g_xfconv.wnd || !dev) return;

    const H w = dev->width;
    const H h = dev->height;

    /* 1. Backdrop */
    RECT bg = { 0, 0, w, h };
    fill_rec(dev, &bg, PMC_COL_BODY);

    /* 2. Top Header */
    RECT hdr = { 0, 0, w, 24 };
    fill_rec(dev, &hdr, PMC_COL_INACT_TITLE);
    drw_rec(dev, &hdr);
    drw_tc_string(dev, 10, 4, "BTRON XF / TAD 形式相互変換 (xfconv)", PMC_COL_OUTLINE, 0x00000000);

    /* 3. Conversion Mode Switches */
    drw_tc_string(dev, 12, 34, "変換形式:", PMC_COL_OUTLINE, 0x00000000);
    for (int i = 0; i < XF_MODE_COUNT; i++) {
        g_xfconv.mode_buttons[i] = (RECT){ 90 + i * 130, 30, 90 + (i + 1) * 130 - 10, 54 };
        bool active = (g_xfconv.mode == (XFMode)i);
        pmc_draw_switch(dev, &g_xfconv.mode_buttons[i], s_mode_labels[i], active, TRUE);
    }

    /* 4. Input Text Area */
    drw_tc_string(dev, 12, 68, "入力データ / ファイル:", PMC_COL_OUTLINE, 0x00000000);
    RECT in_box = { 12, 86, w - 12, 118 };
    fill_rec(dev, &in_box, 0x00FFFFFFU);
    drw_rec(dev, &in_box);
    drw_tc_string(dev, in_box.left + 6, in_box.top + 6, g_xfconv.input_text, COLOR_BLACK, 0x00000000);

    /* 5. Output Preview Area */
    drw_tc_string(dev, 12, 126, "変換出力プレビュー:", PMC_COL_OUTLINE, 0x00000000);
    RECT out_box = { 12, 144, w - 12, 180 };
    fill_rec(dev, &out_box, 0x00F4F4F4U);
    drw_rec(dev, &out_box);
    drw_tc_string(dev, out_box.left + 6, out_box.top + 6, g_xfconv.output_text, COLOR_NAVY, 0x00000000);

    /* 6. Action Button */
    g_xfconv.convert_btn = (RECT){ w / 2 - 80, 190, w / 2 + 80, 218 };
    pmc_draw_switch(dev, &g_xfconv.convert_btn, "【 形式変換実行 】", FALSE, TRUE);

    /* 7. Status Bar */
    RECT sbar = { 0, h - 22, w, h };
    fill_rec(dev, &sbar, PMC_COL_INACT_TITLE);
    drw_rec(dev, &sbar);
    drw_tc_string(dev, 10, sbar.top + 4, g_xfconv.status, 0x00202020U, 0x00000000);
}

static void xfconv_destroy(WND *wnd) {
    (void)wnd;
    g_xfconv.wnd = NULL;
}

static void xfconv_event_handler(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;
    if (evt->type == EV_BUT_DOWN) {
        H rx = evt->pos.x - wnd->client.left;
        H ry = evt->pos.y - wnd->client.top;

        /* Check Mode switches */
        for (int i = 0; i < XF_MODE_COUNT; i++) {
            RECT *r = &g_xfconv.mode_buttons[i];
            if (rx >= r->left && rx <= r->right && ry >= r->top && ry <= r->bottom) {
                g_xfconv.mode = (XFMode)i;
                inval_wnd(wnd);
                return;
            }
        }

        /* Check Convert button */
        RECT *cb = &g_xfconv.convert_btn;
        if (rx >= cb->left && rx <= cb->right && ry >= cb->top && ry <= cb->bottom) {
            xfconv_run();
            inval_wnd(wnd);
            return;
        }
    }
}

WND* open_chokanji_xfconv_window(void) {
    if (g_xfconv.wnd) {
        top_wnd(g_xfconv.wnd);
        return g_xfconv.wnd;
    }
    memset(&g_xfconv, 0, sizeof(g_xfconv));
    g_xfconv.mode = XF_MODE_FROM_UTF8;
    strncpy(g_xfconv.input_text, "■ 超漢字 XF形式 TAD交換文書", sizeof(g_xfconv.input_text) - 1);
    strncpy(g_xfconv.status, "準備完了", sizeof(g_xfconv.status) - 1);
    xfconv_run();

    g_xfconv.wnd = opn_wnd("XF形式変換 (xfconv)", 100, 110, 500, 252,
                           WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_BORDER);
    if (g_xfconv.wnd) {
        g_xfconv.wnd->paint = xfconv_paint;
        g_xfconv.wnd->event_handler = xfconv_event_handler;
        g_xfconv.wnd->destroy = xfconv_destroy;
        inval_wnd(g_xfconv.wnd);
    }
    return g_xfconv.wnd;
}
