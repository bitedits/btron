/*
 * midi.h — MSX-MIDI and turbo R MIDI hardware emulation (8251 UART + 8253 Timer).
 * Hardware emulation source for B-MSX. Clean-room C99.
 * NASA JPL Power of Ten rules compliant.
 */

#ifndef _BTRON_EMULATOR_MSX_MIDI_H_
#define _BTRON_EMULATOR_MSX_MIDI_H_

#include <stdint.h>

#define MSX_MIDI_FIFO_SIZE 256

typedef struct {
    /* Intel 8251 UART */
    uint8_t  uart_status;
    uint8_t  uart_mode;
    uint8_t  uart_command;
    uint8_t  uart_state; /* 0 = wait for mode, 1 = wait for command */
    uint8_t  rx_data;
    uint8_t  tx_data;

    /* Intel 8253 Timer */
    uint16_t timer_count[3];
    uint16_t timer_reload[3];
    uint8_t  timer_mode[3];
    uint8_t  timer_rw_mode[3];
    uint8_t  timer_byte_flip[3];
    uint8_t  timer_ctrl;
    uint8_t  timer_irq;

    /* FIFOs for external connection */
    uint8_t  tx_fifo[MSX_MIDI_FIFO_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;
    uint16_t tx_count;

    uint8_t  rx_fifo[MSX_MIDI_FIFO_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
    uint16_t rx_count;
} msx_midi_t;

extern msx_midi_t g_msx_midi;

void     msx_midi_init(void);
void     msx_midi_reset(void);
uint8_t  msx_midi_read(uint8_t port);
void     msx_midi_write(uint8_t port, uint8_t val);
void     msx_midi_advance(uint32_t cycles);

/* MIDI Event Interface */
void     msx_midi_receive_byte(uint8_t byte);
int      msx_midi_has_tx(void);
uint8_t  msx_midi_pop_tx(void);
int      msx_midi_irq_pending(void);

#endif /* _BTRON_EMULATOR_MSX_MIDI_H_ */

