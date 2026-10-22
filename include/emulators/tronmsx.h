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
    uint16_t    machine;    /* b_msx_body.machine: 1..19             */
    uint16_t    frame_hz;   /* 60 for the first core; PAL 50 later    */
} msx_cfg_t;

/* Standard machines and full set of 18 C-BIOS machines */
#define BMSX_MACHINE_MSX                1u
#define BMSX_MACHINE_MSX2               2u
#define BMSX_MACHINE_MSX2P              3u

#define BMSX_MACHINE_MSX2_CBIOS         2u
#define BMSX_MACHINE_MSX2_CBIOS_BR      4u
#define BMSX_MACHINE_MSX2_CBIOS_EU      5u
#define BMSX_MACHINE_MSX2_CBIOS_JP      6u
#define BMSX_MACHINE_MSX2_CBIOS_FDD     7u
#define BMSX_MACHINE_MSX2_CBIOS_FDD_BR  8u
#define BMSX_MACHINE_MSX2_CBIOS_FDD_EU  9u
#define BMSX_MACHINE_MSX2_CBIOS_FDD_JP  10u

#define BMSX_MACHINE_MSX2P_CBIOS        3u
#define BMSX_MACHINE_MSX2P_CBIOS_BR     11u
#define BMSX_MACHINE_MSX2P_CBIOS_EU     12u
#define BMSX_MACHINE_MSX2P_CBIOS_JP     13u
#define BMSX_MACHINE_MSX2P_CBIOS_FDD    14u
#define BMSX_MACHINE_MSX2P_CBIOS_FDD_BR 15u
#define BMSX_MACHINE_MSX2P_CBIOS_FDD_EU 16u
#define BMSX_MACHINE_MSX2P_CBIOS_FDD_JP 17u

#define BMSX_MACHINE_MSXTR_CBIOS        18u
#define BMSX_MACHINE_MSXTR_CBIOS_FDD    19u

/* The cartridge cap, in bytes: supports up to 4 MiB MegaROMs (e.g. Geo3D-256) */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  define MSX_ROM_SIZE     4194304u
#else
/* Freestanding targets (PS2: 32 MiB of RDRAM for the whole image) hold the
 * cartridge twice -- the core's rom[] and the app's read buffer -- so 4 MiB
 * MegaROMs do not fit; 1 MiB carries the Geo3D demos but not Geo3D-256. */
#  define MSX_ROM_SIZE     1048576u
#endif

/* Where the machine's media lives when the caller names no directory, on each
 * side of the hosted boundary: a folder below the checkout where there is a host
 * file system, or a drawer of the /SYS volume an image carries where there is not
 * (src/tools/manifest.txt imprints those bodies).
 *
 * MSX_BIOS_DEFAULT_DIR is used by both the loader and the window that reports a
 * missing BIOS, so the message cannot drift from the search.  MSX_ROM_CATALOGUE
 * is listed by the XMB Games band and loaded by B-MSX, so the two show the same
 * rows; those rows are file names, and open_msx_window_with_rom() takes one of
 * them and resolves it against this.  Both live in the emulator's public header
 * and not in <btron/apps.h>, which declares the launcher's entry points
 * __WEAK_APP -- and a weak declaration in the app that actually *defines* them
 * would let the launcher's stub win the link. */
#if defined(__STDC_HOSTED__) && __STDC_HOSTED__ == 1
#  define MSX_BIOS_DEFAULT_DIR "third_party/openMSX/Contrib/cbios"
#  define MSX_ROM_CATALOGUE    "assets/msx"
#else
#  define MSX_BIOS_DEFAULT_DIR "/SYS/cbios"
#  define MSX_ROM_CATALOGUE    "/SYS/msx"
#endif

int  msx_init(const msx_cfg_t *cfg);      /* -> MSX_* codes          */
void msx_shutdown(void);                  /* idempotent              */

int  msx_load_rom(const uint8_t *rom, uint32_t size, uint32_t entry);

/* The core's own cartridge buffer, MSX_ROM_SIZE + 1 bytes (*cap).  A caller that
 * reads a cartridge into it and passes the same pointer to msx_load_rom() avoids
 * holding a second copy; the extra byte lets it see an image over the cap. */
uint8_t *msx_rom_buffer(uint32_t *cap);
void msx_reset(void);
void msx_pause(BOOL on);
uint16_t msx_get_machine(void);           /* BMSX_MACHINE_* last init */
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
