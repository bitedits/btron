# B-System (BTRON 3.20) PlayStation 2 & MIPS Developer Guide

This document is the technical porting reference for kernel and driver developers working on the **Sony PlayStation 2 (Emotion Engine)** and **Bare-Metal MIPS (QEMU Malta & Magnum)** targets of the B-System operating system.

## 1. Architectural Principles: Image > Core > Kernel

Like all B-System workstation ports, the PS2 and MIPS targets adhere strictly to the 3-tier **Cleanroom** architecture:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        Image (Final Run Target)                        │
│             btron-ps2.iso (PCSX2)  /  btron-mips.elf (QEMU)            │
├────────────────────────────────────────────────────────────────────────┤
│                       Cores (src/cores/) Layer                         │
│  Platform Integration: Memory Maps, Hardware Framebuffer, Console      │
│  src/cores/core_ps2.c               │  src/cores/core_mips.c           │
├────────────────────────────────────────────────────────────────────────┤
│                       Kernel (src/kernel/) Layer                       │
│  Pure Portable RTOS Executive: uITRON 3.0 Primitives                   │
│  Tasks, Semaphores, Mutexes, Mailboxes, Memory Pools, Timers, Libstr   │
├────────────────────────────────────────────────────────────────────────┤
│                       Drivers (src/drivers/) Layer                     │
│  Hardware Bootstrap, Linker Scripts, Silicon Registers                 │
│  src/drivers/ps2/ (GS, SIO, Pad, USB)│ src/drivers/mips/ (16550 UART) │
└────────────────────────────────────────────────────────────────────────┘
```

### Cleanroom Philosophy

- **Zero Proprietary SDKs**: 100% cleanroom implementation. No proprietary Sony headers (`sifrpc.h`, etc.), no Sony libraries, and no copyrighted code.
- **Direct Hardware Access**: Privileged MMIO registers for the Graphics Synthesizer (GS), SIO0 UART, DualShock 2 SIO2 controller, and USB OHCI are driven directly.
- **Microkernel Isolation**: `src/kernel/` remains pristine, portable, and untouched across all architectures.

## 2. Target 8: Sony PlayStation 2 (`make run-ps2`)

### Hardware Overview

- **CPU**: Sony Emotion Engine (EE) MIPS R5900 (MIPS-III little-endian 64-bit core with 128-bit SIMD vector units @ 294.912 MHz).
- **RAM**: 32 MB RDRAM (Physical addresses `0x00000000` to `0x02000000`).
- **Load Address**: `0x00100000` (1 MB mark into physical RAM, standard PS2 homebrew entry).
- **Stack Pointer**: `0x01FF0000` (top of 32 MB RDRAM with 64 KB safety headroom).
- **Display Controller**: Graphics Synthesizer (GS) with 4 MB embedded DRAM (eDRAM).
- **Graphics Pipeline**: DMAC Channel 2 (GIF) Host-to-Local image blitting (`BITBLTBUF`, `TRXPOS`, `TRXREG`, `TRXDIR`).
- **Video Timing**: 800 x 600 @ 32-bpp RGBA Non-Interlaced Progressive Scan (GS VESA Mode 0x2B, 60Hz).
- **GUI Loading Modes**: Automatic direct Graphical Desktop (`AUTO_GUI=1`, default) or Two-Stage boot with Stage 1 text console (`AUTO_GUI=0`).
- **Input Channels**: DualShock 2 Pad, Sony / OHCI USB Keyboard & Mouse (`HID Keyboard` / `HID Mouse`), and EE SIO0 UART console.

### The Image Budget (Normative)

The whole image -- `.text`, `.data`, `.bss` -- must end below `0x01FF0000`, the stack
pointer `boot_ps2.s` installs.  That is the real limit rather than the `0x02000000` RAM
end, because `.bss` grows upward into the stack.

Statics the link places past it are not an error at runtime: RDRAM simply is not there,
so a store to one is swallowed and the load returns garbage.  This fails silently and
per-object -- `snd_evt()` keeps returning `E_OK` while its `g_q_count++` goes nowhere,
and `get_evt()` answers `E_TMOUT` forever -- which is why the symptom reads as "the
event system is broken" rather than as an out-of-memory condition.  A `.bss` that
overruns also takes the deskbar (`g_gmenu`), the start menu (`g_tracker`), the
compositor's background cache and the fs/vobj tables with it, while pointer-path
statics that section order happened to place low keep working.

`src/drivers/ps2/ps2.ld` enforces the budget with a link-time `ASSERT`, so a source
list that grows the image past RAM fails at the link instead of on the guest.  The
budget is why `PS2_SRCS` carries `PS2_GL_SRCS` (the GL layer plus `xmb.c`/`glgears.c`)
rather than `GL_SRCS`, whose `$(QUAKE_SRCS)` and `$(DEMO_SRCS)` alone account for
roughly 82 MB of `.bss` -- the 64 MB Quake hunk arena and the demo pack's arrays.

### One Calling Convention (Normative)

Every C translation unit in `btron-ps2.elf` is built `-march=mips2 -mabi=32 -msoft-float`,
and the image contains no second ISA of C and no second floating-point ABI.  This is a rule
about the *link*, not about
instruction speed: under O32, a 64-bit-GPR MIPS (what `-march=mips3` selects) passes
fixed arguments 5..8 in `$t0..$t3` where a 32-bit-GPR MIPS passes them on the stack, and
it lays its vararg save area out in 8-byte slots while `va_arg` still walks four bytes at
a time.  Each convention is self-consistent inside one translation unit, so a mixed link
produces no warning, no bad instruction and no crash -- only the fifth and later argument
of a cross-file call arrives as garbage, and every `%s` in a console line printed from the
other side of the boundary shifts the rest of the line by a word.

The cursor reported this law on 2026-10-09.  `ps2_paint_region()` in `src/cores/core_ps2.c`
appends the sprite with `draw_baremetal_mouse_cursor(screen, x, y, w, h)`, a five-argument
call into `src/desktop/desktop.c`; the two files were compiled for different ISAs, so `h`
-- the canvas height -- was junk, every row of the draw failed `py >= h`, and the sprite
vanished on the first banded pass after the whole-canvas repaint that draws it from inside
`desktop.c`, where the call stays within one ISA.  A pointer that appears only every two
seconds, on the convergence repaint, is this failure.

Why `mips2` is the side that wins, and the two exceptions it needs:

* `-march=mips3` cannot be the uniform choice because its vararg layout is the one
  `va_arg` disagrees with, and the port's console (`ps2_kprintf`, `tkl_vsnprintf`) is
  variadic on every platform.
* `-march=mips32r2` would be 32-bit-GPR too, but clang may reach for `ext`, `ins`, `clz`
  and `madd`, which a R5900 does not implement.  `mips2`'s instruction set is a subset of
  the Emotion Engine's MIPS III.
* The GS latches a privileged register on a **single 64-bit store**, which a 32-bit-GPR
  build cannot emit.  `src/drivers/ps2/ps2_gs_reg.s` provides `ps2_gs_poke64()` /
  `ps2_gs_peek64()`, and C reaches them through `GS_REG_POKE` / `GS_REG_PEEK`
  (`src/drivers/ps2/ps2_gs.h`) with three 32-bit arguments -- an interface identical under
  either ISA, which is what makes it a safe place for an assembly-only exception.
  `ps2_halt`'s `wait` in `boot_ps2.s` is enabled the same way, with `.set push` /
  `.set mips3` around the one instruction.
* Converting a `double` to or from a 64-bit integer has no instruction on this CPU either,
  and neither has any other 64-bit FPU operation -- see [The FPU Law](#the-fpu-law-normative),
  which is what `src/drivers/ps2/ps2_builtins.c` exists to satisfy.

The measurement that says the law is holding is the cursor phase of `scripts/ps2_bench.sh`:
a banded pass must report `sig at drawn: bb=1 fb=1` with `pre=0 post=1` per region -- the
append ran and wrote, in the backbuffer and in the presented frame buffer.  `post=0` while
`appended=1` is a five-argument call whose fifth argument did not arrive, and the phase's
verdict names that case.

`scripts/test_ps2.sh` holds the static side of the same law: tests 35-38 assert that the
linked image's MIPS ABI flags read `ISA: MIPS2` / `GPR size: 32`, that **no** `*.ps2.o`
reports 64-bit GPRs (a stale ELF can outlive a flag change), and that `ps2_gs_poke64`
contains exactly one `sd` and `ps2_gs_peek64` exactly one `ld`.

### The FPU Law (Normative)

The Emotion Engine's FPU is **single-precision**.  `ldc1`, `sdc1` and every other
double-precision COP1 instruction are not merely slow on a R5900 -- they are *illegal*, and
PCSX2's reaction to one is to print `Unknown R5900 COP1:` and let the instruction **do
nothing**.  Nothing is worse than a trap here: the load leaves the destination register at
whatever it held, the store lands nowhere, and the program continues with a `double` that is
silently a previous `double`.  Every float-heavy file in this port (`glgears.c`, `xmb.c`,
`backend_virgl.c`, and TinyGL's `matrix.c`/`light.c`/`zmath.c`/`zbuffer.c`) is written in
`double`, so before this law was applied the XMB reported `black=383999 other=1` out of
384000 pixels and a single bench run printed 2,093 `Unknown R5900` rows.

The rule is therefore about the **whole image**, not about the two conversions that happened
to need libcalls: `PS2_CC` carries `-msoft-float`, so a translation unit emits no COP1
instruction and FP arithmetic reaches the runtime as a function call -- with the one
single-precision exception the next paragraph names.  The two exceptions to
`-march=mips2` above are *not* exceptions to this one -- `ps2_gs_reg.s` and `boot_ps2.s`
contain no FP.

One unit is built with the FPU on, by design and only for single precision:
`src/drivers/ps2/ps2_builtins.c` is compiled `-mhard-float -DBTRON_PS2_FP_HW` (Makefile
`PS2_FP ?= hw`, and `PS2_FP=soft` builds the same file without it).  That is permitted by
the second half of this law rather than an exception to it: the `.d` forms stay illegal
everywhere, and the `.s` forms are only ever emitted inside a unit whose entire public
surface is integer-typed.  Mixing remains the hazard the next paragraph describes, so the
sanctioned unit must never let a `float` cross its boundary.

Mixing is not an option either, and this is the part that makes it an ABI law rather than a
performance note.  Under `-msoft-float`, O32 passes floating values in **integer**
registers: a `double` occupies an even/odd GPR pair with the low word in the even register,
so the first two doubles are `$4:$5` and `$6:$7` and a third spills to the stack; a `float`
takes one register; a `double` returns in `$2:$3`.  Those are exactly the slots the FPU ABI
would fill with COP1 registers, so one file built without the flag hands its arguments to a
callee built with it in registers that stay untouched.  This was read off a disassembly of
this toolchain, not off a manual.

`src/drivers/ps2/ps2_builtins.c` supplies the 32 libcalls that link then requires, because
`-nostdlib` provides none and this machine has no libgcc or compiler-rt for the target.  It
is written against that measured ABI -- each is declared with `uint64_t`/`uint32_t`
arguments, which is register-for-register identical to the `double`/`float` form clang
expects -- and implements exact round-to-nearest-even IEEE-754 by hand.  Two details carry
the correctness:

* Every 64-bit shift and split is expressed with 32-bit variable shifts plus constant
  64-bit shifts, so the file never references `__lshrdi3`, `__udivdi3` or `__muldi3`; it
  must be self-contained or it defines the problem it solves.
* A comparison libcall returns `-1/0/1` and the *caller* decides the meaning: `lt` tests
  `< 0`, `le` tests `< 1`, `ge` tests `> -1`, `gt` tests `> 0`.  All four therefore share
  one three-way routine, which returns the unordered case as `+1` -- the only value that
  makes `lt`, `le`, `eq` and `ne` all behave for a NaN.  `gt` and `ge` need unordered `-1`
  and so call the same routine with that argument.

The validation is `make test-ps2-softfloat` (`.build/test_ps2_softfloat`): it includes the
implementation textually and runs it against the host's own FPU over 15,222,125 comparisons
-- random bit patterns mixed with special values, a power-of-two sweep, named ties,
subnormal cancellation and overflow -- and asserts the results are **bit-identical**.  Two
deliberate mutations of the runtime (an off-by-one exponent, an unwindowed multiply) were
caught with 12,356 and 460,302 failures, which is what makes the passing number mean
something rather than prove nothing.

The static side lives in `scripts/test_ps2.sh` as tests 39-44: that no object *except*
`ps2_builtins.ps2.o` contains an FP instruction at all (a leftover `.s` op elsewhere means a
translation unit that escaped `-msoft-float`, and its arguments would arrive in registers the
rest of the image does not use), that the image contains no double-width op and neither of
the two single-precision encodings this CPU does not implement, that those two encodings are
absent **byte-for-byte** as well (a mnemonic grep trusts the disassembler's naming; the
byte scan does not), that `ps2_builtins.ps2.o` really does emit COP1 arithmetic -- so a
stale soft-float build of it cannot pass by having no FPU code at all -- that no *named*
symbol is undefined, and that all 32 libcalls are present.  Test 44 reads the symbol table
into a variable first: piping `readelf` into `grep -q` would make grep exit at the first
match, readelf die of SIGPIPE and `set -o pipefail` report that as a miss, so a symbol that
*is* defined would fail the test.

### What COP1 actually is, measured (Normative)

`btron_fp_init()` enables CU1 if the BIOS left it clear, masks the FPU's exception enables
and proves the unit answers with one round trip (`1.0f + 2.0f` must come back
`0x40400000`); the result is recorded in `btron_fp_on`, so a machine whose FPU does not
answer falls back to the bit engine and costs cycles rather than pixels.  Doubles never go
to the FPU at all, because it has no double-precision instruction.

`btron_fp_selftest()` runs both engines over the same operand vector and prints the verdict
as `[FPU]` rows.  These are the measured facts, and the port is designed around them:

| Question | Measurement |
|:---|:---|
| What does one op cost? | COP1 20 cycles, bit engine 160 -- an 8x on the arithmetic itself |
| ...and from a `-msoft-float` caller, which is everywhere in the image? | COP1 28 cycles, bit engine 536.  The 8 above the engine's own 20 is the `jal` and moving the operands between GPRs and COP1; the 376 above 160 is that plus the generic engine's own entry work.  28 is the price every stage table in this file bills a float operation at, and it is the one number that says whether an arithmetic change is worth making |
| Where does it disagree? | `add.s`, `sub.s`, `mul.s` only; `div.s`, `sqrt.s`, the five compares and `cvt.s.w` agree on every clean pair |
| How far? | Exactly one representable step, never wider, never across zero -- 61 of the 576 clean arithmetic pair-runs (each class runs 144) |
| Which engine is right? | The bit engine: it is host-proven exact, so each of those 61 is COP1 answering the neighbour that is *not* correctly rounded |
| Is it a rounding mode? | No.  Those classes make a rounding decision on 60/58/42 of their clean pairs and COP1 is wrong on 28/14/19 of them, while `div.s` -- which decides on 81, more than any other op -- is wrong on none.  A mode cannot skip the op with the most decisions to make |
| Does it flush subnormals differently? | No: that count is zero |

So the gates on the arithmetic are `fpu_gap_wide == 0` and `fpu_gap_sign == 0`, not
"agrees with the bit engine": a one-step gap is invisible in an 8-bit pixel, and a demand
that `add.s` be IEEE-exact is a demand that this CPU be a different CPU.  `fpu_other` is
reported as information for the same reason.

What this does *not* buy, on its own: the single-precision switch makes each float
operation cheaper, but it does not change *how many* there are.  Instrumenting the paint
phase by phase (`[XMBT] f_*` rows in `xb_paint`) showed the whole cost was in one place --
the ribbon's `xb_ribbon_step`, at 9.6 s of a 10.4 s frame -- and inside that, in
`xb_iqhash`: its argument is an integer lattice coordinate `px + py*57 + pz*113`, far
outside `sinf`'s 4*pi guard, so all 32,768 calls a frame made went to the software double
sine even though the lattice holds only ~160 distinct values.  Caching the hash per integer
(2048-slot open addressing, Fibonacci-hashed) and hoisting the two grid-separable cosines
out of the inner loop cut `f_surface` from 9.6 s to 171 ms -- a 56x cut that changes the
arithmetic nowhere, only how often it runs.

The icon atlas was the other half of that frame, and it went the same way -- by being
removed rather than optimised.  Its geometry is signed-distance arithmetic over 36 cells of
64x64 texels, which is 147,456 evaluations of `sin`-free but still floating-point CSG on a
CPU with no FPU in the pipeline; making it on-demand only moved the bill into the first menu
paint, where it read `[XMBT] f_menu 2996702 us` against a steady `182992 us`, because each
of the ~17 cells a band shows cost ~132 ms to bake *and* ~33 ms to re-upload the whole
384x384 sheet.  So the sheet is now data, not arithmetic: `verify/tests/gen_xmb_atlas.c`
evaluates the same expressions once on the host and emits `src/apps/xmb_atlas.h`, a
committed `static const unsigned char[147456]` -- one alpha byte per texel, which is a
complete description because the ink is the constant `(248,250,255)` and only coverage
varies.  It keeps the one distinction that matters: a texel the geometry wrote at coverage 0
is not a texel nobody touched, because TinyGL's `GL_LINEAR` filter interpolates RGB as well
as alpha, so the generator clamps written texels to alpha 1 and `xb_fill_icon_atlas` paints
RGB only where alpha is non-zero.  Boot now reads `fill_atlas 19885` + `upload 33262` and
uploads once, ever; frame 1 fell to `1036572 us` and its `f_menu` to `182991 us`, i.e. equal
to the steady frame, and the 589,824-byte staging buffer is back on the heap.  Two gates
hold that from drifting: `make test-xmb-atlas` regenerates the header and `cmp`s it (proved
to fail on one perturbed texel), and `make test-xmb-render`'s `atlas_cell_look` ink counts
and whole-atlas PPM were byte-identical between the runtime arithmetic and the shipped sheet
(cell 24 = 1863 ink px, cell 26 = 1889).

What is left: a steady frame is 683 ms, split `f_bg 233 / f_menu 202 / f_ribbon 199 /
f_surface 43 / f_clear 7 / f_particles 0.2` us over a 768x500 canvas.  (That canvas and that
frame are both superseded: the shipped one is 800x480 and *What a pixel costs, measured* below
carries its live split.  The split's *shape* -- which stages are per-pixel -- is what the rest of
this section argues from, and that has not changed.)  The per-pixel blend
switches are gone -- `TGL_BLEND_VARS` now recognises GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA /
GL_FUNC_ADD once per primitive and gives it a branch-free arm (and since 2026-10-11 it recognises
the ribbon's GL_DST_COLOR / GL_ONE the same way; both recognitions and their measured prices are in
*What a pixel costs, measured* below) -- which is what moved `f_bg`
from 313 ms to 233 ms and the frame from 773 to 683.  Two further clamps (the channel
saturate written as arithmetic, and the depth-plane store gated on the depth test per
`ZB_DEPTH_WRITE`) bought 1% more, so the per-fragment work is now where the object code says
it is: about sixty-six integer instructions for a blended pixel, of which six `mult`/`mflo`
pairs are the blend itself -- `-march=mips2` has no `mul` instruction.

The decisive number for what remains is the split, taken by running the same image with
`glEnable(GL_BLEND)` commented out: `f_bg 233 -> 62`, `f_ribbon 199 -> 127`, `f_menu 202 ->
167`, `f_surface 43 -> 43`.  So blending is 174 of `f_bg`'s 233 ms and 41% of the frame, and
a *plain* smooth-shaded fill -- interpolate, write -- costs nine times what a clear costs.
Set against the budget, that is the whole story: `draw_us 16700` over 384000 pixels is 0.044
us per canvas pixel, while this image's cheapest possible per-pixel operation, `f_clear`'s
store, measures 0.018 us.  A 60 Hz budget therefore buys about twelve instructions per canvas
pixel, and the XMB touches the canvas roughly three times a frame.  No arrangement of scalar
arithmetic reaches that; the remaining gap belongs to *how many pixels are blended* -- cached
planes and damaged-rect redraws, which are tasks #28 to #31 -- and to the GS hardware
rasterizer, not to the blend formula.

One thing no measurement in this tree can settle: every `[FPU]` number here came from
PCSX2's Emotion EE.  The same rows print over SIO on a real console, so the identical
image run there says whether the one-step residue is the emulator or the silicon.

### Textures and alpha in TinyGL (the corrupted-fonts fix)

The bar's fonts, icons and 3D sheet reached the PS2 build corrupted, and three independent
defects in the fork's texture path explain all three symptoms:

- **`GL_RGBA` was not an accepted upload.** `glopTexImage2D` required
  `format == GL_RGB && components == 3 && type == GL_UNSIGNED_BYTE` and called
  `gl_fatal_error()` otherwise -- which is `exit(1)`, so an RGBA upload did not degrade, it
  ended the process.  The source's channel count is now taken from `format` alone
  (`gl_tex_format_channels`), and `gl_convert_row_to_8A8R8G8B` converts a row of either 3- or
  4-byte texels.  The stride matters most: reading an RGBA texel as an RGB one walks the
  source one byte further off every texel, which is what a diagonally smeared font sheet
  looks like on a real display.
- **Every upload was resampled into a fixed 256x256 plane.** `GLImage` used to hold
  `PIXEL pixmap[256*256]` inline and nearest-resample anything whose extent differed, so the
  384x384 icon atlas was *downscaled* to 256x256 and lost a third of its texels.  A `GLImage`
  now owns a heap plane at the extent that was uploaded, rounded up to a power of two per axis
  because the sampler wraps by masking, and it carries its own `smask`/`tmask`/`tshift` for
  sampling and `sscale`/`soff`/`tscale`/`toff` for mapping a texture coordinate onto its true
  extent.  `ZB_setTexture()` binds those three sampler fields into the `ZBuffer` once per
  primitive so the span loop keeps them in registers.  For a 256x256 upload the new
  arithmetic reduces to exactly the old constants (`sshift 9`, `tshift 9`,
  `sscale = 256*2^11 - 2^10`), which is what keeps the host comparison below meaningful.
  `TGL_FEATURE_TEXTURE_MAX_POW2` (512) is the ceiling; every in-tree upload is within it --
  the bar's atlas is 384x384, its font sheet 128x96, and `src/quake/render/texture.c` caps
  its own uploads at 512x512.
- **Alpha blending had no alpha.** `TGL_BLEND_FUNC` switched over the GL factors but had no
  `GL_SRC_ALPHA`/`GL_ONE_MINUS_SRC_ALPHA` arm, so the default fell through to
  `dest = src + dst`: every fade was a no-op and the ribbon's
  `glBlendFunc(GL_DST_COLOR, GL_ONE)` silently degraded to additive.  The vertex alpha is now
  carried as a fourth interpolated channel (`ZBufferPoint.a`, left-aligned like r/g/b,
  interpolated by `INTERP_A` and lerp'd across a clip edge in `updateTmp`), and
  `TGL_BLEND_FACTOR` implements the factor set in integer arithmetic -- each channel is an
  8-bit value held at bit 16, so a factor is one byte extract, one multiply and one `>>8`,
  and `0xff0000 * 255` still fits a `GLuint`.  Nothing here costs a float, let alone a
  libcall, on a `-msoft-float` target.  A textured fragment's alpha is the texel's own byte
  times the vertex byte, which is GL's modulate.
- **The blend state was resolved per pixel.** With the factors implemented, a blended fill
  still ran `switch(blendeq)` and two `switch(factor)` for every fragment, because they were
  only loop-invariant to the reader, not to the compiler: the object code for `f_bg` was an
  indirect `jr` through a jump table once per pixel.  `TGL_BLEND_VARS` now reads the three
  parameters once per primitive and recognises the two combinations the applications actually
  ask for, handing `TGL_BLEND_ALPHA_ADD` and `TGL_BLEND_BRIGHTEN` branch-free arms; anything else
  still runs the
  general switches, unchanged.  The arm is bit-exact against the general one by construction
  (same terms, and its channel saturate is `min(v >> 16, 255)` written as subtract-shift-and,
  since a gradient crosses the boundary at a different column in every row).  Related, and
  also per-fragment: `ZB_DEPTH_WRITE` stops the span loop from storing `z >> 14` into the
  depth plane when `GL_DEPTH_TEST` is off -- GL 1.1 4.4.1 does not update the plane at all in
  that state, and the 2D UI is exactly that case.

- **A large float angle was a double-series call.** `sinf`/`cosf` guarded their float fold
  at 4*pi and handed everything past it to the double relative, which on this target is not
  a slower path but a different order of one: read out of a probe TU compiled with
  `PS2_CC`, `btron_sincos` costs 415 instructions and 52 calls into the soft-float runtime,
  13 of them `__divdf3` (the series divides by `n*(n-1)` term by term), against the float
  polynomial's 34.  That is why the guard was there -- folding the same turn count as a
  *float* moves the angle by the rounding of one float product, measured 0.028 rad at 1e6
  and 0.28 rad at 1e7, which is not a precision question but a different point on the
  circle.  It is also the reason the ribbon's surviving
  4,096 `cosf(gz + t/10 + gx)` calls per paint were a cliff rather than a slope: at 0.01 per
  frame the argument leaves 4*pi around frame 12,600, and every call past it paid for a
  double series instead of a fold.
  `btron_sincosf_far` now folds the turn count in double -- nearest integer through a `long
  long`, then `d - k*pi2 - k*pi2_lo`, the two-part constant travelling with the count exactly
  as it does in the float fold, because at 3e8 turns the 2.45e-16 by which `double(2*pi)`
  sits below 2*pi is 8e-8 of angle, i.e. a float's last place -- and keeps the float
  polynomial for the residue, which the nearest-integer quotient has already brought inside
  half a turn.  Cost: 12 double calls and no division, then the same 34 float ones.  The
  tiers are 4*pi (float fold alone), 2^31 (double fold, whose error is half an ulp of the
  turn product and so still under a float's last place near 1.0) and inf/NaN (the double
  relative, which is the only one that answers NaN for both).

Proved on the host, not argued: `make test-xmb-render-tgl` runs the same `xmb.c` through
`backend_tinygl.c` and writes its frames to `.build/tgl/`, and it now reports the same
68/70 as `make test-xmb-render` through `backend_virgl.c` -- including the same two failures,
which are the app's gradient corners, not the rasterizer.  Per pixel, the two backends agree
to within 2 levels of mean absolute difference over the whole 960x600 canvas, with 92% of
pixels identical to ±2 and 1.4% differing by more than 32 (text and icon edges, where the
alpha paths round differently).  The shipped atlases are visually indistinguishable from the
reference, which is the direct check on the two symptoms the user reported.  The twelve
`.build/tgl` frames are byte-identical across every change above, and they have to be: none
of it is a change to a value.

`make test-gl-math` is the same discipline applied to the arithmetic, and the trig fold is
now measured where it is used.  Its sweep covers every integer from 14 to 100000, a stride
to 1e6 on the negative half, and both signs of every binade from 2^3 to 2^19 -- 237,000
comparisons against the host libm on top of the reduction regions, in a run that now reports
3.8 million checks.  The worst absolute error
past the guard is 2.384e-07, two steps of a float near 1.0 and the same figure the regions
inside the guard measure, so the fold buys its speed without spending accuracy.  And the
sweep has teeth of its own: the port TU ships a `p_sinf_far_floatfold` canary that folds the
turn count as a float, which is the bug class the guard was built to hide, and it is caught
on 100,452 of the far samples.  `make test-ps2` reads the cost rather than the value: two new
laws assert that the probe's far fold contains no `__divdf3` at all and that it does go
through the `long long` round trip, so a regression to the double series cannot pass by being
an empty object.  Reverted on purpose once, to check: 13 divisions.

The plane is now heap, and that has a number attached: the 384x384 atlas takes a padded
512x512 `PIXEL` plane, i.e. 1024 kB, against the 256 kB the inline array used to cost -- the
boot's `[HEAP]` rows read `+1024 kB -> used=9822 kB peak=9822 kB largest_free=2465 kB`, and
the 576 kB RGBA staging buffer is returned immediately after the upload.  It fits, with 2.4 MB
of the largest hole left; a `glTexImage2D` that cannot get its plane is still loud, by design.

Cost of correctness: the steady frame went from 556 ms (broken blend, resampled atlas) to
773 ms (real blend, true extents), and the per-fragment work above takes it back to 683 ms
without changing a pixel.  Every one of those extra microseconds is integer, and they are
the subject of the `f_bg` paragraph above.  They are also emulator numbers: the
identical image over SIO on a real console is still the outstanding check.

### The frame buffer's alpha byte is written at the store (Normative)

A `COLOR` is `0xAARRGGBB`, a window's surface is composited as opaque, and the GS reads byte 3
of the canvas word it displays, so every pixel of a GL surface has to arrive with `0xFF` on
top.  TinyGL used to do that after the fact: `tgl_swap_buffers()` walked the whole surface
OR-ing the byte in, one uncached read and one uncached write per pixel per frame, measured at
`swap=10498 us` for the XMB's 384,000 px -- 10.5 ms of a 184 ms frame with no arithmetic in it
at all.  The pass is gone, and the law that replaced it is in `src/gl/tinygl/zbuffer.h`:

- `FB_PIXEL(v)` ORs the constant alpha into a pixel and is used at **frame-buffer stores
  only** -- clear (`ZB_clear`'s colour), the flat, interpolated, textured and both blended
  triangle paths, lines and points, `glDrawPixels`, the glyph blit, the postprocess, and every
  `TGL_*_BLEND_FUNC` arm.  It is idempotent, so a pixel that already carries the byte is
  unchanged.
- It is **never** inside `RGB_TO_PIXEL`, because the same macro builds texels and a texel's top
  byte *is* its alpha (`TGL_TEX_ALPHA`, the upload in `image_util.c`), which the blend
  multiplies by.  Forcing a texel opaque turns every icon and glyph in an atlas into a solid
  square.
- Nothing reads a frame buffer's alpha back: `GL_DST_ALPHA` has no arm in `TGL_BLEND_FACTOR`
  because a canvas has no alpha to read, and `glCopyTexImage2D` takes a `PIXEL` verbatim rather
  than through channel arithmetic.
- The pixels no primitive covers are stamped once, when the array is new:
  `tinygl_backend_init()` and a `tinygl_backend_resize()` that actually changed the buffer or
  its size.  The guard matters -- `egl_make_current()` routes through the resize path every
  time a context is made current (quake and lilcu64 do it per frame), and a walk of the
  surface there would be the per-frame pass this replaced.
- With nothing left to do, the TinyGL backend's `swap_buffers` slot is `NULL`.
  `egl_swap_buffers()` already handles that, and it still marks the window's image valid,
  which is what actually presents the frame.

The gate is `make test-ps2-fb-order`: `verify/tests/test_fb_order.c` renders one frame that
covers every store path above and asserts that both GL canvases hold zero non-opaque words,
in each of the two byte-order domains.  It was shown to discriminate -- removing `FB_PIXEL`
from the recognised-blend pack alone makes it report `2158 non-opaque words of 6144 drawn`
before the restore.  The XMB's PPMs cannot see this change at all (they carry RGB), which is
why the alpha has its own gate rather than riding on the frame comparison.

The device rows say the cut landed: `swap=` reads 0 on every frame (`[XMBT] frame 3 (tinygl)
draw=176514 us swap=0 us`), the present's `swap` stage is 0, and the smoothness gate
`xswap_us`/`full_swap_us` now passes at its "target: absent" budget.

### What a pixel costs, measured (Normative)

Every number in this file so far says what the image *does*; this one says what it *costs*, in
units that can be checked against a row of log.  TinyGL reports its own counts per frame stage:

```
[XMBT] raster <stage>  <n> tris <n> verts <n> px <n> spans <n> px/span <n> us fill <n> us geom <n> px/ms
```

`px` is the area the fill paths were asked to cover, summed from the triangle vertices at the
dispatch; `spans` is the same triangles' y extent, so `px/span` is the mean scanline length.
`fill` is microseconds bracketing the dispatch itself and `geom` is the stage's own timing minus
it, so the two columns separate the pipeline into the part that is billed per pixel and the part
that is billed per vertex.  The counters live in `src/gl/tinygl/clip.c`
(`tgl_count_fill`, `tgl_fill_stats`) and are read through `gl_fill_stats()` in
`src/gl/gl_dispatch.c`; the clock is `gl_perf_us()`, which is `btron_render_perf_us()` and so is
subject to *The measured microsecond (Normative)* further down this file -- 295 CP0 ticks per
microsecond on this machine, which is the divisor between every `us` column here and every cycle
count quoted beside it.

Reproduce with `SMOOTH_APP=1 SMOOTH_FRAMES=3 SMOOTH_IDLE=20 scripts/ps2_smooth.sh` and read the
`[XMBT] raster` rows of `build/ps2-smooth.log`; the pixels themselves are comparable frame by
frame with `make test-xmb-render-tgl`, which writes its PPMs to `.build/tgl/`.

The rows, 2026-10-11, shipped configuration (800x480 canvas, `XB_RIBBON_ROWS/COLS` 32), built
with `PS2_FP_COUNT=0` so the microseconds are the shipped image's own, steady frame
`draw=173750 us swap=10498 us` (that `swap` is the whole-surface alpha fix-up, since deleted --
see *The frame buffer's alpha byte is written at the store*; every stage row below is a draw
number and none of them moved):

| stage | tris | verts | px | spans | px/span | fill us | geom us |
|:---|--:|--:|--:|--:|--:|--:|--:|
| `f_bg` | 5 | 4 | 382,731 | 2,495 | 153 | 29,311 | 132 |
| `f_ribbon` | 1,828 | 1,984 | 102,320 | 16,289 | 6 | 49,778 | 21,763 |
| `f_menu` | 646 | 1,436 | 46,441 | 8,964 | 5 | 38,769 | 11,905 |
| `f_clear` | – | – | 384,000 | – | – | – | 3,398 |
| `f_surface` | – | – | – | – | – | – | 18,479 |

The last two rows are stage timings rather than raster windows: neither opens a fill dispatch, so
`xb_phase_raster()` prints nothing for them and their microseconds belong, by this file's own
definition of the columns, entirely outside the fill.

Those rows, and the same ribbon rows at `16x16` (448 tris, 480 verts, 100,378 px, 7,655 spans,
32,843 fill, 5,530 geom, steady `draw=126833 us`), are enough to price the machine, because the two
mesh rows cover the same pixels with four times fewer triangles.  Both are now the *same* image: the
`16x16` row was taken by setting `XB_RIBBON_ROWS/COLS`, rebuilding, and putting them back, which is
the condition the fit below needs -- a pair of rows from two different images prices whatever changed
between them, not the mesh.  Solving the pair as one equation in two unknowns (setup, blended pixel),
with each row's own span steps billed at 60 cycles, gives 10.9 us of setup and 77 cycles a pixel.  The
older pair -- `61,328` and `43,527` fill, from the binary before `TGL_BLEND_VARS` learned
`GL_DST_COLOR`/`GL_ONE` -- solves to the same setup (3,277 cycles) and 110 cycles a pixel, so a
least-squares fit over all four rows is taken as the price list: **3,206 cycles = 10.9 us of triangle
setup, 77 cycles of recognised blended pixel, worst residual 323 us (0.6% of the largest row)**.  The
fit is sensitive in the direction of the unknowns, not in their sum: pairing a `32x32` row against the
*other* binary's `16x16` row swings setup between 800 and 5,600 cycles, which is why the four rows are
fitted together rather than two at a time.

| what is billed | price | which rows say so |
|:---|:---|:---|
| an opaque smooth-shaded pixel | 22 cycles | `f_bg`: 382,731 px in 29.3 ms, whose 5 triangles and 2,495 spans leave 28.8 ms of it |
| a blended smooth-shaded pixel, mode recognised | 77 cycles | the four-row fit above, on `f_ribbon`'s current 49.8 ms fill |
| a blended smooth-shaded pixel, mode *not* recognised | 110 cycles | the same fit on the binary before `TGL_BLEND_VARS` knew `GL_DST_COLOR`/`GL_ONE` (61.3 ms fill) -- and the difference, 33.3 cycles, is the one price here that needs no fit: it is the 11,550 us the two 32x32 rows differ by, over 102,320 px.  That is the per-pixel switch dispatch, and it is what the recognition buys |
| a blended *textured* pixel | 190 cycles | `f_menu`: 38.8 ms − 646 setups − 8,964 spans = 29.9 ms over 46,441 px |
| a triangle's gradient setup | 10.9 us = 3,206 cycles | the four-row fit; 1,828 of them are 19.9 ms of `f_ribbon`'s 49.8 ms fill, so this is the third-largest line in the frame and not a rounding error on the pixels |
| a scanline step | ≤ 60 cycles | `f_bg`'s 2,495 spans leave under 2% of its row unexplained by pixels |
| a vertex (transform, viewport, clip) | 11.0 us = 3,240 cycles | `f_ribbon`'s `geom` over its 1,984 verts, current binary |
| a vertex that needs no clipping | 8.3 us = 2,440 cycles | `f_menu`'s `geom` over its 1,436 verts: same pipeline, no `gl_clipEdge` lerps |
| a cleared pixel | 2.6 cycles | `f_clear`: 128,000 px/ms |
| a pixel copied RAM to RAM | 14.8 cycles | the compositor's own `blit=` rows: 613,440 px in 30,747 us, and 50.2 ns/px on every row from 768 px to 613,440 px -- three orders of magnitude of size at one price, so it is the DRAM, not the loop |
| a swap | 0 | it was 10.5 ms -- the `swap=` column of every frame row, one uncached read and write per pixel for a constant byte -- until the byte began travelling with the pixel that stores it.  See *The frame buffer's alpha byte is written at the store* |

The copied-pixel row is what closes one obvious candidate for `f_bg` before it is written.  Drawing a
static gradient and blitting a cached copy of it back are the same order of work on this machine (22
cycles vs 14.8), because both are limited by touching 1.5 MB of RDRAM rather than by the arithmetic in
the middle: a write-only fill is the only one that is four times cheaper, and it is what `f_clear`
already does.  So the background cannot be made cheaper per pixel; it can only be made to cover fewer
of them, which is consequence 2 below.

The two terms that are *not* per-pixel were both assumed to be software floating point, and the
census says that assumption is wrong.  The EE's FPU is single-precision only, so the whole image
is `-msoft-float` (see *The FPU Law*) and every float operation is a call into
`src/drivers/ps2/ps2_builtins.c` -- but the calls are not the whole of the 3,206 and 3,240 cycle
figures.  Counted per stage on the same source, a steady frame asks for 578,963 of them, and at
the prices the `[FPU]` rows measure (see *The census: what one frame asks the float engine for*)
that is 58.8 ms against a 173.8 ms frame -- a 34% share, and one that grows as everything else
shrinks.  What is left of both windows is integer: the edge walk, the fixed-point span
interpolation, the blend, and the load/store traffic of the vertex record itself.

Read as a ledger, with the float term from the census and the remainder measured, the steady frame
is:

| stage | ms | soft-float calls | ms of that | ms integer |
|:---|--:|--:|--:|--:|
| `f_surface` (the wave's lattice) | 18.5 | 159,528 | 15.9 | 2.6 |
| `f_clear` | 3.4 | 134 | 0.0 | 3.4 |
| `f_bg` | 29.4 | 1,302 | 0.1 | 29.3 |
| `f_ribbon` | 71.5 | 269,596 | 27.5 | 44.0 |
| `f_particles` | 0.1 | 294 | 0.0 | 0.1 |
| `f_menu` | 50.7 | 148,109 | 15.3 | 35.4 |
| draw | 173.8 | 578,963 | 58.8 | 115.0 |
| swap | 0 | – | – | – |

The swap row is what the alpha-at-store law took back: it used to read `10.5 | – | – | 10.5`,
a whole-surface walk that appeared in no stage row above because it is not drawing.

`f_surface` is 86% floating point -- it is arithmetic that happens to be in float rather than
rastering -- and it is the only stage where making the arithmetic cheaper is close to making the
stage cheaper.  Of the three raster stages, `f_ribbon` is 38% float and `f_menu` 30%; `f_bg` is
0.3% (1,302 calls in 29.4 ms), i.e. it is a pure pixel cost.

Four consequences are normative for where work goes next:

1. **The float layer's ceiling is 1.51x, measured, not argued.**  Delete every one of the 578,963
   calls -- an infinite-speed FPU -- and the draw falls from 173.8 ms to 115.0 ms.  (13,902 of those
   calls are `div.s`, billed here at an add's 28 cycles because the `[FPU]` chain prices the four
   single-precision classes together; if a divide is dearer the float share rises by at most a
   millisecond or two, so the ceiling moves toward 1.6x and not beyond it.)  The frame the user
   needs is 16.7 ms, so no amount of arithmetic work on this layer reaches even a 2x, let alone the
   5x asked for.  Every further float change must be justified against 58.8 ms, and the
   order-of-magnitude answer is the GS, not the CPU.
2. **The static background is the largest thing a CPU-side change can still take back, and only
   fewer pixels count.**  `f_bg` re-rasters the same 382,731 gradient pixels every frame at 22 cycles
   each, and `f_clear` fills the same 384,000 again, because the drawing model is "rebuild the whole
   surface", not "repair what changed" -- 32.7 ms, 19% of the frame.  What it cannot be is a cheaper
   representation: the copied-pixel row above (14.8 cycles) says a cached blit of the baked gradient
   would save 7 cycles a pixel and cost 1.5 MB.  What it can be is a smaller *area*: everything that
   moves in a steady XMB frame is inside one full-width horizontal band, measured as 28.7% of the
   canvas from the composited PPM's bounding box (y 283..454 of 600), so a band-limited repaint of
   these two stages is worth ~23 ms (13% of the frame).  Three facts about the mechanism, all of them
   about this tree rather than about the idea: tinygl has no `glScissor`, and `glViewport` is the only
   rect that clips a triangle (`clip.c`'s own comment says coordinates are viewport-clipped by the
   time a triangle reaches the rasteriser); `glClear` is whole-plane, and the only fill cheap enough
   to replace it is an opaque quad at the 2.6-cycle cleared-pixel price; and the surface does persist
   across frames -- the TinyGL backend has no `swap_buffers` at all now, so nothing walks or rewrites
   the plane between frames, its alpha travels with each stored pixel, the window plane is allocated
   once and recreated only by `rsz_wnd()`, and neither the WM's background/decor fills nor the cursor's
   save-restore ever write it -- so not painting outside the band leaves the right pixels there rather
   than garbage.  The part that does not exist yet is how the app is told a
   full repaint is due (an entry fade changes the gradient's alpha, a selection change redraws the row
   text, `inval_wnd()` currently marks the whole window every 16 ms), and `paint(WND*, GDEV*)` carries
   no damage rect to answer with.
3. The span loop is not a lever -- the `f_bg` row is the control that proves a scanline step is
   noise, so a claim that short spans are what a fine mesh pays for is false and must not be spent
   on -- and the mesh dial is not one either, because it has been priced and lost: 32x32 -> 16x16
   is worth 47.0 ms and costs 10.81% of the sheet's pixels.  See the dial paragraph at the end of
   this section.
4. **A third of the fill is per-triangle, not per-pixel, and it is library-internal.**  1,828 setups
   at the fitted 10.9 us are 19.9 ms of `f_ribbon`'s 49.8 ms fill, and `f_menu`'s 646 add 7.0 ms of
   its 38.8 ms: ~27 ms, 15% of the frame, in `ztriangle.h`'s setup block rather than in its span
   loop.  Two thirds of a setup is integer (the y-sort, the fixed-point deltas, the five span-init
   values) and about 1,570 cycles is the gradient itself -- five interpolated channels, each needing
   two float multiply-subtracts and two `float→GLint` casts, at the census prices of 28 and 47 cycles.
   That block is bit-risky ground: its answers come from `(GLint)(fdy2*d1 - fdy1*d2)` after `fz` has
   been *divided* through, so an integer or fixed-point rewrite would round in a different place than
   the float path does, and the ribbon's span gradients are what the render test's PPMs compare.  This
   is the line to attack only with the 12-PPM byte-identity gate in hand, and it is smaller than
   consequence 2 for the same risk.

The first entry in the table to move was `f_menu`: its fill was 78.3 ms until `clip.c` learned to
route a textured triangle whose three vertices carry the same interpolated `z` to
`ZB_fillTriangleMapping()` instead of `ZB_fillTriangleMappingPerspective()`.  There, 1/z is
constant over the triangle's area, so the perspective path's per-scanline re-derivation of the
texture steps has a constant answer and the integer `INTERP_ST` gradients are the same
interpolation exactly rather than an approximation of it -- which is what a 2D interface under
an orthographic projection always is.  78.3 → 41.7 ms for 36.7 ms of frame, at a cost of 0.02%
of the frame's pixels (94-157 of 576,000, per frame), each of them a one-texel column on one
vertical edge of a glyph or icon where the integer step and the truncated float tie-break
differently; worst channel delta 102/255.  That is the trade to re-open if the type ever reads
wrong on a real display, and the `px/span` column is the row that says which other layers would
benefit the same way.

The second is the price of `(GLint)`.  Float -> integer is the one conversion in
`ps2_builtins.c` with no COP1 form -- the EE has no `trunc.w.s`, which is why it stayed on the
generic bit engine after add/sub/mul/div/compare went to the hardware FPU -- and what the engine
did with it was decode through a format descriptor and carry the significand as a `uint64_t` in
32-bit halves, so every `(GLint)` cast cost a chain of multi-word shifts.  A binary32 truncate is
two shifts once the exponent field says which band the value is in, and `ps2_sf_fixsfsi()` now
reads the field and emits 32 straight-line instructions with no call at all.  The answers are the
same: `make test-ps2-softfloat` is bit-exact over 15,225,197 comparisons, of which 3,072 are the
sweep added with this change (every exponent field 0..0xFF, times the three mantissas that can
land on a band boundary, times both signs, against the host's own cast), and that sweep was shown
to discriminate rather than merely to pass by moving the 158-exponent saturation line to 159 --
694 comparisons failed, including ones the older random pools found -- and then moved back.

The frame agrees with the fit, which is the part worth keeping: 2,479 setups x 3.4 us + 3,424
vertices x 3.7 us + `f_surface`'s lattice 3.5 ms = 24.6 ms predicted from the two prices, against
24.4 ms measured (`draw_us` 221,857 -> 197,393 in `build/ps2-smooth-history.tsv`, and `frame1_us`
224,475 -> 200,203).  A conversion whose result cannot differ cannot change a pixel, so this
saving has no visual price to watch, unlike the affine one above.

The third entry to move was a double that had no business being there.  `gl_clipcode()`'s guard
band was written `w1 * (1.0 + CLIP_EPSILON)`: the literal is a double, so the GLfloat operand was
promoted, the multiply ran on the bit engine, and the result was truncated back -- three double
calls and one float multiply, per vertex, for a guard band whose whole purpose is to be off by one
part in 100,000.  Writing it `w1 * (GLfloat)(1.0 + CLIP_EPSILON)` rounds the band to the operand's
own precision and answers the clip test in single: 10,713 double calls per frame went to zero, and
the A/B measured them (`draw_us` 210,066 -> 197,916 us on a counted build where both halves carry
the same counters, i.e. 336 cycles per double call including its conversions; `f_ribbon`'s geometry
window 28.9 -> 21.8 ms and `f_menu`'s 16.6 -> 11.9 ms clean).  Nothing else moved, which is the
control: the pixel prices above are the same ones the older binary measured, and
`make test-xmb-render-tgl` reports the same 68/70 with the same two pre-existing corner rows.  On a
target where `GLfloat` *is* double -- the hosted and Pi 400 builds -- the cast is a no-op and the
expression is bit-for-bit what it was.

The fourth entry to move was a mode that was recognised by nobody.  `TGL_BLEND_VARS` already hands
the alpha combination a branch-free arm, but recognition is an equality test on one specific
triple, so the ribbon's `glBlendFunc(GL_DST_COLOR, GL_ONE)` -- 102,320 blended pixels a frame, the
largest single pixel line in the image -- still fell to the general path, whose object code is
`switch(sfactor)`, `switch(dfactor)` and `switch(zbblendeq)` executed *per fragment*: an indirect
`jr` through a jump table plus three reloads of the parameters from stack slots the span loop keeps
spilling to.  Adding the second recognition (`TGL_BLEND_BRIGHTEN`: source channel times the
destination's own byte, plus the destination, saturating) moved `f_ribbon`'s fill from 61,328 us to
49,778 us and the steady frame from 185,559 us to 173,750 us, while `f_bg`, `f_clear`, `f_surface`
and the ribbon's own `geom` column did not move at all -- 34 cycles per blended pixel, which is
exactly the difference between the two prices in the table above, and the second measured proof
that in this rasterizer the dispatch, not the arithmetic, is what a blended fragment pays for.

Nothing about the picture changed, and that was shown three ways rather than argued.  Per channel
the fast arm and the general arm differ in two deliberate places -- the multiplier byte drops its
`& 0xff`, because a `GET_*REDER` result is one byte already at bit 16 and can be no wider, and the
clamp drops its `imp > 0` arm, because an unsigned sum of a shifted product and a channel is never
negative -- so a sweep over every sum the expression can produce (33,488,895 values, 0 mismatches)
and every (source channel, multiplier byte) pair (1,051,392 pairs, 0 mismatches) covers both.  Then
the shipped frame itself: the twelve PPMs `make test-xmb-render-tgl` writes are byte-identical with
the recognition on and off, and that comparison was shown to *discriminate* by adding 1/255 of red
inside `TGL_BLEND_BRIGHTEN`, which moves exactly the three frames the ribbon appears in
(`xmb_sheet_on`, `xmb_sheet_lift`, `xmb_particles`) and none of the other nine.

The mesh dial has now been measured on both sides, and it does not ship.  16x16 is worth 47.0 ms
of the steady frame on the shipped binary (173.8 -> 126.8, the same run that provides the control row
of the fit above) and 53.2 ms on the one before it (197.4 -> 144.2), and the price is in the two rows
the dial moves: `make test-xmb-render-tgl` at each setting, diffed frame by frame, gives 10.81% of
the sheet-on frame's pixels different with a worst channel delta of 145/255 (6.40% of its bytes),
against 0.02% for the affine trade above -- three orders of magnitude, and it is the wave itself:
the frames with no sheet in them are byte-identical.  The surface's own field says the same thing
from the geometry side: the shader's fragment term runs 0.0355..0.1453 over the 1,024 vertices of
the shipped mesh and 0.0358..0.1117 over the 256 of the coarse one, so the coarse sheet loses
23% of the light on its steepest fold while keeping the floor.  `verify/tests/test_xmb_render.c`
already refuses `n < 32*32` as a law rather than a preference ("a sheet sampled at fewer than
32x32 points is no longer the same surface"), and the diff is what makes that refusal a measurement
instead of an assertion.  What is left on this layer is therefore not the arithmetic -- the ledger
above puts the whole float layer, setups and vertices and the wave's lattice together, at 58.8 ms of
a 173.8 ms frame, so the ceiling of that line of work is 1.51x -- and the two things with a number
big enough to matter are the GS, which takes the 115.0 ms of pixels off the CPU, and repainting
what changed instead of 1.39 screenfuls every frame, which is the only CPU-side change that can
still remove tens of milliseconds.

### The wave that was not moving: a stall guard that had become a frame budget

"The 3D surface is not moving" is three claims, and the frame timings above answer none of them.
`src/apps/xmb.c` now prints the row that separates them, for the first six paints of a session:

```
[XMBT] wave n=2 dt=354 ms t1024=301 mid1024=335713 dm=1383 dsum=-1262
[XMBT] wave n=6 dt=338 ms t1024=1203 mid1024=336302 dm=1194 dsum=-5091
```

`dt` is the elapsed time the animation was stepped by, `t1024` the effect clock in 1/1024ths of a
unit, and `dm`/`dsum` how far the sheet actually went -- the middle vertex's screen y in 1/1024ths of
a pixel, and the same step summed over all 1,024 vertices.  Before the change the same row read
`dt=16 t1024=+10/frame dm=49..53 dsum=+-40`, and the reason is two defects stacked:

1. **A stall guard smaller than a real frame fires on every frame.**  `xb_paint` had
   `if (dt < 0 || dt > 250) dt = 16`, and the XMB's achieved cadence on this machine is 337-398 ms
   between paints (174 ms of it the app's own raster, the rest the desktop pass and the task's
   `dly_tsk(16)`).  So the guard replaced the true elapsed time with a nominal 16 ms *every single
   frame*, and everything that consumes `dt` -- `xb_tween_tick`, `xb_particles_update`, the launch
   hold -- ran at 1/21 of real time.  A 167 ms menu tween took ten frames, i.e. 3.5 s.  The clock
   itself was never the problem: `get_tim()` is `ps2_task_ms()`, which accumulates real CP0 `Count`
   microseconds, and it reported 354/398/337 ms correctly while the app threw the number away.  The
   bound is now 4,000 ms, which is a stall and not a slow frame.
2. **The effect clock was per drawn frame, so the wave's speed *was* the frame rate.**  The original
   advances `iTime += 0.01` once per frame (gfx_display.c:874), which is a speed only while frames
   come at the 16 ms cadence the bar was tuned at: 0.6 clock units a second, ~3 px/s at the sheet's
   fastest vertex.  At 2.7 frames a second the same 0.01 is 0.016 units a second -- 0.13 px/s, a
   surface that reads as frozen.  It is now `s_effect_time += dt / 1600.0f`, which is the same
   sequence of values at the designed cadence and real time at any other.

Two things made this safe to change rather than merely plausible.  First, the increment is
bit-identical where the cadence is as designed: `16.0f / 1600.0f` is the same float as `0.01f`, so a
target that hits 16 ms frames gets the exact `s_effect_time` sequence it had before -- and
`verify/tests/test_xmb_render.c` drives exactly that (`g_clock += 16` per paint), which is why 10 of
its 12 PPMs are byte-identical across the change.  The two that differ are the two taken after the
test's own deliberate 600 ms jump, where the new behaviour is the point: the particles frame differs
in 62,970 px confined to `y 283..480` (the sheet's band) and the system-data frame in 7,665 px with a
worst channel delta of 9 (a list fade that has correctly got further along).  Second, raising a clamp
means trusting the unit of the clock underneath it, because a clamp that small had been silently
capping any core whose `SYSTIME` was not milliseconds; the three cores that link `src/apps/xmb.c`
were read instead of assumed -- `core_arm64.c` ticks at 1 kHz and says so, `core_posix.c` computes ms
from `gettimeofday`, `core_ps2.c` derives ms from `Count`.

After: the same six paints step the clock by 217-246 in 1/1024ths per frame instead of 10, `dm` runs
+-288..1383 (0.3-1.35 px, sign flipping as the vertex crosses its extremum), and `dsum` -1262..-5091
against the +-40 the frozen sheet measured.  The frame costs what it cost -- 172,773 us against
173,421 us, unchanged within run-to-run noise, for one divide per frame.  What this does *not* buy is
smoothness: an animation whose wall-clock duration is 167 ms cannot be shown in more than one frame
by a machine that paints every 340 ms, so the menu now snaps instead of crawling, and the only way to
get frames back for it is the pixel work above and the GS below.  `make test-ps2` is 54/54.

### The census: what one frame asks the float engine for (Normative)

The stage timings say *when* a frame is slow; they cannot say whether a given window is float or
integer, because the two interleave inside one call.  So the libcalls are counted themselves, in
seven classes, when the image is built with `-DBTRON_PS2_FP_COUNT=1` (`make ps2-bench
PS2_FP_COUNT=1`, or `SMOOTH_FP_COUNT=1 scripts/ps2_smooth.sh`): `ps2_builtins.c` increments
`btron_fp_calls[class]` on the way in, and `xb_phase_census()` in `src/apps/xmb.c` prints the
difference between two consecutive stage boundaries, which is that stage's own bill.  The row is
self-checking -- `S=` and `D=` sum their own columns -- so a stage whose split does not add up
(which is how a mixed `PS2_FP_COUNT` rebuild, one object compiled under a different flag than its
neighbours, used to show up without anyone looking for it) cannot be mistaken for a measurement.

```
[XMBT] census f_ribbon  fmul=168817 fdiv=4060 fcmp=20239 fs2i=38988 fi2s=37492 S=269596 dmul=0 dgen=0 D=0
[FPU]  16000-op soft-float caller: COP1 34 cycles/op  bit engine 542 cycles/op
[FPU]  conversion price: float->int 53 cycles/op (4000 calls)  int->float 29 on COP1 / 229 bit engine (4000/4000)
```

Subtracting the 6.3 cycles a counted call pays for the counter itself (measured on one binary both
ways: the same steady frame is 197,969 us counted and 185,559 us not, over 578,963 counted calls --
the uncounted half of that pair is the frame the `TGL_BLEND_BRIGHTEN` recognition then moved to
173,750 us, which is why the overhead is quoted from the pair rather than from the current rows)
gives the prices the ledger uses:
**a single add, multiply, divide or compare is 28 cycles, float -> integer is 47, integer -> float
is 23, and a double is the bit engine's 536.**  Three consequences, each of which closes a door:

1. **The two directions of conversion are not one op.**  `int -> float` is `cvt.s.w`, which COP1
   answers, so it costs what an add costs.  `float -> int` has no EE instruction at all -- there is
   no `trunc.w.s`, and `cvt.w.s` only under a rounding mode that would corrupt every other
   operation -- and runs the exponent-band truncate in `ps2_sf_fixsfsi()`, which is 47 cycles of
   straight-line integer shifting: under two float adds, and nothing like the 536 the bit engine
   charges for arithmetic.  Handing COP1 a float -> int fixup was therefore never a win, and the
   two columns stay apart in the census so that the question is not asked again.
2. **A steady frame contains no double operations at all** (`dmul=0 dgen=0` in every stage), which
   is what the clip-code row above bought.  This is also the check that catches a regression: any
   future literal written into the hot path shows up as a non-zero `D=` before it shows up as
   microseconds.
3. **`fi2s` is a fingerprint for the fill setup.**  Integer -> float happens in `ztriangle.h`'s
   gradient block and nowhere else in the raster path, so that column over the `tris` column is the
   setup's own conversion count: 20.5 per triangle for the smooth fill (`f_ribbon`) and 23.2 for the
   mapping fill (`f_menu`).  It is the first independent confirmation that the 12.3 us setup price
   measures a real per-triangle cost rather than a fit between two mesh rows.

The counted frame is 12.4 ms slower than the shipped one, so **no timing in this file comes from a
counted build**: counts are read from `PS2_FP_COUNT=1`, microseconds from `PS2_FP_COUNT=0`, and
`scripts/ps2_smooth.sh` deletes every `*.ps2.o` and `*.pbench.o` when the flag changes, so one run
cannot mix two builds.

### Driver Files

| File | Role |
|:---|:---|
| [`src/drivers/ps2/boot_ps2.s`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/boot_ps2.s) | EE reset vector, `$gp` and `$sp` setup, unrolled BSS wipe, BIOS syscall wrappers (`SetGsCrt`, `PutIMR`), jumps to `ps2_kernel_main`. |
| [`src/drivers/ps2/ps2.ld`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2.ld) | Memory layout script linking `.text` at `0x00100000`, and the `ASSERT` that fails the link when `_end` reaches the stack or the 32 MB RDRAM end. |
| [`src/drivers/ps2/ps2_gs_reg.s`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs_reg.s) | The image's only 64-bit-GPR instructions: `ps2_gs_poke64()`/`ps2_gs_peek64()` perform the GS's single-`sd`/single-`ld` register latch. Assembled with local `.set mips3` under the shared `-march=mips2` driver; its arguments and return halves are 32-bit, so it is O32-legal on both sides. |
| [`src/drivers/ps2/ps2_builtins.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_builtins.c) | The whole soft-float runtime the image links against: the 32 `__*sf3`/`__*df3`/conversion libcalls clang invents at `-msoft-float -nostdlib`, a bit engine that serves both IEEE formats from 32-bit pieces, and the single-precision entry points that pick COP1 at run time.  The only unit built `-mhard-float`, and the only one allowed to contain a float instruction (tests 39-44). |
| [`src/drivers/ps2/ps2_gs.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.h) | Privileged GS registers (`0x12000000`: `PMODE`, `SMODE2`, `DISPFB1`, `DISPLAY1`, `CSR`) and GIF DMA registers (`0x1000A000`), reached only through `GS_REG_POKE`/`GS_REG_PEEK`. |
| [`src/drivers/ps2/ps2_gs.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.c) | Hardware display initialization, VSync synchronization (`ps2_gs_vsync`), Host-to-Local GIF DMA blitter (`ps2_gs_flush`) over the desktop's own canvas, and non-destructive cursor restoration. |
| [`src/drivers/ps2/ps2_font.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_font.h) | 8x8 ASCII bitmap font table (128 characters) for crisp, authentic BTRON UI text rendering. |
| [`src/drivers/ps2/ps2_sio.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.c) | Cleanroom SIO0 hardware UART driver (`0x1000F180` / `KPUTCHAR`). |
| [`src/drivers/ps2/ps2_pad.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.c) | Cleanroom DualShock 2 controller driver: analog stick velocity integration, deadband filtering, button edge detection, and event mapping. |
| [`src/drivers/ps2/ps2_usb.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.c) | Cleanroom USB Open Host Controller Interface (OHCI) driver (`0xBF801600`) and standard USB HID Boot Protocol keyboard/mouse decoders. |
| [`src/cores/core_ps2.c`](file:///Users/tonpa/depot/bitedits/btron/src/cores/core_ps2.c) | Platform core adapter: multi-window application suite (Workbench, B-Editor, TAD Cabinet, Settings), Japanese TIP/IME status badge, interactive SIO0 shell, event queue, and RTOS heartbeat. |
| [`scripts/test_ps2.sh`](file:///Users/tonpa/depot/bitedits/btron/scripts/test_ps2.sh) | Automated verification suite checking ELF architecture, entry point, driver symbols, R5900 opcodes, the one-calling-convention ABI law (image *and* every object at 32-bit GPRs, the GS accessors at one `sd`/one `ld`), the FPU law (COP1 instructions in `ps2_builtins.ps2.o` and in no other object, no double-width op and no EE-illegal `.s` encoding either by mnemonic or by byte scan, the hardware path provably compiled in, no named undefined symbol, all 32 soft-float libcalls defined), the trig fold's shape (a large float angle folded with no software double division, read off a probe object built with the image's own flags), the single-canvas law (no `ps2_fb_memory`, no present-path sweep, exactly one screen-sized CT32 object and that it is `s_desktop_backbuffer`, every CP0 write to `Status` and no `cache` op) and the ISO image (54/54 tests). |

### 2.1 Graphics Synthesizer (GS) Framebuffer Architecture

The PlayStation 2 Graphics Synthesizer contains 4 MB of ultra-high-bandwidth embedded DRAM (eDRAM). Memory inside eDRAM is structured into **pages** (2048 32-bit words = 8192 bytes) and **blocks** (64 words = 256 bytes) with non-linear column-interleaved pixel swizzling.

#### The Host-to-Local GIF DMA Blitter Architecture

Rather than rendering individual primitives through the GS rasterizer (which is prone to sub-pixel rounding errors, page-boundary clipping bugs, and high DMA packet overhead), B-System uses the hardware **Host-to-Local Blitter**:

1. **One canvas: the desktop's own backbuffer**:
   The GIF DMA reads the very array the compositor painted -- `core_ps2.c` installs
   `s_desktop_backbuffer` into the driver with `ps2_gs_set_framebuffer()` as the first act
   of `ps2_kernel_main()`, before the SIO and GS come up, because the boot log is written
   into the canvas the screen shows.  There is no second framebuffer and no copy between
   them: the array the driver used to own (`ps2_fb_memory`, 800x600x4 = 1,920,000 bytes of
   `.bss`) and the one-word-per-pixel sweep that filled it (`blit_backbuffer_rect_to_ps2fb`,
   49,094 us of a full present) are both gone.  What makes that sound is that **this port
   never enables the Emotion Engine's L1
   caches**: every coprocessor-0 write in the image is to `Status` (CP0 register 12 select
   0), and there are exactly three of them -- `btron_fp_init()` raising `CU1`,
   `ps2_ee_enable_interrupts()` and `ps2_ee_exit_handler()` setting the EE's global enable
   bit 16 (`EIE`) by read-modify-write, because PCSX2 does not decode `ei` (`Unknown R5900
   COP0`).  Nothing is written to the cache-control
   registers, and there is no `cache` maintenance instruction anywhere, so the caches stay
   in the state the BIOS left them in -- and the upload path reads its source through the
   uncached KSEG1 alias regardless (`UNCACHED()` in `ps2_gs.c`).  Every alias of a given
   address reaches the same RDRAM and a bus master sees a store as soon as it lands.
   `make test-ps2` asserts that premise rather than assuming it: the absence of a
   `ps2_fb_memory` symbol and of the sweep, exactly one screen-sized CT32 object in the
   image and that it is `s_desktop_backbuffer`, `ps2_gs_set_framebuffer` present as the
   install point, that the disassembly's `mtc0` instructions are *all* `Status` writes and
   that none of them names register 9 -- `Count` is the clock this port measures, and
   writing it would break every elapsed time in the image -- and no `cache` op.

2. **The canvas's byte order**:
   A BTRON `COLOR` is `0xAARRGGBB`, whose little-endian bytes are B,G,R,A; a GS CT32 pixel
   is R,G,B,A in memory.  Neither the GIF's Host->Local bit transfer nor the PCRTC's
   display read can permute channels, so the exchange has to live in the port, and it lives
   at the moment each colour enters a pixel array: `BTRON_PIXEL_FB()` in
   [`include/btron/types.h`](file:///Users/tonpa/depot/bitedits/btron/include/btron/types.h)
   swaps the R and B fields, is its own inverse, and is the identity on every non-PS2
   target.  The rule is closed under copying -- a pixel array written through it and read
   back only as words (the compositor's spans, row blits, background cache, cursor save
   area, and the GIF upload itself) needs nothing -- which is why the conversion is a leaf
   store and a component-reader detail rather than a pass over the canvas.
   `make test-ps2-fb-order` draws a saturated colour through each primitive and checks the
   word that lands.

3. **GS Transmission Setup Packet (`ps2_gs_flush`)**:
   - A 5-QW setup packet primes the GS:
     - `BITBLTBUF (0x50)`: `DBA = 0`, `DBW = 10` (640 / 64), `DPSM = GS_PSM_CT32 (0)`.
     - `TRXPOS (0x51)`: `DSAX = 0, DSAY = 0`.
     - `TRXREG (0x52)`: `RRW = 640, RRH = 448`.
     - `TRXDIR (0x53)`: `0` (Host $\rightarrow$ Local eDRAM).

4. **Hardware Swizzling on Arrival**:
   - The canvas is streamed via DMAC Channel 2 in 8 chunks of 8,960 quadwords with `GIFTAG(FLG = IMAGE)`.
   - The GS internal blitter automatically and correctly swizzles linear RDRAM pixels into eDRAM pages and blocks on arrival.

5. **Verified NTSC Video Timings**:
   - `ps2_set_gs_crt(1, 2, 0)`: Interlaced, NTSC, Field mode.
   - `PMODE = 0xFF63ULL`: Circuit 1 + Circuit 2 enabled, `SLBG = 0` (Framebuffer output selected).
   - `SMODE2 = 0x01ULL`: Interlaced, Field mode.
   - `DISPFB1` & `DISPFB2 = 0x1400ULL`: Base address page 0 (`FBP = 0`), buffer width 10 blocks (`FBW = 10`), 32-bit RGBA (`PSM = 0`).
   - `DISPLAY1` & `DISPLAY2 = 0x001bf9ff0983227cULL`: $DX=636$, $DY=50$, $MAGH=3$ (4x), $MAGV=1$ (2x), $DW=2559$, $DH=447$.

6. **Non-Destructive Cursor Restoration**:
   - Mouse cursor drawing backs up background pixels into `cursor_saved[16 * 16]`. Erasing the cursor restores the exact original background pixels with zero smearing or artifacts.

#### The EE/GS backend (plan of record)

Every number in *What a pixel costs, measured* is a CPU number, and the ceiling they add up to
is the reason this exists: the frame the user needs is 16.7 ms, `f_bg` alone is 29.4 ms of
touching pixels, and the float layer's whole measured ceiling is 1.51x.  The GS is the only
hardware rasteriser this machine has, so the answer to a per-fragment cost is to stop paying it
on the EE.  The backend will be written against `third_party/ps2sdk/` as the register and packet
*reference* only -- `ee/draw/include/*.h`, `ee/libgs/include/libgs.h`, `common/include/gif_tags.h`
and `common/include/gs_gp.h` are header descriptions of hardware, and no `.c` from that tree is
linked or copied, in this or any later increment.

Four facts fix the shape before any code is written, all of them measured or read from this tree:

1. **The surface an app renders into is the compositor's blit *source*.**
   `egl_create_window_surface()` sets `surf->pixels = wnd->dev->pixels`
   (`src/gl/egl_surface.c:40`) and `redraw_all_windows_clip()` blits
   `src = wnd->dev->pixels` into `g_screen_dev->pixels` row by row (`src/window/wnd.c:539`).
   The GS cannot write EE memory: its primitives land in its own local VRAM.  So a GS backend
   cannot keep that arrangement -- the GL window's contents stop being an array the CPU can copy
   and become something the GS itself composes.  This is the one architectural consequence of the
   change and every other decision follows from it.
2. **What the apps actually ask for is a 2D UI, which the GS does natively.**  `xmb.c` uses 18 of
   the `gl_ops_t` entry points: 2D vertices, affine texture coordinates (`glTexCoord2f`, never a
   `q`), per-vertex RGBA, `GL_QUADS`, `glBlendFunc`, one bound texture per quad, and no depth at
   all.  Gouraud shading (`PRIM.IIP`), affine texture interpolation (`TEX0`/`TEX1` with
   `MMIN`/`MMAG`), and blending against the destination already in VRAM (`ALPHA` with
   `BLEND_COLOR_DEST`, enabled by `PRIM.ABE`) are all hardware, which is exactly the split the
   XMB wants: the per-fragment work moves to the GS and only per-vertex arithmetic stays on the
   EE.  `glgears.c` is the counter-example and stays on TinyGL: it needs lighting and display
   lists, for which the GS has no hardware.
3. **The GS's coordinate window is 12.4 fixed-point around a 2048 offset.**  `GS_SET_XY` packs
   `ftoi4(x) = x*16` into 16 bits with `XYOFFSET` at the page origin (`gs_gp.h`, `draw_types.h`),
   so vertices are integer-quantised at 1/16 px -- invisible for a 768x500 UI, and the reason a
   first increment does not need a VU program: an EE-side `float`-to-`16.16`-to-packet conversion
   per vertex is enough, and the vertex count is 1,984 + 1,436 per frame, not 384,000.
4. **The port already owns the DMA path it needs.**  `ps2_gs.c` builds GIFTAGs, primes
   `BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR` and streams DMAC channel 2 to the GS today; the
   primitive path is the same pipe with `FLG=PACKED` and a different REGS list
   (`PRIM, RGBAQ, XYZF2, UV/ST, TEX0, CLAMP, FOGA`).  Nothing here requires touching the driver's
   blit path, and the canvas upload stays as it is.

**Selection is a user setting *and* a build flag, and both name the same three answers.**  The row
is the two-choice control

    OpenGL PS2 : [x] TinyGL   [ ] EE/GS

following the existing radio-pair idiom (`g_wm_style`/`pmc_set_style()` declared in
`include/btron/pmc.h`, drawn in `src/settings/appearance.c:114-115`, mutated by a rect hit-test on
`EV_BUT_DOWN` at appearance.c:160-171).  The value lives with the GL layer, not in the settings
file, and in its own translation unit (`src/gl/gl_backend_choice.c`) because it has three callers on
two sides of a link boundary: `gl_backend_select()`/`gl_backend_selected()` are the settings
applet's, `gl_backend_default()` is the build's answer, and `gl_backend_software_default()` is the
rule that used to be the `EGL_DEFAULT_BACKEND` macro in `egl_surface.c` (now deleted; the three
cannot drift apart because there is one of each of them).  A choice nobody has made reads as the
build's answer, so the default image is the byte-for-byte behaviour this section described before
the row existed.  The row itself is
`#if defined(BTRON_PS2_TARGET)`-scoped, because `src/settings/*.c` is linked by every target
including the Pi 400, which links no GL source at all and must keep byte-identical behaviour -- and
is verified to: the row's three strings are in `btron-ps2.elf` and in zero bytes of
`btron-posix`.  The choice is remembered, not applied: `egl_create_window_surface()` asks
`gl_backend_selected()` at every surface creation (`src/gl/egl_surface.c:38`), so it takes effect
at the next GL window rather than under the one on screen, and `gl_resize`, `egl_make_current` and
`egl_destroy_surface` continue to branch on `gl_active_backend()` -- what is installed, never what
was requested.

The build flag is the Makefile's existing `PS2_GL=`, which had two values and now has three:
`tinygl` (the shipped image), `virgl` (the other software rasteriser, for the timing comparisons
the `[XMBT]` rows already make), and `eegs`, which makes the image start on the same backend the
row offers.  That is the point of it: an A/B against the GS pipeline is `make ps2 PS2_GL=eegs` and
not a walk to the Display applet, and the applet still outranks the build on any of the three.
Splitting the rule in two is what makes the third value expressible at all, because the placeholder
in `gl_dispatch.c` has to answer a GS request with a rasteriser that exists, and on this build the
*default* is the GS -- so a fallback to the default would be a call to itself.  Two laws of that
shape are read off the image (`scripts/test_ps2.sh` rows 44 and 45): `gl_init` contains a `jal` to
`gl_backend_software_default` and none to `gl_backend_default`, and the constant
`gl_backend_default` returns is less than 3.  Row 45 is written as a bound and not as `== 0`
because it is a law about the shipped image, and it has the behaviour you would want of a guard:
`make test-ps2 PS2_GL=eegs` fails it with `GLDEF_RET=3` while row 44 still passes, which is the
true statement that this build boots, draws through a real rasteriser, and is not the one to hand
anyone as the default.  It is row 5's job to make 3 legal, and then this row retires.

A value the dial does not know is an error rather than a silent `tinygl`, for the reason every
measurement law in this file gives: a typo builds an image that works, prints timings, and describes
a pipeline nobody asked for.  And because `make` does not see a *change* of flag -- the three
objects that read `PS2_GL` stay newer than their sources and get linked into an image built for the
other value -- the value itself is stamped into `.build/ps2-gl.stamp` and compared by content, with
those objects deleted when it disagrees.  This is the same law `scripts/ps2_smooth.sh` already
applies to `PS2_FP_COUNT`, in the Makefile so that a bare `make ps2 PS2_GL=eegs` is covered by it
rather than only the runs that go through a script.  The gate has to be handed the same value as
the build (`make test-ps2 PS2_GL=eegs`), because `test-ps2` depends on the image and would
otherwise rebuild it back to `tinygl` and grade a different artifact -- which is how the first
attempt at row 45's control passed while believing it was testing the eegs image.
`verify/tests/test_gl_backend_choice.c` is the other half of the same law, one compile per dial
value: the image rows can only see the value the shipped image was built with, and a flag that
selected nothing would look exactly like a flag that selected `tinygl`.

**Increments, each one leaving the image bootable and each with its own gate:**

| # | increment | the gate that says it works |
|:--|:---|:---|
| 1 | *landed 2026-10-12*: the setting and the runtime selection; `GL_BACKEND_EEGS` installs the build's default ops as a placeholder and says so on the console, so nothing can regress | `make -k test` green with five new assertions in `verify/tests/test_xmb_render.c` (default matches the define, un-chosen selection reads the default, `select(EEGS)` is remembered, `gl_init(EEGS)` lands on the linked rasterizer, the round trip back); shown to have teeth -- with `gl_backend_selected()` forced to return the default the third one reports `[FAIL]`; both builds of the suite still 75/75; `make test-ps2` 54/54; `scripts/ps2_bench.sh` on the same image gives `[GL] TinyGL ZBuffer init`, `swap 0`, `render 532 us/pass` |
| 2 | *landed 2026-10-12*: the EE-side packet builder -- `src/drivers/ps2/ps2_gs_pack.{h,c}`, nineteen encoders (`GIF_TAG`, `PRIM`, `RGBAQ`, `ST`/`UV`, `XYZ2`/`XYZF2` with the 12.4 `+2048` page offset, `TEX0`, `TEX1`, `CLAMP`, `XYOFFSET`, `SCISSOR`, `ALPHA`, `TEST`, `FRAME`, `ZBUF`, the REGS nibble packer, the AD-pair writer) and nothing else: no DMA, no register write, no caller | `verify/tests/test_ps2_gs_pack.c` #includes the builder and compares 35 words against literals hand-derived from the manual's field table; `make test-ps2-gs-pack` reports `checks=35 failures=0`, and it has teeth -- shifting `SCISSOR`'s `y1` from bit 48 to 47 produces exactly one `[FAIL] scissor 800x600  got=0x012c000003200000` and exit 1, then the clean source restores 35/0.  Two of the hand literals were wrong on the first run (a `UV` field past 14 bits, and `GIF_TAG`'s 2-bit `FLG`) and the encoders were right both times, which is the evidence the expectations are not computed by the code they check.  `make ps2` compiles the builder under the real `-msoft-float` flags, `make test-ps2` stays 54/54 (test 39 still finds COP1 only in `ps2_builtins.ps2.o`), and `make -k test` is green with the new suite in the aggregate |
| 3 | *landed 2026-10-12*: the drawing environment as one AD-pair stream -- `src/drivers/ps2/ps2_gs_vram.{h,c}` (the two page units below, a bump cursor, the canvas reservation) and `src/drivers/ps2/ps2_gs_env.{h,c}` (GIFTAG then `XYOFFSET`, `SCISSOR`, `FRAME`, `ZBUF`, `TEST`, `ALPHA`, `CLAMP`, `TEX0`, `TEX1`: nine writes, ten quadwords), with `ps2_gs_draw_env_apply()` in `ps2_gs.c` as the only part that touches the bus.  `ps2_dma_gif_send()` became the shared `ps2_gs_gif_send()` -- declared in `ps2_gs.h`, since increment 4 is the second producer on that pipe and a copy of the STR poll is what sharing exists to prevent.  Nothing calls the apply path yet, which is the increment's point: it programs registers the blit path never has, and the screen is unchanged | `verify/tests/test_ps2_gs_env.c` compares the built packet word-for-word and the allocator against hand arithmetic: `checks=59 failures=0`.  It has teeth, three controls each failing the one law it edited: swapping `TEST` and `ALPHA` in the stream -> `[FAIL] test reg`; dropping the 12.4 scaling on the page origin -> `[FAIL] xyoffset value got=0x000081400000800c want=0x00008140000080c0`; taking `FRAME.FBA` in the 256-byte unit -> 25 failures starting `[FAIL] fba atlas`, because the builder refuses a frame plan whose offset is not a whole 8 KB page.  `make ps2` compiles both units under the real `-msoft-float` flags and `make test-ps2` is 57/57 with three new rows read off the image: 39 the apply path calls the builder once and pokes no GS register, 40 the same path writes D2_CHCR, D2_MADR, D2_QWC and D_STAT, 41 the builder touches no memory-mapped register at all, which is what makes the host suite the right instrument.  Controls D and E on the image gate: replacing the send with `GS_REG_POKE(0x18, ...)` makes 39 evaluate `[ 1 -eq 1 ] && [ 1 -eq 0 ]` and drives 40's four counters to zero; one `*(volatile uint32_t *)0x1000a000 = 1` inside the builder makes 41's counter 1.  `make -k test` exits 0 with the suite in the aggregate |
| 4 | *landed 2026-10-12*: recorder: `src/drivers/ps2/ps2_gs_rec.{h,c}` -- a fixed-capacity GIF stream that takes a primitive's vertices and emits packed groups whose REGS list is `PRIM, RGBAQ, ST, XYZ2`, the vertex coordinates folded to 12.4 around the `+2048` page origin by the increment-2 encoder, and nothing else: it holds no matrix and no projection, because the modelview and lighting arithmetic belongs to the downstack row 5 hangs on rather than to the packet layer.  A full buffer is sent and a new group started rather than a vertex dropped -- the one exception is an `op` this recorder does not know, where the vertex is refused and `gs_rec_unsplittable()` counts it -- so capacity is a latency knob and the counter that matters is DMA starts per frame, see the capacity note below the table | host suite against hand-built words, the increment 2 and 3 discipline: a four-vertex quad in full (18 quadwords, every field from the field table), a two-vertex strip that latches a `glColor` set after the first vertex, a primitive change closing its group, and the boundary rule at all seven shapes it knows.  `verify/tests/test_ps2_gs_rec.c` #includes the recorder and the packer with `GS_REC_QW_CAP` overridden to 64 quadwords by the test's own `#define` -- the shipped 4,096 makes the split paths unreachable on a host that would rather write ten thousand vertices -- and `make test-ps2-gs-rec` gives `checks=126 failures=0` with no warnings.  It has teeth, the three promised controls one edit each: dropping the 12.4 scaling gives exactly 3 failures, `[FAIL] vertex 2 XYZ2: 10 px is 0xA0 sixteenths, not 10  got=0x000000008014800a want=0x00000000814080a0`, and the two (0,0) vertices stay silent because the page offset is the same either way; swapping `ST`'s halves gives exactly 1, `[FAIL] vertex 1 ST got=0x3f80000040000000 want=0x400000003f800000`; filling `NLOOP` with the quadword count instead of the vertex count gives 13.  Two bugs were caught by the suite rather than by reading, which is the evidence the expectations are not computed by the code they check: `open_group()` built its tag pair with `gs_pack_pair()`, the AD-form writer, so the REGS quadword arrived as its own low byte -- `[FAIL] REGS got=0x0000000000000010 want=0x0000000000005210`, a group the GIF would read as `PRIM, RGBAQ, PRIM, PRIM` and feed four data quadwords to the wrong four registers -- and `reemit_continuation()` took its second vertex from the sliding window that `write_vertex()` had just overwritten, so every continued strip and fan wrote its first re-emitted vertex twice: a well-formed packet, a degenerate first triangle, and a strip continuing from an edge it never had.  Three of *the test's* literals were wrong on the first pass too (the fan's tag, whose `PRIM` 0x25D sets the tag's bit 47 where 0x25C does not, and the POINT/LINE start counts, which are 12+12+6 in three starts and 12+2 in two, not the 12+18 guessed), corrected from the field table and the boundary arithmetic before the code was consulted.  `make ps2` compiles the recorder under the real `-msoft-float` flags and `make test-ps2` is 61/61 with its two increment-4 rows read off the image: 42 the recorder is linked (9 `gs_rec_*` symbols, 674 lines of disassembly), 43 the recorder is pure -- no `ps2_gs_poke64`, no `ps2_gs_gif_send`, no memory-mapped address in any of its instructions, which is what makes the host suite the right instrument for its words.  Nothing calls it yet, so `scripts/ps2_bench.sh` on that same image is the byte-for-byte image before it: `[GL] TinyGL ZBuffer init`, the XMB opening and animating, `run complete`.  `make -k test` exits 0 with the suite in the aggregate -- and the wiring was itself a trap worth recording: `test-ps2-gs-rec` was added to `.PHONY` and to `test:` before its rule existed, where `make` printed "Nothing to be done" and still exited 0, a suite that passes by not running |
| 5 | presentation, in three steps, and *not* as a second front end.  The step this section used to describe -- "`backend_eegs.c` implements the `gl_ops_t` table on the recorder" -- is the wrong shape, and the tree says so: `gl_ops_t` has 38 members, and everything in them that a GS backend would have to reimplement (the matrix stack, the viewport map, lighting, clipping, texture state, display lists) already exists and is already what the XMB's 20 calls and glgears' 15 exercise.  A parallel implementation would be a second model of the same laws, and the two would drift exactly where drift is invisible -- one binade of a projection, one rounding of a clip.  The seam is instead the one TinyGL already has: `GLContext.draw_triangle_front/back`, assigned per primitive mode in `src/gl/tinygl/vertex.c:101-123` and called from `src/gl/tinygl/clip.c:271-284` *after* clipping, handing three device-space `GLVertex` records.  So **5a** *landed 2026-10-12*, and it replaced only the span loops.  It is not a `gl_draw_triangle_gs()` hung on those hooks -- the hooks already point at three named functions, so the seam is one condition in each of them (`gl_draw_triangle_fill` at `clip.c:611`, `_line` at :721, `_point` at :747; in the fill it sits after the bound plane has been resolved and the fill accounted for, `clip.c:638`) that calls `eegs_triangle`/`eegs_line`/`eegs_point` of the new `src/gl/backend_eegs.{h,c}` and returns.  What it hands over is what the software fill was about to use: `zp.x`/`zp.y` unchanged, `zp.r/g/b/a` each as its top byte, `zp.s`/`zp.t` divided by the upload's own fixed-point scale, and the three PRIM flags read off the same three conditions the software dispatch branches on -- `current_shade_model` for IIP, "a plane exists" for TME, `zb->enable_blend` for ABE -- so no state is modelled twice.  A flat-shaded primitive is recorded with the *third* vertex's colour on all three of its vertices, because that is the colour `ZB_fillTriangleFlat` draws (`ztriangle.c:64-68`) and because a flat GS group's choice between first and last vertex is not something this port has an instrument for -- writing the same colour on all three makes the question unable to bite.  The line and point equivalents exist because the XMB's message-box border is a `GL_LINE_LOOP` (`xmb.c:1786`), and they are recorded untextured whatever the texture state is, because `ZB_line` and `ZB_plot` sample no texture.  **5b** puts the two textures the XMB binds into GS VRAM and points `TEX0` at them: the four transfer encoders (`BITBLTBUF`/`TRXPOS`/`TRXREG`/`TRXDIR`, offsets `0x50..0x53` already named in `ps2_gs_pack.h`) with the blit path's own hand-written shifts moved onto them, the icon atlas (a 384x384 upload into a 512x512 plane, 1,048,576 B, `xmb.c:1196`) and the font sheet (128x96 into 128x128, 65,536 B, `xmb.c:1202`) uploaded as IMAGE-mode (`FLG=2`) groups into the pages increment 3's allocator reserves, and `TEX0`/`TEX1`/`CLAMP` following `glBindTexture` -- which means mid-stream, as a recorder AD group, see the paragraph below the table.  **5c** is the present: the environment packet and then the recorded stream after `ps2_gs_upload()`, clipped to the GL window's rect, with `redraw_all_windows_clip()` ceasing to blit that window's surface into the canvas -- under `BTRON_PS2_TARGET` only.  One fact of the seam governs all three: `g_gl` is a single process-wide global (`gl_dispatch.c:21`) and TinyGL's ZBuffer is one static pair, so the image has one GL target, "last surface created wins", and no per-window backend choice exists to fall back on | **5a** is *landed*, host-gated in the increment 2/3/4 discipline and before anything on screen depends on it: `verify/tests/test_eegs_raster.c` #includes the raster stage, the recorder and the packer, and compares 42 checks against words written from the field tables -- the vertex at the measured far corner of the 768x500 client (`767,499` -> `0x000000009F30AFF0`), a `20.09375` px vertex whose 12.4 value is 321 and not 322, `RGBAQ`'s byte order, the hand tag a three-vertex group of that `PRIM` carries (`0x412DC00000008003`), three same-state triangles in one 38-quadword send, the state change that closes, and a twelve-triangle run in which every group's `NLOOP` is a whole number of triangles.  `make test-eegs-raster` gives `checks=42 failures=0` with no warnings and it has teeth, five controls one edit each: making `select_prim()` re-send unconditionally costs 6, starting `[FAIL] three same-state triangles are one send`; ignoring IIP costs 6, `[FAIL] ... PRIM 0x25B got=0x0000000000000253 want=0x000000000000025b`; scaling x by a half costs exactly the two pass-through checks, `got=0x000000009f3097f8 want=0x000000009f30aff0`; unpacking the packed colour high byte first costs exactly 1, `got=0x3f800000442233ff want=0x3f800000ff332244`; and dropping the frame's `gs_rec_init()` costs 11 failures out of the 14 checks the tests' own empty-capture guards still let run, the whole suite -- which is the control that says the suite is one instrument and not six independent ones, because a recorder that never met its frame keeps no sink, so it is a stream nobody reads and there is nothing left to compare.  The y-direction law this row called the one a hand literal and nothing else can pin turned out to need no pinning, which is the increment's most useful result: the coordinates are *passed through*, so there is no second viewport map here to be upside down, and those two checks measure the encoder's 12.4 fold and the absence of a re-projection rather than a projection of GS shape.  `make ps2` compiles the stage and the adapter under the real `-msoft-float` flags, `make test-ps2` is 61/61 with row 45 still `GLDEF_RET=0`, `make -k test` exits 0 with the suite in the aggregate, and `scripts/ps2_bench.sh` on that image is the byte-for-byte image before it -- `[GL] TinyGL ZBuffer init`, `total=56851 us render 56033/56057`, the XMB's three frames at 31/321/609 ms folded, `run complete` -- because nothing reaches this path until 5c installs the backend, exactly as rows 44/45 hold.  One thing found outside the increment's own files: `test-ps2-fb-order` compiles `clip.c` on the *host* with `-DBTRON_PS2_TARGET`, so the guarded reference made `backend_eegs.c`, `ps2_gs_rec.c` and `ps2_gs_pack.c` link inputs of that suite, and the Makefile comment's claim that nothing else in the tree reads `BTRON_PS2_TARGET` was the stale part -- and both of its binaries still normalise to the same 31,488 words, which is the first direct evidence that linking the GS stage changes nothing the canvas holds.  What that suite gates is the *stage* (`backend_eegs.c`); the *adapter* in `clip.c` that fills its struct has no host assertion and cannot have one yet, because `gl_active_backend()` cannot answer EE/GS on any build and the recorder's sink is installed only by 5c's present -- so the colour shift, the texel scale and the flat-shade source below are gated today by the bench's unchanged canvas and will be gated by the screen at 5c, which is why that step's gate says the user's eyes twice.  **5b**'s gate is the remaining half of that discipline: the transfer packet for a page-sized bitmap and the words that point `TEX0` at it, each against literals written from the field tables, plus the texel-space `ST` law this step finally makes load-bearing.  5c is gated by `scripts/ps2_smooth.sh`: the XMB's `draw=` column, 176.5 ms today, against the frame gates -- **and by the user's eyes**, because it is the first increment that can change what a window looks like, and because "the GS drew it" and "the GS drew it upside down, or one page over, or with the canvas still blitted underneath it" are indistinguishable from a console.  Image gate: rows 44/45 hold until 5c lands -- while `GL_BACKEND_EEGS` resolves to the linked software rasteriser, `make ps2 PS2_GL=eegs` must keep failing row 45 (`GLDEF_RET=3`) and passing 44; row 45 retires the moment the GS ops are the installed table |
| 6 | the pointer and window decorations as GS sprites over the GS-rendered layer, because they are drawn *after* the canvas today and would otherwise be buried | the user's eyes on the screen -- this is the increment whose correctness cannot be established headlessly |

**Increment 4's capacity is a latency knob, not a correctness limit.**  The largest raster window
in the measured frame is `f_ribbon`'s 1,984 vertices over 1,828 triangles, and a frame's total is
about 3,424 (`f_bg`'s 4 plus `f_ribbon` plus `f_menu`'s 1,436).  A group's REGS list here is
`PRIM, RGBAQ, ST, XYZ2` -- four quadwords per vertex, 64 B -- so a frame is about 13,700 quadwords,
219 KB of stream.  Sizing the buffer for a frame is the wrong law, because the frame's vertex count
is an application property that changes with `XB_RIBBON_ROWS/COLS` and with every future window that
draws.  Instead the recorder holds a fixed buffer (4,096 quadwords, 64 KB) and, when a vertex would
not fit, sends what it has through channel 2 and starts a second tag with the same REGS list and the
same `PRIM` -- legal because a packed group's `NLOOP` counts vertices, and a GIF transfer may carry
any number of groups.  The consequence is that no application can overflow anything, and what the
counters report is the number of DMA starts per frame, which for the XMB is about four and is the
number this increment is trying to keep small.  A stream that *drops* a tail is the failure mode to
design out: a partial frame with no evidence is exactly the class of bug that cannot be seen on a
screen that is already 600 ms behind.

The arithmetic the gate measures, at the shipped 4,096 quadwords: the headroom rule closes a group
when the next vertex would leave less than 14 quadwords, so a group holds 1,020 vertices in 4,082
quadwords, and a frame's 3,424 vertices are its four starts.  `make test-ps2-gs-rec` runs the same
rule at 64 quadwords, where a group is 12 vertices in 50 and the same frame is 286 starts
-- which is the knob, and the reason the gate overrides it rather than the code.

The fourth quadword is the cost of a claim not made elsewhere in this file.  `PRIM`'s texture,
blend and fog flags are decremented by the hardware as each primitive completes, under
`PRMODE.MODE/OFFSET`, so a strip that sets them once loses them from its second triangle on --
which is how multipass texturing is done, and is the opposite of what a UI wants.  Two answers
exist.  This one re-sends `PRIM` per vertex, costs 64 B instead of 48 B per vertex, and claims
nothing about `PRMODE`'s bit layout.  The cheaper one programs `PRMODECONT` and `PRMODE` (offsets
`0x1A`/`0x1B`, already named in `ps2_gs_pack.h`) into the increment-3 environment stream once, with
the decrement select set to "no decrement", and then drops `PRIM` from the REGS list.  That is worth
a third of the bus traffic and it needs a field table this file does not yet hold with enough
confidence to gate, so it is recorded here as the follow-up it is: when `PRMODE`'s `MODE[3:0]`,
`OFFSET[6:4]`, `A_MODE[10:8]` and `CONTEXT[11]` widths have been confirmed and pinned by a hand
literal like every other field in this port, `GS_REC_REGS_N` goes from 4 to 3 and the recorder's
per-vertex price goes with it.

**What 5a passes through, and the three things it does not.**  The plan asked whether a hand literal
could pin the viewport map and the direction of `y`.  It can, and the answer is that nothing is left to
pin: `zp.x`/`zp.y` are produced by `gl_transform_to_viewport_vertex_c` (`vertex.c:135-156`) and by its
clipped twin `gl_transform_to_viewport_clip_c` (`clip.c:17-39`), both from the same
`viewport.scale`/`viewport.trans`, and the GS is handed those integers.  A GS layer and a canvas layer
cannot disagree about which row is the window's top because they do not each hold a model of it.

Three things are not passed through, and they are where a bug could still live.  The colour: the
software rasteriser reads `zp.r/g/b/a` as 24-bit scaled values whose byte is the top one
(`TGL_ALPHA_BYTE`, `zbuffer.h:313`), while the GS's `RGBAQ` is four bytes, so the adapter does the
`>> 16` the encoder cannot know to ask for.  The texture coordinate: `zp.s`/`zp.t` are fixed-point
texel indices, one index per `1 << (ZB_POINT_*_FRAC_BITS + 1)` units with half a texel of offset
already folded in at upload (`texture.c:147-152`), while `ST` is a float texel coordinate, so the
adapter divides by the image's own scale -- a power of two, so exact.  And the flat-shaded triangle:
the software path takes its colour from the *last* vertex (`ztriangle.c:64-68`), whereas which vertex a
GS group samples when `IIP` is clear is not a law this port has an instrument for, so the adapter
writes that colour on all three vertices and the question cannot bite, at no cost, since the REGS list
carries an `RGBAQ` for every vertex whatever the shading mode.  What the suite's hand literals
pin is the *encoding* half of the first two -- a packed `rgba` reaching `RGBAQ` as `R[7:0] G[15:8]
B[23:16] A[31:24]`, and a float texel coordinate reaching `ST` as two IEEE singles -- and not the
reading half that lives in `clip.c`, which no host build can reach until 5c installs the backend.

Nothing calls `eegs_frame_begin()` yet, and that is 5c's work rather than an oversight: the frame
boundary belongs to the presentation seam, which is the only thing that knows when a paint is over.
The recorder is safe in the meantime on its own terms -- with no sink installed it accumulates and
counts and sends nothing (`ps2_gs_rec.c`, the `if (s_sink)` in its group flush), so a stray call
before 5c is a stream nobody reads rather than a DMA into an unprogrammed channel.

What increment 2's row called "texture upload into GS VRAM" split across two increments, and where
each half landed is worth stating so the row is not read as finished: the builder produces the
`TEX0`/`TEX1`/`CLAMP` words that *describe* a texture page, the IMAGE-mode stream that moves pixels
into VRAM already exists in `ps2_gs.c`, and the allocator that chooses those page addresses landed
with increment 3 -- what still has no caller is the combination of the two, which is increment 4's
presentation step.

Increment 4 onward is the work.  Increment 3 landed the way its row said it would: it programs
registers nothing else programs and has no caller yet, so what the screen shows did not change when
it went in.  Increment 4 is where the arrangement stops being a CPU one -- the GL window's pixels
become GS VRAM the GS itself draws over, so `redraw_all_windows_clip()` must be told to skip that
window instead of blitting an array nobody writes any more.  That is the first change in this plan
that can affect a window other than the GL one, and it is the reason increment 4 has a bench gate
of its own.

One of increment 3's promised gates had to be restated after it was measured, and the reason
belongs here because it will recur in every image gate that asks whether something is sent as a
packet.  The row said the apply path "calls the DMA sender".  In the linked image it does not:
`ps2_gs_gif_send()` lives in the same translation unit as `ps2_gs_draw_env_apply()`, so `-O2`
inlines it and the disassembly shows the channel-2 register dance inside the apply path with no
`jal` to the sender's label anywhere.  The law was never about a function boundary, so the row
asserts what survives inlining -- the DMAC window the path writes -- while the call that *is*
guaranteed to be real is the one crossing into `ps2_gs_env.c`, which no stage can dissolve because
`PS2_CFLAGS` carries no `-flto`.  Inter-TU calls are assertable as `jal`; intra-TU ones are not.

**Increment 3's VRAM arithmetic, measured before it was written.**  GS local memory is 4 MB, and
its address fields are paged in two different units, which is easy to miss because both are called
"page" in most prose: a texture or transfer address (`TEX0.TBA`, `BITBLTBUF.SBA/DBA`) is a 14-bit
index of 64-word units -- 256 bytes -- and 16,384 x 256 = 4,194,304 exactly; a frame or Z-buffer
base (`FRAME.FBA`, `ZBUF.ZBA`) is a 9-bit index of 2048-word units -- 8192 bytes -- and 512 x 8192
= 4,194,304 exactly.  Each field's width times its unit is the whole of VRAM, which is what makes
the reading unambiguous rather than remembered; ps2tek words both as "in words/N".  This falsified
a comment in `ps2_gs_pack.h`, which said 256-byte units for `FRAME.FBA`; the encoders take a raw
field value, so no built word changes, but the caller's arithmetic would have been off by 32x, and
the header now states both units.

The consequence for the architecture is arithmetic, not taste.  The canvas is CT32 at `FBW`=13
(832 px) x 600 = 1,996,800 B.  The XMB's GL window is 776x536 (`src/apps/xmb.c:3331` takes the
desktop minus 24 and 64), whose own CT32 layer at `FBW`=13 would be 1,783,808 B.  The icon atlas is
a 384x384 *upload*, but what the GS would have to address is the plane TinyGL allocated for it --
512x512, because `gl_image_alloc()` rounds each axis up to a power of two (`texture.c:111-153`) and
`TEX0.TW`/`TH` can only name one -- so 1,048,576 B, not the 589,824 B of staging pixels the
application handed over.  Those three sum to 4,829,184 B, which is 634,880 B more than the GS has,
before the font sheet or any Z buffer.  So the GS backend gets **no second full-screen surface**:
increment 4 programs the drawing environment to render into the very canvas the CPU blit has just
filled, `SCISSOR` clipped to the window's rect -- which is what its own row said ("emitted after
the canvas stream, clipped to the window's rect") and is now the only arrangement that fits.  What
increment 3's allocator therefore reserves is the canvas at page 0 (1,996,800 B rounded up to 244
pages of 8 KB), then texture pages out of the remaining 268: the icon plane takes 128 and the font
sheet's 128x128 plane takes 8, which leaves 132 pages, 1,081,344 B, for a wallpaper-as-texture,
further atlases, and the one 8 KB page a Z buffer needs if a depth test is ever turned on.

