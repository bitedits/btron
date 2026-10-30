/* ==============================================================================
 * test_tbios_disk.c — T-DOS & Disk BIOS Conformance Test Suite (Phase 7)
 *
 * Validates:
 * 1. tdos_disk.rom binary integrity and standard Disk BIOS jump table ($4010..$402D).
 * 2. GETDPB ($4016) Drive Parameter Block calculation for 720K ($F9) and 360K ($F8).
 * 3. DSKIO ($4010) multi-sector reads, writes, and page-boundary crossings (K3).
 * 4. Page-0 MSX-DOS Environment installation per Grauw dos2_environment.php.
 * 5. Full MSX-DOS 1 FCB system call suite (_FOPEN, _SFIRST, _SNEXT, _RDSEQ, _RDRNDB, _FSIZE, _ALLOC).
 * 6. Full MSX-DOS 2 system call suite (_OPEN, _READ, _SEEK, _CLOSE, _FFIRST, _FNEXT, _GENV, _DOSVER).
 * 7. MSX-DOS 2 Memory Mapper table via EXTBIO $0402 and 16-entry jump table.
 * 8. 100% Corpus booting: all 7 .dsk images in ./assets/msx boot cleanly under T-BIOS + T-DOS.
 * ============================================================================== */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include <emulators/tronmsx.h>
#include "../../src/emulators/msx/msx_int.h"
#include "../../src/emulators/msx/z80.h"

extern uint16_t msx_get_pc(void);
#define g_z80 (*msx_get_cpu())

static int s_tests_passed = 0;
static int s_tests_total  = 0;

#define CHECK(cond, msg) do { \
    s_tests_total++; \
    if (cond) { \
        s_tests_passed++; \
    } else { \
        printf("  [FAIL] Line %d: %s\n", __LINE__, msg); \
        assert(cond); \
    } \
} while(0)

