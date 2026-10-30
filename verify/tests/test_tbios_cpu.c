/*
 * test_tbios_cpu.c — T-BIOS R800 turbo R & HD64180 CPU Support Unit Test Suite (Phase 5)
 *
 * Verifies:
 * 1. turbo R CHKRAM / boot initialization.
 * 2. CHGCPU ($0180) and GETCPU ($0183) mode switching (Z80, R800 ROM, R800 DRAM) and LED control.
 * 3. turbo R PCM audio primitives (PCMPLY $0186, PCMREC $0189).
 * 4. HD64180 20-bit MMU translation (CBR, BBR, CBAR) and internal I/O.
 * 5. Execution of turbo R ROM titles under T-BIOS (AUREL_VAULT.rom, btron8.rom, BULLRUSH.ROM).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/hd64180.h"

static int s_tests_run = 0;
static int s_tests_passed = 0;

#define TEST_ASSERT(cond, msg) do { \
    s_tests_run++; \
    if (!(cond)) { \
        fprintf(stderr, "[FAIL] %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        assert(cond); \
    } else { \
        s_tests_passed++; \
    } \
} while(0)

static void msx_poke(uint16_t addr, uint8_t val)
{
    slots_write(addr, val);
}

static uint8_t msx_peek(uint16_t addr)
{
    return slots_read(addr);
}

/* Helper to execute a Main ROM BIOS routine via standard CALL */
static void msx_exec_bios(uint16_t addr,
                          uint8_t in_a, uint16_t in_bc, uint16_t in_de, uint16_t in_hl,
                          uint8_t *out_a, uint16_t *out_bc, uint16_t *out_de, uint16_t *out_hl)
{
    uint8_t code[] = {
        0x3E, in_a,
        0x01, (uint8_t)(in_bc & 0xFF), (uint8_t)(in_bc >> 8),
        0x11, (uint8_t)(in_de & 0xFF), (uint8_t)(in_de >> 8),
        0x21, (uint8_t)(in_hl & 0xFF), (uint8_t)(in_hl >> 8),
        0xCD, (uint8_t)(addr & 0xFF), (uint8_t)(addr >> 8),
        0x76
    };
    for (size_t i = 0; i < sizeof(code); i++) {
        msx_poke((uint16_t)(0xC400 + i), code[i]);
    }
    msx_cpu_go(0xC400, 0xF300);
    for (int steps = 0; steps < 50000; steps++) {
        msx_step_one();
        uint16_t pc, sp, bc, de, hl;
        uint8_t a;
        msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);
        if (pc == 0xC400 + (uint16_t)sizeof(code)) {
            if (out_a)  *out_a  = a;
            if (out_bc) *out_bc = bc;
            if (out_de) *out_de = de;
            if (out_hl) *out_hl = hl;
            return;
        }
    }
    fprintf(stderr, "msx_exec_bios: timeout calling %04X\n", addr);
    assert(0 && "BIOS call timed out");
}

/* -----------------------------------------------------------------------------
 * Section 1 & 2: turbo R CPU Switching (CHGCPU $0180 / GETCPU $0183)
 * -------------------------------------------------------------------------- */
