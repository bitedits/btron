/*
 * B-System (BTRON 3.20) Clarity Frames & Controls Unit Test
 * Tests perimeter hit-testing, 8-handle resizing, word-wrapping, caret positioning,
 * and live window mouse event handling with real window client offsets.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "apps/clarity_doc.h"
#include <btron/dp.h>
#include <btron/wnd.h>
#include <btron/event.h>
#include <btron/app_menu.h>
#include <btron/troncode.h>

extern WND* open_clarity_window(void);

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

static void test_live_window_interactions(void)
{
    WND *w = open_clarity_window();
    TEST_ASSERT(w != NULL, "open_clarity_window should return a valid window");

    ClarityDoc *doc = clarity_get_doc();
    TEST_ASSERT(doc != NULL && doc->frame_count > 0, "Doc must have sample frame");

    ClarityFrame *f = &doc->frames[0];
    H orig_left = f->bounds.left;
    H orig_top = f->bounds.top;
    int zoom = doc->zoom_pct;
    int ox = CLARITY_CANVAS_MARGIN_PX;
    int oy = CLARITY_CANVAS_MARGIN_PX + APP_MENU_BAR_HEIGHT;

    /* 1. Simulate click on perimeter with real window client offset */
    int perim_unzoomed_x = orig_left + 40;
    int perim_unzoomed_y = orig_top + 2;
    int screen_x = w->client.left + ox + (perim_unzoomed_x * zoom) / 100;
    int screen_y = w->client.top  + oy + (perim_unzoomed_y * zoom) / 100;

    EVT evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = EV_BUT_DOWN;
    evt.pos.x = screen_x;
    evt.pos.y = screen_y;
    evt.data = (void*)(uintptr_t)1;
    w->event_handler(w, &evt);

    /* Move mouse by +30 in X, +20 in Y */
    evt.type = EV_MOUSE_MOVE;
    evt.pos.x = screen_x + 30;
    evt.pos.y = screen_y + 20;
    w->event_handler(w, &evt);

    evt.type = EV_BUT_UP;
    evt.pos.x = screen_x + 30;
    evt.pos.y = screen_y + 20;
    w->event_handler(w, &evt);

    int moved_dx = f->bounds.left - orig_left;
    int moved_dy = f->bounds.top - orig_top;
    TEST_ASSERT(moved_dx > 0 && moved_dy > 0, "Perimeter drag must translate frame with window geometry");

    /* 2. Simulate click on Handle 4 (SE corner) */
    H cur_r = f->bounds.right;
    H cur_b = f->bounds.bottom;
    int handle_sx = w->client.left + ox + (cur_r * zoom) / 100;
    int handle_sy = w->client.top  + oy + (cur_b * zoom) / 100;

    evt.type = EV_BUT_DOWN;
    evt.pos.x = handle_sx;
    evt.pos.y = handle_sy;
    w->event_handler(w, &evt);

    /* Drag outward by +50 in X, +40 in Y */
    evt.type = EV_MOUSE_MOVE;
    evt.pos.x = handle_sx + 50;
    evt.pos.y = handle_sy + 40;
    w->event_handler(w, &evt);

    evt.type = EV_BUT_UP;
    evt.pos.x = handle_sx + 50;
    evt.pos.y = handle_sy + 40;
    w->event_handler(w, &evt);

    int resize_dw = f->bounds.right - cur_r;
    int resize_dh = f->bounds.bottom - cur_b;
    TEST_ASSERT(resize_dw > 0 && resize_dh > 0, "Handle drag must resize frame with window geometry");

    printf("PASS: test_live_window_interactions\n");
    g_pass++;
}

