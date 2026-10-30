/*
 * msx_cfg.c — B-MSX machine configuration loader for blueMSX INI files.
 * Clean-room C99. B-MSX Rev 1.05. Zero dynamic allocations post-boot.
 */

#include "msx_cfg.h"
#include "msx_int.h"
#include <emulators/tronmsx.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <btron/libc_shim.h>
#if !BTRON_HOSTED
#include "../clu/vfs.h"
#endif

bmsx_machine_cfg_t g_bmsx_cfg;

const char *bmsx_cfg_name_for_machine(uint16_t machine)
{
    switch (machine) {
    case BMSX_MACHINE_MSX:
        return "MSX";

    case BMSX_MACHINE_MSX2_CBIOS:
        return "MSX2 - T-BIOS";
    case BMSX_MACHINE_MSX2_CBIOS_BR:
        return "MSX2 - T-BIOS - BR";
    case BMSX_MACHINE_MSX2_CBIOS_EU:
        return "MSX2 - T-BIOS - EU";
    case BMSX_MACHINE_MSX2_CBIOS_JP:
        return "MSX2 - T-BIOS - JP";
    case BMSX_MACHINE_MSX2_CBIOS_FDD:
        return "MSX2 - T-BIOS FDD";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_BR:
        return "MSX2 - T-BIOS FDD - BR";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_EU:
        return "MSX2 - T-BIOS FDD - EU";
    case BMSX_MACHINE_MSX2_CBIOS_FDD_JP:
        return "MSX2 - T-BIOS FDD - JP";

    case BMSX_MACHINE_MSX2P_CBIOS:
        return "MSX2+ - T-BIOS";
    case BMSX_MACHINE_MSX2P_CBIOS_BR:
        return "MSX2+ - T-BIOS - BR";
    case BMSX_MACHINE_MSX2P_CBIOS_EU:
        return "MSX2+ - T-BIOS - EU";
    case BMSX_MACHINE_MSX2P_CBIOS_JP:
        return "MSX2+ - T-BIOS - JP";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD:
        return "MSX2+ - T-BIOS FDD";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_BR:
        return "MSX2+ - T-BIOS FDD - BR";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_EU:
        return "MSX2+ - T-BIOS FDD - EU";
    case BMSX_MACHINE_MSX2P_CBIOS_FDD_JP:
        return "MSX2+ - T-BIOS FDD - JP";

    case BMSX_MACHINE_MSXTR_CBIOS:
        return "MSXturboR - T-BIOS";
    case BMSX_MACHINE_MSXTR_CBIOS_FDD:
        return "MSXturboR - T-BIOS FDD";

    case BMSX_MACHINE_HC90_CBIOS:
        return "Hitachi HC-90 - T-BIOS";
    case BMSX_MACHINE_HC95_CBIOS:
        return "Hitachi HC-95 - T-BIOS";
    case BMSX_MACHINE_HC95_CBIOS_FDD:
        return "Hitachi HC-95 - T-BIOS FDD";

    default:
        return "MSX2 - T-BIOS";
    }
}

static int file_exists(const char *path)
{
    if (!path || !*path) return 0;
#if BTRON_HOSTED
    FILE *f = fopen(path, "rb");
    if (f) {
        fclose(f);
        return 1;
    }
    return 0;
#else
    size_t dummy = 0;
    return (vfs_read_blob(path, NULL, 0, &dummy) == 0);
#endif
}