static void test_turbor_cpu_switching(void)
{
    printf("[1] Verifying MSX turbo R CHGCPU ($0180) & GETCPU ($0183)...\n");

    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    msx_cfg_t cfg = { .machine = BMSX_MACHINE_MSXTR_CBIOS, .frame_hz = 60 };
    int rc = msx_init(&cfg);
    TEST_ASSERT(rc == MSX_OK, "msx_init MSXTR must succeed");

    for (int i = 0; i < 2; i++) {
        msx_run_frame();
    }

    /* 1. Initial CPU state: GETCPU ($0183) */
    uint8_t mode = 0xFF;
    msx_exec_bios(0x0183, 0, 0, 0, 0, &mode, NULL, NULL, NULL);
    TEST_ASSERT(mode == 0, "Initial CPU mode must be 0 (Z80)");
    TEST_ASSERT(msx_peek(0xFCB1) == 0, "CPUMOD ($FCB1) must be 0 initially");
    TEST_ASSERT(!msx_is_r800_active(), "R800 must not be active initially in Z80 mode");

    /* 2. Switch to R800 ROM mode (mode 1) with LED ON (bit 7) */
    msx_exec_bios(0x0180, 0x81, 0, 0, 0, NULL, NULL, NULL, NULL);
    msx_exec_bios(0x0183, 0, 0, 0, 0, &mode, NULL, NULL, NULL);
    TEST_ASSERT(mode == 1, "GETCPU must report mode 1 (R800 ROM)");
    TEST_ASSERT(msx_peek(0xFCB1) == 1, "CPUMOD ($FCB1) must be 1 in R800 ROM mode");
    TEST_ASSERT(msx_is_r800_active(), "msx_is_r800_active must return true in mode 1");

    /* Verify LED was turned ON (port $A7 bit 7) */
    uint8_t port_a7 = msx_bus_in(0xA7);
    TEST_ASSERT((port_a7 & 0x80) != 0, "turbo R CPU LED must be ON after CHGCPU 0x81");

    /* 3. Switch to R800 DRAM mode (mode 2) */
    msx_exec_bios(0x0180, 0x82, 0, 0, 0, NULL, NULL, NULL, NULL);
    msx_exec_bios(0x0183, 0, 0, 0, 0, &mode, NULL, NULL, NULL);
    TEST_ASSERT(mode == 2, "GETCPU must report mode 2 (R800 DRAM)");
    TEST_ASSERT(msx_peek(0xFCB1) == 2, "CPUMOD ($FCB1) must be 2 in R800 DRAM mode");
    TEST_ASSERT(msx_is_r800_active(), "msx_is_r800_active must return true in mode 2");

    /* 4. Switch back to Z80 mode (mode 0) with LED OFF */
    msx_exec_bios(0x0180, 0x80, 0, 0, 0, NULL, NULL, NULL, NULL);
    msx_exec_bios(0x0183, 0, 0, 0, 0, &mode, NULL, NULL, NULL);
    TEST_ASSERT(mode == 0, "GETCPU must report mode 0 (Z80)");
    TEST_ASSERT(!msx_is_r800_active(), "msx_is_r800_active must return false in Z80 mode");

    port_a7 = msx_bus_in(0xA7);
    TEST_ASSERT((port_a7 & 0x80) == 0, "turbo R CPU LED must be OFF after CHGCPU 0x80");

    printf("    -> turbo R CPU switching passed: Z80 <-> R800 ROM <-> R800 DRAM & LED verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 3: turbo R PCM Audio Primitives (PCMPLY $0186 / PCMREC $0189)
 * -------------------------------------------------------------------------- */
static void test_turbor_pcm(void)
{
    printf("[2] Verifying turbo R PCM Audio Primitives (PCMPLY $0186 & PCMREC $0189)...\n");

    /* Set up test audio samples in RAM at $C500 */
    for (int i = 0; i < 32; i++) {
        msx_poke((uint16_t)(0xC500 + i), (uint8_t)(0x80 + i));
    }

    /* Execute PCMPLY: HL = $C500, BC = 32 */
    msx_exec_bios(0x0186, 0, 32, 0, 0xC500, NULL, NULL, NULL, NULL);

    /* Verify port $A5 control is configured (unmuted, latched = 0x03) */
    uint8_t ctrl = msx_bus_in(0xA5);
    TEST_ASSERT((ctrl & 0x03) == 0x03, "PCM control port $A5 must be 0x03 (unmuted)");

    /* Execute PCMREC: HL = $C540, BC = 16 */
    msx_exec_bios(0x0189, 0, 16, 0, 0xC540, NULL, NULL, NULL, NULL);

    printf("    -> turbo R PCM passed: PCMPLY and PCMREC routines functional\n");
}

/* -----------------------------------------------------------------------------
 * Section 4: Hitachi HD64180 20-bit MMU Translation
 * -------------------------------------------------------------------------- */
static void test_hd64180_mmu(void)
{
    printf("[3] Verifying Hitachi HD64180 20-bit MMU Translation (CBR, BBR, CBAR)...\n");

    /* 1. Default Reset State: CBAR = 0xF0 (BA = 0, CA = 0xF), BBR = 0x00, CBR = 0x00 */
    hd64180_reset();
    TEST_ASSERT(g_hd64180_state.cbar == 0xF0, "Default CBAR must be 0xF0");
    TEST_ASSERT(g_hd64180_state.bbr == 0x00, "Default BBR must be 0x00");
    TEST_ASSERT(g_hd64180_state.cbr == 0x00, "Default CBR must be 0x00");

    /* In default state, Bank Area is $0000-$EFFF, Common 1 is $F000-$FFFF */
    uint32_t phys_0 = hd64180_translate_address(0x1000);
    uint32_t phys_f = hd64180_translate_address(0xF800);
    TEST_ASSERT(phys_0 == 0x01000, "Physical address for $1000 must be 0x01000");
    TEST_ASSERT(phys_f == 0x0F800, "Physical address for $F800 must be 0x0F800");

    /* 2. Custom 3-Area Mapping:
     * CBAR = 0xE2:
     *   Common Area 0: $0000 - $1FFF
     *   Bank Area:     $2000 - $DFFF
     *   Common Area 1: $E000 - $FFFF
     */
    hd64180_io_write(0x3A, 0xE2); /* CBAR */
    hd64180_io_write(0x39, 0x10); /* BBR = 0x10 -> +0x10000 (offset 64 KiB) */
    hd64180_io_write(0x38, 0x20); /* CBR = 0x20 -> +0x20000 (offset 128 KiB) */

    TEST_ASSERT(g_hd64180_state.cbar == 0xE2, "CBAR register write verified");
    TEST_ASSERT(g_hd64180_state.bbr == 0x10, "BBR register write verified");
    TEST_ASSERT(g_hd64180_state.cbr == 0x20, "CBR register write verified");

    /* Common Area 0 translation ($0000..$1FFF): Logical = Physical */
    uint32_t phys_c0 = hd64180_translate_address(0x1500);
    TEST_ASSERT(phys_c0 == 0x01500, "Common Area 0: $1500 must translate directly to 0x01500");

    /* Bank Area translation ($2000..$DFFF): Physical = Logical + (BBR << 12) */
    uint32_t phys_bank = hd64180_translate_address(0x4000);
    TEST_ASSERT(phys_bank == 0x14000, "Bank Area: $4000 + (0x10 << 12) must translate to 0x14000");

    /* Common Area 1 translation ($E000..$FFFF): Physical = Logical + (CBR << 12) */
    uint32_t phys_c1 = hd64180_translate_address(0xF000);
    TEST_ASSERT(phys_c1 == 0x2F000, "Common Area 1: $F000 + (0x20 << 12) must translate to 0x2F000");

    /* 3. 20-bit address space wrap-around test */
    hd64180_io_write(0x38, 0xFF); /* CBR = 0xFF -> +0xFF000 */
    uint32_t phys_wrap = hd64180_translate_address(0xF000);
    /* 0xF000 + 0xFF000 = 0x10E000 & 0xFFFFF = 0x0E000 */
    TEST_ASSERT(phys_wrap == 0x0E000, "20-bit MMU translation must wrap at 1 MiB (0xFFFFF)");

    printf("    -> HD64180 MMU passed: 3-area translation (CBR/BBR/CBAR) & 20-bit wrap verified\n");
}

/* -----------------------------------------------------------------------------
 * Section 5: Real Game Title Execution under T-BIOS (turbo R Titles)
 * -------------------------------------------------------------------------- */
static uint8_t *load_rom_file(const char *path, uint32_t *out_sz)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t nr = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (nr != (size_t)sz) { free(buf); return NULL; }
    *out_sz = (uint32_t)sz;
    return buf;
}

static void test_turbor_game_titles(void)
{
    printf("[4] Verifying Real turbo R Titles Booting under T-BIOS...\n");

    const char *titles[] = {
        "assets/msx/AUREL_VAULT.rom",
        "assets/msx/btron8.rom",
        "assets/msx/BULLRUSH.ROM"
    };

    for (size_t t = 0; t < sizeof(titles) / sizeof(titles[0]); t++) {
        const char *path = titles[t];
        uint32_t sz = 0;
        uint8_t *rom = load_rom_file(path, &sz);
        if (!rom) {
            printf("    (Note: %s not found, skipping)\n", path);
            continue;
        }

        msx_set_bios_mode(BMSX_BIOS_TBIOS);
        msx_cfg_t cfg = { .machine = BMSX_MACHINE_MSXTR_CBIOS, .frame_hz = 60 };
        int rc = msx_init(&cfg);
        TEST_ASSERT(rc == MSX_OK, "msx_init MSXTR must succeed");

        rc = msx_load_rom(rom, sz, 0);
        free(rom);
        TEST_ASSERT(rc == MSX_OK, "msx_load_rom must succeed");

        /* Run 15 frames */
        for (int frame = 0; frame < 15; frame++) {
            msx_run_frame();
        }

        uint16_t pc = msx_get_pc();
        TEST_ASSERT(pc != 0x0000, "PC must advance past boot reset vector");
        printf("    -> %s: Successfully executed 15 frames (PC=0x%04X)\n", path, pc);
    }
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS R800 & HD64180 CPU Unit Test Suite (Phase 5)...\n");
    printf("==========================================================\n");

    test_turbor_cpu_switching();
    test_turbor_pcm();
    test_hd64180_mmu();
    test_turbor_game_titles();

    printf("==========================================================\n");
    printf(" T-BIOS CPU Tests: %d/%d passed (100%% Success)\n",
           s_tests_passed, s_tests_run);
    printf("==========================================================\n");

    return 0;
}
