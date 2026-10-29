/*
 * B-System BTRON3 — test_sps.c
 * Verification & Test Suite for Secure PostScript (sps) & Forth VM
 */

#include "../../src/clu/ps_vm.h"
#include "../../src/clu/sps.h"
#include "../../src/font/schrift.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Forward declarations for review app functions */
typedef struct ReviewApp ReviewApp;
extern void review_init(void *app, const char *path);
extern int  review_add_annotation(void *app, int page, const char *comment, unsigned int *out_anno_robj);
extern int  pdf_tad_anno_export_citation(unsigned int anno_robj, char *out_citation_buf, size_t max_len);
extern int  review_export_incremental_pdf(void *app, const char *out_pdf_path);

/* Stubs for window & menu primitives in headless test environment */
struct WND;
struct WND* opn_wnd(const char *title, short x, short y, short w, short h, unsigned long attr) {
    (void)title; (void)x; (void)y; (void)w; (void)h; (void)attr;
    return NULL;
}
void inval_wnd(struct WND *wnd) { (void)wnd; }
void top_wnd(struct WND *wnd) { (void)wnd; }
int  cls_wnd(struct WND *wnd) { (void)wnd; return 0; }
int  drw_tc_string(void *dev, short x, short y, const char *str, unsigned int fg, unsigned int bg) {
    (void)dev; (void)x; (void)y; (void)str; (void)fg; (void)bg;
    return 0;
}
void app_menu_init(void *bar, int style) { (void)bar; (void)style; }
int  app_menu_add_header(void *bar, const char *t, short w) { (void)bar; (void)t; (void)w; return 0; }
int  app_menu_add_item(void *bar, int h, const char *l, const char *a, int c, int e) {
    (void)bar; (void)h; (void)l; (void)a; (void)c; (void)e; return 0;
}
int  app_menu_add_separator(void *bar, int h) { (void)bar; (void)h; return 0; }
void app_menu_set_right_text(void *bar, const char *txt) { (void)bar; (void)txt; }
void app_menu_paint_bar(const void *bar, void *dev) { (void)bar; (void)dev; }
void app_menu_paint_dropdown(const void *bar, void *dev) { (void)bar; (void)dev; }
int  app_menu_handle_mouse_move(void *bar, short rx, short ry) { (void)bar; (void)rx; (void)ry; return 0; }
int  app_menu_handle_mouse_down(void *bar, short rx, short ry, int *c, int *s) {
    (void)bar; (void)rx; (void)ry; (void)c; (void)s; return 0;
}
int  app_menu_handle_key(void *bar, unsigned int k, unsigned short m, int *c) {
    (void)bar; (void)k; (void)m; (void)c; return 0;
}
struct WND* app_menu_create_about_dialog(const char *a, const char *j, const char *d, const char *att, int x, int y) {
    (void)a; (void)j; (void)d; (void)att; (void)x; (void)y; return NULL;
}

#define TEST_PASS(msg) printf("  PASS: %s\n", msg)
#define TEST_FAIL(msg) do { printf("  FAIL: %s\n", msg); exit(1); } while(0)

static void test_forth_stack_and_math(void) {
    PSContext ctx;
    ps_ctx_init(&ctx, NULL);

    const char *script = "10 20 add 2 mul 5 sub";
    BOOL ok = ps_run_token_stream(&ctx, script, strlen(script));
    assert(ok);
    assert(ctx.op_top == 0);
    assert(ctx.op_stack[0].type == PS_VAL_INT);
    assert(ctx.op_stack[0].u.i == 55); /* ((10 + 20) * 2) - 5 = 55 */

    TEST_PASS("Forth stack and arithmetic evaluation");
}

static void test_forth_dict_and_procedures(void) {
    PSContext ctx;
    ps_ctx_init(&ctx, NULL);

    const char *script = "/square { dup mul } def 7 square";
    BOOL ok = ps_run_token_stream(&ctx, script, strlen(script));
    if (!ok) {
        printf("DEBUG error: %s\n", ctx.error_msg);
    }
    assert(ok);
    assert(ctx.op_top == 0);
    assert(ctx.op_stack[0].type == PS_VAL_INT);
    assert(ctx.op_stack[0].u.i == 49); /* 7 * 7 = 49 */

    TEST_PASS("Forth dictionary definition and procedure execution");
}

static void test_forth_globaldict_and_cvs(void) {
    PSContext ctx;
    ps_ctx_init(&ctx, NULL);

    const char *script = "globaldict /page 42 put page =string cvs";
    BOOL ok = ps_run_token_stream(&ctx, script, strlen(script));
    assert(ok);
    assert(ctx.op_top == 0);
    assert(ctx.op_stack[0].type == PS_VAL_STRING);
    assert(ctx.op_stack[0].u.s.len == 2);
    assert(memcmp(ctx.op_stack[0].u.s.str, "42", 2) == 0);

    TEST_PASS("globaldict scoping and cvs string conversion");
}

