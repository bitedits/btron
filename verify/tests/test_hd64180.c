/*
 * test_hd64180.c — Verification test suite for Hitachi HD64180 CPU core.
 * Tests MLT, IN0, OUT0, TST, TSTIO, SLP, block I/O, on-chip PRT timers,
 * and Z80 backward compatibility.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../../src/emulators/msx/hd64180.h"

static uint8_t s_ram[65536];
static uint8_t s_ext_ports[256];

static uint8_t mock_read(void *ud, uint16_t addr)
{
    (void)ud;
    return s_ram[addr];
}

static void mock_write(void *ud, uint16_t addr, uint8_t val)
{
    (void)ud;
    s_ram[addr] = val;
}

static uint8_t mock_port_in(z80 *cpu, uint8_t port)
{
    (void)cpu;
    return s_ext_ports[port];
}

static void mock_port_out(z80 *cpu, uint8_t port, uint8_t val)
{
    (void)cpu;
    s_ext_ports[port] = val;
}

static void setup_cpu(hd64180_t *cpu)
{
    memset(s_ram, 0, sizeof(s_ram));
    memset(s_ext_ports, 0, sizeof(s_ext_ports));
    hd64180_init(cpu);
    cpu->read_byte  = mock_read;
    cpu->write_byte = mock_write;
    cpu->port_in    = mock_port_in;
    cpu->port_out   = mock_port_out;
    cpu->userdata   = NULL;
    cpu->pc         = 0x1000;
}

/* ── Test 1: MLT instructions ────────────────────────────────────────────── */
static void test_mlt(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* 1. MLT BC (ED 4C): B=10, C=20 -> BC = 200 (0x00C8) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x4C;
    cpu.b = 10;
    cpu.c = 20;
    cpu.zf = 1; cpu.cf = 1; cpu.sf = 1; /* Flags should remain untouched */
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.b == 0);
    assert(cpu.c == 200);
    assert(cpu.zf == 1 && cpu.cf == 1 && cpu.sf == 1);

    /* 2. MLT BC edge case: 255 * 255 = 65025 (0xFE01) */
    cpu.pc = 0x1000;
    cpu.b = 255;
    cpu.c = 255;
    hd64180_step(&cpu);
    assert(cpu.b == 0xFE);
    assert(cpu.c == 0x01);

    /* 3. MLT DE (ED 5C): D=12, E=34 -> 408 (0x0198) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x5C;
    cpu.d = 12;
    cpu.e = 34;
    hd64180_step(&cpu);
    assert(cpu.d == 0x01);
    assert(cpu.e == 0x98);

    /* 4. MLT HL (ED 6C): H=50, L=60 -> 3000 (0x0BB8) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x6C;
    cpu.h = 50;
    cpu.l = 60;
    hd64180_step(&cpu);
    assert(cpu.h == 0x0B);
    assert(cpu.l == 0xB8);

    /* 5. MLT SP (ED 7C): SP=0x0405 (SP_H=4, SP_L=5) -> 20 (0x0014) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x7C;
    cpu.sp = 0x0405;
    hd64180_step(&cpu);
    assert(cpu.sp == 20);

    printf("  [PASS] HD64180 MLT instructions (BC, DE, HL, SP)\n");
}

/* ── Test 2: IN0 and OUT0 with Internal I/O ──────────────────────────────── */
static void test_in0_out0(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* Write RLDR0 using OUT0 (ED 39 0E: OUT0 (0x0E), A) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x39;
    s_ram[0x1002] = 0x0E;
    cpu.a = 0x56;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1003);
    assert((g_hd64180_state.rldr[0] & 0xFF) == 0x56);

    /* Write high byte: OUT0 (0x0F), A */
    cpu.pc = 0x1000;
    s_ram[0x1002] = 0x0F;
    cpu.a = 0x12;
    hd64180_step(&cpu);
    assert(g_hd64180_state.rldr[0] == 0x1256);

    /* Read back with IN0 B, (0x0E): ED 00 0E */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x00;
    s_ram[0x1002] = 0x0E;
    cpu.b = 0;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1003);
    assert(cpu.b == 0x56);
    assert(cpu.zf == 0);
    assert(cpu.hf == 0 && cpu.nf == 0);

    /* Test IN0 to external port (port 0x88 is outside internal 0x00..0x3F) */
    s_ext_ports[0x88] = 0x99;
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x10; /* IN0 D, (0x88) */
    s_ram[0x1002] = 0x88;
    cpu.d = 0;
    hd64180_step(&cpu);
    assert(cpu.d == 0x99);

    printf("  [PASS] HD64180 IN0 / OUT0 internal & external dispatch\n");
}

