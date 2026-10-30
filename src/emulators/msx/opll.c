/*
 * tronMSX — YM2413 (MSX-MUSIC / OPLL). Clean-room C99, integer only (D4).
 *
 * Register file, from the YM2413 application manual:
 *   #00–#07  user instrument (modulator, then carrier)
 *   #0E      bit 5 selects rhythm. Bits 4–0 key bass drum, snare, tom,
 *            top cymbal and hi-hat. Channels 6–8 then stop being melodic.
 *            #36–#38 hold the five rhythm volumes (0 = loudest).
 *   #10–#18  f-number low
 *   #20–#28  sustain / key / block / f-number bit 8
 *   #30–#38  instrument nibble and volume nibble (0 = loudest)
 * I/O is #7C (address) then #7D (data), write-only. One object serves MSX1
 * FM-PAC carts and the internal MSX-MUSIC latch on MSX2+ and turboR.
 *
 * The fifteen built-in patches are the YM2413 instrument ROM values as
 * read from the decapped die (the same values the Nuke.YKT die-shot work
 * published), packed into the register byte layout. Only the 15 x 8
 * parameter bytes are carried, no code or tables of any emulator.
 * Rhythm instrument bytes and the hi-hat / snare / top-cymbal phase mix are
 * the published hardware measurements (register bytes and phase-bit
 * formulas), reimplemented here. Phase and log amplitude come from fm_int.h.
 * The envelope clock, rate key scale, vibrato and tremolo follow the measured
 * YM2413 laws. Level key scale is not applied. A full carrier (volume 0) is
 * shifted down 2 so one melodic channel sits near a PSG channel. Percussion
 * is shifted down 1: the chip emits each rhythm voice twice (+6 dB).
 */

#include "msx_int.h"
#include "fm_int.h"

#include <string.h>

static uint8_t s_addr;
static uint8_t s_regs[64];
static FmOp    s_op[18];
static int32_t s_fb[9][2];
static int32_t s_last_out;
static uint32_t s_noise = 1u;
static uint32_t s_eg_clock;             /* chip samples, FM clock / 72      */
static uint32_t s_clk_rem;

/* Authentic Yamaha YM2413 ROM parameter tables. Instrument order:
 * violin, guitar, piano, flute, clarinet, oboe, trumpet,
 * organ, horn, synthesizer, harpsichord, vibraphone, synthesizer bass,
 * acoustic bass, electric guitar. Each row is the eight instrument bytes. */
static const uint8_t s_rom[15][8] = {
    { 0x71, 0x61, 0x1e, 0x17, 0xd0, 0x78, 0x00, 0x17 }, /* Violin */
    { 0x13, 0x41, 0x1a, 0x0d, 0xd8, 0xf7, 0x23, 0x13 }, /* Guitar */
    { 0x13, 0x01, 0x99, 0x00, 0xf2, 0xc4, 0x11, 0x23 }, /* Piano */
    { 0x31, 0x61, 0x0e, 0x07, 0xa8, 0x64, 0x70, 0x27 }, /* Flute */
    { 0x32, 0x21, 0x1e, 0x06, 0xe0, 0x76, 0x00, 0x28 }, /* Clarinet */
    { 0x31, 0x22, 0x16, 0x05, 0xe0, 0x71, 0x00, 0x18 }, /* Oboe */
    { 0x21, 0x61, 0x1d, 0x07, 0x82, 0x81, 0x10, 0x07 }, /* Trumpet */
    { 0x23, 0x21, 0x2d, 0x14, 0xa2, 0x72, 0x00, 0x07 }, /* Organ */
    { 0x61, 0x61, 0x1b, 0x06, 0x64, 0x65, 0x10, 0x17 }, /* Horn */
    { 0x41, 0x61, 0x0b, 0x18, 0x85, 0xf7, 0x71, 0x07 }, /* Synthesizer */
    { 0x13, 0x01, 0x83, 0x11, 0xfa, 0xe4, 0x10, 0x04 }, /* Harpsichord */
    { 0x17, 0xc1, 0x24, 0x07, 0xf8, 0xf8, 0x22, 0x12 }, /* Vibraphone */
    { 0x61, 0x50, 0x0c, 0x05, 0xc2, 0xf5, 0x20, 0x42 }, /* Synthesizer Bass */
    { 0x01, 0x01, 0x55, 0x03, 0xc9, 0x95, 0x03, 0x02 }, /* Acoustic Bass */
    { 0x61, 0x41, 0x89, 0x03, 0xf1, 0xe4, 0x40, 0x13 }  /* Electric Guitar */
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
    s_noise = 1u;
    s_eg_clock = 0u;
    s_clk_rem = 0u;
    s_last_out = 0;
    for (i = 0; i < 18; i++) { s_op[i].eg = 511u; s_op[i].stage = 3u; }
}

