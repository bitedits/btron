/*
 * midi.c — MSX-MIDI and turbo R MIDI hardware emulation (8251 UART + 8253 Timer).
 * Hardware emulation source for B-MSX. Clean-room C99.
 * NASA JPL Power of Ten rules compliant.
 */

#include "midi.h"
#include <string.h>

msx_midi_t g_msx_midi;

void msx_midi_init(void)
{
    msx_midi_reset();
}

void msx_midi_reset(void)
{
    memset(&g_msx_midi, 0, sizeof(g_msx_midi));

    /* 8251 UART initial state:
     * Bit 7 (DSR) = 1
     * Bit 2 (TxEMPTY) = 1
     * Bit 0 (TxRDY) = 1
     */
    g_msx_midi.uart_status = 0x85u;
    g_msx_midi.uart_state  = 0u;

    for (int i = 0; i < 3; i++) {
        g_msx_midi.timer_count[i]  = 0xFFFFu;
        g_msx_midi.timer_reload[i] = 0xFFFFu;
        g_msx_midi.timer_rw_mode[i] = 3u; /* LSB then MSB default */
    }
}

uint8_t msx_midi_read(uint8_t port)
{
    switch (port) {
    case 0xE8u: { /* 8251 Data */
        uint8_t val = g_msx_midi.rx_data;
        g_msx_midi.uart_status &= ~0x02u; /* Clear RxRDY */
        if (g_msx_midi.rx_count > 0) {
            uint16_t tail = g_msx_midi.rx_tail;
            g_msx_midi.rx_data = g_msx_midi.rx_fifo[tail];
            g_msx_midi.rx_tail = (uint16_t)((tail + 1u) % MSX_MIDI_FIFO_SIZE);
            g_msx_midi.rx_count--;
            g_msx_midi.uart_status |= 0x02u; /* RxRDY set for next byte */
        }
        return val;
    }

    case 0xE9u: /* 8251 Status */
        return g_msx_midi.uart_status;

    case 0xEAu: /* Timer Interrupt Clear / Latch (Read invalid/returns bus float) */
    case 0xEBu:
        return 0xFFu;

    case 0xECu: /* 8253 Timer Counter 0 */
    case 0xEDu: /* 8253 Timer Counter 1 */
    case 0xEEu: { /* 8253 Timer Counter 2 */
        int ch = (int)(port - 0xECu);
        uint8_t rw = g_msx_midi.timer_rw_mode[ch];
        if (rw == 1u) {
            return (uint8_t)(g_msx_midi.timer_count[ch] & 0xFFu);
        } else if (rw == 2u) {
            return (uint8_t)(g_msx_midi.timer_count[ch] >> 8);
        } else {
            /* RW Mode 3: LSB then MSB */
            if (g_msx_midi.timer_byte_flip[ch] == 0u) {
                g_msx_midi.timer_byte_flip[ch] = 1u;
                return (uint8_t)(g_msx_midi.timer_count[ch] & 0xFFu);
            } else {
                g_msx_midi.timer_byte_flip[ch] = 0u;
                return (uint8_t)(g_msx_midi.timer_count[ch] >> 8);
            }
        }
    }

    case 0xEFu: /* 8253 Timer Control */
        return g_msx_midi.timer_ctrl;

    default:
        return 0xFFu;
    }
}

