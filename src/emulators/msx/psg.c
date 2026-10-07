/*
 * tronMSX - AY-3-8910 PSG: register file, three tone generators, the 17-bit
 * noise shift register, the envelope sequencer and the fixed-point resampler
 * that feeds B-Audio's PCM ring.  B-MSX Rev 1.05, section 2.4.
 *
 * Clean-room C99.  Integer only (D4): the resampler is 32.32 fixed point and
 * the only pseudo-random source is the noise shift register, which starts at
 * 0x0ACE and is the state AV-5 compares against a reference sequence.
 *
 * Clock law: one PSG tick is two Z80 T-states, so a frame of 59,664 T is
 * 29,832 ticks; 735 PCM frames at 44,100 Hz consume 40.5844 ticks each and
 * the remainder stays in the fraction (D1: no host clock, only T).
 *
 * Generator laws, as in the AY-3-8910 data book:
 *   tone   : output toggles every N ticks,  N = max(1, R1<<8 | R0)
 *   noise  : register shifts every N ticks, N = max(1, R6), output is bit 0
 *   envelope: one of 32 steps every N ticks, N = max(1, R12<<8 | R11)
 *   mixer  : a channel sounds when (tone disabled or tone) and (noise
 *            disabled or noise), volumes are the 16-entry table below
 * The enable bits in R7 are inverted, as they are on the chip.
 */

#include "msx_int.h"

#include <string.h>

#define PSG_NR_REGS     16u

/* 32.32 step of PSG ticks per PCM frame: MSX_PSG_HZ / MSX_PCM_HZ. */
#define PSG_STEP      (((uint64_t)MSX_PSG_HZ << 32) / (uint64_t)MSX_PCM_HZ)
#define PSG_STEP_HI   ((uint32_t)(PSG_STEP >> 32))
#define PSG_STEP_LO   ((uint32_t)(PSG_STEP & 0xFFFFFFFFu))

/* 1.5 dB per step, normalised so that level 15 is full scale (section 2.4). */
static const uint8_t s_vol16[16] = {
      0,  51,  57,  64,  71,  80,  90, 101,
    113, 127, 143, 160, 180, 202, 227, 255
};

static uint8_t   s_reg[PSG_NR_REGS];
static uint8_t   s_select;
static uint8_t   s_pa_out;              /* port A drive value                 */
static uint8_t   s_pb_out;              /* port B drive value, bit6 = select  */

static uint32_t  s_tone_c[3];
static uint32_t  s_tone_p[3];
static uint8_t   s_tone_o[3];

static uint32_t  s_noise_c;
static uint32_t  s_noise_p;
static uint16_t  s_lfsr;                /* 17 bits, seed 0x0ACE               */
static uint8_t   s_noise_o;

static uint32_t  s_env_c;
static uint32_t  s_env_p;
static uint8_t   s_env_pos;             /* 0..31 within the current run       */
static uint8_t   s_env_dec;             /* 1 = counting down                  */
static uint8_t   s_env_done;            /* the run has finished               */
static uint8_t   s_env_vol;

static uint32_t  s_frac;                /* fractional part of the resampler   */

/* ---- register decode ---------------------------------------------------- */

static void tone_period(uint8_t ch)
{
    uint32_t v = (uint32_t)s_reg[ch * 2u] |
                 ((uint32_t)(s_reg[ch * 2u + 1u] & 0x0Fu) << 8);
    s_tone_p[ch] = (v == 0u) ? 1u : v;
}

static void noise_period(void)
{
    uint32_t v = (uint32_t)(s_reg[6] & 0x1Fu);
    s_noise_p = (v == 0u) ? 1u : v;
}

static void env_period(void)
{
    uint32_t v = (uint32_t)s_reg[11] | ((uint32_t)s_reg[12] << 8);
    s_env_p = (v == 0u) ? 1u : v;
}

static void reg_write(uint8_t reg, uint8_t value)
{
    s_reg[reg] = value;
    if (reg <= 5u)            tone_period((uint8_t)(reg >> 1));
    else if (reg == 6u)       noise_period();
    else if (reg == 11u || reg == 12u) env_period();
    else if (reg == 14u) s_pa_out = value;               /* port A drive      */
    else if (reg == 15u) s_pb_out = value;   /* port B drive: bit6 socket, bit7 KANA */
    else if (reg == 13u) {                /* a new shape restarts the run     */
        s_env_pos = 0u;
        s_env_dec = (uint8_t)((value & 0x04u) ? 0u : 1u);
        s_env_done = 0u;
        s_env_c = 0u;
    }
}

/* ---- one PSG tick ------------------------------------------------------- */

static void env_step(void)
{
    uint8_t shape = (uint8_t)(s_reg[13] & 0x0Fu);

    if (s_env_done) {
        if (shape & 0x01u) return;                 /* hold: nothing moves     */
        if (shape & 0x08u) {                       /* continue: run again     */
            s_env_done = 0u;
            if (shape & 0x02u) s_env_dec ^= 1u;
        } else {
            s_env_vol = 0u;                        /* fell silent             */
            return;
        }
    }

    /* 32 steps over 16 levels, so each level lasts two steps. */
    s_env_vol = (uint8_t)(s_env_dec ? 15u - (s_env_pos >> 1)
                                    : (s_env_pos >> 1));
    s_env_pos++;
    if (s_env_pos < 32u) return;

    s_env_pos = 0u;
    if (shape & 0x01u) {                           /* hold the last level     */
        s_env_done = 1u;
        s_env_vol = (uint8_t)(s_env_dec ? 0u : 15u);
    } else if (!(shape & 0x08u)) {                 /* no continue: to silence */
        s_env_done = 1u;
        s_env_vol = 0u;
    } else if (shape & 0x02u) {                    /* alternate the direction */
        s_env_dec ^= 1u;
    }
}