/* ── Test 3: TST Instructions ────────────────────────────────────────────── */
static void test_tst(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* 1. TST B (ED 04): A=0xAA, B=0x55 -> A & B = 0 -> Z=1, H=1, N=0, C=0, P=1 */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x04;
    cpu.a = 0xAA;
    cpu.b = 0x55;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.zf == 1);
    assert(cpu.hf == 1);
    assert(cpu.nf == 0);
    assert(cpu.cf == 0);
    assert(cpu.pf == 1); /* 0 bits -> even -> parity 1 */

    /* 2. TST B: A=0xFF, B=0x80 -> A & B = 0x80 -> S=1, Z=0, H=1, P=0 */
    cpu.pc = 0x1000;
    cpu.a = 0xFF;
    cpu.b = 0x80;
    hd64180_step(&cpu);
    assert(cpu.sf == 1);
    assert(cpu.zf == 0);
    assert(cpu.hf == 1);
    assert(cpu.pf == 0); /* 1 bit -> odd -> parity 0 */

    /* 3. TST (HL) (ED 34): (HL) = 0x0F, A = 0xFF -> res = 0x0F */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x34;
    cpu.h = 0x20; cpu.l = 0x50;
    s_ram[0x2050] = 0x0F;
    cpu.a = 0xFF;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.zf == 0);
    assert(cpu.hf == 1);
    assert(cpu.pf == 1); /* 4 bits -> even -> parity 1 */

    /* 4. TST imm (ED 64 3C): imm = 0x3C, A = 0xC3 -> res = 0 */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x64;
    s_ram[0x1002] = 0x3C;
    cpu.a = 0xC3;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1003);
    assert(cpu.zf == 1);

    /* 5. TSTIO imm (ED 74 55): port C = 0x0E (RLDR0), RLDR0 = 0x55, imm = 0x55 -> res = 0x55 */
    g_hd64180_state.rldr[0] = 0x55;
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x74;
    s_ram[0x1002] = 0x55;
    cpu.c = 0x0E;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1003);
    assert(cpu.zf == 0);
    assert(cpu.hf == 1);

    printf("  [PASS] HD64180 TST instructions (TST r, TST (HL), TST imm, TSTIO)\n");
}

/* ── Test 4: SLP and Block I/O ───────────────────────────────────────────── */
static void test_slp_and_block_io(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* 1. SLP (ED 76) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x76;
    cpu.halted = 0;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.halted == 1);

    /* 2. OOTIM (ED 83): out0(C), (HL); HL++; B-- */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0xED;
    s_ram[0x1001] = 0x83;
    cpu.c = 0x0E; /* internal port RLDR0L */
    cpu.h = 0x30; cpu.l = 0x00;
    s_ram[0x3000] = 0x77;
    cpu.b = 1;
    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.b == 0);
    assert(cpu.zf == 1);
    assert(cpu.nf == 1);
    assert(cpu.h == 0x30 && cpu.l == 0x01);
    assert((g_hd64180_state.rldr[0] & 0xFF) == 0x77);

    printf("  [PASS] HD64180 SLP & Block I/O instructions\n");
}

/* ── Test 5: PRT Timer Countdown and Interrupt ───────────────────────────── */
static void test_timer(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* Configure Timer 0: reload = 10, TMDR = 10, TIE0 = 1 */
    g_hd64180_state.rldr[0] = 10;
    g_hd64180_state.tmdr[0] = 10;
    g_hd64180_state.tcr = 0x01; /* TIE0 enabled */
    cpu.iff1 = 1;
    cpu.int_pending = 0;

    /* Advance 100 clock cycles = 5 timer ticks -> TMDR = 5 */
    hd64180_advance(&cpu, 100);
    assert(g_hd64180_state.tmdr[0] == 5);
    assert(cpu.int_pending == 0);

    /* Advance another 140 cycles = 7 timer ticks -> ticks 1..5 decrement to 0, tick 6 reloads to 10, tick 7 decrements to 9 */
    hd64180_advance(&cpu, 140);
    assert(g_hd64180_state.tmdr[0] == 9);
    assert((g_hd64180_state.tcr & 0x10) != 0); /* TIF0 bit set */
    assert(cpu.int_pending == 1);              /* Interrupt requested */

    printf("  [PASS] HD64180 PRT programmable reload timer & interrupt trigger\n");
}

/* ── Test 6: Standard Z80 backward compatibility ─────────────────────────── */
static void test_z80_compat(void)
{
    hd64180_t cpu;
    setup_cpu(&cpu);

    /* LD A, 0x42 (3E 42); ADD A, 0x10 (C6 10) */
    cpu.pc = 0x1000;
    s_ram[0x1000] = 0x3E;
    s_ram[0x1001] = 0x42;
    s_ram[0x1002] = 0xC6;
    s_ram[0x1003] = 0x10;

    hd64180_step(&cpu);
    assert(cpu.pc == 0x1002);
    assert(cpu.a == 0x42);

    hd64180_step(&cpu);
    assert(cpu.pc == 0x1004);
    assert(cpu.a == 0x52);

    printf("  [PASS] Z80 standard instruction passthrough without quality degradation\n");
}

int main(void)
{
    printf("==========================================================\n");
    printf(" Hitachi HD64180 CPU Verification Suite for B-MSX\n");
    printf("==========================================================\n");

    test_mlt();
    test_in0_out0();
    test_tst();
    test_slp_and_block_io();
    test_timer();
    test_z80_compat();

    printf("==========================================================\n");
    printf(" ALL 6/6 HD64180 CPU TEST SUITES PASSED CLEANLY.\n");
    printf("==========================================================\n");
    return 0;
}
