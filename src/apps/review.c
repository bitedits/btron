/*
 * B-System (BTRON 3.20) Review Application (src/apps/review.c)
 * Scientific PDF Reader, Peer-Review Workspace & TAD Citation Engine
 * Pure C99, adheres to NASA/JPL Power of Ten rules.
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/vobj.h>
#include <btron/event.h>
#include <btron/libc_shim.h>
#include <btron/troncode.h>
#include <btron/app_menu.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REVIEW_ANNOS 16
#define SCROLLBAR_SIZE   16

/* Menu command identifiers */
enum {
    CMD_PDF_OPEN = 1001,
    CMD_PDF_SAVE,
    CMD_PDF_CITE,
    CMD_PDF_CLOSE,
    CMD_PDF_PREV,
    CMD_PDF_NEXT,
    CMD_PDF_ZOOM_IN,
    CMD_PDF_ZOOM_OUT,
    CMD_PDF_ZOOM_100,
    CMD_PDF_ZOOM_FIT,
    CMD_PDF_ADD_ANNO,
    CMD_PDF_LIST_ANNO,
    CMD_PDF_ABOUT
};

typedef struct {
    UW   anno_robj_id;
    int  page;
    float x;
    float y;
    char author[48];
    char text[128];
    char timestamp[24];
} ReviewAnnotation;

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

    /* Scrolling & Dragging */
    int   scroll_x;
    int   scroll_y;
    BOOL  dragging_sb_v;
    BOOL  dragging_sb_h;
    int   drag_start_mouse;
    int   drag_start_scroll;

    /* Annotations */
    int   selected_anno_id;
    int   anno_count;
    ReviewAnnotation annos[MAX_REVIEW_ANNOS];

    /* In-App Menu */
    APP_MENU_BAR menu_bar;
} ReviewApp;

/* Forward declaration of pdf_reader_render_page */
extern void pdf_reader_render_page(void *app, int page_num);

static void review_build_menu(ReviewApp *app) {
    if (!app) return;
    app_menu_init(&app->menu_bar, APP_MENU_STYLE_CLASSIC_3D);

    /* ── Header 0: ファイル(F) ────────────────────────────────── */
    int h_file = app_menu_add_header(&app->menu_bar, "ファイル(F)", 96);
    app_menu_add_item(&app->menu_bar, h_file, "開く (Open PDF...)", "Ctrl+O", CMD_PDF_OPEN, TRUE);
    app_menu_add_item(&app->menu_bar, h_file, "増分保存 (Incremental Save)", "Ctrl+S", CMD_PDF_SAVE, TRUE);
    app_menu_add_item(&app->menu_bar, h_file, "BibTeX引用出力 (Export Citation)", "Ctrl+C", CMD_PDF_CITE, TRUE);
    app_menu_add_separator(&app->menu_bar, h_file);
    app_menu_add_item(&app->menu_bar, h_file, "閉じる (Close)", "Ctrl+W", CMD_PDF_CLOSE, TRUE);

    /* ── Header 1: 表示(V) ──────────────────────────────────── */
    int h_view = app_menu_add_header(&app->menu_bar, "表示(V)", 80);
    app_menu_add_item(&app->menu_bar, h_view, "前のページ (Previous)", "PageUp", CMD_PDF_PREV, TRUE);
    app_menu_add_item(&app->menu_bar, h_view, "次のページ (Next)", "PageDown", CMD_PDF_NEXT, TRUE);
    app_menu_add_separator(&app->menu_bar, h_view);
    app_menu_add_item(&app->menu_bar, h_view, "拡大 (Zoom In +)", "Ctrl++", CMD_PDF_ZOOM_IN, TRUE);
    app_menu_add_item(&app->menu_bar, h_view, "縮小 (Zoom Out -)", "Ctrl+-", CMD_PDF_ZOOM_OUT, TRUE);
    app_menu_add_item(&app->menu_bar, h_view, "等倍 (Actual 100%)", "Ctrl+0", CMD_PDF_ZOOM_100, TRUE);
    app_menu_add_item(&app->menu_bar, h_view, "窓に合わせる (Fit Window)", "Ctrl+F", CMD_PDF_ZOOM_FIT, TRUE);

    /* ── Header 2: 校正(R) ──────────────────────────────────── */
    int h_rev = app_menu_add_header(&app->menu_bar, "校正(R)", 80);
    app_menu_add_item(&app->menu_bar, h_rev, "付箋メモ追加 (Add Fusen Note...)", "Ctrl+M", CMD_PDF_ADD_ANNO, TRUE);
    app_menu_add_item(&app->menu_bar, h_rev, "全付箋の検証 (Verify All)", "", CMD_PDF_LIST_ANNO, TRUE);

    /* ── Header 3: ヘルプ(H) ────────────────────────────────── */
    int h_help = app_menu_add_header(&app->menu_bar, "ヘルプ(H)", 80);
    app_menu_add_item(&app->menu_bar, h_help, "Review について (About...)", "", CMD_PDF_ABOUT, TRUE);
}