**What 5b takes from the machine and what it takes from the table.**  The four transmission registers
are the first words in this plan whose bit positions are already load-bearing on real hardware,
because the canvas upload has been programming them since before the plan existed:
`send_trx_setup()` in `ps2_gs.c:116-145` puts `DBW` at bit 48 and `DPSM` at 56, `DASX` at 32 and
`DASY` at 48, `RRW` at 0 and `RRH` at 32, and a screen that renders from those words on both PCSX2
and the box is the evidence.  So 5b does two things with that, in this order.  It moves the blit
path's written-out shifts onto new `gs_pack_bitbltbuf/trxpos/trxreg/trxdir()` encoders and requires
the result to be the *same words* -- the host gate asserts the canvas rectangle's four words equal
the literals the current code builds by hand, which makes the already-working path the instrument for
the encoder rather than a second copy of its arithmetic.  And it leaves the half nothing exercises at
zero: a Host-to-Local transfer ignores `SBA`/`SBW`/`SPI`, because the source is the quadword stream
itself, so the destination half is the measured half and the source positions remain a header
reading until some row reads VRAM back.

Three more laws 5b needs and 5a already paid for.  **A texel plane crosses to VRAM verbatim**: on
`BTRON_PS2_TARGET` the GL pixel domain *is* canvas order (`zbuffer.h:64-75` puts red in the low byte
and blue at 16-23, and `image_util.c:22-38` builds every texel through those macros), which is the
same word the GS's CT32 holds, so an upload streams `im->pixmap` with no conversion pass -- the
mirror image of the canvas's own no-conversion rule at `ps2_gs.h:90-96`.  **The extent the GS is
told is the padded one**: `TEX0.TW`/`TH` are exponents, so a 384x384 upload is described as 512x512
with `TBW`=8, and the padding is transparent black, so an `ST` that ever overshoots the app's own
extent reads a texel rather than someone else's page.  **`ST` is already in texels**: the 5a
adapter's `zp.s / 2^11`, `zp.t / 2^19` division puts the coordinate in the unit the GS interpolates
in, half a texel inside the texel it names (`texture.c:146-152` is where that offset was folded in
for the software sampler), so `CLAMP`'s `MINU`/`MAXU` are texel indices of the same count and no
third coordinate space enters the port.

