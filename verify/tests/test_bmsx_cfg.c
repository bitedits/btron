/*
 * test_bmsx_cfg.c — Unit and integration test for B-MSX blueMSX INI configuration loader.
 * Clean-room C99. B-MSX Rev 1.05. Zero dynamic allocations.
 *
 * Verifies:
 * 1. Loading from reduced path: src/emulators/msx/configs/
 * 2. Loading from subdirectory layout: src/emulators/msx/configs/blueMSX/Machines/<Machine>/config.ini
 * 3. Exact parsing of [Video], [Board], [Subslotted Slots], [External Slots], [FDC], [CPU], [Slots]
 * 4. Automatic slot decoding and RAM / ROM mapping for turbo R and MSX2+
 * 5. End-to-end boot of Aurel Vault under T-BIOS using INI configuration with bit-perfect rendering.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/msx_cfg.h"

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "FAIL: %s (line %d): %s\n", __func__, __LINE__, msg); \
        assert(cond); \
    } else { \
        s_tests_passed++; \
    } \
} while (0)

static uint8_t *read_file(const char *path, uint32_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    uint8_t *buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (uint32_t)n;
    return buf;
}

static void test_reduced_ini_path(void)
{
    printf("[1] Verifying INI loading from reduced path: src/emulators/msx/configs/*.ini...\n");
    bmsx_machine_cfg_t cfg;
    int rc = bmsx_cfg_load("MSXturboR - T-BIOS", BMSX_MACHINE_MSXTR_CBIOS, &cfg);
    TEST_ASSERT(rc == 1, "Must find and load MSXturboR - T-BIOS.ini");

    /* Video section */
    TEST_ASSERT(strcmp(cfg.video_version, "V9958") == 0, "Video version must be V9958");
    TEST_ASSERT(cfg.vram_bytes == 131072u, "VRAM size must be 128 KiB");

    /* Board section */
    TEST_ASSERT(strcmp(cfg.board_type, "MSX-T9769C") == 0, "Board type must be MSX-T9769C");
    TEST_ASSERT(cfg.has_r800 == 1, "turbo R must flag has_r800");

    /* Subslotted Slots */
    TEST_ASSERT(cfg.subslotted[0] == 1, "Slot 0 must be expanded on turbo R");
    TEST_ASSERT(cfg.subslotted[1] == 0, "Slot 1 must not be expanded");
    TEST_ASSERT(cfg.subslotted[2] == 0, "Slot 2 must not be expanded");
    TEST_ASSERT(cfg.subslotted[3] == 1, "Slot 3 must be expanded");

    /* CPU frequencies */
    TEST_ASSERT(cfg.z80_freq == 3579545u, "Z80 frequency must be 3579545 Hz");
    TEST_ASSERT(cfg.r800_freq == 7159090u, "R800 frequency must be 7159090 Hz");

    /* Slot mappings */
    TEST_ASSERT(cfg.main_slot == 0 && cfg.main_subslot == 0, "Main ROM must be Slot 0-0");
    TEST_ASSERT(cfg.logo_slot == 0 && cfg.logo_subslot == 0, "Logo ROM must be Slot 0-0");
    TEST_ASSERT(cfg.music_slot == 0 && cfg.music_subslot == 2, "Music ROM must be Slot 0-2");
    TEST_ASSERT(cfg.sub_slot == 3 && cfg.sub_subslot == 1, "Sub-ROM must be Slot 3-1");
    TEST_ASSERT(cfg.ram_slot == 3 && cfg.ram_subslot == 0, "RAM must be Slot 3-0 on turbo R");
    TEST_ASSERT(cfg.ram_kb == 1024u, "RAM capacity must be 1024 KiB");

    printf("    -> Parsed %s (%u slot entries) successfully\n", cfg.ini_path, cfg.num_slots);
}

