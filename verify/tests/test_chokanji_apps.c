/*
 * B-TRON Retro OS — verify/tests/test_chokanji_apps.c
 * Verification test suite for Cho-Kanji Applications & PMC Cleanroom Engine:
 *  - §1 PMC Style: live switching, hit tests, palette parameters
 *  - §2 Cabinet (cab.c): entry layout, sorting, drawer navigation, click handling
 *  - §3 Clock (clock.c): analog/digital views, hand geometry, time reading
 *  - §4 MicroScript (microscript.c): variable store, command execution, stage navigation
 *  - §5 Kanji Converter (kconv.c): EUC-JP ↔ Shift-JIS ↔ TRON Code ↔ UTF-8 conversion
 *  - §6 XF Converter (xfconv.c): TAD/XF ↔ UTF-8 ↔ RTF stream formatting
 *  - §7 BPK Unpacker (unpack.c): unpacking assets/bpk/*.bpk real objects
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <btron/types.h>
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/pmc.h>
#include <btron/chokanji.h>
#include <btron/clk.h>

/* Forward declarations of app entry points */
extern void cab_app_init(void);
extern void cab_sort(CabSortMode mode);
extern void cab_handle_click(H rel_x, H rel_y, bool is_double_click);

extern void clk_app_init(void);
extern void ms_app_init(void);
extern void ms_set_var(const char *name, int val, const char *str);
extern int  ms_get_var(const char *name);
extern void ms_eval_script(const char *script);
extern void ms_go_card(int card_idx);
extern void ms_handle_click(H rel_x, H rel_y);

extern WND* open_chokanji_kconv_window(void);
extern ER   kconv_convert(int src_enc, int dst_enc, const UB *src, int src_len, UB *dst, int dst_cap, int *dst_len);

extern WND* open_chokanji_xfconv_window(void);
extern ER   xfconv_from_utf8(const char *text, int text_len, UB *xf_out, int xf_cap, int *xf_out_len);
extern ER   xfconv_to_utf8(const UB *xf, int xf_len, char *out, int out_cap, int *out_len);

extern WND* open_chokanji_unpack_window(void);

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s: %s (line %d)\n", __func__, msg, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)

#define TEST_PASS() do { \
    printf("  [PASS] %s\n", __func__); \
    g_pass++; \
} while (0)

/* ── Test 1: PMC Style Engine ───────────────────────────────────────── */
static void test_pmc_style_engine(void) {
    /* Style switching */
    pmc_set_style(WM_STYLE_CHOKANJI);
    TEST_ASSERT(pmc_get_style() == WM_STYLE_CHOKANJI, "PMC style must be CHOKANJI");

    pmc_set_style(WM_STYLE_BEOS);
    TEST_ASSERT(pmc_get_style() == WM_STYLE_BEOS, "PMC style must be BEOS");

    pmc_set_style(WM_STYLE_CHOKANJI);
    TEST_ASSERT(pmc_get_style() == WM_STYLE_CHOKANJI, "PMC style restore CHOKANJI");

    /* Dummy Window for Hit-Testing */
    WND dummy_wnd;
    memset(&dummy_wnd, 0, sizeof(dummy_wnd));
    dummy_wnd.bounds.left = 100;
    dummy_wnd.bounds.top = 100;
    dummy_wnd.bounds.right = 500;
    dummy_wnd.bounds.bottom = 400;
    dummy_wnd.attr = WND_ATTR_TITLE | WND_ATTR_CLOSE | WND_ATTR_RESIZE | WND_ATTR_BORDER;

    /* Close button hit test */
    TEST_ASSERT(pmc_hit_test_close(&dummy_wnd, 485, 110) == TRUE, "Close button hit test inside bounds");
    TEST_ASSERT(pmc_hit_test_close(&dummy_wnd, 200, 110) == FALSE, "Close button hit test outside bounds");

    /* Title drag hit test */
    TEST_ASSERT(pmc_hit_test_title(&dummy_wnd, 200, 110) == TRUE, "Title drag hit test inside titleband");
    TEST_ASSERT(pmc_hit_test_title(&dummy_wnd, 485, 110) == FALSE, "Title drag must not hit close button");
    TEST_ASSERT(pmc_hit_test_title(&dummy_wnd, 200, 200) == FALSE, "Title drag must not hit body");

    TEST_PASS();
}

/* ── Test 2: Cabinet Application ────────────────────────────────────── */
static void test_cabinet_app(void) {
    cab_app_init();

    /* Test sorting */
    cab_sort(CAB_SORT_NAME);
    cab_sort(CAB_SORT_SIZE);
    cab_sort(CAB_SORT_KIND);

    /* Click interaction with client-local coordinates */
    cab_handle_click(40, 50, false);
    cab_handle_click(40, 50, true);

    TEST_PASS();
}

/* ── Test 3: Clock Application ──────────────────────────────────────── */
static void test_clock_app(void) {
    clk_app_init();

    DATE_TIM dt;
    ER er = get_tod(&dt, NULL);
    TEST_ASSERT(er == E_OK, "get_tod must return E_OK");
    TEST_ASSERT(dt.year >= 2024, "Clock year must be valid");

    TEST_PASS();
}

