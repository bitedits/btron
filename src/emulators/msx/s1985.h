/*
 * s1985.h — Yamaha S1985 MSX2 Engine.
 * Hardware emulation source for B-MSX.
 */

#ifndef _BTRON_EMULATOR_MSX_S1985_H_
#define _BTRON_EMULATOR_MSX_S1985_H_

#include <stdint.h>

typedef struct {
    uint8_t mapper_regs[4];  /* ports 0xFC..0xFF */
    uint8_t rtc_reg;         /* port 0xB4 address latch */
    uint8_t rtc_ram[64];     /* RTC register & RAM bank */
    uint8_t printer_status;  /* port 0x90 */
    uint8_t printer_data;    /* port 0x91 */
} s1985_t;

void    s1985_init(s1985_t *s);
void    s1985_reset(s1985_t *s);
uint8_t s1985_read(s1985_t *s, uint8_t port);
void    s1985_write(s1985_t *s, uint8_t port, uint8_t val);

#endif /* _BTRON_EMULATOR_MSX_S1985_H_ */
