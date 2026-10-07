/*
 * tronMSX - VDP: register file, 128 KiB VRAM, bus ports, raster lines and the
 * RGB565 renderer for SCREEN 0..5.  B-MSX Rev 1.05, sections 2.2 and 2.3.
 *
 * Clean-room C99, written from the MSX hardware documentation plus the three
 * bus facts measured off C-BIOS in Part 1:
 *   - the VDP answers both 0x88-0x8B and 0x98-0x9B, always data / status /
 *     palette / register in that relative order;
 *   - the periodic source is the frame interrupt flag FG (status bit7), set at
 *     the end of the display area and cleared by a status read - MSX has no
 *     periodic NMI (section 8.1, D6);
 *   - enabling the frame interrupt while FG is already up raises the IRQ at
 *     once, which is why vdp_write() re-checks the latch.
 *
 * Register encodings are the ones real MSX software writes:
 *   R1 bit4 M1, bit3 M2, bit5 IE0 (frame interrupt), bit6 DISP, bit0 sprite size
 *   R0 bit1 M3, bit2 M4, bit3 M5; mode = M5..M1 as in the table below
 *   R7 high nibble foreground, low nibble EC (backdrop and border colour)
 *   R14 bits 0-2 = CPU address bits 14-16, carried into on a 16 KiB wrap
 *   R15 bits 0-2 select which status register a read of the status port
 *         returns (0 = frame/sprite state)
 */

#include "msx_int.h"

#include <string.h>

#include "msx_body.h"

/* ---- register and port constants --------------------------------------- */

#define VDP_NR          32u          /* R0..R25 used, the rest read 0       */

#define ST_FG           0x80u        /* status0 bit7: end of display        */
#define ST_NO           0x02u        /* status0 bit1: more than four sprites*/

#define R1_SZ           0x01u        /* 1 = 16x16 sprites                   */
#define R1_M2           0x08u
#define R1_M1           0x10u
#define R1_IE0          0x20u
#define R1_DISP         0x40u
#define R0_M3           0x02u
#define R0_M4           0x04u
#define R0_M5           0x08u
#define R8_SZ           0x02u        /* MSX2 sprite size 16x16              */
#define R14_SC          0x40u        /* MSX2 16-colour sprite law           */

/* Display bases, in the order the chip numbers them (M5..M1). */
#define MD_G1           0x00u        /* SCREEN 1, 32x24 tiles, 2 colours    */
#define MD_T1           0x01u        /* SCREEN 0, 40x24 text                */
#define MD_MC           0x02u        /* SCREEN 2, 4x4 colour cells          */
#define MD_G2           0x04u        /* SCREEN 3, two colours per line      */
#define MD_T1Q          0x05u        /* SCREEN 0 variants: same pixels      */
#define MD_MULTIQ       0x06u
#define MD_G3           0x08u        /* SCREEN 4, 4 colours per pixel       */
#define MD_G4           0x0Cu        /* SCREEN 5, 16 colours, 4 bpp         */

/* Ports, indexed by (port & 3); section 1.3 decodes both families. */
#define PORT_DATA       0u
#define PORT_CONTROL    1u
#define PORT_PALETTE    2u
#define PORT_REGISTER   3u

/* Only bits that physically exist may be written (section 2.2). */
static const uint8_t s_mask_msx1[VDP_NR] = {
    0x03, 0xFB, 0x0F, 0xFF, 0x07, 0x7F, 0x07, 0xFF,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};
static const uint8_t s_mask_v9958[VDP_NR] = {
    0x7E, 0x7F, 0x7F, 0xFF, 0x3F, 0xFF, 0x3F, 0xFF,
    0xFB, 0xBF, 0x07, 0x03, 0xFF, 0xFF, 0x07, 0x0F,
    0x0F, 0xBF, 0xFF, 0xFF, 0x3F, 0x3F, 0x3F, 0xFF,
    0x00, 0x7F, 0x3F, 0x07, 0x00, 0x00, 0x00, 0x00
};

/* ---- machine state ------------------------------------------------------ */

static uint8_t   s_reg[VDP_NR];
static uint8_t   s_vram[MSX_VRAM_SIZE];
static uint16_t  s_ptr;           /* CPU pointer, bits 0-13                 */
static uint8_t   s_latch;         /* first byte of a control or palette pair*/
static uint8_t   s_ctrl_pending;
static uint8_t   s_pal_pending;
static uint8_t   s_write_mode;    /* bit6 of the second control byte        */
static uint8_t   s_read_ahead;    /* the VDP prefetches on a read address   */
static uint8_t   s_status;
static uint8_t   s_irq_pending;   /* one frame-interrupt edge the core takes */
static uint32_t  s_t_line;
static uint16_t  s_line;
static uint8_t   s_msx1;
static uint16_t  s_pal[16];

