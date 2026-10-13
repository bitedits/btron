/*
 * t9769b.h — Toshiba T9769B MSX2+ Engine.
 * Hardware emulation source for B-MSX.
 */

#ifndef _BTRON_EMULATOR_MSX_T9769B_H_
#define _BTRON_EMULATOR_MSX_T9769B_H_

#include <stdint.h>

#define KANJI_ROM_MAX_SIZE 262144u  /* 256 KiB */

typedef struct {
    uint8_t mapper_regs[4];  /* ports 0xFC..0xFF */
    uint8_t sys_flags;       /* port 0xF4 */
    uint8_t rtc_reg;         /* port 0xB4 address latch */
    uint8_t rtc_ram[64];     /* RTC register & RAM bank */

    /* Kanji ROM state (ports 0xD8..0xDB) */
    uint32_t kanji_addr;
    uint8_t  kanji_rom[KANJI_ROM_MAX_SIZE];
    uint32_t kanji_size;

    /* MSX-MUSIC (OPLL YM2413) register latch */
    uint8_t opll_reg;
    uint8_t opll_regs[64];
} t9769b_t;

void    t9769b_init(t9769b_t *t);
void    t9769b_reset(t9769b_t *t);
uint8_t t9769b_read(t9769b_t *t, uint8_t port);
void    t9769b_write(t9769b_t *t, uint8_t port, uint8_t val);
void    t9769b_set_kanji(t9769b_t *t, const uint8_t *rom, uint32_t size);

#endif /* _BTRON_EMULATOR_MSX_T9769B_H_ */
