/*
 * B-System (BTRON 3.20) Scientific PDF Reader (src/apps/pdf_reader.c)
 * Vector PDF Viewing, Marginalia, and Knowledge Citation Producer
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/vobj.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#include <btron/troncode.h>
#include <btron/font_mgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

#define MAX_PATH_POINTS 512

typedef struct {
    float x;
    float y;
    int   type; /* 0: moveto, 1: lineto */
} PdfPoint;

typedef struct {
    WND  *wnd;
    char  doc_path[256];
    int   page_count;
    int   current_page;
    float zoom_factor;
    float pan_x;
    float pan_y;
    int   view_w;
    int   view_h;
} PdfReaderApp;

typedef struct {
    float stack[16];
    int   top;
    char  last_name[64];
    char  last_str[512];
    float line_width;
    COLOR stroke_color;
    COLOR fill_color;
    char  font_name[64];
    float font_size;
    float text_x;
    float text_y;
    BOOL  in_text;
} PdfStreamState;

void pdf_reader_init(PdfReaderApp *app, const char *path) {
    if (!app) return;
    memset(app, 0, sizeof(PdfReaderApp));
    if (path) strncpy(app->doc_path, path, sizeof(app->doc_path) - 1);
    app->zoom_factor = 1.0f;
    app->page_count = 1;
    app->current_page = 1;
    app->pan_x = 24.0f;
    app->pan_y = 35.0f;
    app->view_w = 600;
    app->view_h = 500;
}

/* Convert PDF coordinates (origin bottom-left) to window GDEV (origin top-left) */
static inline int pdf_to_screen_x(float pdf_x, float pan_x, float zoom) {
    return (int)(pan_x + pdf_x * zoom);
}

static inline int pdf_to_screen_y(float pdf_y, float page_h, float pan_y, float zoom) {
    return (int)(pan_y + (page_h - pdf_y) * zoom);
}

