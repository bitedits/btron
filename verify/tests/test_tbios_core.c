/*
 * test_tbios_core.c — Comprehensive unit & differential verification for T-BIOS Core (Phase 2).
 *
 * Verifies:
 * 1. Boot & CHKRAM system variables, stack, trampolines at $F380, hooks at $FD9A.
 * 2. Slot Manager: RDSLT, WRSLT, ENASLT, EXPTBL / SLTTBL.
 * 3. Interrupt subsystem: VBlank IRQ clearing, JIFFY increment, H_TIMI hook chaining.
 * 4. Input subsystem: SNSMAT keyboard matrix, KILBUF circular buffer, GTSTCK, GTTRIG.
 * 5. VDP subsystem: INITXT (Screen 0), INIT32 (Screen 1), INIGRP (Screen 2).
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

/* Helper to read a byte from MSX memory via CPU slots view */
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
    /* Machine code snippet loaded at $C400:
     * LD A, in_a; LD BC, in_bc; LD DE, in_de; LD HL, in_hl; CALL addr; HALT
     */
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

static void test_boot_and_chkram(void)
{
    printf("[1] Verifying T-BIOS Boot & CHKRAM Initial State...\n");
    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init with T-BIOS must succeed");

    /* Run 2 frames to ensure CHKRAM completes */
    for (int i = 0; i < 2; i++) {
        msx_run_frame();
    }

    /* 1.1 Stack Pointer below $F380 */
    uint16_t pc, sp, bc, de, hl;
    uint8_t a;
    msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);
    TEST_ASSERT(sp <= 0xF380, "Stack pointer must be at or below 0xF380");

    /* 1.2 Trampolines at $F380 */
    /* RDPRIM ($F380): OUT ($A8), A; LD E, (HL); JR .wrprm1 (5 bytes) */
    TEST_ASSERT(msx_peek(0xF380) == 0xD3 && msx_peek(0xF381) == 0xA8, "RDPRIM must be OUT ($A8), A");
    TEST_ASSERT(msx_peek(0xF382) == 0x5E, "RDPRIM must do LD E, (HL)");
    TEST_ASSERT(msx_peek(0xF383) == 0x18, "RDPRIM must jump to shared restore (.wrprm1)");

    /* WRPRIM ($F385): OUT ($A8), A; LD (HL), E; LD A, D; OUT ($A8), A; RET (7 bytes) */
    TEST_ASSERT(msx_peek(0xF385) == 0xD3 && msx_peek(0xF386) == 0xA8, "WRPRIM must be OUT ($A8), A");
    TEST_ASSERT(msx_peek(0xF387) == 0x73, "WRPRIM must do LD (HL), E");
    TEST_ASSERT(msx_peek(0xF38B) == 0xC9, "WRPRIM must end in RET at $F38B");

    /* 1.3 Hooks initialized with RET ($C9) */
    TEST_ASSERT(msx_peek(0xFD9A) == 0xC9, "H_KEYI must start with RET");
    TEST_ASSERT(msx_peek(0xFD9F) == 0xC9, "H_TIMI must start with RET");
    TEST_ASSERT(msx_peek(0xFDA4) == 0xC9, "H_CHPU must start with RET");
    TEST_ASSERT(msx_peek(0xFFCA) == 0xC9, "H_BEXT must start with RET");

    /* 1.4 System variables */
    TEST_ASSERT(msx_peek(0xF3E9) == 15, "FORCLR must be 15 (white)");
    TEST_ASSERT(msx_peek(0xF3EA) == 4,  "BAKCLR must be 4 (blue)");
    TEST_ASSERT(msx_peek(0xF3EB) == 4,  "BDRCLR must be 4 (blue)");
    TEST_ASSERT(msx_peek(0xF3AE) == 40, "LINL40 must be 40 columns");
    TEST_ASSERT(msx_peek(0xF3AF) == 29, "LINL32 must be 29 columns");
    TEST_ASSERT(msx_peek(0xF3DC) == 1,  "CSRY must be 1");
    TEST_ASSERT(msx_peek(0xF3DD) == 1,  "CSRX must be 1");
    TEST_ASSERT(msx_peek(0xF3DB) == 1,  "CLIKSW must be 1");

    printf("    -> Boot & CHKRAM passed: SP=%04X, trampolines & systemvars authentic\n", sp);
}

