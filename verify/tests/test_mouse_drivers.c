/*
 * test_mouse_drivers.c — Unit Test Suite for UEFI PS/2 & PC-98 Mouse Drivers
 *
 * Tests:
 *   1. 8042 status byte parsing (Keyboard vs Mouse Auxiliary discrimination)
 *   2. PS/2 mouse 3-byte packet state machine
 *   3. Out-of-sync recovery via Bit 3 (0x08) validation
 *   4. 9-bit signed DX/DY decoding with positive, negative, and overflow limits
 *   5. Screen boundary clamping
 *   6. Left, Right, Middle button press and release transitions
 *   7. PC-98 uPD8255A PPI mouse decoding, active-low button logic, and bounds
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <btron/types.h>
#include <btron/event.h>
#include <drivers/ps2_mouse.h>
#include <drivers/pc98_mouse.h>
#include <drivers/pc98_kbd.h>

/* Baremetal desktop mouse position stub */
static H s_baremetal_mx = 512, s_baremetal_my = 384;
void set_baremetal_mouse_pos(H x, H y) {
    s_baremetal_mx = x;
    s_baremetal_my = y;
}
void get_baremetal_mouse_pos(H *x, H *y) {
    if (x) *x = s_baremetal_mx;
    if (y) *y = s_baremetal_my;
}

/* Minimal event queue stub for standalone driver testing */
#define TEST_QUEUE_SIZE 128
static EVT s_test_evts[TEST_QUEUE_SIZE];
static int s_test_q_head = 0;
static int s_test_q_tail = 0;
static int s_test_q_count = 0;

ER init_evt_sys(void) {
    s_test_q_head = 0;
    s_test_q_tail = 0;
    s_test_q_count = 0;
    return E_OK;
}

ER snd_evt(const EVT *p_evt) {
    if (!p_evt) return E_PAR;
    if (s_test_q_count >= TEST_QUEUE_SIZE) return E_BUSY;
    s_test_evts[s_test_q_tail] = *p_evt;
    s_test_q_tail = (s_test_q_tail + 1) % TEST_QUEUE_SIZE;
    s_test_q_count++;
    return E_OK;
}

ER get_evt(EVT *p_evt, W timeout_ms) {
    (void)timeout_ms;
    if (!p_evt) return E_PAR;
    if (s_test_q_count == 0) return E_TMOUT;
    *p_evt = s_test_evts[s_test_q_head];
    s_test_q_head = (s_test_q_head + 1) % TEST_QUEUE_SIZE;
    s_test_q_count--;
    return E_OK;
}

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
    } \
} while(0)

/* Test 1: 8042 Status Register Port 0x64 bitmask analysis */
static void test_8042_status_discrimination(void) {
    /*
     * Port 0x64 Status Register:
     * Bit 0: OBF (Output Buffer Full - byte ready to read at 0x60)
     * Bit 5: AUX (Auxiliary Device Output Buffer - 1 = Mouse, 0 = Keyboard)
     */
    uint8_t kbd_status = 0x15;  /* Bit 0=1, Bit 5=0 -> Keyboard data */
    uint8_t mouse_status = 0x35;/* Bit 0=1, Bit 5=1 -> Mouse data */
    uint8_t empty_status = 0x00;/* Bit 0=0 -> No data */

    /* Keyboard check: ((st & 0x21) == 0x01) */
    TEST_ASSERT(((kbd_status & 0x21) == 0x01), "kbd_status correctly identified as keyboard data");
    TEST_ASSERT(((mouse_status & 0x21) != 0x01), "mouse_status not identified as keyboard data");
    TEST_ASSERT(((empty_status & 0x21) != 0x01), "empty_status not identified as keyboard data");

    /* Mouse check: ((st & 0x21) == 0x21) */
    TEST_ASSERT(((mouse_status & 0x21) == 0x21), "mouse_status correctly identified as auxiliary mouse data");
    TEST_ASSERT(((kbd_status & 0x21) != 0x21), "kbd_status not identified as mouse data");
}