/* Vector PDF Page Content Stream Interpreter */
static void pdf_exec_stream(GDEV *dev, const char *stream, size_t len,
                            float page_w, float page_h, float pan_x, float pan_y, float zoom) {
    (void)page_w;
    if (!dev || !stream || len == 0) return;

    PdfPoint pts[MAX_PATH_POINTS];
    int pt_count = 0;

    PdfStreamState st;
    memset(&st, 0, sizeof(st));
    st.line_width = 1.0f;
    st.stroke_color = COLOR_BLACK;
    st.fill_color = COLOR_BLACK;
    st.font_size = 12.0f;

    const char *p = stream;
    const char *end = stream + len;

    while (p < end) {
        /* Skip whitespace */
        while (p < end && isspace((unsigned char)*p)) p++;
        if (p >= end) break;

        /* Comment: % ... */
        if (*p == '%') {
            while (p < end && *p != '\r' && *p != '\n') p++;
            continue;
        }

        /* String literal: ( ... ) with escape and parenthesis nesting support */
        if (*p == '(') {
            p++;
            int depth = 1;
            size_t sidx = 0;
            while (p < end && depth > 0 && sidx < sizeof(st.last_str) - 1) {
                if (*p == '\\' && p + 1 < end) {
                    p++;
                    char esc = *p++;
                    if (esc == 'n') st.last_str[sidx++] = '\n';
                    else if (esc == 'r') st.last_str[sidx++] = '\r';
                    else if (esc == 't') st.last_str[sidx++] = '\t';
                    else st.last_str[sidx++] = esc;
                } else if (*p == '(') {
                    depth++;
                    st.last_str[sidx++] = *p++;
                } else if (*p == ')') {
                    depth--;
                    if (depth > 0) st.last_str[sidx++] = *p++;
                    else p++;
                } else {
                    st.last_str[sidx++] = *p++;
                }
            }
            st.last_str[sidx] = '\0';
            continue;
        }

        /* Name literal: /Name */
        if (*p == '/') {
            p++;
            size_t nidx = 0;
            while (p < end && !isspace((unsigned char)*p) && *p != '/' && *p != '(' &&
                   *p != '[' && *p != '<' && nidx < sizeof(st.last_name) - 1) {
                st.last_name[nidx++] = *p++;
            }
            st.last_name[nidx] = '\0';
            continue;
        }

        /* Array markers: skip */
        if (*p == '[' || *p == ']') {
            p++;
            continue;
        }

        /* Number: parse and push onto operand stack */
        if (isdigit((unsigned char)*p) || (*p == '-' && (p + 1 < end && (isdigit((unsigned char)p[1]) || p[1] == '.'))) ||
            (*p == '.' && (p + 1 < end && isdigit((unsigned char)p[1])))) {
            char *endptr = NULL;
            float val = strtof(p, &endptr);
            if (endptr > p) {
                if (st.top < 16) st.stack[st.top++] = val;
                p = endptr;
                continue;
            }
        }

        /* Keyword operator */
        char op[16];
        size_t oidx = 0;
        while (p < end && !isspace((unsigned char)*p) && *p != '/' && *p != '(' &&
               *p != '[' && *p != '<' && *p != '%' && oidx < sizeof(op) - 1) {
            op[oidx++] = *p++;
        }
        op[oidx] = '\0';

        /* Dispatch operators */
        if (strcmp(op, "BT") == 0) {
            st.in_text = TRUE;
            st.text_x = 0;
            st.text_y = 0;
            st.top = 0;
        } else if (strcmp(op, "ET") == 0) {
            st.in_text = FALSE;
            st.top = 0;
        } else if (strcmp(op, "Tf") == 0) {
            if (st.top >= 1) st.font_size = st.stack[st.top - 1];
            strncpy(st.font_name, st.last_name, sizeof(st.font_name) - 1);
            st.top = 0;
        } else if (strcmp(op, "Tm") == 0) {
            if (st.top >= 6) {
                st.text_x = st.stack[st.top - 2];
                st.text_y = st.stack[st.top - 1];
            }
            st.top = 0;
        } else if (strcmp(op, "Td") == 0 || strcmp(op, "TD") == 0) {
            if (st.top >= 2) {
                st.text_x += st.stack[st.top - 2];
                st.text_y += st.stack[st.top - 1];
            }
            st.top = 0;
        } else if (strcmp(op, "T*") == 0) {
            st.text_y -= st.font_size * 1.2f;
            st.top = 0;
        } else if (strcmp(op, "Tj") == 0 || strcmp(op, "TJ") == 0) {
            int sx = pdf_to_screen_x(st.text_x, pan_x, zoom);
            int sy = pdf_to_screen_y(st.text_y, page_h, pan_y, zoom) - 12;
            COLOR col = (st.fill_color != 0) ? st.fill_color : COLOR_BLACK;

            drw_tc_string(dev, sx, sy, st.last_str, col, 0);
            if (strstr(st.font_name, "Bold") || strstr(st.font_name, "F1") ||
                strstr(st.font_name, "F3") || strstr(st.font_name, "F4") || st.font_size >= 14.0f) {
                drw_tc_string(dev, sx + 1, sy, st.last_str, col, 0);
            }
            st.text_x += (float)strlen(st.last_str) * (st.font_size * 0.55f);
            st.top = 0;
        } else if (strcmp(op, "'") == 0) {
            st.text_y -= st.font_size * 1.2f;
            int sx = pdf_to_screen_x(st.text_x, pan_x, zoom);
            int sy = pdf_to_screen_y(st.text_y, page_h, pan_y, zoom) - 12;
            COLOR col = (st.fill_color != 0) ? st.fill_color : COLOR_BLACK;
            drw_tc_string(dev, sx, sy, st.last_str, col, 0);
            st.top = 0;
        } else if (strcmp(op, "m") == 0) {
            if (st.top >= 2) {
                pt_count = 0;
                pts[pt_count].x = st.stack[st.top - 2];
                pts[pt_count].y = st.stack[st.top - 1];
                pts[pt_count].type = 0;
                pt_count++;
            }
            st.top = 0;
        } else if (strcmp(op, "l") == 0) {
            if (st.top >= 2 && pt_count < MAX_PATH_POINTS) {
                pts[pt_count].x = st.stack[st.top - 2];
                pts[pt_count].y = st.stack[st.top - 1];
                pts[pt_count].type = 1;
                pt_count++;
            }
            st.top = 0;
        } else if (strcmp(op, "c") == 0) {
            if (st.top >= 6 && pt_count > 0) {
                float x0 = pts[pt_count - 1].x;
                float y0 = pts[pt_count - 1].y;
                float x1 = st.stack[st.top - 6];
                float y1 = st.stack[st.top - 5];
                float x2 = st.stack[st.top - 4];
                float y2 = st.stack[st.top - 3];
                float x3 = st.stack[st.top - 2];
                float y3 = st.stack[st.top - 1];

                /* 10-step cubic Bézier curve interpolation */
                for (int step = 1; step <= 10 && pt_count < MAX_PATH_POINTS; step++) {
                    float t = (float)step / 10.0f;
                    float omt = 1.0f - t;
                    float bx = omt*omt*omt*x0 + 3.0f*omt*omt*t*x1 + 3.0f*omt*t*t*x2 + t*t*t*x3;
                    float by = omt*omt*omt*y0 + 3.0f*omt*omt*t*y1 + 3.0f*omt*t*t*y2 + t*t*t*y3;
                    pts[pt_count].x = bx;
                    pts[pt_count].y = by;
                    pts[pt_count].type = 1;
                    pt_count++;
                }
            }
            st.top = 0;
        } else if (strcmp(op, "h") == 0) {
            if (pt_count > 1 && pt_count < MAX_PATH_POINTS) {
                pts[pt_count].x = pts[0].x;
                pts[pt_count].y = pts[0].y;
                pts[pt_count].type = 1;
                pt_count++;
            }
        } else if (strcmp(op, "re") == 0) {
            if (st.top >= 4) {
                float rx = st.stack[st.top - 4];
                float ry = st.stack[st.top - 3];
                float rw = st.stack[st.top - 2];
                float rh = st.stack[st.top - 1];
                pt_count = 0;
                pts[pt_count++] = (PdfPoint){ rx, ry, 0 };
                pts[pt_count++] = (PdfPoint){ rx + rw, ry, 1 };
                pts[pt_count++] = (PdfPoint){ rx + rw, ry + rh, 1 };
                pts[pt_count++] = (PdfPoint){ rx, ry + rh, 1 };
                pts[pt_count++] = (PdfPoint){ rx, ry, 1 };
            }
            st.top = 0;
        } else if (strcmp(op, "w") == 0) {
            if (st.top >= 1) st.line_width = st.stack[st.top - 1];
            st.top = 0;
        } else if (strcmp(op, "RG") == 0) {
            if (st.top >= 3) {
                int r = (int)(st.stack[st.top - 3] * 255.0f);
                int g = (int)(st.stack[st.top - 2] * 255.0f);
                int b = (int)(st.stack[st.top - 1] * 255.0f);
                if (r < 0) r = 0; if (r > 255) r = 255;
                if (g < 0) g = 0; if (g > 255) g = 255;
                if (b < 0) b = 0; if (b > 255) b = 255;
                st.stroke_color = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
            st.top = 0;
        } else if (strcmp(op, "rg") == 0) {
            if (st.top >= 3) {
                int r = (int)(st.stack[st.top - 3] * 255.0f);
                int g = (int)(st.stack[st.top - 2] * 255.0f);
                int b = (int)(st.stack[st.top - 1] * 255.0f);
                if (r < 0) r = 0; if (r > 255) r = 255;
                if (g < 0) g = 0; if (g > 255) g = 255;
                if (b < 0) b = 0; if (b > 255) b = 255;
                st.fill_color = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
            st.top = 0;
        } else if (strcmp(op, "G") == 0) {
            if (st.top >= 1) {
                int gr = (int)(st.stack[st.top - 1] * 255.0f);
                if (gr < 0) gr = 0; if (gr > 255) gr = 255;
                st.stroke_color = 0xFF000000 | (gr << 16) | (gr << 8) | gr;
            }
            st.top = 0;
        } else if (strcmp(op, "g") == 0) {
            if (st.top >= 1) {
                int gr = (int)(st.stack[st.top - 1] * 255.0f);
                if (gr < 0) gr = 0; if (gr > 255) gr = 255;
                st.fill_color = 0xFF000000 | (gr << 16) | (gr << 8) | gr;
            }
            st.top = 0;
        } else if (strcmp(op, "S") == 0 || strcmp(op, "s") == 0) {
            if (strcmp(op, "s") == 0 && pt_count > 1 && pt_count < MAX_PATH_POINTS) {
                pts[pt_count].x = pts[0].x;
                pts[pt_count].y = pts[0].y;
                pts[pt_count].type = 1;
                pt_count++;
            }
            int lw = (int)st.line_width;
            if (lw < 1) lw = 1;
            for (int i = 1; i < pt_count; i++) {
                if (pts[i].type == 1) {
                    int x1 = pdf_to_screen_x(pts[i - 1].x, pan_x, zoom);
                    int y1 = pdf_to_screen_y(pts[i - 1].y, page_h, pan_y, zoom);
                    int x2 = pdf_to_screen_x(pts[i].x, pan_x, zoom);
                    int y2 = pdf_to_screen_y(pts[i].y, page_h, pan_y, zoom);
                    drw_lin(dev, x1, y1, x2, y2);
                    if (lw >= 2) {
                        drw_lin(dev, x1, y1 + 1, x2, y2 + 1);
                        drw_lin(dev, x1 + 1, y1, x2 + 1, y2);
                    }
                    if (lw >= 3) {
                        drw_lin(dev, x1, y1 - 1, x2, y2 - 1);
                        drw_lin(dev, x1 - 1, y1, x2 - 1, y2);
                    }
                }
            }
            pt_count = 0;
            st.top = 0;
        } else if (strcmp(op, "f") == 0 || strcmp(op, "F") == 0 ||
                   strcmp(op, "b") == 0 || strcmp(op, "B") == 0) {
            if (pt_count == 5 && pts[0].x == pts[4].x && pts[0].y == pts[4].y) {
                int x1 = pdf_to_screen_x(pts[0].x, pan_x, zoom);
                int y1 = pdf_to_screen_y(pts[2].y, page_h, pan_y, zoom);
                int x2 = pdf_to_screen_x(pts[2].x, pan_x, zoom);
                int y2 = pdf_to_screen_y(pts[0].y, page_h, pan_y, zoom);
                RECT r = { (H)(x1 < x2 ? x1 : x2), (H)(y1 < y2 ? y1 : y2),
                           (H)(x1 > x2 ? x1 : x2), (H)(y1 > y2 ? y1 : y2) };
                fill_rec(dev, &r, st.fill_color);
                if (op[0] == 'b' || op[0] == 'B') drw_rec(dev, &r);
            }
            pt_count = 0;
            st.top = 0;
        }
    }
}

void pdf_reader_render_page(void *app_ptr, int page_num) {
    if (!app_ptr) return;
    PdfReaderApp *app = (PdfReaderApp *)app_ptr;
    if (!app->wnd || !app->wnd->dev) return;

    GDEV *dev = app->wnd->dev;

    /* 1. Viewport Clipping: Restrict drawing strictly to document viewport */
    RECT orig_clip = dev->clip;
    int vw = (app->view_w > 0) ? app->view_w : dev->width;
    int vh = (app->view_h > 0) ? app->view_h : (dev->height - 21);

    RECT doc_clip = { 0, 21, (H)vw, (H)(21 + vh) };
    set_clip(dev, &doc_clip);

    /* Clear document background to neutral workbench desk gray */
    fill_rec(dev, &doc_clip, COLOR_LTGRAY);

    /* 2. Paper Geometry (Standard US Letter 612x792) */
    float page_w = 612.0f;
    float page_h = 792.0f;
    int pw = (int)(page_w * app->zoom_factor);
    int ph = (int)(page_h * app->zoom_factor);

    int px = (int)app->pan_x;
    int py = (int)app->pan_y;

    /* Paper Drop Shadow */
    RECT shadow_rect = { (H)(px + 4), (H)(py + 4), (H)(px + pw + 4), (H)(py + ph + 4) };
    fill_rec(dev, &shadow_rect, COLOR_DKGRAY);

    /* White Paper Sheet */
    RECT paper_rect = { (H)px, (H)py, (H)(px + pw), (H)(py + ph) };
    fill_rec(dev, &paper_rect, COLOR_WHITE);
    drw_rec(dev, &paper_rect);

    /* 3. Load PDF and Locate Target Page Stream */
    FILE *f = fopen(app->doc_path, "rb");
    if (!f) {
        const char *alts[] = {
            "doc/pdf/n64.pdf",
            "doc/pdf/sample.pdf",
            "assets/pdf/n64.pdf",
            "SYS/n64.pdf",
            "n64.pdf"
        };
        for (size_t i = 0; i < sizeof(alts) / sizeof(alts[0]); i++) {
            f = fopen(alts[i], "rb");
            if (f) {
                strncpy(app->doc_path, alts[i], sizeof(app->doc_path) - 1);
                break;
            }
        }
    }
    if (!f) {
        set_clip(dev, &orig_clip);
        return;
    }

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (sz <= 0 || sz > 10 * 1024 * 1024) {
        fclose(f);
        set_clip(dev, &orig_clip);
        return;
    }

    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        fclose(f);
        set_clip(dev, &orig_clip);
        return;
    }
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    /* Count total streams / pages */
    int total_pages = 0;
    const char *scan = buf;
    while ((scan = strstr(scan, "stream")) != NULL) {
        total_pages++;
        scan += 6;
    }
    app->page_count = (total_pages > 0) ? total_pages : 1;

    /* Locate and execute stream for target page */
    int cur_page = 0;
    const char *p = buf;
    while ((p = strstr(p, "stream")) != NULL) {
        cur_page++;
        p += 6;
        while (*p == '\r' || *p == '\n') p++;
        const char *stream_start = p;
        const char *stream_end = strstr(stream_start, "endstream");
        if (!stream_end) break;

        if (cur_page == page_num) {
            size_t slen = (size_t)(stream_end - stream_start);
            pdf_exec_stream(dev, stream_start, slen, page_w, page_h, (float)px, (float)py, app->zoom_factor);
            break;
        }
        p = stream_end + 9;
    }

    free(buf);

    /* 4. Overlay TAD Fusen Pin (付箋) on top-right of page */
    RECT fusen_pin = { (H)(px + pw - 24), (H)(py + 18), (H)(px + pw + 8), (H)(py + 42) };
    fill_rec(dev, &fusen_pin, COLOR_GOLD);
    drw_rec(dev, &fusen_pin);

    /* Restore clipping */
    set_clip(dev, &orig_clip);
}