int bmsx_cfg_find_path(const char *name_or_path, char *out_path, uint32_t out_cap)
{
    if (!name_or_path || !*name_or_path || !out_path || out_cap < 16) return 0;

    /* 1. Direct path */
    if (file_exists(name_or_path)) {
        snprintf(out_path, out_cap, "%s", name_or_path);
        return 1;
    }

    /* 2. Reduced path: src/emulators/msx/configs/<name>.ini */
    snprintf(out_path, out_cap, "src/emulators/msx/configs/%s.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    snprintf(out_path, out_cap, "./src/emulators/msx/configs/%s.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    /* 3. Subdirectory layout: src/emulators/msx/configs/blueMSX/Machines/<name>/config.ini */
    snprintf(out_path, out_cap, "src/emulators/msx/configs/blueMSX/Machines/%s/config.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    snprintf(out_path, out_cap, "./src/emulators/msx/configs/blueMSX/Machines/%s/config.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    /* 4. Subdirectory layout: src/emulators/msx/configs/blueMSX/Machines/<name>.ini */
    snprintf(out_path, out_cap, "src/emulators/msx/configs/blueMSX/Machines/%s.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    /* 5. Legacy vendor path */
    snprintf(out_path, out_cap, "third_party/cbios-nextor_FDD-and-turboR/configs/blueMSX/Machines/%s/config.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    /* 6. Baremetal path */
    snprintf(out_path, out_cap, "/SYS/msx/configs/%s.ini", name_or_path);
    if (file_exists(out_path)) return 1;

    return 0;
}

static void trim_whitespace(char *s)
{
    char *p = s;
    char *end;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
}

static const char *extract_basename(const char *path)
{
    const char *p = strrchr(path, '/');
    if (!p) p = strrchr(path, '\\');
    return p ? p + 1 : path;
}

static void set_default_config(uint16_t machine, bmsx_machine_cfg_t *cfg)
{
    const char *name = bmsx_cfg_name_for_machine(machine);
    memset(cfg, 0, sizeof(*cfg));
    strncpy(cfg->name, name, sizeof(cfg->name) - 1);

    cfg->cart_slot_a = 1;
    cfg->cart_subslot_a = 0;
    cfg->cart_slot_b = 2;
    cfg->cart_subslot_b = 0;
    cfg->z80_freq = 3579545u;

    if (machine >= BMSX_MACHINE_MSXTR_CBIOS && machine <= BMSX_MACHINE_MSXTR_CBIOS_FDD) {
        /* turbo R default */
        strncpy(cfg->video_version, "V9958", sizeof(cfg->video_version) - 1);
        cfg->vram_bytes = 131072u;
        strncpy(cfg->board_type, "MSX-T9769C", sizeof(cfg->board_type) - 1);
        cfg->subslotted[0] = 1;
        cfg->subslotted[3] = 1;
        cfg->has_r800 = 1;
        cfg->r800_freq = 7159090u;
        cfg->fdc_count = (machine == BMSX_MACHINE_MSXTR_CBIOS_FDD) ? 1 : 0;

        cfg->main_slot = 0;
        cfg->main_subslot = 0;
        cfg->logo_slot = 0;
        cfg->logo_subslot = 0;
        cfg->music_slot = 0;
        cfg->music_subslot = 2;
        cfg->sub_slot = 3;
        cfg->sub_subslot = 1;
        cfg->ram_slot = 3;
        cfg->ram_subslot = 0;
        cfg->ram_kb = 1024u;
        cfg->disk_slot = 3;
        cfg->disk_subslot = 3;

        strncpy(cfg->main_rom, "tbios_main_msxtr.rom", sizeof(cfg->main_rom) - 1);
        strncpy(cfg->logo_rom, "tbios_logo.rom", sizeof(cfg->logo_rom) - 1);
        strncpy(cfg->sub_rom, "tbios_sub.rom", sizeof(cfg->sub_rom) - 1);
        strncpy(cfg->music_rom, "tbios_music.rom", sizeof(cfg->music_rom) - 1);
        if (cfg->fdc_count) strncpy(cfg->disk_rom, "tdos_disk.rom", sizeof(cfg->disk_rom) - 1);
    } else if (machine >= BMSX_MACHINE_HC90_CBIOS && machine <= BMSX_MACHINE_HC95_CBIOS_FDD) {
        /* HD64180 default */
        strncpy(cfg->video_version, "V9938", sizeof(cfg->video_version) - 1);
        cfg->vram_bytes = 131072u;
        strncpy(cfg->board_type, "MSX-HD64180", sizeof(cfg->board_type) - 1);
        cfg->subslotted[3] = 1;
        cfg->has_hd64180 = 1;
        cfg->fdc_count = (machine == BMSX_MACHINE_HC95_CBIOS_FDD) ? 1 : 0;

        cfg->main_slot = 0;
        cfg->main_subslot = 0;
        cfg->logo_slot = 0;
        cfg->logo_subslot = 0;
        cfg->sub_slot = 3;
        cfg->sub_subslot = 0;
        cfg->ram_slot = 3;
        cfg->ram_subslot = 2;
        cfg->ram_kb = 512u;
        cfg->disk_slot = 3;
        cfg->disk_subslot = 3;

        strncpy(cfg->main_rom, "tbios_main_msx2.rom", sizeof(cfg->main_rom) - 1);
        strncpy(cfg->logo_rom, "tbios_logo.rom", sizeof(cfg->logo_rom) - 1);
        strncpy(cfg->sub_rom, "tbios_sub.rom", sizeof(cfg->sub_rom) - 1);
        if (cfg->fdc_count) strncpy(cfg->disk_rom, "tdos_disk.rom", sizeof(cfg->disk_rom) - 1);
    } else if (machine != BMSX_MACHINE_MSX) {
        /* MSX2 / MSX2+ default */
        int is_2p = (machine >= BMSX_MACHINE_MSX2P_CBIOS && machine <= BMSX_MACHINE_MSX2P_CBIOS_FDD_JP);
        int has_fdd = (machine == BMSX_MACHINE_MSX2_CBIOS_FDD ||
                       machine == BMSX_MACHINE_MSX2_CBIOS_FDD_BR ||
                       machine == BMSX_MACHINE_MSX2_CBIOS_FDD_EU ||
                       machine == BMSX_MACHINE_MSX2_CBIOS_FDD_JP ||
                       machine == BMSX_MACHINE_MSX2P_CBIOS_FDD ||
                       machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_BR ||
                       machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_EU ||
                       machine == BMSX_MACHINE_MSX2P_CBIOS_FDD_JP);
        strncpy(cfg->video_version, is_2p ? "V9958" : "V9938", sizeof(cfg->video_version) - 1);
        cfg->vram_bytes = 131072u;
        strncpy(cfg->board_type, is_2p ? "MSX-T9769B" : "MSX-S1985", sizeof(cfg->board_type) - 1);
        cfg->subslotted[3] = 1;
        cfg->fdc_count = has_fdd ? 1 : 0;

        cfg->main_slot = 0;
        cfg->main_subslot = 0;
        cfg->logo_slot = 0;
        cfg->logo_subslot = 0;
        cfg->sub_slot = 3;
        cfg->sub_subslot = 0;
        cfg->music_slot = 3;
        cfg->music_subslot = 1;
        cfg->ram_slot = 3;
        cfg->ram_subslot = 2;
        cfg->ram_kb = 512u;
        cfg->disk_slot = 3;
        cfg->disk_subslot = 3;

        strncpy(cfg->main_rom, is_2p ? "tbios_main_msx2+.rom" : "tbios_main_msx2.rom", sizeof(cfg->main_rom) - 1);
        strncpy(cfg->logo_rom, "tbios_logo.rom", sizeof(cfg->logo_rom) - 1);
        strncpy(cfg->sub_rom, "tbios_sub.rom", sizeof(cfg->sub_rom) - 1);
        if (is_2p) strncpy(cfg->music_rom, "tbios_music.rom", sizeof(cfg->music_rom) - 1);
        if (has_fdd) strncpy(cfg->disk_rom, "tdos_disk.rom", sizeof(cfg->disk_rom) - 1);
    } else {
        /* MSX1 default */
        strncpy(cfg->video_version, "TMS9918A", sizeof(cfg->video_version) - 1);
        cfg->vram_bytes = 16384u;
        cfg->main_slot = 0;
        cfg->ram_slot = 3;
        cfg->ram_kb = 64u;
        strncpy(cfg->main_rom, "tbios_main_msx1.rom", sizeof(cfg->main_rom) - 1);
    }
}

int bmsx_cfg_load(const char *name_or_key, uint16_t machine, bmsx_machine_cfg_t *cfg)
{
    char path[BMSX_CFG_MAX_PATH];
    const char *target_name = NULL;
    char line[512];
    char section[64];
    FILE *f;

    if (!cfg) return -1;

    /* 1. Identify target machine name */
    if (name_or_key && *name_or_key) {
        target_name = name_or_key;
    } else {
        target_name = bmsx_cfg_name_for_machine(machine);
    }

    /* 2. Populate standard fallback defaults */
    set_default_config(machine, cfg);

    /* 3. Locate INI file */
    if (!bmsx_cfg_find_path(target_name, path, sizeof(path))) {
        /* If user passed "MSX2_FDD", check canonical machine name */
        const char *alt = bmsx_cfg_name_for_machine(machine);
        if (!bmsx_cfg_find_path(alt, path, sizeof(path))) {
            return 0; /* Defaults remain active */
        }
    }

    strncpy(cfg->ini_path, path, sizeof(cfg->ini_path) - 1);

    /* 4. Parse INI lines */
#if BTRON_HOSTED
    f = fopen(path, "r");
    if (!f) return 0;
#else
    /* Non-hosted parsing through VFS if text reader is wired */
    return 0;
#endif

    section[0] = '\0';
    while (fgets(line, sizeof(line), f)) {
        trim_whitespace(line);
        if (line[0] == '\0' || line[0] == ';' || line[0] == '#') continue;

        /* Section header: [SectionName] */
        if (line[0] == '[' && line[strlen(line) - 1] == ']') {
            line[strlen(line) - 1] = '\0';
            strncpy(section, line + 1, sizeof(section) - 1);
            trim_whitespace(section);
            continue;
        }

        /* Video Section */
        if (strcmp(section, "Video") == 0) {
            if (strncmp(line, "version=", 8) == 0) {
                strncpy(cfg->video_version, line + 8, sizeof(cfg->video_version) - 1);
            } else if (strncmp(line, "vram size=", 10) == 0) {
                uint32_t kb = (uint32_t)atoi(line + 10);
                if (kb > 0) cfg->vram_bytes = kb * 1024u;
            }
        }
        /* Board Section */
        else if (strcmp(section, "Board") == 0) {
            if (strncmp(line, "type=", 5) == 0) {
                strncpy(cfg->board_type, line + 5, sizeof(cfg->board_type) - 1);
                if (strstr(cfg->board_type, "T9769C") || strstr(cfg->board_type, "R800") || strstr(cfg->board_type, "r800")) {
                    cfg->has_r800 = 1;
                }
                if (strstr(cfg->board_type, "HD64180") || strstr(cfg->board_type, "hd64180")) {
                    cfg->has_hd64180 = 1;
                }
            }
        }
        /* Subslotted Slots Section */
        else if (strcmp(section, "Subslotted Slots") == 0) {
            if (strncmp(line, "slot 0=", 7) == 0) cfg->subslotted[0] = (uint8_t)atoi(line + 7);
            else if (strncmp(line, "slot 1=", 7) == 0) cfg->subslotted[1] = (uint8_t)atoi(line + 7);
            else if (strncmp(line, "slot 2=", 7) == 0) cfg->subslotted[2] = (uint8_t)atoi(line + 7);
            else if (strncmp(line, "slot 3=", 7) == 0) cfg->subslotted[3] = (uint8_t)atoi(line + 7);
        }
        /* External Slots Section */
        else if (strcmp(section, "External Slots") == 0) {
            if (strncmp(line, "slot A=", 7) == 0) {
                int s = 1, ss = 0;
                sscanf(line + 7, "%d %d", &s, &ss);
                cfg->cart_slot_a = (uint8_t)s;
                cfg->cart_subslot_a = (uint8_t)ss;
            } else if (strncmp(line, "slot B=", 7) == 0) {
                int s = 2, ss = 0;
                sscanf(line + 7, "%d %d", &s, &ss);
                cfg->cart_slot_b = (uint8_t)s;
                cfg->cart_subslot_b = (uint8_t)ss;
            }
        }
        /* FDC Section */
        else if (strcmp(section, "FDC") == 0) {
            if (strncmp(line, "Count=", 6) == 0) {
                cfg->fdc_count = (uint8_t)atoi(line + 6);
            }
        }
        /* CPU Section */
        else if (strcmp(section, "CPU") == 0) {
            if (strncmp(line, "Z80 Frequency=", 14) == 0) {
                cfg->z80_freq = (uint32_t)strtoul(line + 14, NULL, 10);
            } else if (strncmp(line, "R800 Frequency=", 15) == 0) {
                cfg->r800_freq = (uint32_t)strtoul(line + 15, NULL, 10);
                if (cfg->r800_freq > 0) cfg->has_r800 = 1;
            }
        }
        /* Slots Section: slot subslot startpage numpages romtype "rompath" "patch" */
        else if (strcmp(section, "Slots") == 0) {
            int slot = 0, subslot = 0, start_page = 0, num_pages = 0;
            unsigned int rom_type = 0;
            char rom_path[BMSX_CFG_MAX_PATH];
            char patch[64];
            rom_path[0] = '\0';
            patch[0] = '\0';

            int matched = sscanf(line, "%d %d %d %d %u \"%[^\"]\" \"%[^\"]\"",
                                 &slot, &subslot, &start_page, &num_pages, &rom_type,
                                 rom_path, patch);
            if (matched >= 5) {
                if (cfg->num_slots < BMSX_CFG_MAX_SLOTS) {
                    bmsx_slot_entry_t *se = &cfg->slots[cfg->num_slots++];
                    se->slot = (uint8_t)slot;
                    se->subslot = (uint8_t)subslot;
                    se->start_page = (uint8_t)start_page;
                    se->num_pages = (uint8_t)num_pages;
                    se->rom_type = rom_type;
                    strncpy(se->rom_path, rom_path, sizeof(se->rom_path) - 1);
                }

                const char *base = extract_basename(rom_path);

                /* RAM / Mapper RAM */
                if (rom_type == 22) {
                    cfg->ram_slot = (uint8_t)slot;
                    cfg->ram_subslot = (uint8_t)subslot;
                    cfg->ram_kb = (uint32_t)num_pages * 16u;
                }
                /* Main ROM */
                else if (rom_type == 141 || strstr(rom_path, "main")) {
                    cfg->main_slot = (uint8_t)slot;
                    cfg->main_subslot = (uint8_t)subslot;
                    strncpy(cfg->main_rom, base, sizeof(cfg->main_rom) - 1);
                }
                /* Logo ROM */
                else if (strstr(rom_path, "logo") || (rom_type == 20 && start_page == 4)) {
                    cfg->logo_slot = (uint8_t)slot;
                    cfg->logo_subslot = (uint8_t)subslot;
                    strncpy(cfg->logo_rom, base, sizeof(cfg->logo_rom) - 1);
                }
                /* Sub ROM */
                else if (strstr(rom_path, "sub") || (rom_type == 20 && start_page == 0 && num_pages == 2)) {
                    cfg->sub_slot = (uint8_t)slot;
                    cfg->sub_subslot = (uint8_t)subslot;
                    strncpy(cfg->sub_rom, base, sizeof(cfg->sub_rom) - 1);
                }
                /* MSX-MUSIC OPLL */
                else if (rom_type == 78 || strstr(rom_path, "music")) {
                    cfg->music_slot = (uint8_t)slot;
                    cfg->music_subslot = (uint8_t)subslot;
                    strncpy(cfg->music_rom, base, sizeof(cfg->music_rom) - 1);
                }
                /* Disk ROM */
                else if (rom_type == 143 || strstr(rom_path, "disk")) {
                    cfg->disk_slot = (uint8_t)slot;
                    cfg->disk_subslot = (uint8_t)subslot;
                    strncpy(cfg->disk_rom, base, sizeof(cfg->disk_rom) - 1);
                }
                /* Kanji ROM */
                else if (rom_type == 24 || strstr(rom_path, "KANJI")) {
                    strncpy(cfg->kanji_rom, base, sizeof(cfg->kanji_rom) - 1);
                }
            }
        }
    }

#if BTRON_HOSTED
    fclose(f);
#endif

    return 1;
}

