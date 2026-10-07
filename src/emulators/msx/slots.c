/*
 * tronMSX — slot system, canonical memory map and the bus port decode.
 * B-MSX Rev 1.05, §1.2 and §1.3.  Clean-room C99, MIT.
 *
 * Two pointer tables (read view, write view) are recomputed whenever a select
 * register is written, so the Z80 read/write path is one indexed load with no
 * switch in it.  A page with a NULL read view is an unimplemented device and
 * returns the floating-bus value 0xC0; a page with a NULL write view is ROM
 * and discards the store (§1.2, never a fault).
 *
 * Slot placement is the C-BIOS machine definition, measured from its MSX1 and
 * MSX2 configs: primary slot 0 carries the BIOS (32 KiB at 0x0000-0x7FFF) and
 * the logo ROM (page 2), slots 1 and 2 are the external cartridge slots, and
 * main RAM sits in primary slot 3.
 *
 * Port ownership, as the MSX standard wires it (grauw's I/O map, confirmed
 * against two independent emulators):
 *   #88-#8B, #98-#9B  VDP        #A0-#A3  PSG (only #A0/#A1 write, #A2 reads)
 *   #A8-#AB           8255 PPI (decoded exactly, the low two bits pick the reg)
 *   #DC-#DF           SCC/Konami (later)   #F8-#FB  MSX2 sub-slot selects
 *   #FC               EXTSLS     #FD-#FF  mapper segments (MSX2+ mapper, and
 *                              the ASCII8 cart ports)
 * MSX1 has no sub-slots, so #F8-#FF are latched and readable but cannot remap
 * anything: the only slot selector on an MSX1 is PPI port A.  In particular
 * #FE/#FF are not PSG port aliases and must never touch sound or slots.
 */

#include "msx_int.h"

#define SLOT_BIOS   0u
#define SLOT_CART_A 1u
#define SLOT_CART_B 2u
#define SLOT_RAM    3u

static uint8_t  ram[MSX_RAM_SIZE];
static uint8_t  rom[MSX_ROM_SIZE];
static uint8_t  bios[MSX_BIOS_SIZE];
static uint8_t  logo[MSX_LOGO_SIZE];

static uint32_t rom_size, bios_size, logo_size;

static const uint8_t *rview[4];   /* read view per page   */
static uint8_t       *wview[4];   /* write view per page, NULL for ROM */

static uint8_t ppi_a;           /* PPI port A, #A8: the MSX1 slot selector   */
static uint8_t sub[4];          /* #F8-#FB, MSX2 sub-slots - inert on MSX1    */
static uint8_t extsls;          /* #FC                                        */
static uint8_t map_seg[3];      /* #FD-#FF mapper segments, read back only    */

/* ------------------------------------------------------------------ map -- */

static void map_page(uint8_t page, const uint8_t *r, uint8_t *w)
{
    rview[page] = r;
    wview[page] = w;
}

static const uint8_t *bios_view(uint8_t page)
{
    uint32_t off = (uint32_t)page << 14;
    if (off < bios_size) return bios + off;
    if (page == 2 && logo_size) return logo;
    if (page == 3 && logo_size > 0x4000u) return logo + 0x4000u;
    return 0;
}

static const uint8_t *rom_view(uint8_t page)
{
    if (!rom_size) return 0;
    if (page <= 1) return rom;                       /* block 0 at 0x4000   */
    if (rom_size > MSX_ROM_SIZE / 2u) return rom + (MSX_ROM_SIZE / 2u);
    return rom;                                      /* 16 KiB cart mirrors */
}

static void remap(void)
{
    uint8_t page;
    map_page(0, bios, 0);                 /* page 0 is fixed to primary 0   */

    for (page = 1; page < 4; page++) {
        uint8_t prim = (uint8_t)((ppi_a >> (page * 2u)) & 3u);
        const uint8_t *r = 0;
        uint8_t *w = 0;

        /* MSX1 has no sub-slots, so the primary number is the effective one;
         * #F8-#FB are stored for the MSX2 step and change nothing here. */
        switch (prim) {
        case SLOT_BIOS:   r = bios_view(page);              break;
        case SLOT_CART_A: r = rom_view(page);               break;
        case SLOT_RAM: {
            /* Slot 3 is one full 64 KiB pool (C-BIOS_MSX1.xml: base 0x0000,
             * size 0x10000), so page N of slot 3 views ram[N*0x4000].  This
             * keeps every page a distinct, non-overlapping 16 KiB window. */
            uint32_t off = (uint32_t)page << 14;
            r = ram + off;
            w = ram + off;
            break;
        }
        default:          r = 0;                            break;  /* 0xC0 */
        }
        map_page(page, r, w);
    }
}

