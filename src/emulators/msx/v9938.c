/*
 * v9938.c — Yamaha V9938 / V9958 MSX2/MSX2+/Turbo R Video Display Processor.
 * Hardware emulation source for B-MSX. Clean-room C99.
 */

#include "v9938.h"
#include <string.h>
#include <stdio.h>

#define MSX_SCREEN_W     256u
#define MSX_SCREEN_H     212u
#define MSX_LINES        262u
#define MSX_T_PER_LINE   228u

#define ST_FG            0x80u  /* status 0 bit 7: end of display */
#define ST_5S            0x40u  /* status 0 bit 6: 5th sprite     */
#define ST_C             0x20u  /* status 0 bit 5: sprite collision */

#define ST_TR            0x80u  /* status 2 bit 7: transfer ready */
#define ST_VR            0x40u  /* status 2 bit 6: vertical retrace */
#define ST_HR            0x20u  /* status 2 bit 5: horizontal retrace */
#define ST_BD            0x10u  /* status 2 bit 4: border detected */
#define ST_CE            0x01u  /* status 2 bit 0: command executing */

#define R1_DISP          0x40u
#define R1_IE0           0x20u
#define R0_IE1           0x10u

/* Official V9938 power-on default palette (V9938 MSX-VIDEO Technical Data Book p. 148):
 * 0x000, 0x000, 0x611, 0x733, 0x117, 0x327, 0x151, 0x627,
 * 0x171, 0x373, 0x661, 0x664, 0x411, 0x265, 0x555, 0x777 (GRB 3-bit values mapped to 0..255) */
static const uint8_t s_def_pal[16][3] = {
    {   0,   0,   0 },  /*  0: transparent / black */
    {   0,   0,   0 },  /*  1: black               */
    {  36, 219,  36 },  /*  2: medium green (R:1, G:6, B:1) */
    { 109, 255, 109 },  /*  3: light green  (R:3, G:7, B:3) */
    {  36,  36, 255 },  /*  4: dark blue    (R:1, G:1, B:7) */
    {  73, 109, 255 },  /*  5: light blue   (R:2, G:3, B:7) */
    { 182,  36,  36 },  /*  6: dark red     (R:5, G:1, B:1) */
    {  73, 219, 255 },  /*  7: cyan         (R:2, G:6, B:7) */
    { 255,  36,  36 },  /*  8: medium red   (R:7, G:1, B:1) */
    { 255, 109, 109 },  /*  9: light red    (R:7, G:3, B:3) */
    { 219, 219,  36 },  /* 10: dark yellow  (R:6, G:6, B:1) */
    { 219, 219, 146 },  /* 11: light yellow (R:6, G:6, B:4) */
    {  36, 146,  36 },  /* 12: dark green   (R:1, G:4, B:1) */
    { 219,  73, 182 },  /* 13: magenta      (R:6, G:2, B:5) */
    { 182, 182, 182 },  /* 14: gray         (R:5, G:5, B:5) */
    { 255, 255, 255 }   /* 15: white        (R:7, G:7, B:7) */
};

static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((uint32_t)(r >> 3) << 11) |
                      ((uint32_t)(g >> 2) <<  5) |
                      ((uint32_t)(b >> 3)));
}

static inline uint16_t rgb555_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint32_t r5 = (uint32_t)(r & 0x1Fu);
    uint32_t g6 = (uint32_t)(((g & 0x1Fu) << 1) | ((g & 0x1Fu) >> 4));
    uint32_t b5 = (uint32_t)(b & 0x1Fu);
    return (uint16_t)((r5 << 11) | (g6 << 5) | b5);
}

/* V9938 Graphic 7 fixed sprite palette (V9938 data book p. 98) */
static const uint16_t s_g7_sprite_pal[16] = {
    0x0000, 0x0009, 0x6800, 0x6809, 0x0360, 0x0369, 0x6B60, 0x6B69,
    0xFC89, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x07FF, 0xFFE0, 0xFFFF
};

static inline uint16_t rgb332_to_rgb565(uint8_t c)
{
    /* Graphic 7 (Screen 8): bits 7-5 Green, bits 4-2 Red, bits 1-0 Blue (V9938 GGGRRRBB) */
    uint8_t g = (uint8_t)(((c >> 5) & 7) * 255 / 7);
    uint8_t r = (uint8_t)(((c >> 2) & 7) * 255 / 7);
    uint8_t b = (uint8_t)(( c        & 3) * 255 / 3);
    return rgb565(r, g, b);
}

void v9938_init(v9938_t *v, int is_v9958)
{
    if (!v) return;
    memset(v, 0, sizeof(*v));
    v->is_v9958 = (uint8_t)(is_v9958 ? 1 : 0);
    v9938_reset(v);
}

void v9938_reset(v9938_t *v)
{
    uint8_t i;
    if (!v) return;
    memset(v->vram, 0, sizeof(v->vram));
    memset(v->regs, 0, sizeof(v->regs));
    memset(v->stat, 0, sizeof(v->stat));
    for (i = 0; i < 16; i++) {
        v->pal[i] = rgb565(s_def_pal[i][0], s_def_pal[i][1], s_def_pal[i][2]);
    }
    for (int j = 0; j < 256; j++) {
        v->pal256[j] = rgb332_to_rgb565((uint8_t)j);
    }
    v->pal_sub_idx = 0;
    v->pal_r_latch = 0;
    v->pal_g_latch = 0;
    v->vram_ptr = 0;
    v->latch = 0;
    v->pal_latch = 0;
    v->ctrl_pending = 0;
    v->pal_pending = 0;
    v->write_mode = 1;
    v->read_ahead = 0xFF;
    v->irq_pending = 0;
    v->sx = v->sy = v->dx = v->dy = v->nx = v->ny = 0;
    v->col = v->arg = v->cmd = 0;
    v->t_line = 0;
    v->line = 0;
    v->stat[2] = 0x0C; /* bits 2 and 3 always 1 on V9938 */
}

static inline uint32_t cpu_addr(const v9938_t *v)
{
    return ((uint32_t)(v->regs[14] & 0x0Fu) << 14) | v->vram_ptr;
}

