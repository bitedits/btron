/*
 * pc98_kbd.c — NEC PC-98 Intel 8251A USART Keyboard Driver
 *
 * Dedicated in honor of Awe Morris (author of zedBSD PC-98 port &
 * pioneering NEC PC-98 architecture research).
 *
 * Implements native PC-98 keyboard I/O:
 *   - Intel 8251A USART at Ports 0x41 (Data) and 0x43 (Status/Command)
 *   - Bit 1 (RxRDY, 0x02) detection for incoming scancodes
 *   - PC-98 authentic scancode decoding (ESC=0x00, BS=0x0E, Return=0x1C, Space=0x34)
 *   - Seamless dual-mode auto-fallback to PS/2 (Ports 0x60/0x64) under QEMU -M q35
 *
 * Compliant with NASA JPL Rule 3: Static memory, bounded operations.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdint.h>
#include <stddef.h>
#include <btron/types.h>
#include <drivers/pc98_kbd.h>

#if defined(__x86_64__) || defined(__i386__)
static inline void __attribute__((unused)) pc98_kbd_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t __attribute__((unused)) pc98_kbd_inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
#else
static inline void __attribute__((unused)) pc98_kbd_outb(uint16_t port, uint8_t val) {
    (void)port; (void)val;
}
static inline uint8_t __attribute__((unused)) pc98_kbd_inb(uint16_t port) {
    (void)port; return 0xFF;
}
#endif

#define PC98_KBD_DATA_PORT   0x41
#define PC98_KBD_STATUS_PORT 0x43
#define PC98_KBD_RXRDY       0x02

#define PS2_STATUS_PORT      0x64
#define PS2_DATA_PORT        0x60
#define PS2_STATUS_OBF       0x01
#define PS2_STATUS_AUX       0x20

static int s_fallback = 0;

/* Bounded test feeding queue */
#define KBD_TEST_QUEUE_SIZE 32
static uint8_t s_test_scancodes[KBD_TEST_QUEUE_SIZE];
static int s_test_head = 0;
static int s_test_tail = 0;
static int s_test_count = 0;

void pc98_kbd_feed_scancode(uint8_t sc) {
    if (s_test_count < KBD_TEST_QUEUE_SIZE) {
        s_test_scancodes[s_test_tail] = sc;
        s_test_tail = (s_test_tail + 1) % KBD_TEST_QUEUE_SIZE;
        s_test_count++;
    }
}

int pc98_kbd_is_fallback(void) {
    return s_fallback;
}

void pc98_kbd_set_fallback(int fallback) {
    s_fallback = fallback;
}

void pc98_kbd_init(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint8_t st64 = pc98_kbd_inb(PS2_STATUS_PORT);
    /* If Port 0x64 is not 0xFF, 8042 PS/2 controller is present (QEMU -M q35) */
    if (st64 != 0xFF) {
        s_fallback = 1;
        return;
    }
    s_fallback = 0;
#else
    s_fallback = 0;
#endif
}

int pc98_kbd_has_key(void) {
    if (s_test_count > 0) return 1;

#if defined(__x86_64__) || defined(__i386__)
    /* Check 8042 PS/2 controller first (QEMU -M q35 fallback) */
    uint8_t st64 = pc98_kbd_inb(PS2_STATUS_PORT);
    if (st64 != 0xFF) {
        if ((st64 & (PS2_STATUS_OBF | PS2_STATUS_AUX)) == PS2_STATUS_OBF) {
            s_fallback = 1;
            return 1;
        }
        return 0;
    }

    /* Native PC-98: Port 0x64 returned 0xFF (no 8042 controller on bus) */
    uint8_t st43 = pc98_kbd_inb(PC98_KBD_STATUS_PORT);
    if (st43 != 0xFF && (st43 & PC98_KBD_RXRDY)) {
        s_fallback = 0;
        return 1;
    }
    return 0;
#else
    return 0;
#endif
}

uint8_t pc98_kbd_get_scancode(void) {
    if (s_test_count > 0) {
        uint8_t sc = s_test_scancodes[s_test_head];
        s_test_head = (s_test_head + 1) % KBD_TEST_QUEUE_SIZE;
        s_test_count--;
        return sc;
    }

#if defined(__x86_64__) || defined(__i386__)
    if (s_fallback) {
        return pc98_kbd_inb(PS2_DATA_PORT);
    }
    return pc98_kbd_inb(PC98_KBD_DATA_PORT);
#else
    return 0;
#endif
}

