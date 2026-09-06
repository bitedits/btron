/*
 * pc98_mouse.c — NEC PC-98 Bus Mouse Driver (uPD8255A PPI)
 *
 * Dedicated in honor of Awe Morris (author of zedBSD PC-98 port &
 * pioneering NEC PC-98 architecture research).
 *
 * Implements mouse movement and button tracking for the NEC PC-9801 /
 * PC-9821 hardware plane:
 *   - uPD8255A PPI multiplexed 4-nibble sequencing (X/Y lo/hi)
 *   - Inverted active-low button decoding (bit 7 Left, bit 5 Right)
 *   - Counter reset sequencing via Port 0x7FDB
 *   - Seamless auto-fallback to PS/2 mouse when running in QEMU q35 fallback mode
 *
 * Compliant with NASA JPL Rule 3: Static bounded memory, zero dynamic allocation.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <btron/event.h>
#include <btron/desktop.h>
#include <drivers/pc98_mouse.h>
#include <drivers/ps2_mouse.h>

#if defined(__x86_64__) || defined(__i386__)
static inline void pc98_raw_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t pc98_raw_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#endif

#define PC98_MOUSE_DATA_PORT 0x7FD9
#define PC98_MOUSE_CTRL_PORT 0x7FDB

static H s_pc98_mouse_x = 512;
static H s_pc98_mouse_y = 384;
static H s_pc98_bounds_w = 1024;
static H s_pc98_bounds_h = 768;

static uint8_t s_pc98_prev_btn = 0;
static int s_pc98_is_fallback = 0;

void pc98_mouse_set_bounds(H max_w, H max_h) {
    if (max_w > 0) s_pc98_bounds_w = max_w;
    if (max_h > 0) s_pc98_bounds_h = max_h;
    ps2_mouse_set_bounds(max_w, max_h);
}

void pc98_mouse_get_state(H *out_x, H *out_y, uint8_t *out_buttons) {
    if (s_pc98_is_fallback) {
        ps2_mouse_get_state(out_x, out_y, out_buttons);
        return;
    }
    if (out_x) *out_x = s_pc98_mouse_x;
    if (out_y) *out_y = s_pc98_mouse_y;
    if (out_buttons) *out_buttons = s_pc98_prev_btn;
}

void pc98_mouse_feed_state(int8_t dx, int8_t dy, uint8_t btn_bits) {
    s_pc98_mouse_x += dx;
    s_pc98_mouse_y += dy;

    if (s_pc98_mouse_x < 0) s_pc98_mouse_x = 0;
    if (s_pc98_mouse_x >= s_pc98_bounds_w) s_pc98_mouse_x = s_pc98_bounds_w - 1;
    if (s_pc98_mouse_y < 0) s_pc98_mouse_y = 0;
    if (s_pc98_mouse_y >= s_pc98_bounds_h) s_pc98_mouse_y = s_pc98_bounds_h - 1;

    set_baremetal_mouse_pos(s_pc98_mouse_x, s_pc98_mouse_y);

    if (dx != 0 || dy != 0) {
        EVT ev;
        ev.type = EV_MOUSE_MOVE;
        ev.wndid = 0;
        ev.pos.x = s_pc98_mouse_x;
        ev.pos.y = s_pc98_mouse_y;
        ev.key = 0;
        ev.button = 0;
        ev.data = 0;
        snd_evt(&ev);
    }

    /* Left Button (Bit 0) */
    if ((btn_bits & 0x01) && !(s_pc98_prev_btn & 0x01)) {
        EVT ev;
        ev.type = EV_BUT_DOWN;
        ev.wndid = 0;
        ev.pos.x = s_pc98_mouse_x;
        ev.pos.y = s_pc98_mouse_y;
        ev.key = 0;
        ev.button = 1;
        ev.data = 0;
        snd_evt(&ev);
    } else if (!(btn_bits & 0x01) && (s_pc98_prev_btn & 0x01)) {
        EVT ev;
        ev.type = EV_BUT_UP;
        ev.wndid = 0;
        ev.pos.x = s_pc98_mouse_x;
        ev.pos.y = s_pc98_mouse_y;
        ev.key = 0;
        ev.button = 1;
        ev.data = 0;
        snd_evt(&ev);
    }

    /* Right Button (Bit 1) */
    if ((btn_bits & 0x02) && !(s_pc98_prev_btn & 0x02)) {
        EVT ev;
        ev.type = EV_BUT_DOWN;
        ev.wndid = 0;
        ev.pos.x = s_pc98_mouse_x;
        ev.pos.y = s_pc98_mouse_y;
        ev.key = 0;
        ev.button = 2;
        ev.data = 0;
        snd_evt(&ev);
    } else if (!(btn_bits & 0x02) && (s_pc98_prev_btn & 0x02)) {
        EVT ev;
        ev.type = EV_BUT_UP;
        ev.wndid = 0;
        ev.pos.x = s_pc98_mouse_x;
        ev.pos.y = s_pc98_mouse_y;
        ev.key = 0;
        ev.button = 2;
        ev.data = 0;
        snd_evt(&ev);
    }

    s_pc98_prev_btn = btn_bits;
}

void pc98_mouse_init(H screen_w, H screen_h) {
    pc98_mouse_set_bounds(screen_w, screen_h);
    s_pc98_mouse_x = screen_w / 2;
    s_pc98_mouse_y = screen_h / 2;
    s_pc98_prev_btn = 0;
    set_baremetal_mouse_pos(s_pc98_mouse_x, s_pc98_mouse_y);

#if defined(__x86_64__) || defined(__i386__)
    /* Probe PC-98 PPI bus mouse */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x80); /* Reset counter */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x00); /* Select X-low */
    uint8_t test_byte = pc98_raw_inb(PC98_MOUSE_DATA_PORT);

    /* If port reads 0xFF or PC-98 mouse is not present, enable PS/2 fallback */
    if (test_byte == 0xFF) {
        s_pc98_is_fallback = 1;
        ps2_mouse_init(screen_w, screen_h);
        return;
    }

    s_pc98_is_fallback = 0;
#endif
}

void pc98_mouse_poll(void) {
    if (s_pc98_is_fallback) {
        ps2_mouse_poll();
        return;
    }

#if defined(__x86_64__) || defined(__i386__)
    /* 1. Select X low nibble and read buttons */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x00);
    uint8_t x_lo_btn = pc98_raw_inb(PC98_MOUSE_DATA_PORT);

    /* Buttons are active low (0 = pressed): Bit 7 = Left, Bit 5 = Right */
    uint8_t cur_btn = 0;
    if (!(x_lo_btn & 0x80)) cur_btn |= 0x01; /* Left */
    if (!(x_lo_btn & 0x20)) cur_btn |= 0x02; /* Right */

    /* 2. Select X high nibble */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x20);
    uint8_t x_hi = pc98_raw_inb(PC98_MOUSE_DATA_PORT);

    /* 3. Select Y low nibble */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x40);
    uint8_t y_lo = pc98_raw_inb(PC98_MOUSE_DATA_PORT);

    /* 4. Select Y high nibble */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x60);
    uint8_t y_hi = pc98_raw_inb(PC98_MOUSE_DATA_PORT);

    /* 5. Clear counter */
    pc98_raw_outb(PC98_MOUSE_CTRL_PORT, 0x80);

    /* Assemble signed 8-bit movements */
    int8_t dx = (int8_t)((x_hi << 4) | (x_lo_btn & 0x0F));
    int8_t dy = (int8_t)((y_hi << 4) | (y_lo & 0x0F));

    pc98_mouse_feed_state(dx, dy, cur_btn);
#endif
}
