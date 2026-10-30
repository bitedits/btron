/*
 * tronMSX — slot system, canonical memory map and bus port dispatch.
 * B-MSX Rev 1.05. Clean-room C99.
 *
 * Supports MSX1, MSX2, MSX2+ and MSX turbo R configurations with:
 * - Subslotted primary slots and 0xFFFF secondary slot registers
 * - 512 KiB internal Memory Mapper RAM
 * - MegaROM cartridge mapper emulation (ASCII16, ASCII8, Konami, Konami SCC, linear up to 4 MiB)
 * - Sub-ROM, Logo, MSX-MUSIC and Disk ROM loading
 */

#include "msx_int.h"
#include <emulators/tronmsx.h>
#include <string.h>
#include <stdio.h>

#include "v9938.h"
#include "s1985.h"
#include "t9769b.h"
#include "t9769c.h"
#include "geo3d.h"
#include "hd64180.h"
#include "midi.h"
#include "msx_cfg.h"

#define SLOT_BIOS    0u
#define SLOT_CART_A  1u
#define SLOT_CART_B  2u
#define SLOT_RAM     3u

s1985_t g_s1985;
t9769b_t g_t9769b;
t9769c_t g_t9769c;
geo3d_t g_geo3d;
extern v9938_t *msx_get_v9938(void);
static uint16_t s_machine = BMSX_MACHINE_MSX;

/* Memory pools */
static uint8_t  ram[MSX_RAM_SIZE];                     /* 64 KiB for MSX1 */
static uint8_t  mapper_ram[MSX_MAPPER_RAM_SIZE];       /* Up to 4 MiB for MSX2/2+/TR */
static uint8_t  rom[MSX_ROM_SIZE + 1];                 /* cartridge (+1 spare byte lets the app tell an over-cap image from one that just fits) */
static uint8_t  bios[MSX_BIOS_SIZE];                   /* 32-64 KiB main BIOS */
static uint8_t  logo[MSX_LOGO_SIZE];                   /* 16 KiB logo */
static uint8_t  sub_rom[MSX_SUBROM_SIZE];              /* 16 KiB sub-ROM */
static uint8_t  music_rom[MSX_MUSIC_SIZE];            /* 16 KiB MSX-MUSIC */
static uint8_t  disk_rom[MSX_DISK_SIZE];               /* Up to 64 KiB FDD/Nextor */

static uint32_t rom_size, bios_size, logo_size;
static uint32_t sub_size, music_size, disk_size;

/* Cartridge banking, SRAM, and specialized registers */
static int      cart_mapper = MAPPER_PLAIN;
static uint8_t  cart_banks[4];
static uint8_t  cart_sram[65536];      /* 64 KiB battery-backed SRAM */
static uint8_t  sram_enabled_b1;       /* ASCII16 SRAM enable bank 1 */
static uint8_t  sram_enabled_b2;       /* ASCII16 SRAM enable bank 2 */
static uint8_t  ascii8_sram_mask;      /* ASCII8 SRAM active mask */
static uint8_t  ascii8_sram_bank[4];   /* ASCII8 SRAM sub-banks */
static uint8_t  ascii8_sram_bit;       /* bit threshold for SRAM */
static uint8_t  gm2_sram_enabled[4];   /* Game Master 2 SRAM enable per bank */
static uint8_t  gm2_sram_page[4];      /* Game Master 2 SRAM 4K page */
static uint8_t  crossblaim_page[4];    /* Cross Blaim page mapping */
static uint16_t neo_reg[4];            /* NEO8 / NEO16 12-bit register */
static uint8_t  scc_enable_manbow;     /* Manbow2 SCC enable flag */
static uint8_t  halnote_sram_enabled;  /* Halnote SRAM enable flag */
static uint8_t  halnote_sub_enabled;   /* Halnote submapper enable flag */
static uint8_t  majutsushi_dac;        /* Majutsushi DAC latch */

/* 8 KiB cartridge views across the entire 64 KiB address space */
static const uint8_t *cart_rview[8];
static uint8_t       *cart_wview[8];

/* Slot registers */
static uint8_t  ppi_a;           /* #A8: primary slot select per page */
static uint8_t  sub[4];          /* Secondary slot select per primary slot */
static uint8_t  map_seg[4];      /* #FC-#FF mapper segments */

/* Direct page views */
static const uint8_t *rview[4];
static uint8_t       *wview[4];
static uint8_t        is_cart_page[4];
static uint8_t        is_mapper_page[4];
static uint8_t        is_disk_page[4];
/* C-BIOS disk window: $6000, $8000 and $A000 each select one 8 KiB bank.
 * Bank 0 and 1 are the disk ROM. Bank >= 2 is the mounted image. */
static uint8_t        disk_b6000, disk_b8000, disk_bA000;

static void remap(void);

void slots_set_machine(uint16_t machine)
{
    s_machine = machine;
    if (g_bmsx_cfg.name[0] == '\0') {
        bmsx_cfg_load(NULL, machine, &g_bmsx_cfg);
    }
}

static int is_turbor(void)
{
    return g_bmsx_cfg.has_r800 ||
           (s_machine == BMSX_MACHINE_MSXTR_CBIOS ||
            s_machine == BMSX_MACHINE_MSXTR_CBIOS_FDD);
}

static int is_hd64180(void)
{
    return g_bmsx_cfg.has_hd64180 ||
           (s_machine >= BMSX_MACHINE_HC90_CBIOS &&
            s_machine <= BMSX_MACHINE_HC95_CBIOS_FDD);
}

static int is_msx2_or_higher(void)
{
    return s_machine != BMSX_MACHINE_MSX;
}

static int is_primary_subslotted(uint8_t prim)
{
    if (!is_msx2_or_higher()) return 0;
    if (prim < 4) return g_bmsx_cfg.subslotted[prim];
    return 0;
}

/* ---------------------------------------------------------------- Cartridge -- */

/* A 48 KiB image whose "AB" header sits at file+0x4000, with INIT inside
 * that same page, is a plain ROM that occupies CPU pages 0..2. C-BIOS only
 * looks for the header at $4000 and $8000, so the file must not be shown
 * from its first byte. A header at file 0, or anything past 48 KiB, stays
 * on the bank-write score below. */
static int plain_48k_from_0000(void)
{
    uint16_t init;

    if (rom_size <= 32768u || rom_size > 49152u) return 0;
    if (rom[0x4000] != (uint8_t)'A' || rom[0x4001] != (uint8_t)'B') return 0;
    init = (uint16_t)((uint16_t)rom[0x4002] | ((uint16_t)rom[0x4003] << 8));
    return init >= 0x4000u && init < 0x8000u;
}

static uint32_t calc_crc32(const uint8_t *buf, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= buf[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1));
        }
    }
    return ~crc;
}

typedef struct {
    uint32_t crc;
    int mapper;
} rom_crc_entry_t;

