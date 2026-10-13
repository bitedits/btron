/*
 * t9769b.c — Toshiba T9769B MSX2+ Engine.
 * Hardware emulation source for B-MSX. Clean-room C99.
 */

#include "t9769b.h"
#include <string.h>

void t9769b_init(t9769b_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof(*t));
    t9769b_reset(t);
}

void t9769b_reset(t9769b_t *t)
{
    if (!t) return;
    t->mapper_regs[0] = 3;
    t->mapper_regs[1] = 2;
    t->mapper_regs[2] = 1;
    t->mapper_regs[3] = 0;
    t->sys_flags = 0x80;     /* bit 7: cold reset / show logo */
    t->rtc_reg = 0;
    t->kanji_addr = 0;
    t->opll_reg = 0;
    memset(t->opll_regs, 0, sizeof(t->opll_regs));
}

void t9769b_set_kanji(t9769b_t *t, const uint8_t *rom, uint32_t size)
{
    uint32_t i;
    if (!t || !rom) return;
    t->kanji_size = (size > KANJI_ROM_MAX_SIZE) ? KANJI_ROM_MAX_SIZE : size;
    for (i = 0; i < t->kanji_size; i++) t->kanji_rom[i] = rom[i];
}

uint8_t t9769b_read(t9769b_t *t, uint8_t port)
{
    if (!t) return 0xFF;
    switch (port) {
    case 0x7C:
    case 0x7D:
        return 0xFF; /* MSX-MUSIC is write-only */
    case 0xB4:
        return t->rtc_reg;
    case 0xB5: {
        uint8_t r = (uint8_t)(t->rtc_reg & 0x0Fu);
        return (uint8_t)(t->rtc_ram[r] & 0x0Fu);
    }
    case 0xD8:
        return 0xFF;
    case 0xD9: {
        /* Kanji 1 read */
        uint8_t val = 0xFF;
        if (t->kanji_size && t->kanji_addr < t->kanji_size) {
            val = t->kanji_rom[t->kanji_addr];
        }
        /* Auto-advance lowest 5 bits */
        t->kanji_addr = (t->kanji_addr & ~0x1Fu) | ((t->kanji_addr + 1u) & 0x1Fu);
        return val;
    }
    case 0xDA:
        return 0xFF;
    case 0xDB: {
        /* Kanji 2 read (offset + 128 KiB) */
        uint32_t a = t->kanji_addr | 0x20000u;
        uint8_t val = 0xFF;
        if (t->kanji_size && a < t->kanji_size) {
            val = t->kanji_rom[a];
        }
        t->kanji_addr = (t->kanji_addr & ~0x1Fu) | ((t->kanji_addr + 1u) & 0x1Fu);
        return val;
    }
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

void t9769b_write(t9769b_t *t, uint8_t port, uint8_t val)
{
    if (!t) return;
    switch (port) {
    case 0x7C:
        t->opll_reg = (uint8_t)(val & 0x3Fu);
        break;
    case 0x7D:
        t->opll_regs[t->opll_reg] = val;
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
        /* Kanji column (bits 5..10) */
        t->kanji_addr = (t->kanji_addr & 0x3F800u) | ((uint32_t)(val & 0x3Fu) << 5);
        break;
    case 0xD9:
        /* Kanji row (bits 11..16) */
        t->kanji_addr = (t->kanji_addr & 0x007E0u) | ((uint32_t)(val & 0x3Fu) << 11);
        break;
    case 0xDA:
        t->kanji_addr = (t->kanji_addr & 0x3F800u) | ((uint32_t)(val & 0x3Fu) << 5);
        break;
    case 0xDB:
        t->kanji_addr = (t->kanji_addr & 0x007E0u) | ((uint32_t)(val & 0x3Fu) << 11);
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