static inline void cpu_step(v9938_t *v)
{
    v->vram_ptr = (uint16_t)((v->vram_ptr + 1u) & 0x3FFFu);
    if (v->vram_ptr == 0u) {
        v->regs[14] = (uint8_t)((v->regs[14] + 1u) & 0x0Fu);
    }
}

uint8_t v9938_mode(const v9938_t *v)
{
    /* Mode bits M5..M1:
     * R0: bit 3 = M5, bit 2 = M4, bit 1 = M3
     * R1: bit 3 = M2, bit 4 = M1 */
    return (uint8_t)((((v->regs[0] & 0x0Eu) << 1) |
                      ((v->regs[1] & 0x08u) >> 2) |
                      ((v->regs[1] & 0x10u) >> 4)) & 0x1Fu);
}

/* ---- VDP Command Engine (Alex Wulms architecture) ------------------------ */

static uint8_t v9938_point_pixel(v9938_t *v, int x, int y, int mode)
{
    uint32_t addr;
    x &= 511;
    y &= 1023;
    switch (mode) {
    case 0x0C: /* Screen 5: 256x212 4bpp, 128 bytes/line */
        x &= 255;
        addr = (uint32_t)y * 128u + (uint32_t)(x >> 1);
        if (addr >= V9938_VRAM_SIZE) return 0;
        return (uint8_t)((x & 1) ? (v->vram[addr] & 0x0F) : (v->vram[addr] >> 4));
    case 0x10: /* Screen 6: 512x212 2bpp, 128 bytes/line */
        addr = (uint32_t)y * 128u + (uint32_t)(x >> 2);
        if (addr >= V9938_VRAM_SIZE) return 0;
        return (uint8_t)((v->vram[addr] >> ((3 - (x & 3)) * 2)) & 3);
    case 0x14: /* Screen 7: 512x212 4bpp, 256 bytes/line */
        addr = (uint32_t)y * 256u + (uint32_t)(x >> 1);
        if (addr >= V9938_VRAM_SIZE) return 0;
        return (uint8_t)((x & 1) ? (v->vram[addr] & 0x0F) : (v->vram[addr] >> 4));
    case 0x1C: /* Screen 8: 256x212 8bpp, 256 bytes/line */
        x &= 255;
        addr = (uint32_t)y * 256u + (uint32_t)x;
        if (addr >= V9938_VRAM_SIZE) return 0;
        return v->vram[addr];
    default:
        return 0;
    }
}

static void v9938_pset_pixel(v9938_t *v, int x, int y, uint8_t color, uint8_t op, int mode)
{
    uint32_t addr;
    uint8_t old_c, new_c;
    x &= 511;
    y &= 1023;

    switch (mode) {
    case 0x0C: { /* Screen 5: 4bpp */
        x &= 255;
        addr = (uint32_t)y * 128u + (uint32_t)(x >> 1);
        if (addr >= V9938_VRAM_SIZE) return;
        old_c = (uint8_t)((x & 1) ? (v->vram[addr] & 0x0F) : (v->vram[addr] >> 4));
        color &= 0x0F;
        switch (op) {
        case 1: new_c = old_c & color; break;
        case 2: new_c = old_c | color; break;
        case 3: new_c = old_c ^ color; break;
        case 4: new_c = ~color & 0x0F; break;
        case 8: new_c = color ? color : old_c; break; /* TIMP */
        case 9: new_c = color ? (old_c & color) : old_c; break;
        case 10: new_c = color ? (old_c | color) : old_c; break;
        case 11: new_c = color ? (old_c ^ color) : old_c; break;
        case 12: new_c = color ? (~color & 0x0F) : old_c; break;
        default: new_c = color; break; /* IMP */
        }
        if (x & 1) v->vram[addr] = (uint8_t)((v->vram[addr] & 0xF0) | new_c);
        else       v->vram[addr] = (uint8_t)((v->vram[addr] & 0x0F) | (new_c << 4));
        break;
    }
    case 0x10: { /* Screen 6: 2bpp */
        int shift = (3 - (x & 3)) * 2;
        addr = (uint32_t)y * 128u + (uint32_t)(x >> 2);
        if (addr >= V9938_VRAM_SIZE) return;
        old_c = (uint8_t)((v->vram[addr] >> shift) & 3);
        color &= 3;
        switch (op) {
        case 1: new_c = old_c & color; break;
        case 2: new_c = old_c | color; break;
        case 3: new_c = old_c ^ color; break;
        case 4: new_c = ~color & 3; break;
        case 8: new_c = color ? color : old_c; break;
        default: new_c = color; break;
        }
        v->vram[addr] = (uint8_t)((v->vram[addr] & ~(3 << shift)) | (new_c << shift));
        break;
    }
    case 0x14: { /* Screen 7: 4bpp */
        addr = (uint32_t)y * 256u + (uint32_t)(x >> 1);
        if (addr >= V9938_VRAM_SIZE) return;
        old_c = (uint8_t)((x & 1) ? (v->vram[addr] & 0x0F) : (v->vram[addr] >> 4));
        color &= 0x0F;
        switch (op) {
        case 1: new_c = old_c & color; break;
        case 2: new_c = old_c | color; break;
        case 3: new_c = old_c ^ color; break;
        case 4: new_c = ~color & 0x0F; break;
        case 8: new_c = color ? color : old_c; break;
        default: new_c = color; break;
        }
        if (x & 1) v->vram[addr] = (uint8_t)((v->vram[addr] & 0xF0) | new_c);
        else       v->vram[addr] = (uint8_t)((v->vram[addr] & 0x0F) | (new_c << 4));
        break;
    }
    case 0x1C: /* Screen 8: 8bpp */
    default: {
        x &= 255;
        addr = (uint32_t)y * 256u + (uint32_t)x;
        if (addr >= V9938_VRAM_SIZE) return;
        old_c = v->vram[addr];
        switch (op) {
        case 1: new_c = old_c & color; break;
        case 2: new_c = old_c | color; break;
        case 3: new_c = old_c ^ color; break;
        case 4: new_c = (uint8_t)~color; break;
        case 8: new_c = color ? color : old_c; break;
        default: new_c = color; break;
        }
        v->vram[addr] = new_c;
        break;
    }
    }
}