void opll_address(uint8_t value)
{
    s_addr = (uint8_t)(value & 0x3Fu);
}

void opll_data(uint8_t value)
{
    /* A rhythm key that falls and rises again before the next mix must
     * still attack. The mixer only sees the register file at the end of
     * the frame, so the rising write clears the key latch now. */
    if (s_addr == 0x0Eu) {
        uint8_t rise = (uint8_t)(value & (uint8_t)~s_regs[0x0E]);
        if (rise & 0x10u) { s_op[12].key = 0u; s_op[13].key = 0u; }
        if (rise & 0x08u) s_op[15].key = 0u;
        if (rise & 0x04u) s_op[16].key = 0u;
        if (rise & 0x02u) s_op[17].key = 0u;
        if (rise & 0x01u) s_op[14].key = 0u;
    }
    s_regs[s_addr] = value;
}

/* Measured rhythm rows. Don't-care bytes are 0. Bass drum is the 2-operator
 * instrument on channel 6. Hi-hat (mod7) and snare (car7) share channel 7.
 * Tom (mod8) and top cymbal (car8) share channel 8. KSL on these rows is 0. */
static const uint8_t s_bd[8]  = { 0x01, 0x01, 0x18, 0x0F, 0xDF, 0xF8, 0x6A, 0x6D };
static const uint8_t s_hh[8]  = { 0x01, 0x00, 0x00, 0x00, 0xC8, 0x00, 0xA7, 0x00 };
static const uint8_t s_sd[8]  = { 0x00, 0x01, 0x00, 0x00, 0x00, 0xD8, 0x00, 0x68 };
static const uint8_t s_tom[8] = { 0x05, 0x00, 0x00, 0x00, 0xF8, 0x00, 0x59, 0x00 };
static const uint8_t s_cy[8]  = { 0x00, 0x01, 0x00, 0x00, 0x00, 0xAA, 0x00, 0x55 };

static uint16_t ch_fnum(int ch, uint8_t *block)
{
    uint16_t fnum = (uint16_t)(s_regs[0x10 + ch] |
                    ((uint16_t)(s_regs[0x20 + ch] & 0x01u) << 8));
    *block = (uint8_t)((s_regs[0x20 + ch] >> 1) & 0x07u);
    return fnum;
}

/* Half-sine (DM on the modulator, DC on the carrier): the negative half
 * of the cycle is zero. Bit 31 of the 32-bit phase is that half. */
static int32_t fm_wave(uint32_t phase, int half)
{
    if (half && (phase & 0x80000000u)) return 0;
    return fm_sin(phase);
}

/* Feedback shift fb+6 of a full-scale sample stays inside int32 for fb ≤ 7. */
static uint32_t fm_feedback(uint32_t phase, int32_t prev, unsigned fb)
{
    unsigned sh;
    if (!fb) return phase;
    sh = fb + 6u;
    return phase + (uint32_t)(prev * (int32_t)(1u << sh));
}

/* Chip samples covered by one PCM frame. FM clock is MSX_Z80_HZ/72. */
static uint32_t clock_advance(void)
{
    uint32_t n = 0u;
    s_clk_rem += MSX_Z80_HZ / 72u;
    while (s_clk_rem >= MSX_PCM_HZ) {
        s_clk_rem -= MSX_PCM_HZ;
        n++;
    }
    return n;
}

