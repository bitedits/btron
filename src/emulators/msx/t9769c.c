/*
 * t9769c.c — Toshiba T9769C MSX turbo R Engine.
 * Hardware emulation source for B-MSX. Clean-room C99.
 */

#include "t9769c.h"
#include <string.h>

void t9769c_init(t9769c_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t9769c_reset(t);
}

void t9769c_reset(t9769c_t *t)
{
    if (!t) return;
    t->mapper_regs[0] = 3;
    t->mapper_regs[1] = 2;
    t->mapper_regs[2] = 1;
    t->mapper_regs[3] = 0;
    t->sys_flags = 0x20;     /* bit 5: CPU boot mode (1=R800) */
    t->rtc_reg = 0;
    t->s1990_reg = 0;
    t->cpu_status = 0x60;    /* bit 5: 1=Z80, bit 6: 1=ROM mode */
    t->e6_ticks = 0;
    t->e6_accum = 0;
    t->pcm_data = 0x80;
    t->pcm_ctrl = 0x03;      /* bit 0 BUFF=1 latched mode, bit 1 unmuted */
    t->pcm_counter = 0;
    t->pause_led = 0x00;
    t->kanji_addr = 0;
    t->opll_reg = 0;
    memset(t->opll_regs, 0, sizeof(t->opll_regs));
    t->midi_data = 0;
    t->midi_stat = 0x05;     /* TxRdy and TxEmpty */
}

void t9769c_set_kanji(t9769c_t *t, const uint8_t *rom, uint32_t size)
{
    uint32_t i;
    if (!t || !rom) return;
    t->kanji_size = (size > KANJI_ROM_MAX_SIZE) ? KANJI_ROM_MAX_SIZE : size;
    for (i = 0; i < t->kanji_size; i++) t->kanji_rom[i] = rom[i];
}

int t9769c_is_r800(const t9769c_t *t)
{
    if (!t) return 0;
    return (t->cpu_status & 0x20) == 0; /* bit 5: 0 = R800 */
}

void t9769c_advance(t9769c_t *t, uint32_t z80_t_states)
{
    if (!t) return;
    /* E6 timer ticks every 14 Z80 T-states: 3579545 / 14 = 255682 Hz */
    t->e6_accum += z80_t_states;
    while (t->e6_accum >= 14u) {
        t->e6_accum -= 14u;
        t->e6_ticks = (t->e6_ticks + 1u) & 0xFFFFu;
    }
    t->pcm_counter = (uint8_t)((t->pcm_counter + (uint8_t)z80_t_states) & 3u);
}

uint8_t t9769c_read(t9769c_t *t, uint8_t port)
{
    if (!t) return 0xFF;
    switch (port) {
    case 0x7C:
    case 0x7D:
        return 0xFF;
    case 0xA4:
        /* bit 0-1: 15.7kHz counter, bit 2-7: unused */
        return (uint8_t)(t->pcm_counter & 3u);
    case 0xA5:
        /* bit 0: BUFF, bit 1: MUTE, bit 7: COMP */
        return (uint8_t)(t->pcm_ctrl & 0x1Fu);
    case 0xA7:
        return t->pause_led;
    case 0xB4:
        return t->rtc_reg;
    case 0xB5: {
        uint8_t r = (uint8_t)(t->rtc_reg & 0x0Fu);
        return (uint8_t)(t->rtc_ram[r] & 0x0Fu);
    }
    case 0xD8:
        return 0xFF;
    case 0xD9: {
        uint8_t val = 0xFF;
        if (t->kanji_size && t->kanji_addr < t->kanji_size) {
            val = t->kanji_rom[t->kanji_addr];
        }
        t->kanji_addr = (t->kanji_addr & ~0x1Fu) | ((t->kanji_addr + 1u) & 0x1Fu);
        return val;
    }
    case 0xDA:
        return 0xFF;
    case 0xDB: {
        uint32_t a = t->kanji_addr | 0x20000u;
        uint8_t val = 0xFF;
        if (t->kanji_size && a < t->kanji_size) {
            val = t->kanji_rom[a];
        }
        t->kanji_addr = (t->kanji_addr & ~0x1Fu) | ((t->kanji_addr + 1u) & 0x1Fu);
        return val;
    }
    case 0xE4:
        return t->s1990_reg;
    case 0xE5:
        switch (t->s1990_reg) {
        case 5:  return 0x40; /* firmware switch */
        case 6:  return t->cpu_status;
        case 13: return 0x03;
        case 14: return 0x2F;
        case 15: return 0x8B;
        default: return 0xFF;
        }
    case 0xE6:
        return (uint8_t)(t->e6_ticks & 0xFFu);
    case 0xE7:
        return (uint8_t)((t->e6_ticks >> 8) & 0xFFu);
    case 0xE8:
        return t->midi_data;
    case 0xE9:
        return t->midi_stat;
    case 0xF4:
        return t->sys_flags;
    case 0xFC:
    case 0xFD:
    case 0xFE:
    case 0xFF:
        return (uint8_t)(t->mapper_regs[port & 3] | 0xE0u);
    default:
        return 0xFF;
    }
}

void t9769c_write(t9769c_t *t, uint8_t port, uint8_t val)
{
    if (!t) return;
    switch (port) {
    case 0x7C:
        t->opll_reg = (uint8_t)(val & 0x3Fu);
        break;
    case 0x7D:
        t->opll_regs[t->opll_reg] = val;
        break;
    case 0xA4:
        t->pcm_data = val;
        break;
    case 0xA5:
        t->pcm_ctrl = val;
        break;
    case 0xA7:
        t->pause_led = val;
        break;
    case 0xB4:
        t->rtc_reg = (uint8_t)(val & 0x0Fu);
        break;
    case 0xB5: {
        uint8_t r = (uint8_t)(t->rtc_reg & 0x0Fu);
        t->rtc_ram[r] = (uint8_t)(val & 0x0Fu);
        break;
    }
    case 0xD8:
        t->kanji_addr = (t->kanji_addr & 0x3F800u) | ((uint32_t)(val & 0x3Fu) << 5);
        break;
    case 0xD9:
        t->kanji_addr = (t->kanji_addr & 0x007E0u) | ((uint32_t)(val & 0x3Fu) << 11);
        break;
    case 0xDA:
        t->kanji_addr = (t->kanji_addr & 0x3F800u) | ((uint32_t)(val & 0x3Fu) << 5);
        break;
    case 0xDB:
        t->kanji_addr = (t->kanji_addr & 0x007E0u) | ((uint32_t)(val & 0x3Fu) << 11);
        break;
    case 0xE4:
        t->s1990_reg = val;
        break;
    case 0xE5:
        if (t->s1990_reg == 6) {
            t->cpu_status = (uint8_t)(val & 0x60u);
        }
        break;
    case 0xE6:
    case 0xE7:
        /* Reset timer counter */
        t->e6_ticks = 0;
        t->e6_accum = 0;
        break;
    case 0xE8:
        t->midi_data = val;
        break;
    case 0xE9:
        break;
    case 0xF4:
        t->sys_flags = val;
        break;
    case 0xFC:
    case 0xFD:
    case 0xFE:
    case 0xFF:
        t->mapper_regs[port & 3] = (uint8_t)(val & 0x1Fu);
        break;
    default:
        break;
    }
}
