/*
 * tronMSX — C-BIOS image loader for all 18 MSX2 / MSX2+ / Turbo R machines.
 * B-MSX Rev 1.05, §5.3.
 */

#include "msx_int.h"
#include "msx_body.h"
#include <emulators/tronmsx.h>
#include <string.h>

#include "t9769b.h"
#include "t9769c.h"

#include <btron/libc_shim.h>
#if !BTRON_HOSTED
#include "../clu/vfs.h"
#endif

static uint8_t s_main[MSX_BIOS_SIZE];
static uint8_t s_logo[MSX_LOGO_SIZE];
static uint8_t s_sub[MSX_SUBROM_SIZE];
static uint8_t s_music[MSX_MUSIC_SIZE];
static uint8_t s_disk[MSX_DISK_SIZE];
static uint8_t s_kanji[KANJI_ROM_MAX_SIZE];

extern t9769b_t g_t9769b;
extern t9769c_t g_t9769c;

/* The directory `bios_load()` searches when the caller hands it nothing, and the
 * answer is different on each side of the hosted boundary: below a checkout the
 * C-BIOS bodies live in the vendor trees, on an image that has no host file system
 * they live in the /SYS drawer `src/tools/manifest.txt` imprints.
 *
 * Handing the bare-metal side the host list was both dead weight and a broken
 * search -- `read_rom_path()` resolves through the VFS there, so every entry named
 * a folder the machine cannot have, and the volume's own BIOS went unread while
 * the window reported "C-BIOS が読み込めません".  `scripts/test_ps2.sh` scans the
 * shipped `.rodata` for a checkout path precisely to keep the two lists from
 * merging again, and the drawer it must name is the one #define the message and
 * this search share (MSX_BIOS_DEFAULT_DIR), so neither can drift from the other. */
#if BTRON_HOSTED
static const char *s_search_dirs[] = {
    "third_party/cbios-nextor_FDD-and-turboR/cbios/derived/bin",
    "third_party/cbios-nextor_FDD-and-turboR/configs/blueMSX/Machines/Shared Roms",
    "third_party/openMSX/Contrib/cbios",
    "derived/bin",
    NULL
};
#else
static const char *s_search_dirs[] = {
    MSX_BIOS_DEFAULT_DIR,
    NULL
};
#endif

static uint32_t read_rom_path(const char *path, uint8_t *buf, uint32_t cap)
{
#if BTRON_HOSTED
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return 0u;
    n = fread(buf, 1, cap, f);
    fclose(f);
    return (uint32_t)n;
#else
    size_t got = 0;
    if (vfs_read_blob(path, buf, (size_t)cap, &got) != 0) return 0u;
    return (uint32_t)got;
#endif
}

static uint32_t read_rom(const char *dir, const char *name, uint8_t *buf, uint32_t cap)
{
    char path[512];
    uint32_t n;
    int i;

    /* 1. Try explicit directory */
    if (dir && *dir) {
        snprintf(path, sizeof(path), "%s/%s", dir, name);
        n = read_rom_path(path, buf, cap);
        if (n) return n;
        snprintf(path, sizeof(path), "./%s/%s", dir, name);
        n = read_rom_path(path, buf, cap);
        if (n) return n;
    }

    /* 2. Try default search paths */
    for (i = 0; s_search_dirs[i]; i++) {
        snprintf(path, sizeof(path), "%s/%s", s_search_dirs[i], name);
        n = read_rom_path(path, buf, cap);
        if (n) return n;
        snprintf(path, sizeof(path), "./%s/%s", s_search_dirs[i], name);
        n = read_rom_path(path, buf, cap);
        if (n) return n;
    }

    return 0u;
}

static const char *main_rom_name(uint16_t machine)
{
    switch (machine) {
    case BMSX_MACHINE_MSX2_CBIOS:
    case BMSX_MACHINE_MSX2_CBIOS_FDD:
        return "cbios_main_msx2.rom";
    case BMSX_MACHINE_MSX2_CBIOS_BR:
    case BMSX_MACHINE_MSX2_CBIOS_FDD_BR:
        return "cbios_main_msx2_br.rom";
    case BMSX_MACHINE_MSX2_CBIOS_EU:
    case BMSX_MACHINE_MSX2_CBIOS_FDD_EU:
        return "cbios_main_msx2_eu.rom";
    case BMSX_MACHINE_MSX2_CBIOS_JP:
    case BMSX_MACHINE_MSX2_CBIOS_FDD_JP:
        return "cbios_main_msx2_jp.rom";

    case BMSX_MACHINE_MSX2P_CBIOS:
    case BMSX_MACHINE_MSX2P_CBIOS_FDD:
        return "cbios_main_msx2+.rom";
    case BMSX_MACHINE_MSX2P_CBIOS_BR:
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_BR:
        return "cbios_main_msx2+_br.rom";
    case BMSX_MACHINE_MSX2P_CBIOS_EU:
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_EU:
        return "cbios_main_msx2+_eu.rom";
    case BMSX_MACHINE_MSX2P_CBIOS_JP:
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_JP:
        return "cbios_main_msx2+_jp.rom";

    case BMSX_MACHINE_MSXTR_CBIOS:
    case BMSX_MACHINE_MSXTR_CBIOS_FDD:
        return "cbios_main_msxtr_jp.rom";

    case BMSX_MACHINE_HC90_CBIOS:
    case BMSX_MACHINE_HC95_CBIOS:
    case BMSX_MACHINE_HC95_CBIOS_FDD:
        return "cbios_main_msx2_jp.rom";

    default:
        return "cbios_main_msx1.rom";
    }
}

