/*
 * fm_int.h — integer helpers shared by the YM2413 and Y8950 renderers.
 *
 * Include after msx_int.h. No float and no libm on the machine path (D4):
 * the quarter sine is a static table, and the phase increment is the
 * published Yamaha relation
 *   f = fnum * multi * clock / (72 * 2^(19-block))
 * rearranged so a 32-bit phase (2^32 = one cycle) advances at MSX_PCM_HZ.
 */

#ifndef _BTRON_EMULATOR_MSX_FM_INT_H_
#define _BTRON_EMULATOR_MSX_FM_INT_H_

#include <stdint.h>

/* Quarter cycle, index 0 = 0, index 256 = full scale. */
static const int16_t fm_qsin[257] = {
        0,   201,   402,   603,   804,  1005,  1206,  1407,
     1608,  1809,  2009,  2210,  2410,  2611,  2811,  3012,
     3212,  3412,  3612,  3811,  4011,  4210,  4410,  4609,
     4808,  5007,  5205,  5404,  5602,  5800,  5998,  6195,
     6393,  6590,  6786,  6983,  7179,  7375,  7571,  7767,
     7962,  8157,  8351,  8545,  8739,  8933,  9126,  9319,
     9512,  9704,  9896, 10087, 10278, 10469, 10659, 10849,
    11039, 11228, 11417, 11605, 11793, 11980, 12167, 12353,
    12539, 12725, 12910, 13094, 13279, 13462, 13645, 13828,
    14010, 14191, 14372, 14553, 14732, 14912, 15090, 15269,
    15446, 15623, 15800, 15976, 16151, 16325, 16499, 16673,
    16846, 17018, 17189, 17360, 17530, 17700, 17869, 18037,
    18204, 18371, 18537, 18703, 18868, 19032, 19195, 19357,
    19519, 19680, 19841, 20000, 20159, 20317, 20475, 20631,
    20787, 20942, 21096, 21250, 21403, 21554, 21705, 21856,
    22005, 22154, 22301, 22448, 22594, 22739, 22884, 23027,
    23170, 23311, 23452, 23592, 23731, 23870, 24007, 24143,
    24279, 24413, 24547, 24680, 24811, 24942, 25072, 25201,
    25329, 25456, 25582, 25708, 25832, 25955, 26077, 26198,
    26319, 26438, 26556, 26674, 26790, 26905, 27019, 27133,
    27245, 27356, 27466, 27575, 27683, 27790, 27896, 28001,
    28105, 28208, 28310, 28411, 28510, 28609, 28706, 28803,
    28898, 28992, 29085, 29177, 29268, 29358, 29447, 29534,
    29621, 29706, 29791, 29874, 29956, 30037, 30117, 30195,
    30273, 30349, 30424, 30498, 30571, 30643, 30714, 30783,
    30852, 30919, 30985, 31050, 31113, 31176, 31237, 31297,
    31356, 31414, 31470, 31526, 31580, 31633, 31685, 31736,
    31785, 31833, 31880, 31926, 31971, 32014, 32057, 32098,
    32137, 32176, 32213, 32250, 32285, 32318, 32351, 32382,
    32412, 32441, 32469, 32495, 32521, 32545, 32567, 32589,
    32609, 32628, 32646, 32663, 32678, 32692, 32705, 32717,
    32728, 32737, 32745, 32752, 32757, 32761, 32765, 32766,
    32767
};

/* MULTI nibble as a ×2 numerator: 0 → ½, 1 → 1, …, 15 → 15. */
static const uint8_t fm_mul_x2[16] = {
    1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30
};

typedef struct FmOp {
    uint32_t phase;
    uint16_t eg;       /* 0 = full amplitude, 511 = silent */
    uint16_t eg_ct;
    uint8_t  stage;    /* 0 attack, 1 decay, 2 sustain, 3 release */
    uint8_t  key;
} FmOp;

static inline int32_t fm_sin(uint32_t phase)
{
    uint32_t idx = phase >> 22;             /* 0..1023 over the cycle */
    uint32_t quad = idx >> 8;
    uint32_t q = idx & 255u;
    int32_t s = (quad & 1u) ? (int32_t)fm_qsin[256u - q]
                            : (int32_t)fm_qsin[q];
    if (quad & 2u) s = -s;
    return s;
}

static inline uint32_t fm_phase_inc(uint32_t fnum, uint32_t block, uint32_t mul_x2)
{
    uint64_t num;
    if (!fnum || !mul_x2) return 0u;
    if (block > 7u) block = 7u;
    num = (uint64_t)MSX_Z80_HZ * (uint64_t)fnum * (uint64_t)mul_x2;
    num <<= block;
    num <<= 12;                             /* ×2^13 / 2, because mul is ×2 */
    return (uint32_t)(num / (72ull * (uint64_t)MSX_PCM_HZ));
}

static inline int32_t fm_amp(int32_t s, uint16_t eg, uint16_t tl)
{
    uint16_t att = (uint16_t)(eg + tl);
    if (att > 511u) att = 511u;
    return (s * (int32_t)(511u - att)) >> 9;
}

/* Rising key restarts the operator. AR 15 reaches full amplitude immediately. */
static inline void fm_op_key(FmOp *o, int key, uint8_t ar)
{
    if (key && !o->key) {
        o->phase = 0u;
        o->stage = 0u;
        o->eg = 511u;
        o->eg_ct = 0u;
        if (ar >= 15u) { o->eg = 0u; o->stage = 1u; }
    } else if (!key && o->key) {
        o->stage = 3u;
        o->eg_ct = 0u;
    }
    o->key = (uint8_t)(key ? 1u : 0u);
}

static inline void fm_op_env(FmOp *o, uint8_t ar, uint8_t dr, uint8_t sl,
                             uint8_t rr, int percussive)
{
    uint16_t sl_level = (sl >= 15u) ? 511u : (uint16_t)((uint16_t)sl << 5);
    uint8_t rate;
    uint16_t period;
    uint16_t step;

    if (o->stage == 2u && !percussive) return;
    if (o->stage == 0u) rate = ar;
    else if (o->stage == 1u) rate = dr;
    else rate = rr;
    if (rate == 0u) return;
    period = (rate >= 15u) ? 1u : (uint16_t)(1u << (14u - rate));
    if (++o->eg_ct < period) return;
    o->eg_ct = 0u;
    if (o->stage == 0u) {
        step = (uint16_t)((o->eg >> 4) + 1u);
        if (o->eg <= step) { o->eg = 0u; o->stage = 1u; }
        else o->eg = (uint16_t)(o->eg - step);
        return;
    }
    step = (uint16_t)(((511u - o->eg) >> 5) + 1u);
    if ((uint16_t)(o->eg + step) > 511u) o->eg = 511u;
    else o->eg = (uint16_t)(o->eg + step);
    if (o->stage == 1u && o->eg >= sl_level) {
        o->eg = sl_level;
        o->stage = (uint8_t)(percussive ? 3u : 2u);
    }
}

#endif
