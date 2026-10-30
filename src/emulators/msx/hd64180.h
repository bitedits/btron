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

    /* ASCI Channels 0 and 1 */
    uint8_t  asci_cntla[2];
    uint8_t  asci_cntlb[2];
    uint8_t  asci_stat[2];
    uint8_t  asci_tdr[2];
    uint8_t  asci_rdr[2];
    uint8_t  asci_rx_fifo[2][16];
    uint8_t  asci_rx_head[2];
    uint8_t  asci_rx_tail[2];
    uint8_t  asci_rx_count[2];

    /* CSIO */
    uint8_t  csio_cntr;
    uint8_t  csio_trdr;

    /* DMAC Channels 0 and 1 */
    uint32_t dma_sar0;    /* 20-bit Source Address Ch 0 */
    uint32_t dma_dar0;    /* 20-bit Destination Address Ch 0 */
    uint16_t dma_bcr0;    /* 16-bit Byte Count Ch 0 */
    uint32_t dma_mar1;    /* 20-bit Memory Address Ch 1 */
    uint16_t dma_iar1;    /* 16-bit I/O Address Ch 1 */
    uint16_t dma_bcr1;    /* 16-bit Byte Count Ch 1 */
    uint8_t  dma_dstat;   /* DMA Status Register (DE1, DE0, DWE1, DWE0, DIE1, DIE0, DME) */
    uint8_t  dma_dmode;   /* DMA Mode Register (DM1, DM0, SM1, SM0, MMOD) */
    uint8_t  dma_dcntl;   /* DMA Control Register (MWI, IWI, DMS) */
} hd64180_state_t;

extern hd64180_state_t g_hd64180_state;

void     hd64180_init(hd64180_t *cpu);
void     hd64180_reset(void);
void     hd64180_step(hd64180_t *cpu);
void     hd64180_advance(hd64180_t *cpu, uint32_t t_states);

uint8_t  hd64180_io_read(uint8_t port);
void     hd64180_io_write(uint8_t port, uint8_t val);
int      hd64180_is_internal_io(uint8_t port);
uint32_t hd64180_translate_address(uint16_t logical_addr);

/* Peripheral helpers */
void     hd64180_asci_rx(int ch, uint8_t byte);
uint8_t  hd64180_asci_get_tx(int ch);
void     hd64180_dma_exec(hd64180_t *cpu, int ch);

#endif /* _BTRON_EMULATOR_MSX_HD64180_H_ */

