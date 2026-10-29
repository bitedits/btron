/*
 * B-System BTRON3 — sps.c
 * Secure PostScript (sps) Compiler & PDF Transpiler
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include "sps.h"
#include "ps_vm.h"
#include <btron/libc_shim.h>
#include <btron/dp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Global state during single-threaded compilation */
static SPSDocument s_sps_doc;

/* Map PostScript font names to Standard 14 PDF font resource identifiers */
static const char *sps_map_font_res(const char *ps_font) {
    if (!ps_font || !ps_font[0]) return "F2"; /* Default Times-Roman */
    if (strstr(ps_font, "Palatino-Bold") || strstr(ps_font, "Times-Bold")) return "F1";
    if (strstr(ps_font, "Palatino-Italic") || strstr(ps_font, "Times-Italic")) return "F3";
    if (strstr(ps_font, "Palatino") || strstr(ps_font, "Times")) return "F2";
    if (strstr(ps_font, "Arial-Bold") || strstr(ps_font, "Helvetica-Bold")) return "F4";
    if (strstr(ps_font, "Arial") || strstr(ps_font, "Helvetica")) return "F5";
    return "F2";
}

/* Helper to append formatted string to current page stream */
static void sps_stream_append(const char *str) {
    if (s_sps_doc.cur_page_idx < 0 || s_sps_doc.cur_page_idx >= SPS_MAX_PAGES) return;
    SPSPage *p = &s_sps_doc.pages[s_sps_doc.cur_page_idx];
    size_t slen = strlen(str);
    if (p->stream_len + slen < SPS_PAGE_STREAM_MAX - 1) {
        memcpy(p->stream_buf + p->stream_len, str, slen);
        p->stream_len += (UW)slen;
        p->stream_buf[p->stream_len] = '\0';
    }
}

/* Escape text for PDF literal string (...) */
static void sps_escape_pdf_string(const char *in, UW in_len, char *out, size_t out_max) {
    size_t o = 0;
    for (UW i = 0; i < in_len && o < out_max - 2; i++) {
        char c = in[i];
        if (c == '(' || c == ')' || c == '\\') {
            if (o < out_max - 3) {
                out[o++] = '\\';
                out[o++] = c;
            }
        } else {
            out[o++] = c;
        }
    }
    out[o] = '\0';
}

/* ── Graphics Sink Callbacks ────────────────────────────────────── */

static void sps_sink_moveto(void *ud, float x, float y) {
    (void)ud;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %.2f m\n", x, y);
    sps_stream_append(buf);
}

static void sps_sink_lineto(void *ud, float x, float y) {
    (void)ud;
    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f %.2f l\n", x, y);
    sps_stream_append(buf);
}

static void sps_sink_curveto(void *ud, float x1, float y1, float x2, float y2, float x3, float y3) {
    (void)ud;
    char buf[96];
    snprintf(buf, sizeof(buf), "%.2f %.2f %.2f %.2f %.2f %.2f c\n", x1, y1, x2, y2, x3, y3);
    sps_stream_append(buf);
}

static void sps_sink_closepath(void *ud) {
    (void)ud;
    sps_stream_append("h\n");
}

static void sps_sink_stroke(void *ud, const PSGraphicsState *gs) {
    (void)ud;
    char buf[128];
    if (gs->line_width > 0 && gs->line_width != 1.0f) {
        snprintf(buf, sizeof(buf), "%.2f w\n", gs->line_width);
        sps_stream_append(buf);
    }
    if (gs->rgb[0] > 0 || gs->rgb[1] > 0 || gs->rgb[2] > 0) {
        snprintf(buf, sizeof(buf), "%.2f %.2f %.2f RG\n", gs->rgb[0], gs->rgb[1], gs->rgb[2]);
        sps_stream_append(buf);
    }
    sps_stream_append("S\n");
}

static void sps_sink_fill(void *ud, const PSGraphicsState *gs, BOOL eofill) {
    (void)ud;
    char buf[128];
    if (gs->rgb[0] > 0 || gs->rgb[1] > 0 || gs->rgb[2] > 0) {
        snprintf(buf, sizeof(buf), "%.2f %.2f %.2f rg\n", gs->rgb[0], gs->rgb[1], gs->rgb[2]);
        sps_stream_append(buf);
    }
    sps_stream_append(eofill ? "f*\n" : "f\n");
}

static void sps_sink_show(void *ud, const char *str, UW len, const PSGraphicsState *gs) {
    (void)ud;
    if (!str || len == 0) return;

    char esc_buf[1024];
    sps_escape_pdf_string(str, len, esc_buf, sizeof(esc_buf));

    const char *font_res = sps_map_font_res(gs->font_name);
    float fsize = gs->font_size > 0.0f ? gs->font_size : 10.0f;

    char buf[1280];
    snprintf(buf, sizeof(buf),
             "BT /%s %.2f Tf 1 0 0 1 %.2f %.2f Tm (%s) Tj ET\n",
             font_res, fsize, gs->cur_x, gs->cur_y, esc_buf);
    sps_stream_append(buf);
}