static void v9938_exec_command(v9938_t *v)
{
    uint8_t code = (uint8_t)(v->cmd >> 4);
    uint8_t op   = (uint8_t)(v->cmd & 0x0F);
    int mode = v9938_mode(v);
    int dix = (v->arg & 0x04) ? -1 : 1;
    int diy = (v->arg & 0x08) ? -1 : 1;
    int nx = v->nx ? v->nx : ((mode == 0x1C || mode == 0x0C) ? 256 : 512);
    int ny = v->ny ? v->ny : 1024;
    int xi, yi;

    v->stat[2] |= ST_CE;

    switch (code) {
    case 0x04: /* POINT */
        v->col = v9938_point_pixel(v, v->sx, v->sy, mode);
        v->stat[7] = v->col;
        break;

    case 0x05: /* PSET */
        v9938_pset_pixel(v, v->dx, v->dy, v->col, op, mode);
        break;

    case 0x07: { /* LINE: nx is major axis length, ny is minor axis length */
        int maj_len = (int)v->nx;
        int min_len = (int)v->ny;
        int maj = (v->arg & 0x01); /* MAJ = 1 -> Y major, MAJ = 0 -> X major */
        int step = 0;
        int cur_x = v->dx;
        int cur_y = v->dy;
        int d = 2 * min_len - maj_len;
        for (step = 0; step <= maj_len; step++) {
            v9938_pset_pixel(v, cur_x, cur_y, v->col, op, mode);
            if (maj) {
                cur_y += diy;
                if (d >= 0) { cur_x += dix; d -= 2 * maj_len; }
                d += 2 * min_len;
            } else {
                cur_x += dix;
                if (d >= 0) { cur_y += diy; d -= 2 * maj_len; }
                d += 2 * min_len;
            }
        }
        break;
    }

    case 0x08: /* LMMV - fill rectangle with logical op */
        for (yi = 0; yi < ny; yi++) {
            int y = v->dy + yi * diy;
            for (xi = 0; xi < nx; xi++) {
                int x = v->dx + xi * dix;
                v9938_pset_pixel(v, x, y, v->col, op, mode);
            }
        }
        break;

    case 0x03: /* LRMM - V9968 Logical Rotate/Scale Memory to Memory */
    case 0x09: /* LMMM - VRAM to VRAM copy with logical op */
        for (yi = 0; yi < ny; yi++) {
            int sy = v->sy + yi * diy;
            int dy = v->dy + yi * diy;
            for (xi = 0; xi < nx; xi++) {
                int sx = v->sx + xi * dix;
                int dx = v->dx + xi * dix;
                uint8_t c = v9938_point_pixel(v, sx, sy, mode);
                v9938_pset_pixel(v, dx, dy, c, op, mode);
            }
        }
        break;

    case 0x0C: { /* HMMV - high speed VRAM byte fill */
        uint32_t line_pitch = (mode == 0x14 || mode == 0x1C) ? 256u : 128u;
        int start_x = (mode == 0x1C) ? (int)v->dx : ((mode == 0x10) ? (int)(v->dx >> 2) : (int)(v->dx >> 1));
        int count_x = (mode == 0x1C) ? (int)(v->nx ? v->nx : 256) :
                      ((mode == 0x10) ? (int)(v->nx ? (v->nx >> 2) : 128) :
                       (int)(v->nx ? (v->nx >> 1) : (mode == 0x14 ? 256 : 128)));
        for (yi = 0; yi < ny; yi++) {
            int y = (v->dy + yi * diy) & 1023;
            uint32_t row_addr = (uint32_t)y * line_pitch;
            for (xi = 0; xi < count_x; xi++) {
                int x = (start_x + xi * dix) & (int)(line_pitch - 1);
                uint32_t a = (row_addr + (uint32_t)x) & (V9938_VRAM_SIZE - 1u);
                v->vram[a] = v->col;
            }
        }
        break;
    }

    case 0x0D: { /* HMMM - high speed VRAM to VRAM byte copy */
        uint32_t line_pitch = (mode == 0x14 || mode == 0x1C) ? 256u : 128u;
        int start_sx = (mode == 0x1C) ? (int)v->sx : ((mode == 0x10) ? (int)(v->sx >> 2) : (int)(v->sx >> 1));
        int start_dx = (mode == 0x1C) ? (int)v->dx : ((mode == 0x10) ? (int)(v->dx >> 2) : (int)(v->dx >> 1));
        int count_x = (mode == 0x1C) ? (int)(v->nx ? v->nx : 256) :
                      ((mode == 0x10) ? (int)(v->nx ? (v->nx >> 2) : 128) :
                       (int)(v->nx ? (v->nx >> 1) : (mode == 0x14 ? 256 : 128)));
        for (yi = 0; yi < ny; yi++) {
            int sy = (v->sy + yi * diy) & 1023;
            int dy = (v->dy + yi * diy) & 1023;
            uint32_t s_row = (uint32_t)sy * line_pitch;
            uint32_t d_row = (uint32_t)dy * line_pitch;
            for (xi = 0; xi < count_x; xi++) {
                int sx = (start_sx + xi * dix) & (int)(line_pitch - 1);
                int dx = (start_dx + xi * dix) & (int)(line_pitch - 1);
                uint32_t sa = (s_row + (uint32_t)sx) & (V9938_VRAM_SIZE - 1u);
                uint32_t da = (d_row + (uint32_t)dx) & (V9938_VRAM_SIZE - 1u);
                v->vram[da] = v->vram[sa];
            }
        }
        break;
    }

    case 0x0E: { /* YMMM - high speed Y row copy */
        uint32_t line_pitch = (mode == 0x14 || mode == 0x1C) ? 256u : 128u;
        int start_dx = (mode == 0x1C) ? (int)v->dx : ((mode == 0x10) ? (int)(v->dx >> 2) : (int)(v->dx >> 1));
        for (yi = 0; yi < ny; yi++) {
            int sy = (v->sy + yi * diy) & 1023;
            int dy = (v->dy + yi * diy) & 1023;
            uint32_t s_row = (uint32_t)sy * line_pitch;
            uint32_t d_row = (uint32_t)dy * line_pitch;
            int xi;
            for (xi = 0; xi < (int)line_pitch; xi++) {
                int x = (start_dx + xi * dix) & (int)(line_pitch - 1);
                uint32_t sa = (s_row + (uint32_t)x) & (V9938_VRAM_SIZE - 1u);
                uint32_t da = (d_row + (uint32_t)x) & (V9938_VRAM_SIZE - 1u);
                v->vram[da] = v->vram[sa];
            }
        }
        break;
    }

    case 0x00: /* STOP */
        v->tr_active = 0;
        v->stat[2] &= (uint8_t)~(ST_CE | ST_TR);
        return;

    case 0x0B: /* LMMC - Logical Move CPU to VRAM */
        v->tr_active = 1;
        v->tr_code = 0x0B;
        v->tr_op = op;
        v->tr_dix = dix;
        v->tr_diy = diy;
        v->tr_x = 0;
        v->tr_y = 0;
        v->stat[2] |= ST_CE | ST_TR;
        return;

    case 0x0F: /* HMMC - High speed Move CPU to VRAM */
        v->tr_active = 1;
        v->tr_code = 0x0F;
        v->tr_op = op;
        v->tr_dix = dix;
        v->tr_diy = diy;
        v->tr_x = 0;
        v->tr_y = 0;
        if (mode == 0x1C) {
            v->tr_bytes_per_line = v->nx ? v->nx : 256;
        } else if (mode == 0x14) {
            v->tr_bytes_per_line = v->nx ? (v->nx >> 1) : 256;
        } else if (mode == 0x10) {
            v->tr_bytes_per_line = v->nx ? (v->nx >> 2) : 128;
        } else {
            v->tr_bytes_per_line = v->nx ? (v->nx >> 1) : 128;
        }
        v->stat[2] |= ST_CE | ST_TR;
        return;

    default:
        break;
    }

    v->stat[2] &= (uint8_t)~ST_CE;
    v->stat[2] |= ST_TR;
}