/* Scratch for one displayed line.  s_px 0 means EC, so the backdrop is the
 * only colour that can be emitted by an empty pixel and sprites overwrite it.
 */
static uint8_t   s_px[MSX_SCREEN_W];
static uint8_t   s_front[MSX_SCREEN_W];

/* ---- palette ------------------------------------------------------------ */

/* Colour numbers as MSX BASIC names them; the levels are a clean-room
 * approximation of the TMS9918 analogue outputs.  MSX1 never writes a
 * palette, so this table is the whole colour law there (section 2.3).
 */
static const uint8_t s_msx1_rgb[16][3] = {
    {   0,   0,   0 },  /*  0 transparent - drawn as EC          */
    {   0,   0,   0 },  /*  1 black                              */
    {  48, 192,  48 },  /*  2 medium green                       */
    { 136, 224, 136 },  /*  3 light green                        */
    { 176,  48,  48 },  /*  4 dark red                           */
    { 224, 112, 112 },  /*  5 medium red                         */
    { 208, 192,  64 },  /*  6 dark yellow                        */
    {  48,  64, 224 },  /*  7 dark blue                          */
    { 112,  48, 112 },  /*  8 dark magenta                       */
    { 240, 128, 160 },  /*  9 light pink                         */
    {  80,  96, 240 },  /* 10 medium blue                        */
    { 128, 128, 128 },  /* 11 dark grey                          */
    { 160, 192, 255 },  /* 12 light grey                         */
    {  64, 240,  64 },  /* 13 lime                               */
    { 160, 240, 255 },  /* 14 light cyan                         */
    { 255, 255, 255 }   /* 15 white                              */
};

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint32_t)(r >> 3) << 11) |
                      ((uint32_t)(g >> 2) <<  5) |
                      ((uint32_t)(b >> 3)));
}

static void palette_reset(void)
{
    uint8_t i;
    for (i = 0; i < 16u; i++) {
        s_pal[i] = rgb565(s_msx1_rgb[i][0], s_msx1_rgb[i][1],
                          s_msx1_rgb[i][2]);
    }
}

/* One V9938 palette entry: four bits per primary, R5 G6 B5 on the way out. */
static void palette_set(uint8_t index, uint8_t r4, uint8_t g4, uint8_t b4)
{
    s_pal[index & 0x0Fu] = rgb565((uint8_t)((r4 << 1) | (r4 >> 3)),
                                  (uint8_t)((g4 << 2) | (g4 >> 2)),
                                  (uint8_t)((b4 << 1) | (b4 >> 3)));
}

/* ---- small readers ------------------------------------------------------ */

#define VR(a)      (s_vram[(uint32_t)(a) & (MSX_VRAM_SIZE - 1u)])

static uint8_t vdp_mode(void)
{
    return (uint8_t)((((s_reg[0] & (R0_M3 | R0_M4 | R0_M5)) << 1) |
                      ((s_reg[1] & R1_M2) >> 2) |
                      ((s_reg[1] & R1_M1) >> 4)) & 0x1Fu);
}

static uint8_t sprite_mode(void)
{
    uint8_t m = vdp_mode();
    if (m == MD_G1 || m == MD_MC || m == MD_G2 || m == MD_MULTIQ) return 1u;
    if (m == MD_G3 || m == MD_G4) return 2u;
    return 0u;                        /* text and undefined modes: no sprites */
}

static uint16_t display_end(void)
{
    if (s_msx1) return 192u;
    return (uint16_t)((s_reg[9] & 0x80u) ? 212u : 192u);
}

static uint32_t nt_base(void)
{
    uint8_t bits = s_msx1 ? (uint8_t)(s_reg[2] & 0x0Fu)
                          : (uint8_t)(s_reg[2] & 0x3Fu);
    return (uint32_t)bits * 2048u;
}

static uint32_t ct_base(void)
{
    if (s_msx1) return (uint32_t)(s_reg[3] & 0x3Fu) * 256u;
    return (uint32_t)(s_reg[10] & 0x07u) * 16384u +
           (uint32_t)(s_reg[3]  & 0x7Fu) * 64u;
}

