/*
 * Headless disk harness for the B-MSX loader in msx_app.c.
 * Every raw .dsk under MSX_ROM_CATALOGUE (assets/msx) is mounted the way
 * msx_load() does: machine from the file name, msx_init, slots_set_dsk,
 * msx_reset. Each image must leave its boot sector at $C000. A disk whose
 * root holds MSXDOS.SYS must also leave that file's first 16 bytes at $0100.
 * The frame is blitted, the same shot the window would paint, and it has to
 * contain ink. The first disk is then mounted again, which is the File menu
 * returning to another image. Build: make msx-dsk
 */
#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <ctype.h>

extern uint16_t msx_get_pc(void);

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

static uint16_t fat_next(const uint8_t *d, uint32_t n, uint16_t res, uint16_t cl)
{
    uint32_t o = (uint32_t)res * 512u + (uint32_t)cl + ((uint32_t)cl >> 1);
    uint16_t v;
    if (o + 1u >= n) return 0xFFFu;
    v = (uint16_t)(d[o] | (d[o + 1u] << 8));
    if (cl & 1u) return (uint16_t)(v >> 4);
    return (uint16_t)(v & 0x0FFFu);
}

/* First bytes of a named root file, via the same FAT the emulator walks. */
static int file_head(const uint8_t *d, uint32_t n, const char *name11,
                     uint8_t *out, uint32_t want)
{
    uint16_t res, spf, root_ent, root, data, cl;
    uint8_t spc, fats;
    uint32_t i, pos, got, cps;
    if (n < 512u) return 0;
    spc = d[0x0D];
    res = (uint16_t)(d[0x0E] | (d[0x0F] << 8));
    fats = d[0x10];
    root_ent = (uint16_t)(d[0x11] | (d[0x12] << 8));
    spf = (uint16_t)(d[0x16] | (d[0x17] << 8));
    if (spc == 0) return 0;
    root = (uint16_t)(res + (uint16_t)fats * spf);
    data = (uint16_t)(root + (uint16_t)(((uint32_t)root_ent * 32u + 511u) / 512u));
    for (i = 0; i < root_ent; i++) {
        uint32_t o = (uint32_t)root * 512u + i * 32u;
        if (o + 32u > n) return 0;
        if (d[o] == 0) return 0;
        if (memcmp(d + o, name11, 11) != 0) continue;
        cl = (uint16_t)(d[o + 26] | (d[o + 27] << 8));
        cps = (uint32_t)spc * 512u;
        pos = 0;
        got = 0;
        while (got < want) {
            uint32_t sec, off, chunk, k;
            if (cl < 2u || cl >= 0xFF8u) return 0;
            sec = (uint32_t)data + (uint32_t)(cl - 2u) * spc + pos / 512u;
            off = pos % 512u;
            chunk = 512u - off;
            if (chunk > want - got) chunk = want - got;
            if (sec * 512u + off + chunk > n) return 0;
            for (k = 0; k < chunk; k++) out[got + k] = d[sec * 512u + off + k];
            got += chunk;
            pos += chunk;
            if (pos >= cps) { cl = fat_next(d, n, res, cl); pos = 0; }
        }
        return 1;
    }
    return 0;
}

#define SCREEN_W 256u
#define SCREEN_H 212u
#define DSK_CAP  32
#define NAME_CAP 128
/* Emulator menu fast-forward in msx_app.c: 300 frames, then pump audio with
 * queue 0 so the block is rendered and dropped. msx_app_fast_forward() is that
 * loop. The headless harness runs the same step until the disk paints. */
#define FF_FRAMES 300
#define FF_LIMIT  10
#define PCM_FRAME 735

/* Same choice as msx_machine_for() in msx_app.c. A disk is decided before
 * the ROM size rule. Bad Apple keeps the V9958; every other disk is MSX2 FDD. */
static uint16_t machine_for(const char *name)
{
    if (name && strstr(name, "Bad Apple")) return BMSX_MACHINE_MSX2P_CBIOS_FDD;
    return BMSX_MACHINE_MSX2_CBIOS_FDD;
}

static int is_dsk_name(const char *name)
{
    size_t n;
    char a, b, c;
    if (!name) return 0;
    n = strlen(name);
    if (n < 5 || name[n - 4] != '.') return 0;
    a = (char)tolower((unsigned char)name[n - 3]);
    b = (char)tolower((unsigned char)name[n - 2]);
    c = (char)tolower((unsigned char)name[n - 1]);
    return a == 'd' && b == 's' && c == 'k';
}

