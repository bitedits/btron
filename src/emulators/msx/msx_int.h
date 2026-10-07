/*
 * tronMSX — internal contract between the chip units.  B-MSX Rev 1.05,
 * Parts 1, 2 and 5.  Not public: only src/emulators/msx includes this.
 *
 * Every unit keeps its own statics; nothing here allocates, and no unit reads
 * a host clock on the step path (D1) or a float (D4).
 */

#ifndef _BTRON_EMULATOR_MSX_INT_H_
#define _BTRON_EMULATOR_MSX_INT_H_

#include <stdint.h>
#include "msx_err.h"

/* ---- 1.1 clock & frame constants --------------------------------------- */
#define MSX_XTAL_HZ      10738635u
#define MSX_Z80_HZ        3579545u
#define MSX_PSG_HZ        1789772u
#define MSX_T_PER_LINE          228u
#define MSX_LINES             262u      /* NTSC */
#define MSX_T_PER_FRAME     59664u      /* 228 * 262 */
#define MSX_DISPLAY_LINES       212u
#define MSX_PCM_HZ            44100u

/* ---- 5.3 memory bounds (core statics) ---------------------------------- */
#define MSX_VRAM_SIZE     131072u   /* V9958 128 KiB, 17-bit addressing  */
#define MSX_RAM_SIZE       65536u   /* one pool, pages 2 and 3 view it    */
#define MSX_ROM_SIZE       32768u   /* first-core cartridge cap           */
#define MSX_BIOS_SIZE      65536u   /* settings-loaded, outside the sum   */
#define MSX_LOGO_SIZE      16384u
#define MSX_SCREEN_W         256u
#define MSX_SCREEN_H     MSX_DISPLAY_LINES

/* ---- 5.2 rings -------------------------------------------------------- */
#define MSX_PCM_RING_SIZE  4096u   /* stereo frames                       */
#define MSX_CMD_RING_SIZE    16u
#define MSX_INPUT_ROWS       16u   /* one row byte per strobe row         */

/* cmd_ring commands (§5.2). */
#define MSX_CMD_NONE   0u
#define MSX_CMD_RESET  1u
#define MSX_CMD_PAUSE  2u
#define MSX_CMD_RESUME 3u
#define MSX_CMD_KBD    4u
#define MSX_CMD_QUIT   5u

/* ---- slots.c: canonical map, slot selects, port dispatch -------------- */
void    slots_init(void);
void    slots_reset(void);
void    slots_set_bios(const uint8_t *img, uint32_t size);
void    slots_set_logo(const uint8_t *img, uint32_t size);
void    slots_set_rom(const uint8_t *img, uint32_t size);
void    slots_primary_select(uint8_t value);      /* PPI port A alias      */
uint8_t slots_read(uint16_t addr);
void    slots_write(uint16_t addr, uint8_t value);
uint8_t *slots_ram_ptr(uint32_t *size);
uint8_t slots_ppi_a(void);                    /* primary slot-select latch, #A8 */

/* ---- vdp.c: ports 0x88-0x8B and 0x98-0x9B, renderer, VBlank ----------- */
void    vdp_init(uint8_t machine);        /* BMSX_MACHINE_* : TMS9918 or V9958 */
void    vdp_reset(void);
uint8_t vdp_read(uint8_t port);                   /* 0 = status/data pair  */
void    vdp_write(uint8_t port, uint8_t value);
void    vdp_advance(uint32_t t_states);
int     vdp_vblank_taken(void);                   /* 1 once per frame      */
void    vdp_render(uint16_t *fb);                 /* 256x212 RGB565        */
uint8_t *vdp_vram(uint32_t *size);
/* Debug hooks for the headless boot tracer (not used on the step path). */
uint8_t vdp_debug_reg(uint8_t reg);
uint8_t vdp_debug_status(void);
uint8_t vdp_debug_irq_pending(void);
uint32_t vdp_debug_write_addr(void);

/* ---- psg.c: AY-3-8910 at #A0-#A3; #A0 select, #A1 write, #A2 read ------ */
void    psg_init(void);
void    psg_reset(void);
uint8_t psg_read(uint8_t port);
void    psg_write(uint8_t port, uint8_t value);
uint8_t psg_debug_select(void);
uint8_t psg_debug_r7(void);
uint8_t psg_debug_r14(void);
uint8_t psg_debug_r15(void);
uint8_t psg_debug_pb(void);
/* The chip moves only while sampled: psg_render() advances exactly
 * T_PER_FRAME/2 T-states worth of ticks per 735-sample frame. */
uint32_t psg_render(int16_t *out, uint32_t frames);   /* L=R pairs          */

/* ---- ppi.c: 8255 at #A8-#AB, keyboard strobe and data ------------------ */
void    ppi_init(void);
void    ppi_reset(void);
uint8_t ppi_read(uint8_t reg);                    /* reg = port & 3        */
void    ppi_write(uint8_t reg, uint8_t value);

/* ---- kbd.c: matrix, joystick levels and the per-step snapshot ----------- */
void    kbd_init(void);
void    kbd_reset(void);
void    kbd_down(uint8_t row, uint8_t col);
void    kbd_up(uint8_t row, uint8_t col);
uint8_t kbd_row_bits(uint8_t row);                /* pressed = 0 (active low) */
uint8_t kbd_joy_pins(uint8_t sel);                /* 0 = socket A, 1 = socket B */
void    kbd_set_joy(uint8_t bits_a, uint8_t bits_b);
void    kbd_snapshot(uint8_t *out);               /* 16 row bytes          */

/* ---- bus dispatch: one place, §1.3 decode incl. incomplete decode ---- */
uint8_t msx_bus_in(uint8_t port);
void    msx_bus_out(uint8_t port, uint8_t value);

/* FNV-1a 64, the only hash in the module (D7). */
uint64_t msx_fnv1a64(const void *data, uint32_t len);

/* ---- bios.c: C-BIOS image loader (settings-loaded, outside the sum, §5.3) */
/* Loads cbios_main_<tag>.rom (+ optional logo) for `machine` from `dir` into
 * the slot 0 views.  -> MSX_OK, or MSX_ERR_BIOS when the main ROM is absent. */
int  bios_load(uint16_t machine, const char *dir);

/* ---- tronmsx.c: host-side BIOS directory override (posix build) --------- */
void msx_set_bios_dir(const char *dir);

#endif /* _BTRON_EMULATOR_MSX_INT_H_ */