static uint32_t pt_base(void)
{
    return (uint32_t)(s_reg[4] & (s_msx1 ? 0x07u : 0x3Fu)) * 2048u;
}

static uint32_t sat_base(void)
{
    if (s_msx1) return (uint32_t)(s_reg[5] & 0x7Fu) * 256u;
    return (uint32_t)(s_reg[11] & 0x03u) * 32768u +
           (uint32_t)(s_reg[5]  & 0x7Fu) * 256u;
}

static uint32_t spt_base(void)
{
    return (uint32_t)(s_reg[6] & (s_msx1 ? 0x07u : 0x3Fu)) * 2048u;
}

static uint16_t ec565(void)
{
    return s_pal[s_reg[7] & 0x0Fu];
}

/* The CPU data pointer: 14 bits plus the R14 page, +1 per access. */
static uint32_t cpu_addr(void)
{
    return ((uint32_t)(s_reg[14] & 0x07u) << 14) | s_ptr;
}

static void cpu_step(void)
{
    s_ptr = (uint16_t)((s_ptr + 1u) & 0x3FFFu);
    if (s_ptr == 0u && !s_msx1 && (vdp_mode() & 0x18u)) {
        s_reg[14] = (uint8_t)((s_reg[14] + 1u) & 0x07u);
    }
}

/* ---- register file ------------------------------------------------------ */

static void reg_write(uint8_t reg, uint8_t value)
{
    const uint8_t *masks = s_msx1 ? s_mask_msx1 : s_mask_v9958;
    uint8_t old;

    if (reg >= VDP_NR) return;
    value = (uint8_t)(value & masks[reg]);
    old = s_reg[reg];
    s_reg[reg] = value;

    /* Turning the frame interrupt on while FG is already up raises the IRQ
     * on real hardware; the pending flag is what the core consumes.
     */
    if (reg == 1u && !(old & R1_IE0) && (value & R1_IE0) &&
        (s_status & ST_FG)) {
        s_irq_pending = 1u;
    }
}

static uint8_t status_read(void)
{
    uint8_t ret = s_status;

    if ((s_reg[15] & 0x07u) == 0u) {
        ret = (uint8_t)(ret & ~ST_FG);
        s_irq_pending = 0u;
    }
    s_status = ret;
    return ret;
}

/* ---- public state ------------------------------------------------------- */

void vdp_init(uint8_t machine)
{
    s_msx1 = (uint8_t)(machine == BMSX_MACHINE_MSX ? 1u : 0u);
    vdp_reset();
}

void vdp_reset(void)
{
    memset(s_reg, 0, sizeof(s_reg));
    memset(s_vram, 0, sizeof(s_vram));
    memset(s_px, 0, sizeof(s_px));
    memset(s_front, 0, sizeof(s_front));
    s_ptr = 0u;
    s_latch = 0u;
    s_ctrl_pending = 0u;
    s_pal_pending = 0u;
    s_write_mode = 1u;
    s_read_ahead = 0xFFu;
    s_status = 0u;
    s_irq_pending = 0u;
    s_t_line = 0u;
    s_line = 0u;
    palette_reset();
}

uint8_t *vdp_vram(uint32_t *size)
{
    if (size != 0) *size = MSX_VRAM_SIZE;
    return s_vram;
}

/* ---- ports -------------------------------------------------------------- */

uint8_t vdp_read(uint8_t port)
{
    uint8_t idx = (uint8_t)(port & 0x03u);

    s_ctrl_pending = 0u;                 /* any read aborts a write in mid-pair */

    if (idx == PORT_DATA) {
        uint8_t result = s_read_ahead;
        if (!s_write_mode) {
            s_read_ahead = VR(cpu_addr());
            cpu_step();
        }
        return result;
    }
    if (idx == PORT_CONTROL) return status_read();
    return 0xFFu;
}