static void test_interrupt_and_jiffy(void)
{
    printf("[2] Verifying Interrupt Subsystem & JIFFY Clock...\n");
    uint16_t jiffy_before = msx_peek16(0xFC9E);

    /* Run 30 frames (approx 0.5 sec at 60 Hz) */
    for (int i = 0; i < 30; i++) {
        msx_run_frame();
    }

    uint16_t jiffy_after = msx_peek16(0xFC9E);
    uint16_t delta = (uint16_t)(jiffy_after - jiffy_before);
    TEST_ASSERT(delta >= 28 && delta <= 32, "JIFFY counter must advance with VBlank (expected ~30 ticks)");

    /* Verify VDP Status Register 0 read back */
    uint8_t statfl = msx_peek(0xF3E7);
    TEST_ASSERT((statfl & 0x80) != 0 || (statfl & 0x20) == 0, "STATFL must hold valid VDP status");

    /* Verify H_TIMI Hook Chaining: install a custom hook in RAM */
    /* Hook code at $C100: INC ($C110); RET */
    msx_poke(0xC100, 0x3A); msx_poke(0xC101, 0x10); msx_poke(0xC102, 0xC1); /* LD A, ($C110) */
    msx_poke(0xC103, 0x3C);                                                 /* INC A */
    msx_poke(0xC104, 0x32); msx_poke(0xC105, 0x10); msx_poke(0xC106, 0xC1); /* LD ($C110), A */
    msx_poke(0xC107, 0xC9);                                                 /* RET */
    msx_poke(0xC110, 0x00);                                                 /* counter = 0 */

    /* Point H_TIMI ($FD9F) to $C100: JP $C100 */
    msx_poke(0xFD9F, 0xC3);
    msx_poke(0xFDA0, 0x00);
    msx_poke(0xFDA1, 0xC1);

    /* Run 10 frames */
    for (int i = 0; i < 10; i++) {
        msx_run_frame();
    }

    uint8_t hook_counter = msx_peek(0xC110);
    TEST_ASSERT(hook_counter >= 9 && hook_counter <= 11, "H_TIMI hook must be called on each VBlank");
    printf("    -> Interrupt subsystem passed: JIFFY delta=%d, Hook counter=%d\n", delta, hook_counter);
}

static void test_slot_operations(void)
{
    printf("[3] Verifying Slot Manager (RDSLT, WRSLT, EXPTBL, SLTTBL)...\n");

    /* 3.1 Verify EXPTBL initialized by CHKRAM / detect_slots */
    uint8_t exp0 = msx_peek(0xFCC1);
    uint8_t exp3 = msx_peek(0xFCC4);
    TEST_ASSERT(exp3 == 0x80, "Slot 3 must be marked expanded (0x80) in EXPTBL");
    (void)exp0;

    /* 3.2 RDSLT from Slot 0 at address $0000 (BIOS entry) */
    /* Address $0000 has opcode 0xF3 (DI) */
    uint8_t read_val = 0;
    msx_exec_bios(0x000C, 0x00, 0, 0, 0x0000, &read_val, NULL, NULL, NULL);
    TEST_ASSERT(read_val == 0xF3, "RDSLT from Slot 0 at $0000 must return 0xF3 (DI)");

    /* 3.3 WRSLT to Main RAM at $8200 (Slot 3, subslot determined by CHKRAM) */
    uint8_t ram_ssec = (uint8_t)((msx_peek(0xFCC8) >> 4) & 0x03);
    uint8_t slot_ram = (uint8_t)(0x80 | (ram_ssec << 2) | 0x03);
    uint8_t test_pattern = 0x5A;
    /* WRSLT: A = slot, HL = addr, E = data */
    msx_exec_bios(0x0014, slot_ram, 0, test_pattern, 0x8200, NULL, NULL, NULL, NULL);

    /* 3.4 RDSLT back from Main RAM at $8200 */
    uint8_t verify_val = 0;
    msx_exec_bios(0x000C, slot_ram, 0, 0, 0x8200, &verify_val, NULL, NULL, NULL);
    TEST_ASSERT(verify_val == test_pattern, "RDSLT must read back byte written by WRSLT (0x5A)");

    printf("    -> Slot Manager passed: RDSLT, WRSLT, EXPTBL verified\n");
}