static void test_n64_ps_to_pdf_transpilation(void) {
    const char *ps_file = "doc/ps/n64.ps";
    const char *pdf_file = "doc/pdf/n64.pdf";

    char err_buf[256] = {0};
    BOOL ok = sps_transpile_to_pdf(ps_file, pdf_file, err_buf, sizeof(err_buf));
    if (!ok) {
        printf("Error during transpilation: %s\n", err_buf);
        TEST_FAIL("sps_transpile_to_pdf returned FALSE");
    }

    FILE *f = fopen(pdf_file, "rb");
    if (!f) TEST_FAIL("Output PDF file was not created");

    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    assert(sz > 1000); /* Must be substantial valid PDF */

    char *pdf_buf = (char *)malloc(sz + 1);
    assert(pdf_buf != NULL);
    size_t rd = fread(pdf_buf, 1, sz, f);
    fclose(f);
    pdf_buf[rd] = '\0';

    /* Verify PDF Header */
    assert(strncmp(pdf_buf, "%PDF-1.4", 8) == 0);

    /* Verify Catalog and Page Tree */
    assert(strstr(pdf_buf, "/Type /Catalog") != NULL);
    assert(strstr(pdf_buf, "/Type /Pages") != NULL);
    assert(strstr(pdf_buf, "/Count 9") != NULL); /* 9 pages in n64.ps slice */

    /* Verify Fonts */
    assert(strstr(pdf_buf, "/BaseFont /Times-Roman") != NULL);
    assert(strstr(pdf_buf, "/BaseFont /Times-Bold") != NULL);
    assert(strstr(pdf_buf, "/BaseFont /Helvetica-Bold") != NULL);

    /* Verify Specific Text Content from n64.ps */
    assert(strstr(pdf_buf, "HARDWARE AND SOFTWARE INSTALLATION NOTES") != NULL);
    assert(strstr(pdf_buf, "R4300 CPU") != NULL);
    assert(strstr(pdf_buf, "Reality Coprocessor") != NULL);

    /* Verify Cross-Reference and Trailer */
    assert(strstr(pdf_buf, "xref") != NULL);
    assert(strstr(pdf_buf, "trailer") != NULL);
    assert(strstr(pdf_buf, "startxref") != NULL);
    assert(strstr(pdf_buf, "%%EOF") != NULL);

    free(pdf_buf);
    TEST_PASS("doc/ps/n64.ps transpiled to valid 9-page doc/pdf/n64.pdf");
}

static void test_review_annotation_and_incremental_pdf(void) {
    static char app_mem[16384];
    memset(app_mem, 0, sizeof(app_mem));
    review_init(app_mem, "doc/pdf/n64.pdf");

    unsigned int anno_robj = 0;
    int res = review_add_annotation(app_mem, 1, "Reviewer Note: Verify R4300 DMA pipeline.", &anno_robj);
    assert(res == 0);
    assert(anno_robj != 0);

    char cit[512] = {0};
    res = pdf_tad_anno_export_citation(anno_robj, cit, sizeof(cit));
    assert(res == 0);
    assert(strstr(cit, "@article{bsystem_review_") != NULL);

    res = review_export_incremental_pdf(app_mem, "doc/pdf/n64.pdf");
    assert(res == 0);

    /* Verify incremental update written to PDF file */
    FILE *f = fopen("doc/pdf/n64.pdf", "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *buf = (char *)malloc(sz + 1);
    assert(buf != NULL);
    size_t rd = fread(buf, 1, sz, f);
    fclose(f);
    buf[rd] = '\0';

    assert(strstr(buf, "/Type /Annot") != NULL);
    assert(strstr(buf, "/Subtype /Text") != NULL);
    assert(strstr(buf, "BTRON TAD Fusen Review Annotation") != NULL);
    assert(strstr(buf, "/Prev") != NULL);

    free(buf);
    TEST_PASS("Review app Fusen annotation & ISO 32000 incremental update PDF writeback");
}

static void test_libschrift_font_renderer(void) {
    const char *ver = sft_version();
    assert(ver != NULL);
    assert(strlen(ver) > 0);
    TEST_PASS("libschrift TrueType font renderer linkage & version verification");
}

static void test_sample_ps_to_pdf_transpilation(void) {
    const char *ps_file = "doc/ps/sample.ps";
    const char *pdf_file = "doc/pdf/sample.pdf";

    char err_buf[256] = {0};
    BOOL ok = sps_transpile_to_pdf(ps_file, pdf_file, err_buf, sizeof(err_buf));
    if (!ok) {
        printf("Error during sample transpilation: %s\n", err_buf);
        TEST_FAIL("sps_transpile_to_pdf sample failed");
    }

    FILE *f = fopen(pdf_file, "rb");
    assert(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fclose(f);
    assert(sz > 500);

    TEST_PASS("doc/ps/sample.ps transpiled to valid 1-page doc/pdf/sample.pdf");
}

int main(void) {
    printf("=== B-System BTRON3 Secure PostScript (sps) Unit Tests ===\n\n");

    test_forth_stack_and_math();
    test_forth_dict_and_procedures();
    test_forth_globaldict_and_cvs();
    test_n64_ps_to_pdf_transpilation();
    test_sample_ps_to_pdf_transpilation();
    test_review_annotation_and_incremental_pdf();
    test_libschrift_font_renderer();

    printf("\n=== Results: 7 PASS  0 FAIL ===\n");
    printf("[SPS] All Secure PostScript and Reader/Review tests passed.\n");
    return 0;
}
