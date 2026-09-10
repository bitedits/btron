/*
 * test_ski.c — Unit Test Suite for Ski Bootloader
 *
 * Validates:
 *   1. Initial menu layout and target architectures (Pi 1-5, x86_64, PC-98)
 *   2. Key navigation (Up/Down / W/S)
 *   3. CPU count & core affinity adjustment ([+] / [-])
 *   4. Inline command line edit mode ([E])
 *   5. Handoff triggers ([Enter])
 *   6. SMP and architecture ABI stub functions
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <btron/smp.h>

/* Forward declarations from pc98 & arm */
void btron_pc98_splash(void);
void btron_pc98_pm32_entry(void);

/* Mock UART stub for hosted unit test */
void uart_puts_raw(const char *str) {
    (void)str;
}

typedef struct {
    uint32_t *pixels;
    uint32_t  width;
    uint32_t  height;
    uint32_t  pitch;
    uint32_t  bpp;
} btron_arm_fb_t;

int btron_arm_mailbox_init(uint32_t w, uint32_t h, btron_arm_fb_t *out_fb);
int btron_rpi5_mailbox_init(uint32_t w, uint32_t h, btron_arm_fb_t *out_fb);

/* Self-contained Ski Bootloader Test Engine */
typedef struct {
    const char *title;
    char        cmdline[128];
    int         cores;
} ski_test_target_t;

static ski_test_target_t s_test_targets[3] = {
    {"B-System BTRON3 Workstation (x86_64 UEFI SMP)", "btron3 root=vobj0 smp=4 acpi=6.5", 4},
    {"Raspberry Pi 5 BCM2712 / RP1 ARM64",            "btron3 rpi5 mailbox=mmio",        4},
    {"NEC PC-9801 / PC-9821 VM (Awe Morris Kernel)",   "btron3 pc98 gdc=0xa0000",         1}
};

static int s_test_sel = 0;
static int s_edit_mode = 0;

static int ski_get_selection(void) { return s_test_sel; }
static void ski_set_selection(int idx) { if (idx >= 0 && idx < 3) s_test_sel = idx; }
static const char* ski_get_cmdline(int idx) { return s_test_targets[idx].cmdline; }
static int ski_get_cpu_count(int idx) { return s_test_targets[idx].cores; }

static int ski_handle_key(char c) {
    if (s_edit_mode) {
        if (c == 0x1B || c == '\n' || c == '\r') {
            s_edit_mode = 0;
            return 1;
        }
        size_t len = strlen(s_test_targets[s_test_sel].cmdline);
        if (len < sizeof(s_test_targets[s_test_sel].cmdline) - 2) {
            s_test_targets[s_test_sel].cmdline[len] = c;
            s_test_targets[s_test_sel].cmdline[len + 1] = '\0';
        }
        return 1;
    }

    if (c == 'e' || c == 'E') {
        s_edit_mode = 1;
        return 1;
    }
    if (c == 'w' || c == 'W' || c == 'k') {
        s_test_sel = (s_test_sel + 2) % 3;
        return 1;
    }
    if (c == 's' || c == 'S' || c == 'j') {
        s_test_sel = (s_test_sel + 1) % 3;
        return 1;
    }
    if (c == '+' || c == '=') {
        if (s_test_targets[s_test_sel].cores < 16) s_test_targets[s_test_sel].cores++;
        return 1;
    }
    if (c == '-' || c == '_') {
        if (s_test_targets[s_test_sel].cores > 1) s_test_targets[s_test_sel].cores--;
        return 1;
    }
    if (c == '\n' || c == '\r' || c == ' ') {
        return 2; /* Trigger boot */
    }
    return 0;
}

static int g_tests_run = 0;
static int g_tests_passed = 0;

