ENTRY(_start)

SECTIONS
{
    . = 0x80000;
    __stack_top = .;

    .text : {
        KEEP(*(.text._start))
        *(.text*)
    }
    .rodata : {
        *(.rodata*)
    }
    .data : {
        *(.data*)
    }
    .bss : {
        . = ALIGN(8);
        __bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }
    _end = .;

    /* Objects the CPU shares with a bus master that has no cache.  DWC2 hands
     * the addresses of its setup packet, control payload and interrupt-IN
     * reports straight to DWC2_HCDMA with GAHBCFG.DMAEn=1, so they must not
     * live in .bss, which arm64_mmu_init() maps write-back: as .bss objects the
     * setup packet stayed in a dirty line the device never saw and the IN
     * reports were read back stale, which is what killed keyboard and mouse
     * once SCTLR.C went to 1.  They go to the MAIR attr2 window instead
     * (btron/arm64_mem.h), above everything that file assigns by hand: xHCI
     * owns BASE+0x00000..0x58000 and the mailbox/DMA-CB blocks BASE+0x80000.
     * The address below is the same as BTRON_NOCACHE_SECTION in
     * btron/arm64_mem.h (BASE + 0x90000).  A linker script cannot include a C
     * header, so the two are literal copies and scripts/check_structure.sh
     * compares them on every build: .bss running past this line is caught by
     * lld as a section overlap, but a header edit that outruns this line is not
     * caught by anything but the check. */
    . = 0x01C90000;
    .nocache (NOLOAD) : {
        __nocache_start = .;
        *(.btron.nocache)
        *(.btron.nocache.*)
        . = ALIGN(8);
        __nocache_end = .;
    }
}
