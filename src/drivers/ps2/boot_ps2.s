/*
 * boot_ps2.s — Sony PlayStation 2 Emotion Engine (R5900) Startup Code
 *
 * Cleanroom B-System / BTRON3 3.20 RTOS Kernel Entry Point for PS2
 *
 * Target: Emotion Engine MIPS R5900 (MIPS-III Little-Endian)
 * Execution Environment: PCSX2 / Real Hardware
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

.set noreorder
.set noat

.global _start
.global ps2_delay_cycles
.global ps2_halt

.extern ps2_kernel_main
.extern __bss_start
.extern _end
.extern _gp

/* Top of 32MB EE RDRAM minus 64KB headroom for stack */
#define PS2_STACK_TOP       0x01FF0000

.section .text.startup, "ax", @progbits
.align 4

_start:
    /* Initialize Global Pointer ($gp) and Stack Pointer ($sp) */
    la      $gp, _gp
    lui     $sp, 0x01FF
    move    $fp, $sp

    /* Clear .bss section */
    la      $t0, __bss_start
    la      $t1, _end
    beq     $t0, $t1, 2f
    nop

1:
    sw      $zero, 0($t0)
    addiu   $t0, $t0, 4
    sltu    $t2, $t0, $t1
    bnez    $t2, 1b
    nop

2:
    /* Call C kernel entry point */
    jal     ps2_kernel_main
    nop

/* Infinite halt loop if kernel ever returns */
ps2_halt:
3:
    wait
    b       3b
    nop

/* ps2_delay_cycles(uint32_t count) */
ps2_delay_cycles:
4:
    bgtz    $a0, 4b
    subu    $a0, $a0, 1
    jr      $ra
    nop


/* uint32_t ps2_count_read(void)
 * CP0 Count: increments at half the core clock, so on a 294.912 MHz Emotion
 * Engine one tick is ~6.78 ns and 147 ticks is ~1 us.  This is the only wall
 * clock the port has before the IOP alarm handler exists, so every timed wait
 * and every cost measurement goes through it. */
.global ps2_count_read
ps2_count_read:
    mfc0    $v0, $9, 0
    jr      $ra
    nop


/* ps2_set_gs_crt(int16_t interlace, int16_t pal_ntsc, int16_t field) */
.global ps2_set_gs_crt
ps2_set_gs_crt:
    li      $v1, 0x02           /* SetGsCrt syscall */
    syscall
    jr      $ra
    nop

/* ps2_gs_put_imr(uint32_t mask) */
.global ps2_gs_put_imr
ps2_gs_put_imr:
    li      $v1, 0x71           /* GsPutIMR syscall */
    syscall
    jr      $ra
    nop