/* ---- Register writes ---------------------------------------------------- */

static void v9938_reg_write(v9938_t *v, uint8_t reg, uint8_t val)
{
    if (reg >= V9938_REG_COUNT) return;
    v->regs[reg] = val;
    {
        extern uint16_t msx_get_pc(void);
        uint16_t pc = msx_get_pc();
        if (pc >= 0xE000) {
            printf("[REG %d] pc=%04X val=%02X\n", reg, pc, val);
        }
    }
    if (reg == 16) {
        v->pal_pending = 0;
        v->pal_sub_idx = 0;
    }

    /* Indirect register R17 auto-increment or setup */
    if (reg >= 32 && reg <= 46) {
        switch (reg - 32) {
        case 0: v->sx = (uint16_t)((v->sx & 0x100u) | val); break;
        case 1: v->sx = (uint16_t)((v->sx & 0x0FFu) | ((val & 1u) << 8)); break;
        case 2: v->sy = (uint16_t)((v->sy & 0x300u) | val); break;
        case 3: v->sy = (uint16_t)((v->sy & 0x0FFu) | ((val & 3u) << 8)); break;
        case 4: v->dx = (uint16_t)((v->dx & 0x100u) | val); break;
        case 5: v->dx = (uint16_t)((v->dx & 0x0FFu) | ((val & 1u) << 8)); break;
        case 6: v->dy = (uint16_t)((v->dy & 0x300u) | val); break;
        case 7: v->dy = (uint16_t)((v->dy & 0x0FFu) | ((val & 3u) << 8)); break;
        case 8: v->nx = (uint16_t)((v->nx & 0x300u) | val); break;
        case 9: v->nx = (uint16_t)((v->nx & 0x0FFu) | ((val & 3u) << 8)); break;
        case 10: v->ny = (uint16_t)((v->ny & 0x300u) | val); break;
        case 11: v->ny = (uint16_t)((v->ny & 0x0FFu) | ((val & 3u) << 8)); break;
        case 12:
            v->col = val;
            if (v->tr_active) {
                int mode = v9938_mode(v);
                if (v->tr_code == 0x0F) { /* HMMC */
                    int cur_y = (v->dy + (int)v->tr_y * v->tr_diy) & 1023;
                    uint32_t line_pitch = (mode == 0x14 || mode == 0x1C) ? 256u : 128u;
                    int start_byte = (mode == 0x1C) ? (int)v->dx : (mode == 0x10 ? (int)(v->dx >> 2) : (int)(v->dx >> 1));
                    int cur_x_byte = (start_byte + (int)v->tr_x * v->tr_dix) & (int)(line_pitch - 1);
                    uint32_t a = ((uint32_t)cur_y * line_pitch + (uint32_t)cur_x_byte) & (V9938_VRAM_SIZE - 1u);
                    v->vram[a] = val;
                    v->tr_x++;
                    if (v->tr_x >= v->tr_bytes_per_line) {
                        v->tr_x = 0;
                        v->tr_y++;
                        if (v->tr_y >= (v->ny ? v->ny : 1024)) {
                            v->tr_active = 0;
                            v->stat[2] &= (uint8_t)~(ST_CE | ST_TR);
                        }
                    }
                } else if (v->tr_code == 0x0B) { /* LMMC: logical move CPU to VRAM */
                    int cur_y = (v->dy + (int)v->tr_y * v->tr_diy) & 1023;
                    if (mode == 0x1C) {
                        /* Screen 8: 1 pixel per byte */
                        int cur_x = (v->dx + (int)v->tr_x * v->tr_dix);
                        v9938_pset_pixel(v, cur_x, cur_y, val, v->tr_op, mode);
                        v->tr_x++;
                        if (v->tr_x >= (v->nx ? v->nx : 256)) {
                            v->tr_x = 0;
                            v->tr_y++;
                            if (v->tr_y >= (v->ny ? v->ny : 1024)) {
                                v->tr_active = 0;
                                v->stat[2] &= (uint8_t)~(ST_CE | ST_TR);
                            }
                        }
                    } else if (mode == 0x0C || mode == 0x14) {
                        /* Screen 5 / Screen 7: 2 pixels per byte (4 bpp) */
                        uint8_t p0 = (uint8_t)(val >> 4);
                        uint8_t p1 = (uint8_t)(val & 0x0Fu);
                        int cur_x = (v->dx + (int)v->tr_x * v->tr_dix);
                        v9938_pset_pixel(v, cur_x, cur_y, p0, v->tr_op, mode);
                        v->tr_x++;
                        if (v->tr_x >= (v->nx ? v->nx : (mode == 0x0C ? 256 : 512))) {
                            v->tr_x = 0;
                            v->tr_y++;
                        } else {
                            cur_x = (v->dx + (int)v->tr_x * v->tr_dix);
                            v9938_pset_pixel(v, cur_x, cur_y, p1, v->tr_op, mode);
                            v->tr_x++;
                            if (v->tr_x >= (v->nx ? v->nx : (mode == 0x0C ? 256 : 512))) {
                                v->tr_x = 0;
                                v->tr_y++;
                            }
                        }
                        if (v->tr_y >= (v->ny ? v->ny : 1024)) {
                            v->tr_active = 0;
                            v->stat[2] &= (uint8_t)~(ST_CE | ST_TR);
                        }
                    } else if (mode == 0x10) {
                        /* Screen 6: 4 pixels per byte (2 bpp) */
                        for (int s = 3; s >= 0; s--) {
                            uint8_t p = (uint8_t)((val >> (s * 2)) & 3u);
                            int cur_x = (v->dx + (int)v->tr_x * v->tr_dix);
                            v9938_pset_pixel(v, cur_x, cur_y, p, v->tr_op, mode);
                            v->tr_x++;
                            if (v->tr_x >= (v->nx ? v->nx : 512)) {
                                v->tr_x = 0;
                                v->tr_y++;
                                break;
                            }
                        }
                        if (v->tr_y >= (v->ny ? v->ny : 1024)) {
                            v->tr_active = 0;
                            v->stat[2] &= (uint8_t)~(ST_CE | ST_TR);
                        }
                    }
                }
            }
            break;
        case 13: v->arg = val; break;
        case 14:
            v->cmd = val;
            v9938_exec_command(v);
            break;
        }
    }

    if (reg == 1 && (val & R1_IE0) && (v->stat[0] & ST_FG)) {
        v->irq_pending = 1;
    }
}

