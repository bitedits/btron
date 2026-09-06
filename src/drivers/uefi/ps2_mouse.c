/*
 * ps2_mouse.c — PS/2 Auxiliary Mouse Driver for B-System Baremetal & UEFI
 *
 * Dedicated in honor of Kota Uchida (内田 公太, author of MikanOS).
 *
 * Provides:
 *   - 8042 controller auxiliary port initialization and streaming enable (0xF4)
 *   - Isolation of mouse packets from keyboard scancodes via status bit 5 (0x20)
 *   - 3-byte packet assembly with bit 3 (0x08) synchronization validation
 *   - 9-bit signed delta DX/DY decoding with screen boundary clamping
 *   - Button press/release transition detection and BTRON event generation
 *
 * Compliant with NASA JPL Rule 3: All buffers are static, zero dynamic allocation.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <btron/event.h>
#include <btron/desktop.h>
#include <drivers/ps2_mouse.h>

/* Port I/O definitions */
#if defined(__x86_64__) || defined(__i386__)
static inline void __attribute__((unused)) ps2_raw_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t __attribute__((unused)) ps2_raw_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
/* Fallback / mock stubs for host compilation and testing */
static inline void __attribute__((unused)) ps2_raw_outb(uint16_t port, uint8_t val) {
    (void)port; (void)val;
}
static inline uint8_t __attribute__((unused)) ps2_raw_inb(uint16_t port) {
    (void)port; return 0;
}
#endif

#define PS2_DATA_PORT   0x60
#define PS2_STATUS_PORT 0x64
#define PS2_CMD_PORT    0x64

#define PS2_STATUS_OBF  0x01 /* Output buffer full */
#define PS2_STATUS_IBF  0x02 /* Input buffer full */
#define PS2_STATUS_AUX  0x20 /* Auxiliary device (mouse) output buffer */

#define PS2_MAX_TIMEOUT 100000

#if defined(__x86_64__) || defined(__i386__)
/* Bounded wait for input buffer to clear */
static int ps2_wait_write(void) {
    for (int i = 0; i < PS2_MAX_TIMEOUT; i++) {
        if ((ps2_raw_inb(PS2_STATUS_PORT) & PS2_STATUS_IBF) == 0) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/* Bounded wait for output buffer to fill */
static int ps2_wait_read(void) {
    for (int i = 0; i < PS2_MAX_TIMEOUT; i++) {
        if ((ps2_raw_inb(PS2_STATUS_PORT) & PS2_STATUS_OBF) != 0) {
            return 0;
        }
        __asm__ volatile("pause");
    }
    return -1;
}

/* Send a command specifically to the auxiliary device (mouse) */
static void ps2_write_mouse(uint8_t val) {
    ps2_wait_write();
    ps2_raw_outb(PS2_CMD_PORT, 0xD4);
    ps2_wait_write();
    ps2_raw_outb(PS2_DATA_PORT, val);
}

/* Read a byte from data port */
static uint8_t ps2_read_data(void) {
    ps2_wait_read();
    return ps2_raw_inb(PS2_DATA_PORT);
}
#endif

/* Driver state (Static Bounded Allocation) */
static H s_mouse_x = 512;
static H s_mouse_y = 384;
static H s_bounds_w = 1024;
static H s_bounds_h = 768;

static uint8_t s_packet[3];
static uint8_t s_pkt_idx = 0;
static uint8_t s_prev_btn = 0;

void ps2_mouse_set_bounds(H max_w, H max_h) {
    if (max_w > 0) s_bounds_w = max_w;
    if (max_h > 0) s_bounds_h = max_h;
}

void ps2_mouse_set_pos(H x, H y) {
    if (x < 0) x = 0;
    if (x >= s_bounds_w) x = s_bounds_w - 1;
    if (y < 0) y = 0;
    if (y >= s_bounds_h) y = s_bounds_h - 1;
    s_mouse_x = x;
    s_mouse_y = y;
    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);
}

void ps2_mouse_get_state(H *out_x, H *out_y, uint8_t *out_buttons) {
    if (out_x) *out_x = s_mouse_x;
    if (out_y) *out_y = s_mouse_y;
    if (out_buttons) *out_buttons = s_prev_btn;
}

void ps2_mouse_feed_byte(uint8_t b) {
    /* Byte 0 verification: Bit 3 must be 1 in standard PS/2 mouse packets */
    if (s_pkt_idx == 0) {
        if ((b & 0x08) == 0) {
            /* Out of synchronization, discard byte to realign with stream */
            return;
        }
        s_packet[0] = b;
        s_pkt_idx = 1;
        return;
    }

    if (s_pkt_idx == 1) {
        s_packet[1] = b;
        s_pkt_idx = 2;
        return;
    }

    if (s_pkt_idx == 2) {
        s_packet[2] = b;
        s_pkt_idx = 0; /* Reset index for next packet */

        /* Check overflow bits (Byte 0: Bit 6=X overflow, Bit 7=Y overflow) */
        if (s_packet[0] & 0xC0) {
            /* Overflow occurred, ignore movement to avoid erratic jumps */
            return;
        }

        /* Decode 9-bit signed relative movements */
        int16_t dx = (s_packet[0] & 0x10) ? (int16_t)((int16_t)s_packet[1] | 0xFF00) : (int16_t)s_packet[1];
        int16_t dy = (s_packet[0] & 0x20) ? (int16_t)((int16_t)s_packet[2] | 0xFF00) : (int16_t)s_packet[2];

        /* Update cursor position (PS/2 Y+ is up, screen coordinate Y+ is down) */
        s_mouse_x += dx;
        s_mouse_y -= dy;

        /* Clamp to screen boundaries */
        if (s_mouse_x < 0) s_mouse_x = 0;
        if (s_mouse_x >= s_bounds_w) s_mouse_x = s_bounds_w - 1;
        if (s_mouse_y < 0) s_mouse_y = 0;
        if (s_mouse_y >= s_bounds_h) s_mouse_y = s_bounds_h - 1;

        set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);

        /* Emit EV_MOUSE_MOVE if moved */
        if (dx != 0 || dy != 0) {
            EVT ev;
            ev.type = EV_MOUSE_MOVE;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 0;
            ev.data = 0;
            snd_evt(&ev);
        }

        /* Button transition detection */
        uint8_t cur_btn = s_packet[0] & 0x07; /* Bit 0: Left, Bit 1: Right, Bit 2: Middle */

        /* Left Button (Button 1) */
        if ((cur_btn & 0x01) && !(s_prev_btn & 0x01)) {
            EVT ev;
            ev.type = EV_BUT_DOWN;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 1;
            ev.data = 0;
            snd_evt(&ev);
        } else if (!(cur_btn & 0x01) && (s_prev_btn & 0x01)) {
            EVT ev;
            ev.type = EV_BUT_UP;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 1;
            ev.data = 0;
            snd_evt(&ev);
        }

        /* Right Button (Button 2) */
        if ((cur_btn & 0x02) && !(s_prev_btn & 0x02)) {
            EVT ev;
            ev.type = EV_BUT_DOWN;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 2;
            ev.data = 0;
            snd_evt(&ev);
        } else if (!(cur_btn & 0x02) && (s_prev_btn & 0x02)) {
            EVT ev;
            ev.type = EV_BUT_UP;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 2;
            ev.data = 0;
            snd_evt(&ev);
        }

        /* Middle Button (Button 3) */
        if ((cur_btn & 0x04) && !(s_prev_btn & 0x04)) {
            EVT ev;
            ev.type = EV_BUT_DOWN;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 3;
            ev.data = 0;
            snd_evt(&ev);
        } else if (!(cur_btn & 0x04) && (s_prev_btn & 0x04)) {
            EVT ev;
            ev.type = EV_BUT_UP;
            ev.wndid = 0;
            ev.pos.x = s_mouse_x;
            ev.pos.y = s_mouse_y;
            ev.key = 0;
            ev.button = 3;
            ev.data = 0;
            snd_evt(&ev);
        }

        s_prev_btn = cur_btn;
    }
}