static const rom_crc_entry_t k_rom_crc_db[] = {
    { 0x1B86043C, MAPPER_RTYPE },
    { 0xA0DB3627, MAPPER_RTYPE },
    { 0x42DB7958, MAPPER_RTYPE },
    { 0x0041B1F6, MAPPER_GAMEMASTER2 },
    { 0x6F292B28, MAPPER_GAMEMASTER2 },
    { 0xB23F7E50, MAPPER_GAMEMASTER2 },
    { 0x5C2462BB, MAPPER_GAMEMASTER2 },
    { 0x0F49A115, MAPPER_CROSSBLAIM },
    { 0x24EE63A9, MAPPER_CROSSBLAIM },
    { 0x4B37666F, MAPPER_HARRYFOX },
    { 0xE283CE82, MAPPER_HARRYFOX },
    { 0x9D0EAA92, MAPPER_SUPERLODERUNNER },
    { 0xDFE0ECBE, MAPPER_SUPERLODERUNNER },
    { 0x633C1423, MAPPER_ZEMINA80 },
    { 0x7B9C73DF, MAPPER_ZEMINA90 },
    { 0xB4C6D58C, MAPPER_ZEMINA90 },
    { 0xB729CBA2, MAPPER_MAJUTSUSHI },
    { 0xE4B53298, MAPPER_MAJUTSUSHI },
    { 0x55E9E464, MAPPER_MANBOW2 },
    { 0x97E02DB7, MAPPER_MANBOW2 },
    { 0x7C9A4CE0, MAPPER_HALNOTE },
    { 0x323BA88B, MAPPER_HALNOTE },
    { 0x76878434, MAPPER_ASCII16_SRAM }, /* Hydlide 2 */
    { 0x9E7E0D07, MAPPER_ASCII16_SRAM },
    { 0x53951F59, MAPPER_ASCII16_SRAM }, /* Daisenryaku */
    { 0x54637D06, MAPPER_ASCII8_SRAM },  /* Wizardry */
    { 0xEF1253C4, MAPPER_ASCII8_SRAM },
    { 0x6A80538A, MAPPER_ASCII16 },      /* AUREL_VAULT.rom */
    { 0x03CE5577, MAPPER_ASCII16 },      /* BULLRUSH.ROM */
    { 0x5165277D, MAPPER_ASCII16 },      /* Bad Apple (MDPC)[ASCII16].rom */
    { 0x77E550F2, MAPPER_ASCII16 },      /* NEON_REVENANT-HC-Geo3D-256 */
    { 0x84C327F6, MAPPER_ASCII16 },      /* btron8.rom */
    { 0x69FCEAA9, MAPPER_ASCII16 },      /* NIGHT_RAVEN_FLIGHT_DEMO.ROM */
    { 0xCB2C9B3B, MAPPER_ASCII16 },      /* NIGHT_RAVEN_GEO3D.ROM */
};

static void cart_remap(void)
{
    uint32_t num_16k = rom_size >> 14;
    uint32_t num_8k  = rom_size >> 13;
    if (!num_16k) num_16k = 1;
    if (!num_8k) num_8k = 1;

    for (int i = 0; i < 8; i++) {
        cart_rview[i] = NULL;
        cart_wview[i] = NULL;
    }
    if (!rom_size) return;

    switch (cart_mapper) {
    case MAPPER_PLAIN:
        if (rom_size > 32768u && rom_size <= 49152u && plain_48k_from_0000()) {
            cart_rview[0] = rom;
            cart_rview[1] = rom + 0x2000;
            cart_rview[2] = rom + 0x4000;
            cart_rview[3] = rom + 0x6000;
            cart_rview[4] = rom + 0x8000;
            cart_rview[5] = rom + 0xA000;
        } else if (rom_size > 49152u) {
            /* 64 KiB linear */
            for (int i = 0; i < 8 && ((uint32_t)i << 13) < rom_size; i++) {
                cart_rview[i] = rom + ((uint32_t)i << 13);
            }
        } else if (rom_size <= 16384u) {
            /* 16 KiB cart: Page 1 and Page 2 mirrored */
            cart_rview[2] = rom;
            cart_rview[3] = rom + (0x2000 % rom_size);
            cart_rview[4] = rom;
            cart_rview[5] = rom + (0x2000 % rom_size);
        } else {
            /* 32 KiB cart: Page 1 = 0..16K, Page 2 = 16K..32K */
            cart_rview[2] = rom;
            cart_rview[3] = rom + 0x2000;
            cart_rview[4] = rom + 0x4000;
            cart_rview[5] = rom + 0x6000;
        }
        break;

    case MAPPER_ASCII16:
    case MAPPER_ASCII16_SRAM: {
        uint32_t b1 = cart_banks[1] % num_16k;
        uint32_t b2 = cart_banks[2] % num_16k;

        if (cart_mapper == MAPPER_ASCII16_SRAM && sram_enabled_b1) {
            /* Page 1 SRAM: read-only */
            cart_rview[2] = cart_sram;
            cart_rview[3] = cart_sram + 0x2000;
        } else {
            cart_rview[2] = rom + (b1 << 14);
            cart_rview[3] = rom + (b1 << 14) + 0x2000;
        }

        if (cart_mapper == MAPPER_ASCII16_SRAM && sram_enabled_b2) {
            /* Page 2 SRAM: read-write */
            cart_rview[4] = cart_sram;
            cart_rview[5] = cart_sram + 0x2000;
            cart_wview[4] = cart_sram;
            cart_wview[5] = cart_sram + 0x2000;
        } else {
            cart_rview[4] = rom + (b2 << 14);
            cart_rview[5] = rom + (b2 << 14) + 0x2000;
        }
        break;
    }

    case MAPPER_ASCII8:
    case MAPPER_ASCII8_SRAM: {
        for (int b = 0; b < 4; b++) {
            int blk = 2 + b;
            if (cart_mapper == MAPPER_ASCII8_SRAM && (ascii8_sram_mask & (1 << blk))) {
                uint32_t sram_off = ((uint32_t)ascii8_sram_bank[b] * 0x2000u) & (sizeof(cart_sram) - 1u);
                cart_rview[blk] = cart_sram + sram_off;
                cart_wview[blk] = cart_sram + sram_off;
            } else {
                uint32_t bk = cart_banks[b] % num_8k;
                cart_rview[blk] = rom + (bk << 13);
            }
        }
        break;
    }

    case MAPPER_KONAMI:
    case MAPPER_KONAMI_SCC:
    case MAPPER_KONAMI_SCC_PLUS:
    case MAPPER_MANBOW2:
    case MAPPER_MAJUTSUSHI: {
        for (int b = 0; b < 4; b++) {
            int blk = 2 + b;
            uint32_t bk = cart_banks[b] % num_8k;
            cart_rview[blk] = rom + (bk << 13);
        }
        break;
    }

    case MAPPER_GAMEMASTER2: {
        cart_rview[2] = rom; /* Bank 1 fixed at 0 */
        cart_rview[3] = rom + 0x2000;
        for (int b = 1; b < 4; b++) {
            int blk = 2 + b;
            if (gm2_sram_enabled[b]) {
                uint32_t sram_off = gm2_sram_page[b] ? 0x1000 : 0x0000;
                cart_rview[blk] = cart_sram + sram_off;
                if (b == 3) {
                    /* SRAM writeable in bank 4 (0xB000..0xBFFF) */
                    cart_wview[blk] = cart_sram + sram_off;
                }
            } else {
                uint32_t bk = cart_banks[b] % num_8k;
                cart_rview[blk] = rom + (bk << 13);
            }
        }
        break;
    }

    case MAPPER_RTYPE: {
        uint32_t b1 = 0x17u % num_16k;
        uint32_t b2 = cart_banks[2] % num_16k;
        cart_rview[2] = rom + (b1 << 14);
        cart_rview[3] = rom + (b1 << 14) + 0x2000;
        cart_rview[4] = rom + (b2 << 14);
        cart_rview[5] = rom + (b2 << 14) + 0x2000;
        break;
    }

    case MAPPER_ZEMINA80: {
        for (int b = 0; b < 4; b++) {
            int blk = 2 + b;
            uint32_t bk = cart_banks[b] % num_8k;
            cart_rview[blk] = rom + (bk << 13);
        }
        break;
    }

    case MAPPER_ZEMINA90: {
        for (int b = 0; b < 4; b++) {
            int blk = 2 + b;
            uint32_t bk = cart_banks[b] % num_8k;
            cart_rview[blk] = rom + (bk << 13);
        }
        break;
    }

    case MAPPER_CROSSBLAIM: {
        for (int i = 0; i < 8; i++) {
            uint8_t p = crossblaim_page[i >> 1];
            if (p != 0xFFu) {
                uint32_t pk = (uint32_t)p % num_16k;
                cart_rview[i] = rom + (pk << 14) + ((i & 1) ? 0x2000 : 0);
            } else {
                cart_rview[i] = NULL;
            }
        }
        break;
    }

    case MAPPER_NEO8: {
        for (int b = 0; b < 4; b++) {
            int blk = 2 + b;
            uint32_t bk = neo_reg[b] % num_8k;
            cart_rview[blk] = rom + (bk << 13);
        }
        break;
    }

    case MAPPER_NEO16: {
        uint32_t b1 = neo_reg[0] % num_16k;
        uint32_t b2 = neo_reg[1] % num_16k;
        cart_rview[2] = rom + (b1 << 14);
        cart_rview[3] = rom + (b1 << 14) + 0x2000;
        cart_rview[4] = rom + (b2 << 14);
        cart_rview[5] = rom + (b2 << 14) + 0x2000;
        break;
    }

    case MAPPER_SUPERLODERUNNER: {
        uint32_t b2 = cart_banks[2] % num_16k;
        cart_rview[4] = rom + (b2 << 14);
        cart_rview[5] = rom + (b2 << 14) + 0x2000;
        break;
    }

    case MAPPER_HARRYFOX: {
        uint32_t b1 = cart_banks[1] % num_16k;
        uint32_t b2 = cart_banks[2] % num_16k;
        cart_rview[2] = rom + (b1 << 14);
        cart_rview[3] = rom + (b1 << 14) + 0x2000;
        cart_rview[4] = rom + (b2 << 14);
        cart_rview[5] = rom + (b2 << 14) + 0x2000;
        break;
    }

    case MAPPER_HALNOTE: {
        if (halnote_sram_enabled) {
            cart_rview[0] = cart_sram;
            cart_rview[1] = cart_sram + 0x2000;
            cart_wview[0] = cart_sram;
            cart_wview[1] = cart_sram + 0x2000;
        }
        cart_rview[2] = rom + (((uint32_t)cart_banks[0] % num_8k) << 13);
        cart_rview[3] = rom + (((uint32_t)cart_banks[1] % num_8k) << 13);
        cart_rview[4] = rom + (((uint32_t)cart_banks[2] % num_8k) << 13);
        cart_rview[5] = rom + (((uint32_t)cart_banks[3] % num_8k) << 13);
        break;
    }

    default:
        break;
    }
}