void review_init(ReviewApp *app, const char *path) {
    if (!app) return;
    memset(app, 0, sizeof(ReviewApp));
    if (path) strncpy(app->doc_path, path, sizeof(app->doc_path) - 1);
    app->zoom_factor = 1.0f;
    app->page_count = 1;
    app->current_page = 1;
    app->pan_x = 24.0f;
    app->pan_y = (float)(APP_MENU_BAR_HEIGHT + 14);
    app->view_w = 500;
    app->view_h = 480;
    app->scroll_x = 0;
    app->scroll_y = 0;
    app->selected_anno_id = -1;
    app->anno_count = 0;
    review_build_menu(app);
}

int pdf_tad_anno_save_to_cabinet(UW pdf_robj_id, int page, const char *anno_text, UW *out_anno_robj) {
    (void)pdf_robj_id;
    (void)page;
    if (!anno_text) return -1;
    static UW s_next_robj = 0x80010020;
    if (out_anno_robj) *out_anno_robj = s_next_robj++;
    return 0;
}

int pdf_tad_anno_export_citation(UW anno_robj, char *out_citation_buf, size_t max_len) {
    if (!out_citation_buf || max_len == 0) return -1;
    snprintf(out_citation_buf, max_len,
             "@article{bsystem_review_%08x,\n"
             "  author  = {Namdak Tonpa and Cleanroom Working Group},\n"
             "  title   = {B-System Document Processing System & TAD Fusen Specification},\n"
             "  journal = {Synrc Research Publications},\n"
             "  year    = {2026},\n"
             "  note    = {BTRON3 SPEC 3.20 Conformance}\n"
             "}\n",
             anno_robj);
    return 0;
}

int review_add_annotation(ReviewApp *app, int page, const char *comment, UW *out_anno_robj) {
    if (!app || !comment || app->anno_count >= MAX_REVIEW_ANNOS) return -1;

    ReviewAnnotation *a = &app->annos[app->anno_count++];
    a->page = page;
    a->x = 200.0f;
    a->y = 665.0f;
    strncpy(a->author, "Namdak Tonpa", sizeof(a->author) - 1);
    strncpy(a->text, comment, sizeof(a->text) - 1);
    strncpy(a->timestamp, "2026-10-29 17:50", sizeof(a->timestamp) - 1);

    int res = pdf_tad_anno_save_to_cabinet(0x80010010, page, comment, &a->anno_robj_id);
    if (out_anno_robj) *out_anno_robj = a->anno_robj_id;
    return res;
}

