/*
 * tronMSX — C-BIOS image loader.  B-MSX Rev 1.05, §5.3.
 *
 * The BIOS is settings-loaded and lives outside the deterministic sum: it is
 * read from the host file system at msx_init() and copied into the slot 0
 * views (main ROM at 0x0000-0x7FFF, logo ROM in page 2).  Nothing on the step
 * path touches the file system again (D1).
 *
 * File naming follows the openMSX C-BIOS contribution layout:
 *   cbios_main_msx1.rom / cbios_main_msx2.rom / cbios_main_msx2+.rom
 *   cbios_logo_msx1.rom / cbios_logo_msx2.rom / cbios_logo_msx2+.rom
 * Paths are tried bare and then with a "./" prefix, matching how the other
 * B-System media apps (kagee) resolve assets relative to the working dir.
 */

#include "msx_int.h"
#include "msx_body.h"

#include <stdio.h>

static uint8_t s_main[MSX_BIOS_SIZE];
static uint8_t s_logo[MSX_LOGO_SIZE];

static const char *machine_tag(uint16_t machine)
{
    switch (machine) {
    case BMSX_MACHINE_MSX2:  return "msx2";
    case BMSX_MACHINE_MSX2P: return "msx2+";
    default:                 return "msx1";
    }
}

/* Reads up to `cap` bytes; returns the count, or 0 when the file is missing. */
static uint32_t read_rom(const char *dir, const char *name,
                         uint8_t *buf, uint32_t cap)
{
    char path[512];
    FILE *f;
    size_t n;
    int i;

    for (i = 0; i < 2; i++) {
        const char *pre = i ? "./" : "";
        snprintf(path, sizeof(path), "%s%s/%s", pre, dir, name);
        f = fopen(path, "rb");
        if (f) break;
    }
    if (!f) return 0u;

    n = fread(buf, 1, cap, f);
    fclose(f);
    return (uint32_t)n;
}

int bios_load(uint16_t machine, const char *dir)
{
    const char *tag = machine_tag(machine);
    char name[64];
    uint32_t n;

    if (!dir || !*dir) dir = "third_party/openMSX/Contrib/cbios";

    snprintf(name, sizeof(name), "cbios_main_%s.rom", tag);
    n = read_rom(dir, name, s_main, (uint32_t)sizeof(s_main));
    if (!n) return MSX_ERR_BIOS;
    slots_set_bios(s_main, n);

    snprintf(name, sizeof(name), "cbios_logo_%s.rom", tag);
    n = read_rom(dir, name, s_logo, (uint32_t)sizeof(s_logo));
    if (n) slots_set_logo(s_logo, n);      /* logo is optional */

    return MSX_OK;
}
