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
| Where does it disagree? | `add.s`, `sub.s`, `mul.s` only; `div.s`, `sqrt.s`, the five compares and `cvt.s.w` agree on every clean pair |
| How far? | Exactly one representable step, never wider, never across zero -- 61 of the 576 clean arithmetic pair-runs (each class runs 144) |
| Which engine is right? | The bit engine: it is host-proven exact, so each of those 61 is COP1 answering the neighbour that is *not* correctly rounded |
| Is it a rounding mode? | No.  Those classes make a rounding decision on 60/58/42 of their clean pairs and COP1 is wrong on 28/14/19 of them, while `div.s` -- which decides on 81, more than any other op -- is wrong on none.  A mode cannot skip the op with the most decisions to make |
| Does it flush subnormals differently? | No: that count is zero |

So the gates on the arithmetic are `fpu_gap_wide == 0` and `fpu_gap_sign == 0`, not
"agrees with the bit engine": a one-step gap is invisible in an 8-bit pixel, and a demand
that `add.s` be IEEE-exact is a demand that this CPU be a different CPU.  `fpu_other` is
reported as information for the same reason.

What this does *not* buy: the XMB's icon bake and frame draw spend their time in
`include/gl/math.h`'s double-precision series, which no Emotion Engine FPU can accelerate
because the machine has no `.d` instructions to accelerate it with.  The 5x is in routing
that work to single precision, not in the switch above.

One thing no measurement in this tree can settle: every `[FPU]` number here came from
PCSX2's Emotion Engine.  The same rows print over SIO on a real console, so the identical
image run there says whether the one-step residue is the emulator or the silicon.

### Driver Files