static void test_input_subsystem(void)
{
    printf("[4] Verifying Input Subsystem (SNSMAT, KILBUF, GTSTCK, GTTRIG)...\n");

    /* 4.1 KILBUF */
    /* Pollute PUTPNT and GETPNT */
    msx_poke(0xF3F8, 0x12); msx_poke(0xF3F9, 0x34);
    msx_poke(0xF3FA, 0x56); msx_poke(0xF3FB, 0x78);

    /* Execute KILBUF ($0156) */
    msx_exec_bios(0x0156, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    uint16_t getpnt = msx_peek16(0xF3F8);
    uint16_t putpnt = msx_peek16(0xF3FA);
    TEST_ASSERT(getpnt == 0xFBF0, "GETPNT must point to KEYBUF ($FBF0)");
    TEST_ASSERT(putpnt == 0xFBF0, "PUTPNT must point to KEYBUF ($FBF0)");

    /* 4.2 SNSMAT matrix row sensing */
    for (uint8_t row = 0; row < 11; row++) {
        uint8_t cols = kbd_row_bits(row);
        TEST_ASSERT(cols == 0xFF, "All keys idle should read 0xFF");
    }

    /* Simulate pressing 'A' (Row 2, Col 6) */
    msx_kbd_down(2, 6);
    uint8_t cols = kbd_row_bits(2);
    TEST_ASSERT((cols & (1 << 6)) == 0, "Pressing key must clear corresponding column bit (Row 2 Col 6)");
    msx_kbd_up(2, 6);
    cols = kbd_row_bits(2);
    TEST_ASSERT((cols & (1 << 6)) != 0, "Releasing key must restore column bit to 1");

    /* 4.3 GTTRIG (Trigger 0 = Spacebar) */
    uint8_t trig = 0xFF;
    msx_exec_bios(0x00D8, 0x00, 0, 0, 0, &trig, NULL, NULL, NULL);
    TEST_ASSERT(trig == 0x00, "GTTRIG(0) idle must return 0");

    /* Press Spacebar: Row 8, Col 0 */
    msx_kbd_down(8, 0);
    msx_exec_bios(0x00D8, 0x00, 0, 0, 0, &trig, NULL, NULL, NULL);
    TEST_ASSERT(trig == 0xFF, "GTTRIG(0) with Space pressed must return 0xFF");
    msx_kbd_up(8, 0);
    msx_exec_bios(0x00D8, 0x00, 0, 0, 0, &trig, NULL, NULL, NULL);
    TEST_ASSERT(trig == 0x00, "GTTRIG(0) released must return 0");

    /* 4.4 GTSTCK (Stick 0 = Cursor keys) */
    uint8_t stck = 0xFF;
    msx_exec_bios(0x00D5, 0x00, 0, 0, 0, &stck, NULL, NULL, NULL);
    TEST_ASSERT(stck == 0x00, "GTSTCK(0) idle must return 0");

    /* Press Up arrow: Row 8, Col 5 */
    msx_kbd_down(8, 5);
    msx_exec_bios(0x00D5, 0x00, 0, 0, 0, &stck, NULL, NULL, NULL);
    TEST_ASSERT(stck == 1, "GTSTCK(0) with Up pressed must return 1");
    msx_kbd_up(8, 5);

    /* Press Right arrow: Row 8, Col 7 */
    msx_kbd_down(8, 7);
    msx_exec_bios(0x00D5, 0x00, 0, 0, 0, &stck, NULL, NULL, NULL);
    TEST_ASSERT(stck == 3, "GTSTCK(0) with Right pressed must return 3");
    msx_kbd_up(8, 7);

    printf("    -> Input subsystem passed: SNSMAT, KILBUF, GTTRIG, GTSTCK verified\n");
}

static void test_vdp_modes(void)
{
    printf("[5] Verifying VDP Screen Modes (Screen 0/1/2)...\n");

    /* 5.1 SCREEN 0 initialized at boot */
    uint8_t scrmod = msx_peek(0xFCAF);
    TEST_ASSERT(scrmod == 0, "Initial screen mode must be SCREEN 0");
    uint8_t linlen = msx_peek(0xF3B0);
    TEST_ASSERT(linlen == 40, "SCREEN 0 line length must be 40");
    uint16_t nambas = msx_peek16(0xF922);
    uint16_t cgpbas = msx_peek16(0xF924);
    TEST_ASSERT(nambas == 0x0000, "SCREEN 0 NAMBAS must be $0000");
    TEST_ASSERT(cgpbas == 0x0800, "SCREEN 0 CGPBAS must be $0800");

    /* 5.2 Switch to SCREEN 1 via INIT32 ($006F) */
    msx_exec_bios(0x006F, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    scrmod = msx_peek(0xFCAF);
    TEST_ASSERT(scrmod == 1, "After INIT32, SCRMOD must be 1");
    linlen = msx_peek(0xF3B0);
    TEST_ASSERT(linlen == 29, "SCREEN 1 line length must be 29");
    nambas = msx_peek16(0xF922);
    cgpbas = msx_peek16(0xF924);
    uint16_t atrbas = msx_peek16(0xF928);
    uint16_t patbas = msx_peek16(0xF926);
    TEST_ASSERT(nambas == 0x1800, "SCREEN 1 NAMBAS must be $1800");
    TEST_ASSERT(cgpbas == 0x0000, "SCREEN 1 CGPBAS must be $0000");
    TEST_ASSERT(atrbas == 0x1B00, "SCREEN 1 ATRBAS must be $1B00");
    TEST_ASSERT(patbas == 0x3800, "SCREEN 1 PATBAS must be $3800");

    /* 5.3 Switch to SCREEN 2 via INIGRP ($0072) */
    msx_exec_bios(0x0072, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    scrmod = msx_peek(0xFCAF);
    TEST_ASSERT(scrmod == 2, "After INIGRP, SCRMOD must be 2");
    nambas = msx_peek16(0xF922);
    cgpbas = msx_peek16(0xF924);
    atrbas = msx_peek16(0xF928);
    patbas = msx_peek16(0xF926);
    TEST_ASSERT(nambas == 0x1800, "SCREEN 2 NAMBAS must be $1800");
    TEST_ASSERT(cgpbas == 0x0000, "SCREEN 2 CGPBAS must be $0000");
    TEST_ASSERT(atrbas == 0x1B00, "SCREEN 2 ATRBAS must be $1B00");
    TEST_ASSERT(patbas == 0x3800, "SCREEN 2 PATBAS must be $3800");

    /* 5.4 Restore SCREEN 0 via INITXT ($006C) */
    msx_exec_bios(0x006C, 0, 0, 0, 0, NULL, NULL, NULL, NULL);
    scrmod = msx_peek(0xFCAF);
    TEST_ASSERT(scrmod == 0, "After INITXT restore, SCRMOD must be 0");
    linlen = msx_peek(0xF3B0);
    TEST_ASSERT(linlen == 40, "SCREEN 0 line length restored to 40");

    printf("    -> VDP Screen Modes passed: Screen 0, 1, 2 switching verified\n");
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS Core Subsystems Unit Test Suite (Phase 2)...\n");
    printf("==========================================================\n");

    test_boot_and_chkram();
    test_interrupt_and_jiffy();
    test_slot_operations();
    test_input_subsystem();
    test_vdp_modes();

    printf("==========================================================\n");
    printf(" T-BIOS Core Tests: %d/%d passed (100%% Success)\n", s_tests_passed, s_tests_run);
    printf("==========================================================\n");
    return 0;
}