static void sps_sink_showpage(void *ud, int page_num) {
    (void)ud;
    if (s_sps_doc.cur_page_idx < SPS_MAX_PAGES - 1) {
        s_sps_doc.cur_page_idx++;
        s_sps_doc.pages[s_sps_doc.cur_page_idx].page_num = page_num + 1;
        s_sps_doc.pages[s_sps_doc.cur_page_idx].stream_len = 0;
        s_sps_doc.pages[s_sps_doc.cur_page_idx].stream_buf[0] = '\0';
        s_sps_doc.page_count = s_sps_doc.cur_page_idx + 1;
    }
}

/* ── PDF Serializer ─────────────────────────────────────────────── */

static BOOL sps_write_pdf_file(const char *pdf_path, const SPSDocument *doc, char *err_buf, size_t err_len) {
    FILE *out = fopen(pdf_path, "wb");
    if (!out) {
        if (err_buf) snprintf(err_buf, err_len, "cannot create output PDF: %s", pdf_path);
        return FALSE;
    }

    int active_pages = doc->cur_page_idx + 1;
    if (active_pages <= 0) active_pages = 1;

    /*
     * Object Layout:
     * 1: Catalog
     * 2: Pages Root
     * 3: Font F1 (Times-Bold)
     * 4: Font F2 (Times-Roman)
     * 5: Font F3 (Times-Italic)
     * 6: Font F4 (Helvetica-Bold)
     * 7: Font F5 (Helvetica)
     * 8 .. (8 + active_pages - 1) : Page Objects
     * (8 + active_pages) .. (8 + 2 * active_pages - 1) : Content Stream Objects
     */
    int font_base_obj = 3;
    int num_fonts = 5;
    int page_base_obj = font_base_obj + num_fonts;
    int stream_base_obj = page_base_obj + active_pages;
    int total_objs = stream_base_obj + active_pages - 1;

    long offsets[256];
    memset(offsets, 0, sizeof(offsets));

    /* 1. Header */
    long pos = 0;
    pos += fprintf(out, "%%PDF-1.4\n%%\xE2\xE3\xCF\xD3\n");

    /* 2. Object 1: Catalog */
    offsets[1] = pos;
    pos += fprintf(out, "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n");

    /* 3. Object 2: Pages Root */
    offsets[2] = pos;
    pos += fprintf(out, "2 0 obj\n<< /Type /Pages /Kids [");
    for (int p = 0; p < active_pages; p++) {
        pos += fprintf(out, "%d 0 R ", page_base_obj + p);
    }
    pos += fprintf(out, "] /Count %d >>\nendobj\n", active_pages);

    /* 4. Font Objects (3..7) */
    static const struct {
        const char *name;
        const char *base_font;
    } s_fonts[] = {
        { "F1", "Times-Bold" },
        { "F2", "Times-Roman" },
        { "F3", "Times-Italic" },
        { "F4", "Helvetica-Bold" },
        { "F5", "Helvetica" }
    };

    for (int f = 0; f < num_fonts; f++) {
        offsets[font_base_obj + f] = pos;
        pos += fprintf(out, "%d 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /%s >>\nendobj\n",
                       font_base_obj + f, s_fonts[f].base_font);
    }

    /* 5. Page Objects */
    for (int p = 0; p < active_pages; p++) {
        offsets[page_base_obj + p] = pos;
        int stream_obj = stream_base_obj + p;
        pos += fprintf(out,
                       "%d 0 obj\n"
                       "<< /Type /Page\n"
                       "   /Parent 2 0 R\n"
                       "   /MediaBox [0 0 612 792]\n"
                       "   /Contents %d 0 R\n"
                       "   /Resources <<\n"
                       "      /Font <<\n"
                       "         /F1 3 0 R\n"
                       "         /F2 4 0 R\n"
                       "         /F3 5 0 R\n"
                       "         /F4 6 0 R\n"
                       "         /F5 7 0 R\n"
                       "      >>\n"
                       "   >>\n"
                       ">>\n"
                       "endobj\n",
                       page_base_obj + p, stream_obj);
    }

    /* 6. Content Stream Objects */
    for (int p = 0; p < active_pages; p++) {
        offsets[stream_base_obj + p] = pos;
        const SPSPage *page = &doc->pages[p];
        pos += fprintf(out, "%d 0 obj\n<< /Length %u >>\nstream\n",
                       stream_base_obj + p, page->stream_len);
        if (page->stream_len > 0) {
            fwrite(page->stream_buf, 1, page->stream_len, out);
            pos += (long)page->stream_len;
        }
        pos += fprintf(out, "\nendstream\nendobj\n");
    }

    /* 7. Cross-Reference Table (xref) */
    long xref_pos = pos;
    fprintf(out, "xref\n0 %d\n", total_objs + 1);
    fprintf(out, "0000000000 65535 f \n");
    for (int obj = 1; obj <= total_objs; obj++) {
        fprintf(out, "%010ld 00000 n \n", offsets[obj]);
    }

    /* 8. Trailer */
    fprintf(out,
            "trailer\n"
            "<< /Size %d\n"
            "   /Root 1 0 R\n"
            ">>\n"
            "startxref\n"
            "%ld\n"
            "%%%%EOF\n",
            total_objs + 1, xref_pos);

    fclose(out);
    return TRUE;
}

