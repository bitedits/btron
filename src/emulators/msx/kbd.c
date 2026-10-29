/*
 * tronMSX — keyboard matrix and joystick levels.  B-MSX Rev 1.05, §2.5.
 * Clean-room C99.
 *
 * The MSX1 keyboard is a strobe matrix: the host selects a row on PPI port C
 * (0xAA, low nibble) and reads the eight column bits back on PPI port B
 * (0xA9), active low — a pressed key clears its bit.  This unit owns only the
 * level state; debouncing and host-key mapping belong to the window layer.
 *
 * The two joystick sockets are not on the PPI: their direction and trigger
 * pins are multiplexed onto PSG port A (register 14), with PSG register 15
 * bit 6 selecting the socket.  psg.c reads them back through kbd_joy_pins(),
 * so the levels live here next to the matrix and are published the same way —
 * as levels, read once per step (D6 of §8.1).
 */

#include "msx_int.h"

#include <string.h>

#define KBD_COLS 8u                      /* eight column bits per row       */

/* Bit set (1) = released, cleared (0) = pressed.  Idle is all-released. */
static uint8_t s_matrix[MSX_INPUT_ROWS];

/* Pressed-level bits per socket in the MSX_JOY_* layout of tronmsx.h. */
static uint8_t s_joy[2];

/* Proportional host mouse state in Screen 7 space (512x212). */
static uint16_t s_mouse_x = 256u;
static uint8_t  s_mouse_y = 106u;
static uint8_t  s_mouse_buttons = 0u;
static uint8_t  s_mouse_active = 0u;

void kbd_init(void)
{
    kbd_reset();
}

void kbd_reset(void)
{
    memset(s_matrix, 0xFF, sizeof(s_matrix));   /* nothing pressed at power-up */
    s_joy[0] = 0u;
    s_joy[1] = 0u;
    s_mouse_x = 256u;
    s_mouse_y = 106u;
    s_mouse_buttons = 0u;
    s_mouse_active = 0u;
}

void kbd_down(uint8_t row, uint8_t col)
{
    if (row < MSX_INPUT_ROWS && col < KBD_COLS)
        s_matrix[row] = (uint8_t)(s_matrix[row] & ~(1u << col));
}

void kbd_up(uint8_t row, uint8_t col)
{
    if (row < MSX_INPUT_ROWS && col < KBD_COLS)
        s_matrix[row] = (uint8_t)(s_matrix[row] | (1u << col));
}

uint8_t kbd_row_bits(uint8_t row)
{
    return (row < MSX_INPUT_ROWS) ? s_matrix[row] : 0xFFu;
}

void kbd_set_joy(uint8_t bits_a, uint8_t bits_b)
{
    s_joy[0] = bits_a;
    s_joy[1] = bits_b;
}

void kbd_set_mouse(uint16_t x, uint16_t y, uint8_t buttons)
{
    if (x > 511u) x = 511u;
    if (y > 211u) y = 211u;
    s_mouse_x = x;
    s_mouse_y = (uint8_t)y;
    s_mouse_buttons = (uint8_t)(buttons & 0x03u);
    s_mouse_active = 1u;
}

uint8_t kbd_mouse_in(uint8_t reg)
{
    if (!s_mouse_active) return 0xFFu;
    switch (reg) {
    case 0: return (uint8_t)(s_mouse_x & 0xFFu);
    case 1: return (uint8_t)(((s_mouse_x >> 8) & 0x01u) |
                             ((s_mouse_buttons & 0x03u) << 1) |
                             0x80u);
    case 2: return s_mouse_y;
    default: return 0xFFu;
    }
}

uint8_t kbd_joy_pins(uint8_t sel)
{
    /* Return the six pin levels the PSG publishes on port A: a pressed pin
     * reads 0, idle reads 1.  psg.c masks this to bits 0-5 and forces 6-7
     * high, so only the MSX_JOY_* bits under 0x40 matter. */
    uint8_t pressed = (uint8_t)(s_joy[sel ? 1u : 0u] & 0x3Fu);
    return (uint8_t)(~pressed & 0x3Fu);
}

void kbd_snapshot(uint8_t *out)
{
    if (out != 0) memcpy(out, s_matrix, MSX_INPUT_ROWS);
}
