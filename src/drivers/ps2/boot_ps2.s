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
    .set push
    .set mips3                /* wait is not a mips2 mnemonic; the CPU is a R5900 */
    wait
    .set pop
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


/* int32_t ps2_ee_syscall3(int32_t nr, int32_t a0, int32_t a1, int32_t a2)
 *
 * The EE kernel's own calling convention, which is the one the SDK's SYSCALL macro
 * emits: the number in $v1, the arguments shifted down one register on the way in, the
 * answer already in $v0 on the way out.  Three arguments are all the interrupt registry
 * asks for -- AddIntcHandler's source, callback and chain link, and EnableIntc's source.
 * The shifts go low to high because every destination is the next one's source. */
    .set push
    .set noreorder
    .globl  ps2_ee_syscall3
    .type   ps2_ee_syscall3, @function
ps2_ee_syscall3:
    move    $v1, $a0            /* the syscall number is not an argument */
    move    $a0, $a1
    move    $a1, $a2
    move    $a2, $a3
    syscall
    jr      $ra
    nop
    .set pop

/* void ps2_ee_enable_interrupts(void)
 * void ps2_ee_exit_handler(void)
 *
 * Raising the Emotion EE's global interrupt enable, by the two paths that need it: once
 * when the INPUT plane is armed, and once at the end of every period.  The ROM's kernel
 * enters its INTC dispatcher with the enable cleared and does not put it back, so a
 * handler that returns without doing so is one tick long -- which is exactly what this
 * port measured before the sequence below replaced the mnemonic it first shipped with.
 *
 * Why a read-modify-write of Status and not `ei`: the EE's enable is bit 16 (EIE), not
 * bit 0, and `ei` does set it on this silicon -- ps2sdk's EIntr() reads bit 16 back
 * through mfc0 to report the previous state, which is only meaningful if the instruction
 * touches that bit.  But the emulator this port's feedback loop runs on does not decode
 * it: 0x41606020, confirmed against GNU as at .set mips32r2, answers the console with
 * "Unknown R5900 COP0" and takes no effect, so a plane built on it works on a console and
 * dies in the loop that is supposed to tune it.  mfc0/ori/mtc0 is decodable on both and
 * sets the same bit, so the image carries one mechanism instead of two.
 *
 * The two `sync`s are different hazards and both are required.  The first is the store
 * buffer: the SDK records sync-before-enable as a workaround for an EE kernel bug and puts
 * it at the very end of every handler, because an uncommitted write under a live interrupt
 * loses the next edge.  The second is the CP0 write hazard -- Status changes what the
 * pipeline does, so nothing may be fetched in its shadow until it has taken effect.
 * clang emits neither after an inline-asm mtc0, which is why this pair lives in assembly
 * even though clang would assemble the instructions themselves at -march=mips2. */
    .set push
    .set mips3
    .set noreorder
    .globl  ps2_ee_enable_interrupts
    .type   ps2_ee_enable_interrupts, @function
ps2_ee_enable_interrupts:
    sync                        /* commit what is already in the store buffer */
    mfc0    $v0, $12            /* CP0.Status */
    li      $v1, 0x10000        /* EIE, the EE's interrupt enable -- bit 16, which is
                                 * not an ori immediate, so it is loaded rather than
                                 * masked. */
    or      $v0, $v0, $v1
    mtc0    $v0, $12
    sync                        /* let the new Status take effect */
    jr      $ra
    nop

/* The INTC handler's epilogue: the same enable, from inside the period. */
    .globl  ps2_ee_exit_handler
    .type   ps2_ee_exit_handler, @function
ps2_ee_exit_handler:
    sync
    mfc0    $v0, $12
    li      $v1, 0x10000
    or      $v0, $v0, $v1
    mtc0    $v0, $12
    sync
    jr      $ra
    nop
    .set pop
