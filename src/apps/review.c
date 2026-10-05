/*
 * B-System (BTRON 3.20) Review Application (src/apps/review.c)
 * Scientific PDF Reader, Peer-Review Workspace & TAD Citation Engine
 */

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/vobj.h>
#include <btron/event.h>
#include <btron/libc_shim.h>

typedef struct {
    WND *wnd;
    char doc_path[256];
    int page_count;
    int current_page;
    float zoom_factor;
    int selected_anno_id;
} ReviewApp;

extern int pdf_tad_anno_save_to_cabinet(UW pdf_robj_id, int page, const char *anno_text, UW *out_anno_robj);
extern int pdf_tad_anno_export_citation(UW anno_robj, char *out_citation_buf, size_t max_len);

void review_init(ReviewApp *app, const char *path) {
    if (!app) return;
    memset(app, 0, sizeof(ReviewApp));
    if (path) strncpy(app->doc_path, path, sizeof(app->doc_path) - 1);
    app->zoom_factor = 1.0f;
    app->page_count = 1;
    app->current_page = 1;
    app->selected_anno_id = -1;
}

void review_render_page(ReviewApp *app, int page_num) {
    (void)page_num;
    if (!app || !app->wnd) return;
    /* Render vector scientific paper, equation callouts, and peer-review annotation pins */
}

int review_add_annotation(ReviewApp *app, int page, const char *comment, UW *out_anno_robj) {
    if (!app || !comment) return -1;
    return pdf_tad_anno_save_to_cabinet(0x80010010, page, comment, out_anno_robj);
}