static void detect_mapper(void)
{
    uint32_t i;
    int score_ascii16 = 0, score_ascii8 = 0, score_konami = 0, score_konami_scc = 0;
    int score_rtype = 0, score_zemina80 = 0, score_crossblaim = 0;

    cart_banks[0] = 0;
    cart_banks[1] = 0;
    cart_banks[2] = 0;
    cart_banks[3] = 0;
    sram_enabled_b1 = sram_enabled_b2 = 0;
    ascii8_sram_mask = 0;
    ascii8_sram_bit = (rom_size >= 0x40000u) ? 0x20 : 0x10;
    if (rom_size >= 0x80000u) ascii8_sram_bit = 0x40;
    memset(ascii8_sram_bank, 0, sizeof(ascii8_sram_bank));
    memset(gm2_sram_enabled, 0, sizeof(gm2_sram_enabled));
    memset(gm2_sram_page, 0, sizeof(gm2_sram_page));
    crossblaim_page[0] = 1; crossblaim_page[1] = 0; crossblaim_page[2] = 1; crossblaim_page[3] = 1;
    memset(neo_reg, 0, sizeof(neo_reg));
    scc_enable_manbow = 0;
    halnote_sram_enabled = halnote_sub_enabled = 0;
    majutsushi_dac = 0;

    if (!rom_size) {
        cart_mapper = MAPPER_PLAIN;
        cart_remap();
        return;
    }

    /* 1. Explicit header signatures at offset 16 (0x10) or offset 0 */
    if (rom_size >= 24) {
        if (!memcmp(rom + 16, "ASCII16X", 8)) {
            cart_mapper = MAPPER_ASCII16;
            cart_remap();
            return;
        }
        if (!memcmp(rom + 16, "ROM_NEO8", 8) || !memcmp(rom, "ROM_NEO8", 8)) {
            cart_mapper = MAPPER_NEO8;
            cart_remap();
            return;
        }
        if (!memcmp(rom + 16, "ROM_NE16", 8) || !memcmp(rom, "ROM_NE16", 8)) {
            cart_mapper = MAPPER_NEO16;
            cart_remap();
            return;
        }
    }

    /* 2. Known CRC32 database lookup */
    uint32_t crc = calc_crc32(rom, rom_size);
    for (size_t entry = 0; entry < sizeof(k_rom_crc_db) / sizeof(k_rom_crc_db[0]); entry++) {
        if (k_rom_crc_db[entry].crc == crc) {
            cart_mapper = k_rom_crc_db[entry].mapper;
            cart_remap();
            return;
        }
    }

    /* 3. Plain ROM check for small ROMs */
    if (rom_size <= 32768u || plain_48k_from_0000()) {
        cart_mapper = MAPPER_PLAIN;
        cart_remap();
        return;
    }

    /* 4. String search in first 512 bytes */
    uint32_t scan_hdr = (rom_size > 512) ? 512 : rom_size;
    for (i = 0; i + 8 <= scan_hdr; i++) {
        if (!memcmp(rom + i, "R-TYPE", 6)) { cart_mapper = MAPPER_RTYPE; cart_remap(); return; }
        if (!memcmp(rom + i, "CROSS BLAIM", 11)) { cart_mapper = MAPPER_CROSSBLAIM; cart_remap(); return; }
        if (!memcmp(rom + i, "RC751", 5) || !memcmp(rom + i, "GAME MASTER II", 14)) { cart_mapper = MAPPER_GAMEMASTER2; cart_remap(); return; }
        if (!memcmp(rom + i, "HALNOTE", 7)) { cart_mapper = MAPPER_HALNOTE; cart_remap(); return; }
        if (!memcmp(rom + i, "MANBOW 2", 8) || !memcmp(rom + i, "HAMARAJA", 8)) { cart_mapper = MAPPER_MANBOW2; cart_remap(); return; }
        if (!memcmp(rom + i, "SUPER LODE RUNNER", 17)) { cart_mapper = MAPPER_SUPERLODERUNNER; cart_remap(); return; }
        if (!memcmp(rom + i, "HARRY FOX", 9)) { cart_mapper = MAPPER_HARRYFOX; cart_remap(); return; }
        if (!memcmp(rom + i, "RC757", 5) || !memcmp(rom + i, "HAI NO MAJUTSUSHI", 17)) { cart_mapper = MAPPER_MAJUTSUSHI; cart_remap(); return; }
    }

    /* 5. Heuristic code scan for bank switch instructions */
    for (i = 0; i + 2 < rom_size && i < 0x20000u; i++) {
        if (rom[i] == 0x32) { /* LD (nn), A */
            uint16_t target = (uint16_t)(rom[i + 1] | (rom[i + 2] << 8));
            if (target == 0x6000 || target == 0x7000 || target == 0x77FF) score_ascii16 += 2;
            if (target == 0x6800 || target == 0x7800) score_ascii8 += 2;
            if (target == 0x5000 || target == 0x9000 || target == 0xB000) score_konami_scc += 2;
            if (target == 0x4000 || target == 0x8000 || target == 0xA000) score_konami += 2;
            if (target >= 0x4000 && target <= 0x4003) score_zemina80 += 3;
            if (target >= 0x4000 && target < 0x8000 && i > 0 && (rom[i-1] == 0x17 || rom[i-1] == 0x1F)) score_rtype += 3;
            if (target == 0x0000) score_crossblaim += 2;
        }
    }

    if (score_zemina80 > 6 && score_zemina80 > score_ascii16) {
        cart_mapper = MAPPER_ZEMINA80;
    } else if (score_rtype > 6 && score_rtype > score_ascii16) {
        cart_mapper = MAPPER_RTYPE;
    } else if (score_crossblaim > 6 && score_crossblaim > score_ascii16) {
        cart_mapper = MAPPER_CROSSBLAIM;
    } else if (score_konami_scc > score_ascii16 && score_konami_scc > score_ascii8) {
        cart_mapper = MAPPER_KONAMI_SCC;
    } else if (score_konami > score_ascii16 && score_konami > score_ascii8) {
        cart_mapper = MAPPER_KONAMI;
    } else if (score_ascii8 > score_ascii16) {
        cart_mapper = MAPPER_ASCII8;
    } else {
        cart_mapper = MAPPER_ASCII16;
    }

    cart_remap();
}

