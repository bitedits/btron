/*
 * s1985.c — Yamaha S1985 MSX2 Engine.
 * Hardware emulation source for B-MSX. Clean-room C99.
 */

#include "s1985.h"
#include <string.h>

void s1985_init(s1985_t *s)
{
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s1985_reset(s);
}

void s1985_reset(s1985_t *s)
{
    if (!s) return;
    s->mapper_regs[0] = 3;
    s->mapper_regs[1] = 2;
    s->mapper_regs[2] = 1;
    s->mapper_regs[3] = 0;
    s->rtc_reg = 0;
    s->printer_status = 0xFD; /* printer ready, not busy */
    s->printer_data = 0;
}

uint8_t s1985_read(s1985_t *s, uint8_t port)
{
    if (!s) return 0xFF;
    switch (port) {
    case 0x90:
        return s->printer_status;
    case 0x91:
        return s->printer_data;
    case 0xB4:
        return s->rtc_reg;
    case 0xB5: {
        uint8_t r = (uint8_t)(s->rtc_reg & 0x0Fu);
        return (uint8_t)(s->rtc_ram[r] & 0x0Fu);
    }
    case 0xFC:
    case 0xFD:
    case 0xFE:
    case 0xFF:
        return (uint8_t)(s->mapper_regs[port & 3] | 0xE0u); /* MSX 512KB mapper returns 5 bits */
    default:
        return 0xFF;
    }
}

void s1985_write(s1985_t *s, uint8_t port, uint8_t val)
{
    if (!s) return;
    switch (port) {
    case 0x90:
    case 0x91:
        s->printer_data = val;
        break;
    case 0xB4:
        s->rtc_reg = (uint8_t)(val & 0x0Fu);
        break;
    case 0xB5: {
        uint8_t r = (uint8_t)(s->rtc_reg & 0x0Fu);
        s->rtc_ram[r] = (uint8_t)(val & 0x0Fu);
        break;
    }
    case 0xFC:
    case 0xFD:
    case 0xFE:
    case 0xFF:
        s->mapper_regs[port & 3] = (uint8_t)(val & 0x1Fu); /* 32 segments = 512KB */
        break;
    default:
        break;
    }
}
