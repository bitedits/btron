/*
 * B-System (BTRON 3.20) HMI Standard Library Unit Test Suite: test_hmi.c
 * Comprehensive test coverage for TRON HMI Standard components.
 */

#include <stdio.h>
#include <string.h>
#include <btron/hmi.h>

static int g_tests_passed = 0;
static int g_tests_total = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_total++; \
    if (cond) { \
        printf("  [PASS] %s\n", msg); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", msg, __LINE__); \
    } \
} while(0)

/* Callback track counters */
static int g_cb_count = 0;
static int g_last_val = 0;

static void dummy_callback(HMI_CTRL *ctrl, HMI_PANEL *p, void *data) {
    (void)p; (void)data;
    g_cb_count++;
    g_last_val = ctrl->val;
}

/* ── 1. Panel & Lifecycle Tests ── */
static void test_panel_lifecycle(void) {
    printf("\n=== 1. Testing HMI Panel Lifecycle ===\n");
    HMI_PANEL panel;
    ER err = hmi_init_panel(&panel, "Test Appliance Panel", 10, 20, 400, 300, COLOR_LTGRAY);

    TEST_ASSERT(err == E_OK, "Panel initializes with E_OK");
    TEST_ASSERT(strcmp(panel.title, "Test Appliance Panel") == 0, "Panel title matches");
    TEST_ASSERT(panel.bounds.left == 10 && panel.bounds.right == 410, "Bounds width computed correctly");
    TEST_ASSERT(panel.num_controls == 0, "Initial control count is 0");
    TEST_ASSERT(panel.focused_index == -1, "Initial focus is -1");
}

/* ── 2. Switch & Edge Trigger Tests ── */
static void test_switches_and_triggers(void) {
    printf("\n=== 2. Testing Switches and Edge Triggering ===\n");
    HMI_PANEL p;
    hmi_init_panel(&p, "Switch Panel", 0, 0, 300, 200, COLOR_WHITE);

    g_cb_count = 0;
    HMI_CTRL *push = hmi_add_push_switch(&p, 1, "PUSH", 10, 10, 80, 30, dummy_callback);
    TEST_ASSERT(push != NULL, "Push switch added successfully");
    TEST_ASSERT(push->trigger == HMI_TRIGGER_RELEASE_EDGE, "Push switch defaults to Release-Edge (Chapter 9)");

    /* Simulate Mouse Down (Press) -> Should NOT trigger Release-Edge callback yet */
    EVT ev_down = { .type = EV_BUT_DOWN, .pos = { .x = 20, .y = 20 } };
    hmi_dispatch_event(&p, &ev_down);
    TEST_ASSERT((push->flags & HMI_STATE_PRESSED) != 0, "Push switch enters PRESSED state");
    TEST_ASSERT(g_cb_count == 0, "Release-Edge switch did not trigger on Mouse Down");

    /* Simulate Mouse Up (Release) inside bounds -> Should trigger callback */
    EVT ev_up = { .type = EV_BUT_UP, .pos = { .x = 20, .y = 20 } };
    hmi_dispatch_event(&p, &ev_up);
    TEST_ASSERT(g_cb_count == 1, "Release-Edge switch triggered exactly on Mouse Up");
    TEST_ASSERT((push->flags & HMI_STATE_PRESSED) == 0, "Push switch cleared PRESSED state");

    /* Test Toggle Switch (Alternate state) */
    HMI_CTRL *toggle = hmi_add_toggle_switch(&p, 2, "POWER", 100, 10, 80, 30, FALSE, dummy_callback);
    TEST_ASSERT(toggle != NULL, "Toggle switch added");
    TEST_ASSERT(toggle->val == 0, "Initial toggle value is OFF (0)");

    /* Click toggle */
    hmi_dispatch_event(&p, &ev_down); /* on toggle pos */
    EVT ev_toggle_down = { .type = EV_BUT_DOWN, .pos = { .x = 120, .y = 20 } };
    hmi_dispatch_event(&p, &ev_toggle_down);
    TEST_ASSERT(toggle->val == 1, "Toggle switched to ON (1)");
    TEST_ASSERT((toggle->flags & HMI_STATE_CHECKED) != 0, "Toggle has CHECKED state flag");
}

/* ── 3. Selector Tests ── */
static void test_selectors(void) {
    printf("\n=== 3. Testing Selectors (Up/Down & Radio) ===\n");
    HMI_PANEL p;
    hmi_init_panel(&p, "Selector Panel", 0, 0, 300, 200, COLOR_WHITE);

    /* Up/Down Stepper */
    HMI_CTRL *stepper = hmi_add_updown_selector(&p, 10, "COUNT", 10, 10, 120, 30, 0, 10, 5, "個", dummy_callback);
    TEST_ASSERT(stepper != NULL, "Up/Down selector added");
    TEST_ASSERT(stepper->val == 5, "Initial stepper value is 5");

    /* Click top half [▲] */
    EVT ev_up = { .type = EV_BUT_DOWN, .pos = { .x = 120, .y = 15 } };
    hmi_dispatch_event(&p, &ev_up);
    TEST_ASSERT(stepper->val == 6, "Stepper incremented to 6");

    /* Click bottom half [▼] */
    EVT ev_down = { .type = EV_BUT_DOWN, .pos = { .x = 120, .y = 35 } };
    hmi_dispatch_event(&p, &ev_down);
    TEST_ASSERT(stepper->val == 5, "Stepper decremented back to 5");

    /* Radio Selector Matrix */
    const char *opts[] = { "FM", "AM", "SW", "AUX" };
    HMI_CTRL *radio = hmi_add_radio_selector(&p, 11, "BAND", 10, 50, 100, 80, 4, opts, 0, dummy_callback);
    TEST_ASSERT(radio != NULL, "Radio selector added");
    TEST_ASSERT(radio->val == 0, "Initial radio selection is FM (index 0)");

    /* Click item 2 (SW) */
    EVT ev_radio = { .type = EV_BUT_DOWN, .pos = { .x = 20, .y = 95 } };
    hmi_dispatch_event(&p, &ev_radio);
    TEST_ASSERT(radio->val == 2, "Radio selection switched to SW (index 2)");
}

/* ── 4. Volume & Slider Tests ── */
static void test_volume_controls(void) {
    printf("\n=== 4. Testing Continuous Volumes & Dials ===\n");
    HMI_PANEL p;
    hmi_init_panel(&p, "Volume Panel", 0, 0, 300, 200, COLOR_WHITE);

    /* Linear Slider */
    HMI_CTRL *slider = hmi_add_slider_volume(&p, 20, "LEVEL", 10, 10, 200, 40, 0, 100, 50, dummy_callback);
    TEST_ASSERT(slider != NULL, "Slider volume added");
    TEST_ASSERT(slider->val == 50, "Initial slider value is 50%");

    /* Click at 75% track position */
    EVT ev_slide = { .type = EV_BUT_DOWN, .pos = { .x = 160, .y = 28 } };
    hmi_dispatch_event(&p, &ev_slide);
    TEST_ASSERT(slider->val >= 70 && slider->val <= 85, "Slider positioned proportionally near 75%");

    /* Rotary Dial Volume */
    HMI_CTRL *dial = hmi_add_dial_volume(&p, 21, "BASS", 10, 60, 60, 60, -5, 5, 0, dummy_callback);
    TEST_ASSERT(dial != NULL, "Rotary dial added");
    TEST_ASSERT(dial->val == 0, "Initial dial center is 0 dB");
}

/* ── 5. Universal Controller (万能コントローラ) Tests ── */
static void test_universal_controller(void) {
    printf("\n=== 5. Testing Universal Controller Navigation ===\n");
    HMI_PANEL p;
    hmi_init_panel(&p, "Remote Control Panel", 0, 0, 400, 300, COLOR_WHITE);

    HMI_CTRL *c1 = hmi_add_updown_selector(&p, 1, "ITEM1", 10, 10, 100, 30, 0, 10, 3, "", dummy_callback);
    HMI_CTRL *c2 = hmi_add_updown_selector(&p, 2, "ITEM2", 10, 50, 100, 30, 0, 10, 7, "", dummy_callback);
    (void)c1; (void)c2;

    /* Initial focus */
    hmi_set_focus(&p, 0);
    TEST_ASSERT(p.focused_index == 0, "Focused on Item 1");

    /* Remote Key [▼/次] -> Move Focus to Item 2 */
    hmi_handle_universal_key(&p, HMI_KEY_NEXT_ITEM);
    TEST_ASSERT(p.focused_index == 1, "Focus moved to Item 2 via [▼/次]");

    /* Remote Key [►/+] -> Increment Item 2 value */
    hmi_handle_universal_key(&p, HMI_KEY_INC_VALUE);
    TEST_ASSERT(p.controls[1].val == 8, "Item 2 incremented to 8 via [►/+]");

    /* Remote Key [◄/-] -> Decrement Item 2 value */
    hmi_handle_universal_key(&p, HMI_KEY_DEC_VALUE);
    TEST_ASSERT(p.controls[1].val == 7, "Item 2 decremented to 7 via [◄/-]");

    /* Remote Key [▲/前] -> Move Focus back to Item 1 */
    hmi_handle_universal_key(&p, HMI_KEY_PREV_ITEM);
    TEST_ASSERT(p.focused_index == 0, "Focus returned to Item 1 via [▲/前]");

    /* Remote Key [X] -> Reset Item 1 to min_val */
    hmi_handle_universal_key(&p, HMI_KEY_CANCEL);
    TEST_ASSERT(p.controls[0].val == 0, "Item 1 reset to 0 via [X/取り消し]");
}

int main(void) {
    printf("====================================================\n");
    printf(" TRON HMI Standard Library — Unit Test Suite\n");
    printf("====================================================\n");

    test_panel_lifecycle();
    test_switches_and_triggers();
    test_selectors();
    test_volume_controls();
    test_universal_controller();

    printf("\n====================================================\n");
    printf(" Test Results: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_total,
           (g_tests_total > 0) ? ((float)g_tests_passed / g_tests_total * 100.0f) : 0.0f);
    printf("====================================================\n");

    return (g_tests_passed == g_tests_total) ? 0 : 1;
}