void ps2_mouse_init(H screen_w, H screen_h) {
    ps2_mouse_set_bounds(screen_w, screen_h);
    s_mouse_x = screen_w / 2;
    s_mouse_y = screen_h / 2;
    s_pkt_idx = 0;
    s_prev_btn = 0;
    set_baremetal_mouse_pos(s_mouse_x, s_mouse_y);

#if defined(__x86_64__) || defined(__i386__)
    /* Drain output buffer */
    while (ps2_raw_inb(PS2_STATUS_PORT) & PS2_STATUS_OBF) {
        (void)ps2_raw_inb(PS2_DATA_PORT);
    }

    /* Enable PS/2 Auxiliary Port (Command 0xA8) */
    ps2_wait_write();
    ps2_raw_outb(PS2_CMD_PORT, 0xA8);

    /* Read Controller Configuration Byte (Command 0x20) */
    ps2_wait_write();
    ps2_raw_outb(PS2_CMD_PORT, 0x20);
    uint8_t cfg = ps2_read_data();

    /* Enable IRQ 12 (aux interrupt) and enable clock (clear bit 5) */
    cfg |= 0x02;  /* Enable IRQ 12 */
    cfg &= ~0x20; /* Enable Aux clock (disable aux clock disabled bit) */

    /* Write modified Controller Configuration Byte (Command 0x60) */
    ps2_wait_write();
    ps2_raw_outb(PS2_CMD_PORT, 0x60);
    ps2_wait_write();
    ps2_raw_outb(PS2_DATA_PORT, cfg);

    /* Reset mouse (Command 0xFF) */
    ps2_write_mouse(0xFF);
    (void)ps2_read_data(); /* ACK 0xFA */
    (void)ps2_read_data(); /* 0xAA (Self-test passed) */
    (void)ps2_read_data(); /* 0x00 (Mouse ID) */

    /* Enable Streaming Mode / Data Reporting (Command 0xF4) */
    ps2_write_mouse(0xF4);
    (void)ps2_read_data(); /* ACK 0xFA */

    /* Final drain of buffer */
    while (ps2_raw_inb(PS2_STATUS_PORT) & PS2_STATUS_OBF) {
        (void)ps2_raw_inb(PS2_DATA_PORT);
    }
#endif
}

void ps2_mouse_poll(void) {
#if defined(__x86_64__) || defined(__i386__)
    /* Process all available bytes tagged as auxiliary data */
    while ((ps2_raw_inb(PS2_STATUS_PORT) & (PS2_STATUS_OBF | PS2_STATUS_AUX)) == (PS2_STATUS_OBF | PS2_STATUS_AUX)) {
        uint8_t b = ps2_raw_inb(PS2_DATA_PORT);
        ps2_mouse_feed_byte(b);
    }
#endif
}
