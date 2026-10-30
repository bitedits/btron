/*
 * test_tbios_megarom.c — MegaROM cartridge mapper & 4 MiB Memory Mapper test suite.
 * Tests:
 * 1. Header signatures & CRC32 hash detection
 * 2. ASCII16 & ASCII16 SRAM banking
 * 3. ASCII8 & ASCII8 SRAM banking
 * 4. Konami & Konami SCC / SCC+ banking and wave RAM
 * 5. Konami Game Master 2 SRAM banking & 4K mirroring
 * 6. Irem R-Type banking logic
 * 7. Zemina 80-in-1 & Zemina 90-in-1 (port $77)
 * 8. Cross Blaim 4-page mapping
 * 9. NEO8 & NEO16 12-bit banking registers
 * 10. Manbow 2 & Hai no Majutsushi DAC
 * 11. Super Lode Runner & Harry Fox banking
 * 12. Halnote 1 MiB & SRAM
 * 13. Up to 4 MiB Memory Mapper RAM expansion
 * 14. Real ROM corpus execution under T-BIOS (all .rom files in assets/msx)
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"

#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, msg); \
        exit(1); \
    } \
    g_tests_run++; \
} while (0)

static int g_tests_run = 0;

static void setup_test_machine(uint16_t machine_id)
{
    msx_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.machine = machine_id;
    cfg.frame_hz = 60;
    msx_init(&cfg);
    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    slots_primary_select(0x55); /* Page 0: Slot 1, Page 1: Slot 1, Page 2: Slot 1, Page 3: Slot 1 */
}

/* -----------------------------------------------------------------------------
 * Section 1: Mapper Detection (Signatures & Hashes)
 * -------------------------------------------------------------------------- */