int slots_get_mapper(void)
{
    return cart_mapper;
}

void slots_set_mapper(int mapper)
{
    cart_mapper = mapper;
    cart_remap();
}

uint8_t *slots_get_sram(uint32_t *size)
{
    if (size) *size = sizeof(cart_sram);
    return cart_sram;
}

static void cart_write(uint16_t addr, uint8_t val)
{
    uint32_t num_16k = rom_size >> 14;
    uint32_t num_8k  = rom_size >> 13;
    if (!num_16k) num_16k = 1;
    if (!num_8k) num_8k = 1;

    switch (cart_mapper) {
    case MAPPER_ASCII16:
        if (addr >= 0x6000u && addr <= 0x67FFu) {
            cart_banks[1] = (uint8_t)(val % num_16k);
            cart_remap();
        } else if (addr >= 0x7000u && addr <= 0x77FFu) {
            cart_banks[2] = (uint8_t)(val % num_16k);
            cart_remap();
        }
        break;

    case MAPPER_ASCII16_SRAM:
        if (addr >= 0x6000u && addr <= 0x67FFu) {
            if (val == 0x10) {
                sram_enabled_b1 = 1;
            } else {
                sram_enabled_b1 = 0;
                cart_banks[1] = (uint8_t)(val % num_16k);
            }
            cart_remap();
        } else if (addr >= 0x7000u && addr <= 0x77FFu) {
            if (val == 0x10) {
                sram_enabled_b2 = 1;
            } else {
                sram_enabled_b2 = 0;
                cart_banks[2] = (uint8_t)(val % num_16k);
            }
            cart_remap();
        }
        break;

    case MAPPER_ASCII8:
        if (addr >= 0x6000u && addr < 0x6800u) {
            cart_banks[0] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x6800u && addr < 0x7000u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x7000u && addr < 0x7800u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x7800u && addr < 0x8000u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    case MAPPER_ASCII8_SRAM:
        if (addr >= 0x6000u && addr < 0x8000u) {
            uint8_t b = (uint8_t)((addr - 0x6000u) >> 11); /* 0..3 */
            int blk = 2 + b;
            if ((val & ascii8_sram_bit) || (val & 0x80u)) {
                ascii8_sram_mask |= (uint8_t)(1 << blk);
                ascii8_sram_bank[b] = (uint8_t)(val & 3);
            } else {
                ascii8_sram_mask &= (uint8_t)~(1 << blk);
                cart_banks[b] = (uint8_t)(val % num_8k);
            }
            cart_remap();
        }
        break;

    case MAPPER_KONAMI:
        if (addr >= 0x5000u && addr < 0x6000u) {
            cart_banks[0] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x7000u && addr < 0x8000u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x9000u && addr < 0xA000u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0xB000u && addr < 0xC000u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    case MAPPER_KONAMI_SCC:
    case MAPPER_KONAMI_SCC_PLUS:
        if (scc_write(addr, val)) break;
        if (addr >= 0x5000u && addr < 0x5800u) {
            cart_banks[0] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x7000u && addr < 0x7800u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x9000u && addr < 0x9800u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0xB000u && addr < 0xB800u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    case MAPPER_GAMEMASTER2:
        if (addr >= 0x6000u && addr < 0xB000u && !(addr & 0x1000u)) {
            uint8_t b = (uint8_t)(((addr >> 12) - 6) >> 1) + 1; /* region 0x6->1, 0x8->2, 0xA->3 */
            if (val & 0x10) {
                gm2_sram_enabled[b] = 1;
                gm2_sram_page[b] = (val & 0x20) ? 1 : 0;
            } else {
                gm2_sram_enabled[b] = 0;
                cart_banks[b] = val & 0x0F;
            }
            cart_remap();
        } else if (addr >= 0xB000u && addr < 0xC000u) {
            if (gm2_sram_enabled[3]) {
                uint32_t sram_off = (gm2_sram_page[3] ? 0x1000 : 0x0000) | (addr & 0x0FFF);
                cart_sram[sram_off] = val;
            }
        }
        break;

    case MAPPER_RTYPE:
        if (addr >= 0x4000u && addr < 0x8000u) {
            uint8_t p = val & ((val & 0x10) ? 0x17 : 0x1F);
            cart_banks[2] = (uint8_t)(p % num_16k);
            cart_remap();
        }
        break;

    case MAPPER_ZEMINA80:
        if (addr >= 0x4000u && addr <= 0x4003u) {
            cart_banks[addr - 0x4000u] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    case MAPPER_CROSSBLAIM:
        switch (val & 3) {
        case 0:
        case 1:
            crossblaim_page[0] = 1;
            crossblaim_page[1] = 0;
            crossblaim_page[2] = 1;
            crossblaim_page[3] = 1;
            break;
        case 2:
            crossblaim_page[0] = 0xFF;
            crossblaim_page[1] = 0;
            crossblaim_page[2] = 2;
            crossblaim_page[3] = 0xFF;
            break;
        case 3:
            crossblaim_page[0] = 0xFF;
            crossblaim_page[1] = 0;
            crossblaim_page[2] = 3;
            crossblaim_page[3] = 0xFF;
            break;
        }
        cart_remap();
        break;

    case MAPPER_NEO8:
        if (addr >= 0x5000u && addr < 0x8000u) {
            uint8_t bbb = (uint8_t)((addr >> 11) & 7);
            if (bbb >= 2) {
                uint8_t reg = bbb - 2;
                if (!(addr & 1)) {
                    neo_reg[reg] = (uint16_t)((neo_reg[reg] & 0xFF00) | val);
                } else {
                    neo_reg[reg] = (uint16_t)((neo_reg[reg] & 0x00FF) | ((val & 0x0F) << 8));
                }
                cart_remap();
            }
        }
        break;

    case MAPPER_NEO16:
        if (addr >= 0x5000u && addr < 0x8000u) {
            uint8_t bbb = (uint8_t)((addr >> 11) & 7);
            if (bbb >= 2 && !(bbb & 1)) {
                uint8_t reg = (bbb >> 1) - 1;
                if (!(addr & 1)) {
                    neo_reg[reg] = (uint16_t)((neo_reg[reg] & 0xFF00) | val);
                } else {
                    neo_reg[reg] = (uint16_t)((neo_reg[reg] & 0x00FF) | ((val & 0x0F) << 8));
                }
                cart_remap();
            }
        }
        break;

    case MAPPER_MANBOW2:
        if (scc_write(addr, val)) break;
        if ((addr & 0xF800u) == 0x9000u) {
            if ((val & 0x3F) == 0x3F) scc_enable_manbow = 1;
            else scc_enable_manbow = 0;
        }
        if ((addr & 0x1800u) == 0x1000u && addr >= 0x4000u && addr < 0xC000u) {
            uint8_t page = (uint8_t)((addr - 0x4000u) / 0x2000u);
            cart_banks[page] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    case MAPPER_SUPERLODERUNNER:
        if (addr == 0x0000u) {
            cart_banks[2] = (uint8_t)(val % num_16k);
            cart_remap();
        }
        break;

    case MAPPER_HARRYFOX:
        if (addr >= 0x6000u && addr < 0x7000u) {
            cart_banks[1] = (uint8_t)((2 * (val & 1)) + 0);
            cart_remap();
        } else if (addr >= 0x7000u && addr < 0x8000u) {
            cart_banks[2] = (uint8_t)((2 * (val & 1)) + 1);
            cart_remap();
        }
        break;

    case MAPPER_HALNOTE:
        if (addr == 0x4FFFu) {
            cart_banks[0] = val & 0x7F;
            halnote_sram_enabled = (val & 0x80) != 0;
            cart_remap();
        } else if (addr == 0x6FFFu) {
            cart_banks[1] = val & 0x7F;
            halnote_sub_enabled = (val & 0x80) != 0;
            cart_remap();
        } else if (addr == 0x8FFFu) {
            cart_banks[2] = val & 0x7F;
            cart_remap();
        } else if (addr == 0xAFFFu) {
            cart_banks[3] = val & 0x7F;
            cart_remap();
        }
        break;

    case MAPPER_MAJUTSUSHI:
        if (addr >= 0x5000u && addr < 0x6000u) {
            majutsushi_dac = val;
        } else if (addr >= 0x7000u && addr < 0x8000u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0x9000u && addr < 0xA000u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            cart_remap();
        } else if (addr >= 0xB000u && addr < 0xC000u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            cart_remap();
        }
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ Remap -- */

static void remap(void)
{
    uint8_t page;

    for (page = 0; page < 4; page++) {
        uint8_t prim = (uint8_t)((ppi_a >> (page * 2u)) & 3u);
        uint8_t ssec = (uint8_t)((sub[prim] >> (page * 2u)) & 3u);
        const uint8_t *r = NULL;
        uint8_t       *w = NULL;

        is_cart_page[page] = 0;
        is_mapper_page[page] = 0;
        is_disk_page[page] = 0;

        if (!is_msx2_or_higher()) {
            /* MSX1 mapping */
            switch (prim) {
            case SLOT_BIOS:
                if (page <= 1) {
                    uint32_t off = (uint32_t)page << 14;
                    if (off < bios_size) r = bios + off;
                } else if (page == 2 && logo_size) {
                    r = logo;
                }
                break;
            case SLOT_CART_A:
            case SLOT_CART_B:
                is_cart_page[page] = 1;
                break;
            case SLOT_RAM: {
                uint32_t off = (uint32_t)page << 14;
                r = ram + off;
                w = ram + off;
                break;
            }
            default:
                break;
            }
        } else if (g_bmsx_cfg.name[0] != '\0') {
            /* MSX2 / MSX2+ / Turbo R / HD64180 mapping driven by g_bmsx_cfg */
            if (prim == g_bmsx_cfg.cart_slot_a || prim == g_bmsx_cfg.cart_slot_b) {
                is_cart_page[page] = 1;
            } else if (prim == g_bmsx_cfg.main_slot && (!g_bmsx_cfg.subslotted[prim] || ssec == g_bmsx_cfg.main_subslot)) {
                if (page <= 1) {
                    uint32_t off = (uint32_t)page << 14;
                    if (off < bios_size) r = bios + off;
                } else if (page == 2 && logo_size) {
                    r = logo;
                }
            } else if (prim == g_bmsx_cfg.ram_slot && (!g_bmsx_cfg.subslotted[prim] || ssec == g_bmsx_cfg.ram_subslot)) {
                uint32_t seg = (uint32_t)(map_seg[page] & MSX_MAPPER_MASK);
                uint32_t off = seg << 14;
                if (off < MSX_MAPPER_RAM_SIZE) {
                    r = mapper_ram + off;
                    w = mapper_ram + off;
                    is_mapper_page[page] = 1;
                }
            }

            /* Sub-ROM */
            if (prim == g_bmsx_cfg.sub_slot && ssec == g_bmsx_cfg.sub_subslot && page == 0 && sub_size) {
                r = sub_rom;
            }
            /* MSX-MUSIC */
            if (prim == g_bmsx_cfg.music_slot && ssec == g_bmsx_cfg.music_subslot && (page == 1 || page == 2) && music_size) {
                r = music_rom;
            }
            /* Disk ROM */
            if (prim == g_bmsx_cfg.disk_slot && ssec == g_bmsx_cfg.disk_subslot && disk_size && (page == 1 || page == 2)) {
                is_disk_page[page] = 1;
            }
        } else if (is_turbor()) {
            /* MSX turbo R mapping */
            switch (prim) {
            case 0:
                if (ssec == 0) {
                    /* Main BIOS (pages 0 & 1), Logo (page 2) */
                    if (page <= 1) {
                        uint32_t off = (uint32_t)page << 14;
                        if (off < bios_size) r = bios + off;
                    } else if (page == 2 && logo_size) {
                        r = logo;
                    }
                } else if (ssec == 2 && page == 1 && music_size) {
                    r = music_rom;
                }
                break;

            case 1:
            case 2:
                is_cart_page[page] = 1;
                break;

            case 3:
                if (ssec == 0) {
                    /* Slot 3-0 is Main RAM (Memory Mapper: up to 4 MiB) */
                    uint32_t seg = (uint32_t)(map_seg[page] & MSX_MAPPER_MASK);
                    uint32_t off = seg << 14;
                    if (off < MSX_MAPPER_RAM_SIZE) {
                        r = mapper_ram + off;
                        w = mapper_ram + off;
                        is_mapper_page[page] = 1;
                    }
                } else if (ssec == 1 && page == 0 && sub_size) {
                    r = sub_rom;
                } else if (ssec == 3 && disk_size && (page == 1 || page == 2)) {
                    is_disk_page[page] = 1;
                }
                break;

            default:
                break;
            }
        } else {
            /* MSX2 and MSX2+ mapping */
            switch (prim) {
            case 0:
                if (page <= 1) {
                    uint32_t off = (uint32_t)page << 14;
                    if (off < bios_size) r = bios + off;
                } else if (page == 2 && logo_size) {
                    r = logo;
                }
                break;

            case 1:
            case 2:
                is_cart_page[page] = 1;
                break;

            case 3:
                if (ssec == 0 && page == 0 && sub_size) {
                    r = sub_rom;
                } else if (ssec == 1 && page == 1 && music_size) {
                    r = music_rom;
                } else if (ssec == 2) {
                    /* Main RAM: Memory Mapper up to 4 MiB */
                    uint32_t seg = (uint32_t)(map_seg[page] & MSX_MAPPER_MASK);
                    uint32_t off = seg << 14;
                    if (off < MSX_MAPPER_RAM_SIZE) {
                        r = mapper_ram + off;
                        w = mapper_ram + off;
                        is_mapper_page[page] = 1;
                    }
                } else if (ssec == 3 && disk_size && (page == 1 || page == 2)) {
                    is_disk_page[page] = 1;
                }
                break;

            default:
                break;
            }
        }

        rview[page] = r;
        wview[page] = w;
    }
}

/* ------------------------------------------------------------- Lifecycle -- */

void slots_init(void)
{
    memset(ram, 0, sizeof(ram));
    memset(mapper_ram, 0, sizeof(mapper_ram));
    memset(rom, 0xFF, sizeof(rom));
    memset(cart_sram, 0xFF, sizeof(cart_sram));
    memset(bios, 0, sizeof(bios));
    memset(logo, 0, sizeof(logo));
    memset(sub_rom, 0, sizeof(sub_rom));
    memset(music_rom, 0, sizeof(music_rom));
    memset(disk_rom, 0, sizeof(disk_rom));
    rom_size = bios_size = logo_size = sub_size = music_size = disk_size = 0;
    cart_mapper = MAPPER_PLAIN;
    disk_b6000 = 1;
    disk_b8000 = 0;
    disk_bA000 = 1;
    dsk_clear();
    s1985_init(&g_s1985);
    t9769b_init(&g_t9769b);
    t9769c_init(&g_t9769c);
    geo3d_init(&g_geo3d);
}

void slots_reset(void)
{
    ppi_a = 0x00u;
    sub[0] = sub[1] = sub[2] = sub[3] = 0x00u;
    map_seg[0] = 3;
    map_seg[1] = 2;
    map_seg[2] = 1;
    map_seg[3] = 0;
    cart_banks[0] = cart_banks[1] = cart_banks[2] = cart_banks[3] = 0;
    sram_enabled_b1 = sram_enabled_b2 = 0;
    ascii8_sram_mask = 0;
    memset(ascii8_sram_bank, 0, sizeof(ascii8_sram_bank));
    memset(gm2_sram_enabled, 0, sizeof(gm2_sram_enabled));
    memset(gm2_sram_page, 0, sizeof(gm2_sram_page));
    crossblaim_page[0] = 1; crossblaim_page[1] = 0; crossblaim_page[2] = 1; crossblaim_page[3] = 1;
    memset(neo_reg, 0, sizeof(neo_reg));
    scc_enable_manbow = 0;
    halnote_sram_enabled = halnote_sub_enabled = 0;
    majutsushi_dac = 0;
    cart_remap();
    disk_b6000 = 1;
    disk_b8000 = 0;
    disk_bA000 = 1;
    dsk_reset_open();
    msx_midi_reset();
    s1985_reset(&g_s1985);
    t9769b_reset(&g_t9769b);
    t9769c_reset(&g_t9769c);
    geo3d_reset(&g_geo3d);
    if (is_hd64180()) {
        hd64180_reset();
    }
    remap();
}

void slots_set_bios(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    bios_size = (size > MSX_BIOS_SIZE) ? MSX_BIOS_SIZE : size;
    for (i = 0; i < bios_size; i++) bios[i] = img[i];
    remap();
}

void slots_set_logo(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    logo_size = (size > MSX_LOGO_SIZE) ? MSX_LOGO_SIZE : size;
    for (i = 0; i < logo_size; i++) logo[i] = img[i];
    remap();
}

void slots_set_sub(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    sub_size = (size > MSX_SUBROM_SIZE) ? MSX_SUBROM_SIZE : size;
    for (i = 0; i < sub_size; i++) sub_rom[i] = img[i];
    remap();
}

void slots_set_music(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    music_size = (size > MSX_MUSIC_SIZE) ? MSX_MUSIC_SIZE : size;
    for (i = 0; i < music_size; i++) music_rom[i] = img[i];
    remap();
}

void slots_set_disk(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    disk_size = (size > MSX_DISK_SIZE) ? MSX_DISK_SIZE : size;
    for (i = 0; i < disk_size; i++) disk_rom[i] = img[i];
    remap();
}

/* C-BIOS bdos is at file+0x383 and its table at file+0x39D. The padding at
 * file+0x4C0 becomes OUT (D4),A / RET, and functions 0x0F and 0x27 point at it. */
static void disk_patch_bdos(void)
{
    if (disk_size < 0x4C3u) return;
    if (disk_rom[0x383] != 0x79u || disk_rom[0x384] != 0xFEu || disk_rom[0x385] != 0x31u)
        return;
    if (disk_rom[0x4C0] == 0xD3u && disk_rom[0x4C1] == 0xD4u && disk_rom[0x4C2] == 0xC9u)
        return;
    if (disk_rom[0x4C0] != 0xFFu || disk_rom[0x4C1] != 0xFFu || disk_rom[0x4C2] != 0xFFu)
        return;
    disk_rom[0x4C0] = 0xD3u;
    disk_rom[0x4C1] = 0xD4u;
    disk_rom[0x4C2] = 0xC9u;
    disk_rom[0x39D + 0x0F * 2] = 0xC0u;
    disk_rom[0x39D + 0x0F * 2 + 1] = 0x44u;
    disk_rom[0x39D + 0x27 * 2] = 0xC0u;
    disk_rom[0x39D + 0x27 * 2 + 1] = 0x44u;

    /* $44E0: page-0 RAM is the DOS image, so the IM 1 vector cannot stay in
     * the BIOS. The vector jumps here. Select the main ROM, run its KEYINT,
     * then put main RAM back in page 0. The subslot byte is filled below. */
    if (disk_rom[0x4E0] == 0xFFu) {
        static const uint8_t tramp[27] = {
            0xF5,
            0x3A, 0xFF, 0xFF,
            0x2F,
            0xE6, 0xFC,
            0xF6, 0x02,
            0x32, 0xFF, 0xFF,
            0xDB, 0xA8,
            0xF5,
            0xE6, 0xFC,
            0xD3, 0xA8,
            0xCD, 0x38, 0x00,
            0xF1,
            0xD3, 0xA8,
            0xF1,
            0xC9
        };
        uint32_t i;
        for (i = 0; i < sizeof(tramp); i++) disk_rom[0x4E0u + i] = tramp[i];
        disk_rom[0x4E8] = slots_ram_ssec();
    }
    /* MSXDOS.SYS calls $50A9 after it has installed the page-0 jumps.
     * C-BIOS left this byte as padding, so the call was an RST $38.
     * The following instructions reload HL from memory; a return is enough
     * to reach the resident checksum. */
    if (disk_rom[0x10A9] == 0xFFu) disk_rom[0x10A9] = 0xC9u;

    /* $4022 is `jp basic` / `scf`, and $4026 is FORMAT. A boot sector with
     * no MSXDOS.SYS jumps to $4022 after it has replaced SP. basic's RET
     * then lands in empty stack and the BIOS starts over, wiping the
     * sector. Stay halted; an IRQ returns to the JR, which halts again. */
    if (disk_rom[0x22] == 0xC3u) {
        disk_rom[0x22] = 0xF3u;
        disk_rom[0x23] = 0x76u;
        disk_rom[0x24] = 0x18u;
        disk_rom[0x25] = 0xFDu;
    }
}

int slots_set_dsk(const uint8_t *img, uint32_t size)
{
    if (!dsk_set_image(img, size)) return MSX_ERR_SIZE;
    disk_patch_bdos();
    return MSX_OK;
}

void slots_ram_store(uint16_t addr, uint8_t value)
{
    if (addr < 0x4000u && is_msx2_or_higher()) {
        uint32_t seg = (uint32_t)(map_seg[0] & MSX_MAPPER_MASK);
        uint32_t off = (seg << 14) | addr;
        if (off < MSX_MAPPER_RAM_SIZE) mapper_ram[off] = value;
        return;
    }
    slots_write(addr, value);
}

uint8_t slots_ram_ssec(void)
{
    return is_turbor() ? 0u : 2u;
}

void slots_map_com_pages(void)
{
    uint8_t ram = slots_ram_ssec();
    /* Same secondary on every page. Page 1 must be this RAM: the COM
     * reads that slot id and the vblank hook calls back through it. */
    sub[3] = (uint8_t)(ram | (ram << 2) | (ram << 4) | (ram << 6));
    ppi_a = 0xFFu;
    remap();
}

void slots_map_bios_ram(void)
{
    uint8_t ram = slots_ram_ssec();
    /* Page 0 stays the main BIOS, so RST $30 and CALL $0024 are the real
     * entries. Pages 1-3 are the mapper: a BLOAD image lives at $8000 or
     * above and banks page 2 through port $FE. */
    sub[3] = (uint8_t)(ram | (ram << 2) | (ram << 4) | (ram << 6));
    ppi_a = 0xFCu;
    remap();
}

void slots_map_disk_rom(void)
{
    uint8_t ram = slots_ram_ssec();
    /* Page 0: Main BIOS, Page 1: Disk ROM (Slot 3-3), Page 2-3: Main RAM */
    sub[3] = (uint8_t)(ram | (3 << 2) | (ram << 4) | (ram << 6));
    ppi_a = 0xFCu;
    remap();
}

void slots_set_rom(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    rom_size = (size > MSX_ROM_SIZE) ? MSX_ROM_SIZE : size;
    if (img != rom)                      /* img == rom: the app read straight into it */
        for (i = 0; i < rom_size; i++) rom[i] = img[i];
    for (i = rom_size; i < MSX_ROM_SIZE; i++) rom[i] = 0xFF;
    detect_mapper();
    scc_reset();
    remap();
}

uint8_t *slots_rom_ptr(uint32_t *cap)
{
    if (cap) *cap = MSX_ROM_SIZE + 1u;
    return rom;
}

uint8_t *slots_ram_ptr(uint32_t *size)
{
    if (size) *size = is_msx2_or_higher() ? MSX_MAPPER_RAM_SIZE : MSX_RAM_SIZE;
    return is_msx2_or_higher() ? mapper_ram : ram;
}

void slots_primary_select(uint8_t value)
{
    ppi_a = value;
    remap();
}

uint8_t slots_ppi_a(void)
{
    return ppi_a;
}

/* ---------------------------------------------------------------- Memory -- */

static uint8_t disk_rom_byte(uint8_t bank, uint16_t off)
{
    uint32_t o = ((uint32_t)bank << 13) + off;
    if (o >= disk_size) return 0xFFu;
    return disk_rom[o];
}

/* Page 1: $4000 is always bank 0, $6000 follows disk_b6000.
 * Page 2: $8000 follows disk_b8000, $A000 follows disk_bA000. */
static uint8_t disk_window_read(uint16_t addr)
{
    uint16_t off = (uint16_t)(addr & 0x3FFFu);
    uint8_t bank;

    if ((addr >> 14) == 1u) {
        if (off < 0x2000u) return disk_rom_byte(0, off);
        bank = disk_b6000;
        off = (uint16_t)(off - 0x2000u);
    } else if (off < 0x2000u) {
        bank = disk_b8000;
    } else {
        bank = disk_bA000;
        off = (uint16_t)(off - 0x2000u);
    }
    if (bank < 2u) return disk_rom_byte(bank, off);
    return dsk_bank_byte(bank, off);
}

uint8_t slots_read(uint16_t addr)
{
    uint8_t page = (uint8_t)(addr >> 14);

    /* Secondary slot register read at 0xFFFF */
    if (addr == 0xFFFFu) {
        uint8_t prim = (uint8_t)((ppi_a >> 6u) & 3u);
        if (is_primary_subslotted(prim)) {
            return (uint8_t)~sub[prim];
        }
    }

    if (is_cart_page[page]) {
        uint8_t blk = (uint8_t)(addr >> 13);
        if (cart_mapper == MAPPER_KONAMI_SCC || cart_mapper == MAPPER_KONAMI_SCC_PLUS ||
            (cart_mapper == MAPPER_MANBOW2 && scc_enable_manbow)) {
            uint8_t sv;
            if (scc_read(addr, &sv)) return sv;
        }
        if (cart_mapper == MAPPER_GAMEMASTER2 && blk >= 2 && blk <= 5) {
            uint8_t b = (uint8_t)(blk - 2);
            if (gm2_sram_enabled[b]) {
                uint32_t sram_off = (gm2_sram_page[b] ? 0x1000u : 0x0000u) | (addr & 0x0FFFu);
                return cart_sram[sram_off];
            }
        }
        if (cart_rview[blk]) {
            return cart_rview[blk][addr & 0x1FFFu];
        }
        return 0xFFu;
    }

    if (is_mapper_page[page]) {
        uint32_t seg = (uint32_t)(map_seg[page] & MSX_MAPPER_MASK);
        uint32_t off = (seg << 14) | (addr & 0x3FFFu);
        if (off < MSX_MAPPER_RAM_SIZE) return mapper_ram[off];
        return 0xFFu;
    }

    if (is_disk_page[page]) return disk_window_read(addr);

    if (rview[page]) {
        return rview[page][addr & 0x3FFFu];
    }

    return 0xFFu;
}

void slots_write(uint16_t addr, uint8_t value)
{
    uint8_t page = (uint8_t)(addr >> 14);

    /* Secondary slot register write at 0xFFFF */
    if (addr == 0xFFFFu) {
        uint8_t prim = (uint8_t)((ppi_a >> 6u) & 3u);
        if (is_primary_subslotted(prim)) {
            sub[prim] = value;
            remap();
            return;
        }
    }

    /* Disk bank select. Only the three addresses, so an LDIR across the
     * window does not change banks. */
    if (is_disk_page[page]) {
        if (addr == 0x6000u) { disk_b6000 = value; return; }
        if (addr == 0x8000u) { disk_b8000 = value; return; }
        if (addr == 0xA000u) { disk_bA000 = value; return; }
        /* Direct sector write support for mapped sector window (bank >= 2) */
        uint8_t bank = (page == 1) ? disk_b6000 : ((addr < 0xA000u) ? disk_b8000 : disk_bA000);
        if (bank >= 2u) {
            uint16_t off = (uint16_t)(addr & 0x1FFFu);
            dsk_write_bank_byte(bank, off, value);
        }
        return;
    }

    /* Cartridges that snoop writes globally */
    if (cart_mapper == MAPPER_SUPERLODERUNNER && addr == 0x0000u) {
        cart_write(addr, value);
        return;
    }
    if (cart_mapper == MAPPER_CROSSBLAIM) {
        cart_write(addr, value);
        return;
    }

    /* Cartridge banking write and SRAM writes */
    if (is_cart_page[page]) {
        uint8_t blk = (uint8_t)(addr >> 13);
        if (cart_wview[blk]) {
            cart_wview[blk][addr & 0x1FFFu] = value;
        }
        cart_write(addr, value);
        return;
    }

    /* Mapper RAM write */
    if (is_mapper_page[page]) {
        uint32_t seg = (uint32_t)(map_seg[page] & MSX_MAPPER_MASK);
        uint32_t off = (seg << 14) | (addr & 0x3FFFu);
        if (off < MSX_MAPPER_RAM_SIZE) mapper_ram[off] = value;
        return;
    }

    if (wview[page]) {
        wview[page][addr & 0x3FFFu] = value;
    }
}

/* ------------------------------------------------------------------- Bus -- */

static int is_ppi(uint8_t port, uint8_t *reg)
{
    if ((port & 0xFCu) == 0xA8u) { *reg = (uint8_t)(port & 3u); return 1; }
    return 0;
}

uint8_t msx_bus_in(uint8_t port)
{
    uint8_t reg;

    if (is_hd64180() && hd64180_is_internal_io(port)) return hd64180_io_read(port);
    if (port >= 0x2Cu && port <= 0x2Eu) return kbd_mouse_in((uint8_t)(port - 0x2Cu));
    if (port >= 0x88u && port <= 0x8Bu) return vdp_read(port);
    if (port >= 0x98u && port <= 0x9Bu) return vdp_read(port);
    if (port == 0x8Du || port == 0x9Du || port == 0x8Fu || port == 0x9Fu) {
        return geo3d_read(&g_geo3d, port);
    }
    if (port >= 0xA0u && port <= 0xA3u) return psg_read(port);
    if (port >= 0xC0u && port <= 0xC3u) return y8950_read(port);
    if (port >= 0xE8u && port <= 0xEFu) return msx_midi_read(port);
    if (is_ppi(port, &reg))             return ppi_read(reg);

    if (is_turbor()) {
        return t9769c_read(&g_t9769c, port);
    } else if (s_machine >= BMSX_MACHINE_MSX2P_CBIOS) {
        return t9769b_read(&g_t9769b, port);
    } else if (s_machine >= BMSX_MACHINE_MSX2_CBIOS) {
        return s1985_read(&g_s1985, port);
    }

    if (port >= 0xFCu) return (uint8_t)(map_seg[port & 3] | (uint8_t)~MSX_MAPPER_MASK);
    return 0xFFu;
}

void msx_bus_out(uint8_t port, uint8_t value)
{
    uint8_t reg;

    if (is_hd64180() && hd64180_is_internal_io(port)) { hd64180_io_write(port, value); return; }
    if (port >= 0x88u && port <= 0x8Bu) { vdp_write(port, value); return; }
    if (port >= 0x98u && port <= 0x9Bu) { vdp_write(port, value); return; }
    if (port == 0x8Du || port == 0x9Du || port == 0x8Fu || port == 0x9Fu) {
        if (!g_geo3d.vdp) geo3d_attach_vdp(&g_geo3d, msx_get_v9938());
        geo3d_write(&g_geo3d, port, value);
        return;
    }
    if (port == 0x2Eu || port == 0x2Fu) {
        if (port == 0x2Fu) fputc(value, stderr);
        else if (value == 0) fputc('\n', stderr);
        return;
    }
    if (port >= 0xA0u && port <= 0xA3u) { psg_write(port, value); return; }
    if (port >= 0xC0u && port <= 0xC3u) { y8950_write(port, value); return; }
    if (port >= 0xE8u && port <= 0xEFu) { msx_midi_write(port, value); return; }
    if (port == 0x77u && cart_mapper == MAPPER_ZEMINA90) {
        uint8_t page = (uint8_t)(2 * (value & 0x3F));
        switch (value & 0xC0) {
        case 0x00:
        case 0x40:
            cart_banks[0] = page + 0;
            cart_banks[1] = page + 1;
            cart_banks[2] = page + 0;
            cart_banks[3] = page + 1;
            break;
        case 0x80:
            cart_banks[0] = (uint8_t)((page & ~2) + 0);
            cart_banks[1] = (uint8_t)((page & ~2) + 1);
            cart_banks[2] = (uint8_t)((page & ~2) + 2);
            cart_banks[3] = (uint8_t)((page & ~2) + 3);
            break;
        case 0xC0:
            cart_banks[0] = page + 0;
            cart_banks[1] = page + 1;
            cart_banks[2] = page + 1;
            cart_banks[3] = page + 0;
            break;
        }
        cart_remap();
        return;
    }
    if (port == 0x7Cu || port == 0x7Du) {
        /* MSX2+ and turboR forward from the engine latch. MSX / MSX2 see an
         * FM-PAC here, the same OPLL object. */
        if (!is_turbor() && s_machine < BMSX_MACHINE_MSX2P_CBIOS) {
            if (port == 0x7Cu) opll_address(value);
            else opll_data(value);
            return;
        }
    }
    if (is_ppi(port, &reg)) {
        if (reg == 0u) slots_primary_select(value);
        ppi_write(reg, value);
        return;
    }

    if (is_turbor()) {
        t9769c_write(&g_t9769c, port, value);
    } else if (s_machine >= BMSX_MACHINE_MSX2P_CBIOS) {
        t9769b_write(&g_t9769b, port, value);
    } else if (s_machine >= BMSX_MACHINE_MSX2_CBIOS) {
        s1985_write(&g_s1985, port, value);
    }

    if (port >= 0xFCu) {
        map_seg[port & 3] = (uint8_t)(value & MSX_MAPPER_MASK);
        remap();
    }
}