static const char *logo_rom_name(uint16_t machine)
{
    if (machine == BMSX_MACHINE_MSX) return "cbios_logo_msx1.rom";
    if ((machine >= BMSX_MACHINE_MSX2_CBIOS && machine <= BMSX_MACHINE_MSX2_CBIOS_FDD_JP) ||
        (machine >= BMSX_MACHINE_HC90_CBIOS && machine <= BMSX_MACHINE_HC95_CBIOS_FDD)) {
        return "cbios_logo_msx2.rom";
    }
    return "cbios_logo_msx2+.rom";
}

static int machine_has_fdd(uint16_t machine)
{
    return (machine == BMSX_MACHINE_MSX2_CBIOS_FDD ||
            machine == BMSX_MACHINE_MSX2_CBIOS_FDD_BR ||
            machine == BMSX_MACHINE_MSX2_CBIOS_FDD_EU ||
            machine == BMSX_MACHINE_MSX2_CBIOS_FDD_JP ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_FDD ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_BR ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_EU ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_JP ||
            machine == BMSX_MACHINE_MSXTR_CBIOS_FDD ||
            machine == BMSX_MACHINE_HC95_CBIOS_FDD);
}

static int machine_has_music(uint16_t machine)
{
    return (machine >= BMSX_MACHINE_MSX2P_CBIOS && machine <= BMSX_MACHINE_MSXTR_CBIOS_FDD);
}

static int machine_has_kanji(uint16_t machine)
{
    return (machine == BMSX_MACHINE_MSX2_CBIOS_JP ||
            machine == BMSX_MACHINE_MSX2_CBIOS_FDD_JP ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_JP ||
            machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_JP ||
            machine == BMSX_MACHINE_MSXTR_CBIOS ||
            machine == BMSX_MACHINE_MSXTR_CBIOS_FDD ||
            machine == BMSX_MACHINE_HC90_CBIOS ||
            machine == BMSX_MACHINE_HC95_CBIOS ||
            machine == BMSX_MACHINE_HC95_CBIOS_FDD);
}

int bios_load(uint16_t machine, const char *dir)
{
    const char *main_name = main_rom_name(machine);
    const char *logo_name = logo_rom_name(machine);
    uint32_t n;

    slots_set_machine(machine);

    /* 1. Main ROM (mandatory) */
    n = read_rom(dir, main_name, s_main, sizeof(s_main));
    if (!n) {
        /* Fallback to generic tag */
        if (machine >= BMSX_MACHINE_MSXTR_CBIOS) {
            n = read_rom(dir, "cbios_main_msxtr.rom", s_main, sizeof(s_main));
        } else if (machine >= BMSX_MACHINE_MSX2P_CBIOS) {
            n = read_rom(dir, "cbios_main_msx2+.rom", s_main, sizeof(s_main));
        } else if (machine >= BMSX_MACHINE_MSX2_CBIOS) {
            n = read_rom(dir, "cbios_main_msx2.rom", s_main, sizeof(s_main));
        }
    }
    if (!n) return MSX_ERR_BIOS;
    slots_set_bios(s_main, n);

    /* 2. Logo ROM (optional) */
    n = read_rom(dir, logo_name, s_logo, sizeof(s_logo));
    if (n) slots_set_logo(s_logo, n);

    /* 3. Sub-ROM (for MSX2, MSX2+, Turbo R) */
    if (machine != BMSX_MACHINE_MSX) {
        n = read_rom(dir, "cbios_sub.rom", s_sub, sizeof(s_sub));
        if (n) slots_set_sub(s_sub, n);
    }

    /* 4. MSX-MUSIC (for MSX2+ and Turbo R) */
    if (machine_has_music(machine)) {
        n = read_rom(dir, "cbios_music.rom", s_music, sizeof(s_music));
        if (n) slots_set_music(s_music, n);
    }

    /* 5. Disk ROM (for FDD machines) */
    if (machine_has_fdd(machine)) {
        n = read_rom(dir, "cbios_disk.rom", s_disk, sizeof(s_disk));
        if (!n) n = read_rom(dir, "nextor-tc8566af.rom", s_disk, sizeof(s_disk));
        if (n) slots_set_disk(s_disk, n);
    }

    /* 6. KANJI ROM (for JP and Turbo R machines) */
    if (machine_has_kanji(machine)) {
        n = read_rom(dir, "KANJI.rom", s_kanji, sizeof(s_kanji));
        if (n) {
            t9769b_set_kanji(&g_t9769b, s_kanji, n);
            t9769c_set_kanji(&g_t9769c, s_kanji, n);
        }
    }

    return MSX_OK;
}
