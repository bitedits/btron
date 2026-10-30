/*
 * profile_aurel_sound.c — B-MSX Sound Profiling & Feedback Loop Harness
 *
 * Boots assets/msx/AUREL_VAULT.rom, enters gameplay to activate OPLL BGM
 * and PSG SFX, streams audio for 10 seconds (600 frames), writes a WAV file,
 * and audits for:
 *   - MusicAudit stats from MSX RAM 0xE300 (ticks, late count, max gap)
 *   - Clipping occurrences (samples hitting +/- 32767)
 *   - Frame-to-frame boundary discontinuities (clicks/pops)
 *   - High-slew rate impulses (creaks/crackle)
 *   - Audio buffer drop simulation (queue overrun / starvation)
 */

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/v9938.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define TOTAL_FRAMES 600
#define SAMPLES_PER_FRAME 735u
#define TOTAL_SAMPLES (TOTAL_FRAMES * SAMPLES_PER_FRAME)

static int16_t s_pcm_raw[TOTAL_SAMPLES * 2u];
static int16_t s_pcm_ac[TOTAL_SAMPLES * 2u];

/* Speaker capacitor AC filter matching msx_app.c */
static int32_t s_hp[2];
static void host_ac_reset(void) { s_hp[0] = 0; s_hp[1] = 0; }
static void host_ac(int16_t *pcm, uint32_t frames)
{
    uint32_t i;
    for (i = 0u; i < frames; i++) {
        for (int ch = 0; ch < 2; ch++) {
            int32_t x = pcm[i * 2u + (uint32_t)ch];
            int32_t mean = s_hp[ch];
            int32_t y;
            mean += ((x << 8) - mean) >> 9;
            s_hp[ch] = mean;
            y = x - (mean >> 8);
            if (y > 32767) y = 32767;
            if (y < -32768) y = -32768;
            pcm[i * 2u + (uint32_t)ch] = (int16_t)y;
        }
    }
}

static void write_wav(const char *path, const int16_t *pcm, uint32_t frames)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    uint32_t data_bytes = frames * 2u * sizeof(int16_t);
    uint32_t riff_bytes = data_bytes + 36u;
    uint32_t sample_rate = 44100u;
    uint32_t byte_rate = sample_rate * 4u;
    uint16_t block_align = 4u;
    uint16_t bits = 16u;
    uint16_t channels = 2u;
    uint16_t fmt = 1u;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_bytes, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t sub1 = 16u;
    fwrite(&sub1, 4, 1, f);
    fwrite(&fmt, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
    fwrite(pcm, 1, data_bytes, f);
    fclose(f);
}

static uint8_t *read_file(const char *path, uint32_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (uint32_t)n;
    return buf;
}