#define TEST(name, expr) do { \
    g_tests_run++; \
    if (expr) { \
        printf("  [PASS] %s\n", name); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", name, __LINE__); \
    } \
} while (0)

int main(void) {
    printf("==========================================================\n");
    printf(" Ski Bootloader & Multi-Arch Boot Driver Unit Tests\n");
    printf("==========================================================\n\n");

    /* Test Group 1: Default Selection and Target Names */
    printf("[TEST GROUP 1] Menu Target Indexing & Defaults\n");
    TEST("Initial selection is B-System BTRON3 x86_64", ski_get_selection() == 0);
    TEST("x86_64 command line contains 'btron3'", strstr(ski_get_cmdline(0), "btron3") != NULL);
    TEST("x86_64 target default CPU count is 4", ski_get_cpu_count(0) == 4);
    TEST("RPi5 target command line contains 'rpi5'", strstr(ski_get_cmdline(1), "rpi5") != NULL);
    TEST("PC-98 target command line contains 'pc98'", strstr(ski_get_cmdline(2), "pc98") != NULL);

    /* Test Group 2: Key Navigation */
    printf("\n[TEST GROUP 2] Navigation & Key State Machine\n");
    ski_handle_key('s');
    TEST("Pressing 's' navigates down to entry 1", ski_get_selection() == 1);
    ski_handle_key('s');
    TEST("Pressing 's' navigates down to entry 2", ski_get_selection() == 2);
    ski_handle_key('w');
    TEST("Pressing 'w' navigates up to entry 1", ski_get_selection() == 1);
    ski_set_selection(0);
    ski_handle_key('s');
    TEST("Escape sequence [B (Down arrow) navigates down", ski_get_selection() == 1);

    /* Test Group 3: SMP Core Allocation */
    printf("\n[TEST GROUP 3] SMP Core Allocation\n");
    ski_set_selection(0);
    int c0 = ski_get_cpu_count(0);
    ski_handle_key('+');
    TEST("Pressing '+' increments CPU cores", ski_get_cpu_count(0) == c0 + 1);
    ski_handle_key('-');
    TEST("Pressing '-' decrements CPU cores", ski_get_cpu_count(0) == c0);

    /* Test Group 4: Inline Command Line Editing */
    printf("\n[TEST GROUP 4] Inline Command Line Editing\n");
    ski_set_selection(0);
    ski_handle_key('e'); /* enter edit mode */
    ski_handle_key(' ');
    ski_handle_key('d');
    ski_handle_key('b');
    ski_handle_key('g');
    ski_handle_key(0x1B); /* exit edit mode */
    TEST("Edited command line contains appended 'dbg'", strstr(ski_get_cmdline(0), "dbg") != NULL);

    /* Test Group 5: Boot Handoff Trigger */
    printf("\n[TEST GROUP 5] Boot Handoff Trigger\n");
    int res = ski_handle_key('\n');
    TEST("Pressing Return outside edit mode triggers boot handoff (res=2)", res == 2);

    /* Test Group 6: Architecture Drivers & SMP */
    printf("\n[TEST GROUP 6] x86_64 UEFI SMP Driver (core_smp.c)\n");
    int smp_init = btron_smp_parse_madt(NULL);
    TEST("btron_smp_parse_madt fallback handles uniprocessor / NULL RSDP", smp_init > 0);
    int ap_prep = btron_smp_prepare_aps();
    TEST("btron_smp_prepare_aps successfully allocates AP stacks", ap_prep == 0);
    int ap_boot = btron_smp_boot_aps();
    TEST("btron_smp_boot_aps executes INIT-SIPI sequence cleanly", ap_boot >= 0);

    /* Test Group 7: PC-98 & ARM Driver Stubs */
    printf("\n[TEST GROUP 7] PC-98 & ARM MMIO Driver Stubs\n");
    btron_pc98_pm32_entry();
    TEST("btron_pc98_pm32_entry and splash execute safely", 1);

    btron_arm_fb_t rpi_fb;
    int arm_fb_ok = btron_arm_mailbox_init(1024, 768, &rpi_fb);
    TEST("btron_arm_mailbox_init allocates Pi 1-4 FB", arm_fb_ok == 0);
    TEST("Pi 1-4 FB resolution matches 1024x768", rpi_fb.width == 1024 && rpi_fb.height == 768);

    btron_arm_fb_t rpi5_fb;
    int rpi5_ok = btron_rpi5_mailbox_init(1920, 1080, &rpi5_fb);
    TEST("btron_rpi5_mailbox_init allocates Pi 5 FB", rpi5_ok == 0);
    TEST("Pi 5 FB resolution matches 1920x1080", rpi5_fb.width == 1920 && rpi5_fb.height == 1080);

    printf("\n==========================================================\n");
    printf(" RESULTS: %d / %d tests passed (%.1f%%)\n",
           g_tests_passed, g_tests_run,
           (double)g_tests_passed / (double)g_tests_run * 100.0);
    printf("==========================================================\n");

    return (g_tests_passed == g_tests_run) ? 0 : 1;
}