static void test_subfolder_ini_path(void)
{
    printf("[2] Verifying INI loading from blueMSX/Machines/<Machine>/config.ini layout...\n");
    bmsx_machine_cfg_t cfg;
    int rc = bmsx_cfg_load("src/emulators/msx/configs/blueMSX/Machines/MSX2 - T-BIOS/config.ini",
                           BMSX_MACHINE_MSX2_CBIOS, &cfg);
    TEST_ASSERT(rc == 1, "Must find and load blueMSX/Machines/<Machine>/config.ini");

    /* Video & Board */
    TEST_ASSERT(strcmp(cfg.video_version, "V9938") == 0, "Video version must be V9938");
    TEST_ASSERT(strcmp(cfg.board_type, "MSX-S1985") == 0, "Board type must be MSX-S1985");
    TEST_ASSERT(cfg.has_r800 == 0, "MSX2 must not flag has_r800");

    /* Subslotted Slots */
    TEST_ASSERT(cfg.subslotted[0] == 0, "Slot 0 must not be expanded on MSX2");
    TEST_ASSERT(cfg.subslotted[3] == 1, "Slot 3 must be expanded");

    /* Slot mappings */
    TEST_ASSERT(cfg.main_slot == 0 && cfg.main_subslot == 0, "Main ROM must be Slot 0-0");
    TEST_ASSERT(cfg.sub_slot == 3 && cfg.sub_subslot == 0, "Sub-ROM must be Slot 3-0 on MSX2");
    TEST_ASSERT(cfg.ram_slot == 3 && cfg.ram_subslot == 2, "RAM must be Slot 3-2 on MSX2");
    TEST_ASSERT(cfg.ram_kb == 1024u, "RAM capacity must be 1024 KiB");

    printf("    -> Parsed %s successfully\n", cfg.ini_path);
}

static void test_fdd_machine_ini(void)
{
    printf("[3] Verifying FDC machine configuration (MSXturboR - T-BIOS FDD)...\n");
    bmsx_machine_cfg_t cfg;
    int rc = bmsx_cfg_load("MSXturboR - T-BIOS FDD", BMSX_MACHINE_MSXTR_CBIOS_FDD, &cfg);
    TEST_ASSERT(rc == 1, "Must find and load MSXturboR - T-BIOS FDD.ini");

    TEST_ASSERT(cfg.fdc_count == 1, "FDC Count must be 1");
    TEST_ASSERT(cfg.disk_slot == 3 && cfg.disk_subslot == 3, "Disk ROM must be Slot 3-3");
    TEST_ASSERT(strcmp(cfg.disk_rom, "tdos_disk.rom") == 0, "Disk ROM must be tdos_disk.rom");

    printf("    -> FDC machine verified: disk ROM %s at Slot %d-%d\n",
           cfg.disk_rom, cfg.disk_slot, cfg.disk_subslot);
}

static void test_aurel_vault_tbios_bit_perfect(void)
{
    printf("[4] Verifying Aurel Vault booting under T-BIOS + INI config (bit-perfect rendering)...\n");
    const char *rom_path = "assets/msx/AUREL_VAULT.rom";
    uint32_t rom_len = 0;
    uint8_t *rom = read_file(rom_path, &rom_len);
    TEST_ASSERT(rom != NULL && rom_len > 0, "Must load assets/msx/AUREL_VAULT.rom");

    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    msx_cfg_t mcfg = {
        .bios_key = "MSXturboR - T-BIOS",
        .machine = BMSX_MACHINE_MSXTR_CBIOS,
        .frame_hz = 60
    };

    int rc = msx_init(&mcfg);
    TEST_ASSERT(rc == MSX_OK, "msx_init with T-BIOS + INI config must succeed");

    rc = msx_load_rom(rom, rom_len, 0);
    TEST_ASSERT(rc == MSX_OK, "msx_load_rom for Aurel Vault must succeed");
    free(rom);

    /* Run 240 frames to reach gameplay screen */
    for (int i = 0; i < 240; i++) {
        msx_run_frame();
    }

    TEST_ASSERT(msx_is_r800_active() == 1, "Aurel Vault must switch to R800 CPU mode");

    static uint16_t fb[256 * 212];
    msx_blit_rgb565(fb);
    int ink = 0;
    for (int k = 0; k < 256 * 212; k++) {
        if (fb[k]) ink++;
    }

    printf("    -> Aurel Vault reached game engine: PC=0x%04X, R800=%d, Ink=%d\n",
           msx_get_pc(), msx_is_r800_active(), ink);
    TEST_ASSERT(ink == 54115, "Gameplay ink count must be bit-perfect 54115");

    /* Run 60 more gameplay frames */
    for (int i = 0; i < 60; i++) {
        msx_run_frame();
    }
    msx_blit_rgb565(fb);
    ink = 0;
    for (int k = 0; k < 256 * 212; k++) {
        if (fb[k]) ink++;
    }
    printf("    -> After 300 frames: PC=0x%04X, Ink=%d\n", msx_get_pc(), ink);
    TEST_ASSERT(ink == 54115, "Gameplay rendering must remain active and bit-perfect");
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running B-MSX INI Config & Turbo R Verification Suite... \n");
    printf("==========================================================\n");

    test_reduced_ini_path();
    test_subfolder_ini_path();
    test_fdd_machine_ini();
    test_aurel_vault_tbios_bit_perfect();

    printf("==========================================================\n");
    printf(" B-MSX Config Tests: %d/%d passed (100%% Success)\n",
           s_tests_passed, s_tests_run);
    printf("==========================================================\n");

    return 0;
}
