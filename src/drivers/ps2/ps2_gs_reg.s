/*
 * ps2_gs_reg.s — the PS2 port's only use of 64-bit GPR instructions
 *
 * The Graphics Synthesizer latches a privileged register on ONE 64-bit write.
 * Two 32-bit stores to the same register address are two writes: the first half
 * reaches the latch as a register value with the other half zero, and PMODE,
 * SMODE2, DISPFB and DISPLAY never settle.  So every GS register access has to
 * be an `ld`/`sd`, and `sd` needs a 64-bit-GPR MIPS.
 *
 * The port's C is not built for one.  A 64-bit-GPR MIPS under O32 lays its
 * vararg save area out in 8-byte slots while va_arg still walks four bytes at a
 * time, and it passes fixed arguments 5..8 in $t0..$t3 instead of on the stack;
 * mixing it into a link with 32-bit-GPR objects therefore corrupts both the
 * console lines and any call with five or more arguments, silently.  All PS2 C
 * is built -march=mips2 (32-bit GPRs, O32 -- and mips2 is a subset of MIPS III,
 * so nothing illegal for the Emotion Engine is emitted, unlike -march=mips32r2's
 * ext/ins/clz).  The 64-bit access lives here instead, reached from C through
 * three 32-bit arguments and one, which every MIPS ISA agrees on.
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

.set noreorder
.set noat
.set mips3

/* void ps2_gs_poke64(uint32_t addr, uint32_t lo, uint32_t hi)
 *   $a0 = register address, $a1 = bits 0..31, $a2 = bits 32..63
 *
 * The two halves arrive from 32-bit operations, so their 64-bit registers carry
 * a sign extension.  dsrl32 of dsll32 clears lo's upper half, dsll32 pushes hi
 * up and out of its own sign bits, and the two disjoint fields join with daddu. */
.global ps2_gs_poke64
ps2_gs_poke64:
    dsll32  $t0, $a1, 0
    dsrl32  $t0, $t0, 0     /* $t0 = zero-extended lo   */
    dsll32  $t1, $a2, 0     /* $t1 = hi << 32           */
    daddu   $t0, $t0, $t1
    sd      $t0, 0($a0)
    jr      $ra
    nop

/* uint64_t ps2_gs_peek64(uint32_t addr) — $a0 = register address
 *
 * Returned as $v0 = low half, $v1 = high half, which is both the O32 64-bit
 * return convention a 32-bit-GPR caller reads and the whole value in $v0 that a
 * 64-bit-GPR caller reads. */
.global ps2_gs_peek64
ps2_gs_peek64:
    ld      $v0, 0($a0)
    dsrl    $v1, $v0, 32
    jr      $ra
    nop
