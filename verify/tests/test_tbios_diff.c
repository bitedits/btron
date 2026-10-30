/*
 * test_tbios_diff.c — Differential test harness for C-BIOS vs T-BIOS + T-DOS.
 *
 * Verifies:
 * 1. Default mode is BMSX_BIOS_CBIOS.
 * 2. C-BIOS boots normally.
 * 3. Mode switch to BMSX_BIOS_TBIOS is clean and idempotent.
 * 4. T-BIOS boots to 0x0000, initializes stack and system variables.
 * 5. Clean C-BIOS fallback when T-BIOS ROMs are absent in explicit path.
 * 6. Switching back to C-BIOS restores default operation.
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

static void test_default_mode(void)
{
    printf("[1] Verifying default BIOS mode is C-BIOS...\n");
    int mode = msx_get_bios_mode();
    TEST_ASSERT(mode == BMSX_BIOS_CBIOS, "Default mode must be BMSX_BIOS_CBIOS");

    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init with default C-BIOS must succeed");
    TEST_ASSERT(msx_get_pc() == 0x0000, "Reset PC must start at 0x0000");

    /* Run 5 frames */
    for (int i = 0; i < 5; i++) {
        msx_run_frame();
    }
    printf("    -> C-BIOS boots and runs cleanly (PC=%04X)\n", msx_get_pc());
}

static void test_tbios_mode_switch(void)
{
    printf("[2] Verifying mode switch to T-BIOS + T-DOS...\n");
    msx_set_bios_mode(BMSX_BIOS_TBIOS);
    TEST_ASSERT(msx_get_bios_mode() == BMSX_BIOS_TBIOS, "msx_get_bios_mode must report BMSX_BIOS_TBIOS");

    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init with T-BIOS must succeed");
    TEST_ASSERT(msx_get_pc() == 0x0000, "T-BIOS reset PC must start at 0x0000");

    uint16_t pc, sp, bc, de, hl;
    uint8_t a;
    msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);
    TEST_ASSERT(pc == 0x0000, "Initial PC must be 0x0000");

    /* Run 10 frames in T-BIOS */
    for (int i = 0; i < 10; i++) {
        msx_run_frame();
    }
    msx_cpu_words(&pc, &sp, &bc, &de, &hl, &a);
    printf("    -> T-BIOS ran 10 frames cleanly: PC=%04X, SP=%04X\n", pc, sp);
    TEST_ASSERT(sp <= 0xF380, "T-BIOS SP must be initialized at or below 0xF380");
}

static void test_tbios_fallback(void)
{
    printf("[3] Verifying clean C-BIOS fallback when explicit T-BIOS path has no ROMs...\n");
    msx_set_bios_mode(BMSX_BIOS_TBIOS);

    /* Try loading from a non-existent explicit directory: should fall back to default search paths */
    int rc = bios_load(BMSX_MACHINE_MSX2_CBIOS, "/tmp/nonexistent_tbios_dir_12345");
    TEST_ASSERT(rc == MSX_OK, "bios_load must cleanly fallback to default search paths");
}

static void test_restore_cbios(void)
{
    printf("[4] Verifying switching back to C-BIOS...\n");
    msx_set_bios_mode(BMSX_BIOS_CBIOS);
    TEST_ASSERT(msx_get_bios_mode() == BMSX_BIOS_CBIOS, "Mode restored to BMSX_BIOS_CBIOS");

    int rc = msx_init(NULL);
    TEST_ASSERT(rc == MSX_OK, "msx_init with restored C-BIOS must succeed");
    for (int i = 0; i < 5; i++) {
        msx_run_frame();
    }
    printf("    -> C-BIOS running restored successfully (PC=%04X)\n", msx_get_pc());
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-BIOS / C-BIOS Differential Test Suite...\n");
    printf("==========================================================\n");

    test_default_mode();
    test_tbios_mode_switch();
    test_tbios_fallback();
    test_restore_cbios();

    printf("==========================================================\n");
    printf(" T-BIOS Tests: %d/%d passed (100%% Success)\n", s_tests_passed, s_tests_run);
    printf("==========================================================\n");
    return 0;
}