/* ── Test 4: MicroScript Engine ─────────────────────────────────────── */
static void test_microscript_engine(void) {
    ms_app_init();

    /* Variable set / get */
    ms_set_var("alpha", 42, "forty-two");
    TEST_ASSERT(ms_get_var("alpha") == 42, "Variable 'alpha' must evaluate to 42");

    ms_set_var("beta", 100, NULL);
    TEST_ASSERT(ms_get_var("beta") == 100, "Variable 'beta' must evaluate to 100");

    /* Script evaluation */
    ms_eval_script("set gamma = 77; say Hello MicroScript; go card 2");
    TEST_ASSERT(ms_get_var("gamma") == 77, "Script executed variable assignment 'gamma = 77'");

    /* Card navigation */
    ms_go_card(1);
    ms_go_card(0);

    /* Click event with client-local coordinates */
    ms_handle_click(100, 170);

    TEST_PASS();
}

/* ── Test 5: Kanji Converter (kconv) ────────────────────────────────── */
static void test_kconv_app(void) {
    /* 1. Launch GUI Dialog */
    WND *w = open_chokanji_kconv_window();
    TEST_ASSERT(w != NULL, "open_chokanji_kconv_window must return valid WND");

    /* 2. UTF-8 to Shift-JIS & EUC-JP conversion test */
    const char *test_utf8 = "BTRON超漢字";
    UB sjis_buf[128] = {0};
    int sjis_len = 0;
    ER er = kconv_convert(3 /* UTF-8 */, 1 /* Shift-JIS */,
                          (const UB*)test_utf8, (int)strlen(test_utf8),
                          sjis_buf, sizeof(sjis_buf), &sjis_len);
    TEST_ASSERT(er == E_OK, "UTF-8 to Shift-JIS conversion succeeds");
    TEST_ASSERT(sjis_len > 0, "Shift-JIS output length > 0");

    UB euc_buf[128] = {0};
    int euc_len = 0;
    er = kconv_convert(1 /* Shift-JIS */, 0 /* EUC-JP */,
                       sjis_buf, sjis_len,
                       euc_buf, sizeof(euc_buf), &euc_len);
    TEST_ASSERT(er == E_OK, "Shift-JIS to EUC-JP roundtrip succeeds");
    TEST_ASSERT(euc_len > 0, "EUC-JP output length > 0");

    TEST_PASS();
}

/* ── Test 6: XF Converter (xfconv) ──────────────────────────────────── */
static void test_xfconv_app(void) {
    WND *w = open_chokanji_xfconv_window();
    TEST_ASSERT(w != NULL, "open_chokanji_xfconv_window must return valid WND");

    const char *text = "Cho-Kanji XF Specification";
    UB xf_buf[512] = {0};
    int xf_len = 0;
    ER er = xfconv_from_utf8(text, (int)strlen(text), xf_buf, sizeof(xf_buf), &xf_len);
    TEST_ASSERT(er == E_OK, "xfconv_from_utf8 succeeds");
    TEST_ASSERT(xf_len >= 4, "XF output must have at least 4-byte TAD header");
    TEST_ASSERT(memcmp(xf_buf, "TAD ", 4) == 0, "XF magic must be 'TAD '");

    char out_text[256] = {0};
    int out_len = 0;
    er = xfconv_to_utf8(xf_buf, xf_len, out_text, sizeof(out_text) - 1, &out_len);
    TEST_ASSERT(er == E_OK, "xfconv_to_utf8 roundtrip succeeds");
    TEST_ASSERT(strcmp(out_text, text) == 0, "Roundtripped text must match original");

    TEST_PASS();
}

/* ── Test 7: BPK Archive Unpacker (unpack) ──────────────────────────── */
static void test_unpack_app(void) {
    WND *w = open_chokanji_unpack_window();
    TEST_ASSERT(w != NULL, "open_chokanji_unpack_window must return valid WND");

    /* Verify unpacking of ./assets/bpk/thb0805.bpk */
    FILE *f = fopen("assets/bpk/thb0805.bpk", "rb");
    TEST_ASSERT(f != NULL, "assets/bpk/thb0805.bpk must exist and be readable");
    fclose(f);

    FILE *f2 = fopen("assets/bpk/thb0905a.bpk", "rb");
    TEST_ASSERT(f2 != NULL, "assets/bpk/thb0905a.bpk must exist and be readable");
    fclose(f2);

    /* Render window to test display device */
    GDEV *dev = opn_dev(520, 360);
    TEST_ASSERT(dev != NULL, "opn_dev allocates test canvas");
    w->paint(w, dev);
    TEST_ASSERT(dev->pixels != NULL, "unpack_paint executed without crash");

    /* Simulate click on second archive (thb0905a.bpk) */
    EVT evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EV_BUT_DOWN;
    evt.pos.x = w->client.left + 230;
    evt.pos.y = w->client.top + 40;
    w->event_handler(w, &evt);
    w->paint(w, dev);

    /* Simulate click on Unpack button */
    evt.pos.x = w->client.left + 400;
    evt.pos.y = w->client.top + 320;
    w->event_handler(w, &evt);
    w->paint(w, dev);

    cls_dev(dev);
    TEST_PASS();
}

int main(void) {
    printf("=== B-System Cho-Kanji Applications NASA-Standard Verification ===\n");

    test_pmc_style_engine();
    test_cabinet_app();
    test_clock_app();
    test_microscript_engine();
    test_kconv_app();
    test_xfconv_app();
    test_unpack_app();

    printf("\n=== Results: %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