void slots_init(void)
{
    uint32_t i;
    for (i = 0; i < MSX_RAM_SIZE; i++) ram[i] = 0x00;   /* D3 */
    for (i = 0; i < MSX_ROM_SIZE; i++) rom[i] = 0xFF;
}

void slots_reset(void)
{
    /* Power-up: the primary slot register is 0, so all four pages select
     * slot 0.  This is what C-BIOS expects: its 32 KiB main ROM must be
     * visible across pages 0 and 1 (0x0000-0x7FFF) before it probes the
     * cartridge slots and ENASLTs RAM (slot 3) and the cart (slot 1/2) in.
     * A non-zero reset here shadows the upper BIOS half with the cartridge
     * and the boot trace never reaches INIT80. */
    ppi_a = 0x00u;
    sub[0] = sub[1] = sub[2] = sub[3] = 0u;
    extsls = 0u;
    map_seg[0] = map_seg[1] = map_seg[2] = 0u;
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

void slots_set_rom(const uint8_t *img, uint32_t size)
{
    uint32_t i;
    rom_size = (size > MSX_ROM_SIZE) ? MSX_ROM_SIZE : size;
    for (i = 0; i < rom_size; i++) rom[i] = img[i];
    for (i = rom_size; i < MSX_ROM_SIZE; i++) rom[i] = 0xFF;
    remap();
}

uint8_t *slots_ram_ptr(uint32_t *size)
{
    if (size) *size = MSX_RAM_SIZE;
    return ram;
}

void slots_primary_select(uint8_t value)
{
    ppi_a = value;
    remap();
}

/* --------------------------------------------------------------- memory -- */

uint8_t slots_read(uint16_t addr)
{
    const uint8_t *p = rview[addr >> 14];
    return p ? p[addr & 0x3FFFu] : 0xC0u;
}

void slots_write(uint16_t addr, uint8_t value)
{
    uint8_t *p = wview[addr >> 14];
    if (p) p[addr & 0x3FFFu] = value;      /* ROM stores vanish, never fault */
}

/* ----------------------------------------------------------------- bus -- */
/* §1.3.  The 8255 decodes #A8-#AB and nothing else: the low two bits pick the
 * register.  The #10/#50/#90/#D0 group is the cartridge's second PSG and the
 * Centronics port, not the PPI. */

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
    if (port >= 0xA0u && port <= 0xA3u) return psg_read(port);
    if (is_ppi(port, &reg))             return ppi_read(reg);

    switch (port) {
    case 0xDCu: case 0xDDu: case 0xDEu: case 0xDFu: return 0xFFu; /* MegaSCC: later */
    case 0xF8u: return sub[0];
    case 0xF9u: return sub[1];
    case 0xFAu: return sub[2];
    case 0xFBu: return sub[3];
    case 0xFCu: return extsls;
    case 0xFDu: return map_seg[0];
    case 0xFEu: return map_seg[1];
    case 0xFFu: return map_seg[2];
    default:    return 0xC0u;
    }
}

void msx_bus_out(uint8_t port, uint8_t value)
{
    uint8_t reg;

    if (port >= 0x88u && port <= 0x8Bu) { vdp_write(port, value); return; }
    if (port >= 0x98u && port <= 0x9Bu) { vdp_write(port, value); return; }
    if (port >= 0xA0u && port <= 0xA3u) { psg_write(port, value); return; }
    if (is_ppi(port, &reg)) {
        if (reg == 0u) slots_primary_select(value);   /* the MSX1 slot law    */
        ppi_write(reg, value);
        return;
    }

    switch (port) {
    case 0xF8u: sub[0] = value; break;
    case 0xF9u: sub[1] = value; break;
    case 0xFAu: sub[2] = value; break;
    case 0xFBu: sub[3] = value; break;
    case 0xFCu: extsls = value; break;
    case 0xFDu: map_seg[0] = value; break;
    case 0xFEu: map_seg[1] = value; break;
    case 0xFFu: map_seg[2] = value; break;
    default: break;                          /* writes to unhandled ports vanish */
    }
}

uint8_t slots_ppi_a(void) { return ppi_a; }
