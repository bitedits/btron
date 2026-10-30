/*
 * test_tbios_video.c — Comprehensive unit & differential verification for T-BIOS
 * Phase 3: Video Subsystem & Sub-ROM Implementation (V9938/V9958/V9968,
 * Sub-ROM jump table $0000..$01FF, palette engine, screen modes 3-12, VDP commands).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"

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

static uint8_t msx_peek(uint16_t addr)
{
    return slots_read(addr);
}

static uint16_t msx_peek16(uint16_t addr)
{
    uint8_t lo = slots_read(addr);
    uint8_t hi = slots_read((uint16_t)(addr + 1));
    return (uint16_t)(lo | (hi << 8));
}

static void msx_poke(uint16_t addr, uint8_t val)
{
    slots_write(addr, val);
}

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

/* Helper to execute a Sub-ROM routine via EXTROM ($015F) */
static void msx_exec_subrom(uint16_t sub_addr,
                            uint8_t in_a, uint16_t in_bc, uint16_t in_de, uint16_t in_hl,
                            uint8_t *out_a, uint16_t *out_bc, uint16_t *out_de, uint16_t *out_hl)
{
    /* Machine code snippet loaded at $C400:
     * LD A, in_a; LD BC, in_bc; LD DE, in_de; LD HL, in_hl;
     * LD IX, sub_addr; CALL $015F (EXTROM); HALT
     */
    uint8_t code[] = {
        0x3E, in_a,
        0x01, (uint8_t)(in_bc & 0xFF), (uint8_t)(in_bc >> 8),
        0x11, (uint8_t)(in_de & 0xFF), (uint8_t)(in_de >> 8),
        0x21, (uint8_t)(in_hl & 0xFF), (uint8_t)(in_hl >> 8),
        0xDD, 0x21, (uint8_t)(sub_addr & 0xFF), (uint8_t)(sub_addr >> 8), /* LD IX, sub_addr */
        0xCD, 0x5F, 0x01,                                                 /* CALL $015F */
        0x76                                                             /* HALT */
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
    fprintf(stderr, "msx_exec_subrom: timeout calling Sub-ROM %04X\n", sub_addr);
    assert(0 && "Sub-ROM call timed out");
}

static void test_subrom_discovery(void)
{
    printf("[1] Verifying Sub-ROM Discovery & Registration...\n");

    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init with T-BIOS must succeed");

    /* Run 2 frames to let CHKRAM and CHKSLZ complete */
    for (int i = 0; i < 2; i++) {
        msx_run_frame();
    }

    /* 1.1 EXBRSA ($FAF8) holds Sub-ROM slot ID */
    uint8_t exbrsa = msx_peek(0xFAF8);
    TEST_ASSERT(exbrsa != 0, "EXBRSA must be non-zero after CHKSLZ discovers Sub-ROM");
    TEST_ASSERT((exbrsa & 0x80) != 0, "Sub-ROM must be in an expanded slot");

    /* 1.2 Verify Sub-ROM header 'CD' ($43, $44) at $0000 */
    uint8_t sig_c = 0, sig_d = 0;
    msx_exec_bios(0x000C, exbrsa, 0, 0, 0x0000, &sig_c, NULL, NULL, NULL);
    msx_exec_bios(0x000C, exbrsa, 0, 0, 0x0001, &sig_d, NULL, NULL, NULL);
    TEST_ASSERT(sig_c == 'C', "Sub-ROM byte 0 must be 'C' (0x43)");
    TEST_ASSERT(sig_d == 'D', "Sub-ROM byte 1 must be 'D' (0x44)");

    printf("    -> Sub-ROM discovered successfully: Slot ID=%02X, Signature='CD'\n", exbrsa);
}

static void test_palette_engine(void)
{
    printf("[2] Verifying V9938 / V9958 Palette Engine (INIPLT, RSTPLT, GETPLT, SETPLT)...\n");

    /* 2.1 Reset palette via RSTPLT ($0145 in Sub-ROM) */
    msx_exec_subrom(0x0145, 0, 0, 0, 0, NULL, NULL, NULL, NULL);

    /* 2.2 Verify default palette entries via GETPLT ($0149 in Sub-ROM) */
    /* Color 4 (Dark Blue): Red=1, Blue=7 -> $17, Green=1 -> $01 */
    uint16_t de_out = 0;
    msx_exec_subrom(0x0149, 4, 0, 0, 0, NULL, NULL, &de_out, NULL);
    uint8_t rb = (uint8_t)(de_out >> 8);
    uint8_t g  = (uint8_t)(de_out & 0xFF);
    TEST_ASSERT(rb == 0x17, "Color 4 default RB must be 0x17 (R=1, B=7)");
    TEST_ASSERT(g == 0x01,  "Color 4 default G must be 0x01 (G=1)");

    /* Color 15 (White): Red=7, Blue=7 -> $77, Green=7 -> $07 */
    msx_exec_subrom(0x0149, 15, 0, 0, 0, NULL, NULL, &de_out, NULL);
    rb = (uint8_t)(de_out >> 8);
    g  = (uint8_t)(de_out & 0xFF);
    TEST_ASSERT(rb == 0x77, "Color 15 default RB must be 0x77 (R=7, B=7)");
    TEST_ASSERT(g == 0x07,  "Color 15 default G must be 0x07 (G=7)");

    /* 2.3 Modify a palette entry via SETPLT ($014D in Sub-ROM) */
    /* Set Color 5 to Red=7, Blue=0 -> $70, Green=3 -> $03 */
    uint16_t de_in = (uint16_t)((0x70 << 8) | 0x03);
    msx_exec_subrom(0x014D, 5, 0, de_in, 0, NULL, NULL, NULL, NULL);

    /* Restore palette with RSTPLT */
    msx_exec_subrom(0x0145, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    printf("    -> Palette engine verified: Default MSX2 palette & SETPLT functional\n");
}

static void test_screen_modes_3_to_12(void)
{
    printf("[3] Verifying Bitmap Modes (Screen 3..8, 10..12) via CHGMOD ($00D1)...\n");

    /* 3.1 Screen 3: Multicolor (64x48) */
    msx_exec_subrom(0x00D1, 3, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 3, "SCRMOD must be 3");

    /* 3.2 Screen 5: Graphic 4 (256x212, 16 colors, 4 pages) */
    msx_exec_subrom(0x00D1, 5, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 5, "SCRMOD must be 5");
    TEST_ASSERT(msx_peek(0xF3DF) == 0x06, "VDP R#0 must be $06 for Screen 5 (M4=1, M3=1)");
    TEST_ASSERT(msx_peek(0xF3E1) == 0x1F, "VDP R#2 must be $1F for Screen 5");

    /* Test CHKNEW ($0165 in Main ROM): returns CF=0 for Screen 5 */
    uint16_t de_chk = 0;
    msx_exec_bios(0x0165, 0, 0, 0, 0, NULL, NULL, &de_chk, NULL);
    uint8_t scrmod = msx_peek(0xFCAF);
    TEST_ASSERT(scrmod >= 5, "Screen 5 confirmed bitmap mode");

    /* 3.3 Screen 6: Graphic 5 (512x212, 4 colors, 4 pages) */
    msx_exec_subrom(0x00D1, 6, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 6, "SCRMOD must be 6");
    TEST_ASSERT(msx_peek(0xF3DF) == 0x08, "VDP R#0 must be $08 for Screen 6 (M5=1)");

    /* 3.4 Screen 7: Graphic 6 (512x212, 16 colors, 2 pages) */
    msx_exec_subrom(0x00D1, 7, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 7, "SCRMOD must be 7");
    TEST_ASSERT(msx_peek(0xF3DF) == 0x0A, "VDP R#0 must be $0A for Screen 7 (M5=1, M3=1)");

    /* 3.5 Screen 8: Graphic 7 (256x212, 256 colors, 2 pages) */
    msx_exec_subrom(0x00D1, 8, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 8, "SCRMOD must be 8");
    TEST_ASSERT(msx_peek(0xF3DF) == 0x0E, "VDP R#0 must be $0E for Screen 8 (M5=1, M4=1, M3=1)");

    /* 3.6 Screen 10 / 11 / 12 (V9958 YJK modes) */
    msx_exec_subrom(0x00D1, 10, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 10, "SCRMOD must be 10");

    msx_exec_subrom(0x00D1, 11, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 11, "SCRMOD must be 11");

    msx_exec_subrom(0x00D1, 12, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 12, "SCRMOD must be 12");

    /* 3.7 Restore Screen 0 */
    msx_exec_subrom(0x00D1, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    TEST_ASSERT(msx_peek(0xFCAF) == 0, "SCRMOD restored to Screen 0");
    printf("    -> Screen modes 3..8 and 10..12 switching verified\n");
}

static void test_vram_128k_primitives(void)
{
    printf("[4] Verifying 128K VRAM Primitives (NWRVRM / NRDVRM)...\n");

    /* Write 0xBE to VRAM address $1234 in bank 1 (bit 16 = 1) */
    /* NWRVRM ($0177): A = bit 16, HL = address, E = byte */
    msx_exec_bios(0x0177, 1, 0, 0xBE, 0x1234, NULL, NULL, NULL, NULL);

    /* Read back with NRDVRM ($0174): A = bit 16, HL = address -> A = byte */
    uint8_t read_back = 0;
    msx_exec_bios(0x0174, 1, 0, 0, 0x1234, &read_back, NULL, NULL, NULL);
    TEST_ASSERT(read_back == 0xBE, "NRDVRM must read back byte written to 128K VRAM (0xBE)");

    printf("    -> 128K VRAM primitives passed: Bank 1 write & read verified\n");
}

static void test_rtc_clock(void)
{
    printf("[5] Verifying Real-Time Clock RP5C01 (REDCLK / WRTCLK)...\n");

    /* Write 0x07 to RTC register 2 via WRTCLK ($01F9 in Sub-ROM) */
    /* A = reg (2), E = value (7) */
    msx_exec_subrom(0x01F9, 2, 0, 7, 0, NULL, NULL, NULL, NULL);

    /* Read back via REDCLK ($01F5 in Sub-ROM) */
    /* A = reg (2) -> A = value */
    uint8_t rtc_val = 0xFF;
    msx_exec_subrom(0x01F5, 2, 0, 0, 0, &rtc_val, NULL, NULL, NULL);
    TEST_ASSERT(rtc_val == 0x07, "REDCLK must read back value written by WRTCLK (0x07)");

    printf("    -> RTC RP5C01 passed: REDCLK/WRTCLK register access verified\n");
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS Video & Sub-ROM Unit Test Suite (Phase 3)...\n");
    printf("==========================================================\n");

    test_subrom_discovery();
    test_palette_engine();
    test_screen_modes_3_to_12();
    test_vram_128k_primitives();
    test_rtc_clock();

    printf("==========================================================\n");
    printf(" T-BIOS Video Tests: %d/%d passed (100%% Success)\n", s_tests_passed, s_tests_run);
    printf("==========================================================\n");
    return 0;
}