void vdp_write(uint8_t port, uint8_t value)
{
    uint8_t idx = (uint8_t)(port & 0x03u);

    if (s_msx1 && idx >= PORT_PALETTE) return;   /* the TMS9918 does not decode them */

    switch (idx) {
    case PORT_DATA:
        s_ctrl_pending = 0u;
        if (!s_write_mode) break;                /* writes on a read address are ignored */
        s_vram[cpu_addr() & (MSX_VRAM_SIZE - 1u)] = value;
        cpu_step();
        break;

    case PORT_CONTROL:
        if (s_ctrl_pending) {
            s_ctrl_pending = 0u;
            if (value & 0x80u) {
                if (!(value & 0x40u) || s_msx1) {
                    reg_write((uint8_t)(value & (s_msx1 ? 0x07u : 0x3Fu)),
                              s_latch);
                }
                /* A register write also lands in the address on MSX1. */
                if (s_msx1) {
                    s_ptr = (uint16_t)(((uint32_t)value << 8 |
                                        (s_ptr & 0xFFu)) & 0x3FFFu);
                }
            } else {
                s_write_mode = (uint8_t)((value & 0x40u) ? 1u : 0u);
                s_ptr = (uint16_t)(((uint32_t)value << 8 | s_latch) & 0x3FFFu);
                if (!s_write_mode) s_read_ahead = VR(cpu_addr());
            }
        } else {
            s_latch = value;
            s_ctrl_pending = 1u;
            if (s_msx1) s_ptr = (uint16_t)((s_ptr & 0x3F00u) | value);
        }
        break;

    case PORT_PALETTE:
        if (s_pal_pending) {
            uint8_t first = s_latch;             /* blue 0-3, red 4-7      */
            uint8_t index = (uint8_t)(s_reg[16] & 0x0Fu);
            s_pal_pending = 0u;
            palette_set(index, (uint8_t)(first >> 4),
                        (uint8_t)(value & 0x0Fu), (uint8_t)(first & 0x0Fu));
            s_reg[16] = (uint8_t)((index + 1u) & 0x0Fu);
        } else {
            s_latch = value;
            s_pal_pending = 1u;
        }
        break;

    default:                                     /* PORT_REGISTER */
        if (value & 0x80u) {
            s_reg[17] = value;                   /* indirect register select */
        } else {
            uint8_t reg = s_reg[17];
            reg_write((uint8_t)(reg & 0x3Fu), value);
            if (!(reg & 0x80u)) s_reg[17] = (uint8_t)((reg + 1u) & 0x3Fu);
        }
        break;
    }
}

/* ---- raster line model -------------------------------------------------- */

void vdp_advance(uint32_t t_states)
{
    s_t_line += t_states;
    while (s_t_line >= MSX_T_PER_LINE) {
        s_t_line -= MSX_T_PER_LINE;
        s_line = (uint16_t)((s_line + 1u) % MSX_LINES);
        if (s_line == display_end()) {
            s_status = (uint8_t)(s_status & ~(ST_FG | ST_NO));
            s_status = (uint8_t)(s_status | ST_FG);
            if (s_reg[1] & R1_IE0) s_irq_pending = 1u;
        }
    }
}

int vdp_vblank_taken(void)
{
    if (!s_irq_pending) return 0;
    s_irq_pending = 0u;
    return 1;
}

/* ---- renderer ----------------------------------------------------------- */

static void fill_row(uint16_t *row, uint16_t colour)
{
    uint16_t x;
    for (x = 0; x < MSX_SCREEN_W; x++) row[x] = colour;
}

/* SCREEN 0: 40 columns of 6 dots, so the text is 240 of the 256 dots. */
static void line_text(uint16_t y)
{
    uint32_t nt = nt_base(), pt = pt_base();
    uint16_t row = (uint16_t)(y % 8u), band = (uint16_t)(y / 8u);
    uint16_t x;

    for (x = 0; x < MSX_SCREEN_W; x++) {
        uint8_t glyph, bit;
        if (x >= 240u) { s_px[x] = 0u; continue; }
        glyph = VR(nt + (uint32_t)band * 40u + (uint32_t)(x / 6u));
        bit   = (uint8_t)((VR(pt + (uint32_t)glyph * 8u + row) >>
                           (6u - (x % 6u))) & 1u);
        s_px[x] = (uint8_t)(bit ? (s_reg[7] >> 4) & 0x0Fu : 0u);
    }
}

/* SCREEN 1 and SCREEN 3 share the fetch law; SCREEN 3 mirrors its pattern
 * table inside 2 KiB and its colour table inside 256 bytes.
 */
static void line_tiles(uint16_t y, uint8_t g2)
{
    uint32_t nt = nt_base(), ct = ct_base(), pt = pt_base();
    uint16_t row = (uint16_t)(y % 8u), band = (uint16_t)(y / 8u);
    uint16_t x;

    for (x = 0; x < MSX_SCREEN_W; x++) {
        uint32_t cell = (uint32_t)band * 32u + (uint32_t)(x / 8u);
        uint8_t  name = VR(nt + cell);
        uint8_t  cb   = g2 ? VR((ct + (cell & 0xFFu)) ) : VR(ct + cell);
        uint32_t pa   = g2 ? pt + (((uint32_t)name * 8u + row) & 0x7FFu)
                           : pt + (uint32_t)name * 8u + row;
        uint8_t  bit  = (uint8_t)((VR(pa) >> (7u - (x % 8u))) & 1u);
        uint8_t  c    = bit ? (uint8_t)(cb & 0x0Fu) : (uint8_t)(cb >> 4);
        s_px[x] = (uint8_t)(c & 0x0Fu);
    }
}