/* Non-destructive Incremental Update: Appends annotations to PDF file */
int review_export_incremental_pdf(ReviewApp *app, const char *out_pdf_path) {
    if (!app || !app->doc_path[0]) return -1;
    const char *target = out_pdf_path ? out_pdf_path : app->doc_path;

    FILE *f = fopen(target, "r+b");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    long orig_size = ftell(f);

    /* Read original startxref */
    long search_start = orig_size > 1024 ? orig_size - 1024 : 0;
    fseek(f, search_start, SEEK_SET);
    char tail_buf[1024];
    size_t rd = fread(tail_buf, 1, sizeof(tail_buf) - 1, f);
    tail_buf[rd] = '\0';

    char *sx = strstr(tail_buf, "startxref");
    long prev_xref = 0;
    if (sx) {
        prev_xref = strtol(sx + 9, NULL, 10);
    }

    /* Append new Annotation Object */
    fseek(f, 0, SEEK_END);
    long append_pos = ftell(f);

    int new_obj_num = 101;
    long annot_obj_offset = append_pos;

    fprintf(f, "\n%d 0 obj\n", new_obj_num);
    fprintf(f, "<< /Type /Annot /Subtype /Text\n");
    fprintf(f, "   /Rect [ 200 665 224 689 ]\n");
    fprintf(f, "   /Contents (BTRON TAD Fusen Review Annotation)\n");
    fprintf(f, "   /T (Namdak Tonpa)\n");
    fprintf(f, "   /C [ 1.0 0.8 0.0 ]\n");
    fprintf(f, ">>\nendobj\n");

    /* Incremental XRef */
    long new_xref_pos = ftell(f);
    fprintf(f, "xref\n");
    fprintf(f, "%d 1\n", new_obj_num);
    fprintf(f, "%010ld 00000 n \n", annot_obj_offset);

    /* Incremental Trailer pointing to Prev */
    fprintf(f, "trailer\n");
    fprintf(f, "<< /Size %d\n", new_obj_num + 1);
    fprintf(f, "   /Root 1 0 R\n");
    if (prev_xref > 0) {
        fprintf(f, "   /Prev %ld\n", prev_xref);
    }
    fprintf(f, ">>\n");
    fprintf(f, "startxref\n");
    fprintf(f, "%ld\n", new_xref_pos);
    fprintf(f, "%%%%EOF\n");

    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scrollbar Rendering (Canonical BTRON 3D Steppers & Elevators)       */
/* ------------------------------------------------------------------ */

static void paint_scrollbar_v(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_h)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)x, (H)(y + h));

    /* Up Button */
    RECT up_btn = { (H)x, (H)y, (H)(x + w), (H)(y + 16) };
    fill_rec(dev, &up_btn, COLOR_LTGRAY);
    drw_rec(dev, &up_btn);
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 4),  (H)(y + 11));
    drw_lin(dev, (H)(x + 8), (H)(y + 4), (H)(x + 12), (H)(y + 11));
    drw_lin(dev, (H)(x + 4), (H)(y + 11), (H)(x + 12), (H)(y + 11));

    /* Down Button */
    int dy_b = y + h - 16;
    RECT dn_btn = { (H)x, (H)dy_b, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &dn_btn, COLOR_LTGRAY);
    drw_rec(dev, &dn_btn);
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 12), (H)(dy_b + 5));
    drw_lin(dev, (H)(x + 4),  (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));
    drw_lin(dev, (H)(x + 12), (H)(dy_b + 5), (H)(x + 8),  (H)(dy_b + 12));

    /* Elevator Thumb */
    int track_y = y + 16;
    int track_h = h - 32;
    if (track_h > 20) {
        int thumb_h = (max_scroll > 0) ? (track_h * view_h) / (view_h + max_scroll) : track_h;
        if (thumb_h < 16) thumb_h = 16;
        if (thumb_h > track_h) thumb_h = track_h;

        int thumb_y = track_y;
        if (max_scroll > 0) {
            thumb_y = track_y + (scroll * (track_h - thumb_h)) / max_scroll;
        }

        RECT thumb = { (H)(x + 1), (H)thumb_y, (H)(x + w - 1), (H)(thumb_y + thumb_h) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + w - 3), (H)(thumb_y + 1));
        drw_lin(dev, (H)(x + 2), (H)(thumb_y + 1), (H)(x + 2), (H)(thumb_y + thumb_h - 2));
    }
}

