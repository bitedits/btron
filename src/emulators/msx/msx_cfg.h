/*
 * msx_cfg.h — B-MSX machine configuration loader for blueMSX INI files.
 * Clean-room C99. B-MSX Rev 1.05. Zero dynamic allocations.
 */

#ifndef _BTRON_EMULATOR_MSX_CFG_H_
#define _BTRON_EMULATOR_MSX_CFG_H_

#include <stdint.h>

#define BMSX_CFG_MAX_PATH    256
#define BMSX_CFG_MAX_NAME     64
#define BMSX_CFG_MAX_SLOTS    16

typedef struct {
    uint8_t  slot;
    uint8_t  subslot;
    uint8_t  start_page;
    uint8_t  num_pages;
    uint32_t rom_type;
    char     rom_path[BMSX_CFG_MAX_PATH];
} bmsx_slot_entry_t;

typedef struct {
    char     name[BMSX_CFG_MAX_NAME];
    char     ini_path[BMSX_CFG_MAX_PATH];

    /* [Video] */
    char     video_version[16];   /* "V9958", "V9938", etc. */
    uint32_t vram_bytes;          /* e.g. 131072 */

    /* [Board] */
    char     board_type[32];      /* "MSX-T9769C", "MSX-S1985", etc. */

    /* [Subslotted Slots] */
    uint8_t  subslotted[4];       /* 1 if slot 0..3 is expanded */

    /* [External Slots] */
    uint8_t  cart_slot_a;
    uint8_t  cart_subslot_a;
    uint8_t  cart_slot_b;
    uint8_t  cart_subslot_b;

    /* [FDC] */
    uint8_t  fdc_count;

    /* [CPU] */
    uint32_t z80_freq;
    uint32_t r800_freq;
    uint8_t  has_r800;
    uint8_t  has_hd64180;

    /* [Slots] */
    bmsx_slot_entry_t slots[BMSX_CFG_MAX_SLOTS];
    uint8_t  num_slots;

    /* Resolved ROM filenames (basenames) */
    char     main_rom[BMSX_CFG_MAX_NAME];
    char     logo_rom[BMSX_CFG_MAX_NAME];
    char     sub_rom[BMSX_CFG_MAX_NAME];
    char     music_rom[BMSX_CFG_MAX_NAME];
    char     disk_rom[BMSX_CFG_MAX_NAME];
    char     kanji_rom[BMSX_CFG_MAX_NAME];

    /* Resolved Subsystem Slot Mappings */
    uint8_t  main_slot;
    uint8_t  main_subslot;

    uint8_t  logo_slot;
    uint8_t  logo_subslot;

    uint8_t  sub_slot;
    uint8_t  sub_subslot;

    uint8_t  music_slot;
    uint8_t  music_subslot;

    uint8_t  disk_slot;
    uint8_t  disk_subslot;

    uint8_t  ram_slot;
    uint8_t  ram_subslot;
    uint32_t ram_kb;
} bmsx_machine_cfg_t;

extern bmsx_machine_cfg_t g_bmsx_cfg;

/* Look up canonical machine config name from machine enum */
const char *bmsx_cfg_name_for_machine(uint16_t machine);

/* Find INI path for machine name or custom key. Checks:
 * 1. direct path
 * 2. ./src/emulators/msx/configs/<name>.ini
 * 3. ./src/emulators/msx/configs/blueMSX/Machines/<name>/config.ini
 * 4. ./src/emulators/msx/configs/blueMSX/Machines/<name>.ini
 * 5. third_party/cbios-nextor_FDD-and-turboR/configs/blueMSX/Machines/<name>/config.ini
 */
int bmsx_cfg_find_path(const char *name_or_path, char *out_path, uint32_t out_cap);

/* Load and parse machine config from INI file into cfg */
int bmsx_cfg_load(const char *name_or_key, uint16_t machine, bmsx_machine_cfg_t *cfg);

#endif /* _BTRON_EMULATOR_MSX_CFG_H_ */
