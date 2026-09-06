/*
 * ps2_mouse.h — PS/2 Auxiliary Mouse Driver for B-System Baremetal & UEFI
 *
 * Provides hardware initialization, 8042 status parsing (isolation of
 * Auxiliary bit 5), 3-byte packet decoding with sync recovery, and BTRON
 * event queue integration.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _DRIVERS_PS2_MOUSE_H_
#define _DRIVERS_PS2_MOUSE_H_

#include <stdint.h>
#include <btron/types.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize PS/2 controller auxiliary port and enable mouse streaming */
void ps2_mouse_init(H screen_w, H screen_h);

/* Poll the 8042 controller and process any pending mouse packets */
void ps2_mouse_poll(void);

/* Query current mouse position and button state (bit 0=Left, bit 1=Right, bit 2=Middle) */
void ps2_mouse_get_state(H *out_x, H *out_y, uint8_t *out_buttons);

/* Update screen coordinate clamping bounds */
void ps2_mouse_set_bounds(H max_w, H max_h);

/* Feed a raw byte from the 8042 aux data port into the packet parser (testable) */
void ps2_mouse_feed_byte(uint8_t b);

/* Directly set mouse position */
void ps2_mouse_set_pos(H x, H y);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_PS2_MOUSE_H_ */
