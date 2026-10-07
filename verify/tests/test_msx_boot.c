/*
 * Headless boot harness for the tronMSX core: boots C-BIOS MSX1 with a
 * cartridge, runs a number of frames, and dumps the framebuffer to PPM so the
 * result can be turned into a PNG and actually looked at (sips).  It also
 * prints the cycle count and a coarse "is anything on screen" ink measure so a
 * black screen is distinguishable from a live one without opening an image.
 *
 * Not part of the shipped core; a developer tool.  Build manually or via a
 * make target.  Usage:  test_msx_boot <rom-file> [frames] [dump-prefix]
 */

#include <emulators/tronmsx.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SCREEN_W 256u
#define SCREEN_H 212u

static uint16_t s_fb[SCREEN_W * SCREEN_H];

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

static void write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    uint32_t i;
    if (!f) return;
    fprintf(f, "P6\n%u %u\n255\n", SCREEN_W, SCREEN_H);
    for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t p = fb[i];
        uint8_t r = (uint8_t)(((p >> 11) & 0x1Fu) << 3);
        uint8_t g = (uint8_t)(((p >> 5)  & 0x3Fu) << 2);
        uint8_t b = (uint8_t)(( p        & 0x1Fu) << 3);
        fputc(r, f); fputc(g, f); fputc(b, f);
    }
    fclose(f);
}

/* Count pixels that differ from the border/backdrop (fb[0]) — a rough "ink". */
static uint32_t ink(const uint16_t *fb)
{
    uint16_t bg = fb[0];
    uint32_t i, n = 0;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++) if (fb[i] != bg) n++;
    return n;
}

int main(int argc, char **argv)
{
    const char *rom_path = (argc > 1) ? argv[1] : "assets/msx/Lode Runner (1984)(Sony)[a].rom";
    long frames = (argc > 2) ? strtol(argv[2], NULL, 10) : 300;
    const char *prefix = (argc > 3) ? argv[3] : "/tmp/msx_boot";
    msx_cfg_t cfg;
    uint8_t *rom;
    uint32_t rom_len = 0;
    int rc;
    long f;

    rom = read_file(rom_path, &rom_len);
    if (!rom) { fprintf(stderr, "cannot read rom: %s\n", rom_path); return 2; }
    printf("rom: %s (%u bytes)\n", rom_path, rom_len);

    cfg.bios_key = NULL;
    cfg.machine  = 1;          /* BMSX_MACHINE_MSX */
    cfg.frame_hz = 60;
    rc = msx_init(&cfg);
    if (rc != MSX_OK) { fprintf(stderr, "msx_init failed: %d\n", rc); free(rom); return 1; }

    rc = msx_load_rom(rom, rom_len, 0x4000u);
    if (rc != MSX_OK) { fprintf(stderr, "msx_load_rom failed: %d\n", rc); free(rom); return 1; }
    printf("body hash: %016llx\n", (unsigned long long)msx_body_hash());

    msx_reset();

    for (f = 1; f <= frames; f++) {
        msx_run_frame();
        msx_blit_rgb565(s_fb);
        if (f == 1 || f == 30 || f == 60 || f == 120 || f == frames ||
            (f % 60) == 0) {
            char path[256];
            snprintf(path, sizeof(path), "%s_%04ld.ppm", prefix, f);
            write_ppm(path, s_fb);
            printf("frame %4ld: cyc=%12llu ink=%6u  %s\n",
                   f, (unsigned long long)msx_total_cycles(), ink(s_fb), path);
        }
    }

    printf("done: %ld frames, %llu cycles\n", frames,
           (unsigned long long)msx_total_cycles());
    free(rom);
    msx_shutdown();
    return 0;
}