The last of these is the one that changed the plan's shape.  A frame binds the icon sheet for the
icons and the font sheet for the message text, and the environment packet is one per frame, so
`TEX0` cannot live only there: a texture switch has to appear in the primitive stream at the point
the first vertex that uses it is recorded.  The GIF allows exactly that -- a state write is a group
of AD pairs between two packed groups -- so 5b adds `gs_rec_ad()` to the recorder: it closes the open
group, sends `NLOOP` AD pairs, and lets the next `gs_rec_prim()` open a fresh one.  The stage calls it
only when the bound plane's page differs from the page the last recorded vertex used, so the XMB's
frame pays a handful of those groups and the ribbon's 1,828 triangles pay none.

One pair of register facts belongs here because it decides what a window-relative packet looks
like: `SCISSOR` takes plain unsigned screen pixels (11 bits, 0..2047) with no offset, while a
vertex is 12.4 around the 2048 offset and, in the manual's words, "when a vertex kick occurs,
XYOFFSET is subtracted from the vertex's X and Y coordinates".  So `XYOFFSET` carries the page
origin -- 0x8000 plus the window's x/y -- and the clip rect can stay in screen coordinates while
every vertex stays relative to its own window.  Nothing in increments 2 or 3 has to convert a
vertex by the window's position.