/* ---- Ports -------------------------------------------------------------- */

uint8_t v9938_read(v9938_t *v, uint8_t port)
{
    uint8_t idx = (uint8_t)(port & 3u);
    v->ctrl_pending = 0;

    if (idx == 0) { /* Port 0x98: VRAM Data */
        uint8_t res = v->read_ahead;
        if (!v->write_mode) {
            uint32_t a = cpu_addr(v) & (V9938_VRAM_SIZE - 1u);
            v->read_ahead = v->vram[a];
            cpu_step(v);
        }
        return res;
    }
    if (idx == 1) { /* Port 0x99: Status */
        uint8_t sreg = (uint8_t)(v->regs[15] & 0x0Fu);
        uint8_t ret = (sreg < V9938_STAT_COUNT) ? v->stat[sreg] : 0xFFu;
        if (sreg == 0) {
            v->stat[0] &= (uint8_t)~ST_FG;
            v->irq_pending = 0;
        } else if (sreg == 1) {
            v->stat[1] &= 0xFEu; /* clear line interrupt flag */
        }
        return ret;
    }
    return 0xFFu;
}

void v9938_write(v9938_t *v, uint8_t port, uint8_t val)
{
    uint8_t idx = (uint8_t)(port & 3u);

    switch (idx) {
    case 0: { /* Port 0x98: VRAM Data */
        v->ctrl_pending = 0;
        if (v->write_mode) {
            uint32_t a = cpu_addr(v) & (V9938_VRAM_SIZE - 1u);
            v->vram[a] = val;
            cpu_step(v);
        }
        break;
    }
    case 1: { /* Port 0x99: Control / Register */
        if (v->ctrl_pending) {
            v->ctrl_pending = 0;
            if (val & 0x80u) {
                /* Register write */
                uint8_t r = (uint8_t)(val & 0x3Fu);
                v9938_reg_write(v, r, v->latch);
            } else {
                /* VRAM address set */
                v->write_mode = (uint8_t)((val & 0x40u) ? 1 : 0);
                v->vram_ptr = (uint16_t)(((uint32_t)val << 8 | v->latch) & 0x3FFFu);
                if (!v->write_mode) {
                    uint32_t a = cpu_addr(v) & (V9938_VRAM_SIZE - 1u);
                    v->read_ahead = v->vram[a];
                }
            }
        } else {
            v->latch = val;
            v->ctrl_pending = 1;
        }
        break;
    }
    case 2: { /* Port 0x9A: Palette */
        if (v->regs[20] & 0x01) {
            /* V9968 Extended Palette (EPAL): 3 bytes per entry (R, G, B in 0..31) */
            uint8_t pidx = v->regs[16];
            if (v->pal_sub_idx == 0) {
                v->pal_r_latch = (uint8_t)(val & 0x1Fu);
                v->pal_sub_idx = 1;
            } else if (v->pal_sub_idx == 1) {
                v->pal_g_latch = (uint8_t)(val & 0x1Fu);
                v->pal_sub_idx = 2;
            } else {
                uint8_t r = v->pal_r_latch;
                uint8_t g = v->pal_g_latch;
                uint8_t b = (uint8_t)(val & 0x1Fu);
                uint16_t c565 = rgb555_to_rgb565(r, g, b);
                v->pal256[pidx] = c565;
                if (pidx < 16) {
                    v->pal[pidx] = c565;
                }
                v->regs[16] = (uint8_t)(pidx + 1);
                v->pal_sub_idx = 0;
            }
        } else {
            /* Standard V9938 Palette: 2 bytes per color (RGB333) */
            if (v->pal_pending) {
                uint8_t first = v->pal_latch; /* bits 6-4: Red, bits 2-0: Blue */
                uint8_t pidx = (uint8_t)(v->regs[16] & 0x0Fu);
                uint8_t r3 = (uint8_t)((first >> 4) & 0x07u);
                uint8_t b3 = (uint8_t)(first & 0x07u);
                uint8_t g3 = (uint8_t)(val & 0x07u);
                uint8_t r8 = (uint8_t)((r3 * 255u) / 7u);
                uint8_t g8 = (uint8_t)((g3 * 255u) / 7u);
                uint8_t b8 = (uint8_t)((b3 * 255u) / 7u);
                v->pal_pending = 0;
                v->pal[pidx] = rgb565(r8, g8, b8);
                v->pal256[pidx] = v->pal[pidx];
                v->regs[16] = (uint8_t)((pidx + 1) & 0x0Fu);
            } else {
                v->pal_latch = val;
                v->pal_pending = 1;
            }
        }
        break;
    }
    case 3: { /* Port 0x9B: Register indirect */
        uint8_t r = v->regs[17];
        uint8_t target_reg = (uint8_t)(r & 0x3Fu);
        v9938_reg_write(v, target_reg, val);
        if (!(r & 0x80u)) {
            v->regs[17] = (uint8_t)((target_reg + 1u) & 0x3Fu);
        }
        break;
    }
    }
}

