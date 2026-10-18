/*
 * ps2.ld — Sony PlayStation 2 Emotion Engine Linker Script
 *
 * Cleanroom B-System / BTRON3 3.20 RTOS Kernel Layout
 *
 * Target: Emotion Engine MIPS R5900 (MIPS-III Little-Endian)
 * Base Address: 0x00100000 (1 MB physical RAM mark)
 * Total Memory: 32 MB RDRAM (0x00000000 - 0x02000000)
 *
 * Copyright 2026 Synrc Research Center. MIT License.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x00100000;

    .text : {
        *(.text.startup)
        *(.text)
        *(.text.*)
        *(.gnu.linkonce.t.*)
    }

    . = ALIGN(128);
    .rodata : {
        *(.rodata)
        *(.rodata.*)
        *(.gnu.linkonce.r.*)
    }

    . = ALIGN(128);
    .data : {
        _gp = ALIGN(16) + 0x7FF0;
        *(.data)
        *(.data.*)
        *(.gnu.linkonce.d.*)
        *(.sdata)
        *(.sdata.*)
    }

    . = ALIGN(128);
    __bss_start = .;
    .sbss : {
        *(.sbss)
        *(.sbss.*)
        *(.scommon)
    }
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
    . = ALIGN(128);
    _end = .;

    /* The EE's main RDRAM ends here, and the PS2 has no other.  Statics placed
     * above it are not merely missing from a trace: the write to them succeeds, the
     * read of them returns whatever the unmapped window hands back, and on PCSX2
     * that has been zeros -- so a counter that is incremented comes back zero, and
     * the code that reads it concludes nothing ever happened.
     *
     * That is how this port lost its event queue (event.c's g_q_count: snd_evt()
     * returned E_OK for every keystroke and click, get_evt() found an empty queue,
     * and the desktop went on painting a cursor the dispatcher never saw) while the
     * pointer path, whose statics the luck of section order had placed below the
     * line, kept working.  The overflow was invisible because nothing in the build
     * or the guest ever complained, so the build now does: 64 MiB of Quake's hunk
     * arena in a 32 MiB machine is a budget question, and this is where the answer
     * stops being silent.  See PS2_GL_SRCS in the Makefile.
     *
     * The line is boot_ps2.s's stack top, not the RAM end: $sp is set to
     * 0x01FF0000, so an image that reaches the last 64 KB is corrupted by its own
     * calls rather than by an unmapped window, which is worse and harder to see.
     * The clear loop above _end is also boot's cost -- 26 MB of it is 6.5 million
     * stores before the kernel's first instruction. */
    __ps2_ram_end = 0x02000000;
    ASSERT(_end <= 0x01FF0000,
           "PS2 image reaches the stack or the 32 MiB RDRAM end: statics above it are silently lost")

    /* The law the one above does not cover: `_end <= 0x01FF0000` still passes an image that leaves
     * 4 kB of stack, and that image does not fail to link, it corrupts its own heap tail the first
     * time the XMB recurses.  The pool (core_ps2.c's PS2_HEAP_SIZE) is the bulk of `_end` and is
     * sized in kB, so the day someone raises it is exactly the day this matters.  256 kB is 11x the
     * measured stack high-water of a full desktop-plus-XMB session (22 kB, printed by the heap
     * holder census as `stack used=`), and it is a reserve, not a measurement -- widening the pool
     * past it is a decision the build now has to be told about.
     *
     * The reserve is a symbol, not a literal, because it is a shipped-image guarantee rather than a
     * fact about the hardware, and the bench image is not a shipped image.  What lives between `_end`
     * and $sp is only stack, and the thing that spends it is the same desktop/XMB recursion in both
     * builds -- what differs is the cost of the instrument: a bench TU pays for its print rows in
     * .rodata, and LANE 19's census has grown rows faster than 256 kB of headroom can absorb.  A
     * measurement image whose .rodata cannot link is worth nothing, so it takes a smaller reserve:
     * 64 kB is still ~3x the 22 kB measured high-water, and if the bench ever overruns it the run
     * dies in front of the census that prints `stack used=`, which is the honest place to find out.
     * Production keeps 256 kB untouched -- it links with the default PROVIDE'd value below.  The
     * knob is `--defsym=__ps2_stack_reserve=<bytes>` on the link line; see $(PS2_BENCH_TARGET). */
    PROVIDE(__ps2_stack_reserve = 0x40000);
    ASSERT(_end <= 0x01FF0000 - __ps2_stack_reserve,
           "PS2 image leaves under its __ps2_stack_reserve below boot_ps2.s's stack top: the stack will run into the heap")

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