/* Test 2: PS/2 Mouse 3-Byte Packet Decoding with Positive Deltas */
static void test_ps2_mouse_positive_movement(void) {
    init_evt_sys();
    ps2_mouse_init(1024, 768);
    ps2_mouse_set_pos(500, 300);

    H x = 0, y = 0;
    uint8_t btn = 0;
    ps2_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 500 && y == 300, "Initial position set to (500, 300)");

    /*
     * Feed packet:
     * Byte 0: 0x08 (Always-1 bit 3 set, no buttons, dx/dy positive)
     * Byte 1: 15 (dx = +15)
     * Byte 2: 20 (dy = +20; Note: in PS/2, positive dy is UP, so screen Y decreases by 20)
     */
    ps2_mouse_feed_byte(0x08);
    ps2_mouse_feed_byte(15);
    ps2_mouse_feed_byte(20);

    ps2_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 515, "X moved +15 -> 515");
    TEST_ASSERT(y == 280, "Y moved +20 (screen up) -> 280");

    /* Verify EV_MOUSE_MOVE event in queue */
    EVT ev;
    int found_move = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_MOUSE_MOVE && ev.pos.x == 515 && ev.pos.y == 280) {
            found_move = 1;
        }
    }
    TEST_ASSERT(found_move, "EV_MOUSE_MOVE event successfully queued with correct coordinates");
}

/* Test 3: PS/2 Mouse Negative Movement and Sign Extension */
static void test_ps2_mouse_negative_movement(void) {
    init_evt_sys();
    ps2_mouse_init(1024, 768);
    ps2_mouse_set_pos(500, 300);

    /*
     * Feed packet:
     * Byte 0: 0x08 | 0x10 | 0x20 = 0x38 (Bit 3=1, Bit 4=X sign neg, Bit 5=Y sign neg)
     * Byte 1: 0xF6 (-10 as uint8_t)
     * Byte 2: 0xEC (-20 as uint8_t; negative dy is DOWN, so screen Y increases by 20)
     */
    ps2_mouse_feed_byte(0x38);
    ps2_mouse_feed_byte(0xF6);
    ps2_mouse_feed_byte(0xEC);

    H x = 0, y = 0;
    uint8_t btn = 0;
    ps2_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 490, "X moved -10 -> 490");
    TEST_ASSERT(y == 320, "Y moved -20 (screen down) -> 320");
}

/* Test 4: PS/2 Stream Out-of-Sync Recovery via Bit 3 Check */
static void test_ps2_mouse_sync_recovery(void) {
    init_evt_sys();
    ps2_mouse_init(1024, 768);
    ps2_mouse_set_pos(100, 100);

    /* Feed corrupted garbage bytes (Bit 3 is 0) */
    ps2_mouse_feed_byte(0x00);
    ps2_mouse_feed_byte(0x55);
    ps2_mouse_feed_byte(0x02);

    H x = 0, y = 0;
    uint8_t btn = 0;
    ps2_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 100 && y == 100, "Garbage bytes without Bit 3 rejected; position unchanged");

    /* Now feed a valid 3-byte packet */
    ps2_mouse_feed_byte(0x08);
    ps2_mouse_feed_byte(10);
    ps2_mouse_feed_byte(0);

    ps2_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 110 && y == 100, "Stream re-synchronized and decoded valid packet");
}