void v9938_advance(v9938_t *v, uint32_t t_states)
{
    uint16_t disp_end = (uint16_t)((v->regs[9] & 0x80u) ? 212u : 192u);

    v->t_line += t_states;
    while (v->t_line >= MSX_T_PER_LINE) {
        v->t_line -= MSX_T_PER_LINE;
        v->line = (uint16_t)((v->line + 1u) % MSX_LINES);

        /* Line interrupt (R19 / IE1) */
        if (v->regs[0] & R0_IE1) {
            uint16_t int_line = (uint16_t)((v->regs[19] + (v->regs[23] & 0xFF)) % 256u);
            if (v->line == int_line) {
                v->stat[1] |= 0x01u;
                v->irq_pending = 1;
            }
        }

        /* End of display / VBlank (FG) */
        if (v->line == disp_end) {
            v->stat[0] |= ST_FG;
            v->stat[2] |= ST_VR;
            if (v->regs[1] & R1_IE0) v->irq_pending = 1;
        } else if (v->line == 0) {
            v->stat[2] &= (uint8_t)~ST_VR;
        }
    }
}

int v9938_vblank_taken(v9938_t *v)
{
    if (!v->irq_pending) return 0;
    v->irq_pending = 0;
    return 1;
}

/* ---- Raster Renderer ---------------------------------------------------- */