**Restoring a positive control is a trap the gate itself cannot see.**  A control that edits a
source, shows the `[FAIL]`, and puts the file back must leave the source *newer* than the object
built from the broken version.  `cp -p` (or any mtime-preserving restore) leaves the copy's older
timestamp on the file, make then judges the object up to date, and the suite re-reports the
injected failure as though it were still in the source.  That happened during increment 2 and was
reproduced deliberately: patched source -> `[FAIL] scissor`; `cp -p` restore -> the same `[FAIL]`
with a clean source; `rm` the object and rebuild -> 35/0.  The recorded-dependency machinery is
not at fault (`-MMD -MP` plus the `-include` of `*.test.d` already rebuilds a `#include`d source,
which is why no explicit prerequisite was added).  Prefer `touch` over `cp -p` when undoing a
control, and treat a failure that survives a restore as a stale object until a rebuild says
otherwise.

The trap has a second shape, met during increment 3: several controls run as one compound
command, where a later control reads a source the previous control never restored.  Control B
removed the 12.4 scaling from `gs_pack_xyoffset`'s caller, control C changed `gs_vram_fba`'s page
unit, and C's run printed B's `[FAIL] xyoffset` -- which looks like a control that does not work
and is really a control that ran against a dirty tree.  Run one control per command, restore it,
and re-gate to 0 failures before starting the next; a control whose `[FAIL]` names a *different*
law than the one it edited has measured nothing.

