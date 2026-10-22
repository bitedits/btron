/*
 * tronMSX — MSX turbo R PCM (T9769 #A4/#A5). Integer only (D4), no heap.
 *
 * #A4 write is an unsigned 8-bit sample, 0x80 at silence. #A4 read is the low
 * two bits of a free-running counter that ticks every 227 Z80 T-states
 * (3579545/227 ≈ 15.77 kHz), which is the handshake BIOS playback loops poll.
 * #A5 bit 0 selects the 4-deep latch; bit 1 set is sound enabled (the reset
 * value 0x03 is latched and unmuted). Bit 1 clear forces silence.
 *
 * t9769c_advance feeds T-states in during the frame. pcm_mix expands whatever
 * the frame captured across the 735-sample B-Audio block, then holds the last
 * sample. Machines that never write #A4 contribute zeros.
 */

#include "msx_int.h"

#include <string.h>

#define PCM_DIV   227u
#define PCM_RING  1024u

static uint32_t s_acc;
static uint32_t s_clock;
static uint8_t  s_dac;
static uint8_t  s_ctrl;
static uint8_t  s_fifo[4];
static uint8_t  s_fifo_n;
static uint8_t  s_fifo_r;
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
    memset(s_fifo, 0, sizeof(s_fifo));
    memset(s_ring, 0, sizeof(s_ring));
    s_acc = 0u;
    s_clock = 0u;
    s_dac = 0x80u;
    s_ctrl = 0x03u;
    s_fifo_n = 0u;
    s_fifo_r = 0u;
    s_wr = 0u;
    s_rd = 0u;
    s_count = 0u;
}

void pcm_write_data(uint8_t value)
{
    s_dac = value;
    if ((s_ctrl & 0x01u) && s_fifo_n < 4u) {
        s_fifo[(s_fifo_r + s_fifo_n) & 3u] = value;
        s_fifo_n++;
    }
}

void pcm_write_ctrl(uint8_t value)
{
    s_ctrl = value;
    if ((s_ctrl & 0x01u) == 0u) {
        s_fifo_n = 0u;
        s_fifo_r = 0u;
    }
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
        uint8_t sample = s_dac;
        s_acc -= PCM_DIV;
        s_clock++;
        if ((s_ctrl & 0x01u) && s_fifo_n) {
            sample = s_fifo[s_fifo_r];
            s_fifo_r = (uint8_t)((s_fifo_r + 1u) & 3u);
            s_fifo_n--;
            s_dac = sample;
        }
        push(sample);
    }
}

void pcm_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;

    if (!acc || !frames) return;
    if (s_count == 0u) {
        int32_t v = level(s_dac);
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
