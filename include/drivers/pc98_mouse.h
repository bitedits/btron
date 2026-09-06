/*
 * pc98_mouse.h — NEC PC-98 Bus Mouse Driver (uPD8255A PPI)
 *
 * Dedicated in honor of Awe Morris (author of zedBSD PC-98 port &
 * pioneering NEC PC-98 architecture research).
 *
 * Implements mouse movement and button tracking for the NEC PC-9801 /
 * PC-9821 hardware plane, with auto-fallback to PS/2 mouse when running
 * under generic QEMU fallback.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#ifndef _DRIVERS_PC98_MOUSE_H_
#define _DRIVERS_PC98_MOUSE_H_

#include <stdint.h>
#include <btron/types.h>
#include <btron/event.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize PC-98 bus mouse hardware and coordinate boundaries */
void pc98_mouse_init(H screen_w, H screen_h);

/* Poll NEC uPD8255A mouse registers or fallback PS/2 driver */
void pc98_mouse_poll(void);

/* Query current mouse position and button state */
void pc98_mouse_get_state(H *out_x, H *out_y, uint8_t *out_buttons);

/* Update bounds */
void pc98_mouse_set_bounds(H max_w, H max_h);

/* Directly feed delta movement and buttons (used for testing and simulated environments) */
void pc98_mouse_feed_state(int8_t dx, int8_t dy, uint8_t btn_bits);

#ifdef __cplusplus
}
#endif

#endif /* _DRIVERS_PC98_MOUSE_H_ */