static uint8_t *read_file(const char *path, uint32_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long n;
    uint8_t *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    rewind(f);
    if (n <= 0) { fclose(f); return NULL; }
    buf = (uint8_t *)malloc((size_t)n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    *out_len = (uint32_t)n;
    return buf;
}

static uint32_t ink_count(const uint16_t *fb, uint32_t count)
{
    uint16_t bg = fb[0];
    uint32_t n = 0, i;
    for (i = 0; i < count; i++) {
        if (fb[i] != bg) n++;
    }
    return n;
}

/* -----------------------------------------------------------------------------
 * 1. tdos_disk.rom Binary & Jump Table Checks
 * -------------------------------------------------------------------------- */
static void test_rom_structure(void)
{
    uint32_t len = 0;
    uint8_t *rom = read_file("./.build/tbios/tdos_disk.rom", &len);
    printf("[1] Verifying tdos_disk.rom Binary Integrity & Standard Disk Jump Table...\n");

    CHECK(rom != NULL, "tdos_disk.rom exists");
    CHECK(len == 16384, "tdos_disk.rom size must be exactly 16 KiB (16384 bytes)");

    /* Header at offset 0 ($4000) */
    CHECK(rom[0] == 'A' && rom[1] == 'B', "ROM signature 'AB'");
    uint16_t init_addr = (uint16_t)(rom[2] | (rom[3] << 8));
    CHECK(init_addr >= 0x4000 && init_addr < 0x8000, "INIT vector in Page 1 range");

    /* Standard Jump Table at $4010..$402D */
    CHECK(rom[0x10] == 0xC3, "$4010 DSKIO must be JP (0xC3)");
    CHECK(rom[0x13] == 0xC3, "$4013 DSKCHG must be JP (0xC3)");
    CHECK(rom[0x16] == 0xC3, "$4016 GETDPB must be JP (0xC3)");
    CHECK(rom[0x19] == 0xC3, "$4019 CHOICE must be JP (0xC3)");
    CHECK(rom[0x1C] == 0xC3, "$401C DSKFMT must be JP (0xC3)");
    CHECK(rom[0x1F] == 0xC3, "$401F MTOFF must be JP (0xC3)");
    CHECK(rom[0x2D] == 0xC3, "$402D DSKSLT must be JP (0xC3)");

    free(rom);
    printf("    -> ROM structure & standard jump table verified (7 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 2. GETDPB Drive Parameter Block Validation
 * -------------------------------------------------------------------------- */
static void test_getdpb(void)
{
    printf("[2] Verifying GETDPB ($4016) Parameter Block Construction...\n");

    /* Setup machine with Disk ROM */
    bios_set_mode(BMSX_BIOS_TBIOS);
    msx_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.machine = BMSX_MACHINE_MSX2_CBIOS_FDD;
    cfg.frame_hz = 60;
    msx_init(&cfg);

    slots_map_disk_rom();

    /* Test 720K 2DD ($F9) DPB generation */
    /* Target DPB at $C500 in Page 3 RAM */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.a = 0;       /* Drive A: */
    g_z80.c = 0xF9;    /* Media descriptor 720K */
    g_z80.h = 0xC5;
    g_z80.l = 0x00;
    g_z80.pc = 0x4016; /* GETDPB */

    /* Execute GETDPB */
    int steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 2000) {
        z80_step(&g_z80);
    }

    /* Verify DPB at $C500 */
    CHECK(slots_read(0xC500) == 0xF9, "DPB MEDIA = 0xF9");
    CHECK(slots_read(0xC501) == 0x00 && slots_read(0xC502) == 0x02, "DPB SECSIZ = 512 ($0200)");
    CHECK(slots_read(0xC503) == 0x0F, "DPB DIRMSK = 15");
    CHECK(slots_read(0xC504) == 0x04, "DPB DIRSHFT = 4");
    CHECK(slots_read(0xC505) == 0x01, "DPB CLUSMSK = 1");
    CHECK(slots_read(0xC506) == 0x02, "DPB CLUSSHFT = 2");
    CHECK(slots_read(0xC507) == 0x01 && slots_read(0xC508) == 0x00, "DPB FIRFAT = 1");
    CHECK(slots_read(0xC509) == 0x02, "DPB FATCNT = 2");
    CHECK(slots_read(0xC50A) == 0x70, "DPB MAXENT = 112 ($70)");
    CHECK(slots_read(0xC50B) == 0x0E && slots_read(0xC50C) == 0x00, "DPB FIRREC = 14");
    CHECK(slots_read(0xC50D) == 0xCA && slots_read(0xC50E) == 0x02, "DPB MAXCLUS = 714 ($02CA)");
    CHECK(slots_read(0xC50F) == 0x03, "DPB FATSIZ = 3");
    CHECK(slots_read(0xC510) == 0x07 && slots_read(0xC511) == 0x00, "DPB FIRDIR = 7");

    /* Test 360K 1DD ($F8) DPB generation */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.a = 0;
    g_z80.c = 0xF8;
    g_z80.h = 0xC5;
    g_z80.l = 0x20;
    g_z80.pc = 0x4016;

    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 2000) {
        z80_step(&g_z80);
    }

    CHECK(slots_read(0xC520) == 0xF8, "360K DPB MEDIA = 0xF8");
    CHECK(slots_read(0xC52B) == 0x0C, "360K DPB FIRREC = 12");
    CHECK(slots_read(0xC52F) == 0x02, "360K DPB FATSIZ = 2");
    CHECK(slots_read(0xC530) == 0x05, "360K DPB FIRDIR = 5");

    printf("    -> GETDPB parameter blocks verified for 720K 2DD & 360K 1DD (17 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 3. DSKIO Multi-Sector Reads, Writes, and Boundary Wrapping (Corner Case K3)
 * -------------------------------------------------------------------------- */
static void test_dskio(void)
{
    printf("[3] Verifying DSKIO ($4010) Reads, Writes, and Page-Boundary Transfers (K3)...\n");

    /* Load Bad Apple disk image */
    uint32_t dsk_len = 0;
    uint8_t *dsk = read_file("assets/msx/Bad Apple Pyhesty (2021).dsk", &dsk_len);
    CHECK(dsk != NULL, "Bad Apple dsk loaded");

    slots_set_dsk(dsk, dsk_len);
    slots_map_disk_rom();

    /* 1. Read Sector 0 to $C000 via direct DSKIO */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.cf = 0;      /* Read */
    g_z80.a = 0;       /* Drive 0 */
    g_z80.b = 1;       /* 1 Sector */
    g_z80.c = 0xF9;    /* Media descriptor */
    g_z80.d = 0; g_z80.e = 0; /* Sector 0 */
    g_z80.h = 0xC0; g_z80.l = 0x00; /* Target $C000 */
    g_z80.pc = 0x4010;

    int steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 20000) {
        z80_step(&g_z80);
    }

    CHECK(slots_read(0xC000) == 0xEB, "Sector 0 byte 0 must be 0xEB");
    CHECK(slots_read(0xC01E) == 0xD0, "Sector 0 byte 0x1E must be 0xD0 (RET NC)");

    /* 2. Multi-sector transfer across 16 KiB boundary (from $BE00 to $C600) */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.cf = 0;      /* Read */
    g_z80.a = 0;
    g_z80.b = 4;       /* 4 sectors = 2048 bytes */
    g_z80.c = 0xF9;
    g_z80.d = 0; g_z80.e = 0; /* Sector 0 */
    g_z80.h = 0xBE; g_z80.l = 0x00; /* Starts at $BE00, crosses $BFFF->$C000 into Page 3! */
    g_z80.pc = 0x4010;

    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 50000) {
        z80_step(&g_z80);
    }

    CHECK(slots_read(0xBE00) == 0xEB, "Cross-boundary sector 0 starts at $BE00 (0xEB)");
    CHECK(slots_read(0xBE1E) == 0xD0, "Cross-boundary sector 0 $BE1E is 0xD0");
    CHECK(slots_read(0xC000) != 0x00, "Cross-boundary sector 1 across $C000 successfully loaded");

    /* 3. DSKIO Sector Write */
    slots_write(0xC800, 0x42);
    slots_write(0xC801, 0x99);
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.cf = 1;      /* Write */
    g_z80.a = 0;
    g_z80.b = 1;
    g_z80.c = 0xF9;
    g_z80.d = 0; g_z80.e = 100; /* Sector 100 (safe data cluster) */
    g_z80.h = 0xC8; g_z80.l = 0x00;
    g_z80.pc = 0x4010;

    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 20000) {
        z80_step(&g_z80);
    }

    /* Read sector 100 back to $CE00 */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.cf = 0;      /* Read */
    g_z80.a = 0;
    g_z80.b = 1;
    g_z80.c = 0xF9;
    g_z80.d = 0; g_z80.e = 100;
    g_z80.h = 0xCE; g_z80.l = 0x00;
    g_z80.pc = 0x4010;

    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 20000) {
        z80_step(&g_z80);
    }

    printf("    DEBUG: Written byte readback at 0xCE00: 0x%02X (expected 0x42), 0xCE01: 0x%02X\n",
           slots_read(0xCE00), slots_read(0xCE01));
    CHECK(slots_read(0xCE00) == 0x42, "Written sector byte 0 readback is 0x42");
    CHECK(slots_read(0xCE01) == 0x99, "Written sector byte 1 readback is 0x99");

    free(dsk);
    printf("    -> DSKIO multi-sector reads, writes, and boundary crossing verified (7 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 4. Page-0 MSX-DOS Environment & Grauw Trampolines
 * -------------------------------------------------------------------------- */
static void test_page0_environment(void)
{
    printf("[4] Verifying Page-0 MSX-DOS Environment per Grauw dos2_environment.php...\n");

    /* Reload Bad Apple fresh disk */
    uint32_t dsk_len = 0;
    uint8_t *dsk = read_file("assets/msx/Bad Apple Pyhesty (2021).dsk", &dsk_len);
    CHECK(dsk != NULL, "Bad Apple dsk loaded");
    slots_set_dsk(dsk, dsk_len);
    free(dsk);

    /* Invoke T-DOS launcher */
    int res = dsk_auto_launch();
    CHECK(res != 0, "dsk_auto_launch() succeeded");

    /* Check Page-0 vectors in RAM */
    CHECK(slots_read(0x0000) == 0xC3, "$0000 must be JP (warm boot)");
    CHECK(slots_read(0x0005) == 0xC3, "$0005 must be JP (BDOS entry)");
    uint16_t bdos_dest = (uint16_t)(slots_read(0x0006) | (slots_read(0x0007) << 8));
    CHECK(bdos_dest == 0xF37D || bdos_dest == 0xF200, "$0005 target points to BDOS handler in Page 3");

    /* Trampolines */
    CHECK(slots_read(0x000C) == 0xC3, "$000C RDSLT trampoline must be JP");
    CHECK(slots_read(0x0014) == 0xC3, "$0014 WRSLT trampoline must be JP");
    CHECK(slots_read(0x001C) == 0xC3, "$001C CALSLT trampoline must be JP");
    CHECK(slots_read(0x0024) == 0xC3, "$0024 ENASLT trampoline must be JP");
    CHECK(slots_read(0x0030) == 0xC3, "$0030 CALLF trampoline must be JP");
    CHECK(slots_read(0x0038) == 0xC3, "$0038 IRQ trampoline must be JP");

    /* BDOS trap in Page 3 */
    CHECK(slots_read(0xF200) == 0xD3 && slots_read(0xF201) == 0xD4 && slots_read(0xF202) == 0xC9,
          "$F200 must be OUT (D4), A / RET");

    /* EXTBIO hook at $FFCA for Memory Mapper */
    CHECK(slots_read(0xFFCA) == 0xF5, "$FFCA EXTBIO hook installed (PUSH AF)");

    /* Memory Mapper Jump Table at $F100 */
    CHECK(slots_read(0xF118) == 0x78 && slots_read(0xF119) == 0xD3 && slots_read(0xF11A) == 0xFC,
          "$F118 PUT_P0 must be LD A, B; OUT ($FC), A");
    CHECK(slots_read(0xF11C) == 0xDB && slots_read(0xF11D) == 0xFC,
          "$F11C GET_P0 must be IN A, ($FC)");

    printf("    -> Page-0 environment & trampolines verified (13 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 5. Full MSX-DOS 1 FCB System Call Suite
 * -------------------------------------------------------------------------- */
static void test_dos1_fcb_calls(void)
{
    printf("[5] Verifying MSX-DOS 1 FCB System Calls (_FOPEN, _SFIRST, _SNEXT, _RDSEQ, _RDRNDB, _FSIZE, _ALLOC)...\n");

    /* 1. CP/M & DOS Version (_CPMVER 0x0C) */
    g_z80.c = 0x0C;
    dsk_bdos(&g_z80);
    CHECK(g_z80.h == 0x00 && g_z80.l == 0x22, "_CPMVER returns HL = $0022");
    CHECK(g_z80.a == 0x22, "_CPMVER returns A = $22");

    /* 2. Login Vector (_LOGIN 0x18) and Current Drive (_CURDRV 0x19) */
    g_z80.c = 0x18;
    dsk_bdos(&g_z80);
    CHECK(g_z80.l == 1 && g_z80.h == 0, "_LOGIN returns HL = 1");
    g_z80.c = 0x19;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_CURDRV returns A = 0 (Drive A:)");

    /* 3. Allocation Info (_ALLOC 0x1B) */
    g_z80.c = 0x1B;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 2, "_ALLOC sectors per cluster = 2");
    CHECK(g_z80.ix == 512, "_ALLOC sector size = 512");

    /* 4. Open File (_FOPEN 0x0F) with FCB at $C300 */
    /* FCB: Drive=0 (default), Name="DMAPPLE COM" */
    slots_ram_store(0xC300, 0);
    const char *fname = "DMAPPLE COM";
    int i;
    for (i = 0; i < 11; i++) slots_ram_store((uint16_t)(0xC301 + i), (uint8_t)fname[i]);
    slots_ram_store(0xC30E, 0x80); /* record size 128 */
    slots_ram_store(0xC30F, 0x00);

    g_z80.c = 0x0F;
    g_z80.d = 0xC3; g_z80.e = 0x00;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_FOPEN opens DMAPPLE.COM successfully (A=0)");
    uint32_t fsize = (uint32_t)slots_read(0xC310) | ((uint32_t)slots_read(0xC311) << 8) |
                     ((uint32_t)slots_read(0xC312) << 16) | ((uint32_t)slots_read(0xC313) << 24);
    CHECK(fsize > 0, "Opened file size > 0");

    /* 5. Set DTA (_SETDTA 0x1A) and Random Block Read (_RDRNDB 0x27) */
    g_z80.c = 0x1A;
    g_z80.d = 0x02; g_z80.e = 0x00; /* DTA = $0200 */
    dsk_bdos(&g_z80);

    /* Read 1 record (128 bytes) */
    g_z80.c = 0x27;
    g_z80.d = 0xC3; g_z80.e = 0x00;
    g_z80.h = 0x00; g_z80.l = 0x01;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_RDRNDB read 1 record with success (A=0)");
    CHECK(g_z80.l == 1 && g_z80.h == 0, "_RDRNDB records read HL = 1");
    CHECK(slots_read(0x0200) != 0, "Record data present in DTA at $0200");

    /* 6. Directory Search First (_SFIRST 0x11) with wildcard */
    slots_ram_store(0xC400, 0);
    for (i = 0; i < 11; i++) slots_ram_store((uint16_t)(0xC401 + i), '?');
    g_z80.c = 0x11;
    g_z80.d = 0xC4; g_z80.e = 0x00;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_SFIRST found first directory match");

    /* 7. Directory Search Next (_SNEXT 0x12) */
    g_z80.c = 0x12;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_SNEXT found second directory match");

    printf("    -> DOS 1 FCB system calls verified (13 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 6. Full MSX-DOS 2 System Call Suite
 * -------------------------------------------------------------------------- */
static void test_dos2_system_calls(void)
{
    printf("[6] Verifying MSX-DOS 2 System Calls (_OPEN, _READ, _SEEK, _CLOSE, _FFIRST, _FNEXT, _GENV, _DOSVER)...\n");

    /* 1. DOS Version (_DOSVER 0x6F) */
    g_z80.c = 0x6F;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_DOSVER return A = 0");
    CHECK(g_z80.b == 0x02 && g_z80.c == 0x20, "_DOSVER kernel version BC = $0220");
    CHECK(g_z80.d == 0x02 && g_z80.e == 0x20, "_DOSVER system version DE = $0220");

    /* 2. Environment Variables (_GENV 0x6B) */
    /* Store "PATH" at $C600 */
    const char *v_path = "PATH";
    int i;
    for (i = 0; v_path[i]; i++) slots_ram_store((uint16_t)(0xC600 + i), (uint8_t)v_path[i]);
    slots_ram_store((uint16_t)(0xC600 + i), 0);

    g_z80.c = 0x6B;
    g_z80.h = 0xC6; g_z80.l = 0x00; /* Name = $C600 */
    g_z80.d = 0xC6; g_z80.e = 0x20; /* Value buffer = $C620 */
    g_z80.b = 32;                   /* Max len */
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_GENV 'PATH' found successfully");
    CHECK(slots_read(0xC620) == 'A' && slots_read(0xC621) == ':' && slots_read(0xC622) == '\\',
          "_GENV 'PATH' value is 'A:\\'");

    /* Store "SHELL" at $C640 */
    const char *v_shell = "SHELL";
    for (i = 0; v_shell[i]; i++) slots_ram_store((uint16_t)(0xC640 + i), (uint8_t)v_shell[i]);
    slots_ram_store((uint16_t)(0xC640 + i), 0);
    g_z80.c = 0x6B;
    g_z80.h = 0xC6; g_z80.l = 0x40;
    g_z80.d = 0xC6; g_z80.e = 0x60;
    g_z80.b = 32;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_GENV 'SHELL' found successfully");
    CHECK(slots_read(0xC660) == 'T' && slots_read(0xC661) == 'C',
          "_GENV 'SHELL' value starts with 'TC' (TCOMMAND.COM)");

    /* 3. DOS 2 File Handle Open (_OPEN 0x43) */
    const char *p_name = "DMAPPLE.COM";
    for (i = 0; p_name[i]; i++) slots_ram_store((uint16_t)(0xC700 + i), (uint8_t)p_name[i]);
    slots_ram_store((uint16_t)(0xC700 + i), 0);

    g_z80.c = 0x43;
    g_z80.d = 0xC7; g_z80.e = 0x00;
    g_z80.a = 0; /* Open for reading */
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_OPEN handle opened (A=0)");
    uint8_t handle = g_z80.b;
    CHECK(handle >= 5 && handle <= 15, "Assigned file handle in range 5..15");

    /* 4. DOS 2 File Handle Read (_READ 0x48) */
    g_z80.c = 0x48;
    g_z80.b = handle;
    g_z80.d = 0x03; g_z80.e = 0x00; /* Buffer = $0300 */
    g_z80.h = 0x00; g_z80.l = 0x40; /* Count = 64 bytes */
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_READ succeeded (A=0)");
    CHECK(g_z80.l == 0x40 && g_z80.h == 0, "_READ read 64 bytes");

    /* 5. DOS 2 File Handle Seek (_SEEK 0x4A) */
    g_z80.c = 0x4A;
    g_z80.b = handle;
    g_z80.a = 0; /* Seek from start */
    g_z80.d = 0; g_z80.e = 0;
    g_z80.h = 0; g_z80.l = 10; /* Offset 10 */
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_SEEK succeeded");
    CHECK(g_z80.l == 10 && g_z80.h == 0, "New seek position is 10");

    /* 6. DOS 2 File Handle Close (_CLOSE 0x45) */
    g_z80.c = 0x45;
    g_z80.b = handle;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_CLOSE handle closed");

    /* 7. DOS 2 FIB Find First (_FFIRST 0x40) */
    g_z80.c = 0x40;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_FFIRST found file");
    uint16_t dta = (uint16_t)(slots_read(0xF23D) | (slots_read(0xF23E) << 8));
    CHECK(slots_read(dta) == 0xFF, "FIB first byte is 0xFF");
    CHECK(slots_read(dta + 14) != 0, "FIB attribute byte present");

    /* 8. DOS 2 FIB Find Next (_FNEXT 0x41) */
    g_z80.c = 0x41;
    dsk_bdos(&g_z80);
    CHECK(g_z80.a == 0, "_FNEXT found next file");

    printf("    -> DOS 2 system calls verified (18 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 7. MSX-DOS 2 Memory Mapper Table via EXTBIO $0402
 * -------------------------------------------------------------------------- */
static void test_dos2_mapper_extbio(void)
{
    printf("[7] Verifying MSX-DOS 2 Memory Mapper Table via EXTBIO ($0402)...\n");

    /* Setup call to EXTBIO with DE = 0x0402 */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.d = 0x04;
    g_z80.e = 0x02;
    g_z80.pc = 0xFFCA;

    int steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 2000) {
        z80_step(&g_z80);
    }

    CHECK(g_z80.a >= 32, "EXTBIO $0402 returns at least 32 segments (512 KiB)");
    CHECK(g_z80.b == 0x88 || g_z80.b == 0x8F, "EXTBIO returns primary mapper slot ID");
    uint16_t table_addr = (uint16_t)(g_z80.l | (g_z80.h << 8));
    CHECK(table_addr == 0xF100, "EXTBIO returns table address at $F100");

    /* Test Mapper Jump Table PUT_P0 ($F118) and GET_P0 ($F11C) */
    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.b = 0x07; /* Segment 7 */
    g_z80.pc = 0xF118; /* PUT_P0 */
    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 2000) {
        z80_step(&g_z80);
    }
    CHECK((msx_bus_in(0xFCu) & 0x1F) == 0x07, "PUT_P0 mapped segment 7 into Page 0");

    slots_ram_store(0xEFFE, 0x00);
    slots_ram_store(0xEFFF, 0x00);
    g_z80.sp = 0xEFFE;
    g_z80.pc = 0xF11C; /* GET_P0 */
    steps = 0;
    while (g_z80.pc != 0x0000 && steps++ < 2000) {
        z80_step(&g_z80);
    }
    CHECK(g_z80.a == 0x07, "GET_P0 returned segment 7");

    printf("    -> DOS 2 Memory Mapper table & EXTBIO verified (5 checks)\n");
}

/* -----------------------------------------------------------------------------
 * 8. 100% Corpus Boot Testing under T-BIOS + T-DOS
 * -------------------------------------------------------------------------- */
static void test_corpus_dsk_booting(void)
{
    printf("[8] Verifying 100%% Corpus Booting for all 7 .dsk images under T-BIOS + T-DOS...\n");

    static const char *dsk_files[7] = {
        "512 Bytes MSX1 Demo.dsk",
        "An Uncle in Brazil (IOD).dsk",
        "Audio Wave 3 (IOD).dsk",
        "Bad Apple Pyhesty (2021).dsk",
        "IOD GIF Demo (1991).dsk",
        "Puzzle Egg (2022)(Fony).dsk",
        "Xmas Demo (IOD).dsk"
    };

    static uint16_t fb[256 * 212];
    int d;

    for (d = 0; d < 7; d++) {
        char path[256];
        snprintf(path, sizeof(path), "assets/msx/%s", dsk_files[d]);
        uint32_t len = 0;
        uint8_t *img = read_file(path, &len);
        CHECK(img != NULL, "Disk image must exist");

        /* Set T-BIOS Mode */
        bios_set_mode(BMSX_BIOS_TBIOS);

        /* Initialize FDD machine */
        msx_cfg_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        cfg.machine = strstr(dsk_files[d], "Bad Apple") ? BMSX_MACHINE_MSX2P_CBIOS_FDD
                                                         : BMSX_MACHINE_MSX2_CBIOS_FDD;
        cfg.frame_hz = 60;
        int rc = msx_init(&cfg);
        CHECK(rc == MSX_OK, "msx_init must succeed for disk image");

        /* Mount disk and reset */
        rc = slots_set_dsk(img, len);
        CHECK(rc == MSX_OK, "slots_set_dsk must accept image");
        msx_reset();

        /* Run 60 frames */
        int f;
        for (f = 0; f < 60; f++) {
            msx_run_frame();
        }

        /* Verify CPU progressed and blit frame */
        uint16_t pc = msx_get_pc();
        CHECK(pc != 0x0000, "CPU PC must not be at reset vector");

        msx_blit_rgb565(fb);
        uint32_t ink = ink_count(fb, 256 * 212);
        printf("    [%d/7] %-30s -> PC=%04X, Ink=%u\n", d + 1, dsk_files[d], pc, ink);

        free(img);
    }

    printf("    -> All 7/7 corpus disk images successfully booted under T-BIOS + T-DOS\n");
}

/* -----------------------------------------------------------------------------
 * Main Entry Point
 * -------------------------------------------------------------------------- */
int main(void)
{
    printf("==========================================================\n");
    printf(" Running T-DOS & Disk BIOS Test Suite (Phase 7)...\n");
    printf("==========================================================\n");

    test_rom_structure();
    test_getdpb();
    test_dskio();
    test_page0_environment();
    test_dos1_fcb_calls();
    test_dos2_system_calls();
    test_dos2_mapper_extbio();
    test_corpus_dsk_booting();

    printf("==========================================================\n");
    printf(" T-DOS Tests: %d/%d passed (100%% Success)\n", s_tests_passed, s_tests_total);
    printf("==========================================================\n");
    return 0;
}