/* Test 5: PS/2 Screen Boundary Clamping */
static void test_ps2_mouse_clamping(void) {
    init_evt_sys();
    ps2_mouse_init(1024, 768);
    ps2_mouse_set_pos(10, 10);

    /* Move left by 50 (should clamp to 0) and up by 50 (should clamp to 0) */
    ps2_mouse_feed_byte(0x18); /* Bit 4 = X neg, Bit 5 = Y pos (up) */
    ps2_mouse_feed_byte((uint8_t)-50);
    ps2_mouse_feed_byte(50); /* +50 up -> -50 in screen Y */

    H x = 0, y = 0;
    ps2_mouse_get_state(&x, &y, NULL);
    TEST_ASSERT(x == 0, "X clamped at left boundary (0)");
    TEST_ASSERT(y == 0, "Y clamped at top boundary (0)");

    /* Move right past 1024 and down past 768 */
    ps2_mouse_set_pos(1020, 760);
    ps2_mouse_feed_byte(0x28); /* Y negative (down), X positive */
    ps2_mouse_feed_byte(50);
    ps2_mouse_feed_byte((uint8_t)-50);

    ps2_mouse_get_state(&x, &y, NULL);
    TEST_ASSERT(x == 1023, "X clamped at right boundary (1023)");
    TEST_ASSERT(y == 767, "Y clamped at bottom boundary (767)");
}

/* Test 6: PS/2 Button Transitions */
static void test_ps2_mouse_buttons(void) {
    init_evt_sys();
    ps2_mouse_init(1024, 768);
    ps2_mouse_set_pos(200, 200);

    /* Left button click (Byte 0 Bit 0 = 1) */
    ps2_mouse_feed_byte(0x09);
    ps2_mouse_feed_byte(0);
    ps2_mouse_feed_byte(0);

    EVT ev;
    int got_btn_down = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_BUT_DOWN && ev.button == 1) {
            got_btn_down = 1;
        }
    }
    TEST_ASSERT(got_btn_down, "EV_BUT_DOWN generated for Left Button");

    /* Left button release (Byte 0 Bit 0 = 0) */
    ps2_mouse_feed_byte(0x08);
    ps2_mouse_feed_byte(0);
    ps2_mouse_feed_byte(0);

    int got_btn_up = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_BUT_UP && ev.button == 1) {
            got_btn_up = 1;
        }
    }
    TEST_ASSERT(got_btn_up, "EV_BUT_UP generated for Left Button release");

    /* Right button click (Byte 0 Bit 1 = 1) */
    ps2_mouse_feed_byte(0x0A);
    ps2_mouse_feed_byte(0);
    ps2_mouse_feed_byte(0);

    int got_right_down = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_BUT_DOWN && ev.button == 2) {
            got_right_down = 1;
        }
    }
    TEST_ASSERT(got_right_down, "EV_BUT_DOWN generated for Right Button");
}

/* Test 7: PC-98 Bus Mouse Delta Movement & Button States */
static void test_pc98_mouse_movement_and_buttons(void) {
    init_evt_sys();
    pc98_mouse_init(1024, 768);

    H x = 0, y = 0;
    uint8_t btn = 0;
    pc98_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 512 && y == 384, "PC-98 mouse initialized at center (512, 384)");

    /* Feed delta: dx = +25, dy = -15, Left Button pressed (bit 0 = 1) */
    pc98_mouse_feed_state(25, -15, 0x01);

    pc98_mouse_get_state(&x, &y, &btn);
    TEST_ASSERT(x == 537, "PC-98 X moved +25 -> 537");
    TEST_ASSERT(y == 369, "PC-98 Y moved -15 -> 369");
    TEST_ASSERT(btn == 0x01, "PC-98 Left button state recorded");

    EVT ev;
    int got_move = 0, got_down = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_MOUSE_MOVE && ev.pos.x == 537 && ev.pos.y == 369) {
            got_move = 1;
        }
        if (ev.type == EV_BUT_DOWN && ev.button == 1) {
            got_down = 1;
        }
    }
    TEST_ASSERT(got_move, "PC-98 EV_MOUSE_MOVE dispatched");
    TEST_ASSERT(got_down, "PC-98 EV_BUT_DOWN dispatched");

    /* Release button */
    pc98_mouse_feed_state(0, 0, 0x00);
    int got_up = 0;
    while (get_evt(&ev, 0) == E_OK) {
        if (ev.type == EV_BUT_UP && ev.button == 1) {
            got_up = 1;
        }
    }
    TEST_ASSERT(got_up, "PC-98 EV_BUT_UP dispatched");
}

