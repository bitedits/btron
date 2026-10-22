/*
 * tronMSX — YM2413 (MSX-MUSIC / OPLL). Clean-room C99, integer only (D4).
 *
 * Register file, from the YM2413 application manual:
 *   #00–#07  user instrument (modulator, then carrier)
 *   #0E      rhythm enable is bit 5; when set, channels 6–8 are silent
 *            (rhythm voices are a later step, not melodic stand-ins)
 *   #10–#18  f-number low
 *   #20–#28  sustain / key / block / f-number bit 8
 *   #30–#38  instrument nibble and volume nibble (0 = loudest)
 * I/O is #7C (address) then #7D (data), write-only. One object serves MSX1
 * FM-PAC carts and the internal MSX-MUSIC latch on MSX2+ and turboR.
 *
 * The fifteen built-in patches are clean-room parameter sketches in the
 * published instrument order. They are not a Yamaha instrument ROM image.
 * Phase and envelope come from fm_int.h. A full carrier (volume 0, no
 * modulator) is about ±8k and sums with the PSG under the int16 saturate.
 */

#include "msx_int.h"
#include "fm_int.h"

#include <string.h>

static uint8_t s_addr;
static uint8_t s_regs[64];
static FmOp    s_op[18];
static int32_t s_fb[9];

/* Instrument order: violin, guitar, piano, flute, clarinet, oboe, trumpet,
 * organ, horn, synthesizer, harpsichord, vibraphone, synthesizer bass,
 * acoustic bass, electric guitar. Each row is the eight instrument bytes. */
static const uint8_t s_rom[15][8] = {
    { 0x61, 0x21, 0x18, 0x06, 0xF2, 0xF4, 0x24, 0x14 },
    { 0x13, 0x41, 0x10, 0x03, 0xF1, 0xF2, 0x23, 0x13 },
    { 0x01, 0x01, 0x20, 0x07, 0xF0, 0xF5, 0x55, 0x05 },
    { 0x21, 0x21, 0x28, 0x01, 0xF0, 0xF1, 0x00, 0x02 },
    { 0x22, 0x21, 0x1C, 0x02, 0xF3, 0xF2, 0x13, 0x04 },
    { 0x21, 0x22, 0x16, 0x02, 0xF2, 0xF3, 0x22, 0x13 },
    { 0x25, 0x21, 0x0C, 0x05, 0xF4, 0xF6, 0x43, 0x26 },
    { 0x01, 0x11, 0x08, 0x04, 0xF0, 0xF0, 0x10, 0x04 },
    { 0x23, 0x21, 0x14, 0x03, 0xE4, 0xF3, 0x32, 0x15 },
    { 0x41, 0x21, 0x12, 0x06, 0xF1, 0xF2, 0x21, 0x12 },
    { 0x02, 0x01, 0x1A, 0x05, 0xF8, 0xF4, 0x62, 0x17 },
    { 0x11, 0x21, 0x22, 0x02, 0xC2, 0xF1, 0x31, 0x06 },
    { 0x04, 0x21, 0x06, 0x01, 0xF2, 0xF2, 0x43, 0x28 },
    { 0x03, 0x21, 0x0A, 0x02, 0xE3, 0xF2, 0x34, 0x16 },
    { 0x12, 0x41, 0x0E, 0x04, 0xF3, 0xF5, 0x44, 0x27 }
};

static const uint8_t *patch(uint8_t inst)
{
    if (inst == 0u || inst > 15u) return s_regs;
    return s_rom[inst - 1u];
}

void opll_init(void) { opll_reset(); }

void opll_reset(void)
{
    int i;
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_op, 0, sizeof(s_op));
    memset(s_fb, 0, sizeof(s_fb));
    s_addr = 0u;
    for (i = 0; i < 18; i++) { s_op[i].eg = 511u; s_op[i].stage = 3u; }
}

void opll_address(uint8_t value)
{
    s_addr = (uint8_t)(value & 0x3Fu);
}

void opll_data(uint8_t value)
{
    s_regs[s_addr] = value;
}

void opll_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;
    int rhythm = (s_regs[0x0E] & 0x20u) ? 1 : 0;

    if (!acc || !frames) return;
    for (i = 0u; i < frames; i++) {
        int32_t sum = 0;
        int ch;

        for (ch = 0; ch < 9; ch++) {
            const uint8_t *pat;
            uint8_t inst, vol, key, block, fb;
            uint16_t fnum;
            uint32_t inc_m, inc_c, mp;
            int32_t mod, car;
            int opm, opc;
            uint16_t tl;

            if (rhythm && ch >= 6) continue;
            inst = (uint8_t)(s_regs[0x30 + ch] >> 4);
            vol  = (uint8_t)(s_regs[0x30 + ch] & 0x0Fu);
            fnum = (uint16_t)(s_regs[0x10 + ch] |
                              ((uint16_t)(s_regs[0x20 + ch] & 0x01u) << 8));
            block = (uint8_t)((s_regs[0x20 + ch] >> 1) & 0x07u);
            key   = (uint8_t)((s_regs[0x20 + ch] >> 4) & 0x01u);
            pat = patch(inst);
            fb = (uint8_t)(pat[3] & 0x07u);
            opm = ch * 2;
            opc = opm + 1;

            fm_op_key(&s_op[opm], key, (uint8_t)(pat[4] >> 4));
            fm_op_key(&s_op[opc], key, (uint8_t)(pat[5] >> 4));
            fm_op_env(&s_op[opm],
                      (uint8_t)(pat[4] >> 4), (uint8_t)(pat[4] & 15u),
                      (uint8_t)(pat[6] >> 4), (uint8_t)(pat[6] & 15u),
                      (pat[0] & 0x20u) ? 0 : 1);
            fm_op_env(&s_op[opc],
                      (uint8_t)(pat[5] >> 4), (uint8_t)(pat[5] & 15u),
                      (uint8_t)(pat[7] >> 4), (uint8_t)(pat[7] & 15u),
                      (pat[1] & 0x20u) ? 0 : 1);

            inc_m = fm_phase_inc(fnum, block, fm_mul_x2[pat[0] & 15u]);
            inc_c = fm_phase_inc(fnum, block, fm_mul_x2[pat[1] & 15u]);
            tl = (uint16_t)((pat[2] & 0x3Fu) << 3);
            mp = s_op[opm].phase;
            if (fb) mp += ((uint32_t)s_fb[ch]) << (unsigned)(fb + 6);
            mod = fm_amp(fm_sin(mp), s_op[opm].eg, tl);
            s_fb[ch] = mod;
            car = fm_amp(fm_sin(s_op[opc].phase + (((uint32_t)mod) << 14)),
                         s_op[opc].eg, (uint16_t)(vol << 5));
            s_op[opm].phase += inc_m;
            s_op[opc].phase += inc_c;
            sum += car >> 2;
        }
        acc[i * 2u]      += sum;
        acc[i * 2u + 1u] += sum;
    }
}
