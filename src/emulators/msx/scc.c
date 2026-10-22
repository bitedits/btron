/*
 * tronMSX — Konami SCC and SCC-I. Clean-room C99, integer only (D4).
 *
 * Plain SCC (Sound Creative Chip), enabled when a Konami SCC cart writes
 * xx111111b to the bank register at #9000–#97FF and the mode register is not
 * in SCC+ mode. The register window is #9800–#9FFF:
 *   #9800–#987F  waveforms 1–4; a write to channel 4 also fills channel 5
 *   #9880–#9889  12-bit frequencies, little-endian (mirrored at #9890)
 *   #988A–#988E  4-bit volumes
 *   #988F        channel enables, bit 0 = channel 1
 *   #98E0–#98FF  deformation / test register, latched
 * A period below 9 produces no sound, matching the chip.
 *
 * SCC-I: mode register #BFFE/#BFFF bit 5 selects SCC+ mode. With that bit set
 * and bit 7 of the #B000 bank set, the five independent waveforms live at
 * #B800 (layout of the Konami Sound Cartridge). The deformation register is
 * stored. Its rotate / frequency-shift bits are not applied.
 *
 * Tone law: f = MSX_Z80_HZ / (32 * period). The resampler is 32.32 fixed
 * point, the same shape as the PSG. Output is mono, added to L and R.
 * Five full-scale channels (127 * 15) stay under the int16 rail on their own.
 */

#include "msx_int.h"

#include <string.h>

#define SCC_STEP      (((uint64_t)MSX_Z80_HZ << 32) / (uint64_t)MSX_PCM_HZ)
#define SCC_STEP_HI   ((uint32_t)(SCC_STEP >> 32))
#define SCC_STEP_LO   ((uint32_t)(SCC_STEP & 0xFFFFFFFFu))

static uint8_t  s_wave[5][32];
static uint16_t s_freq[5];
static uint8_t  s_vol[5];
static uint8_t  s_en;
static uint8_t  s_test;
static uint8_t  s_bank2;
static uint8_t  s_bank3;
static uint8_t  s_mode;
static uint8_t  s_active;          /* 0 off, 1 plain SCC, 2 SCC+ */
static uint32_t s_clock[5];
static uint8_t  s_pos[5];
static uint32_t s_frac;

static void refresh(void)
{
    if ((s_mode & 0x20u) && (s_bank3 & 0x80u)) s_active = 2u;
    else if (!(s_mode & 0x20u) && (s_bank2 & 0x3Fu) == 0x3Fu) s_active = 1u;
    else s_active = 0u;
}

static void freq_write(uint8_t ch, uint8_t off, uint8_t value)
{
    if ((off & 1u) == 0u)
        s_freq[ch] = (uint16_t)((s_freq[ch] & 0x0F00u) | value);
    else
        s_freq[ch] = (uint16_t)((s_freq[ch] & 0x00FFu) |
                                ((uint16_t)(value & 0x0Fu) << 8));
}

static void reg_plain(uint8_t off, uint8_t value)
{
    if (off < 0x80u) {
        uint8_t ch = (uint8_t)(off >> 5);
        uint8_t i = (uint8_t)(off & 31u);
        s_wave[ch][i] = value;
        if (ch == 3u) s_wave[4][i] = value;
        return;
    }
    if (off >= 0xE0u) { s_test = value; return; }
    if (off >= 0xA0u) return;
    off = (uint8_t)(0x80u + (off & 0x0Fu));
    if (off < 0x8Au) {
        freq_write((uint8_t)((off - 0x80u) >> 1), off, value);
        return;
    }
    if (off < 0x8Fu) {
        s_vol[off - 0x8Au] = (uint8_t)(value & 0x0Fu);
        return;
    }
    s_en = value;
}