**Reading an image gate needs the pipe kept alive.**  `scripts/test_ps2.sh` runs under `set -o
pipefail`, so an awk that extracts one function's disassembly and `exit`s at the blank line
SIGPIPEs the disassembler on its way out: the pipeline's status becomes 141, the assignment that
took it fails, and the script dies before any row is reported -- with no message naming the row
that caused it.  Stop *printing* at the range's end instead of exiting, and the disassembler runs
to completion.  The rows that already used `awk '...{exit}...'` survived only because their
pipeline sits inside an evaluated `[ $(...) ]`, where the status is consumed by the test.

### 2.2 DualShock 2 Controller Driver (`ps2_pad`)

The PS2 gamepad input system provides direct analog pointer manipulation and tactile shortcuts:

| Controller Input | B-System Action | Description |
|:---|:---|:---|
| **Left Analog Stick (LX, LY)** | Smooth Mouse Pointer Velocity | Integrated with a deadzone filter (`abs(axis - 128) > 16`) to steer the 16x16 desktop arrow cursor. |
| **Cross (✕)** | Left Mouse Click | Selects icons, clicks buttons, and focuses windows.  While a menu is open it is Return instead -- see [Menu Navigation Without a Pointer](#menu-navigation-without-a-pointer-normative). |
| **Square (□)** | Right Mouse Click | Activates context menus and property dialogs. |
| **Circle (◯)** | Cancel / ESC | Dismisses active dropdown menus and dialogs. |
| **Triangle (△)** | Cycle TIP / IME Mode | Rotates language input: ASCII → Hiragana (`あ`) → Katakana (`ア`) → Tibetan (`བོད`). |
| **Start** | Toggle Desktop Menu | Opens or closes the top system menu bar. |
| **Select / L1 / R1** | Cycle Active Window | Shifts focus between open desktop windows. |
| **D-Pad (Up, Down, Left, Right)** | Discrete Navigation Keys | Injects `BTRON_KEY_UP/DOWN/LEFT/RIGHT` events into the event queue.  It also moves the cursor by 16 px, but not while a menu is open. |

Developers can simulate controller state via the serial shell using:

```bash
pad <btns_hex> [lx ly]
# Example: Click Cross button with centered sticks:
btron-ps2> pad bfff 128 128
```

#### Menu Navigation Without a Pointer (Normative)

A launcher row must be reachable with the D-pad alone.  Three rules follow from that,
and each has a measured defect behind it.

1. **Two key encodings are live, and both are accepted.** A windowed host forwards its
   own keysyms (`SDLK_UP`/`SDLK_DOWN` are `0x111`/`0x112`, see `src/window/event.c`),
   while every bare-metal HID driver -- the PS2 pad, the PS2 USB keyboard, the Mac
   Quadra ports -- injects the `BTRON_KEY_*` codes from `include/btron/event.h`
   (`RIGHT 0x4000004F`, `LEFT 0x40000050`, `DOWN 0x40000051`, `UP 0x40000052`).
   A handler that matches only one of the two is dead on the other half of the targets;
   `tracker_handle_key()` used to match only the keysym, which is why the D-pad moved
   the launcher's highlight by nothing but the pointer's new pixel.
2. **While a menu is open the D-pad must not displace the cursor, and Cross must become
   Return.** `tracker_handle_mouse_move()` recomputes `hover_index` from the pixel it
   lands on, and the row pitch is `TRACKER_ITEM_HEIGHT` = 20 px while one D-pad step was
   16 px, so a move that accompanies the arrow picks whichever row the pointer reached
   rather than the row the arrow asked for.  A click is worse still: it is delivered at
   the pointer, not at the highlighted row.
3. **A menu handler consumes a key only when it acted on it.**
   `workbench_process_event()` asks `global_menu_handle_key()` before
   `tracker_handle_key()`, and `global_menu_is_open()` answers true whenever the
   *launcher* is open as well as when a deskbar dropdown is (`global_menu.c:239`).
   Returning `TRUE` for an arrow while no dropdown is active therefore swallows the key
   one stage before the launcher sees it -- the exact shape of the first on-target
   measurement, 14 D-pad DOWN hops with the highlight fixed at row 0.

The state to read is not `global_menu_is_open()` but `global_menu_get_active()`: a
dropdown worth navigating has `active_menu > 0`.

Measured on the target by `BENCH_APP=1 BENCH_TIMEOUT=460 scripts/ps2_bench.sh` (the script
builds the bench ELF itself; `BENCH_APP=1` reaches its `make ps2-bench` through the
environment, and the default 300 s cap stops the run before the slow phases print).  Its
`launch XMB by key` phase presses the pad's own report bytes rather than calling the
tracker's API (`src/cores/core_ps2.c`, `ps2_bench_launcher_key_route()` and
`ps2_bench_launch_xmb_by_key()`); the arrow rows above come from the ordinary
`scripts/ps2_bench.sh` run, which finishes in ~15 s:

```text
[BENCH] key: hover 0->6 in 5 DOWN hops, want row 6, selected=' (XMB)'
[BENCH] key: launcher=1 deskbar_open=1 dropdown=-1
[BENCH] key: the XMB row is reachable by arrow alone
[BENCH] key: Cross on row 5 '(Terminal)' launched 1->2 menu=0 pointer 400,450->400,450
[BENCH] xmb by key: 4 DOWN hops put the highlight on row 5, launcher=1
[BENCH] xmb by key: heap before the press -- used=5847 of 12288 kB, largest hole 6439 kB, pool sound
[BENCH] drain: first out type=4 (EVT is 24 bytes, move=3 but=1,2)
[HEAP] +1500 kB -> 0xccd4c0   used=7348 kB   largest_free=4939 kB
[HEAP] +1500 kB -> 0xe44500   used=8848 kB   largest_free=3439 kB
[GL] VirtIO-GPU OpenGL (virgl) backend init: 768x500
[HEAP] +576 kB  -> 0xfbb510   used=9424 kB   largest_free=2863 kB
[BENCH] xmb surface frame 3 : 768x500 black=0 other=384000 first_colour=0xff0283cd box=0,0..767,499
```

The last row is the answer to "can't run xmb from menu": the row was reached with arrows
only, Cross opened the application (its surface and depth buffer are the two 1,500 kB
allocations), and its own task loop drew frames whose surface carries no black pixel.
`pointer 400,450->400,450` is rule 2 above, and `dropdown=-1` is rule 3's condition.
Host coverage for the same three rules is `make test-tracker` (78/78) and
`make test-global-menu` (55/55), the latter including the fall-through assertion.

Two things this run says out loud that are easy to read past:

