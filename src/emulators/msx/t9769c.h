/*
 * t9769c.h — Toshiba T9769C MSX turbo R Engine.
 * Hardware emulation source for B-MSX.
 */

#ifndef _BTRON_EMULATOR_MSX_T9769C_H_
#define _BTRON_EMULATOR_MSX_T9769C_H_

#include <stdint.h>

#define KANJI_ROM_MAX_SIZE 262144u

typedef struct {
    uint8_t mapper_regs[4];  /* ports 0xFC..0xFF */
    uint8_t sys_flags;       /* port 0xF4 */
    uint8_t rtc_reg;         /* port 0xB4 */
    uint8_t rtc_ram[64];     /* RTC register & RAM bank */

    /* S1990 CPU arbitration & bus controller (0xE4..0xE5) */
    uint8_t s1990_reg;
    uint8_t cpu_status;      /* reg 6: bit 5: 1=Z80, 0=R800; bit 6: 1=ROM, 0=DRAM */

    /* E6 Timer (ports 0xE6..0xE7) */
    uint32_t e6_ticks;       /* 16-bit counter advanced by bus cycles */
    uint32_t e6_accum;       /* sub-cycle accumulator */

    /* Turbo-R PCM (ports 0xA4..0xA5) */
    uint8_t pcm_data;        /* 0xA4 */
    uint8_t pcm_ctrl;        /* 0xA5 */
    uint8_t pcm_counter;

    /* Pause / CPU LED (port 0xA7) */
    uint8_t pause_led;       /* bit 7: LED state */

    /* Kanji ROM state (ports 0xD8..0xDB) */
    uint32_t kanji_addr;
    uint8_t  kanji_rom[KANJI_ROM_MAX_SIZE];
    uint32_t kanji_size;

    /* MSX-MUSIC */
    uint8_t opll_reg;
    uint8_t opll_regs[64];

    /* MSX-MIDI (ports 0xE8..0xEB) */
    uint8_t midi_data;
    uint8_t midi_stat;
} t9769c_t;

void    t9769c_init(t9769c_t *t);
void    t9769c_reset(t9769c_t *t);
uint8_t t9769c_read(t9769c_t *t, uint8_t port);
void    t9769c_write(t9769c_t *t, uint8_t port, uint8_t val);
void    t9769c_advance(t9769c_t *t, uint32_t z80_t_states);
void    t9769c_set_kanji(t9769c_t *t, const uint8_t *rom, uint32_t size);
int     t9769c_is_r800(const t9769c_t *t);

#endif /* _BTRON_EMULATOR_MSX_T9769C_H_ */
