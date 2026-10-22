/*
 * Headless checks for the tronMSX sound-chip mixer.
 * No SDL: render into a buffer and measure it.
 *
 *   make msx-audio
 */

#include "../../src/emulators/msx/msx_int.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define FRAMES 735u

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        printf("FAIL %s\n", msg);
        g_fail++;
    } else {
        printf("ok   %s\n", msg);
    }
}

static void quiet(void)
{
    psg_reset();
    scc_reset();
    opll_reset();
    y8950_reset();
    pcm_reset();
}

static void psg_reg(uint8_t r, uint8_t v)
{
    psg_write(0xA0, r);
    psg_write(0xA1, v);
}

static void opll_reg(uint8_t r, uint8_t v)
{
    opll_address(r);
    opll_data(v);
}

static void y8950_reg(uint8_t r, uint8_t v)
{
    y8950_write(0xC0, r);
    y8950_write(0xC1, v);
}

static uint32_t edges(const int16_t *b, uint32_t frames)
{
    uint32_t i, n = 0;
    for (i = 1u; i < frames; i++) {
        int a = b[(i - 1u) * 2u] > 0;
        int c = b[i * 2u] > 0;
        if (a != c) n++;
    }
    return n;
}

static uint32_t crossings(const int16_t *b, uint32_t frames)
{
    uint32_t i, n = 0;
    for (i = 1u; i < frames; i++) {
        int16_t a = b[(i - 1u) * 2u];
        int16_t c = b[i * 2u];
        if ((a < 0 && c >= 0) || (a >= 0 && c < 0)) n++;
    }
    return n;
}

static void peak(const int16_t *b, uint32_t frames, int16_t *lo, int16_t *hi)
{
    uint32_t i;
    *lo = 32767;
    *hi = -32768;
    for (i = 0u; i < frames; i++) {
        int16_t s = b[i * 2u];
        if (s < *lo) *lo = s;
        if (s > *hi) *hi = s;
    }
}

static int all_zero(const int16_t *b, uint32_t frames)
{
    uint32_t i;
    for (i = 0u; i < frames * 2u; i++) if (b[i] != 0) return 0;
    return 1;
}

static void scc_square(uint16_t period)
{
    uint8_t i;
    scc_write(0x9000, 0x3F);
    for (i = 0u; i < 32u; i++)
        scc_write((uint16_t)(0x9800u + i), (i < 16u) ? 0x7Fu : 0x80u);
    scc_write(0x9880, (uint8_t)(period & 0xFFu));
    scc_write(0x9881, (uint8_t)((period >> 8) & 0x0Fu));
    scc_write(0x988A, 15);
    scc_write(0x988F, 0x01);
}

static void opll_sine(void)
{
    /* Custom instrument, silent modulator, sustained carrier, A-ish 437 Hz.
     * fnum 288, block 4: reg #20 = key | (block << 1) | fnum bit 8. */
    opll_reg(0x00, 0x21);
    opll_reg(0x01, 0x21);
    opll_reg(0x02, 0x3F);
    opll_reg(0x03, 0x00);
    opll_reg(0x04, 0xF0);
    opll_reg(0x05, 0xF0);
    opll_reg(0x06, 0x00);
    opll_reg(0x07, 0x00);
    opll_reg(0x10, 0x20);
    opll_reg(0x30, 0x00);
    opll_reg(0x20, 0x19);
}

static void y8950_sine(void)
{
    y8950_reg(0x20, 0x21);
    y8950_reg(0x23, 0x21);
    y8950_reg(0x40, 0x3F);
    y8950_reg(0x43, 0x00);
    y8950_reg(0x60, 0xF0);
    y8950_reg(0x63, 0xF0);
    y8950_reg(0x80, 0x00);
    y8950_reg(0x83, 0x00);
    y8950_reg(0xA0, 0x20);
    y8950_reg(0xC0, 0x00);
    y8950_reg(0xB0, 0x31);
}

