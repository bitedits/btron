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
#include "../../src/emulators/msx/v9938.h"

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

extern uint8_t slots_read(uint16_t addr);

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

    const char *mach_arg = (argc > 4 && argv[4][0]) ? argv[4] : NULL;
    cfg.bios_key = mach_arg;
    cfg.machine  = 1;
    if (mach_arg) {
        int m = atoi(mach_arg);
        if (m > 0) cfg.machine = (uint16_t)m;
    } else {
        if (rom_len > 65536u || strstr(rom_path, "BULLRUSH") || strstr(rom_path, "NEON") || strstr(rom_path, "NIGHT")) {
            cfg.machine = BMSX_MACHINE_MSXTR_CBIOS;
        }
    }
    cfg.frame_hz = 60;
    rc = msx_init(&cfg);
    if (rc != MSX_OK) { fprintf(stderr, "msx_init failed: %d\n", rc); free(rom); return 1; }

    rc = msx_load_rom(rom, rom_len, 0x4000u);
    if (rc != MSX_OK) { fprintf(stderr, "msx_load_rom failed: %d\n", rc); free(rom); return 1; }
    printf("body hash: %016llx\n", (unsigned long long)msx_body_hash());

    msx_reset();

extern uint16_t msx_get_pc(void);
extern int msx_is_r800_active(void);
extern v9938_t *msx_get_v9938(void);
extern uint8_t vdp_debug_reg(uint8_t reg);
extern uint8_t *vdp_vram(uint32_t *size);

    for (f = 1; f <= frames; f++) {
        if (f == 270) {
            msx_kbd_down(8, 0);
            msx_joy_set(MSX_JOY_TRGA);
        } else if (f == 290) {
            msx_kbd_up(8, 0);
            msx_joy_set(0);
        }
        msx_run_frame();
        msx_blit_rgb565(s_fb);
        if (f == 1 || (f >= 5 && f <= 35) || f == 60 || f == 120 || f == frames ||
            (f >= 170 && f <= 300 && (f % 5) == 0) ||
            (f % 60) == 0) {
            char path[256];
            v9938_t *v = msx_get_v9938();
            uint8_t m = v ? v9938_mode(v) : 0;
            snprintf(path, sizeof(path), "%s_%04ld.ppm", prefix, f);
            write_ppm(path, s_fb);
            printf("frame %4ld: pc=%04X r800=%d mode=%02X r0=%02X r1=%02X r2=%02X r7=%02X st2=%02X tr=%d(%d,%d) r17=%02X cyc=%12llu ink=%6u  %s\n",
                   f, msx_get_pc(), msx_is_r800_active(), m,
                   v ? v->regs[0] : 0, v ? v->regs[1] : 0, v ? v->regs[2] : 0, v ? v->regs[7] : 0,
                   v ? v->stat[2] : 0, v ? v->tr_active : 0, v ? v->tr_x : 0, v ? v->tr_y : 0, v ? v->regs[17] : 0,
                   (unsigned long long)msx_total_cycles(), ink(s_fb), path);
        }
        if (f == frames) {
            v9938_t *v = msx_get_v9938();
            if (v) {
                printf("V9938 regs:");
                for (int r = 0; r <= 31; r++) printf(" R%d=%02X", r, v->regs[r]);
                printf("\n");
                printf("V9938 pal:");
                for (int p = 0; p < 16; p++) printf(" P%d=%04X", p, v->pal[p]);
                printf("\n");
                printf("stat[2]=%02X tr_active=%d tr_x=%d tr_y=%d\n", v->stat[2], v->tr_active, v->tr_x, v->tr_y);
                printf("VRAM dump around line 212 (0xD400) and bottom HUD:\n");
                printf("  addr 0xD400: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                       v->vram[0xD400], v->vram[0xD401], v->vram[0xD402], v->vram[0xD403],
                       v->vram[0xD404], v->vram[0xD405], v->vram[0xD406], v->vram[0xD407]);
                uint32_t b_addr = (1008u * 256u) & (V9938_VRAM_SIZE - 1u);
                printf("  addr 0x%05X: %02X %02X %02X %02X %02X %02X %02X %02X\n",
                       b_addr, v->vram[b_addr], v->vram[b_addr+1], v->vram[b_addr+2], v->vram[b_addr+3],
                       v->vram[b_addr+4], v->vram[b_addr+5], v->vram[b_addr+6], v->vram[b_addr+7]);
            }
        }
    }

    printf("done: %ld frames, %llu cycles\n", frames,
           (unsigned long long)msx_total_cycles());
    free(rom);
    msx_shutdown();
    return 0;
}