/* ── Public API ─────────────────────────────────────────────────── */

BOOL sps_transpile_to_pdf(const char *ps_path, const char *pdf_path, char *err_buf, size_t err_len) {
    if (!ps_path || !pdf_path) {
        if (err_buf) snprintf(err_buf, err_len, "null input parameters");
        return FALSE;
    }

    /* Initialize Document State */
    memset(&s_sps_doc, 0, sizeof(s_sps_doc));
    s_sps_doc.cur_page_idx = 0;
    s_sps_doc.page_count = 1;
    s_sps_doc.pages[0].page_num = 1;

    /* Setup Graphics Sink */
    PSGraphicsSink sink;
    memset(&sink, 0, sizeof(sink));
    sink.user_data = NULL;
    sink.moveto    = sps_sink_moveto;
    sink.lineto    = sps_sink_lineto;
    sink.curveto   = sps_sink_curveto;
    sink.closepath = sps_sink_closepath;
    sink.stroke    = sps_sink_stroke;
    sink.fill      = sps_sink_fill;
    sink.show      = sps_sink_show;
    sink.showpage  = sps_sink_showpage;

    /* Run PostScript Virtual Machine */
    PSContext ctx;
    ps_ctx_init(&ctx, &sink);

    BOOL run_ok = ps_run_file(&ctx, ps_path);
    if (!run_ok) {
        if (err_buf) snprintf(err_buf, err_len, "PS execution error: %s", ctx.error_msg);
        return FALSE;
    }

    /* If last page has content and showpage was not fired, keep it */
    if (s_sps_doc.pages[s_sps_doc.cur_page_idx].stream_len == 0 && s_sps_doc.cur_page_idx > 0) {
        s_sps_doc.cur_page_idx--;
    }

    /* Write out final conformant PDF 1.4 binary */
    return sps_write_pdf_file(pdf_path, &s_sps_doc, err_buf, err_len);
}

/* ── CLU gterm Command Dispatcher ───────────────────────────────── */

void clu_sps(const char *args, ShellOutputFn out, void *ud) {
    if (!args || !args[0]) {
        out("Usage: sps <input.ps> [output.pdf]", COLOR_YELLOW, ud);
        return;
    }

    char in_file[256] = {0};
    char out_file[256] = {0};

    const char *p = args;
    while (*p && isspace((unsigned char)*p)) p++;

    int i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 255) in_file[i++] = *p++;
    in_file[i] = '\0';

    while (*p && isspace((unsigned char)*p)) p++;
    i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 255) out_file[i++] = *p++;
    out_file[i] = '\0';

    if (out_file[0] == '\0') {
        /* Replace .ps with .pdf or append .pdf */
        strncpy(out_file, in_file, sizeof(out_file) - 5);
        char *dot = strrchr(out_file, '.');
        if (dot && strcmp(dot, ".ps") == 0) {
            strcpy(dot, ".pdf");
        } else {
            strcat(out_file, ".pdf");
        }
    }

    out("Secure PostScript (sps) Engine — B-System DPS 3.20:", COLOR_CYAN, ud);
    char line[320];
    snprintf(line, sizeof(line), "  Input  : %s", in_file);
    out(line, COLOR_LTGRAY, ud);
    snprintf(line, sizeof(line), "  Output : %s", out_file);
    out(line, COLOR_LTGRAY, ud);
    out("  Policy : Zero dynamic execution (JS/XFA stripped and denied)", COLOR_YELLOW, ud);

    char err_buf[256] = {0};
    BOOL ok = sps_transpile_to_pdf(in_file, out_file, err_buf, sizeof(err_buf));

    if (ok) {
        snprintf(line, sizeof(line), "  Success: PDF 1.4 generated (%d pages processed)",
                 s_sps_doc.cur_page_idx + 1);
        out(line, COLOR_GREEN, ud);
    } else {
        snprintf(line, sizeof(line), "  Failure: %s", err_buf[0] ? err_buf : "unknown error");
        out(line, COLOR_RED, ud);
    }
}
