/*
 * tronMSX — Y8950 (MSX-AUDIO). Clean-room C99, integer only (D4).
 *
 * The FM half is an OPL1 register file on I/O #C0 (address / status) and
 * #C1 (data). It uses the same integer phase generator and envelope as the
 * YM2413 (fm_int.h). Nine 2-operator channels, sine only. Connection bit 0
 * of #C0+ch selects FM (0) or additive (1). Key is bit 5 of #B0+ch.
 *
 * ADPCM-B registers written through #C0/#C1 sit in the register file and are
 * not rendered. The #C2/#C3 data ports are accepted and discarded. A read of
 * #C0 returns 0: no IRQ, not busy. Playback of ADPCM samples is a later step
 * on this same mix.
 */

#include "msx_int.h"
#include "fm_int.h"

#include <string.h>

static uint8_t s_addr;
static uint8_t s_regs[256];
static FmOp    s_op[18];
static int32_t s_fb[9];

static const uint8_t s_map[18] = {
    0, 1, 2, 3, 4, 5, 8, 9, 10, 11, 12, 13, 16, 17, 18, 19, 20, 21
};

static int ch_mod(int ch)
{
    return (ch % 3) + (ch / 3) * 6;
}

static uint8_t op_reg(uint8_t base, int op)
{
    return (uint8_t)(base + s_map[op]);
}

void y8950_init(void) { y8950_reset(); }

void y8950_reset(void)
{
    int i;
    memset(s_regs, 0, sizeof(s_regs));
    memset(s_op, 0, sizeof(s_op));
    memset(s_fb, 0, sizeof(s_fb));
    s_addr = 0u;
    for (i = 0; i < 18; i++) { s_op[i].eg = 511u; s_op[i].stage = 3u; }
}

uint8_t y8950_read(uint8_t port)
{
    if ((port & 0x03u) == 0u) return 0u;          /* status: idle */
    return 0xFFu;
}

void y8950_write(uint8_t port, uint8_t value)
{
    switch (port & 0x03u) {
    case 0u: s_addr = value; break;
    case 1u: s_regs[s_addr] = value; break;
    default: break;                               /* #C2/#C3 ADPCM data, stored by a later step */
    }
}

void y8950_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;

    if (!acc || !frames) return;
    for (i = 0u; i < frames; i++) {
        int32_t sum = 0;
        int ch;

        for (ch = 0; ch < 9; ch++) {
            int opm = ch_mod(ch);
            int opc = opm + 3;
            uint8_t rm = s_regs[op_reg(0x20, opm)];
            uint8_t rc = s_regs[op_reg(0x20, opc)];
            uint8_t km = s_regs[op_reg(0x40, opm)];
            uint8_t kc = s_regs[op_reg(0x40, opc)];
            uint8_t am = s_regs[op_reg(0x60, opm)];
            uint8_t ac = s_regs[op_reg(0x60, opc)];
            uint8_t sm = s_regs[op_reg(0x80, opm)];
            uint8_t sc = s_regs[op_reg(0x80, opc)];
            uint8_t b  = s_regs[0xB0 + ch];
            uint8_t creg = s_regs[0xC0 + ch];
            uint16_t fnum = (uint16_t)(s_regs[0xA0 + ch] | ((uint16_t)(b & 0x03u) << 8));
            uint8_t block = (uint8_t)((b >> 2) & 0x07u);
            uint8_t key = (uint8_t)((b >> 5) & 0x01u);
            uint8_t fb = (uint8_t)((creg >> 1) & 0x07u);
            uint8_t conn = (uint8_t)(creg & 0x01u);
            uint32_t inc_m, inc_c, mp;
            int32_t mod, car;

            fm_op_key(&s_op[opm], key, (uint8_t)(am >> 4));
            fm_op_key(&s_op[opc], key, (uint8_t)(ac >> 4));
            fm_op_env(&s_op[opm], (uint8_t)(am >> 4), (uint8_t)(am & 15u),
                      (uint8_t)(sm >> 4), (uint8_t)(sm & 15u),
                      (rm & 0x20u) ? 0 : 1);
            fm_op_env(&s_op[opc], (uint8_t)(ac >> 4), (uint8_t)(ac & 15u),
                      (uint8_t)(sc >> 4), (uint8_t)(sc & 15u),
                      (rc & 0x20u) ? 0 : 1);

            inc_m = fm_phase_inc(fnum, block, fm_mul_x2[rm & 15u]);
            inc_c = fm_phase_inc(fnum, block, fm_mul_x2[rc & 15u]);
            mp = s_op[opm].phase;
            if (fb) mp += ((uint32_t)s_fb[ch]) << (unsigned)(fb + 6);
            mod = fm_amp(fm_sin(mp), s_op[opm].eg, (uint16_t)((km & 0x3Fu) << 3));
            s_fb[ch] = mod;
            if (conn) {
                car = fm_amp(fm_sin(s_op[opc].phase), s_op[opc].eg,
                             (uint16_t)((kc & 0x3Fu) << 3));
                sum += (mod + car) >> 2;
            } else {
                car = fm_amp(fm_sin(s_op[opc].phase + (((uint32_t)mod) << 14)),
                             s_op[opc].eg,
                             (uint16_t)((kc & 0x3Fu) << 3));
                sum += car >> 2;
            }
            s_op[opm].phase += inc_m;
            s_op[opc].phase += inc_c;
        }
        acc[i * 2u]      += sum;
        acc[i * 2u + 1u] += sum;
    }
}