static void test_z_ordering(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.frame_count = 4;
    for (int i = 0; i < 4; i++) {
        doc.frames[i].id = (UB)(i + 1);
        doc.frames[i].type = FRAME_TEXT;
    }

    /* Initial state: IDs 1, 2, 3, 4 */
    /* 1. Send frame at index 3 (ID 4) to back (index 0) */
    doc.selected_frame = 3;
    int rc = clarity_doc_send_to_back(&doc, 3);
    TEST_ASSERT(rc == 0, "send_to_back should return 0");
    TEST_ASSERT(doc.frames[0].id == 4, "Frame ID 4 should now be at index 0");
    TEST_ASSERT(doc.frames[1].id == 1, "Frame ID 1 should now be at index 1");
    TEST_ASSERT(doc.frames[2].id == 2, "Frame ID 2 should now be at index 2");
    TEST_ASSERT(doc.frames[3].id == 3, "Frame ID 3 should now be at index 3");
    TEST_ASSERT(doc.selected_frame == 0, "selected_frame should update to 0");

    /* 2. Send frame at index 0 (ID 4) to front (index 3) */
    rc = clarity_doc_send_to_front(&doc, 0);
    TEST_ASSERT(rc == 0, "send_to_front should return 0");
    TEST_ASSERT(doc.frames[3].id == 4, "Frame ID 4 should now be at index 3");
    TEST_ASSERT(doc.frames[0].id == 1, "Frame ID 1 should now be at index 0");
    TEST_ASSERT(doc.selected_frame == 3, "selected_frame should update to 3");

    /* 3. Send backward: swap index 2 (ID 3) with index 1 (ID 2) */
    doc.selected_frame = 2;
    rc = clarity_doc_send_backward(&doc, 2);
    TEST_ASSERT(rc == 0, "send_backward should return 0");
    TEST_ASSERT(doc.frames[1].id == 3, "Frame ID 3 should now be at index 1");
    TEST_ASSERT(doc.frames[2].id == 2, "Frame ID 2 should now be at index 2");
    TEST_ASSERT(doc.selected_frame == 1, "selected_frame should update to 1");

    /* 4. Send forward: swap index 1 (ID 3) with index 2 (ID 2) */
    rc = clarity_doc_send_forward(&doc, 1);
    TEST_ASSERT(rc == 0, "send_forward should return 0");
    TEST_ASSERT(doc.frames[2].id == 3, "Frame ID 3 should now be back at index 2");
    TEST_ASSERT(doc.frames[1].id == 2, "Frame ID 2 should now be back at index 1");
    TEST_ASSERT(doc.selected_frame == 2, "selected_frame should update to 2");

    printf("PASS: test_z_ordering\n");
    g_pass++;
}

static void test_frame_duplication(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.frame_count = 1;
    doc.selected_frame = 0;

    ClarityFrame *f = &doc.frames[0];
    f->id = 1;
    f->type = FRAME_TEXT;
    f->bounds.left = 50;
    f->bounds.top = 60;
    f->bounds.right = 200;
    f->bounds.bottom = 180;
    f->text[0] = 'H';
    f->text[1] = 'i';
    f->text_len = 2;

    int dup_idx = clarity_doc_duplicate_frame(&doc, 0);
    TEST_ASSERT(dup_idx == 1, "Duplicate frame index should be 1");
    TEST_ASSERT(doc.frame_count == 2, "frame_count should now be 2");
    TEST_ASSERT(doc.selected_frame == 1, "selected_frame should be 1");

    ClarityFrame *f2 = &doc.frames[1];
    TEST_ASSERT(f2->id == 2, "Duplicated frame should have id 2");
    TEST_ASSERT(f2->bounds.left == 50 + 16, "Duplicated frame left should be offset by 16");
    TEST_ASSERT(f2->bounds.top == 60 + 16, "Duplicated frame top should be offset by 16");
    TEST_ASSERT(f2->bounds.right == 200 + 16, "Duplicated frame right should be offset by 16");
    TEST_ASSERT(f2->bounds.bottom == 180 + 16, "Duplicated frame bottom should be offset by 16");
    TEST_ASSERT(f2->text_len == 2 && f2->text[0] == 'H' && f2->text[1] == 'i', "Text contents should be preserved");

    printf("PASS: test_frame_duplication\n");
    g_pass++;
}

static void test_multilingual_tip_text(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.frame_count = 1;
    doc.selected_frame = 0;

    ClarityFrame *f = &doc.frames[0];
    f->id = 1;
    f->type = FRAME_TEXT;
    f->text_len = 0;
    f->cursor_pos = 0;

    /* Insert UTF-8 Japanese via TIP helper */
    clarity_insert_tip_text(&doc, 0, "電子帳票");
    TEST_ASSERT(f->text_len == 4, "Japanese string '電子帳票' should produce 4 TRON Code units");
    TEST_ASSERT(f->cursor_pos == 4, "Caret position should advance by 4");

    /* Verify first character is Kanji (Plane 1 >= 0x2100) */
    TEST_ASSERT(f->text[0] >= 0x2100, "First character must be a valid TRON Kanji code point (Plane 1)");

    printf("PASS: test_multilingual_tip_text\n");
    g_pass++;
}

