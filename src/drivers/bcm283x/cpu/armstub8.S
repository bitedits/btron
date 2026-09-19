/* BTRON3 Pi 400 — EL3 armstub (armstub8.bin), loaded & run by start4.elf at
 * Secure EL3 BEFORE the kernel.  Linked at 0x0 but position-independent
 * (pc-relative literal pools / adr only), so it is safe wherever firmware
 * places it.
 *
 * Why this exists: from Non-secure EL1 the 1 kHz timer PPI can never be
 * delivered.  GICD_IGROUPRn is RAZ/WI from NS, so the group (IRQ vs FIQ line)
 * is Secure-owned and invisible to the kernel, and the stock firmware's
 * SCR_EL3 traps the resulting line to its EL3 monitor, which never returns.
 * Both fixes are Secure-EL3-only privileges, so they live here:
 *   1. SCR_EL3 with IRQ=FIQ=EA=0  -> physical IRQ/FIQ are NOT trapped to EL3.
 *   2. GICD_IGROUPR0..7 = all-1s  -> every interrupt is Group 1, so the timer
 *      PPI signals the IRQ line and is deliverable to NS EL1.
 * The stub then erets to NS EL2h at 0x80000 (the kernel's normal entry); the
 * kernel's own _start performs the EL2->EL1 drop exactly as with stock
 * firmware.  x0 (firmware DTB pointer) is preserved for the kernel.
 *
 * CRASH-PROOF: a local VBAR_EL3 whose every entry skips the faulting
 * instruction is installed FIRST.  Without it, any unexpected exception at
 * EL3 (e.g. firmware handing us a lower EL than expected) hangs with no
 * output — indistinguishable from a firmware-level reject (rainbow screen).
 * With it, a bad write is skipped and boot proceeds to the console, which
 * keeps the two failure modes separable on hardware. */

    .section .text
    .globl _start
    .balign 4
_start:
    /* Install the fault-skip EL3 vector table before touching anything. */
    adr   x16, el3_vectors
    msr   vbar_el3, x16
    isb

    /* Park secondary cores; BTRON3 runs single-core on core 0. */
    mrs   x16, mpidr_el1
    and   x16, x16, #3
    cbz   x16, 1f
2:  wfe
    b     2b

1:
    /* SCR_EL3 = NS|SMD|HCE|RW (0x581); IRQ=FIQ=EA=0 so nothing is trapped. */
    ldr   x16, =0x00000581
    msr   scr_el3, x16
    isb

    /* GIC-400 distributor, Secure view: enable both groups and move every
     * interrupt (incl. timer PPI 27 in IGROUPR0 bit 11) into Group 1. */
    ldr   x17, =0xFF841000
    mov   w18, #3
    str   w18, [x17, #0x000]          /* GICD_CTLR  EnableGrp0|EnableGrp1 */
    mvn   w18, wzr
    str   w18, [x17, #0x080]          /* IGROUPR0   SGI/PPI  (0-31)  */
    str   w18, [x17, #0x084]          /* IGROUPR1   SPI 32-63   */
    str   w18, [x17, #0x088]          /* IGROUPR2   SPI 64-95   */
    str   w18, [x17, #0x08C]          /* IGROUPR3   SPI 96-127  */
    str   w18, [x17, #0x090]          /* IGROUPR4   SPI 128-159 */
    str   w18, [x17, #0x094]          /* IGROUPR5   SPI 160-191 */
    str   w18, [x17, #0x098]          /* IGROUPR6   SPI 192-223 */
    str   w18, [x17, #0x09C]          /* IGROUPR7   SPI 224-255 */

    /* CPU interface, Secure view: full priority mask, enable both groups.
     * (The NS-banked GICC_CTLR/PMR are programmed by the kernel itself.) */
    ldr   x17, =0xFF842000
    mov   w18, #0xFF
    str   w18, [x17, #0x004]          /* GICC_PMR  */
    mov   w18, #3
    str   w18, [x17, #0x000]          /* GICC_CTLR EnableGrp0|EnableGrp1 */
    dsb   sy
    isb

    /* Hand off: NS EL2h at the kernel entry, DAIF masked, x0 (DTB) intact. */
    ldr   x16, =0x00080000
    msr   elr_el3, x16
    mov   x16, #0x3C9                 /* SPSR: M=EL2h(0b01001) | DAIF masked */
    msr   spsr_el3, x16
    isb
    eret
    b     .                           /* never reached */

    /* Every EL3 exception vector skips the faulting instruction and returns,
     * so an unexpected EL at entry or a refused system-register write degrades
     * to "config not applied" instead of a silent hang. */
    .balign 2048
el3_vectors:
    .rept 16
    .balign 128
    b     el3_skip
    .endr

    .balign 4
el3_skip:
    mrs   x16, elr_el3
    add   x16, x16, #4
    msr   elr_el3, x16
    eret
