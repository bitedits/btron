/*
 * tronMSX — 8255 PPI at #A8-#AB (and the #10-#13 alias family).  B-MSX
 * Rev 1.05, §2.5.  Clean-room C99.
 *
 * Port map, as the MSX standard wires it and C-BIOS drives it:
 *   #A8 port A  primary slot-select register (write); reads back the latch
 *   #A9 port B  keyboard column data (input, active low) for the row on C
 *   #AA port C  low nibble = keyboard row select (output); high = cassette
 *   #AB control mode word (C-BIOS writes 0x82), or 8255 bit-set/reset (BSR)
 *
 * Port A's slot-select side effect is applied by slots.c (slots_primary_select)
 * before it dispatches the write here, so this unit only owns B, C and control.
 * The keyboard matrix itself lives in kbd.c; this unit is the 8255 decode and
 * the row latch that addresses it.
 */

#include "msx_int.h"

static uint8_t s_ctrl;      /* control word, #AB                            */
static uint8_t s_pb;        /* port B output latch (port B is input on MSX)  */
static uint8_t s_pc;        /* port C: low nibble row select, high cassette  */

void ppi_init(void)
{
    ppi_reset();
}

void ppi_reset(void)
{
    s_ctrl = 0u;
    s_pb   = 0u;
    s_pc   = 0u;
}

uint8_t ppi_read(uint8_t reg)
{
    switch (reg & 3u) {
    case 0u:  return slots_ppi_a();                        /* primary slot latch */
    case 1u:  return kbd_row_bits((uint8_t)(s_pc & 0x0Fu)); /* keyboard columns   */
    case 2u:  return s_pc;                                  /* port C readback    */
    default:  return 0xFFu;                                 /* control: not readable */
    }
}

void ppi_write(uint8_t reg, uint8_t value)
{
    switch (reg & 3u) {
    case 0u:
        /* Port A: the primary slot-select side effect was already applied by
         * slots.c before this call; the latch lives there, nothing to store. */
        break;
    case 1u:
        s_pb = value;              /* port B is the keyboard input on MSX     */
        break;
    case 2u:
        s_pc = value;              /* row select (low nibble) + cassette bits */
        break;
    default:
        if (value & 0x80u) {
            s_ctrl = value;        /* mode-set word                           */
        } else {
            /* 8255 BSR mode: set or clear one port C bit.  C-BIOS uses this
             * for the cassette and keyboard-tone lines. */
            uint8_t bit = (uint8_t)((value >> 1) & 7u);
            if (value & 1u) s_pc = (uint8_t)(s_pc | (1u << bit));
            else            s_pc = (uint8_t)(s_pc & ~(1u << bit));
        }
        break;
    }
}