/* KSR=0 keeps the top two block bits. KSR=1 uses block and f-number bit 8.
 * R=0 forces rate 0. Otherwise RATE = min(63, 4*R + Rks). */
static uint8_t key_rks(uint8_t block, uint16_t fnum, int ksr)
{
    uint8_t bf = (uint8_t)(((block & 7u) << 1) | ((fnum >> 8) & 1u));
    return ksr ? bf : (uint8_t)(bf >> 2);
}

static uint8_t eff_rate(uint8_t r, uint8_t ks)
{
    unsigned rate;
    if (r == 0u) return 0u;
    rate = 4u * (unsigned)r + ks;
    return (uint8_t)(rate > 63u ? 63u : rate);
}

/* How many 0.375 dB levels this chip sample moves. 127 means attack snaps
 * to full. Rates 0..3 stay put. The 8-step pattern is the measured one:
 * low two rate bits pick the row, and a step lands only when the clock's
 * low eg_shift bits are zero. */
static const uint8_t s_eg_seq[4][8] = {
    { 0, 1, 0, 1, 0, 1, 0, 1 },
    { 0, 1, 0, 1, 1, 1, 0, 1 },
    { 0, 1, 1, 1, 0, 1, 1, 1 },
    { 0, 1, 1, 1, 1, 1, 1, 1 }
};

static uint8_t eg_advance(uint32_t clock, uint8_t rate, int attack)
{
    unsigned shift, idx;
    if (rate < 4u) return 0u;
    if (attack && rate >= 60u) return 127u;
    if (!attack && rate >= 60u) return 2u;
    if (rate >= 56u) {
        idx = clock & 7u;
        return s_eg_seq[rate & 3u][idx] ? 2u : 1u;
    }
    shift = 13u - (rate >> 2);
    if ((clock & ((1u << shift) - 1u)) != 0u) return 0u;
    idx = (clock >> shift) & 7u;
    return s_eg_seq[rate & 3u][idx] ? 1u : 0u;
}

static void env_one(FmOp *o, uint8_t ar, uint8_t dr, uint8_t sl, uint8_t rr,
                    int sustained, uint8_t ks, uint32_t clock)
{
    uint8_t rate, adv;
    uint16_t sl_level = (sl >= 15u) ? 511u : (uint16_t)((uint16_t)sl << 5);
    uint16_t level;

    if (o->stage == 2u && sustained) return;
    if (o->stage == 0u) rate = eff_rate(ar, ks);
    else if (o->stage == 1u) rate = eff_rate(dr, ks);
    else rate = eff_rate(rr, ks);
    adv = eg_advance(clock, rate, o->stage == 0u);
    if (!adv) return;
    if (o->stage == 0u) {
        if (adv >= 64u) { o->eg = 0u; o->stage = 1u; return; }
        level = (uint16_t)(o->eg >> 2);
        if (level <= adv) { o->eg = 0u; o->stage = 1u; }
        else o->eg = (uint16_t)((level - adv) << 2);
        return;
    }
    o->eg = (uint16_t)(o->eg + (uint16_t)adv * 4u);
    if (o->eg > 511u) o->eg = 511u;
    if (o->stage == 1u && o->eg >= sl_level) {
        o->eg = sl_level;
        o->stage = (uint8_t)(sustained ? 2u : 3u);
    }
}

static void op_tick(FmOp *o, int key, const uint8_t *pat, int modulator,
                    uint8_t block, uint16_t fnum, uint32_t clk, uint32_t nclk)
{
    uint8_t ar, dr, sl, rr, row, ks;
    uint32_t k;
    row = modulator ? pat[0] : pat[1];
    ar = (uint8_t)((modulator ? pat[4] : pat[5]) >> 4);
    dr = (uint8_t)((modulator ? pat[4] : pat[5]) & 15u);
    sl = (uint8_t)((modulator ? pat[6] : pat[7]) >> 4);
    rr = (uint8_t)((modulator ? pat[6] : pat[7]) & 15u);
    ks = key_rks(block, fnum, (row & 0x10u) != 0);
    fm_op_key(o, key, ar);
    for (k = 0u; k < nclk; k++)
        env_one(o, ar, dr, sl, rr, (row & 0x20u) != 0, ks, clk + 1u + k);
}

