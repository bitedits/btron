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

### Driver Files

| File | Role |
|:---|:---|
| [`src/drivers/ps2/boot_ps2.s`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/boot_ps2.s) | EE reset vector, `$gp` and `$sp` setup, unrolled BSS wipe, BIOS syscall wrappers (`SetGsCrt`, `PutIMR`), jumps to `ps2_kernel_main`. |
| [`src/drivers/ps2/ps2.ld`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2.ld) | Memory layout script linking `.text` at `0x00100000`. |
| [`src/drivers/ps2/ps2_gs.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.h) | Privileged GS registers (`0x12000000`: `PMODE`, `SMODE2`, `DISPFB1`, `DISPLAY1`, `CSR`) and GIF DMA registers (`0x1000A000`). |
| [`src/drivers/ps2/ps2_gs.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_gs.c) | Hardware display initialization, VSync synchronization (`ps2_gs_vsync`), Host-to-Local GIF DMA blitter (`ps2_gs_flush`), uncached KSEG1 RDRAM framebuffer, and non-destructive cursor restoration. |
| [`src/drivers/ps2/ps2_font.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_font.h) | 8x8 ASCII bitmap font table (128 characters) for crisp, authentic BTRON UI text rendering. |
| [`src/drivers/ps2/ps2_sio.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_sio.c) | Cleanroom SIO0 hardware UART driver (`0x1000F180` / `KPUTCHAR`). |
| [`src/drivers/ps2/ps2_pad.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_pad.c) | Cleanroom DualShock 2 controller driver: analog stick velocity integration, deadband filtering, button edge detection, and event mapping. |
| [`src/drivers/ps2/ps2_usb.h`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.h) & [`.c`](file:///Users/tonpa/depot/bitedits/btron/src/drivers/ps2/ps2_usb.c) | Cleanroom USB Open Host Controller Interface (OHCI) driver (`0xBF801600`) and standard USB HID Boot Protocol keyboard/mouse decoders. |
| [`src/cores/core_ps2.c`](file:///Users/tonpa/depot/bitedits/btron/src/cores/core_ps2.c) | Platform core adapter: multi-window application suite (Workbench, B-Editor, TAD Cabinet, Settings), Japanese TIP/IME status badge, interactive SIO0 shell, event queue, and RTOS heartbeat. |
| [`scripts/test_ps2.sh`](file:///Users/tonpa/depot/bitedits/btron/scripts/test_ps2.sh) | Automated verification suite checking ELF architecture, entry point, driver symbols, R5900 opcodes, and ISO image (19/19 tests). |

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
| **Cross (✕)** | Left Mouse Click | Selects icons, clicks buttons, and focuses windows. |
| **Square (□)** | Right Mouse Click | Activates context menus and property dialogs. |
| **Circle (◯)** | Cancel / ESC | Dismisses active dropdown menus and dialogs. |
| **Triangle (△)** | Cycle TIP / IME Mode | Rotates language input: ASCII → Hiragana (`あ`) → Katakana (`ア`) → Tibetan (`བོད`). |
| **Start** | Toggle Desktop Menu | Opens or closes the top system menu bar. |
| **Select / L1 / R1** | Cycle Active Window | Shifts focus between open desktop windows. |
| **D-Pad (Up, Down, Left, Right)** | Discrete Navigation Keys | Injects `BTRON_KEY_UP/DOWN/LEFT/RIGHT` events into the event queue. |

Developers can simulate controller state via the serial shell using:

```bash
pad <btns_hex> [lx ly]
# Example: Click Cross button with centered sticks:
btron-ps2> pad bfff 128 128
```

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

5. **Real BTRON Applications**:
   - VObject Manager (`src/apps/vobj_manager.c`), T-Editor (`src/apps/t_editor.c`), GTerm (`src/apps/gterm.c`), and Control Panel (`src/settings/control_panel.c`).

6. **Double-Buffered GIF DMA Blitter**:
   - Renders directly to a 32-bit ARGB backbuffer (`s_desktop_backbuffer`).
   - `blit_backbuffer_to_ps2fb()` translates ARGB to native GS CT32 RGBA little-endian format.
   - `ps2_gs_flush()` streams the full 640x448 display to GS 4MB eDRAM via DMAC Channel 2.

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
| `mem` | Display physical RDRAM, kernel heap (8 MB), and VRAM memory usage. |
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
  above (`macOS` acceleration, then `PointerXScale`).
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
| `emu` | the pointer on the bus answers as PCSX2's HID Mouse, device `0627:0001` (`desc_id == 0x00010627`) | `256 * 3 / 8 = 96` per count: the emulator's own `PointerXScale` divided out, then **3 guest px per host cursor px** | none | off |
| `hw` | anything else answers -- a mouse somebody sells | verbatim `256`, modified by `sens` | `dp_ptr_riscos()` at `g_mouse_step_mult`, the same curve the arm64 / Pi 400 port uses | off |

Why the emulator stub sits *above* 1:1, when the temptation with an already-
accelerated source is to slow it down: this source is bounded in a way a mouse on a
desk is not. The host cursor stops at the edge of the Mac's display, so a 1:1
mapping can never carry the guest cursor further than the desktop it sits on, and
one stroke cannot cross an 800 px canvas. A real mouse has unlimited travel and
only ever needs slowing. Both stay a single constant, so where the cursor lands
still depends only on how far the hand moved -- which is what separates this from
the velocity cap, whose per-paint bound made the destination depend on *speed*.

Detection runs once at boot after the host engine has enumerated, and again after
`usb probe`; the boot log says which one it picked, in both units:

```
[PS2] Pointer source emu (detected): 96/256 px per count = 3 px per host cursor px, no curve, cap off
```

`ptrsrc hw` / `ptrsrc emu` override that for a session, which is the only way to
run the emulator's stub against a hardware-shaped count stream or the reverse
without changing what is plugged in. `ptrsrc auto` hands the decision back to
detection. Switching profiles clears the carried subpixel remainder and any
distance the previous one had not spent, so a change mid-session cannot leave the
old source's half-pixel tail to be paid out by the new one.

### Dialing the emulator's gain without a rebuild

`PS2_EMU_POINTER_SCALE` (8) and `PointerXScale` in the emulator's ini are two
copies of one number, and the second is outside this repository -- so before
tuning anything, check they agree. The gain has two human reports bounding it:
**1 px per host px could not cross the canvas**, **8 px per host px (= a verbatim
count) sat pinned against a wall**. Everything between them is a keystroke:

| `sens` | px per count | guest px per host cursor px |
|:---|:---|:---|
| 25 | 64/256 | 2.0 |
| **38** | **97/256** | **3.0 -- the shipped default** |
| 51 | 130/256 | 4.0 |
| 64 | 163/256 | 5.0 |
| 77 | 197/256 | 6.1 |
| 89 | 227/256 | 7.1 |
| 100 | 256/256 | 8.0 -- the too-fast bound, counts verbatim |

Roughly **13 `sens` per guest px per host px**. `sens` with no argument prints the
current value in both units, and `ptrsrc` on its own does the same for the live
profile's default, so either can be read while the hand is still moving. A dial
settled this way belongs in `PS2_EMU_GUEST_PX_PER_HOST_PX` afterwards -- the prompt
value does not survive a reboot, and the two units are printed on every row so a
session is never ambiguous about which one was set.

### Calibrating against a host movement you already know

Everything above assumes `PS2_EMU_POINTER_SCALE == 8`. That assumption is load
bearing -- it is the difference between a pointer that is 3 times too slow and one
that is right -- and the 2026-09-24 run says it may be false: across 460 reports the
largest count seen on any axis was **27** and the median was **2**, against a byte
ceiling of 127. Eight counts per host pixel would make that a cursor crawling at
under 10 px/s, which no hand does. So check it rather than carry it, with a movement
whose length is not in doubt. **`ptgain`** is that check, and it is the only
instrument here whose denominator comes from a hand rather than from a file.

1. `ptgain 0` -- clear the counters, which have been running since boot.
2. Park the **Mac** cursor hard against the left edge of the Mac's display, then slide
   it in one unbroken stroke to the right edge. The travel is now exactly the display's
   logical width, which System Settings > Displays states, and it is the one host
   distance that needs no measuring.
3. `ptgain <width>` -- e.g. `ptgain 1440`. Reads out the measured counts-per-host-px
   next to the constant it is checking, and the current gain in that unit.
4. `ptgain <width> <percent>` -- the same sweep again, now setting the gain so the
   cursor moves `percent`/100 guest px per host px. `300` is the shipped target.

| measured counts per host px | what it means | what changes |
|:---|:---|:---|
| ~8.00 | `PointerXScale` is applied to this device, as assumed | nothing; `sens` is the whole dial |
| ~1.00 | the scale does **not** reach the `hidmouse` binding | `PS2_EMU_POINTER_SCALE` to 1; the gain `ptgain` just installed is the same 3 px per host px, so the shipped default becomes 768/256 |
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
end to end, which should read `0.37` at the shipped default; if `counts` came out
8 times smaller than expected, this column goes up 8 times with no change to the
code, and that is the whole of "not close enough".

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
  rate and not the rate itself; the `loop:` row above is what settles it. If it
  comes back near 7 Hz the binding constraint is the repaint, not the pointer: a
  pass that moves the cursor calls `workbench_render()` plus a full 800x600
  byte-swapping `blit_backbuffer_to_ps2fb()`, which is the rect-limited-upload work
  in `doc/md/STABILIZATION.md`, and not a pointer constant at all.

Note what a gain cannot fix: the emulator truncates its float delta toward zero
and clamps each event at `|127|`, so distance is lost at both ends of the speed
range before the byte is on the wire. `ptrstat`'s `sat=` and the zero bucket of
its histogram are what those two look like from the guest.

The two ceilings sit at numbers worth writing down, because they are the reason
`PointerXScale` is not free to leave at 8. With that scale, one event can only
carry `127 / 8` = **15.9 host cursor px** before it saturates, so any brisk drag
loses distance on the wire no guest constant can get back; the same scale makes
the dead zone anything under `1/8` of a host px, which is harmless. Lowering the
ini's scale and raising `PS2_EMU_GUEST_PX_PER_HOST_PX` by the same factor keeps
the travel identical while moving both ceilings -- `sat=` at scale 1 does not
bite until 127 px in one event. **The two numbers are one knob split across a
boundary**, which is also why editing the ini is *not* a shortcut around `sens`.
The emulator's other pointer sliders (`PointerXSpeed`, `PointerYSpeed`,
`PointerInertia`, `PointerXDeadZone`, `PointerYDeadZone`) are all `0` in this
machine's Mouse Mapping Settings panel, i.e. no curve, no smoothing and no dead
zone in the chain; what each one means numerically is still **unverified**, and
they are deliberately left at zero rather than reasoned about.

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
| PCSX2 `hidmouse` | one HID event per IN token; publishes byte 0 = 3 buttons + pad, then X, Y, wheel as three 8-bit signed fields; reports **the Mac cursor's own deltas**, not device counts | `PointerXScale` / `PointerYScale` in the **[repo-local ini](#running-with-pcsx2)**, both present and **= 8** | **proven** accelerated-cursor source; **proven** gain of 8 in the config this repo runs |
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
attached `[USBn]` devices, their bindings, `PointerXScale`, the BIOS path -- is
read from **`pcsx2/PCSX2/inis/PCSX2.ini` inside this repository**, and that file
is tracked here deliberately so a run gets these settings rather than whatever the
user-level config has drifted to. Edit that file, not
`~/Library/Application Support/PCSX2/inis/PCSX2.ini`, and edit it with PCSX2
closed: the running process rewrites the file on exit.

PCSX2 supports both direct ELF execution and virtual CD/DVD disc images:

- **Direct ELF (`make run-ps2`)** [Recommended]: Executes `btron-ps2.elf` directly using PCSX2's native Host filesystem (`-elf`).
- **Disc ISO (`make run-ps2 ISO=1`)**: Boots `btron-ps2.iso` via virtual CDVD (`-fastboot`).

```bash
# Build ELF and packaged ISO:
make ps2

# Launch ELF directly in PCSX2:
make run-ps2

# Automated verification suite:
make test-ps2
```

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
   /opt/homebrew/opt/llvm/bin/clang --target=mipsel-unknown-elf -march=mips3 -mabi=32 -ffreestanding -nostdlib
   ```
   - Target: `mipsel-unknown-elf` (MIPS 32-bit little-endian).
   - CPU Architecture: `-march=mips3` (Emotion Engine R5900 compatible, zero invalid MIPS32r2 opcodes).
   - ABI: `-mabi=32` (standard MIPS o32 ABI).

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
| `make test-ps2` | Run PS2 automated test | 35/35 driver and ELF assertions |
| `make mips` | Build MIPS ELF | `btron-mips.elf` |
| `make run-mips` | Launch in QEMU Window | Interactive console & display on Malta |
| `make test-mips` | Run MIPS automated test | Headless validation of all 8 kernel boot markers |
| `make test` | Run full test suite | Validates all B-System test suites (100% pass) |
| `make clean` | Clean all outputs | Removes all `.elf`, `.iso`, `.o`, and test binaries |

# Credits

* Namdak Tonpa and Grok 4.5
