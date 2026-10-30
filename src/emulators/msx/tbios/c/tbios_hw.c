/*
 * tbios_hw.c — MSX hardware control and MegaROM primitives for SDCC.
 * Imported from ../msx.btron.pro/src/msxgl/ (core.c, megarom.c).
 */

#include "../inc/tbios_hw.h"

static uint8_t s_current_bank1 = 0;
static uint8_t s_current_bank2 = 0;

uint8_t msx_in(uint8_t port) __naked
{
    (void)port;
#ifdef __SDCC
    __asm
    ld   c, a
    in   a, (c)
    ret
    __endasm;
#else
    return 0;
#endif
}

void msx_out(uint8_t port, uint8_t val) __naked
{
    (void)port;
    (void)val;
#ifdef __SDCC
    __asm
    ld   c, a
    ld   a, l
    out  (c), a
    ret
    __endasm;
#endif
}

void msx_di(void) __naked
{
#ifdef __SDCC
    __asm
    di
    ret
    __endasm;
#endif
}

void msx_ei(void) __naked
{
#ifdef __SDCC
    __asm
    ei
    ret
    __endasm;
#endif
}

void msx_halt(void) __naked
{
#ifdef __SDCC
    __asm
    halt
    ret
    __endasm;
#endif
}

void msx_delay(uint16_t cycles)
{
    while (cycles > 0) {
#ifdef __SDCC
        __asm
        nop
        nop
        nop
        nop
        __endasm;
#endif
        cycles--;
    }
}

void msx_wait_vblank(void)
{
    uint16_t timeout;
    msx_out(PORT_VDP_REG, 0x02);
    msx_out(PORT_VDP_REG, 0x80 | 15);

    timeout = 2500;
    while (timeout--) {
        if (!(msx_in(PORT_VDP_REG) & 0x40)) break;
    }

    timeout = 2500;
    while (timeout--) {
        if (msx_in(PORT_VDP_REG) & 0x40) break;
    }

    msx_out(PORT_VDP_REG, 0x00);
    msx_out(PORT_VDP_REG, 0x80 | 15);
}

void megarom_set_bank_page1(uint8_t bank)
{
    volatile uint8_t *reg = (volatile uint8_t *)ASCII16_BANK1_REG;
    *reg = bank;
    s_current_bank1 = bank;
}

void megarom_set_bank_page2(uint8_t bank)
{
    volatile uint8_t *reg = (volatile uint8_t *)ASCII16_BANK2_REG;
    *reg = bank;
    s_current_bank2 = bank;
}

uint8_t megarom_get_bank_page1(void)
{
    return s_current_bank1;
}

uint8_t megarom_get_bank_page2(void)
{
    return s_current_bank2;
}