static void tick(void)
{
    uint8_t c;

    for (c = 0u; c < 3u; c++) {
        if (++s_tone_c[c] >= s_tone_p[c]) {
            s_tone_c[c] = 0u;
            s_tone_o[c] ^= 1u;
        }
    }

    if (++s_noise_c >= s_noise_p) {
        uint16_t fb;
        s_noise_c = 0u;
        fb = (uint16_t)(((s_lfsr ^ (s_lfsr >> 3)) & 1u) << 16);  /* x^17+x^14+1 */
        s_lfsr = (uint16_t)(((s_lfsr >> 1) | fb) & 0x1FFFFu);
        s_noise_o = (uint8_t)(s_lfsr & 1u);
    }

    if (++s_env_c >= s_env_p) {
        s_env_c = 0u;
        env_step();
    }
}

/* ---- mixer ------------------------------------------------------------- */

static uint32_t mix(void)
{
    uint32_t sum = 0u;
    uint8_t  c;

    for (c = 0u; c < 3u; c++) {
        uint8_t tone = (uint8_t)((s_reg[7] & (1u << c)) ? 1u : s_tone_o[c]);
        uint8_t noise = (uint8_t)((s_reg[7] & (8u << c)) ? 1u : s_noise_o);
        uint8_t v;
        if (!(tone & noise)) continue;
        v = s_reg[8u + c];
        sum += (v & 0x10u) ? s_env_vol : s_vol16[v & 0x0Fu];
    }
    return sum;
}

/* ---- ports ------------------------------------------------------------- */
/* MSX wires the two I/O ports like this (measured against the standard I/O
 * map and two independent emulators):
 *   port A (#A2 read with R14 selected) = the joystick socket pins:
 *      bits 0-5 up/down/left/right/TRGA/TRGB, active low, idle 0x3F;
 *      bit 6 = the 50/60 Hz keyboard switch (JIS side reads 1);
 *      bit 7 = cassette data in, idle high here.
 *   which socket answers is bit 6 of the last port B drive value (R15), and
 *   R7 bit 6 says whether the lines are inputs at all.
 *   port B carries no input pins on an MSX: it drives the socket select, the
 *   KANA lamp and the cassette output, so a read echoes the drive value.
 */
static uint8_t port_a_pins(void)
{
    uint8_t sel;

    /* AY-3-8910 R7 bit 6 is the Port A direction, active for OUTPUT: a set bit
     * drives the pins from the R14 latch (a read echoes it); a clear bit lets
     * Port A float, so the read returns the socket pins.  On MSX Port A is the
     * joystick, so the idle case (bit 6 = 0) is what games poll. */
    if (s_reg[7] & 0x40u) return s_pa_out;                 /* output: echo     */
    sel = (uint8_t)((s_pb_out & 0x40u) ? 1u : 0u);
    return (uint8_t)((kbd_joy_pins(sel) & 0x3Fu) | 0xC0u);
}

static uint8_t port_b_pins(void)
{
    return s_pb_out;
}

uint8_t psg_read(uint8_t port)
{
    if ((port & 0x03u) != 2u) return 0xFFu;   /* #A0/#A1/#A3 read as floating */

    switch (s_select) {
    case 14u: return port_a_pins();
    case 15u: return port_b_pins();
    default:  return s_reg[s_select];
    }
}

/* Introspection for the headless tracer: the joystick-relevant PSG state. */
uint8_t psg_debug_select(void) { return s_select; }
uint8_t psg_debug_r7(void)     { return s_reg[7]; }
uint8_t psg_debug_r14(void)    { return s_reg[14]; }
uint8_t psg_debug_r15(void)    { return s_reg[15]; }
uint8_t psg_debug_pb(void)     { return s_pb_out; }

void psg_write(uint8_t port, uint8_t value)
{
    switch (port & 0x03u) {
    case 0u:
        s_select = (uint8_t)(value & 0x0Fu);
        break;
    case 1u:
        reg_write(s_select, value);
        break;
    default:
        break;                                /* #A2/#A3 are not write ports  */
    }
}

/* ---- PCM --------------------------------------------------------------- */

void psg_init(void)
{
    psg_reset();
}

void psg_reset(void)
{
    uint8_t c;
    memset(s_reg, 0, sizeof(s_reg));
    s_select = 0u;
    s_pa_out = 0u;
    s_pb_out = 0u;
    for (c = 0u; c < 3u; c++) { s_tone_c[c] = 0u; s_tone_p[c] = 1u; s_tone_o[c] = 0u; }
    s_noise_c = 0u;  s_noise_p = 1u;  s_noise_o = 0u;
    s_lfsr = 0x0ACEu;
    s_env_c = 0u;    s_env_p = 1u;    s_env_pos = 0u;
    s_env_dec = 0u;  s_env_done = 0u; s_env_vol = 0u;
    s_frac = 0u;
}

/* Produce `frames` interleaved stereo frames from the chip state and move the
 * generators by exactly the ticks they consume.  The register file is read at
 * each sample, so a write taken during a frame shows up in the next block -
 * the 60 Hz block law of section 2.4.  Dropping samples is the caller's
 * problem (D5): it never reaches back into the chip.
 */
uint32_t psg_render(int16_t *out, uint32_t frames)
{
    uint32_t i;

    for (i = 0u; i < frames; i++) {
        uint32_t frac = s_frac + PSG_STEP_LO;
        uint32_t n = PSG_STEP_HI + (frac < PSG_STEP_LO ? 1u : 0u);
        s_frac = frac;
        while (n--) tick();
        {
            int16_t v = (int16_t)(mix() << 5);
            out[i * 2u] = v;
            out[i * 2u + 1u] = v;
        }
    }
    return frames;
}
