/*
 * tronMSX — B-MSX embedded MSX2+ core, public surface.
 * B-MSX specification Rev 1.05, Part 5 §5.1.
 *
 * Clean-room C99.  All state is static and bounded; the module performs no
 * heap allocation and reads no host clock on the step path (D1–D7, §8.1).
 */

#ifndef _BTRON_EMULATORS_TRONMSX_H_
#define _BTRON_EMULATORS_TRONMSX_H_

#include <btron/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* One machine instance per process; a second msx_init replaces the first. */
typedef struct {
    const char *bios_key;   /* "bios.msx2p" | NULL = C-BIOS default  */
    uint16_t    machine;    /* b_msx_body.machine: 1 MSX, 2 MSX2, 3 MSX2+ */
    uint16_t    frame_hz;   /* 60 for the first core; PAL 50 later    */
} msx_cfg_t;

int  msx_init(const msx_cfg_t *cfg);      /* -> MSX_* codes          */
void msx_shutdown(void);                  /* idempotent              */

int  msx_load_rom(const uint8_t *rom, uint32_t size, uint32_t entry);
void msx_reset(void);
void msx_pause(BOOL on);
void msx_run_frame(void);                 /* exactly T_PER_FRAME T   */
void msx_blit_rgb565(uint16_t *fb);       /* 256x212, caller-owned   */

void msx_kbd_down(uint8_t row, uint8_t col);
void msx_kbd_up(uint8_t row, uint8_t col);

/* Joystick socket A pin levels, as the BIOS reads them on the PSG's port A
 * input (2.5): TRGA bit0, up/down/left/right bits 1..4, TRGB bit6; port-B
 * steering is latched under TRGB.  Level, not edge.
 * The same bits are what a caller hands to msx_joy_set() below. */
#define MSX_JOY_TRGA   0x01u
#define MSX_JOY_UP     0x02u
#define MSX_JOY_DOWN   0x04u
#define MSX_JOY_LEFT   0x08u
#define MSX_JOY_RIGHT  0x10u
#define MSX_JOY_TRGB   0x40u

void msx_joy_set(uint8_t bits);

void msx_psg_submit_pcm(int16_t *buf, uint32_t *frames_out); /* §5.2, Part 6 */

uint64_t msx_total_cycles(void);
uint64_t msx_body_hash(void);             /* FNV-1a 64, fixed at load (D7) */
const char *msx_version_string(void);

/* Error codes are the stable ABI of §4.3; the string table is msx_err.h. */
enum {
    MSX_OK            = 0,
    MSX_ERR_BIOS      = 1,
    MSX_ERR_BIOS_MISS = 2,
    MSX_ERR_MAGIC     = 3,
    MSX_ERR_HDR       = 4,
    MSX_ERR_ENTRY     = 5,
    MSX_ERR_SIZE      = 6,
    MSX_ERR_RAM       = 7,
    MSX_ERR_KIND      = 8,
    MSX_ERR_MACHINE   = 9,
    MSX_ERR_TT_STALE  = 10,
    MSX_ERR_TT_RANGE  = 11,
    MSX_ERR_TT_STATE  = 12
};

#ifdef __cplusplus
}
#endif

#endif /* _BTRON_EMULATORS_TRONMSX_H_ */
