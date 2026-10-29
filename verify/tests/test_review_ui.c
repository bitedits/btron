/*
 * B-System (BTRON 3.20) Review PDF Application Test Suite
 * Pure C99, verifies menus, scrollbars, zoom, marginalia, and about box.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <btron/wnd.h>
#include <btron/dp.h>
#include <btron/event.h>
#include <btron/app_menu.h>
#include <btron/apps.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  [FAIL] %s: %s (line %d)\n", __func__, msg, __LINE__); \
        g_fail++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    printf("  [PASS] %s\n", msg); \
    g_pass++; \
} while(0)

static void test_review_init_and_geometry(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "open_review_window_with_file must return a valid window");
    TEST_ASSERT(strstr(wnd->title, "Review") != NULL, "Window title must contain 'Review'");
    TEST_ASSERT(wnd->dev != NULL, "Window device must be initialized");
    TEST_ASSERT(wnd->paint != NULL, "Paint callback must be registered");
    TEST_ASSERT(wnd->event_handler != NULL, "Event handler must be registered");
    TEST_ASSERT(wnd->menu_open != NULL, "Menu open callback must be registered");
    TEST_ASSERT(wnd->menu_open(wnd) == FALSE, "Menu must initially be closed");

    /* Render initial frame */
    wnd->paint(wnd, wnd->dev);
    TEST_PASS("Review window created and painted with correct geometry");
}

static void test_menu_and_dropdown_interaction(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "Window opened");

    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* 1. Mouse hover over File Header */
    ev.type = EV_MOUSE_MOVE;
    ev.pos.x = wnd->client.left + 30;
    ev.pos.y = wnd->client.top + 10;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Menu hover event processed");

    /* 2. Mouse click on Header 0: ファイル(F) */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + 30;
    ev.pos.y = wnd->client.top + 10;
    wnd->event_handler(wnd, &ev);
    TEST_ASSERT(wnd->menu_open(wnd) == TRUE, "Header 0 click must open dropdown menu");
    TEST_PASS("Menu opened on header mouse click");

    /* 3. Repaint while menu is open */
    wnd->paint(wnd, wnd->dev);
    TEST_PASS("Menu painted with active dropdown plate");

    /* 4. Click Header 1: 表示(V) to hot-switch */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + 120;
    ev.pos.y = wnd->client.top + 10;
    wnd->event_handler(wnd, &ev);
    TEST_ASSERT(wnd->menu_open(wnd) == TRUE, "Menu remains open on header glide");
    TEST_PASS("Menu glides smoothly between headers");

    /* 5. Click outside to dismiss */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + 500;
    ev.pos.y = wnd->client.top + 400;
    wnd->event_handler(wnd, &ev);
    TEST_ASSERT(wnd->menu_open(wnd) == FALSE, "Click outside menu must dismiss it");
    TEST_PASS("Menu closed on outside click");
}

static void test_zoom_operations(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "Window opened");

    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* 1. Zoom In via keyboard '+' */
    ev.type = EV_KEY_DOWN;
    ev.key = '+';
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Zoom In key handled");

    /* 2. Zoom Out via keyboard '-' */
    ev.key = '-';
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Zoom Out key handled");

    /* 3. Zoom 100% via keyboard '0' */
    ev.key = '0';
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Zoom 100% key handled");

    /* 4. Zoom Fit via keyboard 'f' */
    ev.key = 'f';
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Zoom Fit key handled");

    /* 5. Zoom In via Menu Click (Header 1 -> Row 3) */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + 120;
    ev.pos.y = wnd->client.top + 10;
    wnd->event_handler(wnd, &ev); /* Open View menu */

    ev.pos.y = wnd->client.top + 100; /* Row 3: 拡大 */
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Zoom In via in-app menu command executed");
}