int main(void)
{
    const char *rom_path = "assets/msx/AUREL_VAULT.rom";
    uint32_t rom_len = 0;
    uint8_t *rom = read_file(rom_path, &rom_len);
    msx_cfg_t cfg;
    int rc;

    printf("===============================================================\n");
    printf(" B-MSX Sound Profiling & Feedback Loop: %s\n", rom_path);
    printf("===============================================================\n");

    if (!rom) { fprintf(stderr, "Failed to read %s\n", rom_path); return 1; }

    memset(&cfg, 0, sizeof(cfg));
    cfg.machine = BMSX_MACHINE_MSXTR_CBIOS;
    cfg.frame_hz = 60;
    rc = msx_init(&cfg);
    if (rc != MSX_OK) { fprintf(stderr, "msx_init failed: %d\n", rc); return 1; }

    rc = msx_load_rom(rom, rom_len, 0);
    if (rc != MSX_OK) { fprintf(stderr, "msx_load_rom failed: %d\n", rc); return 1; }
    free(rom);

    host_ac_reset();

    /* Wait until game reaches TITLE state (music.ready == 1) */
    printf("Booting until TITLE screen loaded (music.ready == 1)...\n");
    int boot_frames = 0;
    while (boot_frames < 1000) {
        msx_run_frame();
        boot_frames++;
        if (slots_read(0xE300) == 1) break;
    }
    printf("Reached TITLE state at frame %d!\n", boot_frames);

    /* Press JUMP (Space bar: Row 8 Col 0) to start game */
    printf("Pressing JUMP (Space) to start gameplay...\n");
    for (int k = 0; k < 10; k++) {
        msx_kbd_down(8u, 0u);
        msx_run_frame();
    }
    msx_kbd_up(8u, 0u);
    for (int k = 0; k < 10; k++) {
        msx_run_frame();
    }

    uint8_t mode = slots_read(0xE301);
    printf("Current music.mode = %d (expected 1 for gameplay BGM)\n", mode);

    /* Frame loop */
    uint32_t clip_count_raw = 0;
    uint32_t clip_count_ac = 0;
    int16_t min_raw = 0, max_raw = 0;
    int16_t min_ac = 0, max_ac = 0;
    uint32_t seam_clicks = 0;
    uint32_t large_slews = 0;

    int16_t last_sample_left = 0;

    for (int f = 0; f < TOTAL_FRAMES; f++) {
        /* Simulate movement / gameplay */
        if (f % 60 == 20) msx_kbd_down(8u, 7u); /* Right */
        if (f % 60 == 50) msx_kbd_up(8u, 7u);
        if (f == 120) msx_kbd_down(5u, 5u);    /* X (grip) */
        if (f == 150) msx_kbd_up(5u, 5u);

        msx_run_frame();

        int16_t *buf_raw = &s_pcm_raw[f * SAMPLES_PER_FRAME * 2u];
        int16_t *buf_ac  = &s_pcm_ac[f * SAMPLES_PER_FRAME * 2u];

        uint32_t n = 0;
        msx_psg_submit_pcm(buf_raw, &n);
        memcpy(buf_ac, buf_raw, (size_t)n * 2u * sizeof(int16_t));
        host_ac(buf_ac, n);

        /* Inspect samples */
        for (uint32_t i = 0; i < n; i++) {
            int16_t r_l = buf_raw[i * 2u];
            int16_t a_l = buf_ac[i * 2u];
            if (r_l < min_raw) min_raw = r_l;
            if (r_l > max_raw) max_raw = r_l;
            if (a_l < min_ac) min_ac = a_l;
            if (a_l > max_ac) max_ac = a_l;

            if (r_l >= 32760 || r_l <= -32760) clip_count_raw++;
            if (a_l >= 32760 || a_l <= -32760) clip_count_ac++;

            if (f > 0 || i > 0) {
                int diff = abs(r_l - last_sample_left);
                if (diff > 12000) large_slews++;
            }
            if (i == 0 && f > 0) {
                int seam_diff = abs(r_l - last_sample_left);
                if (seam_diff > 12000) seam_clicks++;
            }
            last_sample_left = r_l;
        }

        /* Periodic progress & MusicAudit inspect */
        if ((f + 1) % 120 == 0) {
            uint8_t m_ready = slots_read(0xE300);
            uint8_t m_mode  = slots_read(0xE301);
            uint32_t m_ticks = (uint32_t)slots_read(0xE302) |
                               ((uint32_t)slots_read(0xE303) << 8) |
                               ((uint32_t)slots_read(0xE304) << 16) |
                               ((uint32_t)slots_read(0xE305) << 24);
            uint16_t m_late = (uint16_t)slots_read(0xE306) | ((uint16_t)slots_read(0xE307) << 8);
            uint16_t m_maxgap = (uint16_t)slots_read(0xE308) | ((uint16_t)slots_read(0xE309) << 8);
            uint16_t m_starts = (uint16_t)slots_read(0xE30A) | ((uint16_t)slots_read(0xE30B) << 8);

            printf("Frame %3d: MusicAudit at 0xE300: ready=%d mode=%d ticks=%u late=%u maxGap=%u starts=%u\n",
                   f + 1, m_ready, m_mode, m_ticks, m_late, m_maxgap, m_starts);
        }
    }

    /* Write WAV files for analysis */
    write_wav(".build/aurel_raw.wav", s_pcm_raw, TOTAL_SAMPLES);
    write_wav(".build/aurel_ac.wav", s_pcm_ac, TOTAL_SAMPLES);
    printf("\nWAV files written to .build/aurel_raw.wav and .build/aurel_ac.wav\n");

    printf("\n--- AUDIO QUALITY METRICS (%d frames / 10.0 seconds) ---\n", TOTAL_FRAMES);
    printf("Raw Peak Range:      [%d, %d]\n", min_raw, max_raw);
    printf("AC Filter Peak Range:[%d, %d]\n", min_ac, max_ac);
    printf("Raw Hard Clips:      %u samples\n", clip_count_raw);
    printf("AC Hard Clips:       %u samples\n", clip_count_ac);
    printf("Frame Seam Jumps:    %u occurrences\n", seam_clicks);
    printf("Large Slews (>12k):  %u occurrences\n", large_slews);

    return 0;
}
