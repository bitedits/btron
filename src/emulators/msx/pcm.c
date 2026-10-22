/*
 * tronMSX — MSX turbo R PCM (T9769 #A4/#A5). Integer only (D4), no heap.
 *
 * The sample clock is 15700 Hz: 228 Z80 T-states, which is 456 clocks of the
 * 7.15909 MHz clock. 3579545/228 = 15700. The published 15750 Hz figure
 * (227 T-states) is the wrong one. #A4 read is the low two bits of that
 * counter, the handshake a playback loop polls. #A4 write is an unsigned
 * 8-bit sample, 0x80 at silence.
 *
 * #A5 = 03H (bit 0 set, bit 1 set) latches the sample onto the next tick of
 * that clock. That is the normal mode. #A5 = 02H (bit 0 clear) takes the
 * sample immediately, and the DAC slews at most PCM_SLEW steps per tick, so
 * a high rate cannot reach full swing. Bit 1 clear forces this DAC silent.
 * The engine bit also mutes the analog mixer on hardware; this model gates
 * the PCM DAC only.
 *
 * t9769c_advance feeds T-states in during the frame. pcm_mix expands whatever
 * the frame captured across the 735-sample B-Audio block, then holds the last
 * sample. Machines that never write #A4 contribute zeros.
 */

#include "msx_int.h"

#include <string.h>

#define PCM_DIV   228u
#define PCM_SLEW  48          /* 02H: largest step per 15700 Hz tick */
#define PCM_RING  1024u

static uint32_t s_acc;
static uint32_t s_clock;
static uint8_t  s_dac;       /* last sample written to #A4            */
static uint8_t  s_hold;      /* sample the 03H latch is playing       */
static uint8_t  s_pend;      /* 03H sample waiting for the next tick  */
static uint8_t  s_pend_on;
static int      s_slew;      /* 02H DAC level, 0..255                 */
static uint8_t  s_ctrl;
static uint8_t  s_ring[PCM_RING];
static uint16_t s_wr;
static uint16_t s_rd;
static uint16_t s_count;

static void push(uint8_t sample)
{
    if (s_count >= PCM_RING) return;
    s_ring[s_wr] = sample;
    s_wr = (uint16_t)((s_wr + 1u) & (PCM_RING - 1u));
    s_count++;
}

static int32_t level(uint8_t sample)
{
    if ((s_ctrl & 0x02u) == 0u) return 0;
    return ((int32_t)sample - 128) << 6;
}

void pcm_init(void)  { pcm_reset(); }

void pcm_reset(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    s_acc = 0u;
    s_clock = 0u;
    s_dac = 0x80u;
    s_hold = 0x80u;
    s_pend = 0x80u;
    s_pend_on = 0u;
    s_slew = 128;
    s_ctrl = 0x03u;
    s_wr = 0u;
    s_rd = 0u;
    s_count = 0u;
}

void pcm_write_data(uint8_t value)
{
    s_dac = value;
    if (s_ctrl & 0x01u) {
        s_pend = value;
        s_pend_on = 1u;
    } else {
        s_pend_on = 0u;
    }
}

void pcm_write_ctrl(uint8_t value)
{
    uint8_t prev = s_ctrl;
    s_ctrl = value;
    if ((prev & 0x01u) != 0u && (value & 0x01u) == 0u) s_pend_on = 0u;
}

uint8_t pcm_read_status(void)
{
    return (uint8_t)(s_clock & 3u);
}

uint8_t pcm_read_ctrl(void)
{
    return (uint8_t)(s_ctrl & 0x1Fu);
}

void pcm_advance(uint32_t t_states)
{
    s_acc += t_states;
    while (s_acc >= PCM_DIV) {
        uint8_t sample;
        s_acc -= PCM_DIV;
        s_clock++;
        if (s_ctrl & 0x01u) {
            if (s_pend_on) {
                s_hold = s_pend;
                s_pend_on = 0u;
                s_slew = (int)s_hold;
            }
            sample = s_hold;
        } else {
            int d = (int)s_dac - s_slew;
            if (d > PCM_SLEW) d = PCM_SLEW;
            if (d < -PCM_SLEW) d = -PCM_SLEW;
            s_slew += d;
            s_hold = (uint8_t)s_slew;
            sample = s_hold;
        }
        push(sample);
    }
}

void pcm_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;

    if (!acc || !frames) return;
    if (s_count == 0u) {
        int32_t v = level(s_hold);
        for (i = 0u; i < frames; i++) {
            acc[i * 2u]     += v;
            acc[i * 2u + 1u] += v;
        }
        return;
    }

    {
        uint32_t n = s_count;
        uint64_t step = ((uint64_t)n << 32) / (uint64_t)frames;
        uint32_t pos = 0u;
        uint32_t frac = 0u;

        for (i = 0u; i < frames; i++) {
            uint32_t idx = pos;
            int32_t v;
            uint32_t nf;
            if (idx >= n) idx = n - 1u;
            v = level(s_ring[(s_rd + idx) & (PCM_RING - 1u)]);
            acc[i * 2u]     += v;
            acc[i * 2u + 1u] += v;
            nf = frac + (uint32_t)step;
            pos += (uint32_t)(step >> 32) + (nf < frac ? 1u : 0u);
            frac = nf;
        }
        s_rd = (uint16_t)((s_rd + n) & (PCM_RING - 1u));
        s_count = 0u;
    }
}