/* Vibrato adds an integer to 2*fnum. Column 2 of the 8-step cycle is the
 * top 3 f-number bits; the neighbours are that value shifted down one.
 * One step per 1024 chip samples is 6.1 Hz. */
static int vib_pm(uint16_t fnum, uint32_t clock)
{
    unsigned col = (unsigned)((clock / 1024u) & 7u);
    int peak = (int)((fnum >> 6) & 7u);
    int side = peak >> 1;
    switch (col) {
    case 1: case 3: return side;
    case 2: return peak;
    case 5: case 7: return -side;
    case 6: return -peak;
    default: return 0;
    }
}

/* Same phase law as fm_phase_inc. vib is the measured f-number correction
 * on the doubled f-number, so vib 0 matches the unmodulated increment. */
static uint32_t phase_inc(uint32_t fnum, uint32_t block, uint32_t mul_x2, int vib)
{
    int32_t f2;
    uint64_t num;
    if (!mul_x2) return 0u;
    if (block > 7u) block = 7u;
    f2 = (int32_t)(fnum * 2u) + vib;
    if (f2 <= 0) return 0u;
    num = (uint64_t)MSX_Z80_HZ * (uint64_t)(uint32_t)f2 * (uint64_t)mul_x2;
    num <<= block;
    num <<= 11;
    return (uint32_t)(num / (72ull * (uint64_t)MSX_PCM_HZ));
}

/* Tremolo: attenuation 0..13 (4.8 dB). Zero is held 15 steps, 1..12 for
 * eight steps each way, 13 for three. Each step is 64 chip samples. */
static uint8_t am_level(uint32_t clock)
{
    uint32_t step = (clock / 64u) % 210u;
    if (step < 15u) return 0u;
    step -= 15u;
    if (step < 96u) return (uint8_t)(1u + step / 8u);
    step -= 96u;
    if (step < 3u) return 13u;
    step -= 3u;
    return (uint8_t)(12u - step / 8u);
}

static uint16_t am_att(uint8_t row, uint32_t clock)
{
    if (!(row & 0x80u)) return 0u;
    return (uint16_t)((uint16_t)am_level(clock) * 4u);
}

/* 10-bit phase bit n. fm_sin uses phase>>22 as that index. */
static unsigned phase_bit(uint32_t phase, unsigned n)
{
    return (phase >> (22u + n)) & 1u;
}

/* Shared by hi-hat and top cymbal. Bits are from mod7 (HH) and car8 (CY). */
static unsigned rhythm_bit(void)
{
    uint32_t m7 = s_op[14].phase;
    uint32_t c8 = s_op[17].phase;
    unsigned c85 = phase_bit(c8, 5);
    unsigned c83 = phase_bit(c8, 3);
    unsigned m77 = phase_bit(m7, 7);
    unsigned m72 = phase_bit(m7, 2);
    unsigned m73 = phase_bit(m7, 3);
    return (c85 ^ c83) | (m77 ^ m72) | (c85 ^ m73);
}

/* x^23 + x^9 + 1, one step. The waveform reads bit 0 before the step.
 * Hi-hat and snare are three operator slots apart, so the snare reads the
 * bit three steps later. Eighteen steps close one chip sample. */
static void noise_step(void)
{
    uint32_t bit = (s_noise ^ (s_noise >> 14)) & 1u;
    s_noise = (bit << 22) | (s_noise >> 1);
}

static void noise_hats(int *hh, int *sd)
{
    int i;
    *hh = (int)(s_noise & 1u);
    for (i = 0; i < 3; i++) noise_step();
    *sd = (int)(s_noise & 1u);
    for (i = 0; i < 15; i++) noise_step();
}

