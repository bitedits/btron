/*
 * hd64180.h — Hitachi HD64180 CPU emulation for Hitachi HC-90 / HC-95.
 * Hardware emulation source for B-MSX. Clean-room C99.
 * NASA JPL Power of Ten rules compliant.
 */

#ifndef _BTRON_EMULATOR_MSX_HD64180_H_
#define _BTRON_EMULATOR_MSX_HD64180_H_

#include "z80.h"

/* HD64180 shares the exact register set with Z80 */
typedef z80 hd64180_t;

typedef struct {
    uint8_t  io_regs[64];
    uint16_t tmdr[2];     /* Timer 0, 1 counter */
    uint16_t rldr[2];     /* Timer 0, 1 reload */
    uint8_t  tcr;         /* Timer control register */
    uint8_t  icr;         /* Interrupt / I/O control register (base address) */
    uint8_t  cbr;         /* MMU Common Base Register */
    uint8_t  bbr;         /* MMU Bank Base Register */
    uint8_t  cbar;        /* MMU Common/Bank Area Register */
    uint32_t timer_cycles[2];
} hd64180_state_t;

extern hd64180_state_t g_hd64180_state;

void    hd64180_init(hd64180_t *cpu);
void    hd64180_reset(void);
void    hd64180_step(hd64180_t *cpu);
void    hd64180_advance(hd64180_t *cpu, uint32_t t_states);

uint8_t hd64180_io_read(uint8_t port);
void    hd64180_io_write(uint8_t port, uint8_t val);
int     hd64180_is_internal_io(uint8_t port);

#endif /* _BTRON_EMULATOR_MSX_HD64180_H_ */