- **The GL backend is still named `virgl` on PS2.** The row is `[GL] VirtIO-GPU OpenGL
  (virgl) backend init: 768x500` -- `src/gl/gl_dispatch.c` starts with `s_active_backend`
  at VIRGL and nothing on this target overrides it by capability.  It now *works* because
  the window is sized from `btron_desktop_note_size()` (768x500, not the 952x564 the
  phantom GPU reported) and the TinyGL rasterizer answers the calls, but the label is a
  lie: capability-based backend selection in
  [`src/gl/gl_dispatch.c`](file:///Users/tonpa/depot/bitedits/btron/src/gl/gl_dispatch.c)
  is the unfinished half of "enable the OpenGL downstack with the common code", and it is
  what [`egl_surface.c`](file:///Users/tonpa/depot/bitedits/btron/src/gl/egl_surface.c)
  currently decides by `#if defined(BTRON_UEFI_TARGET)` instead of by what the hardware
  reports.
- **One XMB frame used to cost ~78 s of emulator time** (`[BENCH] xmb frame 1/2/3` on
  emulator timestamps 71.1, 149.1 and 227.1 s for a Cross press at 5.6 s).  A frame that
  long could not be timed by a guest-side clock at all: CP0 Count is a 32-bit counter, so a
  plain delta wraps every 29.1 s at the documented 147.18 MHz -- and every 14.6 s on this
  emulator, whose `Count` runs at the full core rate (see
  [The measured microsecond](#the-measured-microsecond-normative)) --
  and the fold in `btron_render_perf_us()` only
  recovers a wrap if something polls it inside the interval -- which a frame that blocks in
  the rasterizer does not.  The heartbeat therefore printed `... ms folded` and the app
  phase's rate printed `<= N passes/s`: monotonic upper bounds, with the wall cadence read
  from the emulator's own prefix.  After the surface-hash cache and shipping the icon sheet
  as data (above), and after the texture, blend and per-fragment fixes below, a steady frame
  is 683 ms and frame 1 is 0.69 s (`frame1_us 687,935`; the 5% it has since given back is
  code layout, not any one fix) -- both far inside the shortest of those rotations -- so the
  `[XMBT] draw=/swap=` rows are now exact microseconds, and the
  phase breakdown that produced that attribution is still compiled in: `xb_paint` emits
  `[XMBT] f_surface/f_clear/f_bg/f_ribbon/f_particles/f_menu` for its first two paints,
  `[XMBT] raster ...` for the tris/verts/px/spans split of each of those windows, and
  `[XMBT] wave ...` for the sheet's motion over its first six paints.

### 2.3 Keyboard & USB Subsystem (`ps2_usb`)

The PlayStation 2 motherboard includes two USB 1.1 ports driven by an Open Host Controller Interface (OHCI) block at `0x1F801600` (uncached KSEG1: `0xBF801600`).

The cleanroom driver (`ps2_usb.c` and `ps2_usb.h`):
- Verifies controller presence by checking `HcRevision` (`0x10` or `0x11`).
- Transitions the host controller state machine to `OHCI_CTRL_HCFS_OPER` (Operational).
- Monitors `HcRhPortStatus[0]` and `HcRhPortStatus[1]` for port connection status (`OHCI_PORT_CCS`).
- **Standard 8-Byte USB HID Keyboard Decoder**:
  - Differential key state tracking across 6-key rollover reports (`s_prev_keys[6]`).
  - Converts modifiers (Shift, Ctrl, Alt, GUI) and USB HID usage IDs into standard BTRON keycodes.
  - Full alphanumeric and symbol support with Caps Lock state management.
  - Function keys F1–F12, navigation cluster (`Home`, `End`, `PageUp`, `PageDown`, `Insert`, `Delete`, Arrows).
  - Keypad numbers and arithmetic operators.
  - Authentic Japanese keyboard keys: `Henkan` (Convert), `Muhenkan` (Cancel), `Hiragana/Katakana` toggle.
- **Mouse Subsystem**:
  - Decodes 3-byte / 4-byte reports: relative displacements `(dx, dy)` and button bitmask (Left, Right, Middle).
  - Injects `EV_MOUSE_MOVE` and `EV_BUT_DOWN` / `EV_BUT_UP` events directly into the B-System event queue.

#### DualShock 2 On-Screen Software Keyboard (OSK)

For setups without physical USB keyboards, the PS2 port integrates an interactive on-screen software keyboard overlay directly into B-Editor:

- **Toggle**: Pressing `L2` or `R2` on the gamepad (or executing `osk` in the shell) displays a $10 \times 4$ on-screen keyboard panel inside B-Editor.

- **Layout**:
  - Row 0: `1 2 3 4 5 6 7 8 9 0`
  - Row 1: `Q W E R T Y U I O P`
  - Row 2: `A S D F G H J K L RET`
  - Row 3: `Z X C V B N M SPC DEL ESC`

- **Gamepad Controls**:
  - `D-Pad (Up, Down, Left, Right)`: Navigate active key cell with high-contrast accent highlight.
  - `Cross (✕)`: Type selected character into the document.
  - `Square (□)`: Quick Backspace shortcut.
  - `Circle (◯)`: Close OSK.
  - `Triangle (△)`: Cycle TIP / IME input mode.

### 2.4 Full B-System Workbench Desktop & Compositor

The PS2 cleanroom port integrates the **full, authentic B-System Graphical Workbench** matching the Motorola 68040 Macintosh Quadra 800 port and the unified specification in [`EVENTING.md`](file:///Users/tonpa/depot/bitedits/btron/EVENTING.md):

1. **Desktop Background & Real Body Icons** ([`src/desktop/desktop.c`](file:///Users/tonpa/depot/bitedits/btron/src/desktop/desktop.c)):
   - Authentic BTRON Teal background (`0x008080`) with retro dot-grid wallpaper pattern.
   - Five standard Real Body / Virtual Object desktop icons:
     - `実身・仮身` (Cabinet / Real Body Store)
     - `基本エディタ` (T-Editor)
     - `端末シェル` (GTerm Shell)
     - `音響機器` (Audio Player)
     - `会話通信` (Chat)
   - Double-clicking or clicking any icon launches its full application window.

2. **Authentic Window Manager** ([`src/window/wnd.c`](file:///Users/tonpa/depot/bitedits/btron/src/window/wnd.c)):
   - Complete hit-testing and z-order elevation (`top_wnd`).
   - 16x16 diagonal hatch corner resize grip in bottom-right corner.
   - Compact sliding titlebar tab dragging and tab offset adjustment.
   - Close button (`[X]`) and client area event routing.

3. **Global System Deskbar & Dropdown Menus** ([`src/desktop/global_menu.c`](file:///Users/tonpa/depot/bitedits/btron/src/desktop/global_menu.c)):
   - Top menu bar with `［BTRON］`, `システム(S)`, `実身・仮身(O)`, `ウィンドウ(W)`, `道具・文字(T)`.
   - Japanese calendar plate with authentic Kanji weekday indicators (`日/月/火/水/木/金/土`).
   - Mozc / TIP input method badge toggling (`[ ASC ]`, `[ あ ]`, `[ ア ]`, `[ བོད ]`).

4. **Tracker Start Menu** ([`src/desktop/tracker.c`](file:///Users/tonpa/depot/bitedits/btron/src/desktop/tracker.c)):
   - Haiku-style root application and window tracker menu toggled via gamepad `Start` button or clicking `［BTRON］`.
   - Three blocks, in this order: the live visible-window list, the application rows
     `端末 (Terminal)` and `横断メディアメニュー (XMB)`, and the system rows (Sleep,
     Restart, Shutdown, Quit).  The openers are the weak declarations in
     [`include/btron/apps.h`](file:///Users/tonpa/depot/bitedits/btron/include/btron/apps.h),
     so a target that links without an app renders the row and does nothing on click.
     `scripts/ps2_bench.sh` proves the rows on the target: `[BENCH] launch: terminal
     row=… wins 3->4 terminal 1->2` is the gterm window arriving in the table.
     Both rows are reachable and activatable with the D-pad alone -- see
     [Menu Navigation Without a Pointer](#menu-navigation-without-a-pointer-normative)
     for the three rules that made that true and for the `[BENCH] xmb by key:` rows that
     prove the XMB row opens the application and paints it.

5. **Real BTRON Applications**:
   - VObject Manager (`src/apps/vobj_manager.c`), T-Editor (`src/apps/t_editor.c`), GTerm (`src/apps/gterm.c`), and Control Panel (`src/settings/control_panel.c`).

6. **One canvas, one GIF DMA upload**:
   - The desktop composites into `s_desktop_backbuffer`, which *is* the GS canvas: it is
     installed with `ps2_gs_set_framebuffer()` and `ps2_gs_upload(x, y, w, h)` streams the
     damaged rectangle of it straight to the GS's local framebuffer over DMAC channel 2.
     The ARGB->CT32 translation is no longer a pass; it happens per colour, at the leaf
     stores, through `BTRON_PIXEL_FB()` -- see [The canvas's byte order](#21-graphics-synthesizer-gs-framebuffer-architecture).
   - The rectangle comes from a set of up to `PS2_BAND_MAX` merged boxes (y-touching
     damage unions its x span; overflow falls back to the whole canvas), and it is a
     *box* rather than a full-width row band because the render pass costs ~0.3 us per
     pixel: 16 rows at full width is 5370 us with the byte-swap sweep still in the path,
     the same 16 rows clipped to the cursor sprite's own 16 columns is 528 us.  See
     [Why the cursor lags](#why-the-cursor-lags-and-which-row-says-so) for that table.
   - Deleting the sweep is what `full_swap_us` measures: the smoothness gate expects the
     `swap` column to read 0, and the whole-canvas present to lose its 49 ms of 274 ms.

7. **The kernel heap, and what a black XMB turned out to be**:
   `src/cores/core_ps2.c` owns a 12 MB pool in `.bss`.  It used to be a bump allocator whose
   `kfree()` returned the block to nothing, so opening one window per menu click consumed the
   pool until a later allocation failed and the app painted nothing at all.  It is a
   first-fit pool with real coalescing now, and `ps2_heap_check()` walks it for the
   invariants (payload inside the pool, sizes a multiple of the 16-byte header, no cycle) on
   both sides of the open, which is what `[BENCH] heap before/after xmb: … pool sound`
   reports.  The header is padded to 16 bytes by a `reserve` word and a negative-size typedef
   asserts it: with 12 MB of `.bss` the image sits ~1.66 MB under the `ASSERT` in `ps2.ld`, so
   a header that grew by 8 bytes would quietly eat that headroom.
   The XMB reported black for three separate measured reasons, and only the third was in the
   app: the un-freable heap above; a window sized from a desktop record that had not been
   filled in yet, which asked for 952x564 -- larger than the canvas -- so the surface and its
   depth buffer were allocated beyond what the port presents (`btron_desktop_note_size()` in
   `src/desktop/desktop.c:510`, called from the PS2 startup with the real mode, is the single
   place the window layer now reads the canvas size from before allocating); and the
   double-precision FPU ops described in [The FPU Law](#the-fpu-law-normative), which did
   nothing at all and left the geometry uncomputed.  The decisive row is the surface scan in
   `scripts/ps2_bench.sh`: `[BENCH] xmb surface frame 3 : 768x500 black=0 other=384000
   first_colour=0xff0283cd box=0,0..767,499` -- every pixel of the window's surface has
   reached the guest.  `black=383999 other=1` was the failure, one pixel and all the rest
   unlit, and a count of `Unknown R5900` rows in the thousands says which cause it is.

#### One task-driven frame, stage by stage (Normative)

An application's frame on this port is not the render its own log times.  `xb_paint()`
reports `[XMBT] frame … draw=173357 us swap=10540 us` -- the `swap` half is the whole-surface
alpha fix-up and reads 0 on the current image, see *The frame buffer's alpha byte is written at
the store* -- and stops at `egl_swap_buffers()`,
while the pixels reach the GS only after the compositor has blitted the window's surface
onto the canvas and the port has uploaded that rectangle.  The two halves are joined by
`[XMBT] cycle`, which `ps2_gui_pass()` fills and `ps2_task_dly()` prints once the hold has
been spent, because the hold is part of the period the body sees and nothing else times it:

```
[XMBT] cycle <n> period=<us> pass=<us> tail=<us> hold=<spent/asked ms> | poll= hook= evt= net= paint= note= |
        render= swap= upload= px= nb= full= why=<hex> dmg=<x0,y0,x1,y1/bands> |
        comp bg= frame= paint= blit= panel= bars= total= bfn= wins=<drawn/walked>
```

| Column | Owner | What it decides |
|:---|:---|:---|
| `poll hook evt net note` | this port | the pass' own bookkeeping: HID polls, the bench frame hook, the event dispatch, the two timer nets, `[PSTAT]` accounting |
| `render swap upload` | this port | the three stages of one present: composite, byte-order (structurally 0, see [The canvas's byte order](#21-graphics-synthesizer-gs-framebuffer-architecture)), GIF DMA |
| `px nb full` | this port | the area, band count and whether band 0 is the canvas |
| `why` | this port | *which rule* made it the canvas: `1` a non-move event, `2` the convergence timer, `4/8/16/32` global menu / tracker / window-manager interaction / IME, `64` band-set overflow, `128` damage that already spanned it |
| `dmg` | shared compositor | the band set as the app and the pointer left it, before any of the rules above replaced it |
| `comp …` | shared compositor | that composite's own split, taken from `g_render_stats` for this one pass: decorations, the apps' `paint` callbacks, the client-area blit, panel, bars |

The row is not behind `BTRON_PS2_BENCH`: an unarmed profiler costs one branch per boundary,
and gating it would make the row unavailable to whoever has the real machine.  Arming is
`left`, which the app phase sets, so a desktop session pays for it and does not print it.

**The measured frame (2026-10-11, `SMOOTH_FRAMES=8 ./scripts/ps2_smooth.sh`, cycle 1).**
`period=350523 pass=334514 | poll=1 hook=7 evt=0 net=1 paint=334502 note=0 | render=333682
swap=0 upload=819 px=480000 full=1 why=c dmg=0,0,800,556/1 | comp bg=0 frame=20324
paint=238860 blit=49913 panel=0 bars=0 bfn=0 wins=4/4`, against the app's own
`draw+swap = 183897 us` and the 16 ms the body asked for:

| ms | stage | who pays |
|--:|:---|:---|
| 184 | the XMB's own rasterising (173) and its surface swap (10.5, now 0) | the app (`f_bg`/`f_ribbon`/`f_menu`, tasks #41/#43) |
| 55 | the *other three* windows' `paint` callbacks | the compositor: `redraw_all_windows_clip()` used to run every intersecting window's callback whenever the composite was not blit-only, and one `inval_wnd()` made the whole composite non-blit-only -- closed by the cut below |
| 50 | the client-area blit, 613,440 px | the compositor |
| 20 | four window decorations | the compositor |
| 12.3 | the desktop's own art, now measured rather than derived: background restore 9.9, panel + gold bar 1.8, eight test bars 0.6 | the compositor -- `bg=`/`panel=`/`bars=` on a `full=1` row, which is what consequence 2's instrument exists to print |
| 0.05-10.7 | `render=` minus the composite's own `total=`: the overlays the pass draws around the composite, and the span between its two marks | this port's render phase, *outside* the compositor.  It is 10.7 ms on two frames and 0.05 ms on the third, so it is not one fixed stage and does not get a row of its own until something measures it |
| 10 | polls, dispatch, timer nets, `[PSTAT]` | this port, all six columns summed |
| 0.8 | GIF upload of the 480,000 px canvas | this port |
| 16 | the hold the body asked for | `dly_tsk()` |

Re-measured 2026-10-12 after the alpha-at-store cut, steady frame 3 of
`SMOOTH_FRAMES=3 scripts/ps2_smooth.sh`: `draw=176514 us swap=0 us`, cycle row `render=259156
swap=0 upload=816 px=480000 full=1 why=1 | comp bg=9926 frame=20392 paint=176526 blit=49890
panel=1801 bars=566 total=259109`.  The frame's own cost fell by the 10.5 ms the swap used to
be and nothing else moved, which is the expected shape for a change that deletes traffic
rather than arithmetic.

Three consequences are normative for the work that follows:

1. There is nothing left to win in the port's own pass.  Ten microseconds of bookkeeping
   and 0.8 ms of DMA against a 334 ms pass is not a scheduler problem; the whole gap
   between what the app logs and what the user sees is inside the shared compositor.
2. A zero in a timing column means *not instrumented*, never *free*.  The damage path
   (`redraw_baremetal_desktop_mode()`) times all of the stages, and where it runs the
   background comes from the cache: every banded `[BENCH] … compositor:` row in the same log
   reads `bg=<nonzero>(<band px>) … bfn=0`, and a cache miss would have incremented `bfn` and
   reported the whole canvas instead of the band.  The whole-canvas path filled none of those
   columns, which is why its cost only appeared as the residue in the table above.  It now
   goes through `render_desktop_background_rect()` -- the one builder that owns the cache and
   `bfn` -- and records `bg`/`panel`/`bars`/`total` (the cycle row's name for `st.comp_us`)
   the way the damage mode does
   (`src/desktop/desktop.c:363-404`), so that residue is measured rather than derived.
   Two things this makes available and does not yet make true: the ~25 ms has no split
   between the restore, the panel and the bars until a device row reports it, and the
   columns' *absolute* microseconds are the Emotion Engine's only on the device --
   `btron_render_perf_us()` is a weak `return 0` in `src/graphics/dp_core.c:64` and each
   platform core installs a strong clock, so a host build structurally reads 0 everywhere.
   The wiring is held by the `whole-canvas composite` block of
   `verify/tests/test_desktop_clip.c`, which installs a host `CLOCK_MONOTONIC` clock for
   exactly that reason and asserts only relations between columns (a stage that did work is
   non-zero, a total covers its parts); the panel clause is `SKIP`ped there, not passed,
   because the panel's bar is one of that harness's no-op stubs and the two-row gold accent
   can land inside a single tick.  Positive control, both halves: removing the total's
   `btron_render_stat_max()` makes the coverage check fail at `0 >= 21`, and routing the path
   back to the untimed builder makes the background check fail at `bg=0` -- and the pixel
   comparison passes under both, so again the timings are the assertion and the picture is
   the safety net.

   The instrument on the machine, `SMOOTH_FRAMES=8 ./scripts/ps2_smooth.sh` on 2026-10-12,
   cycle 1 of the app phase -- the same frame shape as the row quoted above, one year of
   emulator noise later:

   ```
   render=277379 swap=0 upload=816 px=480000 nb=1 full=1 why=c dmg=0,0,800,556/1 |
   comp bg=9910 frame=20315 paint=184228 blit=49869 panel=1816 bars=576 total=266721 bfn=0 wins=4/4
   ```

   `bfn=0` is the premise corrected: the whole-canvas path never did the procedural rebuild,
   so there was no 25 ms of LZW decodes to cache away.  What it did do is the 9.9 ms restore
   the `bg=` column now names, and the six columns sum to 266,714 against the composite's own
   `total=266721` -- within 7 us of self-consistency, which is what makes the row usable as an
   attribution rather than a collection of numbers.  (One caveat on reading them: `btron_render_stats_take()`
   runs at the stamp, so a column is the largest value the compositor recorded since the
   previous profiled pass, not this frame's exclusive cost.  With the app's whole-canvas
   composite at 266 ms and the intervening session passes painting bands, the app's frame *is*
   that maximum here; a bench that profiled a quiet frame would not be reading what it names.)
   The consequence for the plan is that the desktop's own art is 12.3 ms of a 277 ms frame, so
   no cut in it reaches the 5x the target asks for; the 254 ms of window work under `paint=`,
   `blit=` and `frame=` is gated on how much of the canvas the app declares stale, which is
   task #41.
3. Staleness belongs to one window, never to a composite.  `WND::img_valid` is the app's
   declaration about *its own* client GDEV (`inval_wnd()` in `src/window/wnd.c:326`), and the
   window's `dev` persists between composites, so blitting a valid window's surface and
   re-running its callback write the same pixels.  The walk used to read the composite's mode
   instead of that flag: `workbench_render_damage_paint()` and the whole-canvas path asked
   every intersecting window to re-render, so one app's `inval_wnd()` billed every other
   window's frame to that app.  The walk now gates on `wnd->img_valid` per window
   (`src/window/wnd.c:514`), which is what closes the 55 ms row above:

   | | before | after | measured by |
   |:---|--:|--:|:---|
   | `comp paint=` | 238,860 | 184,260 | cycle 1, same bench, same frame |
   | `render=` | 333,682 | 279,041 | the same row |
   | `full_render_us` | 112,670 | 57,764 | `scripts/ps2_smooth.sh` |

   `draw_us` reads 172,847 against 172,733 before it -- the app's own rasterising is
   untouched, so the 54.6 ms fell exactly where the table attributed it.  The rule holds only
   while the flag is honest, and it is: `img_valid` goes FALSE when `rsz_wnd()` re-opens the
   client GDEV (`src/window/wnd.c:282,286`), when an app calls `inval_wnd()` (:330), and at
   creation (`opn_wnd()` `calloc`s the struct), and nothing but the composite's own blit erases
   a client GDEV -- `clo_wnd()` (:196) frees the window with it, and `draw_retro_window_frame()`
   writes to the canvas, never to a `dev`.  Every time-driven app in the tree ticks itself
   through `inval_wnd()` rather than relying on the composite to re-render it: the clock
   (`src/chokanji/clock.c:174`), GLGears (`src/apps/glgears.c:177`), the demoscene
   (`src/demo/lilcu64_demo.c:1483`), the orchestra (:839), the audio player (:610) and the XMB
   itself (`src/apps/xmb.c:2851`).

   This is a shared-compositor change, so the Pi 400 takes it too, and it is held by a
   discriminating headless test rather than by the argument above: the `selective composite`
   block in `verify/tests/test_desktop_clip.c` counts each window's callback separately and
   requires that invalidating one of two overlapping windows renders exactly one -- with the
   gate removed that block reports `A rendered 1, B rendered 1` and fails, while the pixel
   comparison still passes, because re-rendering an idempotent callback cannot change a pixel.
   The tally is the assertion; the pixel diff is the safety net.

The band the app itself dirtied is `dmg=0,0,800,556`: `xb_task()` invalidates its whole
window every frame, and that window is 776x536 of an 800x600 canvas, whose rect the panel
timer widens to the canvas's full 800 columns.  The stages a band would clip are now named
by the row rather than guessed at -- `blit=` 49.9 + `frame=` 20.3 + `bg=` 9.9 + `panel=` 1.8
+ `bars=` 0.6, i.e. 82.5 ms of compositor work per frame, on top of the app's own 184 ms of
`paint=` -- and a band-limited repaint is worth that only if the app also stops invalidating
what it did not change, which is why task #41 is written as two changes, not one.

What the frame above does *not* settle is the banded case, and the row says why: it carries
`why=c`, an open global menu and an open tracker.  That is not a stuck overlay.  The task
body runs on the caller's stack (see the µITRON layer above), so
`ps2_bench_launch_xmb_by_key()`'s Cross reaches `tracker_execute_item()`, which opens the
window -- and the app's three profiled frames are painted inside that call, before the
`tracker_close_menu()` on the line after it.  A frame with every overlay closed would take
`workbench_render_damage*()` instead of `workbench_render()`.  Of the two differences that
would show, one is now closed by the instrument in consequence 2 (the desktop's own art is
12.3 ms, and none of it is the procedural rebuild); the one still open is the `dmg=` the
rules above replace with the canvas before the band is ever clipped.

#### The plane table (Normative)

Every period this machine presents, integrates and ages against is one record, installed
by the core and read through the shared renderer: `btron_plane_cfg_t` in
[`include/btron/async_rt.h`](file:///Users/tonpa/depot/bitedits/btron/include/btron/async_rt.h),
owned by `btron_planes_install()` / `btron_planes()` in
[`src/graphics/dp_core.c`](file:///Users/tonpa/depot/bitedits/btron/src/graphics/dp_core.c).
The reason the periods live in one table rather than at the places that read them is that
a period is not a property of whichever rasterizer the image happens to link.  On this
port the pass that paints is also the pass that spends the pointer's distance, so every
"pixels per pass" the cursor is tuned with is the frame time in disguise -- and the divisor
that turns a period into clock ticks is the machine's own measurement rather than a literal,
for the reason given in [The measured microsecond](#the-measured-microsecond-normative).

| Field | Value | Where it is consumed | Status |
|:---|:---|:---|:---|
| `input_period_us` | 1000 | `ps2_ptr_input_slices()`: what one pass may spend is `PS2_PTR_MAX_STEP` times the number of 1 ms periods that actually elapsed since the last service | **proven**: `HcFmInterval = 0x27782EDF` is a 1 ms OHCI frame, and `ps2_usb_frame_number()` is that clock -- see [Which layer owns the pointer](#which-layer-owns-the-pointer) |
| `ui_period_us` | 16667 | nothing yet.  It is the absolute deadline the async UI plane is installed against, so it is in the table before it has a reader | inferred from the programmed mode (800x600 inside one 720p frame at 60 Hz) |
| `panel_refresh_us` | 200000 | `ps2_gui_pass()`: the system panel's 28 rows are added to the band set on this clock even with no damage | the panel's own clock, chosen not measured |
| `full_convergence_us` | 2000000 | `ps2_gui_pass()`: the whole-canvas `force_full` net | convergence, not animation -- see above |
| `input_slices_max` | 16 | the clamp on the slice count: one 60 Hz frame of catch-up, bounding the work rather than the debt, so a pass returning from a 683 ms XMB frame costs one pass | deliberate |

`ps2_planes_install()` is step 1a of `ps2_kernel_main()`, immediately after `ps2_sio_init()`
and before the timebase probe, the OHCI probe, the shell loop and the desktop session --
which is why the readers above take the table as given instead of testing it for NULL.  A
missing table is a boot-order bug; self-pacing is the arm64 port's business, not this
one's.  The machine states its own periods as the first row after SIO comes up:

```
[PLANE] input=1000 us  ui=16667 us  panel=200000 us  converge=2000000 us  slices=16
```

With the shipped pointer cap off (`PS2_PTR_MAX_STEP 0`) the product fed to
`dp_ptr_limit()` is zero and a non-positive limit means no limit, so the slicing is the
identity exactly when the limiter is: nothing about the shipped, proportional pointer
changes.  What changes is what `maxstep N` now means -- *N pixels per millisecond* rather
than *N pixels per pass*, which at the measured 259 passes/s is a little under 4x more
travel for the same number, and is the difference the loopback calibrator has to be tuned
against.

#### The measured microsecond (Normative)

**One number per machine, and it is measured.**  A single function converts microseconds
into CP0 `Count` ticks for the whole image -- `btron_ee_ticks_per_us()` in
[`src/drivers/ps2/ps2_irq.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_irq.c)
-- and the boot measures it instead of quoting the core clock.  Four copies of the datasheet
figure used to be in the tree: `EE_TICKS_PER_US` in `core_ps2.c` (`ps2_us_since()`, the
stage split, the pointer's slices, the panel and convergence nets, the task hold), one in
`ps2_irq.c`, `EE_COUNTS_PER_US` in
[`src/drivers/ps2/ps2_usb.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.c)
and another in
[`src/drivers/ps2/ps2_iopram.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_iopram.c).
Each was an independent opinion about what a microsecond is, and on this emulator all four
were wrong by exactly 2x.

**The instrument is the INPUT plane itself.**  Timer0 -- `T0_COUNT 0xB0000000`,
`T0_MODE 0xB0000010`, `T0_COMP 0xB0000020`, all through the uncached KSEG1 alias -- is
programmed `CLKS=2` (bus clock / 256) with `COMP = 576`, which is the plane table's
`input_period_us = 1000` at the documented rate.  It is installed through the ROM kernel's
own interrupt registry rather than by taking the exception vector: `AddIntcHandler`
(syscall `0x10`) for `INTC_TIM0 = 9`, then `EnableIntc` (`0x14`), so the BIOS dispatcher
still owns vectoring.  The mode word the image writes each period is `0x1c2` -- `CLKS=2` |
`ZRET` | `CUE` | `CMPE` | `EQUF|OVFF` ack.  `ZRET` makes the period self-repeating, and
because the compare flag *latches*, the handler has to re-write that same word to acknowledge
it: without the acknowledge the machine delivered exactly one tick ever and then sat there
looking armed.

**The handler is integer-only, by constraint and not by taste.**  No exception vector is
installed, so the ROM kernel saves what it saves -- the GPRs, not the FPRs -- before it
reaches this handler.  With COP1 live ([What COP1 actually is, measured](#what-cop1-actually-is-measured-normative)),
a handler that touched a float would corrupt whatever the interrupted code had in those
registers.  The same reason makes `ps2_ee_exit_handler()` a `sync` plus a hand-written
`Status` read-modify-write: `ei` (`0x41606020`) is not decoded by this PCSX2's R5900 -- it
prints `Unknown R5900 COP0` and does nothing -- so the EE's global enable is bit 16, `EIE`,
and it is raised with `li`/`or`/`mtc0 $12`.

**The calibration law.**  On each entry the handler takes the gap since the previous entry,
and sums the first `CALIB_PERIODS = 32` of them; when the 32nd lands it divides by
`periods x input_period_us` = 32,000 real microseconds, rounding half up, and publishes the
quotient.  The window closes in the handler because only the entries know the spacing; the
ISR budget is re-priced in the same breath, since the periods that filled the window had been
charged against the documented divisor.  Zero is the "not yet measured" sentinel, so a machine
whose `Count` does not advance cannot report a measurement and keeps the fallback
`EE_TICKS_PER_US_DOC = 147`.  `ps2_irq_calib_wait()` bounds its patience in *spins*, not in
microseconds -- a timeout written in the unit being measured would be circular -- and boot
step 2a runs it before the first thing the image times with the result.

| Quantity | Status |
|:---|:---|
| `295` ticks per microsecond on this emulator (PCSX2 on this Mac) | **measured**: 9,437,184 ticks summed over 32 periods |
| `Count` runs at the core rate, 294.912 MHz, not the documented half of it | **measured**, and the mean gap is 294,912 ticks per 1,000 us to the digit |
| The plane and the OHCI agree: 50 Timer0 periods in a window the frame counter also calls 50 frames | **measured** -- two independent hardware clocks; the 147 was the outlier of the three |
| `~147` on a real console, which is why the fallback is 147 | **inferred** from the datasheet; the same code measures it there, so the row will say which it got |

The consequence is that every microsecond this image printed on PCSX2 was 2x too long, and
the accepted scope of the fix says so out loud rather than hiding it: `full_total_us`
225,524 -> 113,318, `full_render_us` 223,885 -> 112,501, `band64_total_us` 17,151 -> 8,618,
`full_upload_us` 1,638 -> 816, `h4_total_us` 552 -> 279.  The gates that read those rows did
not move (same 3 reds, same ratio), because a budget and its measurement halved together.
What did change is the IOP-RAM reset hold, which was written as 20 ms and spent as 10, and
`[BENCH] passes/s`, which divided a tick-unit numerator by a microsecond denominator and
reported 6.8x low.

**Everything stays 32-bit.**  `make test-ps2` TEST 43 forbids a named undefined symbol, so a
`__muldi3` or `__udivdi3` from a stray 64-bit product kills the image at link time.  The
wrap-fold's step is therefore `0xFFFFFFFF / tpu` (about 14.56 ms of `Count` at 295), the
panel and convergence nets fit plainly (`200000 x 295`, `2000000 x 295`), and a task hold
budget in `ms x 1000 x tpu` is capped at 250 ms rather than widened.

The boot prints the whole chain, and each row exists to falsify a different claim:

```
[ASYNC] input plane Timer0 (INTC 9): period=1000 us compare=576 ticks=50 of the 50 this 50000 us window owes -> LIVE
[ASYNC] hid collection: the input plane reads the OHCI rings and a pass delivers them
[ASYNC] machine microsecond = 295 Count ticks, measured over 32 periods of the plane (9437184 ticks summed, mean 294912) -- measured, so every microsecond this image prints is real time
[ASYNC] gaps since the arm: 9881..4118890 ticks = 33..13962 us, against the 1000 us the period means
[ASYNC] the same window on the OHCI's frame counter: 50 frames of the 50 a 1 ms frame says this is -- agree -- the measured divisor and the bus's own frame counter say the same 50 ms
[ASYNC] CP0.Status 0x70030c11 at the arm, 0x70030c11 after it: EIE(bit16)=1 -> 1, IEc(bit0)=1 -> 1
[ASYNC] timer0 MODE=0x000001c2 (clk256+zret+cue+cmpe+ack) count 102 -> 102 over a 2 ms hold, compare=576, ticks=238 total
[ASYNC] isr wcet=498 ticks = 1.68 us of the 50 us budget, overruns=0  ui=16667 us is still the present's own pass, not a plane
[CPU] CP0 Count running  (295 ticks/us, measured against the input plane)
```

`LIVE` is `got + 1 >= owed`, not "at least one tick" -- a plane that delivered 3 of the 50
periods it owes is `STALLED`, and one that delivered none is `DEAD`.  The divisor row prints
the window behind it (periods, ticks summed) because a quotient with no sample cannot be
checked against the plane's own period.  The gaps are printed in raw ticks *and* microseconds
for that reason: the mean gap divided by the divisor *is* the period by construction, so the
µs form of the calibration's own sample would carry no information, and the accessors that
used to offer it (`ps2_irq_gap_min_us()`, `ps2_irq_gap_max_us()`) are gone.  The `wcet` row
is the handler's cost against `ASYNC_ISR_BUDGET_US` (50 us, `ASYNC.txt` Tier-1).

**The one instrument that does not work here, measured.**  A plain read of `T0_COUNT` barely
advances on this emulator: over a hold that is two periods of `COMP=576` -- 1152 counts --
the delta was `140 -> 140`, `276 -> 277`, `551 -> 551`, while the same timer's compare
interrupt arrived 50 times in the same 50 ms.  **Inferred**: PCSX2 keeps the timer's schedule
somewhere other than that word, so the readable counter is a residue; and with `ZRET`, a
sample taken a period apart aliases any phase difference to zero.  The tick count and the
OHCI row are therefore the cadence evidence, and the `MODE` word is the part of that row worth
reading.

**Gates.**  `make test-ps2` TEST 51 asserts every `mtc0` in the image is a `Status` write and
TEST 52 that none names register 9 (`Count` is read, never set -- writing it would break every
elapsed time in the image).  [`scripts/ps2_smooth.sh`](file:///Users/tonpa/depot/bitedits/btron/scripts/ps2_smooth.sh)
takes `ee_ticks_per_us` as `[INFO]` -- it is the machine's business, not the port's -- and
judges `input_plane_short_us` and `ohci_frame_short_us` at a target of 0.

#### The plane as the producer of HID collection (Normative)

The pointer's report is now retired by the interrupt handler and delivered by the pass.  That
split is not a stylistic preference: `ASYNC.txt` §5 forbids "USB progress only in the GUI
loop", and a report read at present time inherits the GUI loop's latency, which is
`ui=16667 us` plus whatever a frame costs.

**What crosses the boundary.**  `src/drivers/ps2/ps2_usb.c` divides its own work in two.
`intr_take()` is the bus half -- read the descriptor's condition code, eight uncached byte
reads, advance the ring index, `ed_unhalt()`, `intr_top_up()` -- and nothing else; it is the
only part that runs on the plane.  Everything with a side effect on the rest of the image
(the event-queue syscalls, the prints, the report processors) stays in the pass.  The handler
therefore touches no heap, renders nothing, and calls no BIOS service that could block, which
is what makes its measured cost mean what it says.

**One queue, one writer per index.**  `hoff_push()` publishes into a 256-slot ring; the plane
owns `s_hoff_in` and the pass owns `s_hoff_out`, and no other party writes either, so there is
no lock and no test-and-set in the handler.  A full queue increments `dropped` rather than
overwriting an unread report.  `hoff_drain()` stamps each slot with the `Count` reading taken
at the handler entry that collected it -- so the hold the pass reports is the hold from the
bus, not from the moment the slot was written.

**The handover happens once, and only on evidence.**  `ps2_usb_input_plane(1)` is called in
the `[ASYNC]` boot block when the plane's own verdict is `LIVE` (`got + 1 >= owed` over the
boot window), before the divisor and `wcet` rows print so that the cost of the collection is
inside the measurement of the handler that performs it.  It sets `s_plane_collects` *before*
installing the hook: the flag is the protocol, and installing a producer that the consumer
does not yet know about would collect into a queue nothing drains.  On `DEAD` or `STALLED` the
pass keeps polling the rings itself -- a queue nobody fills is a dead keyboard, so the fallback
is not optional.  The check for a reclaimed IOP-RAM shadow block also stays on the pass side,
because reading it is a bus access and the handler has a budget.

**Proven, on this machine.**  The handler's cost with the collection inside it is
`isr wcet=498 ticks = 1.68 us`, against `wcet=104 ticks = 0.35 us` with an empty hook -- so
a period of polling four interrupt rings costs about 1.3 µs, 3.4 % of §3.2's 50 µs budget,
with `overruns=0`.  `ASYNC.txt` §10's 2 ms pointer-update gap is the gate on the other side:
`plane_held_max_us` is the longest hold the pass has measured and is judged against 2000.

**What a headless run can and cannot show.**  `scripts/ps2_smooth.sh` moves no host cursor,
so a run legitimately prints `reports=0`.  The instrument that proves the collector runs with
no hand on the mouse is `collected on 2223 periods`: `plane_periods` counts handler entries
that passed the host guard and reached the collector, and a period needs only the clock, while
a report needs motion.  `plane_handover_miss` is judged at 0 for a `LIVE` plane and SKIPped
for any other verdict; `plane_reports_in_pass` must not grow once the plane owns the rings.
Non-zero `plane_reports_collected` and a non-zero `plane_held_max_us` require
`make run-ps2` and a hand on the mouse.

```
[PSTAT] plane: reports=0 on the input plane, 0 in a pass, dropped=0 queue max=0 of 256, collected on 2223 periods
[PSTAT] plane: held max=0 us mean=0 us over 0 delivered -- isr 1.68 us of the 50 us budget, overruns=0
```

### 2.5 SIO0 Interactive Shell Commands

The SIO0 UART console (`115200 8N1`) provides an interactive debugging shell with full ANSI terminal escape sequence handling (`\e[A/B/C/D`, `\e[H`, `\e[F`, `\e[3~`, `\e[5~`, `\e[6~`):

| Command | Action |
|:---|:---|
| `help` | List all available shell commands. |
| `info` | Display hardware specifications, CPU frequency, and memory layout. |
| `apps` | List all available desktop applications. |
| `open <app>` | Launch an application window (`cabinet`, `editor`, `terminal`, `sound`, `chat`, `settings`). |
| `tip [mode]` | Switch TIP/IME mode (`ascii`, `hira`, `kata`, `tibetan`) or cycle if no argument. |
| `tasks` | Dump active RTOS task table and stack pointers. |
| `mem` | Display physical RDRAM, kernel heap (12 MB first-fit pool, now reclaimable) and VRAM memory usage. |
| `desktop` | Force an immediate full-screen redraw of the Workbench desktop. |
| `status` | Show mouse `(x, y)` and active TIP mode. |
| `mouse <x> <y>` | Set absolute mouse cursor coordinates. |
| `move <dx> <dy>` | Displace mouse cursor relative to current position. |
| `click [1\|2]` | Simulate left (1) or right (2) mouse button click. |
| `key <char>` | Inject a single keystroke into the active window. |
| `type <text>` | Inject an entire text string sequentially into the active window. |
| `pad <hex> [lx ly]` | Inject simulated DualShock 2 controller state. |
| `sens [pct]` | Show or set the pointer's px-per-count scale (1..400%, 100 = verbatim). |
| `maxstep [px]` | Show or set the pointer's cap in px per INPUT period -- the period is the [plane table's](#the-plane-table-normative) (0 = no cap). |
| `ptrsrc [emu\|hw\|auto]` | Show, force, or re-detect which pointer source profile is live. |
| `ptrstat <tag>` | Print and reset the counts the bus delivered since the last call. |
| `ptrcal` | Loopback-calibrate the pointer path with synthetic reports; no mouse, no hand. |
| `reboot` | Halt the Emotion Engine. |

#### Pointer instruments

Four commands, four different questions. Running them in the wrong order
measures the wrong thing, so each is stated with what it excludes:

- **`ptrcal`** — *is the mapping correct?* It feeds synthetic reports through the
  driver's own report entry, so a count travels the same road as a count off the
  bus (decoded, scaled, bordered, posted as an event) with the hand out of the
  loop. It turns the cap off for the run and restores it afterwards, because the
  cap is a policy about how fast a person may be moved while `ptrcal` measures how
  a count becomes a pixel; left in place it would report the limiter as a broken
  mapping. Its header prints the live `source=` and `curve=`, because under the
  `hw` profile the gain rows are meant to be non-linear -- that is the accelerator,
  not a fault -- and a `[CAL]` log read without its header says the opposite.
- **`ptrstat`** — *what is actually on the wire?* It counts **every** report, not
  the one-in-sixty-four the `[RAW]` rows print, and sorts them by magnitude.
- **`ptgain`** — *what is a count worth to the hand that sent it?* The only
  instrument here whose denominator is a human movement of known length rather
  than a constant, and therefore the only one that can check `PS2_EMU_POINTER_SCALE`
  instead of assuming it. See "Calibrating against a host movement you already know".
- **`ptrsrc`** — *which of the two stubs below is answering, and why?* See
  "Pointer source profiles".
- **`sens` / `maxstep`** — this port's own policy, applied only once the first two
  agree the path is sound.

`ptrstat` closes the window since its last call, so the first call of a session
reports whatever accumulated during boot: type `ptrstat boot` to discard that
window, then perform a slide, then label the window that contains it. The decisive
protocol is five windows:

| Window | Hand | Decides |
|:---|:---|:---|
| `ptrstat boot` | nothing | clears the warm-up; prints no ratio row |
| `ptrstat slow` | one deliberate slide, desk-middle to desk-edge | distance at low speed |
| `ptrstat fast` | **the same physical slide**, done quickly | ...and at high speed |
| `ptrstat h` | sideways only, either speed | whether the other axis stays at zero |
| `ptrstat w` | Mac cursor, display's left edge to its right edge | the gain against a host distance that needs no measuring -- see "Calibrating against a host movement you already know" |

Read the `vs previous window` row:

- **`x=1.0x`** — the wire carries *distance*. A single guest gain is then correct
  and sufficient, and `sens` is where the feel gets set.
- **`x` appreciably above `1.0x`** — the numbers are already cursor pixels,
  multiplied by speed by something outside this port. No constant can be right for
  both rows. The answer is **not** a cap: `dp_ptr_limit()` spends a bounded distance
  per INPUT period and abandons debt past `DP_PTR_LIMIT_DEBT` steps, which makes where the
  cursor lands a function of hand *speed* rather than hand *distance* -- the same
  non-proportionality, moved. What this port does instead is take the ratio as
  evidence that the wrong source profile is live, and the gain belongs to the layer
  above: macOS's own cursor curve, which is the only multiplier in this chain that
  acts on distance.  (`PointerXScale` would have been the other candidate, and v2.8.2
  source says it is not read for this device.)
- **`still`** — a real mouse that is not moving answers IN tokens with NAKs, not
  with `00 00 00` reports, so a large `still` count says a synthetic source is
  producing reports whether or not anything moved. Cross-check the `e0/e1` error
  column of the `[SCH]` row, which is where NAKs do show up.
- **`x max=`** — a genuine device saturates at `|127|` on a fast slide, that being
  the 8-bit byte's ceiling; a source whose maximum never approaches it across a
  slide as fast as a hand can manage is coalescing motion into something other
  than device counts.
- **`reports -> N/s`** — the bus's effective polling rate, set by the device's
  `bInterval`, *not* by this controller's 1 ms frame. A low-speed device cannot be
  polled faster than every 10 ms (USB 1.1 gives low-speed interrupt pipes a 10 ms
  floor and full-speed HID 1-8 ms), so **100-125/s is what a real PS2 mouse also
  reads at** and is not by itself evidence of an emulator. The 1 kHz figure is the
  frame rate; it bounds the frame engine, not the device.
- **`net` far below `total`** on the `h` window — counts are leaking into the axis
  nobody asked for, which no per-axis synthetic test can see because the synthetic
  sweep never moves both axes the way a hand does.
- **`border:` `discarded` and `onwall`** — what the screen edge did to the same
  window, in guest pixels and per paint pass. This is the row that answers "why is
  the cursor sticky at the edges", and no count row can, because the answer is
  about *position* rather than about the wire. A relative pointer's position is the
  integral of its counts, so pixels the border discards are gone for good while
  pushing further into the wall is free and produces nothing: **an edge is a
  one-way sink**, and leaving it costs exactly as much host travel as entering it
  gave away. Read `discarded` as the share of the hand's distance the wall ate:
  near zero means the cursor merely *rests* at the edge and the stickiness is
  somewhere else; a third or more means the gain is spending the whole 800 px
  canvas faster than the hand can cross the desktop it sits on, which is a
  saturation limit and is lowered with `sens`, not raised.

`ptrcal` drives the same report handler that `ptrstat` counts, so it must not run
inside a measurement window. Every `ptrstat` call resets, so forgetting costs one
keystroke to recover from.

#### Pointer source profiles

A count off this bus is worth a different number of pixels depending on what
produced it, and no single guest constant can be right for both cases -- that is
why one value tuned the pointer for a session and never made it usable. The port
therefore ships **two stubs**, picked from the enumerated device's own USB
identity at boot rather than from where the code happened to be built:

| Profile | Selected when | Gain applied | Curve | Cap |
|:---|:---|:---|:---|:---|
| `emu` | the pointer on the bus answers as PCSX2's HID Mouse, device `0627:0001` (`desc_id == 0x00010627`) | `PS2_EMU_MULT_FP` = **96/256 = 0.375 guest px per count**, and since a count is one host cursor px (see the profile's constants in `core_ps2.c`), 0.375 px per px the hand travelled | none | off |
| `hw` | anything else answers -- a mouse somebody sells | verbatim `256`, modified by `sens` | `dp_ptr_riscos()` at `g_mouse_step_mult`, the same curve the arm64 / Pi 400 port uses | off |

Where 96 comes from is a hand, not arithmetic: at 32/256 a stroke could not cross the
800 px canvas, at 256/256 the cursor sat pinned against a wall, and 96/256 is what has
shipped between those two reports. Those observations were taken against counts, so
they survive the correction above unchanged; what does not survive is reading them as
"3 px per host px", which was eight times too flattering -- the shipped pointer has
been tracking the hand at a third of its travel all along.

That is the half of "not usable" no present could fix, and it is left to the same hand
that set the bounds: `sens 100` is 1:1 and `sens 300` is the 3 px per host px the
constant used to claim, neither needing a rebuild. Trying them is only worth doing now
because the price of a fast stroke changed: a gain reads as too fast when 27 painted
positions a second make it overshoot into a trail, and the rectangle the present makes
now runs 137-267 of them.

Detection runs once at boot after the host engine has enumerated, and again after
`usb probe`; the boot log says which one it picked, in both units:

```
[PS2] Pointer source emu (detected): 96/256 px per count = 0.37 px per host cursor px, no curve, cap off
```

`ptrsrc hw` / `ptrsrc emu` override that for a session, which is the only way to
run the emulator's stub against a hardware-shaped count stream or the reverse
without changing what is plugged in. `ptrsrc auto` hands the decision back to
detection. Switching profiles clears the carried subpixel remainder and any
distance the previous one had not spent, so a change mid-session cannot leave the
old source's half-pixel tail to be paid out by the new one.

### Dialing the emulator's gain without a rebuild

There is one gain constant in this stack: `PS2_EMU_MULT_FP`. `PointerXScale` and
`PointerYScale` look like a second copy of it -- both are in the repo's ini, set to 8 --
but neither name exists anywhere in PCSX2 v2.8.2's source, a USB `Type::Pointer` binding
has no scale factor at all (`InputManager.cpp:1043-1057`), and its callback is handed the
raw delta (`:1398-1403`). Editing them changes nothing, so `sens` is the whole dial and
the guest constant is the whole truth.

The gain has two human reports bounding it, restated here in the unit a hand actually
moved in (the column below multiplied every one of them by a scale of 8 that is never
applied): **0.125 px per host px could not cross the canvas**, **1 px per host px (= a
verbatim count) sat pinned against a wall**. Everything between them is a keystroke:

| `sens` | px per count | guest px per host cursor px |
|:---|:---|:---|
| 25 | 64/256 | 0.25 |
| **38** | **97/256** | **0.38 -- the shipped default** |
| 51 | 130/256 | 0.51 |
| 64 | 163/256 | 0.64 |
| 77 | 197/256 | 0.77 |
| 89 | 227/256 | 0.89 |
| 100 | 256/256 | 1.0 -- the too-fast bound, counts verbatim |

Roughly **one `sens` per hundredth of a guest px per host px**, since a count is a host
px: a dial settled this way ships as `PS2_EMU_MULT_FP = 256 x percent / 100`. `sens`
with no argument prints the current value in both units, and `ptrsrc` on its own does
the same for the live profile's default, so either can be read while the hand is still
moving. The prompt value does not survive a reboot, so what a dial settles belongs in
`PS2_EMU_MULT_FP` afterwards -- the one constant, because nothing in the emulator reads
the two ini keys that used to look like its partner.

### Calibrating against a host movement you already know

`PS2_EMU_POINTER_SCALE == 8` used to be an assumption, and the 2026-09-24 run said it
might be false: across 460 reports the largest count seen on any axis was **27** and the
median was **2**, against a byte ceiling of 127. Eight counts per host pixel would make
that a cursor crawling at under 10 px/s, which no hand does. It is now settled from the
emulator's own source instead of by that inference: one count per host cursor pixel, with
no scale applied to a `Type::Pointer` binding (`InputManager.cpp:1043-1057`,
`DisplayWidget.cpp:321-326`), so the constant reads 1.

What `ptgain` still measures is what the source cannot say: what one of those counts is
worth on this Mac, whose own acceleration curve sits in front of every delta PCSX2 reads
off `QCursor::pos()`. It remains the only instrument here whose denominator comes from a
hand rather than from a file.

1. `ptgain 0` -- clear the counters, which have been running since boot.
2. Park the **Mac** cursor hard against the left edge of the Mac's display, then slide
   it in one unbroken stroke to the right edge. The travel is now exactly the display's
   logical width, which System Settings > Displays states, and it is the one host
   distance that needs no measuring.
3. `ptgain <width>` -- e.g. `ptgain 1440`. Reads out the measured counts-per-host-px
   next to the constant it is checking, and the current gain in that unit.
4. `ptgain <width> <percent>` -- the same sweep again, now setting the gain so the
   cursor moves `percent`/100 guest px per host px. `38` is the shipped default and
   `100` is 1:1; the sweep says which of the two the hand wants.

| measured counts per host px | what it means | what changes |
|:---|:---|:---|
| ~8.00 | `PointerXScale` is applied to this device, as the old comment assumed | would put the constant back to 8. **Ruled out from PCSX2's source, not from a sweep**: no code in v2.8.2 reads those two keys |
| **~1.00** | the scale does **not** reach the `hidmouse` binding | **confirmed**, and already installed: `PS2_EMU_POINTER_SCALE` is 1. Note the shipped gain stayed at 96/256 rather than moving to the 768/256 this row used to prescribe -- the hand's two bounds were taken against counts, so keeping them means keeping 96, and 768 would have been 8x the speed the user has actually been judging |
| anything else | the scale is applied in a unit this port has not identified | that number *is* the constant; put it in `PS2_EMU_POINTER_SCALE` |

`ptgain` takes its counts from the same report handler `ptrstat` tallies, so a
`ptrcal` run between the sweep and the call is in the window and invalidates it --
`ptgain 0` recovers in one keystroke. It warns when the y totals say the sweep was
not straight, and it caps the gain at 1024/256 because `dp_ptr_scale()` truncates a
report at `DP_PTR_MAX_PIXELS`, so above that the top of a fast stroke would be eaten
inside the shaper instead of here where the row says so.

The same window answers the two other open questions without another keystroke.
**`loop:`** is the pass rate, and the pass rate is the pointer's frame rate -- the
cursor can only be moved once per pass, so a low number here is steppiness and no
gain constant will smooth it. **`chain x: ... px/count`** is the mapping measured
end to end, which should read `0.37` at the shipped default, and whose second column
is that number in host cursor pixels once the scale is known -- which it now is, from
the emulator's source rather than from this row, so the row is a check on the guest's
own spending (counts vs `spent`, including border clipping) and no longer a way to
discover the emulator's gain.

### What the 2026-09-24 run already settled

Read out of `pcsx2/PCSX2/logs/emulog.txt` (24 s, 460 mouse reports, `startx`
desktop, gain 96/256):

- **The emulator's `|127|` clamp is not biting.** Max count seen on any axis: 27.
  So the "distance lost at speed on the wire" hypothesis is dead for a normal slide,
  and lowering `PointerXScale` to buy headroom buys nothing. Do not re-open it
  without a `sat=` column that is non-zero.
- **Nothing is being dropped between the bus and the handler.** `[DEV] p=` retired
  reports and the `[RAW]` sequence numbers agree (460 vs `#448` at 1-in-64
  sampling), and `[RING] n=1 max=1` says the ring never even queued up. The ~20-50
  reports/s is what PCSX2 produces, not what this port loses.
- **The counts are small enough that the guest's own step is 3 counts.** At 96/256 a
  1-count report pays out 0 px and holds the remainder in the carry, so with a
  median of 2 the pointer moves in whole pixels only once every couple of reports.
  Distance is not lost -- the carry keeps it -- but it is why slow motion reads as
  reluctance.
- **`[MOVE]` rows are 135 ms apart, and 265 ms later in the run.** That is the
  interval between passes that *moved* the cursor, which is a bound on the loop
  rate and not the rate itself; the `loop:` row above is what settles it. The
  inference drawn from it was correct and became the next task: a pass that moved
  the cursor was calling `workbench_render()` plus a full 800x600 byte-swapping
  `blit_backbuffer_to_ps2fb()`, which is the rect-limited-upload work in
  `doc/md/STABILIZATION.md` and not a pointer constant at all.  The 7 Hz is gone --
  with the present clipped to a rectangle, `startx` plus a moving pointer runs
  137-267 passes a second, priced in the table below.

### Why the cursor lags, and which row says so

A relative pointer's screen position is only ever updated when the screen is
repainted, so **the cursor's frame rate is the paint pass's frame rate** -- 7.4 Hz
at the 135 ms spacing above (that is the 2026-09-24 run; a cursor pass costs 536-1050 us
now, and the table further down is what it buys), and no gain, scale or accelerator
changes it. That is
the whole of "too much latency", and it lives in the compositor rather than in the
USB port.

Two facts bound it without any hand protocol:

- The full-canvas GIF upload costs **1638 us** (`[GS] ... full canvas upload 120000
  QW 1638 us`, with a 16-row console band at 45 us -- linear, ~2.7 us per row).
- Each pass that moves the cursor used to make **three whole-canvas sweeps**: the
  render itself, which rebuilt every window for a change the size of a cursor; the
  byte-swap in `blit_backbuffer_to_ps2fb()`, which touched 480000 words for the same
  reason; and that 1.6 ms upload. So ~133 ms of the pass was CPU work in the first
  two, and nothing in this tree writes the CP0 cache-control registers, which means the
  EE's caches are off and every one of those words is an uncached bus access.  (That
  premise is now an asserted image law rather than a reading of the sources, and the
  byte-swap named above is gone -- §2.1.)

`ptrstat` now prints the split, and `startx` prints the cold one on its own:

```
[PS2] paint cost: render=... us  swap=... us  upload=... us  total=....ms/pass
[PSTAT] paint: n=... us/pass  render max/avg  swap max/avg  upload max/avg
[PSTAT] lat: mouse->on screen avg=.. ms max=.. ms over N moves
```

`lat` is stamped on the report that opens an empty queue and read after the flush, so
it is queueing *plus* the paint -- the latency the eye actually gets, not the one the
`loop:` row only implies. A large average with a small maximum is a uniformly slow
repaint; a large maximum on a small average is one stall somewhere else in the loop.

**Read the ratio between the columns, not the milliseconds.** PCSX2 does not model EE
cycle counts, so absolute timings are the emulator's; the proportion between render,
swap and upload is instruction counts, and that is what picks the fix: `swap`
dominating means the byte-order pass should not exist per frame (render in
GS-native order, or swap only the damaged rectangle) and the caches are the other
order of magnitude; `render` dominating means the damage rectangle is the fix, and
`ps2_gs_upload(x, y, w, h)` already takes one.

It does, and the measured cost of taking it is the reason the present is now clipped
in **both** dimensions rather than to a full-width row band:

| region presented | pixels | us/pass | of which render | swap | upload |
|:---|---:|---:|---:|---:|---:|
| whole canvas 800x600 | 480000 | 265782 | 215049 | 49094 | 1637 |
| one 16-row band, full width | 12800 | 5370 | 4015 | 1309 | 45 |
| 64-row band, full width | 51200 | 20378 | 14964 | 5237 | 175 |
| cursor sprite 16x16 | 256 | 528 | 486 | 29 | 10 |
| 80x16 | 1280 | 1033 | 883 | 133 | 14 |
| 16x80 | 1280 | 1204 | 1006 | 146 | 50 |

The upload column was never the problem -- 45 us for a 16-row band, 1638 us for the
canvas.  `render` and `swap` are, and they scale with **pixels**, not rows, which is
the thing this document used to get backwards: it argued that a narrow region "has to
be uploaded a row at a time" and therefore that bands must be full-width to be cheap.
The per-row GIF setup is real -- 16 rows cost 10 us of upload, 32 rows 20 us, so about
0.6 us a row -- and it is nothing next to the 528 us the same 16x16 region costs to
render and swap.  Clipping columns is what buys the speed, so a cursor pass now
presents the union of the old and new sprite boxes and costs 536-1050 us instead of
5370.  Against the emulator's ~60 reports a second, that is the difference between 27
painted positions per second (one screen update per four hand movements) and 137-267,
which is why the supply finally shows.

Note what a gain cannot fix, and note which of the two candidates is real. The
emulator keeps a cursor delta in a 16.16 accumulator, exchanges it whole at each poll
(`InputManager.cpp:1434`, `:1367`) and truncates the float on the way into the device
(`usb-hid.cpp:731`), so sub-count fractions are dropped at that boundary. The `|127|`
byte ceiling is not one of them: `hid.cpp:632-635` clamps with
`dx = int_clamp(xdx, -127, 127); xdx -= dx;`, so a report's overflow is carried into the
next one and no distance is lost at speed. `ptrstat`'s `sat=` therefore counts a
saturated report, not a missing stroke.

The ceiling that does bite is cadence, not width. New motion is published once per
emulated EE frame (`Counters.cpp:500` VSyncStart -> `PollSources`), so at 60 fps the host
makes at most ~60 distinct reports a second however often OHCI polls at its 1 ms frame,
button-unchanged motion coalesces into a single slot of a 16-slot queue
(`hid.cpp:404-418`), and a full queue drops (`:383-389`). That 60 is the rate the guest's
paint loop has to clear to show every position the hand made, which is why the present's
cost was measured before any gain constant was touched -- the painted-positions figure in
the table above is what says the supply is now the limiter rather than the repaint.

The old arithmetic on those ceilings -- `127 / 8` = 15.9 host px per event before
saturation, and a dead zone under 1/8 of a pixel -- was division by a scale that is never
applied. At one count per host px, `sat=` cannot bite until 127 px of cursor travel
inside one event, and a brisk drag on a Mac reaches that only as a flick, which the
carry-over above spends harmlessly on the next report. So the ini's scale is not a knob
worth reaching for: editing it moves nothing, and `sens` (or `PS2_EMU_MULT_FP`, which is
the same number in the tree) is the whole transfer function. **One constant, one
boundary** -- which is also why editing the ini is *not* a shortcut around `sens`.
The emulator's other pointer sliders (`PointerXSpeed`, `PointerYSpeed`,
`PointerInertia`, `PointerXDeadZone`, `PointerYDeadZone`) are all `0` in this machine's
Mouse Mapping Settings panel, and that turns out not to matter: they are read
(`InputManager.cpp:1645-1653`) and applied (`:1372-1381`) to the *scaled* `value` on the
`ProcessEvent` path -- pad and lightgun bindings -- while the `hidmouse` callback is
handed the unscaled `delta` (`:1398-1403`). No value of any of them can reach this
device, so they are inert rather than neutral, and the only live control on this row is
`[USB1] hidmouse_Pointer = Pointer-0`, which is what registers the callback and makes the
host grab and warp the cursor (`InputManager.cpp:1669`, `:1681`).

#### Which layer owns the pointer

A count means something different at each stage of this chain, and each stage has
its own setting. Measuring one layer's output and correcting it at another is how
this pointer was tuned in circles for a session, so the table below is the thing
`ptrstat` reads against. **Proven** = measured on our own run or read out of the
spec; **inferred** = believed from a secondary source or from a default we have not
confirmed, and therefore still a candidate.

| Layer | Timing / behaviour | The setting that belongs to it | Status |
|:---|:---|:---|:---|
| macOS | pointer acceleration and inertia applied to the host cursor before any byte exists | `defaults write -g com.apple.mouse.scaling -1` disables it | inferred (no raw mode exists in PCSX2 to bypass it) |
| PCSX2 `hidmouse` | one HID event per IN token; publishes byte 0 = 3 buttons + pad, then X, Y, wheel as three 8-bit signed fields; reports **the Mac cursor's own deltas**, not device counts | nothing. `PointerXScale` / `PointerYScale` exist in the **[repo-local ini](#running-with-pcsx2)** and in no v2.8.2 code | **proven from PCSX2 v2.8.2 source**: the callback is handed the raw `delta` (`InputManager.cpp:1398-1403`) and a `Type::Pointer` USB binding carries no scale factor (`:1043-1057`); `int_clamp` carries the `|127|` remainder (`hid.cpp:632-635`); motion is published once per emulated EE frame (`Counters.cpp:500`), so the wire ceiling is **~60 reports/s** |
| OHCI frame | `HcFmInterval = 0x27782EDF` -> 11999 bit times at 12 Mbit/s = **1 ms**, and the periodic list is walked once per frame, so a device polls at 1/2/4/8/16/32 ms | `bInterval` at enumerate time | **proven**: the same word real PS2 firmware programs |
| Real PS2 hardware | one OHCI interface shared with the IOP at IOP physical `0x1F801600`, **Full Speed + Low Speed only**, IOP interrupt 22, DMA structures confined to the IOP's reachable 2 MB; the host stack is an IOP-side module and the EE is a client of it over SIF | nothing in this port | spec-derived (secondary but well-corroborated); note that our probe reading that base only proves PCSX2 decodes it, and "the EE cannot poll it on retail hardware" is the open architectural risk |
| This port | the live **source profile** (above) sets the starting gain and whether a curve runs at all; then `dp_ptr_scale()`'s 8.8 gain (`sens`), then `dp_ptr_limit()` px per **INPUT period** (`maxstep`, off; the period is the plane table's, scaled by the periods a pass actually owes), then the screen borders | `ptrsrc`, `sens`, `maxstep` | **proven** by `ptrcal` |

Two rows in that table are worth dwelling on because they cut opposite ways. The
frame interval is a **match** with real hardware -- the value our boot log measures
is the value Sony's own `usbd` writes -- so the bus row of the table will not
change on a retail console and there is no point tuning for it. The device rate
row is the opposite: 100-125 reports/s is normal there too, so the emulator cannot
be blamed for it, and the only real-hardware divergence that matters for pointer
*feel* is the macOS row above the wire.

### Running with PCSX2

**`make run-ps2` does not use the emulator's own user-level configuration.** It
passes `-datapath <repo>/pcsx2`, so every setting that changes a run -- the
attached `[USBn]` devices, their bindings, the BIOS path -- is
read from **`pcsx2/PCSX2/inis/PCSX2.ini` inside this repository**, and that file
is tracked here deliberately so a run gets these settings rather than whatever the
user-level config has drifted to. Edit that file, not
`~/Library/Application Support/PCSX2/inis/PCSX2.ini`, and edit it with PCSX2
closed: the running process rewrites the file on exit.

One caveat about that file, because it is the reason a pointer session was spent
tuning nothing: it contains `[Pad] PointerXScale=8` and `PointerYScale=8`, and **no
code in v2.8.2 reads either key**, so changing them cannot change this guest.  The
`hidmouse` binding is set by `[USB1] hidmouse_Pointer = Pointer-0`; that line is the
only pointer-related setting in the file with an effect.

PCSX2 supports both direct ELF execution and virtual CD/DVD disc images:

- **Direct ELF (`make run-ps2`)** [Recommended]: Executes `btron-ps2.elf` directly using PCSX2's native Host filesystem (`-elf`).
- **Disc ISO (`make run-ps2 ISO=1`)**: Boots `btron-ps2.iso` via virtual CDVD (`-fastboot`).

```bash
# Build ELF and packaged ISO:
make ps2

# Launch ELF directly in PCSX2:
make run-ps2

# Automated verification suite:
make test-ps2            # static laws on the linked image (54 tests)
make test-ps2-softfloat  # the R5900 soft-float runtime, bit-exact vs the host FPU
make test-gl-math        # the freestanding series branches, per-binade vs the host libm
make test-xmb-atlas      # the shipped icon sheet == what gen_xmb_atlas.c computes (drift gate)
make xmb-atlas           # regenerate src/apps/xmb_atlas.h after changing the geometry
make test-xmb-render     # the real xmb.c on the host: atlas ink counts + PPM frames
make test-xmb-render-tgl # the same xmb.c through backend_tinygl.c -- the PS2 rasterizer's own gate

# Smoothness loop against explicit per-phase budgets (opens XMB in PCSX2):
SMOOTH_APP=1 SMOOTH_FRAMES=3 bash scripts/ps2_smooth.sh
```

The first rows that loop prints are the periods the guest installed, read off its own
`[PLANE]` line, and the last two of them are distances rather than timings: a non-zero
`plane_input_drift_us` or `plane_ui_drift_us` means the budgets underneath are pricing a
clock the machine does not have, and a SKIP means the table was never installed -- which
is a boot-order bug, not a missing measurement.  The three rows after them are the input
plane's own evidence: `ee_ticks_per_us` is printed as `[INFO]` because the divisor is the
machine's business rather than the port's, while `input_plane_short_us` and
`ohci_frame_short_us` are judged at 0 -- periods the plane owed its boot window and did not
deliver, and the same window short against the OHCI's own 1 ms frames.  A non-zero shortfall
there says the plane is not running at the rate the table installed, which is a different
fault from a wrong divisor and cannot be told apart in a tick count.  The rows after those
judge the plane as a worker rather than as a clock: `plane_handover_miss` at 0 (a `LIVE` plane
that never took the HID rings), `plane_dropped` at 0, `plane_held_max_us` against §10's 2 ms,
`isr_overruns` at 0, and `plane_periods_collected` / `isr_wcet_ticks` as `[INFO]`.  See
[The plane table](#the-plane-table-normative),
[The measured microsecond](#the-measured-microsecond-normative) and
[The plane as the producer of HID collection](#the-plane-as-the-producer-of-hid-collection-normative).

Neither `make ps2` nor `make ps2-bench` rebuilds an object because a *flag* changed --
dependency tracking only sees sources -- so changing `PS2_CC` needs `find src -name '*.ps2.o'
-delete` first.  Tests 36 and 39 are what catch a stale object left behind by that.

## 3. Target 9: Bare-Metal MIPS (`make run-mips`)

### Hardware Overview

B-System supports two QEMU MIPS platforms:

1. **MIPS Malta Core LV** (Default):
   - QEMU target: `qemu-system-mipsel -M malta -cpu 24Kf`
   - Memory: 256 MB RAM mapped into KSEG0 at `0x80000000`.
   - Entry point: `0x80100000`.
   - Console: Standard NS16550 UART at ISA COM1 physical `0x180003F8` / KSEG1 `0xB80003F8` (115200 8N1).

2. **MIPS Magnum R4000** (Legacy Jazz Workstation):
   - QEMU target: `qemu-system-mips64el -M magnum -bios ./ntprom.raw -m 64M`
   - Requires `./ntprom.raw` ARC firmware image in the repository root.

```bash
# Build MIPS kernel ELF:
make mips

# Run interactive serial console on QEMU Malta:
make run-mips

# Automated headless regression test:
make test-mips
```

## 4. Cross-Compilation Toolchain

Both targets compile natively without external GCC toolchains using LLVM/Clang and GNU Binutils on macOS:

1. **C Compiler (LLVM Clang)**:
   ```bash
   /opt/homebrew/opt/llvm/bin/clang --target=mipsel-unknown-elf -march=mips2 -mabi=32 -ffreestanding -nostdlib
   ```
   - Target: `mipsel-unknown-elf` (MIPS 32-bit little-endian).
   - CPU Architecture: `-march=mips2` (a strict subset of the Emotion Engine R5900's MIPS III, and the one ISA that keeps the whole image on a single O32 calling convention -- see "One Calling Convention" in section 1; zero invalid MIPS32r2 opcodes).
   - ABI: `-mabi=32` (standard MIPS o32 ABI).
   - Exceptions to the flag, both narrow: `src/drivers/ps2/ps2_gs_reg.s` re-enables 64-bit
     GPR instructions locally for the GS's single-`sd` register latch, and `boot_ps2.s`
     wraps its `wait` in `.set mips3`.

2. **Linker**:
   ```bash
   mipsel-linux-gnu-ld -T <script.ld> <objects...> -o <target.elf>
   ```

## 5. Environment Tuning & Running in QEMU Window (No PCSX2 .elf Association)

### 5.1 Decoupling PCSX2 from the macOS `.elf` File Association

#### Root Cause of the Association

When PCSX2 is installed on macOS (`/Applications/PCSX2.app`), its bundle `Info.plist` declares itself as the default operating-system handler for the `.elf` extension:

```xml
<key>CFBundleTypeName</key>
<string>PS2 Homebrew Application</string>
<key>CFBundleTypeIconFile</key>
<string>PCSX2.icns</string>
<key>public.filename-extension</key>
<array>
    <string>elf</string>
</array>
```

Whenever macOS LaunchServices executes `open -a /Applications/PCSX2.app <file.elf>`, Finder and LaunchServices register PCSX2 as the primary handler for all `.elf` files across the entire system. This causes other native or embedded ELF files (like `btron-foma.elf`) to display the PCSX2 icon and trigger PCSX2 on double-click.

#### How B-System Prevents This

1. **Booting Disc ISO by Default**: `make run-ps2` boots `btron-ps2.iso` by default rather than raw `.elf`. Disc images are treated as standard CDVD media, keeping the `.elf` extension completely detached from LaunchServices.
2. **Direct CLI Binary Execution**: The Makefile executes `/Applications/PCSX2.app/Contents/MacOS/PCSX2` directly via CLI rather than using macOS `open -a`, avoiding LaunchServices file-type registration.

#### How to Reset & Clean macOS `.elf` File Associations

If macOS is already showing PCSX2 icons for all `.elf` files or attempting to open them in PCSX2:

```bash
/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister -r -domain local -domain system -domain user
killall Finder
brew install duti
duti -s com.apple.TextEdit .elf all
```

### 5.2 Running in a QEMU Window (No PCSX2 Required)

If you prefer not to use PCSX2, you can run the MIPS architecture inside a native **QEMU Cocoa graphical window**:

```bash
make run-ps2
make run-mips
```

#### QEMU Window Controls & Features

- **Native Graphical Display**: Spawns a Cocoa window running MIPS Malta Core LV (Little-Endian MIPS-III/32r2).
- **Mouse Pointer Release (`Ctrl+Alt+G` / `^G`)**: Press **`Ctrl+Alt+G`** inside the QEMU window to release the captured mouse pointer back to macOS.
- **Interactive Serial Console**: Terminal input/output remains connected to the UART COM1 serial console (`-serial stdio`) while the graphical display is active.
- **Headless Mode**: Run `make run-mips GUI=0` or `make test-mips` for fast, non-graphical CI/testing.

### 5.3 PCSX2 Performance & Display Tuning for B-System

When running under PCSX2 (`make run-ps2`):

1. **Fast Boot (`-fastboot`)**:
   - Enabled by default in the Makefile. Skips the Sony 7-sound intro animation and boots straight into the B-System kernel.

2. **Graphics Renderer Selection**:
   - In PCSX2 menu $\rightarrow$ **Settings** $\rightarrow$ **Graphics** $\rightarrow$ **Rendering**:
     - **Metal** (Recommended for macOS Apple Silicon): Lowest latency and native host acceleration for GIF DMA Host-to-Local transfers.
     - **Vulkan / Software**: Useful for verifying pixel-exact GS eDRAM swizzling without host GPU filtering.

3. **Display & VSync Settings**:
   - Video timing in `ps2_gs.c` is configured for 60Hz NTSC progressive scan. If you observe frame cadence issues, ensure PCSX2 Frame Limiting is set to **Normal (100%)**.

4. **Input Configuration**:
   - **DualShock 2 Pad**: Analog stick controls pointer velocity, Cross (X) clicks, Circle/Square trigger BTRON menus.
   - **USB Keyboard & Mouse**: Under PCSX2 **Settings** $\rightarrow$ **Controllers** $\rightarrow$ **USB Settings**, configure Port 1 as standard HID Mouse and Port 2 as standard HID Keyboard for full BTRON mouse/keyboard operation.

## 6. Developer Quick Reference

| Command | Action | Platform / Output |
|:---|:---|:---|
| `make ps2` | Build PS2 ELF & Disc ISO | `btron-ps2.elf` and `btron-ps2.iso` |
| `make run-ps2` | Launch Disc ISO in PCSX2 | Bootable CDVD execution (No `.elf` association) |
| `make test-ps2` | Run PS2 automated test | 54/54 driver, ELF, ABI and clock assertions |
| `make mips` | Build MIPS ELF | `btron-mips.elf` |
| `make run-mips` | Launch in QEMU Window | Interactive console & display on Malta |
| `make test-mips` | Run MIPS automated test | Headless validation of all 8 kernel boot markers |
| `make test` | Run full test suite | Validates all B-System test suites (100% pass) |
| `make clean` | Clean all outputs | Removes all `.elf`, `.iso`, `.o`, and test binaries |

# Credits

* Namdak Tonpa and Grok 4.5