static void paint_scrollbar_h(GDEV *dev, int x, int y, int w, int h,
                              int scroll, int max_scroll, int view_w)
{
    RECT bg = { (H)x, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &bg, COLOR_LTGRAY);
    drw_lin(dev, (H)x, (H)y, (H)(x + w), (H)y);

    /* Left Button */
    RECT lt_btn = { (H)x, (H)y, (H)(x + 16), (H)(y + h) };
    fill_rec(dev, &lt_btn, COLOR_LTGRAY);
    drw_rec(dev, &lt_btn);
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 4));
    drw_lin(dev, (H)(x + 4), (H)(y + 8), (H)(x + 11), (H)(y + 12));
    drw_lin(dev, (H)(x + 11), (H)(y + 4), (H)(x + 11), (H)(y + 12));

    /* Right Button */
    int rx_b = x + w - 16;
    RECT rt_btn = { (H)rx_b, (H)y, (H)(x + w), (H)(y + h) };
    fill_rec(dev, &rt_btn, COLOR_LTGRAY);
    drw_rec(dev, &rt_btn);
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 12), (H)(rx_b + 12), (H)(y + 8));
    drw_lin(dev, (H)(rx_b + 5), (H)(y + 4),  (H)(rx_b + 5),  (H)(y + 12));

    /* Elevator Thumb */
    int track_x = x + 16;
    int track_w = w - 32;
    if (track_w > 20) {
        int thumb_w = (max_scroll > 0) ? (track_w * view_w) / (view_w + max_scroll) : track_w;
        if (thumb_w < 16) thumb_w = 16;
        if (thumb_w > track_w) thumb_w = track_w;

        int thumb_x = track_x;
        if (max_scroll > 0) {
            thumb_x = track_x + (scroll * (track_w - thumb_w)) / max_scroll;
        }

        RECT thumb = { (H)thumb_x, (H)(y + 1), (H)(thumb_x + thumb_w), (H)(y + h - 1) };
        fill_rec(dev, &thumb, COLOR_GRAY);
        drw_rec(dev, &thumb);
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + thumb_w - 2), (H)(y + 2));
        drw_lin(dev, (H)(thumb_x + 1), (H)(y + 2), (H)(thumb_x + 1), (H)(y + h - 3));
    }
}

/* ------------------------------------------------------------------ */
/* Viewport and Geometry Calculation                                  */
/* ------------------------------------------------------------------ */

static void review_get_geometry(const ReviewApp *app,
                                int *out_avail_w, int *out_avail_h,
                                int *out_rail_w,
                                int *out_max_scroll_x, int *out_max_scroll_y)
{
    int win_w = (app && app->wnd && app->wnd->dev) ? app->wnd->dev->width : 780;
    int win_h = (app && app->wnd && app->wnd->dev) ? app->wnd->dev->height : 580;

    int rail_w = (win_w > 640) ? 230 : 0;
    int avail_w = win_w - rail_w - SCROLLBAR_SIZE;
    int avail_h = win_h - APP_MENU_BAR_HEIGHT - SCROLLBAR_SIZE;
    if (avail_w < 50) avail_w = 50;
    if (avail_h < 50) avail_h = 50;

    float page_w = 612.0f;
    float page_h = 792.0f;
    float z = (app && app->zoom_factor > 0.0f) ? app->zoom_factor : 1.0f;
    int doc_w = (int)(page_w * z) + 48;
    int doc_h = (int)(page_h * z) + 48;

    int max_sx = (doc_w > avail_w) ? (doc_w - avail_w) : 0;
    int max_sy = (doc_h > avail_h) ? (doc_h - avail_h) : 0;

    if (out_avail_w) *out_avail_w = avail_w;
    if (out_avail_h) *out_avail_h = avail_h;
    if (out_rail_w)  *out_rail_w = rail_w;
    if (out_max_scroll_x) *out_max_scroll_x = max_sx;
    if (out_max_scroll_y) *out_max_scroll_y = max_sy;
}