| File | Role |
|:---|:---|
| [`src/drivers/ps2/boot_ps2.s`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/boot_ps2.s) | EE reset vector, `$gp` and `$sp` setup, unrolled BSS wipe, BIOS syscall wrappers (`SetGsCrt`, `PutIMR`), jumps to `ps2_kernel_main`. |
| [`src/drivers/ps2/ps2.ld`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2.ld) | Memory layout script linking `.text` at `0x00100000`, and the `ASSERT` that fails the link when `_end` reaches the stack or the 32 MB RDRAM end. |
| [`src/drivers/ps2/ps2_gs_reg.s`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs_reg.s) | The image's only 64-bit-GPR instructions: `ps2_gs_poke64()`/`ps2_gs_peek64()` perform the GS's single-`sd`/single-`ld` register latch. Assembled with local `.set mips3` under the shared `-march=mips2` driver; its arguments and return halves are 32-bit, so it is O32-legal on both sides. |
| [`src/drivers/ps2/ps2_builtins.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_builtins.c) | The `__floatdidf` / `__fixdfdi` O32 libcalls clang emits for `double ↔ int64` at `-march=mips2` and `-nostdlib` leaves undefined, written from 32-bit pieces because the EE FPU only speaks 32-bit GPRs. |
| [`src/drivers/ps2/ps2_gs.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.h) | Privileged GS registers (`0x12000000`: `PMODE`, `SMODE2`, `DISPFB1`, `DISPLAY1`, `CSR`) and GIF DMA registers (`0x1000A000`), reached only through `GS_REG_POKE`/`GS_REG_PEEK`. |
| [`src/drivers/ps2/ps2_gs.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.c) | Hardware display initialization, VSync synchronization (`ps2_gs_vsync`), Host-to-Local GIF DMA blitter (`ps2_gs_flush`), uncached KSEG1 RDRAM framebuffer, and non-destructive cursor restoration. |
| [`src/drivers/ps2/ps2_font.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_font.h) | 8x8 ASCII bitmap font table (128 characters) for crisp, authentic BTRON UI text rendering. |
| [`src/drivers/ps2/ps2_sio.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.c) | Cleanroom SIO0 hardware UART driver (`0x1000F180` / `KPUTCHAR`). |
| [`src/drivers/ps2/ps2_pad.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.c) | Cleanroom DualShock 2 controller driver: analog stick velocity integration, deadband filtering, button edge detection, and event mapping. |
| [`src/drivers/ps2/ps2_usb.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.c) | Cleanroom USB Open Host Controller Interface (OHCI) driver (`0xBF801600`) and standard USB HID Boot Protocol keyboard/mouse decoders. |
| [`src/cores/core_ps2.c`](file:///Users/tonpa/depot/bitedits/btron/src/cores/core_ps2.c) | Platform core adapter: multi-window application suite (Workbench, B-Editor, TAD Cabinet, Settings), Japanese TIP/IME status badge, interactive SIO0 shell, event queue, and RTOS heartbeat. |
| [`scripts/test_ps2.sh`](file:///Users/tonpa/depot/bitedits/btron/scripts/test_ps2.sh) | Automated verification suite checking ELF architecture, entry point, driver symbols, R5900 opcodes, the one-calling-convention ABI law (image *and* every object at 32-bit GPRs, the GS accessors at one `sd`/one `ld`), the FPU law (COP1 instructions in `ps2_builtins.ps2.o` and in no other object, no double-width op and no EE-illegal `.s` encoding either by mnemonic or by byte scan, the hardware path provably compiled in, no named undefined symbol, all 32 soft-float libcalls defined) and the ISO image (45/45 tests). |

### 2.1 Graphics Synthesizer (GS) Framebuffer Architecture

The PlayStation 2 Graphics Synthesizer contains 4 MB of ultra-high-bandwidth embedded DRAM (eDRAM). Memory inside eDRAM is structured into **pages** (2048 32-bit words = 8192 bytes) and **blocks** (64 words = 256 bytes) with non-linear column-interleaved pixel swizzling.

#### The Host-to-Local GIF DMA Blitter Architecture

Rather than rendering individual primitives through the GS rasterizer (which is prone to sub-pixel rounding errors, page-boundary clipping bugs, and high DMA packet overhead), B-System uses the hardware **Host-to-Local Blitter**:

1. **Uncached RDRAM Backing Store (`ps2_fb_memory`)**:
   - The CPU maintains a linear 640 x 448 x 32-bpp frame buffer in RDRAM (`71,680` quadwords = `1,146,880` bytes).
   - All drawing functions (`ps2_gs_draw_rect`, `ps2_draw_char`, `ps2_gs_putpixel`) write through an uncached KSEG1 pointer (`0xA0xxxxxx`), bypassing the EE L1 Data Cache so RDRAM is always 100% coherent before DMA transfers.

2. **GS Transmission Setup Packet (`ps2_gs_flush`)**:
   - A 5-QW setup packet primes the GS:
     - `BITBLTBUF (0x50)`: `DBA = 0`, `DBW = 10` (640 / 64), `DPSM = GS_PSM_CT32 (0)`.
     - `TRXPOS (0x51)`: `DSAX = 0, DSAY = 0`.
     - `TRXREG (0x52)`: `RRW = 640, RRH = 448`.
     - `TRXDIR (0x53)`: `0` (Host $\rightarrow$ Local eDRAM).

3. **Hardware Swizzling on Arrival**:
   - `ps2_fb_memory` is streamed via DMAC Channel 2 in 8 chunks of 8,960 quadwords with `GIFTAG(FLG = IMAGE)`.
   - The GS internal blitter automatically and correctly swizzles linear RDRAM pixels into eDRAM pages and blocks on arrival.

4. **Verified NTSC Video Timings**:
   - `ps2_set_gs_crt(1, 2, 0)`: Interlaced, NTSC, Field mode.
   - `PMODE = 0xFF63ULL`: Circuit 1 + Circuit 2 enabled, `SLBG = 0` (Framebuffer output selected).
   - `SMODE2 = 0x01ULL`: Interlaced, Field mode.
   - `DISPFB1` & `DISPFB2 = 0x1400ULL`: Base address page 0 (`FBP = 0`), buffer width 10 blocks (`FBW = 10`), 32-bit RGBA (`PSM = 0`).
   - `DISPLAY1` & `DISPLAY2 = 0x001bf9ff0983227cULL`: $DX=636$, $DY=50$, $MAGH=3$ (4x), $MAGV=1$ (2x), $DW=2559$, $DH=447$.

5. **Non-Destructive Cursor Restoration**:
   - Mouse cursor drawing backs up background pixels into `cursor_saved[16 * 16]`. Erasing the cursor restores the exact original background pixels with zero smearing or artifacts.

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
- **One XMB frame costs ~78 s of emulator time** (`[BENCH] xmb frame 1/2/3` on emulator
  timestamps 71.1, 149.1 and 227.1 s for a Cross press at 5.6 s).  No guest-side clock in
  this port can time a single one of those frames: CP0 Count is 32-bit at 147.18 MHz, so a
  plain delta wraps every 29.1 s, and the fold in `btron_render_perf_us()` only recovers a
  wrap if something polls it inside the interval -- which a frame that blocks in the
  rasterizer does not.  The heartbeat therefore prints `... ms folded` and the app phase's
  rate prints `<= N passes/s`: both are monotonic upper bounds, and the wall cadence is read
  from the emulator's own prefix on each line.  This is the number task 20 opens with.

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

6. **Double-Buffered GIF DMA Blitter**:
   - Renders directly to a 32-bit ARGB backbuffer (`s_desktop_backbuffer`).
   - `blit_backbuffer_rect_to_ps2fb()` translates ARGB to native GS CT32 RGBA
     little-endian format **for the damaged rectangle only**, and `ps2_gs_upload(x, y,
     w, h)` streams that rectangle to the GS's local framebuffer over DMAC channel 2.
     The rectangle comes from a set of up to `PS2_BAND_MAX` merged boxes (y-touching
     damage unions its x span; overflow falls back to the whole canvas), and it is a
     *box* rather than a full-width row band because the render and byte-swap passes
     cost ~0.3 us per pixel: 16 rows at full width is 5370 us, the same 16 rows
     clipped to the cursor sprite's own 16 columns is 528 us.  See
     [Why the cursor lags](#why-the-cursor-lags-and-which-row-says-so) for the table.
   - The full-canvas path (`blit_backbuffer_to_ps2fb()`, 480000 px, 265.8 ms) still
     exists and is what the Stage 1 console and the cold `startx` paint use; it is no
     longer what a moving pointer costs.

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
| `maxstep [px]` | Show or set the pointer's cap in px per paint (0 = no cap). |
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
  per paint and abandons debt past `DP_PTR_LIMIT_DEBT` steps, which makes where the
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
  two, and nothing in this tree ever writes CP0 `Config`, which means the EE's
  caches are off and every one of those words is an uncached bus access.

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
| This port | the live **source profile** (above) sets the starting gain and whether a curve runs at all; then `dp_ptr_scale()`'s 8.8 gain (`sens`), then `dp_ptr_limit()` px-per-paint (`maxstep`, off), then the screen borders | `ptrsrc`, `sens`, `maxstep` | **proven** by `ptrcal` |

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
make test-ps2            # static laws on the linked image (42 tests)
make test-ps2-softfloat  # the R5900 soft-float runtime, bit-exact vs the host FPU
```

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
| `make test-ps2` | Run PS2 automated test | 39/39 driver, ELF and ABI assertions |
| `make mips` | Build MIPS ELF | `btron-mips.elf` |
| `make run-mips` | Launch in QEMU Window | Interactive console & display on Malta |
| `make test-mips` | Run MIPS automated test | Headless validation of all 8 kernel boot markers |
| `make test` | Run full test suite | Validates all B-System test suites (100% pass) |
| `make clean` | Clean all outputs | Removes all `.elf`, `.iso`, `.o`, and test binaries |

# Credits

* Namdak Tonpa and Grok 4.5