static int cmp_name(const void *a, const void *b)
{
    return strcmp((const char *)a, (const char *)b);
}

/* The catalogue walk msx_scan_roms() does, limited to disk images. */
static int collect_disks(char names[][NAME_CAP], int cap)
{
    DIR *d;
    struct dirent *de;
    int n = 0;

    d = opendir(MSX_ROM_CATALOGUE);
    if (!d) {
        fprintf(stderr, "cannot open %s\n", MSX_ROM_CATALOGUE);
        return -1;
    }
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        if (!is_dsk_name(de->d_name)) continue;
        if (n >= cap) {
            fprintf(stderr, "more than %d disk images in %s\n", cap, MSX_ROM_CATALOGUE);
            closedir(d);
            return -1;
        }
        snprintf(names[n], NAME_CAP, "%s", de->d_name);
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, NAME_CAP, cmp_name);
    return n;
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

static uint32_t ink(const uint16_t *fb)
{
    uint16_t bg = fb[0];
    uint32_t i, n = 0;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++) if (fb[i] != bg) n++;
    return n;
}

/* Same step as XCMD_EMU_FAST_FORWARD / msx_app_fast_forward: run the frames,
 * render the mix, and do not queue it. */
static void bmsx_fast_forward(int frames)
{
    int i;
    int16_t pcm[PCM_FRAME * 2];
    uint32_t n;

    for (i = 0; i < frames; i++) {
        msx_run_frame();
        n = 0;
        msx_psg_submit_pcm(pcm, &n);
    }
}

static uint32_t fb_hash(const uint16_t *fb)
{
    uint32_t h = 2166136261u;
    uint32_t i;
    for (i = 0; i < SCREEN_W * SCREEN_H; i++) {
        h ^= (uint32_t)(fb[i] & 0xFFu);
        h *= 16777619u;
        h ^= (uint32_t)((fb[i] >> 8) & 0xFFu);
        h *= 16777619u;
    }
    return h;
}