/* SCREEN 2: one colour per 4x4 cell, from the combined colour/pattern table. */
static void line_multicolor(uint16_t y)
{
    uint32_t nt = nt_base(), pt = pt_base();
    uint16_t band = (uint16_t)(y / 8u), row = (uint16_t)(y % 8u);
    uint16_t x;

    for (x = 0; x < MSX_SCREEN_W; x++) {
        uint32_t cell = (uint32_t)band * 32u + (uint32_t)(x / 8u);
        uint8_t  name = VR(nt + cell);
        uint32_t a    = pt + (uint32_t)name * 8u + (uint32_t)(row >> 2) * 2u +
                        (uint32_t)((x >> 2) & 1u);
        uint8_t  b    = VR(a);
        s_px[x] = (uint8_t)(((x >> 3) & 1u) ? (b & 0x0Fu) : (b >> 4));
    }
}

/* SCREEN 4: two bytes per 8x8 cell, pattern bit selects one of two colours. */
static void line_g3(uint16_t y)
{
    uint32_t nt = nt_base(), pt = pt_base();
    uint16_t row = (uint16_t)(y % 8u), band = (uint16_t)(y / 8u);
    uint16_t x;

    for (x = 0; x < MSX_SCREEN_W; x++) {
        uint32_t cell = (uint32_t)band * 32u + (uint32_t)(x / 8u);
        uint8_t  name = VR(nt + cell * 2u);
        uint8_t  cr   = VR(nt + cell * 2u + 1u);
        uint8_t  bit  = (uint8_t)((VR(pt + (uint32_t)name * 8u + row) >>
                                   (7u - (x % 8u))) & 1u);
        uint8_t  c    = bit ? (uint8_t)(cr & 0x0Fu) : (uint8_t)(cr >> 4);
        s_px[x] = (uint8_t)(c & 0x0Fu);
    }
}

/* SCREEN 5: 4 bpp pattern, colour from a nibble per 8 dots. */
static void line_g4(uint16_t y)
{
    uint32_t pt = pt_base(), ct = ct_base();
    uint16_t band = (uint16_t)(y / 8u);
    uint16_t x;

    for (x = 0; x < MSX_SCREEN_W; x++) {
        uint8_t pb = VR(pt + (uint32_t)y * 128u + (uint32_t)(x >> 1));
        uint8_t v  = (uint8_t)((x & 1u) ? (pb & 0x0Fu) : (pb >> 4));
        uint8_t cr = VR(ct + (uint32_t)band * 32u + (uint32_t)(x >> 3));
        uint8_t c  = (uint8_t)(((x >> 2) & 1u) ? (cr & 0x0Fu) : (cr >> 4));
        s_px[x] = (uint8_t)(v ? (c & 0x0Fu) : 0u);
    }
}

/* ---- sprites ------------------------------------------------------------ */

static void sprite_put(uint16_t x, uint8_t colour, uint8_t behind)
{
    if (x >= MSX_SCREEN_W) return;
    if (colour == 0u) return;                      /* transparent          */
    if (behind && !s_front[x] && s_px[x] != 0u) return;  /* the wins        */
    s_px[x] = colour;
    s_front[x] = 1u;
}

/* MSX1 sprite mode: 32 entries of four bytes, four per display line, the
 * magic Y value 0xD0 ends the scan.
 */
