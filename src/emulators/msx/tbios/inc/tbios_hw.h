/*
 * tbios_hw.h — Low-level I/O port and CPU primitives for T-BIOS / T-DOS (SDCC).
 * Imported from ../msx.btron.pro/src/msxgl/ (msxgl_config.h, core.h, megarom.h).
 * Compatible with SDCC z80 / z180 / r800 targets.
 */

#ifndef _TBIOS_HW_H_
#define _TBIOS_HW_H_

#include <stdint.h>
#include <stdbool.h>

/* I/O Port Definitions for MSX / V9938 / V9958 / V9968 / R800 / HD64180 */
#define PORT_VDP_DATA           0x98
#define PORT_VDP_REG            0x99
#define PORT_VDP_PALETTE        0x9A
#define PORT_VDP_INDIR          0x9B

#define PORT_PSG_REG            0xA0
#define PORT_PSG_WRITE          0xA1
#define PORT_PSG_READ           0xA2

#define PORT_PPI_PA             0xA8    /* Primary Slot Register */
#define PORT_PPI_PB             0xA9    /* Keyboard Row Read */
#define PORT_PPI_PC             0xAA    /* Keyboard Column / Cassette / Caps */
#define PORT_PPI_MODE           0xAB    /* PPI Mode Control */

#define PORT_OPLL_ADDR          0x7C    /* YM2413 MSX-MUSIC Address */
#define PORT_OPLL_DATA          0x7D    /* YM2413 MSX-MUSIC Data */

#define PORT_MSXAUDIO_STATUS    0xC0    /* Y8950 Status / Address */
#define PORT_MSXAUDIO_DATA      0xC1    /* Y8950 Data */

#define PORT_KANJI_ADDR_LO      0xD8    /* Kanji ROM Address Low */
#define PORT_KANJI_ADDR_HI      0xD9    /* Kanji ROM Address High */
#define PORT_KANJI_DATA_L1      0xDA    /* Kanji ROM Level 1 Data */
#define PORT_KANJI_DATA_L2      0xDB    /* Kanji ROM Level 2 Data */

#define PORT_MAPPER_P0          0xFC    /* Memory Mapper Page 0 */
#define PORT_MAPPER_P1          0xFD    /* Memory Mapper Page 1 */
#define PORT_MAPPER_P2          0xFE    /* Memory Mapper Page 2 */
#define PORT_MAPPER_P3          0xFF    /* Memory Mapper Page 3 */

#define PORT_S1990_REG          0xE4    /* MSX turbo R S1990 Register Select */
#define PORT_S1990_DATA         0xE5    /* MSX turbo R S1990 Register Data */
#define PORT_PCM_CNT_LO         0xE6    /* MSX turbo R 15.75kHz Counter Low */
#define PORT_PCM_CNT_HI         0xE7    /* MSX turbo R 15.75kHz Counter High */
#define PORT_PCM_DATA           0xA4    /* MSX turbo R 8-bit PCM DAC/ADC */
#define PORT_PCM_CTRL           0xA5    /* MSX turbo R PCM Control/Status */

#define PORT_RTC_ADDR           0xB4    /* RP5C01 RTC Address */
#define PORT_RTC_DATA           0xB5    /* RP5C01 RTC Data */

/* MegaROM Bank Switch addresses (ASCII16 / ASCII8 / Konami) */
#define ASCII16_BANK1_REG       0x6000
#define ASCII16_BANK2_REG       0x7000
#define ASCII8_BANK0_REG        0x6000
#define ASCII8_BANK1_REG        0x6800
#define ASCII8_BANK2_REG        0x7000
#define ASCII8_BANK3_REG        0x7800

/* CPU Mode Constants (CHGCPU 0x0180) */
#define CPU_MODE_Z80            0x00
#define CPU_MODE_R800_DRAM      0x81
#define CPU_MODE_R800_ROM       0x82

/* Low-level I/O primitives (SDCC __sdcccall(1) ABI) */
uint8_t msx_in(uint8_t port);
void    msx_out(uint8_t port, uint8_t val);
void    msx_di(void);
void    msx_ei(void);
void    msx_halt(void);
void    msx_delay(uint16_t cycles);
void    msx_wait_vblank(void);

/* MegaROM banking helpers */
void    megarom_set_bank_page1(uint8_t bank);
void    megarom_set_bank_page2(uint8_t bank);
uint8_t megarom_get_bank_page1(void);
uint8_t megarom_get_bank_page2(void);

#endif /* _TBIOS_HW_H_ */

