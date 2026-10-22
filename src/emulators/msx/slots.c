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

#define SLOT_BIOS    0u
#define SLOT_CART_A  1u
#define SLOT_CART_B  2u
#define SLOT_RAM     3u

enum {
    MAPPER_NONE = 0,
    MAPPER_PLAIN,
    MAPPER_ASCII16,
    MAPPER_ASCII8,
    MAPPER_KONAMI,
    MAPPER_KONAMI_SCC
};

s1985_t g_s1985;
t9769b_t g_t9769b;
t9769c_t g_t9769c;
geo3d_t g_geo3d;
extern v9938_t *msx_get_v9938(void);
static uint16_t s_machine = BMSX_MACHINE_MSX;

/* Memory pools */
static uint8_t  ram[MSX_RAM_SIZE];                     /* 64 KiB for MSX1 */
static uint8_t  mapper_ram[MSX_MAPPER_RAM_SIZE];       /* 512 KiB for MSX2/2+/TR */
static uint8_t  rom[MSX_ROM_SIZE + 1];                 /* cartridge (+1 spare byte lets the app tell an over-cap image from one that just fits) */
static uint8_t  bios[MSX_BIOS_SIZE];                   /* 32-64 KiB main BIOS */
static uint8_t  logo[MSX_LOGO_SIZE];                   /* 16 KiB logo */
static uint8_t  sub_rom[MSX_SUBROM_SIZE];              /* 16 KiB sub-ROM */
static uint8_t  music_rom[MSX_MUSIC_SIZE];            /* 16 KiB MSX-MUSIC */
static uint8_t  disk_rom[MSX_DISK_SIZE];               /* Up to 64 KiB FDD/Nextor */

static uint32_t rom_size, bios_size, logo_size;
static uint32_t sub_size, music_size, disk_size;

/* Cartridge banking */
static int      cart_mapper = MAPPER_PLAIN;
static uint8_t  cart_banks[4];

/* Slot registers */
static uint8_t  ppi_a;           /* #A8: primary slot select per page */
static uint8_t  sub[4];          /* Secondary slot select per primary slot */
static uint8_t  map_seg[4];      /* #FC-#FF mapper segments */

/* Direct page views */
static const uint8_t *rview[4];
static uint8_t       *wview[4];
static uint8_t        is_cart_page[4];
static uint8_t        is_mapper_page[4];

static void remap(void);

void slots_set_machine(uint16_t machine)
{
    s_machine = machine;
}

static int is_turbor(void)
{
    return (s_machine == BMSX_MACHINE_MSXTR_CBIOS ||
            s_machine == BMSX_MACHINE_MSXTR_CBIOS_FDD);
}

static int is_msx2_or_higher(void)
{
    return s_machine != BMSX_MACHINE_MSX;
}

