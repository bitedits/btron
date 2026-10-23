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
                   /* MSX_ROM_SIZE is the public cap, in tronmsx.h: an app that
                    * sizes a buffer for a cartridge has to know it too. */
#define MSX_BIOS_SIZE      65536u   /* settings-loaded, outside the sum   */
#define MSX_LOGO_SIZE      16384u
#define MSX_SUBROM_SIZE    16384u
#define MSX_MUSIC_SIZE     16384u
#define MSX_DISK_SIZE      65536u
/* A mounted disk image. 1.44 MiB on a host; 720 KiB where the image is small. */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#define MSX_DSK_CAP        1474560u
#else
#define MSX_DSK_CAP         737280u
#endif
#define MSX_MAPPER_RAM_SIZE 524288u /* 512 KiB */
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
void    slots_set_machine(uint16_t machine);
void    slots_set_bios(const uint8_t *img, uint32_t size);
void    slots_set_logo(const uint8_t *img, uint32_t size);
void    slots_set_sub(const uint8_t *img, uint32_t size);
void    slots_set_music(const uint8_t *img, uint32_t size);
void    slots_set_disk(const uint8_t *img, uint32_t size);
/* Raw sector image for the C-BIOS disk window. The disk ROM must already
 * be in place (FDD machine). -> MSX_OK, or MSX_ERR_SIZE. */
int     slots_set_dsk(const uint8_t *img, uint32_t size);
/* Store into the memory the CPU will see, including page 0 while the BIOS
 * is still mapped there. */
void    slots_ram_store(uint16_t addr, uint8_t value);
/* Secondary slot of main RAM on this machine (2 on MSX2, 0 on turboR). */
uint8_t slots_ram_ssec(void);
/* All four pages of slot 3 show main RAM, which is the DOS TPA.
 * A COM samples the page-1 slot and asks the BIOS to enter its
 * vblank handler there. */
void    slots_map_com_pages(void);
/* Page 0 stays the BIOS. Pages 1-3 show main RAM. A Disk BASIC BLOAD
 * image calls the BIOS directly and is not a TPA program. */
void    slots_map_bios_ram(void);
void    slots_set_rom(const uint8_t *img, uint32_t size);
uint8_t *slots_rom_ptr(uint32_t *cap);
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
void     psg_mix(int32_t *acc, uint32_t frames);     /* add into L/R        */

/* ---- audio.c: one S16 block from every chip --------------------------- */
uint32_t msx_audio_render(int16_t *out, uint32_t frames);

/* ---- scc.c: Konami SCC / SCC-I, memory mapped ------------------------- */
void    scc_init(void);
void    scc_reset(void);
int     scc_write(uint16_t addr, uint8_t value);     /* 1 = consumed       */
int     scc_read(uint16_t addr, uint8_t *out);       /* 1 = claimed        */
void    scc_mix(int32_t *acc, uint32_t frames);

/* ---- opll.c: YM2413 at #7C/#7D ---------------------------------------- */
void    opll_init(void);
void    opll_reset(void);
void    opll_address(uint8_t value);
void    opll_data(uint8_t value);
void    opll_mix(int32_t *acc, uint32_t frames);

/* ---- pcm.c: turboR DAC at #A4/#A5 ------------------------------------- */
void    pcm_init(void);
void    pcm_reset(void);
void    pcm_write_data(uint8_t value);
void    pcm_write_ctrl(uint8_t value);
uint8_t pcm_read_status(void);
uint8_t pcm_read_ctrl(void);
void    pcm_advance(uint32_t t_states);
void    pcm_mix(int32_t *acc, uint32_t frames);

/* ---- y8950.c: MSX-AUDIO FM at #C0–#C3. ADPCM-B is latched, not played. */
void    y8950_init(void);
void    y8950_reset(void);
uint8_t y8950_read(uint8_t port);
void    y8950_write(uint8_t port, uint8_t value);
void    y8950_mix(int32_t *acc, uint32_t frames);

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

/* ---- dsk.c: FAT12 behind the C-BIOS MegaROM window and BDOS 0x0F/0x27 -- */
void    dsk_clear(void);
void    dsk_reset_open(void);
int     dsk_set_image(const uint8_t *img, uint32_t size); /* 1 = accepted */
uint8_t dsk_bank_byte(uint8_t bank, uint16_t off);
struct z80;
void    dsk_bdos(struct z80 *z);
/* Root menu drawn into the text name table. dsk_menu_service() is called
 * from the frame loop: 1 means the menu owns this frame and the CPU stays. */
int     dsk_menu_service(void);
int     dsk_menu_active(void);
int     dsk_menu_key(int op);                 /* 0 up, 1 down, 2 enter */
int     dsk_menu_cursor(void);
int     dsk_menu_rows(void);
int     dsk_menu_row(int index, char *out, uint32_t cap);

/* ---- tronmsx.c: host-side BIOS directory override (posix build) --------- */
void msx_set_bios_dir(const char *dir);
/* One instruction, with the same VDP advance as msx_run_frame. */
void msx_step_one(void);
/* Point the running CPU at a loaded COM. Does not reset the chip. */
void msx_cpu_go(uint16_t pc, uint16_t sp);
void msx_cpu_words(uint16_t *pc, uint16_t *sp, uint16_t *bc,
                   uint16_t *de, uint16_t *hl, uint8_t *a);
/* Copy port hit counts since the previous take, then zero them. */
void msx_port_take(uint32_t r[256], uint32_t w[256]);

#endif /* _BTRON_EMULATOR_MSX_INT_H_ */
