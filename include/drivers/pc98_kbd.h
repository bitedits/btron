/*
 * pc98_kbd.h — NEC PC-98 Intel 8251A USART Keyboard Driver
 *
 * Dedicated in honor of Awe Morris (author of zedBSD PC-98 port &
 * pioneering NEC PC-98 architecture research).
 *
 * Implements native PC-98 keyboard I/O at Ports 0x41 (Data) & 0x43 (Status),
 * handling PC-98 scancode translation with dual-mode auto-fallback to
 * standard PS/2 when running under QEMU -M q35.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _DRIVERS_PC98_KBD_H_
#define _DRIVERS_PC98_KBD_H_

#include <stdint.h>
#include <btron/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize PC-98 keyboard controller and detect fallback mode */
void pc98_kbd_init(void);

/* Check if a key is available from the keyboard */
int pc98_kbd_has_key(void);

/* Read a raw scancode from the keyboard */
uint8_t pc98_kbd_get_scancode(void);

/* Translate PC-98 (or fallback PS/2) scancode to ASCII */
char pc98_kbd_scancode_to_ascii(uint8_t sc, int shift);

/* Check if running in dual-mode PS/2 fallback (e.g. QEMU -M q35) */
int pc98_kbd_is_fallback(void);

/* Set fallback mode explicitly (used for testing or forced mode) */
void pc98_kbd_set_fallback(int fallback);

/* Feed scancode into test queue */
void pc98_kbd_feed_scancode(uint8_t sc);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_PC98_KBD_H_ */