static int is_primary_subslotted(uint8_t prim)
{
    if (!is_msx2_or_higher()) return 0;
    if (prim == 3) return 1;
    if (prim == 0 && is_turbor()) return 1;
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

static void detect_mapper(void)
{
    uint32_t i;
    int score_ascii16 = 0, score_ascii8 = 0, score_konami = 0;

    if (rom_size <= 32768u || plain_48k_from_0000()) {
        cart_mapper = MAPPER_PLAIN;
        cart_banks[0] = 0;
        cart_banks[1] = 0;
        cart_banks[2] = (rom_size > 16384u) ? 1 : 0;
        cart_banks[3] = 0;
        return;
    }

    /* Scan code for mapper bank writes */
    for (i = 0; i + 2 < rom_size && i < 0x20000u; i++) {
        if (rom[i] == 0x32) { /* LD (nn), A */
            uint16_t target = (uint16_t)(rom[i + 1] | (rom[i + 2] << 8));
            if (target == 0x6000 || target == 0x7000 || target == 0x77FF) score_ascii16 += 2;
            if (target == 0x6800 || target == 0x7800) score_ascii8 += 2;
            if (target == 0x5000 || target == 0x9000 || target == 0xB000) score_konami += 2;
        }
    }

    if (score_konami > score_ascii16 && score_konami > score_ascii8) {
        cart_mapper = MAPPER_KONAMI_SCC;
    } else if (score_ascii8 > score_ascii16) {
        cart_mapper = MAPPER_ASCII8;
    } else {
        cart_mapper = MAPPER_ASCII16;
    }

    cart_banks[0] = 0;
    cart_banks[1] = 0;
    cart_banks[2] = 0;
    cart_banks[3] = 0;
}

static const uint8_t *get_cart_view(uint8_t page)
{
    uint32_t num_16k = rom_size >> 14;
    uint32_t num_8k  = rom_size >> 13;
    if (!rom_size) return NULL;
    if (!num_16k) num_16k = 1;
    if (!num_8k) num_8k = 1;

    switch (cart_mapper) {
    case MAPPER_PLAIN:
        /* 48 KiB plain-from-0000. 16/32 KiB carts still start at page 1. */
        if (rom_size > 32768u) {
            if (page < 3u) return rom + ((uint32_t)page << 14);
            return NULL;
        }
        if (page == 1) return rom;
        if (page == 2) {
            if (rom_size > 16384u) return rom + 16384u;
            return rom; /* 16 KiB cart mirror */
        }
        return NULL;

    case MAPPER_ASCII16:
        if (page == 1) {
            uint32_t b = cart_banks[1] % num_16k;
            return rom + (b << 14);
        }
        if (page == 2) {
            uint32_t b = cart_banks[2] % num_16k;
            return rom + (b << 14);
        }
        return NULL;

    case MAPPER_ASCII8:
    case MAPPER_KONAMI:
    case MAPPER_KONAMI_SCC:
        if (page == 1) {
            uint32_t b = cart_banks[1] % num_8k;
            return rom + (b << 13);
        }
        if (page == 2) {
            uint32_t b = cart_banks[2] % num_8k;
            return rom + (b << 13);
        }
        return NULL;

    default:
        return NULL;
    }
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
            remap();
        } else if (addr >= 0x7000u && addr <= 0x77FFu) {
            cart_banks[2] = (uint8_t)(val % num_16k);
            remap();
        }
        break;

    case MAPPER_ASCII8:
        if (addr >= 0x6000u && addr < 0x6800u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0x7000u && addr < 0x7800u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            remap();
        }
        break;

    case MAPPER_KONAMI:
        if (addr >= 0x5000u && addr < 0x6000u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0x7000u && addr < 0x8000u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0x9000u && addr < 0xA000u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0xB000u && addr < 0xC000u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            remap();
        }
        break;

    case MAPPER_KONAMI_SCC:
        /* SCC / SCC-I window wins over the bank alias that shares #98xx/#B8xx. */
        if (scc_write(addr, val)) break;
        if (addr >= 0x5000u && addr < 0x5800u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0x7000u && addr < 0x7800u) {
            cart_banks[1] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0x9000u && addr < 0x9800u) {
            cart_banks[2] = (uint8_t)(val % num_8k);
            remap();
        } else if (addr >= 0xB000u && addr < 0xB800u) {
            cart_banks[3] = (uint8_t)(val % num_8k);
            remap();
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
                r = get_cart_view(page);
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
                r = get_cart_view(page);
                is_cart_page[page] = 1;
                break;

            case 3:
                if (ssec == 0) {
                    /* Slot 3-0 is Main RAM (Memory Mapper: 512 KiB) */
                    uint32_t seg = (uint32_t)(map_seg[page] & 0x1Fu);
                    uint32_t off = seg << 14;
                    r = mapper_ram + off;
                    w = mapper_ram + off;
                    is_mapper_page[page] = 1;
                } else if (ssec == 1 && page == 0 && sub_size) {
                    r = sub_rom;
                } else if (ssec == 3 && page == 1 && disk_size) {
                    r = disk_rom;
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
                r = get_cart_view(page);
                is_cart_page[page] = 1;
                break;

            case 3:
                if (ssec == 0 && page == 0 && sub_size) {
                    r = sub_rom;
                } else if (ssec == 1 && page == 1 && music_size) {
                    r = music_rom;
                } else if (ssec == 2) {
                    /* Main RAM: Memory Mapper 512 KiB */
                    uint32_t seg = (uint32_t)(map_seg[page] & 0x1Fu);
                    uint32_t off = seg << 14;
                    r = mapper_ram + off;
                    w = mapper_ram + off;
                    is_mapper_page[page] = 1;
                } else if (ssec == 3 && page == 1 && disk_size) {
                    r = disk_rom;
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
    memset(bios, 0, sizeof(bios));
    memset(logo, 0, sizeof(logo));
    memset(sub_rom, 0, sizeof(sub_rom));
    memset(music_rom, 0, sizeof(music_rom));
    memset(disk_rom, 0, sizeof(disk_rom));
    rom_size = bios_size = logo_size = sub_size = music_size = disk_size = 0;
    cart_mapper = MAPPER_PLAIN;
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
    s1985_reset(&g_s1985);
    t9769b_reset(&g_t9769b);
    t9769c_reset(&g_t9769c);
    geo3d_reset(&g_geo3d);
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

    if (is_cart_page[page] && cart_mapper == MAPPER_KONAMI_SCC) {
        uint8_t sv;
        if (scc_read(addr, &sv)) return sv;
    }

    if (is_mapper_page[page]) {
        uint32_t seg = (uint32_t)(map_seg[page] & 0x1Fu);
        uint32_t off = (seg << 14) | (addr & 0x3FFFu);
        return mapper_ram[off];
    }

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

    /* Cartridge banking write */
    if (is_cart_page[page]) {
        cart_write(addr, value);
        return;
    }

    /* Mapper RAM write */
    if (is_mapper_page[page]) {
        uint32_t seg = (uint32_t)(map_seg[page] & 0x1Fu);
        uint32_t off = (seg << 14) | (addr & 0x3FFFu);
        mapper_ram[off] = value;
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

    if (port >= 0x88u && port <= 0x8Bu) return vdp_read(port);
    if (port >= 0x98u && port <= 0x9Bu) return vdp_read(port);
    if (port == 0x8Du || port == 0x9Du || port == 0x8Fu || port == 0x9Fu) {
        return geo3d_read(&g_geo3d, port);
    }
    if (port >= 0xA0u && port <= 0xA3u) return psg_read(port);
    if (port >= 0xC0u && port <= 0xC3u) return y8950_read(port);
    if (is_ppi(port, &reg))             return ppi_read(reg);

    if (is_turbor()) {
        return t9769c_read(&g_t9769c, port);
    } else if (s_machine >= BMSX_MACHINE_MSX2P_CBIOS) {
        return t9769b_read(&g_t9769b, port);
    } else if (s_machine >= BMSX_MACHINE_MSX2_CBIOS) {
        return s1985_read(&g_s1985, port);
    }

    if (port >= 0xFCu) return (uint8_t)(map_seg[port & 3] | 0xE0u);
    return 0xFFu;
}

void msx_bus_out(uint8_t port, uint8_t value)
{
    uint8_t reg;

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
        map_seg[port & 3] = (uint8_t)(value & 0x1Fu);
        remap();
    }
}