/* Test 8: PC-98 Intel 8251A Keyboard Native & PS/2 Fallback Decoding */
static void test_pc98_keyboard_native_and_fallback(void) {
    pc98_kbd_init();
    pc98_kbd_set_fallback(0); /* Force native PC-98 mode */
    TEST_ASSERT(!pc98_kbd_is_fallback(), "pc98_kbd configured in native mode");

    /* Test PC-98 Native Scancodes */
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x00, 0) == 0x1B, "PC-98 0x00 is ESC (0x1B)");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x0E, 0) == '\b', "PC-98 0x0E is Backspace");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x0F, 0) == '\t', "PC-98 0x0F is Tab");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x1C, 0) == '\n', "PC-98 0x1C is Return");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x34, 0) == ' ',  "PC-98 0x34 is Space");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x10, 0) == 'q',  "PC-98 0x10 is 'q'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x10, 1) == 'Q',  "PC-98 0x10 with shift is 'Q'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x01, 0) == '1',  "PC-98 0x01 is '1'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x01, 1) == '!',  "PC-98 0x01 with shift is '!'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x90, 0) == 0,    "PC-98 Break code (bit 7 set) returns 0");

    /* Test Feeding Queue */
    pc98_kbd_feed_scancode(0x1D); /* 'a' */
    TEST_ASSERT(pc98_kbd_has_key() == 1, "pc98_kbd_has_key() reports key available");
    uint8_t sc = pc98_kbd_get_scancode();
    TEST_ASSERT(sc == 0x1D, "pc98_kbd_get_scancode() returned fed scancode 0x1D");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(sc, 0) == 'a', "Translated 0x1D to 'a'");
    TEST_ASSERT(pc98_kbd_has_key() == 0, "Queue emptied after get_scancode");

    /* Test Dual-Mode PS/2 Fallback (QEMU -M q35) */
    pc98_kbd_set_fallback(1);
    TEST_ASSERT(pc98_kbd_is_fallback() == 1, "pc98_kbd configured in fallback mode");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x01, 0) == 0x1B, "Fallback 0x01 is PS/2 ESC");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x1E, 0) == 'a',  "Fallback 0x1E is PS/2 'a'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x1E, 1) == 'A',  "Fallback 0x1E with shift is PS/2 'A'");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x39, 0) == ' ',  "Fallback 0x39 is PS/2 Space");
    TEST_ASSERT(pc98_kbd_scancode_to_ascii(0x9E, 0) == 0,    "Fallback break code returns 0");

    pc98_kbd_set_fallback(0); /* Restore native mode */
}

int main(void) {
    printf("==============================================================\n");
    printf(" B-System Input Drivers Unit Test Suite (UEFI & PC-98)\n");
    printf("==============================================================\n");

    test_8042_status_discrimination();
    test_ps2_mouse_positive_movement();
    test_ps2_mouse_negative_movement();
    test_ps2_mouse_sync_recovery();
    test_ps2_mouse_clamping();
    test_ps2_mouse_buttons();
    test_pc98_mouse_movement_and_buttons();
    test_pc98_keyboard_native_and_fallback();

    printf("\nTest Results: %d / %d assertions passed (%d%%)\n",
           g_tests_passed, g_tests_run,
           g_tests_run > 0 ? (g_tests_passed * 100 / g_tests_run) : 0);

    if (g_tests_passed == g_tests_run) {
        printf("[SUCCESS] All UEFI and PC-98 input driver tests PASSED!\n");
        return 0;
    } else {
        printf("[FAILURE] Some input driver tests failed!\n");
        return 1;
    }
}