static int32_t sin10(unsigned pg)
{
    return fm_sin((uint32_t)pg << 22);
}

static int32_t bass_drum(int key, uint16_t vol, uint32_t clk, uint32_t nclk)
{
    const uint8_t *pat = s_bd;
    uint8_t block;
    uint16_t fnum = ch_fnum(6, &block);
    int pm_m = (pat[0] & 0x40u) ? vib_pm(fnum, clk) : 0;
    int pm_c = (pat[1] & 0x40u) ? vib_pm(fnum, clk) : 0;
    uint32_t inc_m = phase_inc(fnum, block, fm_mul_x2[pat[0] & 15u], pm_m);
    uint32_t inc_c = phase_inc(fnum, block, fm_mul_x2[pat[1] & 15u], pm_c);
    uint32_t mp;
    int32_t mod, car;
    int32_t fb_avg;
    op_tick(&s_op[12], key, pat, 1, block, fnum, clk, nclk);
    op_tick(&s_op[13], key, pat, 0, block, fnum, clk, nclk);
    fb_avg = (s_fb[6][0] + s_fb[6][1]) >> 1;
    mp = fm_feedback(s_op[12].phase, fb_avg, (unsigned)(pat[3] & 7u));
    mod = fm_amp(fm_wave(mp, pat[3] & 0x08), s_op[12].eg,
                 (uint16_t)(((pat[2] & 0x3Fu) << 3) + am_att(pat[0], clk)));
    s_fb[6][1] = s_fb[6][0];
    s_fb[6][0] = mod;
    car = fm_amp(fm_wave(s_op[13].phase + ((uint32_t)mod << 14), pat[3] & 0x10),
                 s_op[13].eg, (uint16_t)((vol << 5) + am_att(pat[1], clk)));
    s_op[12].phase += inc_m;
    s_op[13].phase += inc_c;
    return car >> 1;
}

static int32_t rhythm_sample(uint32_t clk, uint32_t nclk)
{
    uint8_t rhy = s_regs[0x0E];
    uint8_t block7, block8;
    uint16_t f7 = ch_fnum(7, &block7);
    uint16_t f8 = ch_fnum(8, &block8);
    unsigned rm, pg, b8;
    int hh_n, sd_n;
    int32_t sum = 0;
    int32_t s;
    int pm;

    noise_hats(&hh_n, &sd_n);
    rm = rhythm_bit();

    op_tick(&s_op[14], (rhy & 0x01u) != 0, s_hh, 1, block7, f7, clk, nclk);
    op_tick(&s_op[15], (rhy & 0x08u) != 0, s_sd, 0, block7, f7, clk, nclk);
    op_tick(&s_op[16], (rhy & 0x04u) != 0, s_tom, 1, block8, f8, clk, nclk);
    op_tick(&s_op[17], (rhy & 0x02u) != 0, s_cy, 0, block8, f8, clk, nclk);

    sum += bass_drum((rhy & 0x10u) != 0, (uint16_t)(s_regs[0x36] & 0x0Fu),
                     clk, nclk);

    /* Hi-hat: sine of a phase built from rm_bit and the noise bit. */
    pg = (rm << 9) | ((rm ^ (unsigned)hh_n) ? 0xD0u : 0x34u);
    s = fm_amp(sin10(pg), s_op[14].eg,
               (uint16_t)(((s_regs[0x37] >> 4) << 5) + am_att(s_hh[0], clk)));
    sum += s >> 1;

    /* Snare: sign follows hi-hat phase bit 8. Full scale when noise differs. */
    b8 = phase_bit(s_op[14].phase, 8);
    pg = (b8 << 9) | ((b8 ^ (unsigned)sd_n) << 8);
    s = fm_amp(sin10(pg), s_op[15].eg,
               (uint16_t)(((s_regs[0x37] & 0x0Fu) << 5) + am_att(s_sd[1], clk)));
    sum += s >> 1;

    s = fm_amp(fm_sin(s_op[16].phase), s_op[16].eg,
               (uint16_t)(((s_regs[0x38] >> 4) << 5) + am_att(s_tom[0], clk)));
    sum += s >> 1;

    /* Top cymbal: rm_bit selects +full or −full through the sine peak. */
    pg = (rm << 9) | 0x100u;
    s = fm_amp(sin10(pg), s_op[17].eg,
               (uint16_t)(((s_regs[0x38] & 0x0Fu) << 5) + am_att(s_cy[1], clk)));
    sum += s >> 1;

    pm = (s_hh[0] & 0x40u) ? vib_pm(f7, clk) : 0;
    s_op[14].phase += phase_inc(f7, block7, fm_mul_x2[s_hh[0] & 15u], pm);
    pm = (s_sd[1] & 0x40u) ? vib_pm(f7, clk) : 0;
    s_op[15].phase += phase_inc(f7, block7, fm_mul_x2[s_sd[1] & 15u], pm);
    pm = (s_tom[0] & 0x40u) ? vib_pm(f8, clk) : 0;
    s_op[16].phase += phase_inc(f8, block8, fm_mul_x2[s_tom[0] & 15u], pm);
    pm = (s_cy[1] & 0x40u) ? vib_pm(f8, clk) : 0;
    s_op[17].phase += phase_inc(f8, block8, fm_mul_x2[s_cy[1] & 15u], pm);
    return sum;
}