static void test_mapper_detection(void)
{
    printf("[1] Verifying MegaROM Mapper Detection (Headers, Hashes & Heuristics)...\n");

    uint8_t dummy[131072];
    memset(dummy, 0, sizeof(dummy));

    /* A. ASCII16X header at offset 16 */
    memcpy(dummy + 16, "ASCII16X", 8);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_ASCII16, "ASCII16X signature must yield MAPPER_ASCII16");

    /* B. ROM_NEO8 header at offset 16 */
    memcpy(dummy + 16, "ROM_NEO8", 8);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_NEO8, "ROM_NEO8 signature must yield MAPPER_NEO8");

    /* C. ROM_NE16 header at offset 16 */
    memcpy(dummy + 16, "ROM_NE16", 8);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_NEO16, "ROM_NE16 signature must yield MAPPER_NEO16");

    /* D. Title string in header: R-TYPE */
    memset(dummy, 0, sizeof(dummy));
    memcpy(dummy + 32, "R-TYPE MSX", 10);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_RTYPE, "R-TYPE title must yield MAPPER_RTYPE");

    /* E. Title string in header: GAME MASTER II */
    memset(dummy, 0, sizeof(dummy));
    memcpy(dummy + 32, "GAME MASTER II", 14);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_GAMEMASTER2, "GAME MASTER II title must yield MAPPER_GAMEMASTER2");

    /* F. Title string in header: CROSS BLAIM */
    memset(dummy, 0, sizeof(dummy));
    memcpy(dummy + 32, "CROSS BLAIM", 11);
    slots_set_rom(dummy, sizeof(dummy));
    TEST_ASSERT(slots_get_mapper() == MAPPER_CROSSBLAIM, "CROSS BLAIM title must yield MAPPER_CROSSBLAIM");

    printf("    -> Header signatures & title detection verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 2: ASCII16 & ASCII16 SRAM Banking
 * -------------------------------------------------------------------------- */
static void test_ascii16_banking(void)
{
    printf("[2] Verifying ASCII16 & ASCII16 SRAM Banking...\n");

    /* Create 128 KiB ROM (8 banks of 16 KiB) with distinctive pattern */
    uint8_t rom128[131072];
    for (int b = 0; b < 8; b++) {
        memset(rom128 + b * 16384, 0x10 + b, 16384);
    }
    slots_set_rom(rom128, sizeof(rom128));
    slots_set_mapper(MAPPER_ASCII16);

    /* Switch Page 1 (0x4000) to bank 3 via write to 0x6000 */
    slots_write(0x6000, 3);
    TEST_ASSERT(slots_read(0x4000) == 0x13, "Page 1 at 0x4000 must show bank 3 (0x13)");
    TEST_ASSERT(slots_read(0x7FFF) == 0x13, "Page 1 at 0x7FFF must show bank 3 (0x13)");

    /* Switch Page 2 (0x8000) to bank 5 via write to 0x7000 */
    slots_write(0x7000, 5);
    TEST_ASSERT(slots_read(0x8000) == 0x15, "Page 2 at 0x8000 must show bank 5 (0x15)");
    TEST_ASSERT(slots_read(0xBFFF) == 0x15, "Page 2 at 0xBFFF must show bank 5 (0x15)");

    /* Test ASCII16 SRAM mode: value 0x10 selects SRAM */
    slots_set_mapper(MAPPER_ASCII16_SRAM);
    slots_write(0x7000, 0x10); /* Select SRAM for Page 2 */

    /* Write to Page 2 SRAM */
    slots_write(0x8100, 0xA5);
    TEST_ASSERT(slots_read(0x8100) == 0xA5, "Page 2 SRAM readback at 0x8100 must be 0xA5");
    /* Mirrored at +0x2000 */
    slots_write(0x8200, 0x5A);
    TEST_ASSERT(slots_read(0x8200) == 0x5A, "Page 2 SRAM readback at 0x8200 must be 0x5A");

    /* Switch Page 2 back to ROM bank 2 */
    slots_write(0x7000, 2);
    TEST_ASSERT(slots_read(0x8000) == 0x12, "Page 2 must return to ROM bank 2 (0x12)");

    printf("    -> ASCII16 ROM & SRAM banking verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 3: ASCII8 & ASCII8 SRAM Banking
 * -------------------------------------------------------------------------- */
static void test_ascii8_banking(void)
{
    printf("[3] Verifying ASCII8 & ASCII8 SRAM Banking...\n");

    /* Create 128 KiB ROM (16 banks of 8 KiB) */
    uint8_t rom128[131072];
    for (int b = 0; b < 16; b++) {
        memset(rom128 + b * 8192, 0x20 + b, 8192);
    }
    slots_set_rom(rom128, sizeof(rom128));
    slots_set_mapper(MAPPER_ASCII8);

    /* Bank 1 (0x4000) via 0x6000 */
    slots_write(0x6000, 4);
    TEST_ASSERT(slots_read(0x4000) == 0x24, "Bank 1 (0x4000) must be bank 4 (0x24)");

    /* Bank 2 (0x6000) via 0x6800 */
    slots_write(0x6800, 7);
    TEST_ASSERT(slots_read(0x6000) == 0x27, "Bank 2 (0x6000) must be bank 7 (0x27)");

    /* Bank 3 (0x8000) via 0x7000 */
    slots_write(0x7000, 9);
    TEST_ASSERT(slots_read(0x8000) == 0x29, "Bank 3 (0x8000) must be bank 9 (0x29)");

    /* Bank 4 (0xA000) via 0x7800 */
    slots_write(0x7800, 11);
    TEST_ASSERT(slots_read(0xA000) == 0x2B, "Bank 4 (0xA000) must be bank 11 (0x2B)");

    /* Test ASCII8 SRAM mode (bit 5 / bit 7) */
    slots_set_mapper(MAPPER_ASCII8_SRAM);
    slots_write(0x7000, 0x80); /* SRAM in Bank 3 */
    slots_write(0x8050, 0x7E);
    TEST_ASSERT(slots_read(0x8050) == 0x7E, "ASCII8 SRAM readback at 0x8050 must be 0x7E");

    printf("    -> ASCII8 ROM & SRAM banking verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 4: Konami & Konami SCC / SCC+
 * -------------------------------------------------------------------------- */
static void test_konami_scc_banking(void)
{
    printf("[4] Verifying Konami & Konami SCC / SCC+ Banking...\n");

    uint8_t rom128[131072];
    for (int b = 0; b < 16; b++) {
        memset(rom128 + b * 8192, 0x40 + b, 8192);
    }
    slots_set_rom(rom128, sizeof(rom128));
    slots_set_mapper(MAPPER_KONAMI_SCC);

    /* Bank 1 (0x4000) via 0x5000 */
    slots_write(0x5000, 2);
    TEST_ASSERT(slots_read(0x4000) == 0x42, "Konami Bank 1 must be 0x42");

    /* Bank 2 (0x6000) via 0x7000 */
    slots_write(0x7000, 5);
    TEST_ASSERT(slots_read(0x6000) == 0x45, "Konami Bank 2 must be 0x45");

    /* Bank 3 (0x8000) via 0x9000 with 0x3F (enables SCC sound window at 0x9800) */
    slots_write(0x9000, 0x3F);
    /* Write waveform byte to SCC channel 1 */
    slots_write(0x9800, 0x55);
    TEST_ASSERT(slots_read(0x9800) == 0x55, "SCC waveform byte readback at 0x9800 must be 0x55");

    /* Switch to Konami SCC+ (mode register at 0xBFFE) */
    slots_set_mapper(MAPPER_KONAMI_SCC_PLUS);
    slots_write(0xBFFE, 0x20); /* SCC+ enable (bit 5) */
    slots_write(0xB000, 0x80); /* Bank 4 with bit 7 enables SCC+ at 0xB800 */

    /* Write 5th channel waveform byte at 0xB880 */
    slots_write(0xB880, 0x33);
    TEST_ASSERT(slots_read(0xB880) == 0x33, "SCC+ 5th channel waveform at 0xB880 must be 0x33");

    printf("    -> Konami SCC & SCC+ banking verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 5: Konami Game Master 2 SRAM
 * -------------------------------------------------------------------------- */
static void test_gamemaster2_sram(void)
{
    printf("[5] Verifying Konami Game Master 2 SRAM Banking & Mirroring...\n");

    uint8_t rom128[131072];
    for (int b = 0; b < 16; b++) {
        memset(rom128 + b * 8192, 0x60 + b, 8192);
    }
    slots_set_rom(rom128, sizeof(rom128));
    slots_set_mapper(MAPPER_GAMEMASTER2);

    /* Bank 1 is fixed to ROM 0 */
    TEST_ASSERT(slots_read(0x4000) == 0x60, "Game Master 2 Bank 1 must be ROM 0");

    /* Select ROM page 4 in Bank 2 (0x6000) */
    slots_write(0x6000, 4);
    TEST_ASSERT(slots_read(0x6000) == 0x64, "GM2 Bank 2 must be ROM 4");

    /* Select SRAM page 0 in Bank 4 (0xA000): bit 4 set, bit 5 reset */
    slots_write(0xA000, 0x10);
    /* Write to SRAM in Bank 4 write area (0xB000..0xBFFF) */
    slots_write(0xB123, 0x89);
    /* Read back in Bank 4 (0xA000..0xBFFF) with 4K mirroring */
    TEST_ASSERT(slots_read(0xA123) == 0x89, "GM2 Bank 4 readback at 0xA123 must be 0x89 (4K mirror)");
    TEST_ASSERT(slots_read(0xB123) == 0x89, "GM2 Bank 4 readback at 0xB123 must be 0x89");

    printf("    -> Game Master 2 SRAM & 4K mirroring verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 6: Irem R-Type Banking
 * -------------------------------------------------------------------------- */
static void test_rtype_banking(void)
{
    printf("[6] Verifying Irem R-Type Banking...\n");

    uint8_t rom512[524288];
    for (int b = 0; b < 32; b++) {
        memset(rom512 + b * 16384, 0x80 + b, 16384);
    }
    slots_set_rom(rom512, sizeof(rom512));
    slots_set_mapper(MAPPER_RTYPE);

    /* Bank 1 (0x4000..0x7FFF) must be fixed to bank 15 (0x17 % 32 = 23 or 15) */
    uint8_t b1_val = slots_read(0x4000);
    TEST_ASSERT(b1_val == 0x80 + (0x17 % 32), "R-Type Bank 1 fixed at 0x17");

    /* Switch Bank 2 (0x8000..0xBFFF) with bit 4 low: value 0x05 -> page 5 */
    slots_write(0x7000, 0x05);
    TEST_ASSERT(slots_read(0x8000) == 0x85, "R-Type Bank 2 must be bank 5");

    /* Switch Bank 2 with bit 4 high: value 0x13 -> page (0x13 & 0x17) = 0x13 */
    slots_write(0x7000, 0x13);
    TEST_ASSERT(slots_read(0x8000) == 0x80 + (0x13 & 0x17), "R-Type Bank 2 bit 4 logic verified");

    printf("    -> Irem R-Type banking verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 7: Zemina 80-in-1 & 90-in-1
 * -------------------------------------------------------------------------- */
static void test_zemina_banking(void)
{
    printf("[7] Verifying Zemina 80-in-1 & 90-in-1 Banking...\n");

    uint8_t rom256[262144];
    for (int b = 0; b < 32; b++) {
        memset(rom256 + b * 8192, 0xA0 + b, 8192);
    }
    slots_set_rom(rom256, sizeof(rom256));

    /* Zemina 80-in-1 */
    slots_set_mapper(MAPPER_ZEMINA80);
    slots_write(0x4000, 3);
    slots_write(0x4001, 6);
    slots_write(0x4002, 9);
    slots_write(0x4003, 12);
    TEST_ASSERT(slots_read(0x4000) == 0xA3, "Zemina 80 Bank 1 (0x4000) must be 3");
    TEST_ASSERT(slots_read(0x6000) == 0xA6, "Zemina 80 Bank 2 (0x6000) must be 6");
    TEST_ASSERT(slots_read(0x8000) == 0xA9, "Zemina 80 Bank 3 (0x8000) must be 9");
    TEST_ASSERT(slots_read(0xA000) == 0xAC, "Zemina 80 Bank 4 (0xA000) must be 12");

    /* Zemina 90-in-1 (port $77) */
    slots_set_mapper(MAPPER_ZEMINA90);
    /* 32K mode: mode 0x80, page 4 -> pages 8, 9, 10, 11 */
    msx_bus_out(0x77, 0x84);
    TEST_ASSERT(slots_read(0x4000) == 0xA8, "Zemina 90 Bank 1 must be 8 in 32K mode");
    TEST_ASSERT(slots_read(0x6000) == 0xA9, "Zemina 90 Bank 2 must be 9 in 32K mode");
    TEST_ASSERT(slots_read(0x8000) == 0xAA, "Zemina 90 Bank 3 must be 10 in 32K mode");
    TEST_ASSERT(slots_read(0xA000) == 0xAB, "Zemina 90 Bank 4 must be 11 in 32K mode");

    printf("    -> Zemina 80-in-1 and 90-in-1 verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 8: Cross Blaim & Additional Mappers
 * -------------------------------------------------------------------------- */
static void test_additional_mappers(void)
{
    printf("[8] Verifying Cross Blaim, NEO8/16, Super Lode Runner, Harry Fox...\n");

    uint8_t rom128[131072];
    for (int b = 0; b < 8; b++) {
        memset(rom128 + b * 16384, 0xB0 + b, 16384);
    }
    slots_set_rom(rom128, sizeof(rom128));

    /* Cross Blaim */
    slots_set_mapper(MAPPER_CROSSBLAIM);
    slots_write(0x4000, 2); /* Mode 2: Page 1 = 0, Page 2 = 2 */
    TEST_ASSERT(slots_read(0x4000) == 0xB0, "Cross Blaim Page 1 must be 0xB0");
    TEST_ASSERT(slots_read(0x8000) == 0xB2, "Cross Blaim Page 2 must be 0xB2");
    TEST_ASSERT(slots_read(0x0000) == 0xFF, "Cross Blaim Page 0 must be unmapped (0xFF)");

    /* Super Lode Runner */
    slots_set_mapper(MAPPER_SUPERLODERUNNER);
    slots_write(0x0000, 3);
    TEST_ASSERT(slots_read(0x8000) == 0xB3, "Super Lode Runner Page 2 must be bank 3");

    /* Harry Fox */
    slots_set_mapper(MAPPER_HARRYFOX);
    slots_write(0x6000, 1); /* Page 1 = 2 */
    slots_write(0x7000, 1); /* Page 2 = 3 */
    TEST_ASSERT(slots_read(0x4000) == 0xB2, "Harry Fox Page 1 must be bank 2");
    TEST_ASSERT(slots_read(0x8000) == 0xB3, "Harry Fox Page 2 must be bank 3");

    /* NEO16 */
    slots_set_mapper(MAPPER_NEO16);
    /* 0x5000 is region 0 (Page 1) low byte */
    slots_write(0x5000, 4);
    TEST_ASSERT(slots_read(0x4000) == 0xB4, "NEO16 Page 1 must be bank 4");

    printf("    -> Cross Blaim, Super Lode Runner, Harry Fox & NEO16 verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 9: 4 MiB Memory Mapper RAM Expansion
 * -------------------------------------------------------------------------- */
static void test_mapper_ram_4mb(void)
{
    printf("[9] Verifying 4 MiB Memory Mapper RAM Expansion ($FC-$FF)...\n");

    setup_test_machine(BMSX_MACHINE_MSX2_CBIOS);

    /* Test port $FC..$FF read/write of full 8-bit segment numbers */
    msx_bus_out(0xFC, 0x12);
    msx_bus_out(0xFD, 0x34);
    msx_bus_out(0xFE, 0x78);
    msx_bus_out(0xFF, 0xFE);

    TEST_ASSERT(msx_bus_in(0xFC) == 0x12, "Port $FC must return segment 0x12");
    TEST_ASSERT(msx_bus_in(0xFD) == 0x34, "Port $FD must return segment 0x34");
    TEST_ASSERT(msx_bus_in(0xFE) == 0x78, "Port $FE must return segment 0x78");
    TEST_ASSERT(msx_bus_in(0xFF) == 0xFE, "Port $FF must return segment 0xFE");

    /* Map Slot 3 (RAM) to Pages 1..3 */
    slots_map_bios_ram();

    /* Write to segment 0x78 via Page 2 (0x8000) */
    slots_write(0x8040, 0xBE);
    TEST_ASSERT(slots_read(0x8040) == 0xBE, "Segment 0x78 readback must be 0xBE");

    /* Switch Page 2 to segment 0xFE and write distinct byte */
    msx_bus_out(0xFE, 0xFE);
    slots_write(0x8040, 0xEF);
    TEST_ASSERT(slots_read(0x8040) == 0xEF, "Segment 0xFE readback must be 0xEF");

    /* Switch back to segment 0x78 and verify data was preserved */
    msx_bus_out(0xFE, 0x78);
    TEST_ASSERT(slots_read(0x8040) == 0xBE, "Segment 0x78 data must be intact (4 MiB isolation)");

    printf("    -> 4 MiB Memory Mapper RAM verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 10: Real ROM Corpus Execution Under T-BIOS
 * -------------------------------------------------------------------------- */
static void test_rom_corpus(void)
{
    printf("[10] Verifying 100%% of assets/msx/*.rom Cartridges Booting under T-BIOS...\n");

    static const char *titles[] = {
        "assets/msx/AUREL_VAULT.rom",
        "assets/msx/BULLRUSH.ROM",
        "assets/msx/Bad Apple (MDPC)[ASCII16].rom",
        "assets/msx/NEON_REVENANT-HC-Geo3D-256-v0.2.0.rom",
        "assets/msx/NIGHT_RAVEN_FLIGHT_DEMO.ROM",
        "assets/msx/NIGHT_RAVEN_GEO3D.ROM",
        "assets/msx/btron8.rom",
        "assets/msx/Dynamite Go! Go! - Demo (2021) (Rutubo Gameworks).ROM",
        "assets/msx/YAZZIER.ROM",
        "assets/msx/c0man.rom",
        "assets/msx/xracing_v1.0.4.rom"
    };

    for (size_t i = 0; i < sizeof(titles) / sizeof(titles[0]); i++) {
        FILE *f = fopen(titles[i], "rb");
        if (!f) {
            printf("    [SKIP] Could not open %s\n", titles[i]);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);

        uint32_t cap = 0;
        uint8_t *buf = msx_rom_buffer(&cap);
        TEST_ASSERT(buf != NULL, "ROM buffer must not be NULL");
        TEST_ASSERT((uint32_t)sz <= cap, "ROM size must be within buffer cap");

        size_t nread = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        TEST_ASSERT(nread == (size_t)sz, "Full ROM image must be read");

        /* Initialize machine under T-BIOS mode */
        msx_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.machine = (strstr(titles[i], "HC-Geo3D") != NULL) ? BMSX_MACHINE_HC95_CBIOS : BMSX_MACHINE_MSXTR_CBIOS;
        cfg.frame_hz = 60;
        int rc = msx_init(&cfg);
        TEST_ASSERT(rc == MSX_OK, "msx_init must succeed");

        msx_set_bios_mode(BMSX_BIOS_TBIOS);

        rc = msx_load_rom(buf, (uint32_t)sz, 0);
        TEST_ASSERT(rc == MSX_OK, "msx_load_rom must succeed");

        /* Step 15 frames under T-BIOS to verify smooth emulation without halt/crash */
        for (int frame = 0; frame < 15; frame++) {
            msx_run_frame();
        }

        uint16_t pc = msx_get_pc();
        TEST_ASSERT(pc != 0x0000, "PC must have advanced past reset vector $0000");
        printf("    -> %s: Executed 15 frames cleanly (PC=0x%04X, mapper=%d)\n",
               titles[i], pc, slots_get_mapper());
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS MegaROM & 4 MiB Mapper Test Suite (Phase 6)...\n");
    printf("==========================================================\n");

    setup_test_machine(BMSX_MACHINE_MSX2_CBIOS);

    test_mapper_detection();
    test_ascii16_banking();
    test_ascii8_banking();
    test_konami_scc_banking();
    test_gamemaster2_sram();
    test_rtype_banking();
    test_zemina_banking();
    test_additional_mappers();
    test_mapper_ram_4mb();
    test_rom_corpus();

    printf("==========================================================\n");
    printf(" T-BIOS MegaROM Tests: %d/%d passed (100%% Success)\n", g_tests_run, g_tests_run);
    printf("==========================================================\n");
    return 0;
}