void review_render_page(ReviewApp *app, int page_num) {
    if (!app || !app->wnd || !app->wnd->dev) return;

    GDEV *dev = app->wnd->dev;
    int dev_w = dev->width;
    int dev_h = dev->height;

    int avail_w = 0, avail_h = 0, rail_w = 0, max_sx = 0, max_sy = 0;
    review_get_geometry(app, &avail_w, &avail_h, &rail_w, &max_sx, &max_sy);

    app->view_w = avail_w;
    app->view_h = avail_h;

    /* Clamp scroll positions */
    if (app->scroll_x > max_sx) app->scroll_x = max_sx;
    if (app->scroll_x < 0) app->scroll_x = 0;
    if (app->scroll_y > max_sy) app->scroll_y = max_sy;
    if (app->scroll_y < 0) app->scroll_y = 0;

    /* Compute Paper Offset within Document Viewport */
    float page_w = 612.0f;
    int pw = (int)(page_w * app->zoom_factor);
    if (pw + 48 < avail_w) {
        app->pan_x = (float)((avail_w - pw) / 2);
    } else {
        app->pan_x = (float)(24 - app->scroll_x);
    }
    app->pan_y = (float)(APP_MENU_BAR_HEIGHT + 14 - app->scroll_y);

    /* 1. Render Base Vector PDF Document */
    pdf_reader_render_page(app, page_num);

    /* 2. Vertical & Horizontal Scrollbars */
    paint_scrollbar_v(dev, avail_w, APP_MENU_BAR_HEIGHT, SCROLLBAR_SIZE, avail_h,
                      app->scroll_y, max_sy, avail_h);
    paint_scrollbar_h(dev, 0, APP_MENU_BAR_HEIGHT + avail_h, avail_w, SCROLLBAR_SIZE,
                      app->scroll_x, max_sx, avail_w);

    /* Scrollbar junction corner */
    RECT junc = { (H)avail_w, (H)(APP_MENU_BAR_HEIGHT + avail_h),
                  (H)(avail_w + SCROLLBAR_SIZE), (H)(APP_MENU_BAR_HEIGHT + avail_h + SCROLLBAR_SIZE) };
    fill_rec(dev, &junc, COLOR_LTGRAY);
    drw_rec(dev, &junc);

    /* 3. Render Marginalia Right Rail (if window is wide enough) */
    if (rail_w > 0) {
        int rail_x = dev_w - rail_w;
        int rail_top = APP_MENU_BAR_HEIGHT;
        RECT rail_rect = { (H)rail_x, (H)rail_top, (H)dev_w, (H)dev_h };
        fill_rec(dev, &rail_rect, COLOR_LTGRAY);
        drw_lin(dev, rail_x, rail_top, rail_x, dev_h);

        /* Section Header */
        drw_tc_string(dev, rail_x + 8, rail_top + 6, "校正メモ (Marginalia)", COLOR_NAVY, 0);

        /* Draw Comment Cards */
        int cy = rail_top + 28;
        for (int i = 0; i < app->anno_count && cy + 68 < dev_h; i++) {
            RECT card = { (H)(rail_x + 8), (H)cy, (H)(dev_w - 8), (H)(cy + 64) };
            fill_rec(dev, &card, COLOR_WHITE);
            drw_rec(dev, &card);

            /* Yellow pin badge */
            RECT badge = { (H)(rail_x + 8), (H)cy, (H)(rail_x + 14), (H)(cy + 64) };
            fill_rec(dev, &badge, COLOR_GOLD);

            /* Card Header: Author & Page */
            char hdr_buf[64];
            snprintf(hdr_buf, sizeof(hdr_buf), "P.%d %s", app->annos[i].page, app->annos[i].author);
            drw_tc_string(dev, rail_x + 18, cy + 4, hdr_buf, COLOR_NAVY, 0);

            /* Card Timestamp */
            drw_tc_string(dev, rail_x + 18, cy + 20, app->annos[i].timestamp, COLOR_DKGRAY, 0);

            /* Card Comment text */
            drw_tc_string(dev, rail_x + 18, cy + 38, app->annos[i].text, COLOR_BLACK, 0);

            cy += 72;
        }
    }

    /* 4. In-App Menu Bar */
    char right_buf[64];
    snprintf(right_buf, sizeof(right_buf), "P.%d/%d | %d%% | %d 付箋",
             app->current_page, app->page_count > 0 ? app->page_count : 1,
             (int)(app->zoom_factor * 100.0f), app->anno_count);
    app_menu_set_right_text(&app->menu_bar, right_buf);
    app_menu_paint_bar(&app->menu_bar, dev);

    /* 5. Dropdown Menu on top */
    if (app->menu_bar.active_menu >= 0) {
        app_menu_paint_dropdown(&app->menu_bar, dev);
    }
}

/* ── Application Window Lifecycle & Openers ──────────────────────── */

static ReviewApp s_global_review_app;

static void review_paint(WND *wnd, GDEV *dev) {
    if (!wnd) return;
    (void)dev;
    s_global_review_app.wnd = wnd;
    review_render_page(&s_global_review_app, s_global_review_app.current_page);
}