static void test_tibetan_glyph_advances_and_stacking(void)
{
    /* 1. Base consonant Ka (U+0F40 -> 0x9F40): 8px advance */
    TC ka = 0x9F40;
    TEST_ASSERT(tc_get_char_advance(ka, 0) == 8, "Tibetan base consonant Ka must have 8px advance");

    /* 2. Combining Vowel I (U+0F72 -> 0x9F72): 0px advance (stacks onto base) */
    TC vowel_i = 0x9F72;
    TEST_ASSERT(tc_get_char_advance(vowel_i, ka) == 0, "Tibetan combining vowel I must have 0px advance");

    /* 3. Subjoined Consonant Ya (U+0F9B -> 0x9F9B): 0px advance (stacks onto base) */
    TC sub_ya = 0x9F9B;
    TEST_ASSERT(tc_get_char_advance(sub_ya, ka) == 0, "Tibetan subjoined Ya must have 0px advance");

    /* 4. Tsheg syllable delimiter (U+0F0B -> 0x9F0B): 3px compact advance */
    TC tsheg = 0x9F0B;
    TEST_ASSERT(tc_get_char_advance(tsheg, ka) == 3, "Tibetan Tsheg must have 3px compact advance");

    /* 5. Space after Tsheg collapses to 0px */
    TEST_ASSERT(tc_get_char_advance(' ', tsheg) == 0, "ASCII space following Tibetan Tsheg must collapse to 0px");

    /* 6. Standard ASCII and Japanese advances */
    TEST_ASSERT(tc_get_char_advance('A', 0) == 8, "ASCII 'A' must have 8px advance");
    TEST_ASSERT(tc_get_char_advance(0x2121, 0) == 16, "Japanese fullwidth character must have 16px advance");

    printf("PASS: test_tibetan_glyph_advances_and_stacking\n");
    g_pass++;
}

static void test_text_frame_invisible_area_scrolling(void)
{
    ClarityDoc doc;
    memset(&doc, 0, sizeof(doc));
    doc.frame_count = 1;
    doc.selected_frame = 0;

    ClarityFrame *f = &doc.frames[0];
    f->id = 1;
    f->type = FRAME_TEXT;
    f->bounds.left = 10;
    f->bounds.top = 10;
    f->bounds.right = 200;
    f->bounds.bottom = 60; /* Small height (~50px), fits only ~2 lines */
    f->scroll_y = 0;

    /* Fill with 6 lines of text */
    const char *text = "Line 1\nLine 2\nLine 3\nLine 4\nLine 5\nLine 6\n";
    f->text_len = (UW)utf8_to_tc_string(text, (TC*)f->text, CLARITY_TEXT_BUF - 1);
    f->cursor_pos = 0;

    /* 1. Initial scroll_y is 0 */
    TEST_ASSERT(f->scroll_y == 0, "Initial frame scroll_y must be 0");

    /* 2. PageDown scrolls down */
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_PAGEDOWN, 0);
    TEST_ASSERT(f->scroll_y > 0, "PageDown must scroll invisible area downwards (scroll_y > 0)");

    /* 3. PageUp scrolls back up to 0 */
    clarity_handle_text_action(&doc, 0, CLARITY_ACT_PAGEUP, 0);
    TEST_ASSERT(f->scroll_y == 0, "PageUp must scroll back up and clamp to 0");

    /* 4. Moving caret down across lines auto-scrolls */
    for (int i = 0; i < 5; i++) {
        clarity_handle_text_action(&doc, 0, CLARITY_ACT_DOWN, 0);
    }
    TEST_ASSERT(f->scroll_y > 0, "Navigating caret down past bottom must auto-scroll to keep caret visible");

    /* 5. Hit testing with scroll_y accurately maps line */
    int hit_pos = clarity_text_xy_to_pos(f, 20, 25, 0, 0, 100);
    TEST_ASSERT(hit_pos > 0, "clarity_text_xy_to_pos must account for scroll_y when mapping mouse to position");

    printf("PASS: test_text_frame_invisible_area_scrolling\n");
    g_pass++;
}

int main(void)
{
    printf("=== Clarity Frames & Controls Test Suite ===\n");
    test_hit_testing();
    test_resizing_and_clamping();
    test_word_wrap_and_navigation();
    test_live_window_interactions();
    test_z_ordering();
    test_frame_duplication();
    test_multilingual_tip_text();
    test_tibetan_glyph_advances_and_stacking();
    test_text_frame_invisible_area_scrolling();

    printf("Results: %d Passed, %d Failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