/* Standard IBM PC PS/2 Scancode Set 1 Translation (for QEMU fallback) */
static char ps2_fallback_to_ascii(uint8_t sc, int shift) {
    if (sc & 0x80) return 0;
    switch (sc) {
        case 0x1E: return shift ? 'A' : 'a';
        case 0x30: return shift ? 'B' : 'b';
        case 0x2E: return shift ? 'C' : 'c';
        case 0x20: return shift ? 'D' : 'd';
        case 0x12: return shift ? 'E' : 'e';
        case 0x21: return shift ? 'F' : 'f';
        case 0x22: return shift ? 'G' : 'g';
        case 0x23: return shift ? 'H' : 'h';
        case 0x17: return shift ? 'I' : 'i';
        case 0x24: return shift ? 'J' : 'j';
        case 0x25: return shift ? 'K' : 'k';
        case 0x26: return shift ? 'L' : 'l';
        case 0x32: return shift ? 'M' : 'm';
        case 0x31: return shift ? 'N' : 'n';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x10: return shift ? 'Q' : 'q';
        case 0x13: return shift ? 'R' : 'r';
        case 0x1F: return shift ? 'S' : 's';
        case 0x14: return shift ? 'T' : 't';
        case 0x16: return shift ? 'U' : 'u';
        case 0x2F: return shift ? 'V' : 'v';
        case 0x11: return shift ? 'W' : 'w';
        case 0x2D: return shift ? 'X' : 'x';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x2C: return shift ? 'Z' : 'z';
        case 0x02: return shift ? '!' : '1';
        case 0x03: return shift ? '@' : '2';
        case 0x04: return shift ? '#' : '3';
        case 0x05: return shift ? '$' : '4';
        case 0x06: return shift ? '%' : '5';
        case 0x07: return shift ? '^' : '6';
        case 0x08: return shift ? '&' : '7';
        case 0x09: return shift ? '*' : '8';
        case 0x0A: return shift ? '(' : '9';
        case 0x0B: return shift ? ')' : '0';
        case 0x0C: return shift ? '_' : '-';
        case 0x0D: return shift ? '+' : '=';
        case 0x1A: return shift ? '{' : '[';
        case 0x1B: return shift ? '}' : ']';
        case 0x27: return shift ? ':' : ';';
        case 0x28: return shift ? '"' : '\'';
        case 0x29: return shift ? '~' : '`';
        case 0x2B: return shift ? '|' : '\\';
        case 0x33: return shift ? '<' : ',';
        case 0x34: return shift ? '>' : '.';
        case 0x35: return shift ? '?' : '/';
        case 0x0F: return '\t';
        case 0x39: return ' ';
        case 0x1C: return '\n';
        case 0x0E: return '\b';
        case 0x01: return 0x1B; /* ESC */
        default: return 0;
    }
}

/* Authentic NEC PC-98 Scancode Translation */
static char pc98_native_to_ascii(uint8_t sc, int shift) {
    if (sc & 0x80) return 0; /* Key Release */

    switch (sc) {
        case 0x00: return 0x1B; /* ESC */
        case 0x01: return shift ? '!' : '1';
        case 0x02: return shift ? '"' : '2';
        case 0x03: return shift ? '#' : '3';
        case 0x04: return shift ? '$' : '4';
        case 0x05: return shift ? '%' : '5';
        case 0x06: return shift ? '&' : '6';
        case 0x07: return shift ? '\'' : '7';
        case 0x08: return shift ? '(' : '8';
        case 0x09: return shift ? ')' : '9';
        case 0x0A: return shift ? ' ' : '0';
        case 0x0B: return shift ? '=' : '-';
        case 0x0C: return shift ? '`' : '^';
        case 0x0D: return shift ? '|' : '\\';
        case 0x0E: return '\b'; /* Backspace */
        case 0x0F: return '\t'; /* Tab */

        case 0x10: return shift ? 'Q' : 'q';
        case 0x11: return shift ? 'W' : 'w';
        case 0x12: return shift ? 'E' : 'e';
        case 0x13: return shift ? 'R' : 'r';
        case 0x14: return shift ? 'T' : 't';
        case 0x15: return shift ? 'Y' : 'y';
        case 0x16: return shift ? 'U' : 'u';
        case 0x17: return shift ? 'I' : 'i';
        case 0x18: return shift ? 'O' : 'o';
        case 0x19: return shift ? 'P' : 'p';
        case 0x1A: return shift ? '~' : '@';
        case 0x1B: return shift ? '{' : '[';
        case 0x1C: return '\n'; /* Return */

        case 0x1D: return shift ? 'A' : 'a';
        case 0x1E: return shift ? 'S' : 's';
        case 0x1F: return shift ? 'D' : 'd';
        case 0x20: return shift ? 'F' : 'f';
        case 0x21: return shift ? 'G' : 'g';
        case 0x22: return shift ? 'H' : 'h';
        case 0x23: return shift ? 'J' : 'j';
        case 0x24: return shift ? 'K' : 'k';
        case 0x25: return shift ? 'L' : 'l';
        case 0x26: return shift ? '+' : ';';
        case 0x27: return shift ? '*' : ':';
        case 0x28: return shift ? '}' : ']';

        case 0x29: return shift ? 'Z' : 'z';
        case 0x2A: return shift ? 'X' : 'x';
        case 0x2B: return shift ? 'C' : 'c';
        case 0x2C: return shift ? 'V' : 'v';
        case 0x2D: return shift ? 'B' : 'b';
        case 0x2E: return shift ? 'N' : 'n';
        case 0x2F: return shift ? 'M' : 'm';
        case 0x30: return shift ? '<' : ',';
        case 0x31: return shift ? '>' : '.';
        case 0x32: return shift ? '?' : '/';
        case 0x33: return shift ? '_' : '\\';
        case 0x34: return ' '; /* Space */

        default: return 0;
    }
}

char pc98_kbd_scancode_to_ascii(uint8_t sc, int shift) {
    if (s_fallback) {
        return ps2_fallback_to_ascii(sc, shift);
    }
    return pc98_native_to_ascii(sc, shift);
}