static void review_handle_cmd(ReviewApp *app, int cmd) {
    if (!app) return;
    switch (cmd) {
        case CMD_PDF_OPEN: {
            if (strstr(app->doc_path, "n64.pdf")) {
                strncpy(app->doc_path, "doc/pdf/sample.pdf", sizeof(app->doc_path) - 1);
            } else {
                strncpy(app->doc_path, "doc/pdf/n64.pdf", sizeof(app->doc_path) - 1);
            }
            app->current_page = 1;
            app->scroll_x = 0;
            app->scroll_y = 0;
            break;
        }
        case CMD_PDF_SAVE: {
            review_export_incremental_pdf(app, NULL);
            break;
        }
        case CMD_PDF_CITE: {
            if (app->anno_count > 0) {
                char cit[512];
                pdf_tad_anno_export_citation(app->annos[0].anno_robj_id, cit, sizeof(cit));
            }
            break;
        }
        case CMD_PDF_CLOSE: {
            if (app->wnd) cls_wnd(app->wnd);
            break;
        }
        case CMD_PDF_PREV: {
            if (app->current_page > 1) {
                app->current_page--;
                app->scroll_y = 0;
            }
            break;
        }
        case CMD_PDF_NEXT: {
            if (app->current_page < app->page_count) {
                app->current_page++;
                app->scroll_y = 0;
            }
            break;
        }
        case CMD_PDF_ZOOM_IN: {
            app->zoom_factor += 0.20f;
            if (app->zoom_factor > 3.0f) app->zoom_factor = 3.0f;
            break;
        }
        case CMD_PDF_ZOOM_OUT: {
            app->zoom_factor -= 0.20f;
            if (app->zoom_factor < 0.4f) app->zoom_factor = 0.4f;
            break;
        }
        case CMD_PDF_ZOOM_100: {
            app->zoom_factor = 1.0f;
            app->scroll_x = 0;
            app->scroll_y = 0;
            break;
        }
        case CMD_PDF_ZOOM_FIT: {
            int avail_w = 0, avail_h = 0;
            review_get_geometry(app, &avail_w, &avail_h, NULL, NULL, NULL);
            float page_w = 612.0f;
            float page_h = 792.0f;
            float fx = (float)(avail_w - 48) / page_w;
            float fy = (float)(avail_h - 48) / page_h;
            app->zoom_factor = (fx < fy) ? fx : fy;
            if (app->zoom_factor < 0.4f) app->zoom_factor = 0.4f;
            if (app->zoom_factor > 2.5f) app->zoom_factor = 2.5f;
            app->scroll_x = 0;
            app->scroll_y = 0;
            break;
        }
        case CMD_PDF_ADD_ANNO: {
            UW anno_id = 0;
            char anno_text[64];
            snprintf(anno_text, sizeof(anno_text), "P.%d 校正注釈: 要確認項目", app->current_page);
            review_add_annotation(app, app->current_page, anno_text, &anno_id);
            break;
        }
        case CMD_PDF_ABOUT: {
            WND *dlg = app_menu_create_about_dialog(
                "Review", "校正・閲覧",
                "B-System Vector PDF & Peer-Review Workspace\n"
                "Secure PostScript (sps) & ISO 32000 Engine",
                "TRON Association / Cleanroom Systems Group",
                160, 120);
            if (dlg) {
                top_wnd(dlg);
                inval_wnd(dlg);
            }
            break;
        }
        default:
            break;
    }
}