static void shot_path(char *out, size_t cap, const char *name)
{
    size_t i, o = 0;
    const char *prefix = "/tmp/msx_dsk_shots/";
    for (i = 0; prefix[i] && o + 1 < cap; i++) out[o++] = prefix[i];
    for (i = 0; name[i] && o + 5 < cap; i++) {
        unsigned char c = (unsigned char)name[i];
        out[o++] = (isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    }
    if (o + 5 < cap) {
        out[o++] = '.'; out[o++] = 'p'; out[o++] = 'p'; out[o++] = 'm';
    }
    out[o] = '\0';
}

/* One menu selection: init the FDD machine, mount, then fast-forward until
 * the picture is no longer the C-BIOS firmware text. */
static int boot_disk(const char *name)
{
    char path[512];
    char ppm[512];
    uint8_t *img;
    uint32_t len = 0;
    msx_cfg_t cfg;
    uint16_t machine;
    int rc;
    int step;
    long frame;
    int saw_boot = 0;
    int want_dos;
    int drew = 0;
    uint8_t expect[16];
    uint8_t b0, b1, b2;
    int i;
    static uint16_t fb[SCREEN_W * SCREEN_H];
    uint32_t painted;
    uint32_t banner = 0;
    int have_banner = 0;

    snprintf(path, sizeof(path), "%s/%s", MSX_ROM_CATALOGUE, name);
    img = read_file(path, &len);
    if (!img) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    if (len < 3u) { fprintf(stderr, "FAIL %s: image shorter than a boot jump\n", name); free(img); return 1; }
    b0 = img[0]; b1 = img[1]; b2 = img[2];
    want_dos = file_head(img, len, "MSXDOS  SYS", expect, 16);

    machine = machine_for(name);
    memset(&cfg, 0, sizeof(cfg));
    cfg.machine = machine;
    cfg.frame_hz = 60;
    rc = msx_init(&cfg);
    if (rc != MSX_OK) { fprintf(stderr, "msx_init %d\n", rc); free(img); return 1; }
    if (msx_get_machine() != machine) {
        fprintf(stderr, "FAIL %s: machine %u, harness asked %u\n",
                name, msx_get_machine(), machine);
        free(img);
        return 1;
    }
    rc = slots_set_dsk(img, len);
    if (rc != MSX_OK) { fprintf(stderr, "slots_set_dsk %d\n", rc); free(img); return 1; }
    msx_reset();

    frame = 0;
    /* One frame at a time until the sector lands. The picture on the frame
     * before that is still C-BIOS. The disk's own code paints afterwards. */
    while (frame < 900) {
        uint32_t h;
        bmsx_fast_forward(1);
        frame++;
        msx_blit_rgb565(fb);
        h = fb_hash(fb);
        if (slots_read(0xC000) == b0 && slots_read(0xC001) == b1 &&
            slots_read(0xC002) == b2) {
            saw_boot = 1;
            break;
        }
        banner = h;
        have_banner = 1;
    }
    if (saw_boot) {
        uint32_t seen = 0;
        int holds = 0;
        printf("  frame %ld C-BIOS hash=%08X pc=%04X\n", frame, banner, msx_get_pc());
        for (step = 0; step < FF_LIMIT && !drew; step++) {
            uint32_t h;
            bmsx_fast_forward(FF_FRAMES);
            frame += FF_FRAMES;
            if (slots_read(0xC000) != b0 || slots_read(0xC001) != b1 ||
                slots_read(0xC002) != b2) break;
            msx_blit_rgb565(fb);
            h = fb_hash(fb);
            painted = ink(fb);
            /* A cleared frame is the program starting, not the picture it holds. */
            if (!have_banner || h == banner || painted < 200u) {
                holds = 0;
                seen = 0;
                continue;
            }
            if (h == seen) holds++;
            else { seen = h; holds = 1; }
            if (holds >= 2) {
                drew = 1;
                printf("  frame %ld disk picture pc=%04X C000=%02X%02X%02X 0100=%02X%02X%02X%02X hash=%08X\n",
                       frame, msx_get_pc(),
                       slots_read(0xC000), slots_read(0xC001), slots_read(0xC002),
                       slots_read(0x0100), slots_read(0x0101),
                       slots_read(0x0102), slots_read(0x0103), h);
            }
        }
    }

    if (!drew || !saw_boot ||
        slots_read(0xC000) != b0 || slots_read(0xC001) != b1 || slots_read(0xC002) != b2) {
        fprintf(stderr, "FAIL %s: fast-forward %ld frames, pc=%04X, disk picture never replaced the C-BIOS screen\n",
                name, frame, msx_get_pc());
        free(img);
        return 1;
    }
    printf("  boot sector at $C000\n");
    if (want_dos) {
        for (i = 0; i < 16; i++) {
            if (slots_read((uint16_t)(0x0100 + i)) != expect[i]) {
                fprintf(stderr, "FAIL %s: $%04X is %02X, MSXDOS.SYS says %02X\n",
                        name, 0x0100 + i, slots_read((uint16_t)(0x0100 + i)), expect[i]);
                free(img);
                return 1;
            }
        }
        printf("  MSXDOS.SYS at $0100\n");
    }

    msx_blit_rgb565(fb);
    painted = ink(fb);
    shot_path(ppm, sizeof(ppm), name);
    write_ppm(ppm, fb);
    if (painted < 200u) {
        fprintf(stderr, "FAIL %s: screenshot has %u ink pixels (%s)\n", name, painted, ppm);
        free(img);
        return 1;
    }
    printf("  screenshot %s (%u ink)\n", ppm, painted);
    free(img);
    return 0;
}

int main(void)
{
    char names[DSK_CAP][NAME_CAP];
    int n, i, rc;

    if (system("mkdir -p /tmp/msx_dsk_shots") != 0) {
        fprintf(stderr, "cannot create /tmp/msx_dsk_shots\n");
        return 1;
    }
    n = collect_disks(names, DSK_CAP);
    if (n < 1) {
        fprintf(stderr, "no disk images in %s\n", MSX_ROM_CATALOGUE);
        return 1;
    }
    printf("msx-dsk: %d disk images in %s\n", n, MSX_ROM_CATALOGUE);
    rc = 0;
    for (i = 0; i < n; i++) {
        printf("%s (%s)\n", names[i],
               machine_for(names[i]) == BMSX_MACHINE_MSX2P_CBIOS_FDD ? "MSX2+ FDD" : "MSX2 FDD");
        rc |= boot_disk(names[i]);
    }
    printf("reload %s\n", names[0]);
    rc |= boot_disk(names[0]);
    if (rc) fprintf(stderr, "msx-dsk failed\n");
    else printf("msx-dsk ok\n");
    return rc ? 1 : 0;
}