void opll_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;
    int rhythm = (s_regs[0x0E] & 0x20u) ? 1 : 0;

    if (!acc || !frames) return;
    for (i = 0u; i < frames; i++) {
        int32_t sum = 0;
        int ch;
        uint32_t nclk = clock_advance();
        uint32_t clk = s_eg_clock;

        s_eg_clock += nclk;
        for (ch = 0; ch < 9; ch++) {
            const uint8_t *pat;
            uint8_t inst, vol, key, block, fb;
            uint16_t fnum;
            uint32_t inc_m, inc_c, mp;
            int32_t mod, car;
            int opm, opc;
            uint16_t tl;
            int pm_m, pm_c;

            if (rhythm && ch >= 6) continue; /* replaced by rhythm_sample */
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

            op_tick(&s_op[opm], key, pat, 1, block, fnum, clk, nclk);
            op_tick(&s_op[opc], key, pat, 0, block, fnum, clk, nclk);

            pm_m = (pat[0] & 0x40u) ? vib_pm(fnum, s_eg_clock) : 0;
            pm_c = (pat[1] & 0x40u) ? vib_pm(fnum, s_eg_clock) : 0;
            inc_m = phase_inc(fnum, block, fm_mul_x2[pat[0] & 15u], pm_m);
            inc_c = phase_inc(fnum, block, fm_mul_x2[pat[1] & 15u], pm_c);
            tl = (uint16_t)(((pat[2] & 0x3Fu) << 3) + am_att(pat[0], s_eg_clock));
            int32_t fb_avg = (s_fb[ch][0] + s_fb[ch][1]) >> 1;
            mp = fm_feedback(s_op[opm].phase, fb_avg, fb);
            mod = fm_amp(fm_wave(mp, pat[3] & 0x08), s_op[opm].eg, tl);
            s_fb[ch][1] = s_fb[ch][0];
            s_fb[ch][0] = mod;
            car = fm_amp(fm_wave(s_op[opc].phase + (((uint32_t)mod) << 14),
                                 pat[3] & 0x10),
                         s_op[opc].eg,
                         (uint16_t)((vol << 5) + am_att(pat[1], s_eg_clock)));
            s_op[opm].phase += inc_m;
            s_op[opc].phase += inc_c;
            sum += car >> 2;
        }
        if (rhythm) sum += rhythm_sample(clk, nclk);
        int32_t filtered = (sum + s_last_out) >> 1;
        s_last_out = sum;
        acc[i * 2u]      += filtered;
        acc[i * 2u + 1u] += filtered;
    }
}