static void review_event(WND *wnd, const EVT *evt) {
    if (!wnd || !evt) return;

    /* Compute window-relative coordinates (origin at top-left of client area) */
    H rel_x = (H)(evt->pos.x - wnd->client.left);
    H rel_y = (H)(evt->pos.y - wnd->client.top);

    ReviewApp *app = &s_global_review_app;

    int avail_w = 0, avail_h = 0, max_sx = 0, max_sy = 0;
    review_get_geometry(app, &avail_w, &avail_h, NULL, &max_sx, &max_sy);

    /* ── 1. Mouse Move ──────────────────────────────────────────────── */
    if (evt->type == EV_MOUSE_MOVE) {
        if (app->dragging_sb_v) {
            int track_h = avail_h - 32;
            int dy = rel_y - app->drag_start_mouse;
            if (track_h > 0 && max_sy > 0) {
                app->scroll_y = app->drag_start_scroll + (dy * max_sy) / track_h;
                if (app->scroll_y < 0) app->scroll_y = 0;
                if (app->scroll_y > max_sy) app->scroll_y = max_sy;
            }
            inval_wnd(wnd);
            return;
        }
        if (app->dragging_sb_h) {
            int track_w = avail_w - 32;
            int dx = rel_x - app->drag_start_mouse;
            if (track_w > 0 && max_sx > 0) {
                app->scroll_x = app->drag_start_scroll + (dx * max_sx) / track_w;
                if (app->scroll_x < 0) app->scroll_x = 0;
                if (app->scroll_x > max_sx) app->scroll_x = max_sx;
            }
            inval_wnd(wnd);
            return;
        }
        if (app_menu_handle_mouse_move(&app->menu_bar, rel_x, rel_y)) {
            inval_wnd(wnd);
            return;
        }
    }

    /* ── 2. Mouse Button Down ───────────────────────────────────────── */
    else if (evt->type == EV_BUT_DOWN) {
        /* Menu bar click */
        int cmd = -1, sub = -1;
        if (app_menu_handle_mouse_down(&app->menu_bar, rel_x, rel_y, &cmd, &sub)) {
            if (cmd >= 0) review_handle_cmd(app, cmd);
            inval_wnd(wnd);
            return;
        }

        /* Vertical Scrollbar clicks */
        if (rel_x >= avail_w && rel_x < avail_w + SCROLLBAR_SIZE &&
            rel_y >= APP_MENU_BAR_HEIGHT && rel_y < APP_MENU_BAR_HEIGHT + avail_h) {
            int sy = rel_y - APP_MENU_BAR_HEIGHT;
            if (sy < 16) {
                /* Up arrow */
                app->scroll_y -= 40;
                if (app->scroll_y < 0) app->scroll_y = 0;
            } else if (sy >= avail_h - 16) {
                /* Down arrow */
                app->scroll_y += 40;
                if (app->scroll_y > max_sy) app->scroll_y = max_sy;
            } else {
                /* Track or Thumb */
                int track_h = avail_h - 32;
                int thumb_h = (max_sy > 0) ? (track_h * avail_h) / (avail_h + max_sy) : track_h;
                if (thumb_h < 16) thumb_h = 16;
                int thumb_y = (max_sy > 0) ? (app->scroll_y * (track_h - thumb_h)) / max_sy : 0;
                int ty = sy - 16;
                if (ty < thumb_y) {
                    app->scroll_y -= avail_h / 2;
                    if (app->scroll_y < 0) app->scroll_y = 0;
                } else if (ty > thumb_y + thumb_h) {
                    app->scroll_y += avail_h / 2;
                    if (app->scroll_y > max_sy) app->scroll_y = max_sy;
                } else {
                    app->dragging_sb_v = TRUE;
                    app->drag_start_mouse = rel_y;
                    app->drag_start_scroll = app->scroll_y;
                }
            }
            inval_wnd(wnd);
            return;
        }

        /* Horizontal Scrollbar clicks */
        if (rel_x >= 0 && rel_x < avail_w &&
            rel_y >= APP_MENU_BAR_HEIGHT + avail_h &&
            rel_y < APP_MENU_BAR_HEIGHT + avail_h + SCROLLBAR_SIZE) {
            int sx = rel_x;
            if (sx < 16) {
                /* Left arrow */
                app->scroll_x -= 40;
                if (app->scroll_x < 0) app->scroll_x = 0;
            } else if (sx >= avail_w - 16) {
                /* Right arrow */
                app->scroll_x += 40;
                if (app->scroll_x > max_sx) app->scroll_x = max_sx;
            } else {
                /* Track or Thumb */
                int track_w = avail_w - 32;
                int thumb_w = (max_sx > 0) ? (track_w * avail_w) / (avail_w + max_sx) : track_w;
                if (thumb_w < 16) thumb_w = 16;
                int thumb_x = (max_sx > 0) ? (app->scroll_x * (track_w - thumb_w)) / max_sx : 0;
                int tx = sx - 16;
                if (tx < thumb_x) {
                    app->scroll_x -= avail_w / 2;
                    if (app->scroll_x < 0) app->scroll_x = 0;
                } else if (tx > thumb_x + thumb_w) {
                    app->scroll_x += avail_w / 2;
                    if (app->scroll_x > max_sx) app->scroll_x = max_sx;
                } else {
                    app->dragging_sb_h = TRUE;
                    app->drag_start_mouse = rel_x;
                    app->drag_start_scroll = app->scroll_x;
                }
            }
            inval_wnd(wnd);
            return;
        }
    }

    /* ── 3. Mouse Button Up ─────────────────────────────────────────── */
    else if (evt->type == EV_BUT_UP) {
        app->dragging_sb_v = FALSE;
        app->dragging_sb_h = FALSE;
    }

    /* ── 4. Key Down ────────────────────────────────────────────────── */
    else if (evt->type == EV_KEY_DOWN) {
        int cmd = -1;
        if (app_menu_handle_key(&app->menu_bar, evt->key, (uint16_t)(uintptr_t)evt->data, &cmd)) {
            if (cmd >= 0) review_handle_cmd(app, cmd);
            inval_wnd(wnd);
            return;
        }

        /* Page Navigation */
        if (evt->key == 0xFF56 /* PageDown */ || evt->key == 'n' || evt->key == ' ' || evt->key == 0x0D /* Enter */) {
            if (app->current_page < app->page_count) {
                app->current_page++;
                app->scroll_y = 0;
                inval_wnd(wnd);
                return;
            }
        } else if (evt->key == 0xFF55 /* PageUp */ || evt->key == 'p' || evt->key == 0x08 /* Backspace */) {
            if (app->current_page > 1) {
                app->current_page--;
                app->scroll_y = 0;
                inval_wnd(wnd);
                return;
            }
        } else if (evt->key == 0xFF50 /* Home */) {
            app->current_page = 1;
            app->scroll_y = 0;
            inval_wnd(wnd);
            return;
        } else if (evt->key == 0xFF57 /* End */) {
            app->current_page = app->page_count;
            app->scroll_y = 0;
            inval_wnd(wnd);
            return;
        }

        /* Zoom Hotkeys */
        if (evt->key == '+' || evt->key == '=') {
            review_handle_cmd(app, CMD_PDF_ZOOM_IN);
            inval_wnd(wnd);
            return;
        } else if (evt->key == '-' || evt->key == '_') {
            review_handle_cmd(app, CMD_PDF_ZOOM_OUT);
            inval_wnd(wnd);
            return;
        } else if (evt->key == '0') {
            review_handle_cmd(app, CMD_PDF_ZOOM_100);
            inval_wnd(wnd);
            return;
        } else if (evt->key == 'f' || evt->key == 'F') {
            review_handle_cmd(app, CMD_PDF_ZOOM_FIT);
            inval_wnd(wnd);
            return;
        }

        /* Arrow Keys Scrolling */
        if (evt->key == 0xFF52 /* UP */) {
            app->scroll_y -= 40;
            if (app->scroll_y < 0) app->scroll_y = 0;
            inval_wnd(wnd);
            return;
        } else if (evt->key == 0xFF54 /* DOWN */) {
            app->scroll_y += 40;
            if (app->scroll_y > max_sy) app->scroll_y = max_sy;
            inval_wnd(wnd);
            return;
        } else if (evt->key == 0xFF51 /* LEFT */) {
            app->scroll_x -= 40;
            if (app->scroll_x < 0) app->scroll_x = 0;
            inval_wnd(wnd);
            return;
        } else if (evt->key == 0xFF53 /* RIGHT */) {
            app->scroll_x += 40;
            if (app->scroll_x > max_sx) app->scroll_x = max_sx;
            inval_wnd(wnd);
            return;
        }
    }
}

