/*
 * B-System (BTRON 3.20) Clarity Frames & Controls Unit Test
 * Tests perimeter hit-testing, 8-handle resizing, word-wrapping, and caret positioning.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "apps/clarity_doc.h"
#include <btron/dp.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s: %s (line %d)\n", __func__, msg, __LINE__); \
        g_fail++; \
        return; \
    } \
} while(0)

static void test_hit_testing(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.fmt = FMT_A4;
    doc.zoom_pct = 100;
    doc.frame_count = 1;
    doc.selected_frame = 0;

    ClarityFrame *f = &doc.frames[0];
    f->id = 1;
    f->type = FRAME_TEXT;
    f->bounds.left = 100;
    f->bounds.top = 100;
    f->bounds.right = 300;
    f->bounds.bottom = 200;
    f->flow = FLOW_H_LTR;

    int ox = 0, oy = 0;
    ClarityHitInfo info;

    /* 1. Test handle hits (Handle 0: top-left corner 100, 100) */
    clarity_hittest_full(&doc, 100, 100, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_HANDLE, "Handle 0 top-left should be HIT_HANDLE");
    TEST_ASSERT(info.handle_idx == 0, "Handle index should be 0");

    /* Handle 4: bottom-right corner 300, 200 */
    clarity_hittest_full(&doc, 300, 200, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_HANDLE, "Handle 4 bottom-right should be HIT_HANDLE");
    TEST_ASSERT(info.handle_idx == 4, "Handle index should be 4");

    /* Handle 1: top-middle 200, 100 */
    clarity_hittest_full(&doc, 200, 100, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_HANDLE, "Handle 1 top-mid should be HIT_HANDLE");
    TEST_ASSERT(info.handle_idx == 1, "Handle index should be 1");

    /* Handle 7: left-middle 100, 150 */
    clarity_hittest_full(&doc, 100, 150, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_HANDLE, "Handle 7 left-mid should be HIT_HANDLE");
    TEST_ASSERT(info.handle_idx == 7, "Handle index should be 7");

    /* 2. Test perimeter hits (away from handles, e.g. at x=140, y=102) */
    clarity_hittest_full(&doc, 140, 102, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_PERIMETER, "Top border band should be HIT_PERIMETER");
    TEST_ASSERT(info.frame_idx == 0, "Frame index should be 0");

    /* Left border band at x=102, y=125 (between handle 0 at y=100 and handle 7 at y=150) */
    clarity_hittest_full(&doc, 102, 125, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_PERIMETER, "Left border band should be HIT_PERIMETER");
    TEST_ASSERT(info.frame_idx == 0, "Frame index should be 0");

    /* 3. Test interior hit (center of frame at x=200, y=125) */
    clarity_hittest_full(&doc, 200, 125, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_INTERIOR, "Frame interior should be HIT_INTERIOR");
    TEST_ASSERT(info.frame_idx == 0, "Frame index should be 0");

    /* 4. Test outside hit (x=50, y=50) */
    clarity_hittest_full(&doc, 50, 50, ox, oy, &info);
    TEST_ASSERT(info.target == CLARITY_HIT_NONE, "Outside should be HIT_NONE");

    printf("PASS: test_hit_testing\n");
    g_pass++;
}

static void test_resizing_and_clamping(void)
{
    ClarityFrame f;
    memset(&f, 0, sizeof(f));
    f.id = 1;
    f.bounds.left = 100;
    f.bounds.top = 100;
    f.bounds.right = 300;
    f.bounds.bottom = 200;

    int ox = 0, oy = 0, zoom = 100;

    /* Resize Handle 4 (SE corner) outward to (350, 250) */
    clarity_resize_frame_handle(&f, 4, 350, 250, ox, oy, zoom);
    TEST_ASSERT(f.bounds.right == 350, "Right bound should expand to 350");
    TEST_ASSERT(f.bounds.bottom == 250, "Bottom bound should expand to 250");

    /* Resize Handle 4 inward past minimum width -> clamp */
    clarity_resize_frame_handle(&f, 4, 110, 110, ox, oy, zoom);
    TEST_ASSERT(f.bounds.right == 350, "Right bound should clamp at MIN_W");
    TEST_ASSERT(f.bounds.bottom == 250, "Bottom bound should clamp at MIN_H");

    /* Resize Handle 0 (NW corner) outward to (80, 80) */
    clarity_resize_frame_handle(&f, 0, 80, 80, ox, oy, zoom);
    TEST_ASSERT(f.bounds.left == 80, "Left bound should be 80");
    TEST_ASSERT(f.bounds.top == 80, "Top bound should be 80");

    /* Move frame */
    clarity_move_frame(&f, 20, -10);
    TEST_ASSERT(f.bounds.left == 100, "Left moved +20");
    TEST_ASSERT(f.bounds.top == 70, "Top moved -10");
    TEST_ASSERT(f.bounds.right == 370, "Right moved +20");
    TEST_ASSERT(f.bounds.bottom == 240, "Bottom moved -10");

    printf("PASS: test_resizing_and_clamping\n");
    g_pass++;
}

static void test_word_wrap_and_navigation(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.frame_count = 1;
    doc.selected_frame = 0;

    ClarityFrame *f = &doc.frames[0];
    f->id = 1;
    f->type = FRAME_TEXT;
    f->bounds.left = 0;
    f->bounds.top = 0;
    f->bounds.right = 160;  /* inner_w ~ 152 px, can hold ~16 halfwidth chars */
    f->bounds.bottom = 200;
    f->flow = FLOW_H_LTR;

    const char *text = "Hello BTRON World of Clarity DTP!";
    f->text_len = 0;
    for (int i = 0; text[i]; i++) {
        f->text[f->text_len++] = (UH)(unsigned char)text[i];
    }
    f->cursor_pos = (int)f->text_len;

    /* Up arrow from end */
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_UP, 0);
    TEST_ASSERT(f->cursor_pos < (int)f->text_len, "Up arrow should move caret to previous visual line");

    /* Down arrow should return towards end */
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_DOWN, 0);
    TEST_ASSERT(f->cursor_pos > 0, "Down arrow should move caret downward");

    /* Click to pos */
    int pos_start = clarity_text_xy_to_pos(f, 4, 4, 0, 0, 100);
    TEST_ASSERT(pos_start == 0, "Click at top-left should position caret at index 0");

    /* Typing character */
    f->cursor_pos = 0;
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_CHAR, (UH)'X');
    TEST_ASSERT(f->text[0] == 'X', "Inserted char should be 'X'");
    TEST_ASSERT(f->cursor_pos == 1, "Cursor should advance to 1");

    /* Backspace */
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_BACKSPACE, 0);
    TEST_ASSERT(f->text[0] == 'H', "First char restored to 'H'");
    TEST_ASSERT(f->cursor_pos == 0, "Cursor back to 0");

    printf("PASS: test_word_wrap_and_navigation\n");
    g_pass++;
}

int main(void)
{
    printf("=== Clarity Frames & Controls Test Suite ===\n");
    test_hit_testing();
    test_resizing_and_clamping();
    test_word_wrap_and_navigation();

    printf("Results: %d Passed, %d Failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