static void reg_plus(uint8_t off, uint8_t value)
{
    if (off < 0xA0u) {
        uint8_t ch = (uint8_t)(off >> 5);
        if (ch < 5u) s_wave[ch][off & 31u] = value;
        return;
    }
    if (off >= 0xC0u) { s_test = value; return; }
    if (off >= 0xB0u) return;
    if (off < 0xAAu) {
        freq_write((uint8_t)((off - 0xA0u) >> 1), off, value);
        return;
    }
    if (off < 0xAFu) {
        s_vol[off - 0xAAu] = (uint8_t)(value & 0x0Fu);
        return;
    }
    if (off == 0xAFu) s_en = value;
}

static uint8_t read_plain(uint8_t off)
{
    if (off < 0x80u) return s_wave[off >> 5][off & 31u];
    if (off >= 0xE0u) return s_test;
    return 0xFFu;
}

static uint8_t read_plus(uint8_t off)
{
    if (off < 0xA0u) {
        uint8_t ch = (uint8_t)(off >> 5);
        if (ch < 5u) return s_wave[ch][off & 31u];
    }
    if (off >= 0xC0u && off < 0xE0u) return s_test;
    return 0xFFu;
}

void scc_init(void)  { scc_reset(); }

void scc_reset(void)
{
    memset(s_wave, 0, sizeof(s_wave));
    memset(s_freq, 0, sizeof(s_freq));
    memset(s_vol, 0, sizeof(s_vol));
    memset(s_clock, 0, sizeof(s_clock));
    memset(s_pos, 0, sizeof(s_pos));
    s_en = 0u;
    s_test = 0u;
    s_bank2 = 0u;
    s_bank3 = 0u;
    s_mode = 0u;
    s_active = 0u;
    s_frac = 0u;
}

int scc_write(uint16_t addr, uint8_t value)
{
    if ((addr & 0xFFFEu) == 0xBFFEu) {
        s_mode = value;
        refresh();
        return 1;
    }
    if (s_active == 1u && addr >= 0x9800u && addr <= 0x9FFFu) {
        reg_plain((uint8_t)(addr & 0xFFu), value);
        return 1;
    }
    if (s_active == 2u && addr >= 0xB800u && addr <= 0xBFFDu) {
        reg_plus((uint8_t)(addr & 0xFFu), value);
        return 1;
    }
    if (addr >= 0x9000u && addr < 0x9800u) {
        s_bank2 = value;
        refresh();
        return 0;
    }
    if (addr >= 0xB000u && addr < 0xB800u) {
        s_bank3 = value;
        refresh();
        return 0;
    }
    return 0;
}

int scc_read(uint16_t addr, uint8_t *out)
{
    if (!out) return 0;
    if (s_active == 1u && addr >= 0x9800u && addr <= 0x9FFFu) {
        *out = read_plain((uint8_t)(addr & 0xFFu));
        return 1;
    }
    if (s_active == 2u && addr >= 0xB800u && addr < 0xBFFEu) {
        *out = read_plus((uint8_t)(addr & 0xFFu));
        return 1;
    }
    return 0;
}

void scc_mix(int32_t *acc, uint32_t frames)
{
    uint32_t i;

    if (!acc || !frames || !s_active) return;
    for (i = 0u; i < frames; i++) {
        uint32_t frac = s_frac + SCC_STEP_LO;
        uint32_t n = SCC_STEP_HI + (frac < SCC_STEP_LO ? 1u : 0u);
        int32_t sum = 0;
        uint8_t ch;

        s_frac = frac;
        for (ch = 0u; ch < 5u; ch++) {
            uint16_t per = (uint16_t)(s_freq[ch] & 0x0FFFu);
            uint32_t left = n;
            int32_t w;

            if (!(s_en & (1u << ch)) || per < 9u) continue;
            s_clock[ch] += left;
            while (s_clock[ch] >= per) {
                s_clock[ch] -= per;
                s_pos[ch] = (uint8_t)((s_pos[ch] + 1u) & 31u);
            }
            w = (int32_t)(int8_t)s_wave[ch][s_pos[ch]];
            sum += w * (int32_t)s_vol[ch];
        }
        acc[i * 2u]     += sum;
        acc[i * 2u + 1u] += sum;
    }
}