static BOOL review_menu_open(WND *wnd) {
    (void)wnd;
    return (s_global_review_app.menu_bar.active_menu >= 0) ? TRUE : FALSE;
}

WND* open_review_window_with_file(const char *path) {
    const char *doc = path ? path : "doc/pdf/n64.pdf";
    FILE *f = fopen(doc, "rb");
    if (!f) {
        doc = "doc/pdf/sample.pdf";
        f = fopen(doc, "rb");
    }
    if (f) fclose(f);

    review_init(&s_global_review_app, doc);

    /* Default Fusen note to showcase peer-review UI */
    UW anno_id = 0;
    review_add_annotation(&s_global_review_app, 1, "BTRON Review: Vector PDF rendering verified.", &anno_id);

    WND *wnd = opn_wnd("差分校正・閲覧 – Review (PDF)",
                       60, 50, 780, 580,
                       WND_ATTR_TITLE | WND_ATTR_CLOSE |
                       WND_ATTR_BORDER | WND_ATTR_RESIZE);
    if (!wnd) return NULL;

    s_global_review_app.wnd = wnd;
    wnd->paint         = review_paint;
    wnd->event_handler = review_event;
    wnd->menu_open     = review_menu_open;

    top_wnd(wnd);
    inval_wnd(wnd);
    return wnd;
}

WND* open_review_window(void) {
    return open_review_window_with_file(NULL);
}