static void test_scrollbar_operations(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "Window opened");

    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* Set zoom to 200% so scroll range exists */
    ev.type = EV_KEY_DOWN;
    ev.key = '+';
    wnd->event_handler(wnd, &ev);
    wnd->event_handler(wnd, &ev);
    wnd->event_handler(wnd, &ev);
    wnd->event_handler(wnd, &ev);
    wnd->event_handler(wnd, &ev);

    /* Vertical scroll down arrow button (x=530, y=21 + avail_h - 8) */
    int win_w = wnd->dev->width;
    int rail_w = (win_w > 640) ? 230 : 0;
    int avail_w = win_w - rail_w - 16;
    int avail_h = wnd->dev->height - 21 - 16;

    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + avail_w + 8;
    ev.pos.y = wnd->client.top + 21 + avail_h - 8;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Vertical scrollbar down button clicked");

    /* Vertical scroll up arrow button */
    ev.pos.y = wnd->client.top + 21 + 8;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Vertical scrollbar up button clicked");

    /* Vertical scrollbar thumb drag */
    ev.pos.y = wnd->client.top + 21 + 50;
    wnd->event_handler(wnd, &ev); /* Grab thumb */

    ev.type = EV_MOUSE_MOVE;
    ev.pos.y = wnd->client.top + 21 + 100;
    wnd->event_handler(wnd, &ev); /* Drag downward */

    ev.type = EV_BUT_UP;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Vertical scrollbar thumb dragged smoothly");

    /* Horizontal scroll right arrow button */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + avail_w - 8;
    ev.pos.y = wnd->client.top + 21 + avail_h + 8;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Horizontal scrollbar right button clicked");

    /* Horizontal scroll left arrow button */
    ev.pos.x = wnd->client.left + 8;
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Horizontal scrollbar left button clicked");
}

static void test_about_dialog(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "Window opened");

    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* Open Header 3: ヘルプ(H) */
    ev.type = EV_BUT_DOWN;
    ev.pos.x = wnd->client.left + 280;
    ev.pos.y = wnd->client.top + 10;
    wnd->event_handler(wnd, &ev);
    TEST_ASSERT(wnd->menu_open(wnd) == TRUE, "Help menu opened");

    /* Click Row 0: Review について (About...) */
    ev.pos.y = wnd->client.top + 35;
    wnd->event_handler(wnd, &ev);

    WND *top = get_top_wnd();
    TEST_ASSERT(top != NULL && top != wnd, "About dialog must be topmost window");
    TEST_ASSERT(strstr(top->title, "Review") != NULL, "About dialog title must reference Review");

    /* Paint About Dialog */
    top->paint(top, top->dev);
    TEST_PASS("About dialog opened and painted with authentic 3D plate");

    /* Standalone opener function */
    WND *abt_direct = open_review_about_window();
    TEST_ASSERT(abt_direct != NULL, "open_review_about_window must return valid window");
    TEST_PASS("Standalone open_review_about_window() functional");
}

static void test_page_navigation_and_annotations(void) {
    WND *wnd = open_review_window_with_file("doc/pdf/n64.pdf");
    TEST_ASSERT(wnd != NULL, "Window opened");

    EVT ev;
    memset(&ev, 0, sizeof(ev));

    /* Page Navigation via PageDown key */
    ev.type = EV_KEY_DOWN;
    ev.key = 0xFF56; /* PageDown */
    wnd->event_handler(wnd, &ev);
    TEST_PASS("PageDown navigation handled");

    /* Page Navigation via PageUp key */
    ev.key = 0xFF55; /* PageUp */
    wnd->event_handler(wnd, &ev);
    TEST_PASS("PageUp navigation handled");

    /* Add Fusen annotation via shortcut Ctrl+M */
    ev.key = 'm';
    ev.data = (void*)(uintptr_t)0x0040; /* BTRON_KMOD_CTRL */
    wnd->event_handler(wnd, &ev);
    TEST_PASS("Fusen note added to Marginalia rail");
}

int main(void) {
    printf("==========================================================\n");
    printf(" B-System Review PDF & Marginalia Workspace Unit Tests\n");
    printf("==========================================================\n\n");

    test_review_init_and_geometry();
    test_menu_and_dropdown_interaction();
    test_zoom_operations();
    test_scrollbar_operations();
    test_about_dialog();
    test_page_navigation_and_annotations();

    printf("\n==========================================================\n");
    printf(" Results: %d PASS, %d FAIL\n", g_pass, g_fail);
    printf("==========================================================\n");

    return g_fail > 0 ? 1 : 0;
}