void v9938_render(v9938_t *v, uint16_t *fb)
{
    uint8_t m = v9938_mode(v);
    int use_epal = (v->regs[20] & 0x01) != 0;
    uint8_t bg_idx = (uint8_t)(v->regs[7] & ((m == 0x1C && use_epal) ? 0xFFu : 0x0Fu));
    uint16_t bg = (m == 0x1C && use_epal) ? v->pal256[bg_idx] : v->pal[bg_idx];
    uint16_t y, x;
    uint16_t disp_lines = (v->regs[9] & 0x80u) ? 212u : 192u;
    uint16_t top_border = (disp_lines < MSX_SCREEN_H) ? (uint16_t)((MSX_SCREEN_H - disp_lines) / 2u) : 0u;
    uint16_t bot_border = (uint16_t)(top_border + disp_lines);

    if (!fb) return;

    if (!(v->regs[1] & R1_DISP)) {
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
        }
        return;
    }

    uint32_t page_base;
    if (m == 0x1C || m == 0x14) {
        /* Screen 8 (G7) / Screen 7 (G6): 64KB per page. Page 0 at 0x00000, Page 1 at 0x10000.
         * R#2 bit 5 (0x20) is A16 in G6/G7 modes, selecting Page 1. */
        page_base = (v->regs[2] & 0x20u) ? 0x10000u : 0u;
    } else if (m == 0x0C || m == 0x10) {
        /* Screen 5 (G4) / Screen 6 (G5): 32KB per page (4 pages in 128KB).
         * R#2 bits 6..5 select pages 0..3: 00->0, 01->32KB, 10->64KB, 11->96KB */
        uint8_t pidx = (uint8_t)((v->regs[2] >> 5) & 0x03u);
        page_base = (uint32_t)pidx * 0x8000u;
    } else {
        page_base = 0;
    }

    if (m == 0x1C) {
        /* SCREEN 8: Mode G7 (256x212, 8bpp RGB332 or V9968 256-color palette) */
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint32_t eff_y = ((uint32_t)(y - top_border) + (uint32_t)v->regs[23]) & 0xFFu;
            uint32_t line_addr = page_base + eff_y * 256u;
            for (x = 0; x < MSX_SCREEN_W; x++) {
                uint8_t c = v->vram[(line_addr + x) & (V9938_VRAM_SIZE - 1u)];
                row[x] = use_epal ? v->pal256[c] : rgb332_to_rgb565(c);
            }
        }
    } else if (m == 0x0C) {
        /* SCREEN 5: Mode G4 (256x212, 4bpp 16 colours) */
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint32_t eff_y = ((uint32_t)(y - top_border) + (uint32_t)v->regs[23]) & 0xFFu;
            uint32_t line_addr = page_base + eff_y * 128u;
            for (x = 0; x < MSX_SCREEN_W; x += 2) {
                uint8_t b = v->vram[(line_addr + (x >> 1)) & (V9938_VRAM_SIZE - 1u)];
                row[x]     = v->pal[b >> 4];
                row[x + 1] = v->pal[b & 0x0F];
            }
        }
    } else if (m == 0x14) {
        /* SCREEN 7: Mode G6 (512x212, 4bpp -> scaled to 256) */
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint32_t eff_y = ((uint32_t)(y - top_border) + (uint32_t)v->regs[23]) & 0xFFu;
            uint32_t line_addr = page_base + eff_y * 256u;
            for (x = 0; x < MSX_SCREEN_W; x++) {
                uint8_t b = v->vram[(line_addr + x) & (V9938_VRAM_SIZE - 1u)];
                row[x] = v->pal[b >> 4];
            }
        }
    } else if (m == 0x10) {
        /* SCREEN 6: Mode G5 (512x212, 2bpp -> scaled to 256) */
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint32_t eff_y = ((uint32_t)(y - top_border) + (uint32_t)v->regs[23]) & 0xFFu;
            uint32_t line_addr = page_base + eff_y * 128u;
            for (x = 0; x < MSX_SCREEN_W; x++) {
                uint8_t b = v->vram[(line_addr + (x >> 1)) & (V9938_VRAM_SIZE - 1u)];
                uint8_t col = (uint8_t)((x & 1) ? (b & 3) : ((b >> 4) & 3));
                row[x] = v->pal[col];
            }
        }
    } else if (m == 0x04 || m == 0x00) {
        /* SCREEN 2 (G2, m == 0x04) and SCREEN 1 (G1, m == 0x00) */
        uint32_t nt = (uint32_t)(v->regs[2] & 0x7Fu) << 10;
        uint32_t pt = (uint32_t)(v->regs[4] & (m == 0x04 ? 0x04u : 0x3Fu)) << 11;
        uint32_t ct = (uint32_t)(v->regs[3] & (m == 0x04 ? 0x80u : 0xFFu)) << 6;
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint16_t eff_y = (uint16_t)(y - top_border);
            uint16_t r = (uint16_t)(eff_y % 8u);
            uint16_t band = (uint16_t)(eff_y / 8u);
            uint32_t chunk = (uint32_t)(eff_y / 64u);
            for (x = 0; x < MSX_SCREEN_W; x++) {
                uint32_t cell = (uint32_t)band * 32u + (uint32_t)(x / 8u);
                uint8_t name = v->vram[(nt + cell) & (V9938_VRAM_SIZE - 1u)];
                uint8_t cb, bit;
                if (m == 0x04) {
                    uint32_t pat_off = ((((uint32_t)chunk & (v->regs[4] & 3u)) << 8) | name) * 8u + r;
                    uint32_t col_off = ((chunk * 256u + name) * 8u + r) &
                                       (((uint32_t)(v->regs[3] & 0x7Fu) << 6) | 0x3Fu);
                    cb  = v->vram[(ct + col_off) & (V9938_VRAM_SIZE - 1u)];
                    bit = (uint8_t)((v->vram[(pt + pat_off) & (V9938_VRAM_SIZE - 1u)] >> (7u - (x % 8u))) & 1u);
                } else {
                    cb  = v->vram[(ct + (name >> 3)) & (V9938_VRAM_SIZE - 1u)];
                    bit = (uint8_t)((v->vram[(pt + (uint32_t)name * 8u + r) & (V9938_VRAM_SIZE - 1u)] >> (7u - (x % 8u))) & 1u);
                }
                uint8_t c = bit ? (uint8_t)(cb >> 4) : (uint8_t)(cb & 0x0Fu);
                row[x] = c ? v->pal[c & 0x0Fu] : bg;
            }
        }
    } else if (m == 0x01) {
        /* SCREEN 0: Text 1 (40x24) */
        uint32_t nt = (uint32_t)(v->regs[2] & 0x7Fu) << 10;
        uint32_t pt = (uint32_t)(v->regs[4] & 0x3Fu) << 11;
        uint16_t fg = v->pal[(v->regs[7] >> 4) & 0x0Fu];
        for (y = 0; y < MSX_SCREEN_H; y++) {
            uint16_t *row = fb + (uint32_t)y * MSX_SCREEN_W;
            if (y < top_border || y >= bot_border) {
                for (x = 0; x < MSX_SCREEN_W; x++) row[x] = bg;
                continue;
            }
            uint16_t eff_y = (uint16_t)(y - top_border);
            uint16_t r = (uint16_t)(eff_y % 8u);
            uint16_t band = (uint16_t)(eff_y / 8u);
            for (x = 0; x < MSX_SCREEN_W; x++) {
                if (x < 8u || x >= 248u) { row[x] = bg; continue; }
                uint16_t tx = (uint16_t)(x - 8u);
                uint8_t glyph = v->vram[(nt + (uint32_t)band * 40u + (uint32_t)(tx / 6u)) & (V9938_VRAM_SIZE - 1u)];
                uint8_t bit = (uint8_t)((v->vram[(pt + (uint32_t)glyph * 8u + r) & (V9938_VRAM_SIZE - 1u)] >>
                                         (7u - (tx % 6u))) & 1u);
                row[x] = bit ? fg : bg;
            }
        }
    }

    /* ---- Sprites in V9938 ---- */
    if (!(v->regs[8] & 0x02u)) { /* SPD (bit 1 of R8) = 0: sprites enabled */
        uint16_t size = (v->regs[1] & 0x02u) ? 16u : 8u;
        uint8_t  mag  = (uint8_t)(v->regs[1] & 0x01u);
        uint16_t h    = mag ? (uint16_t)(size * 2u) : size;
        uint32_t spt  = (uint32_t)(v->regs[6] & 0x3Fu) << 11;

        if (m == 0x04 || m == 0x00 || m == 0x02) {
            /* Sprite Mode 1: TMS9918 compatible */
            uint32_t sat = (((uint32_t)(v->regs[11] & 0x03u) << 15) | ((uint32_t)(v->regs[5] & 0xFFu) << 7)) & (V9938_VRAM_SIZE - 1u);
            for (y = top_border; y < bot_border; y++) {
                uint16_t *row_px = fb + (uint32_t)y * MSX_SCREEN_W;
                uint16_t eff_y = (uint16_t)(y - top_border);
                uint8_t drawn = 0u;
                uint8_t s_front[MSX_SCREEN_W];
                memset(s_front, 0, sizeof(s_front));

                for (uint16_t n = 0u; n < 32u; n++) {
                    uint32_t a = (sat + (uint32_t)n * 4u) & (V9938_VRAM_SIZE - 1u);
                    uint16_t sy = v->vram[a];
                    if (sy == 0xD0u) break;

                    uint8_t row = (uint8_t)(eff_y - sy - 1u);
                    if (row >= h) continue;

                    if (drawn == 4u) {
                        v->stat[0] |= ST_5S;
                        break;
                    }
                    drawn++;

                    uint8_t  raw_x = v->vram[(a + 1u) & (V9938_VRAM_SIZE - 1u)];
                    uint8_t  tile  = (uint8_t)(v->vram[(a + 2u) & (V9938_VRAM_SIZE - 1u)] & (size == 16u ? 0xFCu : 0xFFu));
                    uint8_t  col   = v->vram[(a + 3u) & (V9938_VRAM_SIZE - 1u)];
                    uint8_t  color = (uint8_t)(col & 0x0Fu);
                    int      sx    = (col & 0x80u) ? ((int)raw_x - 32) : (int)raw_x;

                    if (color == 0u) continue;

                    uint16_t pat_row = mag ? (uint16_t)(row >> 1) : (uint16_t)row;

                    for (uint16_t i = 0u; i < h; i++) {
                        int px = sx + (int)i;
                        if (px < 0 || px >= (int)MSX_SCREEN_W) continue;

                        uint16_t pat_col = mag ? (uint16_t)(i >> 1) : i;
                        uint16_t t;
                        uint8_t  pat;

                        if (size == 16u) {
                            t = (uint16_t)(tile + (pat_col >> 3) * 2u + (pat_row >> 3));
                        } else {
                            t = (uint16_t)tile;
                        }
                        pat = v->vram[(spt + (uint32_t)t * 8u + (pat_row & 7u)) & (V9938_VRAM_SIZE - 1u)];
                        if (pat & (uint8_t)(0x80u >> (pat_col & 7u))) {
                            if (s_front[px]) {
                                v->stat[0] |= ST_C;
                            } else {
                                row_px[px] = v->pal[color & 0x0Fu];
                                s_front[px] = 1u;
                            }
                        }
                    }
                }
            }
        } else if (m == 0x0C || m == 0x1C || m == 0x14 || m == 0x10 || m == 0x08) {
            /* Sprite Mode 2: V9938 mode with Sprite Color Table */
            uint32_t sat_m2 = (((uint32_t)(v->regs[11] & 0x03u) << 15) | ((uint32_t)(v->regs[5] & 0xFCu) << 7)) & (V9938_VRAM_SIZE - 1u);
            uint32_t sct = (sat_m2 - 512u) & (V9938_VRAM_SIZE - 1u);
            for (y = top_border; y < bot_border; y++) {
                uint16_t *row_px = fb + (uint32_t)y * MSX_SCREEN_W;
                uint16_t eff_y = (uint16_t)(y - top_border);
                uint8_t drawn = 0u;
                uint8_t s_front[MSX_SCREEN_W];
                uint8_t s_color[MSX_SCREEN_W];
                uint8_t s_ic[MSX_SCREEN_W];
                memset(s_front, 0, sizeof(s_front));
                memset(s_color, 0, sizeof(s_color));
                memset(s_ic, 0, sizeof(s_ic));

                for (uint16_t n = 0u; n < 32u; n++) {
                    uint32_t a = (sat_m2 + (uint32_t)n * 4u) & (V9938_VRAM_SIZE - 1u);
                    uint16_t sy = v->vram[a];
                    if (sy == 216u) break;

                    uint8_t row = (uint8_t)(eff_y - sy - 1u);
                    if (row >= h) continue;

                    if (drawn == 8u) {
                        v->stat[0] |= ST_5S;
                        break;
                    }
                    drawn++;

                    uint8_t  raw_x = v->vram[(a + 1u) & (V9938_VRAM_SIZE - 1u)];
                    uint8_t  tile  = (uint8_t)(v->vram[(a + 2u) & (V9938_VRAM_SIZE - 1u)] & (size == 16u ? 0xFCu : 0xFFu));
                    uint16_t pat_row = mag ? (uint16_t)(row >> 1) : (uint16_t)row;
                    uint8_t  color_attr = v->vram[(sct + (uint32_t)n * 16u + pat_row) & (V9938_VRAM_SIZE - 1u)];

                    uint8_t  color = (uint8_t)(color_attr & 0x0Fu);
                    uint8_t  cc    = (uint8_t)(color_attr & 0x40u);
                    uint8_t  ic    = (uint8_t)(color_attr & 0x20u);
                    int      sx    = (color_attr & 0x80u) ? ((int)raw_x - 32) : (int)raw_x;

                    if (color == 0u) continue;

                    for (uint16_t i = 0u; i < h; i++) {
                        int px = sx + (int)i;
                        if (px < 0 || px >= (int)MSX_SCREEN_W) continue;

                        uint16_t pat_col = mag ? (uint16_t)(i >> 1) : i;
                        uint16_t t;
                        uint8_t  pat;

                        if (size == 16u) {
                            t = (uint16_t)(tile + (pat_col >> 3) * 2u + (pat_row >> 3));
                        } else {
                            t = (uint16_t)tile;
                        }
                        pat = v->vram[(spt + (uint32_t)t * 8u + (pat_row & 7u)) & (V9938_VRAM_SIZE - 1u)];
                        if (pat & (uint8_t)(0x80u >> (pat_col & 7u))) {
                            if (s_front[px]) {
                                if (cc) {
                                    s_color[px] |= color;
                                    row_px[px] = (m == 0x1C && !use_epal) ? s_g7_sprite_pal[s_color[px] & 0x0Fu] : v->pal[s_color[px] & 0x0Fu];
                                } else {
                                    if (!ic && !s_ic[px]) {
                                        v->stat[0] |= ST_C;
                                    }
                                }
                            } else {
                                s_front[px] = 1u;
                                s_color[px] = color;
                                s_ic[px] = ic ? 1u : 0u;
                                row_px[px] = (m == 0x1C && !use_epal) ? s_g7_sprite_pal[color & 0x0Fu] : v->pal[color & 0x0Fu];
                            }
                        }
                    }
                }
            }
        }
    }
}

void v9938_draw_line(v9938_t *v, int x0, int y0, int x1, int y1, uint8_t color, uint8_t op)
{
    if (!v) return;
    int mode = v9938_mode(v);
    int dx = (x1 >= x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 >= y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;

    for (;;) {
        v9938_pset_pixel(v, x, y, color, op, mode);
        if (x == x1 && y == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
    }
}

void v9938_draw_hline(v9938_t *v, int x0, int x1, int y, uint8_t color, uint8_t op)
{
    if (!v) return;
    int mode = v9938_mode(v);
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) {
        v9938_pset_pixel(v, x, y, color, op, mode);
    }
}