static void sprites_mode1(uint16_t y)
{
    uint32_t sat = sat_base(), spt = spt_base();
    uint16_t h = (uint16_t)((s_reg[1] & R1_SZ) ? 16u : 8u);
    uint8_t  drawn = 0u;
    uint16_t n;

    for (n = 0u; n < 32u; n++) {
        uint32_t a  = sat + (uint32_t)n * 4u;
        uint16_t sy = VR(a);
        uint16_t sx, i;
        uint8_t  tile, col, behind;
        uint32_t pa;

        if (sy == 0xD0u) break;
        if (drawn == 4u) { s_status |= ST_NO; break; }
        if (y < sy || y >= (uint16_t)(sy + h)) continue;
        drawn++;

        sx = VR(a + 1u);
        if (sx == 0u) continue;                    /* X = 0 is not displayed */
        tile = (uint8_t)(VR(a + 2u) & (h == 16u ? 0xFCu : 0xFFu));
        col  = VR(a + 3u);
        behind = (uint8_t)((col & 0x80u) ? 1u : 0u);
        pa = spt + (uint32_t)tile * (uint32_t)h + (uint16_t)(y - sy) *
             (uint32_t)(h == 16u ? 2u : 1u);

        for (i = 0u; i < h; i++) {
            uint8_t pat = VR(pa + (uint32_t)(i >> 3));
            if (pat & (uint8_t)(0x80u >> (i & 7u))) {
                sprite_put((uint16_t)(sx + i), (uint8_t)(col & 0x0Fu), behind);
            }
        }
    }
}

/* MSX2 sprite mode, 1.05 scope: 8x8 and 16x16, no rotation, no 32-pixel
 * sizes; the colour law is R14 bit6 (section 2.3).
 */
static void sprites_mode2(uint16_t y)
{
    uint32_t sat = sat_base(), spt = spt_base();
    uint16_t gsize = (uint16_t)((s_reg[8] & R8_SZ) ? 16u : 8u);
    uint8_t  sixteen = (uint8_t)((s_reg[14] & R14_SC) ? 1u : 0u);
    uint8_t  drawn = 0u;
    uint16_t n;

    for (n = 0u; n < 32u; n++) {
        uint32_t a = sat + (uint32_t)n * 8u;
        uint16_t sy = VR(a);
        uint16_t sx, h, i;
        uint16_t tile;
        uint8_t  attr, cr1, cr0;

        if (sy == 0xE0u) break;
        if (drawn == 4u) { s_status |= ST_NO; break; }
        attr = VR(a + 2u);
        h = (uint16_t)((attr & R8_SZ) ? 16u : gsize);
        if (y < sy || y >= (uint16_t)(sy + h)) continue;
        drawn++;

        sx = VR(a + 1u);
        if (sx == 0u) continue;
        tile = (uint16_t)(VR(a + 3u) | ((uint16_t)(VR(a + 7u) & 0x01u) << 8));
        cr1  = (uint8_t)(VR(a + 4u) & 0x0Fu);
        cr0  = (uint8_t)(VR(a + 5u) & 0x0Fu);
        if (sixteen) cr0 = 0u;                     /* only the own colour    */

        for (i = 0u; i < h; i++) {
            uint32_t pa = spt + (uint32_t)tile * (uint32_t)h +
                          (uint16_t)(y - sy) * (uint32_t)(h == 16u ? 2u : 1u);
            uint8_t pat = VR(pa + (uint32_t)(i >> 3));
            if (pat & (uint8_t)(0x80u >> (i & 7u))) {
                sprite_put((uint16_t)(sx + i), cr1, 0u);
            } else if (!sixteen && cr0) {
                sprite_put((uint16_t)(sx + i), cr0, 0u);
            }
        }
    }
}

/* ---- frame render ------------------------------------------------------- */

void vdp_render(uint16_t *fb)
{
    uint16_t end = display_end();
    uint8_t  sm  = sprite_mode();
    uint8_t  m   = vdp_mode();
    uint16_t y;

    if (m == MD_T1Q) m = MD_T1;
    if (m == MD_MULTIQ) m = MD_MC;

    for (y = 0u; y < MSX_SCREEN_H; y++) {
        uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
        uint16_t x;

        if (!(s_reg[1] & R1_DISP) || y >= end) {
            fill_row(row, ec565());
            continue;
        }
        switch (m) {
        case MD_T1:  line_text(y);        break;
        case MD_G1:  line_tiles(y, 0u);   break;
        case MD_MC:  line_multicolor(y);  break;
        case MD_G2:  line_tiles(y, 1u);   break;
        case MD_G3:  line_g3(y);          break;
        case MD_G4:  line_g4(y);          break;
        default:     fill_row(row, 0u);   /* undefined display: black (2.3) */
            continue;
        }
        memset(s_front, 0, sizeof(s_front));
        if (sm == 1u) sprites_mode1(y);
        else if (sm == 2u) sprites_mode2(y);

        {
            uint16_t ec = ec565();
            for (x = 0u; x < MSX_SCREEN_W; x++) {
                row[x] = s_px[x] ? s_pal[s_px[x] & 0x0Fu] : ec;
            }
        }
    }
}