void msx_midi_write(uint8_t port, uint8_t val)
{
    switch (port) {
    case 0xE8u: /* 8251 Transmit Data */
        g_msx_midi.tx_data = val;
        if (g_msx_midi.tx_count < MSX_MIDI_FIFO_SIZE) {
            uint16_t head = g_msx_midi.tx_head;
            g_msx_midi.tx_fifo[head] = val;
            g_msx_midi.tx_head = (uint16_t)((head + 1u) % MSX_MIDI_FIFO_SIZE);
            g_msx_midi.tx_count++;
        }
        /* Ready for next transmit */
        g_msx_midi.uart_status |= 0x05u; /* TxRDY=1, TxEMPTY=1 */
        break;

    case 0xE9u: /* 8251 Control */
        if (g_msx_midi.uart_state == 0u) {
            /* Mode Instruction */
            g_msx_midi.uart_mode = val;
            g_msx_midi.uart_state = 1u;
        } else {
            /* Command Instruction */
            g_msx_midi.uart_command = val;
            if (val & 0x10u) {
                /* Error Reset (clears PE, OE, FE) */
                g_msx_midi.uart_status &= ~0x38u;
            }
            if (val & 0x40u) {
                /* Internal Reset -> return to Mode state */
                g_msx_midi.uart_state = 0u;
            }
        }
        break;

    case 0xEAu: /* Clear Timer Counter #2 Interrupt */
    case 0xEBu:
        g_msx_midi.timer_irq = 0u;
        break;

    case 0xECu: /* 8253 Counter 0 */
    case 0xEDu: /* 8253 Counter 1 */
    case 0xEEu: { /* 8253 Counter 2 */
        int ch = (int)(port - 0xECu);
        uint8_t rw = g_msx_midi.timer_rw_mode[ch];
        if (rw == 1u) {
            g_msx_midi.timer_reload[ch] = (uint16_t)((g_msx_midi.timer_reload[ch] & 0xFF00u) | val);
            g_msx_midi.timer_count[ch]  = g_msx_midi.timer_reload[ch];
        } else if (rw == 2u) {
            g_msx_midi.timer_reload[ch] = (uint16_t)((g_msx_midi.timer_reload[ch] & 0x00FFu) | ((uint16_t)val << 8));
            g_msx_midi.timer_count[ch]  = g_msx_midi.timer_reload[ch];
        } else {
            if (g_msx_midi.timer_byte_flip[ch] == 0u) {
                g_msx_midi.timer_reload[ch] = (uint16_t)((g_msx_midi.timer_reload[ch] & 0xFF00u) | val);
                g_msx_midi.timer_byte_flip[ch] = 1u;
            } else {
                g_msx_midi.timer_reload[ch] = (uint16_t)((g_msx_midi.timer_reload[ch] & 0x00FFu) | ((uint16_t)val << 8));
                g_msx_midi.timer_count[ch]  = g_msx_midi.timer_reload[ch];
                g_msx_midi.timer_byte_flip[ch] = 0u;
            }
        }
        break;
    }

    case 0xEFu: { /* 8253 Control Word */
        g_msx_midi.timer_ctrl = val;
        int ch = (int)((val >> 6) & 3u);
        if (ch < 3) {
            g_msx_midi.timer_rw_mode[ch]   = (uint8_t)((val >> 4) & 3u);
            g_msx_midi.timer_mode[ch]      = (uint8_t)((val >> 1) & 7u);
            g_msx_midi.timer_byte_flip[ch] = 0u;
        }
        break;
    }

    default:
        break;
    }
}

void msx_midi_receive_byte(uint8_t byte)
{
    if (!(g_msx_midi.uart_status & 0x02u)) {
        /* RX register is empty */
        g_msx_midi.rx_data = byte;
        g_msx_midi.uart_status |= 0x02u; /* Set RxRDY */
    } else if (g_msx_midi.rx_count < MSX_MIDI_FIFO_SIZE) {
        /* Enqueue into FIFO */
        uint16_t head = g_msx_midi.rx_head;
        g_msx_midi.rx_fifo[head] = byte;
        g_msx_midi.rx_head = (uint16_t)((head + 1u) % MSX_MIDI_FIFO_SIZE);
        g_msx_midi.rx_count++;
    } else {
        /* Overrun Error */
        g_msx_midi.uart_status |= 0x10u; /* OE bit 4 */
    }
}

void msx_midi_advance(uint32_t cycles)
{
    if (cycles == 0) return;

    /* 8253 Counter 2 countdown */
    if (g_msx_midi.timer_count[2] <= cycles) {
        g_msx_midi.timer_count[2] = g_msx_midi.timer_reload[2];
        g_msx_midi.timer_irq = 1u;
    } else {
        g_msx_midi.timer_count[2] -= (uint16_t)cycles;
    }
}

int msx_midi_has_tx(void)
{
    return g_msx_midi.tx_count > 0;
}

uint8_t msx_midi_pop_tx(void)
{
    if (g_msx_midi.tx_count == 0) return 0;
    uint16_t tail = g_msx_midi.tx_tail;
    uint8_t val = g_msx_midi.tx_fifo[tail];
    g_msx_midi.tx_tail = (uint16_t)((tail + 1u) % MSX_MIDI_FIFO_SIZE);
    g_msx_midi.tx_count--;
    return val;
}

int msx_midi_irq_pending(void)
{
    return g_msx_midi.timer_irq != 0;
}