int main(void)
{
    static int16_t buf[FRAMES * 2u];
    int16_t lo, hi;
    uint32_t n;
    uint8_t i;

    quiet();
    memset(buf, 0x5A, sizeof(buf));
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "reset mix is silent");

    quiet();
    /* Both generators disabled is the AY constant level, not silence.
     * Volume 0 is what mutes the channel. */
    psg_reg(7, 0x3F);
    psg_reg(8, 15);
    msx_audio_render(buf, FRAMES);
    n = edges(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    expect(n == 0u && hi == 8160 && lo == 8160, "PSG both-off is the flat AY level");
    psg_reg(8, 0);
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "PSG volume 0 is silent");

    quiet();
    psg_reg(7, 0x38);
    psg_reg(8, 15);
    psg_reg(0, 0x00);
    psg_reg(1, 0x01);                 /* period 256: 437 Hz, ~15 edges / frame */
    msx_audio_render(buf, FRAMES);
    n = edges(buf, FRAMES);
    printf("     PSG edges %u\n", n);
    expect(n >= 12u && n <= 18u, "PSG period 256 lands in band");

    /* Envelope mode is the same 16-level table as a fixed volume. */
    quiet();
    psg_reg(7, 0x3F);
    psg_reg(8, 0x10);
    psg_reg(11, 1);
    psg_reg(12, 0);
    psg_reg(13, 0x08);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    printf("     PSG envelope peak %d..%d\n", (int)lo, (int)hi);
    expect(hi == 8160, "PSG envelope level 15 matches fixed volume 15");

    quiet();
    psg_reg(7, 0x3F);
    psg_reg(8, 0x10);
    psg_reg(11, 1);
    psg_reg(13, 0x00);
    msx_audio_render(buf, FRAMES);
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "PSG envelope shape 0 finishes at silence");

    quiet();
    psg_reg(7, 0x3F);
    psg_reg(8, 0x10);
    psg_reg(11, 1);
    psg_reg(13, 0x0B);
    msx_audio_render(buf, FRAMES);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    expect(lo == 8160 && hi == 8160, "PSG envelope shape 0x0B holds full level");

    quiet();
    psg_reg(7, 0x3F);
    psg_reg(8, 0x10);
    psg_reg(11, 1);
    psg_reg(13, 0x0F);
    msx_audio_render(buf, FRAMES);
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "PSG envelope shape 0x0F holds silence");

    quiet();
    scc_square(0x100);                /* 3579545/(32*256) ≈ 437 Hz, ~15 crossings */
    msx_audio_render(buf, FRAMES);
    n = crossings(buf, FRAMES);
    printf("     SCC crossings %u\n", n);
    expect(n >= 12u && n <= 18u, "SCC period 0x100 lands in band");
    expect(buf[0] != 0 || buf[2] != 0, "SCC square is audible");

    /* Memory hook: a Konami-SCC-shaped image, page 2 mapped to the cart. */
    {
        static uint8_t rom[40000];
        uint8_t got = 0;
        memset(rom, 0xFF, sizeof(rom));
        rom[0] = 0x32; rom[1] = 0x00; rom[2] = 0x90;
        rom[3] = 0x32; rom[4] = 0x00; rom[5] = 0x50;
        rom[6] = 0x32; rom[7] = 0x00; rom[8] = 0xB0;
        slots_init();
        slots_set_machine(BMSX_MACHINE_MSX);
        slots_set_rom(rom, (uint32_t)sizeof(rom));
        slots_primary_select(0x10);
        slots_write(0x9000, 0x3F);
        slots_write(0x9800, 0x7F);
        got = slots_read(0x9800);
        expect(got == 0x7F, "SCC waveform write is visible through the cart window");
    }
    /* 48 KiB plain carts keep "AB" in the second 16 KiB and occupy CPU
     * pages 0..2. A 32 KiB cart still starts at page 1. */
    {
        static uint8_t rom[65536];
        slots_init();
        slots_set_machine(BMSX_MACHINE_MSX);

        memset(rom, 0x11, 49152u);
        rom[0] = 0xD1;
        rom[0x4000] = (uint8_t)'A';
        rom[0x4001] = (uint8_t)'B';
        rom[0x4002] = 0x10;
        rom[0x4003] = 0x40;
        rom[0x8000] = 0xD2;
        slots_set_rom(rom, 49152u);
        slots_primary_select(0x55);
        expect(slots_read(0x0000) == 0xD1, "48K plain page 0 is the first 16K");
        expect(slots_read(0x4000) == (uint8_t)'A' && slots_read(0x4001) == (uint8_t)'B',
               "48K plain page 1 holds the AB header");
        expect(slots_read(0x8000) == 0xD2, "48K plain page 2 is the third 16K");

        memset(rom, 0x22, 32768u);
        rom[0] = (uint8_t)'A';
        rom[1] = (uint8_t)'B';
        rom[2] = 0x10;
        rom[3] = 0x40;
        rom[0x4000] = 0xD3;
        slots_set_rom(rom, 32768u);
        slots_primary_select(0x55);
        expect(slots_read(0x0000) == 0xFF, "32K plain page 0 stays empty");
        expect(slots_read(0x4000) == (uint8_t)'A', "32K plain page 1 is the file start");
        expect(slots_read(0x8000) == 0xD3, "32K plain page 2 is the second 16K");

        memset(rom, 0x33, 49152u);
        rom[0] = 0xA1;
        rom[0x4000] = 0xA2;
        rom[0x8000] = 0xA3;
        slots_set_rom(rom, 49152u);
        slots_primary_select(0x55);
        expect(slots_read(0x4000) == 0xA1 && slots_read(0x8000) == 0xA1,
               "48K image without an AB header is not mapped from 0000");

        memset(rom, 0x55, 49152u);
        rom[0] = 0xC0;
        rom[0x4000] = (uint8_t)'A';
        rom[0x4001] = (uint8_t)'B';
        rom[0x4002] = 0x00;
        rom[0x4003] = 0x00;
        slots_set_rom(rom, 49152u);
        slots_primary_select(0x55);
        expect(slots_read(0x4000) == 0xC0, "48K AB with INIT outside page 1 stays banked");

        memset(rom, 0x44, sizeof(rom));
        rom[0] = 0xB0;
        rom[0x4000] = (uint8_t)'A';
        rom[0x4001] = (uint8_t)'B';
        rom[0x4002] = 0x10;
        rom[0x4003] = 0x40;
        slots_set_rom(rom, 65536u);
        slots_primary_select(0x55);
        expect(slots_read(0x4000) == 0xB0 && slots_read(0x8000) == 0xB0,
               "64K image with AB at file+0x4000 stays banked");
    }
    {
        uint8_t v = 0u;
        quiet();
        scc_write(0xBFFE, 0x20);
        scc_write(0xB000, 0x80);
        scc_write(0xB800, 0x5A);
        expect(scc_read(0xB800, &v) == 1 && v == 0x5A, "SCC+ waveform is at #B800");
        scc_reset();
        scc_write(0x9000, 0x3F);
        scc_write(0x98E0, 0xA5);
        v = 0u;
        expect(scc_read(0x98E0, &v) == 1 && v == 0xA5, "SCC deformation register latches");
    }

    quiet();
    opll_sine();
    msx_audio_render(buf, FRAMES);
    n = crossings(buf, FRAMES);
    printf("     OPLL crossings %u\n", n);
    expect(n >= 12u && n <= 18u, "OPLL fnum 288 block 4 lands in band");

    quiet();
    opll_reg(0x16, 0x20);
    opll_reg(0x26, 0x05);
    opll_reg(0x36, 0x00);
    opll_reg(0x0E, 0x20);             /* rhythm on, bass drum key up */
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "OPLL rhythm with keys up is silent");
    opll_reg(0x0E, 0x30);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    printf("     OPLL bass drum peak %d\n", (int)hi);
    expect(hi > 1000, "OPLL bass drum is audible");
    {
        int k;
        for (k = 0; k < 24; k++) msx_audio_render(buf, FRAMES);
    }
    peak(buf, FRAMES, &lo, &hi);
    printf("     OPLL bass drum after 24 frames %d\n", (int)hi);
    expect(hi == 0, "OPLL bass drum decays to silence within 24 frames");
    opll_reg(0x0E, 0x20);
    opll_reg(0x0E, 0x30);             /* rise before the next mix */
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    expect(hi > 1000, "OPLL bass drum retriggers when the key bit rises");

    /* Volume nibble is 3 dB per step. Eight steps is 24 dB, about ×16. */
    quiet();
    opll_sine();
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    {
        int peak0 = (int)hi;
        int peak8;
        int ratio10;
        opll_reg(0x30, 0x08);
        msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        peak8 = (int)hi;
        ratio10 = (peak8 > 0) ? (peak0 * 10) / peak8 : 0;
        printf("     OPLL vol0 %d vol8 %d ratio_x10 %d\n", peak0, peak8, ratio10);
        expect(ratio10 >= 120 && ratio10 <= 220,
               "OPLL volume 8 is 24 dB under volume 0");
        opll_reg(0x30, 0x0F);
        msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        printf("     OPLL vol15 peak %d\n", (int)hi);
        expect(hi > 0 && (int)hi * 80 < peak0, "OPLL volume 15 is far below volume 0");
    }

    /* Hi-hat is the measured phase mix, not a flat level and not DC. */
    quiet();
    opll_reg(0x17, 0x50);
    opll_reg(0x27, 0x05);
    opll_reg(0x18, 0xC0);
    opll_reg(0x28, 0x01);
    opll_reg(0x37, 0x00);
    opll_reg(0x0E, 0x21);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    n = crossings(buf, FRAMES);
    printf("     OPLL hi-hat %d..%d crossings %u\n", (int)lo, (int)hi, n);
    expect(hi > 400 && (int)hi - (int)lo > 400, "OPLL hi-hat is a moving waveform");

    /* Same decay rate: KSR makes block 7 fall faster than block 0. */
    {
        int peak_lo, peak_hi;
        int k;
        quiet();
        opll_reg(0x00, 0x01);
        opll_reg(0x01, 0x31);
        opll_reg(0x02, 0x3F);
        opll_reg(0x05, 0xF6);
        opll_reg(0x07, 0xF0);
        opll_reg(0x10, 0x80);
        opll_reg(0x30, 0x00);
        opll_reg(0x20, 0x10);
        for (k = 0; k < 20; k++) msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        peak_lo = (int)hi;
        quiet();
        opll_reg(0x00, 0x01);
        opll_reg(0x01, 0x31);
        opll_reg(0x02, 0x3F);
        opll_reg(0x05, 0xF6);
        opll_reg(0x07, 0xF0);
        opll_reg(0x10, 0x80);
        opll_reg(0x30, 0x00);
        opll_reg(0x20, 0x1F);
        for (k = 0; k < 20; k++) msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        peak_hi = (int)hi;
        printf("     OPLL ksr block0 %d block7 %d\n", peak_lo, peak_hi);
        expect(peak_lo > 100 && peak_hi == 0,
               "OPLL key scale decays a high block faster");
    }

    /* Tremolo pulls a sustained carrier down inside one AM cycle. */
    {
        int p0, p1;
        int k;
        quiet();
        opll_reg(0x00, 0x21);
        opll_reg(0x01, 0xA1);
        opll_reg(0x02, 0x3F);
        opll_reg(0x04, 0xF0);
        opll_reg(0x05, 0xF0);
        opll_reg(0x10, 0x20);
        opll_reg(0x30, 0x00);
        opll_reg(0x20, 0x19);
        msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        p0 = (int)hi;
        for (k = 0; k < 20; k++) msx_audio_render(buf, FRAMES);
        peak(buf, FRAMES, &lo, &hi);
        p1 = (int)hi;
        printf("     OPLL am %d -> %d\n", p0, p1);
        expect(p0 > 1000 && p1 > 0 && p0 > p1 + p1 / 8,
               "OPLL amplitude modulation deepens over a cycle");
    }

    quiet();
    y8950_sine();
    msx_audio_render(buf, FRAMES);
    n = crossings(buf, FRAMES);
    printf("     Y8950 crossings %u\n", n);
    expect(n >= 12u && n <= 18u, "Y8950 fnum 288 block 4 lands in band");

    quiet();
    pcm_write_ctrl(0x03);
    pcm_write_data(0xFF);
    pcm_advance(228u * 80u);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    printf("     PCM peak %d..%d\n", (int)lo, (int)hi);
    expect(hi > 1000 && lo >= 0, "turboR PCM 0xFF is a positive level");
    expect(pcm_read_status() == (uint8_t)(80u & 3u), "PCM status is the 15700 Hz clock");

    quiet();
    pcm_write_ctrl(0x02);             /* immediate: one tick slews, not a step */
    pcm_write_data(0xFF);
    pcm_advance(228u);
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    printf("     PCM 02H peak %d..%d\n", (int)lo, (int)hi);
    expect(hi > 2800 && hi < 3400, "PCM 02H one tick does not reach full scale");

    quiet();
    pcm_write_ctrl(0x01);             /* bit 1 clear: muted */
    pcm_write_data(0xFF);
    pcm_advance(228u * 40u);
    msx_audio_render(buf, FRAMES);
    expect(all_zero(buf, FRAMES), "turboR PCM mute is silent");

    quiet();
    psg_reg(7, 0x38);
    psg_reg(8, 15);
    psg_reg(9, 15);
    psg_reg(10, 15);
    psg_reg(0, 0xFF);
    psg_reg(1, 0x0F);                 /* long high stretch so the sum can rail */
    for (i = 0u; i < 32u; i++) scc_write((uint16_t)(0x9800u + i), 0x7F);
    scc_write(0x9000, 0x3F);
    for (i = 0u; i < 5u; i++) {
        scc_write((uint16_t)(0x9800u + i * 32u), 0x7F);
    }
    for (i = 0u; i < 32u; i++) {
        scc_write((uint16_t)(0x9800u + i), 0x7F);
        scc_write((uint16_t)(0x9820u + i), 0x7F);
        scc_write((uint16_t)(0x9840u + i), 0x7F);
        scc_write((uint16_t)(0x9860u + i), 0x7F);
    }
    scc_write(0x9880, 0x00);
    scc_write(0x9881, 0x01);
    scc_write(0x9882, 0x00);
    scc_write(0x9883, 0x01);
    scc_write(0x9884, 0x00);
    scc_write(0x9885, 0x01);
    scc_write(0x9886, 0x00);
    scc_write(0x9887, 0x01);
    scc_write(0x9888, 0x00);
    scc_write(0x9889, 0x01);
    scc_write(0x988A, 15);
    scc_write(0x988B, 15);
    scc_write(0x988C, 15);
    scc_write(0x988D, 15);
    scc_write(0x988E, 15);
    scc_write(0x988F, 0x1F);
    opll_sine();
    msx_audio_render(buf, FRAMES);
    peak(buf, FRAMES, &lo, &hi);
    printf("     mix peak %d..%d\n", (int)lo, (int)hi);
    expect(hi == 32767, "full mix saturates at 32767");
    expect(lo > -20000, "saturation does not wrap a positive sum");

    if (g_fail) {
        printf("%d failed\n", g_fail);
        return 1;
    }
    printf("msx-audio: all checks passed\n");
    return 0;
}
